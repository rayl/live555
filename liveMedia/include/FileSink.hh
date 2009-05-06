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
// "liveMedia"
// Copyright (c) 1996-2003 Live Networks, Inc.  All rights reserved.
// File Sinks
// C++ header

#ifndef _FILE_SINK_HH
#define _FILE_SINK_HH

#ifndef _MEDIA_SINK_HH
#include "MediaSink.hh"
#endif

class FileSink: public MediaSink {
public:
  static FileSink* createNew(UsageEnvironment& env, char const* fileName,
			     unsigned bufferSize = 10000);
  // "bufferSize" should be at least as large as the largest expected
  // input frame.

  FILE* fid() const { return fOutFid; }
  // (Available in case a client wants to add extra data to the output file)

protected:
  FileSink(UsageEnvironment& env, FILE* fid, unsigned bufferSize);
      // called only by createNew()
  virtual ~FileSink();

  static FILE* openFileByName(UsageEnvironment& env, char const* fileName);

protected:
  static void afterGettingFrame(void* clientData, unsigned frameSize,
				struct timeval presentationTime);
  friend void afterGettingFrame(void*, unsigned, struct timeval);

  FILE* fOutFid;
  unsigned char* fBuffer;
  unsigned fBufferSize;

private: // redefined virtual functions:
  virtual Boolean continuePlaying();
};


#endif
