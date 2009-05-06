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
// Mutual Exclusion Primitives
// Implementation

#include "Lock.hh"
#include <signal.h>

///// Read-Write Locks /////

// Since (in this implementation) the only concurrency in the application
// comes from the SIGALRM handler, we can implement these locks simply by
// deferring this signal.
// Should the implementation change to become multithreaded, we'd need to
// implement real locks here.

#if defined(__WIN32__) || defined(_WIN32)
// FIX THIS #####
#else
static sigset_t *sigsToBlock= NULL;
#endif

RWLock::RWLock() {
#if defined(__WIN32__) || defined(_WIN32)
// FIX THIS #####
#else
	if (sigsToBlock == NULL) {
		sigsToBlock = new sigset_t;
		sigaddset(sigsToBlock, SIGALRM);
	}
#endif
}

RWLock::~RWLock() {}

void RWLock::rLock() const { // obtain a reader's lock
#if defined(__WIN32__) || defined(_WIN32)
// FIX THIS #####
#else
	sigprocmask(SIG_BLOCK, sigsToBlock, NULL);
#endif
	*((unsigned*)&fLock) = 1; // deliberately violate the "const"ness here
}

void RWLock::rwLock() { // obtain a read-write lock
#if defined(__WIN32__) || defined(_WIN32)
// FIX THIS #####
#else
	sigprocmask(SIG_BLOCK, sigsToBlock, NULL);
#endif
	fLock = 1;
}

void RWLock::unlock() const { // unlock either kind of lock
	*((unsigned*)&fLock) = 0; // deliberately violate the "const"ness here
#if defined(__WIN32__) || defined(_WIN32)
// FIX THIS #####
#else
	sigprocmask(SIG_UNBLOCK, sigsToBlock, NULL);
#endif
}
    		
Boolean RWLock::isLocked() {
  return fLock != 0;
}


///// Scoped Locks /////

ScopedRWLock::ScopedRWLock(RWLock& lock)
    : fLock(lock)
{ fLock.rwLock(); }

ScopedRWLock::~ScopedRWLock()
{ fLock.unlock(); }

ScopedRLock::ScopedRLock(RWLock const& lock)
    : fLock(lock)
{ fLock.rLock(); }

ScopedRLock::~ScopedRLock()
{ fLock.unlock(); }
