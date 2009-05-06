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
// Copyright (c) 1996-2001, Live Networks, Inc.  All rights reserved
// A test program for receiving a MCT SEC/SLAP stream
// main program

#include "liveMedia.hh"

#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"

#include <string.h>
#if defined(__WIN32__) || defined(_WIN32)
#else
#include <unistd.h>
#endif

void afterPlaying(void* clientData); // forward

// A structure to hold the state of the current session.
// It is used in the "afterPlaying()" function to clean up the session.
struct sessionState_t {
  MediaSource* source;
  MediaSink* sink;
} sessionState;

void addNewRTPStream(UsageEnvironment& env,
		     PrioritizedRTPStreamSelector* selector,
		     char* sessionAddressStr,
		     unsigned short rtpPortNum, unsigned char ttl,
		     unsigned bandwidth /*kbps*/);
    // forward

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  BasicTaskScheduler scheduler;
  UsageEnvironment* env = new BasicUsageEnvironment(scheduler);

  // Create the data sink: a local HTTP server (on port 10006):
  sessionState.sink = HTTPSink::createNew(*env, 10006);

  // Create the data source: a "Prioritized RTP Stream Selector"
  // To compute the sequence number stagger, assume
  // - one MP3 frame per packet
  // - 1152 samples per MP3 frame (defined by MP3 standard)
  // - a sampling frequency of 44100 Hz
  // - a desired time stagger of 5 seconds
  unsigned const samplesPerFrame = 1152;
  unsigned samplesPerSecond = 44100;
  double secondsPerFrame = (double)samplesPerFrame/samplesPerSecond;
  unsigned timeStaggerSeconds = 5;
  unsigned seqNumStagger = (unsigned)(timeStaggerSeconds/secondsPerFrame);
  PrioritizedRTPStreamSelector* s
    = PrioritizedRTPStreamSelector::createNew(*env, seqNumStagger);
  sessionState.source = s;

  // Create and add each of the MCT substreams, highest priority first:
  addNewRTPStream(*env, s, "233.64.133.30", 7000, 255, 160);
  addNewRTPStream(*env, s, "233.64.133.31", 7002, 255, 96);
  // NOTE: Don't add the lowest-priority 32 kbps stream, because it's
  // mono rather than stereo, and Winamp hangs if a mono frame ever appears
  // in the middle of a stereo stream.  If you really want this, uncomment:
  //addNewRTPStream(*env, s, "233.64.133.32", 7004, 255, 32);

  // Finally, start receiving the multicast streams:
  fprintf(stderr, "Beginning receiving multicast streams...\n");
  sessionState.sink->startPlaying(*sessionState.source, afterPlaying, NULL);

  env->taskScheduler().blockMyself(); // does not return

  return 0; // only to prevent compiler warning
}

void addNewRTPStream(UsageEnvironment& env,
		     PrioritizedRTPStreamSelector* selector,
		     char* sessionAddressStr,
		     unsigned short rtpPortNum, unsigned char ttl,
		     unsigned bandwidth /*kbps*/) {
  // Create 'groupsocks' for RTP and RTCP:
  const unsigned short rtcpPortNum = rtpPortNum+1;
  
  struct in_addr sessionAddress;
  sessionAddress.s_addr = our_inet_addr(sessionAddressStr);
  const Port rtpPort(rtpPortNum);
  const Port rtcpPort(rtcpPortNum);
  
  Groupsock* rtpGroupsock
    = new Groupsock(env, sessionAddress, rtpPort, ttl);
  Groupsock* rtcpGroupsock
    = new Groupsock(env, sessionAddress, rtcpPort, ttl);
  
  // Create the data source: a "MPEG Audio RTP source"
  RTPSource* rtpSource = MPEGAudioRTPSource::createNew(env, rtpGroupsock);

  // Create (and start) a 'RTCP instance' for the RTP source:
  const unsigned maxCNAMElen = 100;
  unsigned char CNAME[maxCNAMElen+1];
  gethostname((char*)CNAME, maxCNAMElen);
  CNAME[maxCNAMElen] = '\0'; // just in case
  RTCPInstance* rtcpInstance
    = RTCPInstance::createNew(env, rtcpGroupsock,
			      bandwidth, CNAME,
			      NULL /* we're a client */, rtpSource);
      // Note: This starts RTCP running automatically

  // Finally, insert the new stream into our selector:
  selector->addInputRTPStream(rtpSource, rtcpInstance);
}


void afterPlaying(void* /*clientData*/) {
  fprintf(stderr, "...done receiving\n");

  // End by closing the media:
  Medium::close(sessionState.sink);
  Medium::close(sessionState.source);
}



