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
// Copyright (c) 1996-2004 Live Networks, Inc.  All rights reserved.
// A source that consists of multiple byte-stream files, read sequentially
// C++ header

#ifndef _BYTE_STREAM_MULTI_FILE_SOURCE_HH
#define _BYTE_STREAM_MULTI_FILE_SOURCE_HH

#ifndef _BYTE_STREAM_FILE_SOURCE_HH
#include "ByteStreamFileSource.hh"
#endif

class ByteStreamMultiFileSource: public FramedSource {
public:
  static ByteStreamMultiFileSource*
      createNew(UsageEnvironment& env, char const** fileNameArray);
  // A 'filename' of NULL indicates the end of the array

protected:
  ByteStreamMultiFileSource(UsageEnvironment& env, char const** fileNameArray);
	// called only by createNew()

  virtual ~ByteStreamMultiFileSource();

private:
  // redefined virtual functions:
  virtual void doGetNextFrame();

private:
  static void onSourceClosure(void* clientData);
  void onSourceClosure1();
  static void afterGettingFrame(void* clientData,
				unsigned frameSize, unsigned numTruncatedBytes,
                                struct timeval presentationTime,
				unsigned durationInMicroseconds);

private:
  unsigned fNumSources;
  unsigned fCurrentlyReadSourceNumber;
  char const** fFileNameArray;
  ByteStreamFileSource** fSourceArray;
};

#endif
