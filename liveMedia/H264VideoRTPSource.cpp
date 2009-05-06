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
// H.264 Video RTP Sources
// Implementation 

#include "H264VideoRTPSource.hh"
#include "Base64.hh"

H264VideoRTPSource*
H264VideoRTPSource::createNew(UsageEnvironment& env, Groupsock* RTPgs,
			      unsigned char rtpPayloadFormat,
			      unsigned rtpTimestampFrequency) {
  return new H264VideoRTPSource(env, RTPgs, rtpPayloadFormat,
				rtpTimestampFrequency);
}

H264VideoRTPSource
::H264VideoRTPSource(UsageEnvironment& env, Groupsock* RTPgs,
		     unsigned char rtpPayloadFormat,
		     unsigned rtpTimestampFrequency)
  : MultiFramedRTPSource(env, RTPgs,
			 rtpPayloadFormat, rtpTimestampFrequency) {
}

H264VideoRTPSource::~H264VideoRTPSource() {
}

Boolean H264VideoRTPSource
::processSpecialHeader(BufferedPacket* packet,
                       unsigned& resultSpecialHeaderSize) {
  unsigned char* headerStart = packet->data();
  unsigned packetSize = packet->dataSize();
  
  // The header has a minimum size of 0, since the NAL header is used
  // as a payload header
  unsigned expectedHeaderSize = 0;
  
  // Check if the type field is 28 (FU-A) or 29 (FU-B)
  unsigned char nal_unit_type = (headerStart[0]&0x1F);
  if (nal_unit_type == 28 || nal_unit_type == 29) {
    // For these NALUs, the first two bytes are the FU indicator and the FU header.
    // If the start bit is set, we reconstruct the original NAL header:
    unsigned char startBit = headerStart[1]&0x80;
    unsigned char endBit = headerStart[1]&0x40;
    if (startBit) {
      expectedHeaderSize = 1;
      if (packetSize < expectedHeaderSize) return False;

      headerStart[1] = (headerStart[0]&0xE0)+(headerStart[1]&0x1F); 
      fCurrentPacketBeginsFrame = True;
    } else {
      // If the startbit is not set, both the FU indicator and header
      // can be discarded
      expectedHeaderSize = 2;
      if (packetSize < expectedHeaderSize) return False;
      fCurrentPacketBeginsFrame = False;
    }
    fCurrentPacketCompletesFrame = (endBit != 0);
  } else {
    // Every arriving packet contains a decodable NAL unit
    // Other types, such as STAP-A has to be handled in the mediaplayer
    fCurrentPacketBeginsFrame = fCurrentPacketCompletesFrame = True;
  }
  
  resultSpecialHeaderSize = expectedHeaderSize;
  return True;
}

char const* H264VideoRTPSource::MIMEtype() const {
  return "video/H264";
}

SPropRecord* parseSPropParameterSets(char const* sPropParameterSetsStr,
                                     // result parameter:
                                     unsigned& numSPropRecords) {
  // Make a copy of the input string, so we can replace the commas with '\0's:
  char* inStr = strDup(sPropParameterSetsStr);
  if (inStr == NULL) {
    numSPropRecords = 0;
    return NULL;
  }

  // Count the number of commas (and thus the number of parameter sets):
  numSPropRecords = 1;
  char* s;
  for (s = inStr; *s != '\0'; ++s) {
    if (*s == ',') {
      ++numSPropRecords;
      *s = '\0';
    }
  }
  
  // Allocate and fill in the result array:
  SPropRecord* resultArray = new SPropRecord[numSPropRecords];
  s = inStr;
  for (unsigned i = 0; i < numSPropRecords; ++i) {
    resultArray[i].sPropBytes = base64Decode(s, resultArray[i].sPropLength);
    s += strlen(s) + 1;
  }

  delete[] inStr;
  return resultArray;
}
