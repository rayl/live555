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
// A test program that receives a RTP/RTCP multicast MPEG video stream,
// and outputs the resulting MPEG file stream to 'stdout'
// main program

#include "liveMedia.hh"
#include "GroupsockHelper.hh"

#include "BasicUsageEnvironment.hh"

// To receive a "source-specific multicast" (SSM) stream, uncomment this:
//#define USE_SSM 1

void afterPlaying(void* clientData); // forward

// A structure to hold the state of the current session.
// It is used in the "afterPlaying()" function to clean up the session.
struct sessionState_t {
  RTPSource* source;
  MediaSink* sink;
  RTCPInstance* rtcpInstance;
} sessionState;

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

  // Create the data sink for 'stdout':
  sessionState.sink = FileSink::createNew(*env, "stdout");
  // Note: The string "stdout" is handled as a special case.
  // A real file name could have been used instead.

  // Create 'groupsocks' for RTP and RTCP:
  char* sessionAddressStr
#ifdef USE_SSM
    = "232.255.42.42";
#else
    = "239.255.42.42";
  // Note: If the session is unicast rather than multicast,
  // then replace this string with the (unicast) address of the *source*.
  // (This will cause RTCP Reception Reports to be sent back to the source)
#endif
  const unsigned short rtpPortNum = 8888;
  const unsigned short rtcpPortNum = rtpPortNum+1;
  const unsigned char ttl
#ifdef USE_SSM
    = 0; // RTCP RRs don't go anywhere
#else
    = 1; // low, in case routers don't admin scope
#endif
  
  struct in_addr sessionAddress;
  sessionAddress.s_addr = our_inet_addr(sessionAddressStr);
  const Port rtpPort(rtpPortNum);
  const Port rtcpPort(rtcpPortNum);
  
#ifdef USE_SSM
  char* sourceAddressStr = "aaa.bbb.ccc.ddd";
                           // replace this with the real source address
  struct in_addr sourceFilterAddress;
  sourceFilterAddress.s_addr = our_inet_addr(sourceAddressStr);

  Groupsock rtpGroupsock(*env, sessionAddress, sourceFilterAddress, rtpPort);
#else
  Groupsock rtpGroupsock(*env, sessionAddress, rtpPort, ttl);
#endif
  Groupsock rtcpGroupsock(*env, sessionAddress, rtcpPort, ttl);
  
  // Create the data source: a "MPEG Video RTP source"
  sessionState.source = MPEGVideoRTPSource::createNew(*env, &rtpGroupsock);

  // Create (and start) a 'RTCP instance' for the RTP source:
  const unsigned totalSessionBandwidth = 160; // in kbps; for RTCP b/w share
  const unsigned maxCNAMElen = 100;
  unsigned char CNAME[maxCNAMElen+1];
  gethostname((char*)CNAME, maxCNAMElen);
  CNAME[maxCNAMElen] = '\0'; // just in case
  sessionState.rtcpInstance
    = RTCPInstance::createNew(*env, &rtcpGroupsock,
			      totalSessionBandwidth, CNAME,
			      NULL /* we're a client */, sessionState.source);
  // Note: This starts RTCP running automatically

  // Finally, start receiving the multicast stream:
  fprintf(stderr, "Beginning receiving multicast stream...\n");
  sessionState.sink->startPlaying(*sessionState.source, afterPlaying, NULL);

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}


void afterPlaying(void* /*clientData*/) {
  fprintf(stderr, "...done receiving\n");

  // End by closing the media:
  Medium::close(sessionState.sink);
  Medium::close(sessionState.source);
  Medium::close(sessionState.rtcpInstance); // Note: Sends a RTCP BYE
}
