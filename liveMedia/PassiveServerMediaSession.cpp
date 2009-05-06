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
// A 'ServerMediaSession' object that represents an existing 'RTPSink',
// rather than one that creates new 'RTPSink's (e.g., to handle RTSP
// commands).
// Implementation

#include "PassiveServerMediaSession.hh"
#include "GroupsockHelper.hh"

////////// PassiveServerMediaSession //////////

PassiveServerMediaSession*
PassiveServerMediaSession::createNew(UsageEnvironment& env,
				     char const* description,
				     char const* info,
				     Boolean isSSM) {
  return new PassiveServerMediaSession(env, description, info, isSSM);
}

static char const* const libraryNameString = "LIVE.COM Streaming Media";

PassiveServerMediaSession
::PassiveServerMediaSession(UsageEnvironment& env,
			    char const* description, char const* info,
			    Boolean isSSM)
  : ServerMediaSession(env, description, info, isSSM) {
}

PassiveServerMediaSession::~PassiveServerMediaSession() {
}

void PassiveServerMediaSession::addSubsession(RTPSink& rtpSink) {
  // Use the components from "rtpSink":
  Groupsock const& gs = rtpSink.groupsockBeingUsed();
  addSubsessionByComponents(gs.groupAddress(),
			    ntohs(gs.port().num()),
			    gs.ttl(), rtpSink.rtpTimestampFrequency(),
			    rtpSink.numChannels(),
			    rtpSink.rtpPayloadType(),
			    rtpSink.sdpMediaType(),
			    rtpSink.rtpPayloadFormatName(),
			    rtpSink.auxSDPLine());
}
			    
void PassiveServerMediaSession
::addSubsessionByComponents(struct in_addr const& ipAddress,
			    unsigned short portNum, unsigned char ttl,
			    unsigned rtpTimestampFrequency,
			    unsigned numChannels,
			    unsigned char rtpPayloadType,
			    char const* mediaType,
			    char const* rtpPayloadFormatName,
			    char const* auxSDPLine) {
  // Construct a set of SDP lines that describe this subsession:

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

  // Set up our 'track id':
  char trackIdBuffer[100];
  sprintf(trackIdBuffer, "track%d", ++fSubsessionCounter);

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
    + strlen(trackIdBuffer)
    + strlen(ipAddressStr) + 3 /* max char len */;
  char* sdpLine = new char[sdpFmtSize];
  sprintf(sdpLine, sdpFmt, 
	  mediaType, // m= <media>
	  portNum, // m= <port>
	  rtpPayloadType, // m= <fmt list>
	  rtpmapLine, // a=rtpmap:... (if present)
	  auxSDPLine, // optional extra SDP line
	  trackIdBuffer, // a=control:<track-id>
	  ipAddressStr, // c= <connection address>
	  ttl); // c= TTL
  delete[] ipAddressStr; delete[] rtpmapLine;

  // Finally, create a new subsession description:
  GroupEId const groupEId(ipAddress, portNum, ttl);
  ServerMediaSubsession* subsession
    = new ServerMediaSubsession(groupEId, trackIdBuffer, sdpLine);
  delete[] sdpLine;
  if (subsession == NULL) return;

  if (fSubsessionsHead == NULL) {
    fSubsessionsHead = fSubsessionsTail = subsession;
  } else {
    fSubsessionsTail->setNext(subsession);
    fSubsessionsTail = subsession;
  }
}
