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
// C++ header

#ifndef _BASIC_USAGE_ENVIRONMENT_HH
#define _BASIC_USAGE_ENVIRONMENT_HH

#ifndef _BASICUSAGEENVIRONMENT_VERSION_HH
#include "BasicUsageEnvironment_version.hh"
#endif

#ifndef _USAGE_ENVIRONMENT_HH
#include "UsageEnvironment.hh"
#endif

#ifndef _DELAY_QUEUE_HH
#include "DelayQueue.hh"
#endif

#include <sys/types.h>
#if defined(_QNX4)
#include <sys/select.h>
#include <unix.h>
#endif

#define RESULT_MSG_BUFFER_MAX 1000

class BasicUsageEnvironment: public UsageEnvironment {
public:
  static BasicUsageEnvironment* createNew(TaskScheduler& taskScheduler);
  virtual ~BasicUsageEnvironment();
  
  // redefined virtual functions:
  MsgString getResultMsg() const;
  
  void setResultMsg(MsgString msg);
  void setResultMsg(MsgString msg1,
		    MsgString msg2);
  void setResultMsg(MsgString msg1,
		    MsgString msg2,
		    MsgString msg3);
  void setResultErrMsg(MsgString msg);
  
  void appendToResultMsg(MsgString msg);
  
  void reportBackgroundError();
  
protected:
  BasicUsageEnvironment(TaskScheduler& taskScheduler);
      // called only by "createNew()" (or subclass constructors)

private:
  void reset();
  
  char fResultMsgBuffer[RESULT_MSG_BUFFER_MAX];
  unsigned fCurBufferSize;
  unsigned fBufferMaxSize;
};

class HandlerSet; // forward

class BasicTaskScheduler: public TaskScheduler {
public:
  static BasicTaskScheduler* createNew();
  virtual ~BasicTaskScheduler();

  void SingleStep();
  
protected:
  BasicTaskScheduler();
      // called only by "createNew()" (or subclass constructors)

private:
  // Redefined virtual functions:
  TaskToken scheduleDelayedTask(int microseconds, TaskFunc* proc,
				void* clientData);
  void unscheduleDelayedTask(TaskToken& prevTask);
  
  void turnOnBackgroundReadHandling(int socketNum,
				    BackgroundHandlerProc* handlerProc,
				    void* clientData);
  void turnOffBackgroundReadHandling(int socketNum);
  
  void doEventLoop(char* watchVariable);

private:
  // To implement delayed operations:
  DelayQueue fDelayQueue;

  // To implement background reads:
  int fMaxNumSockets;
  fd_set fReadSet;
  HandlerSet* fReadHandlers;
};

#endif
