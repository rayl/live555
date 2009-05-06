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
// Implementation

#include "MPEG2TransportStreamFromPESFileSource.hh"

#define MAX_PES_PACKET_SIZE 65536

MPEG2TransportStreamFromPESFileSource* MPEG2TransportStreamFromPESFileSource
::createNew(UsageEnvironment& env, PESFileSource* inputSource) {
  return new MPEG2TransportStreamFromPESFileSource(env, inputSource);
}

MPEG2TransportStreamFromPESFileSource
::MPEG2TransportStreamFromPESFileSource(UsageEnvironment& env,
					PESFileSource* inputSource)
  : MPEG2TransportStreamMultiplexor(env),
    fInputSource(inputSource) {
  fInputBuffer = new unsigned char[MAX_PES_PACKET_SIZE];
}

MPEG2TransportStreamFromPESFileSource::~MPEG2TransportStreamFromPESFileSource() {
  Medium::close(fInputSource);
  delete[] fInputBuffer;
}

void MPEG2TransportStreamFromPESFileSource::doStopGettingFrames() {
  fInputSource->stopGettingFrames();
}

void MPEG2TransportStreamFromPESFileSource
::awaitNewBuffer(unsigned char* /*oldBuffer*/) {
  fInputSource->getNextFrame(fInputBuffer, MAX_PES_PACKET_SIZE,
			     afterGettingFrame, this,
			     FramedSource::handleClosure, this);
}

void MPEG2TransportStreamFromPESFileSource
::afterGettingFrame(void* clientData, unsigned frameSize,
		    unsigned numTruncatedBytes,
		    struct timeval presentationTime,
		    unsigned durationInMicroseconds) {
  MPEG2TransportStreamFromPESFileSource* source
    = (MPEG2TransportStreamFromPESFileSource*)clientData;
  source->afterGettingFrame1(frameSize, numTruncatedBytes,
			    presentationTime, durationInMicroseconds);
}

void MPEG2TransportStreamFromPESFileSource
::afterGettingFrame1(unsigned frameSize,
		     unsigned /*numTruncatedBytes*/,
		     struct timeval /*presentationTime*/,
		     unsigned /*durationInMicroseconds*/) {
  if (frameSize < 4) return;

  u_int8_t stream_id = 0;
  if (frameSize >= 4/*should always be true*/) stream_id = fInputBuffer[3];
  handleNewBuffer(fInputBuffer, frameSize,
		  fInputSource->mpegVersion(),
		  fInputSource->lastSeenSCR(stream_id));
}
