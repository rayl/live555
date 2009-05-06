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
// C++ header

#ifndef _LOCK_HH
#define _LOCK_HH

#ifndef _BOOLEAN_HH
#include "Boolean.hh"
#endif

///// Read-Write Locks /////

class RWLock {
    public:
    	RWLock();
    	~RWLock();

    	void rLock() const; // obtain a reader's lock
    	void rwLock(); // obtain a read-write lock
   	void unlock() const; // unlock either kind of lock
    		
    	Boolean isLocked();
    private:
    	unsigned fLock;
};


///// Scoped Locks /////

// "Scoped" locks are a convenient way of providing locking on a section of
// code, because the lock will get released no matter how the code is exited.

class ScopedRWLock {
    public:
    	ScopedRWLock(RWLock& lock);
    	~ScopedRWLock();

    private:
    	RWLock& fLock;
};

// "ScopedRLock" is used to obtain a read-only lock on a const object.
// (We pretend that the encapsulated lock is also "const".)
class ScopedRLock {
    public:
    	ScopedRLock(RWLock const& lock);
    	~ScopedRLock();

    private:
    	RWLock const& fLock;
};

#endif
