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
// A RTP source for a simple RTP payload format that
//     - doesn't have any special headers following the RTP header
//     - doesn't have any special framing apart from the packet data itself
// Implementation

#include "SimpleRTPSource.hh"
#include <string.h>

SimpleRTPSource* SimpleRTPSource::createNew(UsageEnvironment& env,
					    Groupsock* RTPgs,
					    unsigned char rtpPayloadFormat,
					    unsigned rtpTimestampFrequency,
					    char const* mimeTypeString,
					    unsigned offset) {
  return new SimpleRTPSource(env, RTPgs, rtpPayloadFormat,
			     rtpTimestampFrequency,
			     mimeTypeString, offset);
}

SimpleRTPSource::SimpleRTPSource(UsageEnvironment& env, Groupsock* RTPgs,
				 unsigned char rtpPayloadFormat,
				 unsigned rtpTimestampFrequency,
				 char const* mimeTypeString,
				 unsigned offset)
  : MultiFramedRTPSource(env, RTPgs,
			 rtpPayloadFormat, rtpTimestampFrequency),
    fMIMEtypeString(strdup(mimeTypeString)), fOffset(offset) {
}

SimpleRTPSource::~SimpleRTPSource() {
  delete (char*)fMIMEtypeString;
}

Boolean SimpleRTPSource
::processSpecialHeader(unsigned char* /*headerStart*/,
		       unsigned /*packetSize*/,
		       Boolean /*rtpMarkerBit*/,
		       unsigned& resultSpecialHeaderSize) {
  resultSpecialHeaderSize = fOffset;
  return True;
}

char const* SimpleRTPSource::MIMEtype() const {
  if (fMIMEtypeString == NULL) return MultiFramedRTPSource::MIMEtype();

  return fMIMEtypeString;
}
