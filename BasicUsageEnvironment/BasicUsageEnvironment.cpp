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
// Copyright (c) 1996-2000 Live Networks, Inc.  All rights reserved.
// Basic Usage Environment: for a simple, non-scripted, console application
// Implementation

#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"
#include <errno.h>
#include <string.h>
#if defined(__WIN32__) || defined(_WIN32)
#else
#include <unistd.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

////////// BasicUsageEnvironment //////////

#if defined(__WIN32__) || defined(_WIN32)
extern "C" int initializeWinsockIfNecessary();
#endif

BasicUsageEnvironment::BasicUsageEnvironment(TaskScheduler& taskScheduler)
  : UsageEnvironment(taskScheduler),
    fBufferMaxSize(RESULT_MSG_BUFFER_MAX) {
#if defined(__WIN32__) || defined(_WIN32)
  if (!initializeWinsockIfNecessary()) {
    setResultErrMsg("Failed to initialize 'winsock': ");
    reportBackgroundError();
    exit(1);
  }
#endif

  reset();
}

BasicUsageEnvironment*
BasicUsageEnvironment::createNew(TaskScheduler& taskScheduler) {
  return new BasicUsageEnvironment(taskScheduler);
}

BasicUsageEnvironment::~BasicUsageEnvironment() {
}

void BasicUsageEnvironment::reset() {
  fCurBufferSize = 0;
  fResultMsgBuffer[fCurBufferSize] = '\0';
}


// Implementation of virtual functions:

char const* BasicUsageEnvironment::getResultMsg() const {
  return fResultMsgBuffer;
}

void BasicUsageEnvironment::setResultMsg(MsgString msg) {
  reset();
  appendToResultMsg(msg);
}

void BasicUsageEnvironment::setResultMsg(MsgString msg1, MsgString msg2) {
  setResultMsg(msg1);
  appendToResultMsg(msg2);
}

void BasicUsageEnvironment::setResultMsg(MsgString msg1, MsgString msg2,
				       MsgString msg3) {
  setResultMsg(msg1, msg2);
  appendToResultMsg(msg3);
}

void BasicUsageEnvironment::setResultErrMsg(MsgString msg) {
  setResultMsg(msg);

  appendToResultMsg(strerror(errno));
}

void BasicUsageEnvironment::appendToResultMsg(MsgString msg) {
  char* curPtr = &fResultMsgBuffer[fCurBufferSize];
  unsigned spaceAvailable = fBufferMaxSize - fCurBufferSize;
  unsigned msgLength = strlen(msg);

  // Copy only enough of "msg" as will fit:
  if (msgLength > spaceAvailable-1) {
    msgLength = spaceAvailable-1;
  }

  our_bcopy((char*)msg, curPtr, msgLength);
  fCurBufferSize += msgLength;
  fResultMsgBuffer[fCurBufferSize] = '\0';
}

void BasicUsageEnvironment::reportBackgroundError() {
  fputs(fResultMsgBuffer, stderr);
}


////////// A subclass of DelayQueueEntry,
//////////     used to implement BasicTaskScheduler::scheduleDelayedTask()

class AlarmHandler: public DelayQueueEntry {
public:
  AlarmHandler(TaskFunc* proc, void* clientData, DelayInterval timeToDelay)
    : DelayQueueEntry(timeToDelay), fProc(proc), fClientData(clientData) {
  }
  
private: // redefined virtual functions
  virtual void handleTimeout() {
    (*fProc)(fClientData);
    DelayQueueEntry::handleTimeout();
  }
  
private:
  TaskFunc* fProc;
  void* fClientData;
};


////////// HandlerSet (etc.) definition //////////

class HandlerDescriptor {
  HandlerDescriptor(HandlerDescriptor* nextHandler);
  virtual ~HandlerDescriptor();

public:
  int socketNum;
  TaskScheduler::BackgroundHandlerProc* handlerProc;
  void* clientData;

private:
  // Descriptors are linked together in a doubly-linked list:
  friend class HandlerSet;
  friend class HandlerIterator;
  HandlerDescriptor* fNextHandler;
  HandlerDescriptor* fPrevHandler;
};

