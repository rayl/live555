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
// Copyright (c) 1996-2005 Live Networks, Inc.  All rights reserved.
// A filter for converting a stream of MPEG PES packets (coming from a file)
// to a MPEG-2 Transport Stream
// C++ header

#ifndef _MPEG2_TRANSPORT_STREAM_FROM_PES_FILE_SOURCE_HH
#define _MPEG2_TRANSPORT_STREAM_FROM_PES_FILE_SOURCE_HH

#ifndef _MPEG2_TRANSPORT_STREAM_MULTIPLEXOR_HH
#include "MPEG2TransportStreamMultiplexor.hh"
#endif
#ifndef _PES_FILE_SOURCE_HH
#include "PESFileSource.hh"
#endif

class MPEG2TransportStreamFromPESFileSource: public MPEG2TransportStreamMultiplexor {
public:
  static MPEG2TransportStreamFromPESFileSource*
  createNew(UsageEnvironment& env, PESFileSource* inputSource);

protected:
  MPEG2TransportStreamFromPESFileSource(UsageEnvironment& env,
					PESFileSource* inputSource);
      // called only by createNew()
  virtual ~MPEG2TransportStreamFromPESFileSource();

private:
  // Redefined virtual functions:
  virtual void doStopGettingFrames();
  virtual void awaitNewBuffer(unsigned char* oldBuffer);

private:
  static void afterGettingFrame(void* clientData, unsigned frameSize,
				unsigned numTruncatedBytes,
				struct timeval presentationTime,
				unsigned durationInMicroseconds);
  void afterGettingFrame1(unsigned frameSize,
			  unsigned numTruncatedBytes,
			  struct timeval presentationTime,
			  unsigned durationInMicroseconds);

private:
  PESFileSource* fInputSource;
  unsigned char* fInputBuffer;
};

#endif
