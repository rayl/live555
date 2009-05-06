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
// A 'ServerMediaSubsession' object that represents an existing
// 'RTPSink', rather than one that creates new 'RTPSink's on demand.
// Implementation

#include "PassiveServerMediaSubsession.hh"
#include <GroupsockHelper.hh>

////////// PassiveServerMediaSubsession //////////

PassiveServerMediaSubsession*
PassiveServerMediaSubsession::createNew(RTPSink& rtpSink) {
  return new PassiveServerMediaSubsession(rtpSink);
}
			    
PassiveServerMediaSubsession
::PassiveServerMediaSubsession(RTPSink& rtpSink)
  : ServerMediaSubsession(rtpSink.envir()),
    fRTPSink(rtpSink), fSDPLines(NULL) {
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
      "%s"
      "%s"
      "a=control:%s\r\n"
      "c=IN IP4 %s/%d\r\n"; 
    unsigned sdpFmtSize = strlen(sdpFmt)
      + strlen(mediaType) + 5 /* max short len */ + 3 /* max char len */
      + rtpmapLineSize
      + auxSDPLineSize
      + strlen(trackId())
      + strlen(ipAddressStr) + 3 /* max char len */;
    char* sdpLines = new char[sdpFmtSize];
    sprintf(sdpLines, sdpFmt, 
	    mediaType, // m= <media>
	    portNum, // m= <port>
	    rtpPayloadType, // m= <fmt list>
	    rtpmapLine, // a=rtpmap:... (if present)
	    auxSDPLine, // optional extra SDP line
	    trackId(), // a=control:<track-id>
	    ipAddressStr, // c= <connection address>
	    ttl); // c= TTL
    delete[] ipAddressStr; delete[] rtpmapLine;
    
    fSDPLines = strDup(sdpLines);
    delete[] sdpLines;
  }

  return fSDPLines;
}

void PassiveServerMediaSubsession
::getStreamParameters(struct sockaddr_in /*clientAddress*/,
		      Port const& /*clientRTPPort*/,
		      Port const& /*clientRTCPPort*/,
		      GroupEId& groupEId, Boolean& isMulticast,
		      void*& streamToken) {
  Groupsock const& gs = fRTPSink.groupsockBeingUsed();
  groupEId = GroupEId(gs.groupAddress(), ntohs(gs.port().num()), gs.ttl());
  isMulticast = True;
  streamToken = NULL; // not used
}

PassiveServerMediaSubsession::~PassiveServerMediaSubsession() {
}