class HandlerSet {
public:
  HandlerSet();
  virtual ~HandlerSet();

  void assignHandler(int socketNum,
		     TaskScheduler::BackgroundHandlerProc* handlerProc,
		     void* clientData);
  void removeHandler(int socketNum);

private:
  friend class HandlerIterator;
  HandlerDescriptor fHandlers;
};
  
class HandlerIterator {
public:
  HandlerIterator(HandlerSet& handlerSet);
  virtual ~HandlerIterator();

  HandlerDescriptor* next(); // returns NULL if none
  void reset();

private:
  HandlerSet& fOurSet;
  HandlerDescriptor* fNextPtr;
};


////////// BasicTaskScheduler //////////

BasicTaskScheduler::BasicTaskScheduler()
  : fMaxNumSockets(0) {
  fReadHandlers = new HandlerSet;
  FD_ZERO(&fReadSet);
}

BasicTaskScheduler* BasicTaskScheduler::createNew() {
  return new BasicTaskScheduler();
}

BasicTaskScheduler::~BasicTaskScheduler() {
  delete fReadHandlers;
}

TaskToken BasicTaskScheduler::scheduleDelayedTask(int microseconds,
						 TaskFunc* proc,
						 void* clientData) {
  if (microseconds < 0) microseconds = 0;
  DelayInterval timeToDelay(microseconds/1000000, microseconds%1000000);
  AlarmHandler* alarmHandler = new AlarmHandler(proc, clientData, timeToDelay);
  fDelayQueue.addEntry(alarmHandler);

  return (void*)(alarmHandler->token()); 
}

void BasicTaskScheduler::unscheduleDelayedTask(TaskToken& prevTask) {
  DelayQueueEntry* alarmHandler = fDelayQueue.removeEntry((int)prevTask);
  delete alarmHandler;
}

void BasicTaskScheduler::turnOnBackgroundReadHandling(int socketNum,
				BackgroundHandlerProc* handlerProc,
				void* clientData) {
  if (socketNum < 0) return;
  FD_SET(socketNum, &fReadSet);
  fReadHandlers->assignHandler(socketNum, handlerProc, clientData);

  if (socketNum+1 > fMaxNumSockets) {
    fMaxNumSockets = socketNum+1;
  }
}

void BasicTaskScheduler::turnOffBackgroundReadHandling(int socketNum) {
  if (socketNum < 0) return;
  FD_CLR((unsigned)socketNum, &fReadSet);
  fReadHandlers->removeHandler(socketNum);

  if (socketNum+1 == fMaxNumSockets) {
    --fMaxNumSockets;
  }
}

