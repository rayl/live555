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
// Copyright (c) 1996-2001 Live Networks, Inc.  All rights reserved.
// Usage Environment
// C++ header

#ifndef _USAGE_ENVIRONMENT_HH
#define _USAGE_ENVIRONMENT_HH

#ifndef _USAGEENVIRONMENT_VERSION_HH
#include "UsageEnvironment_version.hh"
#endif

#ifndef _STRDUP_HH
// "strDup()" is used often, so include this here, so everyone gets it:
#include "strDup.hh"
#endif

#ifndef NULL
#define NULL 0
#endif

class TaskScheduler; // forward

// An abstract base class, subclassed for each use of the library

class UsageEnvironment {
public:
  virtual ~UsageEnvironment();

  // task scheduler:
  TaskScheduler& taskScheduler() const {return fScheduler;}

  // result message handling:
  typedef char const* MsgString;
  virtual MsgString getResultMsg() const = 0;

  virtual void setResultMsg(MsgString msg) = 0;
  virtual void setResultMsg(MsgString msg1, MsgString msg2) = 0;
  virtual void setResultMsg(MsgString msg1, MsgString msg2, MsgString msg3) = 0;
  virtual void setResultErrMsg(MsgString msg) = 0;
	// like setResultMsg(), except that an 'errno' message is appended

  virtual void appendToResultMsg(MsgString msg) = 0;

  virtual void reportBackgroundError() = 0;
	// used to report a (previously set) error message within
	// a background event

  void* priv;
      // a pointer to additional, optional, client-specific state

protected:
  UsageEnvironment(TaskScheduler& scheduler); // abstract base class

private:
  TaskScheduler& fScheduler;
};


typedef void TaskFunc(void* clientData);
typedef void* TaskToken;

class TaskScheduler {
public:
  virtual ~TaskScheduler();

  virtual TaskToken scheduleDelayedTask(int microseconds, TaskFunc* proc,
					 void* clientData) = 0;
	// Schedules a task to occur (after a delay) when we next
	// reach a scheduling point.
	// (Does not delay if "microseconds" <= 0)
	// Returns a token that can be used in a subsequent call to
	// unscheduleDelayedTask()

  virtual void unscheduleDelayedTask(TaskToken& prevTask) = 0;
	// (Has no effect if "prevTask" == NULL)

  // For handling socket reads in the background:
  typedef void BackgroundHandlerProc(void* clientData, int mask);
    // Possible bits to set in "mask".  (These are deliberately defined
    // the same as those in Tcl, to make a Tcl-based subclass easy.)
    #define SOCKET_READABLE    (1<<1)
    #define SOCKET_WRITABLE    (1<<2)
    #define SOCKET_EXCEPTION   (1<<3)
  virtual void turnOnBackgroundReadHandling(int socketNum,
				BackgroundHandlerProc* handlerProc,
				void* clientData) = 0;
  virtual void turnOffBackgroundReadHandling(int socketNum) = 0;

  virtual void doEventLoop(char* watchVariable = NULL) = 0;
	// Stops the current thread of control from proceeding,
	// but allows delayed tasks (and/or background I/O handling)
	// to proceed.
        // (If "watchVariable" is not NULL, then we return from this
        // routine when *watchVariable != 0)

protected:
  TaskScheduler(); // abstract base class
};

#endif
