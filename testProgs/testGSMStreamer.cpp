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
// Copyright (c) 1996-2000, Live Networks, Inc.  All rights reserved
// A test program that streams GSM audio via RTP/RTCP
// main program

// NOTE: This program assumes the existence of a (currently nonexistent)
// function called "createNewGSMAudioSource()".

#include "liveMedia.hh"
#include "GroupsockHelper.hh"

#include "BasicUsageEnvironment.hh"

////////// Main program //////////

UsageEnvironment* env;

void play(); // forward

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  play(); // does not return

  return 0; // only to prevent compiler warning
}

PassiveServerMediaSession* serverMediaSession;
RTSPServer* rtspServer;

void afterPlaying(void* clientData); // forward

// A structure to hold the state of the current session.
// It is used in the "afterPlaying()" function to clean up the session.
struct sessionState_t {
  FramedSource* source;
  RTPSink* sink;
  RTCPInstance* rtcpInstance;
  Groupsock* rtpGroupsock;
  Groupsock* rtcpGroupsock;
} sessionState;

void play() {
  // Open the input source:
  extern FramedSource* createNewGSMAudioSource(UsageEnvironment&);
  sessionState.source = createNewGSMAudioSource(*env);
  if (sessionState.source == NULL) {
    fprintf(stderr, "Failed to create GSM source\n");
    exit(1);
  }
  
  // Create 'groupsocks' for RTP and RTCP:
  char* destinationAddressStr
#ifdef USE_SSM
    = "232.255.42.42";
#else
    = "239.255.42.42";
  // Note: This is a multicast address.  If you wish to stream using
  // unicast instead, then replace this string with the unicast address
  // of the (single) destination.  (You may also need to make a similar
  // change to the receiver program.)
#endif
  const unsigned short rtpPortNum = 6666;
  const unsigned short rtcpPortNum = rtpPortNum+1;
  const unsigned char ttl = 1; // low, in case routers don't admin scope
  
  struct in_addr destinationAddress;
  destinationAddress.s_addr = our_inet_addr(destinationAddressStr);
  const Port rtpPort(rtpPortNum);
  const Port rtcpPort(rtcpPortNum);
  
  sessionState.rtpGroupsock
    = new Groupsock(*env, destinationAddress, rtpPort, ttl);
  sessionState.rtcpGroupsock
    = new Groupsock(*env, destinationAddress, rtcpPort, ttl);
  
  // Create a 'GSM RTP' sink from the RTP 'groupsock':
  sessionState.sink
    = GSMAudioRTPSink::createNew(*env, sessionState.rtpGroupsock);
  
  // Create (and start) a 'RTCP instance' for this RTP sink:
  const unsigned totalSessionBandwidth = 160; // in kbps; for RTCP b/w share
  const unsigned maxCNAMElen = 100;
  unsigned char CNAME[maxCNAMElen+1];
  gethostname((char*)CNAME, maxCNAMElen);
  CNAME[maxCNAMElen] = '\0'; // just in case
  sessionState.rtcpInstance
    = RTCPInstance::createNew(*env, sessionState.rtcpGroupsock,
			      totalSessionBandwidth, CNAME,
			      sessionState.sink, NULL /* we're a server */);
  // Note: This starts RTCP running automatically

  serverMediaSession = PassiveServerMediaSession
    ::createNew(*env, "Session streamed by \"testGSMStreamer\"");
  rtspServer = RTSPServer::createNew(*env, *serverMediaSession, 7070);
  if (rtspServer == NULL) {
    fprintf(stderr, "Failed to create RTSP server: %s\n",
	    env->getResultMsg());
    exit(1);
  }

  // Finally, start the streaming:
  fprintf(stderr, "Beginning streaming...\n");
  sessionState.sink->startPlaying(*sessionState.source, afterPlaying, NULL);

  env->taskScheduler().doEventLoop();
}


void afterPlaying(void* /*clientData*/) {
  fprintf(stderr, "...done streaming\n");

  // End this loop by closing the media:
  Medium::close(rtspServer);
  Medium::close(sessionState.sink);
  delete sessionState.rtpGroupsock;
  Medium::close(sessionState.source);
  Medium::close(sessionState.rtcpInstance);
  delete sessionState.rtcpGroupsock;

  // And start another loop:
  play();
}


////////// GSMAudioRTPSink implementation //////////

GSMAudioRTPSink::GSMAudioRTPSink(UsageEnvironment& env, Groupsock* RTPgs)
  : MultiFramedRTPSink(env, RTPgs, 3, 8000, "GSM") {
}

GSMAudioRTPSink::~GSMAudioRTPSink() {
}

GSMAudioRTPSink*
GSMAudioRTPSink::createNew(UsageEnvironment& env, Groupsock* RTPgs) {
  return new GSMAudioRTPSink(env, RTPgs);
}

char const* GSMAudioRTPSink::sdpMediaType() const {
  return "audio";
}
