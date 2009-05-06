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
// A 'ServerMediaSubsession' object that represents an existing
// 'RTPSink', rather than one that creates new 'RTPSink's on demand.
// Implementation

#include "PassiveServerMediaSubsession.hh"
#include <GroupsockHelper.hh>

////////// PassiveServerMediaSubsession //////////

PassiveServerMediaSubsession*
PassiveServerMediaSubsession::createNew(RTPSink& rtpSink,
					RTCPInstance* rtcpInstance) {
  return new PassiveServerMediaSubsession(rtpSink, rtcpInstance);
}
			    
PassiveServerMediaSubsession
::PassiveServerMediaSubsession(RTPSink& rtpSink, RTCPInstance* rtcpInstance)
  : ServerMediaSubsession(rtpSink.envir()),
    fRTPSink(rtpSink), fRTCPInstance(rtcpInstance), fSDPLines(NULL) {
}

char const* PassiveServerMediaSubsession::sdpLines() {
  if (fSDPLines == NULL ) {
    // Construct a set of SDP lines that describe this subsession:
    // Use the components from "rtpSink":
    Groupsock const& gs = fRTPSink.groupsockBeingUsed();
    struct in_addr const& ipAddress = gs.groupAddress();
    unsigned short portNum = ntohs(gs.port().num());
    unsigned char ttl = gs.ttl();
    unsigned rtpTimestampFrequency = fRTPSink.rtpTimestampFrequency();
    unsigned numChannels = fRTPSink.numChannels();
    unsigned char rtpPayloadType = fRTPSink.rtpPayloadType();
    char const* mediaType = fRTPSink.sdpMediaType();
    char const* rtpPayloadFormatName = fRTPSink.rtpPayloadFormatName();
    char const* auxSDPLine = fRTPSink.auxSDPLine();

    // For dynamic payload types, we need a "a=rtpmap:" line also:
    char* rtpmapLine;
    unsigned rtpmapLineSize;
    if (rtpPayloadType >= 96) {
      char* encodingParamsPart;
      if (numChannels != 1) {
	encodingParamsPart = new char[1 + 20 /* max int len */];
	sprintf(encodingParamsPart, "/%d", numChannels);
      } else {
	encodingParamsPart = strDup("");
      }
      char const* const rtpmapFmt = "a=rtpmap:%d %s/%d%s\r\n";
      unsigned rtpmapFmtSize = strlen(rtpmapFmt)
	+ 3 /* max char len */ + strlen(rtpPayloadFormatName)
	+ 20 /* max int len */ + strlen(encodingParamsPart);
      rtpmapLine = new char[rtpmapFmtSize];
      sprintf(rtpmapLine, rtpmapFmt,
	      rtpPayloadType, rtpPayloadFormatName,
	      rtpTimestampFrequency, encodingParamsPart);
      rtpmapLineSize = strlen(rtpmapLine);
      delete[] encodingParamsPart;
    } else {
      // There's no "a=rtpmap:" line:
      // Static payload type => no "a=rtpmap:" line
      rtpmapLine = strDup("");
      rtpmapLineSize = 0;
    }
    
    unsigned auxSDPLineSize;
    if (auxSDPLine == NULL) {
      auxSDPLine = "";
      auxSDPLineSize = 0;
    } else {
      auxSDPLineSize = strlen(auxSDPLine);
    }
    
    char* const ipAddressStr = strDup(our_inet_ntoa(ipAddress));
    
    char const* const sdpFmt =
      "m=%s %d RTP/AVP %d\r\n"
      "c=IN IP4 %s/%d\r\n" 
      "%s"
      "%s"
      "a=control:%s\r\n";
    unsigned sdpFmtSize = strlen(sdpFmt)
      + strlen(mediaType) + 5 /* max short len */ + 3 /* max char len */
      + strlen(ipAddressStr) + 3 /* max char len */
      + rtpmapLineSize
      + auxSDPLineSize
      + strlen(trackId());
    char* sdpLines = new char[sdpFmtSize];
    sprintf(sdpLines, sdpFmt, 
	    mediaType, // m= <media>
	    portNum, // m= <port>
	    rtpPayloadType, // m= <fmt list>
	    ipAddressStr, // c= <connection address>
	    ttl, // c= TTL
	    rtpmapLine, // a=rtpmap:... (if present)
	    auxSDPLine, // optional extra SDP line
	    trackId()); // a=control:<track-id>
    delete[] ipAddressStr; delete[] rtpmapLine;
    
    fSDPLines = strDup(sdpLines);
    delete[] sdpLines;
  }

  return fSDPLines;
}

void PassiveServerMediaSubsession
::getStreamParameters(unsigned /*clientSessionId*/,
		      netAddressBits /*clientAddress*/,
		      Port const& /*clientRTPPort*/,
		      Port const& /*clientRTCPPort*/,
		      int /*tcpSocketNum*/,
		      unsigned char /*rtpChannelId*/,
		      unsigned char /*rtcpChannelId*/,
		      netAddressBits& destinationAddress,
		      u_int8_t& destinationTTL,
		      Boolean& isMulticast,
		      Port& serverRTPPort,
		      Port& serverRTCPPort,
		      void*& streamToken) {
  isMulticast = True;
  Groupsock& gs = fRTPSink.groupsockBeingUsed();
  if (destinationTTL == 255) destinationTTL = gs.ttl();
  if (destinationAddress == 0) { // normal case
    destinationAddress = gs.groupAddress().s_addr;
  } else { // use the client-specified destination address instead:
    struct in_addr destinationAddr; destinationAddr.s_addr = destinationAddress;
    gs.changeDestinationParameters(destinationAddr, 0, destinationTTL);
    if (fRTCPInstance != NULL) {
      Groupsock* rtcpGS = fRTCPInstance->RTCPgs();
      rtcpGS->changeDestinationParameters(destinationAddr, 0, destinationTTL);
    }
  }
  serverRTPPort = gs.port();
  // serverRTCPPort is not needed, so we don't set it
  streamToken = NULL; // not used
}

PassiveServerMediaSubsession::~PassiveServerMediaSubsession() {
}
