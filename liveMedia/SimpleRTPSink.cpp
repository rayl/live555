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
// Copyright (c) 1996-2002 Live Networks, Inc.  All rights reserved.
// A simple RTP sink that packs frames into each outgoing
//     packet, without any fragmentation or special headers.
// Implementation

#include "SimpleRTPSink.hh"

SimpleRTPSink::SimpleRTPSink(UsageEnvironment& env, Groupsock* RTPgs,
			     unsigned char rtpPayloadFormat,
			     unsigned rtpTimestampFrequency,
			     char const* sdpMediaTypeString,
			     char const* rtpPayloadFormatName,
			     Boolean allowMultipleFramesPerPacket)
  : MultiFramedRTPSink(env, RTPgs, rtpPayloadFormat,
		       rtpTimestampFrequency, rtpPayloadFormatName),
    fSDPMediaTypeString(strdup(sdpMediaTypeString)),
    fAllowMultipleFramesPerPacket(allowMultipleFramesPerPacket) {
}

SimpleRTPSink::~SimpleRTPSink() {
  delete (char*)fSDPMediaTypeString;
}

SimpleRTPSink*
SimpleRTPSink::createNew(UsageEnvironment& env, Groupsock* RTPgs,
			 unsigned char rtpPayloadFormat,
			 unsigned rtpTimestampFrequency,
			 char const* sdpMediaTypeString,
			 char const* rtpPayloadFormatName,
			 Boolean allowMultipleFramesPerPacket) {
  return new SimpleRTPSink(env, RTPgs,
			   rtpPayloadFormat, rtpTimestampFrequency,
			   sdpMediaTypeString, rtpPayloadFormatName,
			   allowMultipleFramesPerPacket);
}

char const* SimpleRTPSink::sdpMediaType() const {
  return fSDPMediaTypeString;
}

Boolean SimpleRTPSink::
frameCanAppearAfterPacketStart(unsigned char const* frameStart,
			       unsigned numBytesInFrame) const {
  return fAllowMultipleFramesPerPacket;
}