void BasicTaskScheduler::SingleStep() {
  fd_set readSet = fReadSet; // make a copy for this select() call
  
  DelayInterval& timeToDelay = fDelayQueue.timeToNextAlarm();
  struct timeval tv_timeToDelay;
  tv_timeToDelay.tv_sec = timeToDelay.seconds();
  tv_timeToDelay.tv_usec = timeToDelay.useconds();
  // Very large "tv_sec" values cause select() to fail.
  // Don't make it any larger than 1000000 seconds (11.5 days)
  const long MAX_TV_SEC = 1000000;
  if (tv_timeToDelay.tv_sec > MAX_TV_SEC) {
    tv_timeToDelay.tv_sec = MAX_TV_SEC;
  }
  
  int selectResult = select(fMaxNumSockets, &readSet, NULL, NULL,
			    &tv_timeToDelay);
  if (selectResult < 0) {
#if defined(__WIN32__) || defined(_WIN32)
    // Windows sucks - ignore bogus 'errors' that aren't real errors at all:
    if (errno == 0) {
      errno = WSAGetLastError();
    }
    // For some unknown reason, select() in Windoze sometimes fails with WSAEINVAL if
    // it was called with no entries set in "readSet".  If this happens, ignore it:
    if (errno == WSAEINVAL && readSet.fd_count == 0) {
      errno = 0;
    }
    if (errno != 0)
#endif
      {
	// Unexpected error - treat this as fatal:
	perror("BasicTaskScheduler::blockMyself(): select() fails");
	exit(0);
      }
  }
  
  // Handle any delayed events that have come due:
  fDelayQueue.handleAlarm();
  
  // Call the handler function for each readable socket:
  HandlerIterator iter(*fReadHandlers);
  HandlerDescriptor* handler;
  while ((handler = iter.next()) != NULL) {
    if (FD_ISSET(handler->socketNum, &readSet) &&
	FD_ISSET(handler->socketNum, &fReadSet) /* sanity check */ &&
	handler->handlerProc != NULL) {
      (*handler->handlerProc)(handler->clientData, SOCKET_READABLE);
    }
  }
}

void BasicTaskScheduler::blockMyself(char* watchVariable) {
  // Repeatedly loop, calling select() on readable sockets:
  while (1) {
    if (watchVariable != NULL && *watchVariable != 0) break;
    SingleStep();
  }
}


////////// HandlerSet (etc.) implementation //////////

HandlerDescriptor::HandlerDescriptor(HandlerDescriptor* nextHandler) {
  // Link this descriptor into a doubly-linked list:
  // (Note that this code works even if "nextHandler == this")
  fNextHandler = nextHandler;
  fPrevHandler = nextHandler->fPrevHandler;
  nextHandler->fPrevHandler = this;
  fPrevHandler->fNextHandler = this;
}

HandlerDescriptor::~HandlerDescriptor() {
  // Unlink this descriptor from a doubly-linked list:
  fNextHandler->fPrevHandler = fPrevHandler;
  fPrevHandler->fNextHandler = fNextHandler;
}

HandlerSet::HandlerSet()
  : fHandlers(&fHandlers) {
  fHandlers.socketNum = -1; // shouldn't ever get looked at, but in case...
}

HandlerSet::~HandlerSet() {
  // Delete each handler descriptor:
  while (fHandlers.fNextHandler != &fHandlers) {
    delete fHandlers.fNextHandler; // changes fHandlers->fNextHandler
  }
}

void HandlerSet
::assignHandler(int socketNum,
		TaskScheduler::BackgroundHandlerProc* handlerProc,
		void* clientData) {
  // First, see if there's already a handler for this socket:
  HandlerDescriptor* handler;
  HandlerIterator iter(*this);
  while ((handler = iter.next()) != NULL) {
    if (handler->socketNum == socketNum) break;
  }
  if (handler == NULL) { // No existing handler, so create a new descr:
    handler = new HandlerDescriptor(fHandlers.fNextHandler);
    handler->socketNum = socketNum;
  }
  
  handler->handlerProc = handlerProc;
  handler->clientData = clientData;
}

void HandlerSet::removeHandler(int socketNum) {
  HandlerDescriptor* handler;
  HandlerIterator iter(*this);
  while ((handler = iter.next()) != NULL) {
    if (handler->socketNum == socketNum) {
      delete handler;
      break;
    }
  }
}

HandlerIterator::HandlerIterator(HandlerSet& handlerSet)
  : fOurSet(handlerSet) {
  reset();
}

HandlerIterator::~HandlerIterator() {
}

void HandlerIterator::reset() {
  fNextPtr = fOurSet.fHandlers.fNextHandler;
}

HandlerDescriptor* HandlerIterator::next() {
  HandlerDescriptor* result = fNextPtr;
  if (result == &fOurSet.fHandlers) { // no more
    result = NULL;
  } else {
    fNextPtr = fNextPtr->fNextHandler;
  }

  return result;
}
