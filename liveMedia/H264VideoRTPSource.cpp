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
  if (packetSize < expectedHeaderSize) return False;
  
  // Has to check if the type field says 28 (=> FU-A)
  Boolean FUA = (headerStart[0]&0x1C) == 28;
  if (FUA) {
    // FU-A has 2 bytes payload header
    // One byte FU indicator, and one byte FU header
    // If it's the startbit is set, we reconstruct the original NAL header
    if ((headerStart[1]&0x80) == 128) {
      expectedHeaderSize = 1;
      headerStart[1] = (headerStart[0]&0xE0)+(headerStart[1]&0x1F); 
      fCurrentPacketCompletesFrame = False;
      
      if (packetSize < expectedHeaderSize) return False;
    } else {
      // If the startbit is not set, both the FU indicator and header
      // can be discarded
      expectedHeaderSize = 2;
      if (packetSize < expectedHeaderSize) return False;
      
      // Checking for end bit
      if ((headerStart[1]&0x40) == 64) {
	fCurrentPacketCompletesFrame = True;
      }
      else {
	fCurrentPacketCompletesFrame = False;
      }
    }
  } else {
    // Every arriving packet contains a decodable NAL unit
    // Other types, such as STAP-A has to be handled in the mediaplayer
    fCurrentPacketCompletesFrame = True;
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
