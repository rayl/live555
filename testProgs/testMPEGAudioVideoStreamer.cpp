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
// A test program that reads a MPEG Program Stream file,
// splits it into Audio and Video Elementary Streams,
// and streams both using RTP
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"

UsageEnvironment* env;
char const* inputFileName = "test.mpg";
MPEGDemux* mpegDemux;
FramedSource* audioSource;
FramedSource* videoSource;
RTPSink* audioSink;
RTPSink* videoSink;

void play(); // forward

// To stream using "source-specific multicast" (SSM), uncomment the following:
//#define USE_SSM 1
#ifdef USE_SSM
Boolean const isSSM = True;
#else
Boolean const isSSM = False;
#endif

// To set up an internal RTSP server, uncomment the following:
//#define IMPLEMENT_RTSP_SERVER 1
// (Note that this RTSP server works for multicast only)

// To stream *only* MPEG "I" frames (e.g., to reduce network bandwidth),
// change the following "False" to "True":
Boolean iFramesOnly = False;

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

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
  const unsigned short rtpPortNumAudio = 6666;
  const unsigned short rtcpPortNumAudio = rtpPortNumAudio+1;
  const unsigned short rtpPortNumVideo = 8888;
  const unsigned short rtcpPortNumVideo = rtpPortNumVideo+1;
  const unsigned char ttl = 7; // low, in case routers don't admin scope

  struct in_addr destinationAddress;
  destinationAddress.s_addr = our_inet_addr(destinationAddressStr);
  const Port rtpPortAudio(rtpPortNumAudio);
  const Port rtcpPortAudio(rtcpPortNumAudio);
  const Port rtpPortVideo(rtpPortNumVideo);
  const Port rtcpPortVideo(rtcpPortNumVideo);

  Groupsock rtpGroupsockAudio(*env, destinationAddress, rtpPortAudio, ttl);
  Groupsock rtcpGroupsockAudio(*env, destinationAddress, rtcpPortAudio, ttl);
  Groupsock rtpGroupsockVideo(*env, destinationAddress, rtpPortVideo, ttl);
  Groupsock rtcpGroupsockVideo(*env, destinationAddress, rtcpPortVideo, ttl);
#ifdef USE_SSM
  rtpGroupsockAudio.multicastSendOnly();
  rtcpGroupsockAudio.multicastSendOnly();
  rtpGroupsockVideo.multicastSendOnly();
  rtcpGroupsockVideo.multicastSendOnly();
#endif

  // Create a 'MPEG Audio RTP' sink from the RTP 'groupsock':
  audioSink = MPEGAudioRTPSink::createNew(*env, &rtpGroupsockAudio);

  // Create (and start) a 'RTCP instance' for this RTP sink:
  const unsigned totalSessionBandwidthAudio = 160; // in kbps; for RTCP b/w share
  const unsigned maxCNAMElen = 100;
  unsigned char CNAME[maxCNAMElen+1];
  gethostname((char*)CNAME, maxCNAMElen);
  CNAME[maxCNAMElen] = '\0'; // just in case
  RTCPInstance::createNew(*env, &rtcpGroupsockAudio,
			  totalSessionBandwidthAudio, CNAME,
			  audioSink, NULL /* we're a server */, isSSM);
  // Note: This starts RTCP running automatically

  // Create a 'MPEG Video RTP' sink from the RTP 'groupsock':
  videoSink = MPEGVideoRTPSink::createNew(*env, &rtpGroupsockVideo);

  // Create (and start) a 'RTCP instance' for this RTP sink:
  const unsigned totalSessionBandwidthVideo = 4500; // in kbps; for RTCP b/w share
  RTCPInstance::createNew(*env, &rtcpGroupsockVideo,
			  totalSessionBandwidthVideo, CNAME,
			  videoSink, NULL /* we're a server */, isSSM);
  // Note: This starts RTCP running automatically

#ifdef IMPLEMENT_RTSP_SERVER
  PassiveServerMediaSession* serverMediaSession
    = PassiveServerMediaSession::createNew(*env, inputFileName,
		   "Session streamed by \"testMPEGAudioVideoStreamer\"",
					   isSSM);
  serverMediaSession->addSubsession(*audioSink);
  serverMediaSession->addSubsession(*videoSink);
  RTSPServer* rtspServer
    = RTSPServer::createNew(*env, *serverMediaSession);
  // Note that this (attempts to) start a server on the default RTSP server
  // port: 554.  To use a different port number, add it as an extra
  // (optional) parameter to the "RTSPServer::createNew()" call above.
  if (rtspServer == NULL) {
    fprintf(stderr, "Failed to create RTSP server: %s\n",
            env->getResultMsg());
    exit(1);
  }
#endif

  // Finally, start the streaming:
  fprintf(stderr, "Beginning streaming...\n");
  play();

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}

void afterPlaying(void* clientData) {
  // One of the sinks has ended playing.
  // Check whether any of the sources have a pending read.  If so,
  // wait until its sink ends playing also:
  if (audioSource->isCurrentlyAwaitingData()
      || videoSource->isCurrentlyAwaitingData()) return;
  
  // Now that both sinks have ended, close both input sources,
  // and start playing again:
  fprintf(stderr, "...done reading from file\n");

  audioSink->stopPlaying();
  videoSink->stopPlaying();
      // ensures that both are shut down
  Medium::close(audioSource);
  Medium::close(videoSource);
  Medium::close(mpegDemux);
  // Note: This also closes the input file that this source read from.

  // Start playing once again:
  play();
}

void play() {
  // Open the input file as a 'byte-stream file source':
  ByteStreamFileSource* fileSource
    = ByteStreamFileSource::createNew(*env, inputFileName);
  if (fileSource == NULL) {
    fprintf(stderr, "Unable to open file \"%s\" as a byte-stream file source\n",
	    inputFileName);
    exit(1);
  }
  
  // We must demultiplex Audio and Video Elementary Streams
  // from the input source:
  mpegDemux = MPEGDemux::createNew(*env, fileSource);
  FramedSource* audioES = mpegDemux->newAudioStream();
  FramedSource* videoES = mpegDemux->newVideoStream();

  // Create a framer for each Elementary Stream:
  audioSource
    = MPEGAudioStreamFramer::createNew(*env, audioES);
  videoSource
    = MPEGVideoStreamFramer::createNew(*env, videoES, iFramesOnly);

  // Finally, start playing each sink.
  // (Start playing video first, to ensure that any video sequence header
  // at the start of the file gets read.)
  fprintf(stderr, "Beginning to read from file...\n");
  videoSink->startPlaying(*videoSource, afterPlaying, videoSink);
  audioSink->startPlaying(*audioSource, afterPlaying, audioSink);
}
