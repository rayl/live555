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
// MPEG4-GENERIC ("audio", "video", or "application") RTP stream sources
// Implementation

#include "MPEG4GenericRTPSource.hh"

///////// MPEG4GenericRTPSource implementation ////////

//##### NOTE: INCOMPLETE!!! #####

MPEG4GenericRTPSource*
MPEG4GenericRTPSource::createNew(UsageEnvironment& env, Groupsock* RTPgs,
				 unsigned char rtpPayloadFormat,
				 unsigned rtpTimestampFrequency,
				 char const* mediumName) {
  return new MPEG4GenericRTPSource(env, RTPgs, rtpPayloadFormat,
				   rtpTimestampFrequency, mediumName);
}

MPEG4GenericRTPSource
::MPEG4GenericRTPSource(UsageEnvironment& env, Groupsock* RTPgs,
			unsigned char rtpPayloadFormat,
			unsigned rtpTimestampFrequency,
			char const* mediumName)
  : MultiFramedRTPSource(env, RTPgs,
			 rtpPayloadFormat, rtpTimestampFrequency) {
    unsigned mimeTypeLength =
      strlen(mediumName) + 14 /* strlen("/MPEG4-GENERIC") */ + 1;
    fMIMEType = new char[mimeTypeLength];
    if (fMIMEType != NULL) {
      sprintf(fMIMEType, "%s/MPEG4-GENERIC", mediumName);
    }
}

MPEG4GenericRTPSource::~MPEG4GenericRTPSource() {
  delete fMIMEType;
}

Boolean MPEG4GenericRTPSource
::processSpecialHeader(unsigned char* /*headerStart*/,
		       unsigned /*packetSize*/,
		       Boolean rtpMarkerBit,
		       unsigned& resultSpecialHeaderSize) {
  // The RTP "M" (marker) bit indicates the last fragment of a frame:
  fCurrentPacketCompletesFrame = rtpMarkerBit;

  // There is no special header
  resultSpecialHeaderSize = 0;
  return True;
}

char const* MPEG4GenericRTPSource::MIMEtype() const {
  return fMIMEType;
}
