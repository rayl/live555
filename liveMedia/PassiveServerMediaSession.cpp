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

#if defined(__WIN32__) || defined(_WIN32)
#define snprintf _snprintf
#endif

////////// PassiveServerMediaSession //////////

PassiveServerMediaSession*
PassiveServerMediaSession::createNew(UsageEnvironment& env,
			      char const* description, char const* info) {
  return new PassiveServerMediaSession(env, description, info);
}

static char const* const libraryNameString = "LIVE.COM Streaming Media";

PassiveServerMediaSession::PassiveServerMediaSession(UsageEnvironment& env,
						    char const* description,
						    char const* info)
  : ServerMediaSession(env, description, info) {
}

PassiveServerMediaSession::~PassiveServerMediaSession() {
}

void PassiveServerMediaSession::addSubsession(RTPSink& rtpSink) {
  // Use the components from "rtpSink":
  Groupsock const& gs = rtpSink.groupsockBeingUsed();
  addSubsessionByComponents(gs.groupAddress(),
			    ntohs(gs.port().num()),
			    gs.ttl(), rtpSink.rtpTimestampFrequency(),
			    rtpSink.rtpPayloadType(),
			    rtpSink.sdpMediaType(),
			    rtpSink.rtpPayloadFormatName());
}
			    
void PassiveServerMediaSession
::addSubsessionByComponents(struct in_addr const& ipAddress,
			    unsigned short portNum, unsigned char ttl,
			    unsigned rtpTimestampFrequency,
			    unsigned char rtpPayloadType,
			    char const* mediaType,
			    char const* rtpPayloadFormatName) {
  // Construct a set of SDP lines that describe this subsession:

  // For dynamic payload types, we need a "a=rtpmap:" line also:
  char rtpmapBuffer[100];
  if (rtpPayloadType >= 96) {
    char const* rtpmapFormat = "a=rtpmap:%d %s/%d\r\n";
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4)
    // snprintf() isn't defined, so just use sprintf().  Warning!
    sprintf(rtpmapBuffer, rtpmapFormat,
	    rtpPayloadType, rtpPayloadFormatName, rtpTimestampFrequency);
#else
    snprintf(rtpmapBuffer, sizeof rtpmapBuffer, rtpmapFormat,
	     rtpPayloadType, rtpPayloadFormatName, rtpTimestampFrequency);
#endif    
  } else {
    // There's no "a=rtpmap:" line:
    rtpmapBuffer[0] = '\0';
  }

  // Set up our 'track id':
  char trackIdBuffer[100];
  sprintf(trackIdBuffer, "track%d", ++fSubsessionCounter);

  char sdpBuffer[1000];
  char const* sdpFormat
    = "m=%s %d RTP/AVP %d\r\n%sa=control:%s\r\nc=IN IP4 %s/%d\r\n"; 
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4)
    // snprintf() isn't defined, so just use sprintf().  Warning!
  sprintf(sdpBuffer, sdpFormat, 
	  mediaType, // m= <media>
	  portNum, // m= <port>
	  rtpPayloadType, // m= <fmt list>
	  rtpmapBuffer, // a=rtpmap:... (if present)
	  trackIdBuffer, // a=control:<track-id>
	  our_inet_ntoa(ipAddress), // c= <connection address>
	  ttl); // c= TTL
#else
  snprintf(sdpBuffer, sizeof sdpBuffer, sdpFormat, 
	   mediaType, // m= <media>
	   portNum, // m= <port>
	   rtpPayloadType, // m= <fmt list>
	   rtpmapBuffer, // a=rtpmap:... (if present)
	   trackIdBuffer, // a=control:<track-id>
	   our_inet_ntoa(ipAddress), // c= <connection address>
	   ttl); // c= TTL
#endif    

  // Finally, create a new subsession description:
  GroupEId const groupEId(ipAddress, portNum, ttl);
  ServerMediaSubsession* subsession
    = new ServerMediaSubsession(groupEId, trackIdBuffer, sdpBuffer);
  if (subsession == NULL) return;

  if (fSubsessionsHead == NULL) {
    fSubsessionsHead = fSubsessionsTail = subsession;
  } else {
    fSubsessionsTail->setNext(subsession);
    fSubsessionsTail = subsession;
  }
}
