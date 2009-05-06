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
// MPEG-1 or MPEG-2 Video RTP Sources
// Implementation

#include "MPEGVideoRTPSource.hh"

MPEGVideoRTPSource*
MPEGVideoRTPSource::createNew(UsageEnvironment& env, Groupsock* RTPgs,
			      unsigned char rtpPayloadFormat,
			      unsigned rtpTimestampFrequency) {
  return new MPEGVideoRTPSource(env, RTPgs, rtpPayloadFormat,
				rtpTimestampFrequency);
}

MPEGVideoRTPSource::MPEGVideoRTPSource(UsageEnvironment& env,
				       Groupsock* RTPgs,
				       unsigned char rtpPayloadFormat,
				       unsigned rtpTimestampFrequency)
  : MultiFramedRTPSource(env, RTPgs,
			 rtpPayloadFormat, rtpTimestampFrequency){
}

MPEGVideoRTPSource::~MPEGVideoRTPSource() {
}

Boolean MPEGVideoRTPSource
::processSpecialHeader(unsigned char* headerStart, unsigned packetSize,
		       Boolean /*rtpMarkerBit*/,
		       unsigned& resultSpecialHeaderSize) {
  // There's a 4-byte video-specific header
  if (packetSize < 4) return False;

  //unsigned header = ntohl(*(unsigned*)headerStart);

  // Assume that clients are able to handle even fragmented slices,
  // so don't bother checking for that here.
  // (I.e., leave "fCurrentPacketCompletesFrame" at True permanently)

  resultSpecialHeaderSize = 4;
  return True;
}    

Boolean MPEGVideoRTPSource
::packetIsUsableInJitterCalculation(unsigned char* packet,
				    unsigned packetSize) {
  // There's a 4-byte video-specific header
  if (packetSize < 4) return False;

  // Extract the "Picture-Type" field from this, to determine whether
  // this packet can be used in jitter calculations:
  unsigned header = ntohl(*(unsigned*)packet);

  unsigned short pictureType = (header>>8)&0x7;
  if (pictureType == 1) { // an I frame
    return True;
  } else { // a P, B, D, or other unknown frame type
    return False;
  }
}    

char const* MPEGVideoRTPSource::MIMEtype() const {
  return "video/mpeg";
}

