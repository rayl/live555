/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**********/
// Copyright (c) 1996-2000, Live Networks, Inc.  All rights reserved
//	Help by Carlo Bonamico to get working for Windows
// Delay queue
// Implementation

#include "DelayQueue.hh"
#include "GroupsockHelper.hh"
#include <signal.h>

static const int MILLION = 1000000;

///// Timeval /////

int Timeval::operator>=(const Timeval& arg2) const {
  return seconds() > arg2.seconds()
    || (seconds() == arg2.seconds()
	&& useconds() >= arg2.useconds());
}

void Timeval::operator+=(const DelayInterval& arg2) {
  secs() += arg2.seconds(); usecs() += arg2.useconds();
  if (usecs() >= MILLION) {
    usecs() -= MILLION;
    ++secs();
  }
}

void Timeval::operator-=(const DelayInterval& arg2) {
  secs() -= arg2.seconds(); usecs() -= arg2.useconds();
  if (usecs() < 0) {
    usecs() += MILLION;
    --secs();
  }
  if (secs() < 0)
    secs() = usecs() = 0;
}

DelayInterval operator-(const Timeval& arg1, const Timeval& arg2) {
  time_base_seconds secs = arg1.seconds() - arg2.seconds();
  time_base_seconds usecs = arg1.useconds() - arg2.useconds();
  
  if (usecs < 0) {
    usecs += MILLION;
    --secs;
  }
  if (secs < 0)
    return ZERO;
  else
    return DelayInterval(secs, usecs);
}


///// DelayInterval /////

DelayInterval operator*(short arg1, const DelayInterval& arg2) {
  time_base_seconds result_seconds = arg1*arg2.seconds();
  time_base_seconds result_useconds = arg1*arg2.useconds();
  
  time_base_seconds carry = result_useconds/MILLION;
  result_useconds -= carry*MILLION;
  result_seconds += carry;
  
  return DelayInterval(result_seconds, result_useconds);
}

#ifndef INT_MAX
#define INT_MAX	0x7FFFFFFF
#endif
const DelayInterval ZERO(0, 0);
const DelayInterval SECOND(1, 0);
const DelayInterval ETERNITY(INT_MAX, MILLION-1);
// used internally to make the implementation work


///// DelayQueueEntry /////

long DelayQueueEntry::tokenCounter = 0;

DelayQueueEntry::DelayQueueEntry(DelayInterval delay)
  : fDeltaTimeRemaining(delay) {
  fNext = fPrev = this;
  fToken = ++tokenCounter;
}

void DelayQueueEntry::handleTimeout() {
  delete this;
}


///// DelayQueue /////

DelayQueue::DelayQueue()
  : DelayQueueEntry(ETERNITY) {
}

DelayQueue::~DelayQueue() {
  ScopedRWLock l(fLock);
  while (fNext != this) removeEntry1(fNext);
}

void DelayQueue::addEntry(DelayQueueEntry* newEntry) {
  ScopedRWLock l(fLock);
  addEntry1(newEntry);
}

void DelayQueue::updateEntry(DelayQueueEntry* entry, DelayInterval newDelay) {
  if (entry == NULL) return;
  ScopedRWLock l(fLock);
  
  removeEntry1(entry);
  entry->fDeltaTimeRemaining = newDelay;
  addEntry1(entry);
}

void DelayQueue::updateEntry(long tokenToFind, DelayInterval newDelay) {
  ScopedRWLock l(fLock);
  DelayQueueEntry* entry = findEntryByToken(tokenToFind);
  if (entry == NULL) return;
  
  removeEntry1(entry);
  entry->fDeltaTimeRemaining = newDelay;
  addEntry1(entry);
}

void DelayQueue::removeEntry(DelayQueueEntry* entry) {
  ScopedRWLock l(fLock);
  removeEntry1(entry);
}

DelayQueueEntry* DelayQueue::removeEntry(long tokenToFind) {
  ScopedRWLock l(fLock);
  DelayQueueEntry* entry = findEntryByToken(tokenToFind);
  if (entry != NULL) {
    removeEntry1(entry);
  }

  return entry;
}

///// Implementation /////

DelayQueueEntry* DelayQueue::findEntryByToken(long tokenToFind) {
  DelayQueueEntry* cur = head();
  while (cur != this) {
    if (cur->token() == tokenToFind) return cur;
    cur = cur->fNext;
  }

  return NULL;
}

void DelayQueue::addEntry1(DelayQueueEntry* newEntry) {
  EventTime timeNow = TimeNow();
  if (fLastAlarmTime.seconds() == 0) {
    // Hack: We're just starting up, so set "fLastAlarmTime" to now:
    fLastAlarmTime = timeNow;
  }

  // Adjust for time elapsed since the last alarm:
  DelayInterval timeSinceLastAlarm = timeNow - fLastAlarmTime;
  newEntry->fDeltaTimeRemaining += timeSinceLastAlarm;

  DelayQueueEntry* cur = head();
  while (newEntry->fDeltaTimeRemaining >= cur->fDeltaTimeRemaining) {
    newEntry->fDeltaTimeRemaining -= cur->fDeltaTimeRemaining;
    cur = cur->fNext;
  }
  
  cur->fDeltaTimeRemaining -= newEntry->fDeltaTimeRemaining;
  
  // Add "newEntry" to the queue, just before "cur":
  newEntry->fNext = cur;
  newEntry->fPrev = cur->fPrev;
  cur->fPrev = newEntry->fPrev->fNext = newEntry;
}

void DelayQueue::removeEntry1(DelayQueueEntry* entry) {
  if (entry == NULL || entry->fNext == NULL) return;
  
  entry->fNext->fDeltaTimeRemaining += entry->fDeltaTimeRemaining;
  entry->fPrev->fNext = entry->fNext;
  entry->fNext->fPrev = entry->fPrev;
  entry->fNext = entry->fPrev = NULL;
  // in case we should try to remove it again
}

void DelayQueue::handleAlarm() {
  fLock.rwLock();
  
  // Begin by figuring out the exact time that elapsed since the last alarm
  EventTime timeNow = TimeNow();
  DelayInterval timeSinceLastAlarm = timeNow - fLastAlarmTime;
  fLastAlarmTime = timeNow;
  
  // Now adjust the delay queue for any entries that have expired.
  // Also, handle the first such entry (if any).
  DelayQueueEntry* curEntry = head();
  while (timeSinceLastAlarm >= curEntry->fDeltaTimeRemaining) {
    timeSinceLastAlarm -= curEntry->fDeltaTimeRemaining;
    curEntry->fDeltaTimeRemaining = ZERO;
    curEntry = curEntry->fNext;
  }
  curEntry->fDeltaTimeRemaining -= timeSinceLastAlarm;
  
  if (head()->fDeltaTimeRemaining == ZERO) {
    DelayQueueEntry* toRemove = head();
    removeEntry1(toRemove);
    
    fLock.unlock(); // in case timeout handler accesses delay queue
    toRemove->handleTimeout();
    fLock.rwLock();
  }

  fLock.unlock();
}


///// EventTime /////

EventTime TimeNow() {
  struct timeval tvNow;

#ifdef BSD
  struct timezone Idunno;
#else
  int Idunno;
#endif
  gettimeofday(&tvNow, &Idunno);

  return EventTime(tvNow.tv_sec, tvNow.tv_usec);
}

DelayInterval TimeRemainingUntil(const EventTime& futureEvent) {
  return futureEvent - TimeNow();
}

const EventTime THE_END_OF_TIME(INT_MAX);
