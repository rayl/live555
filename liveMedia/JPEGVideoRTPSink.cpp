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
// RTP sink for JPEG video (RFC 2435)
// Implementation

#include "JPEGVideoRTPSink.hh"
#include "JPEGVideoSource.hh"

JPEGVideoRTPSink
::JPEGVideoRTPSink(UsageEnvironment& env, Groupsock* RTPgs)
  : VideoRTPSink(env, RTPgs, 26, 90000, "JPEG") {
}

JPEGVideoRTPSink::~JPEGVideoRTPSink() {
}

JPEGVideoRTPSink*
JPEGVideoRTPSink::createNew(UsageEnvironment& env, Groupsock* RTPgs) {
  return new JPEGVideoRTPSink(env, RTPgs);
}

Boolean JPEGVideoRTPSink::sourceIsCompatibleWithUs(MediaSource& source) {
  return source.isJPEGVideoSource();
}

Boolean JPEGVideoRTPSink
::frameCanAppearAfterPacketStart(unsigned char const* frameStart,
				 unsigned numBytesInFrame) const {
  // A packet can contain only one frame
  return False;
}

void JPEGVideoRTPSink
::doSpecialFrameHandling(unsigned fragmentationOffset,
			 unsigned char* frameStart,
			 unsigned numBytesInFrame,
			 struct timeval frameTimestamp,
			 unsigned numRemainingBytes) {
  // Our source is assumed to be a JPEGVideoSource #####
  JPEGVideoSource* source = (JPEGVideoSource*)fSource; 

  u_int8_t mainJPEGHeader[8]; // the special header

  mainJPEGHeader[0] = 0; // Type-specific
  mainJPEGHeader[1] = fragmentationOffset >> 16;
  mainJPEGHeader[2] = fragmentationOffset >> 8;
  mainJPEGHeader[3] = fragmentationOffset;
  mainJPEGHeader[4] = source->type();
  mainJPEGHeader[5] = source->qFactor();
  mainJPEGHeader[6] = source->width();
  mainJPEGHeader[7] = source->height();
  setSpecialHeaderBytes(mainJPEGHeader, sizeof mainJPEGHeader);

  if (numRemainingBytes == 0) {
    // This packet contains the last (or only) fragment of the frame.
    // Set the RTP 'M' ('marker') bit:
    setMarkerBit();
  }

  // Also set the RTP timestamp:
  setTimestamp(frameTimestamp);
}


unsigned JPEGVideoRTPSink::specialHeaderSize() const {
  // In the current implementation, we generate just the 8-byte
  // "main JPEG header", under the assumption that
  // (i) no restart markers are used, and
  // (ii) no custom quantization tables are used.
  return 8;
}
