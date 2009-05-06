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
// H.264 Video RTP Sources
// Implementation 

#include "H264VideoRTPSource.hh"

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
	if(FUA){
		
		// FU-A has 2 bytes payload header
		// One byte FU indicator, and one byte FU header
		// If it's the startbit is set, we reconstruct the original NAL header
		if((headerStart[1]&0x80) == 128){
			expectedHeaderSize = 1;
    		headerStart[1] = (headerStart[0]&0xE0)+(headerStart[1]&0x1F); 
			fCurrentPacketCompletesFrame = false;
			
			if (packetSize < expectedHeaderSize) return False;
		}
		
		// If the startbit is not set, both the FU indicator and header
		// can be discarded
		else{
			expectedHeaderSize = 2;
			if (packetSize < expectedHeaderSize) return False;
					
			// Checking for end bit
			if((headerStart[1]&0x40) == 64){
				fCurrentPacketCompletesFrame = true;
			}
			else{
				fCurrentPacketCompletesFrame = false;
			}
		}
		
	}
	
	else{
		// Every arriving packet contains a decodable NAL unit
		// Other types, such as STAP-A has to be handled in the mediaplayer
		fCurrentPacketCompletesFrame = true;
				
	}
	  	
	resultSpecialHeaderSize = expectedHeaderSize;
  	return true;
	
}

char const* H264VideoRTPSource::MIMEtype() const {
  return "video/H264";
}
