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
// Copyright (c) 1996-2003, Live Networks, Inc.  All rights reserved
// A test program that reads a MPEG-4 Video Elementary Stream file,
// and streams it using RTP
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"

UsageEnvironment* env;
char const* inputFileName = "test.m4v";
MPEG4VideoStreamFramer* videoSource;
RTPSink* videoSink;

void play(); // forward
void startRTSPServerWhenReady(void* clientData = NULL); // forward

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  // Create 'groupsocks' for RTP and RTCP:
  struct in_addr destinationAddress;
  destinationAddress.s_addr = chooseRandomIPv4SSMAddress(*env);
  // Note: This is a multicast address.  If you wish to stream using
  // unicast instead, then replace this with the unicast address
  // of the (single) destination.  (You may also need to make a similar
  // change to the receiver program.)

  const unsigned short rtpPortNum = 18888;
  const unsigned short rtcpPortNum = rtpPortNum+1;
  const unsigned char ttl = 255;

  const Port rtpPort(rtpPortNum);
  const Port rtcpPort(rtcpPortNum);

  Groupsock rtpGroupsock(*env, destinationAddress, rtpPort, ttl);
  rtpGroupsock.multicastSendOnly(); // we're a SSM source
  Groupsock rtcpGroupsock(*env, destinationAddress, rtcpPort, ttl);
  rtcpGroupsock.multicastSendOnly(); // we're a SSM source

  // Create a 'MPEG-4 Video RTP' sink from the RTP 'groupsock':
  videoSink = MPEG4ESVideoRTPSink::createNew(*env, &rtpGroupsock, 96);

  // Create (and start) a 'RTCP instance' for this RTP sink:
  const unsigned totalSessionBandwidth = 500; // in kbps; for RTCP b/w share
  const unsigned maxCNAMElen = 100;
  unsigned char CNAME[maxCNAMElen+1];
  gethostname((char*)CNAME, maxCNAMElen);
  CNAME[maxCNAMElen] = '\0'; // just in case
  RTCPInstance::createNew(*env, &rtcpGroupsock,
			  totalSessionBandwidth, CNAME,
			  videoSink, NULL /* we're a server */,
			  True /* we're a SSM source */);
  // Note: This starts RTCP running automatically

  // Start the streaming:
  *env << "Beginning streaming...\n";
  play();

  // Create and start a RTSP server to serve this stream.
  // (We do this after we've started playing, so that the sink object
  // knows about its source framer object, and can get parameters from it.)
  startRTSPServerWhenReady();

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}

void afterPlaying(void* /*clientData*/) {
  *env << "...done reading from file\n";

  Medium::close(videoSource);
  // Note that this also closes the input file that this source read from.

  play();
}

void play() {
  // Open the input file as a 'byte-stream file source':
  ByteStreamFileSource* fileSource
    = ByteStreamFileSource::createNew(*env, inputFileName);
  if (fileSource == NULL) {
    *env << "Unable to open file \"" << inputFileName
	 << "\" as a byte-stream file source\n";
    exit(1);
  }
  
  FramedSource* videoES = fileSource;

  // Create a framer for the Video Elementary Stream:
  videoSource = MPEG4VideoStreamFramer::createNew(*env, videoES);
  
  // Finally, start playing:
  *env << "Beginning to read from file...\n";
  videoSink->startPlaying(*videoSource, afterPlaying, videoSink);
}

void startRTSPServerWhenReady(void* /*clientData*/) {
  // Wait until the video framer source object has read enough data to
  // construct a 'config' string.  Then, create and start a RTSP server
  // (which will use this config string).

  unsigned configLength;
  if (videoSource->getConfigBytes(configLength) == NULL) {
    // The video framer is not ready; try again after a short delay:
    env->taskScheduler().scheduleDelayedTask(100000/* 100 ms*/,
			     (TaskFunc*)startRTSPServerWhenReady, NULL);
    return;
  }

  PassiveServerMediaSession* serverMediaSession
    = PassiveServerMediaSession::createNew(*env, inputFileName,
		   "Session streamed by \"testMPEG4VideoStreamer\"",
					   True /*SSM*/);
  serverMediaSession->addSubsession(*videoSink);
  RTSPServer* rtspServer
    = RTSPServer::createNew(*env, *serverMediaSession, 7070);
  if (rtspServer == NULL) {
    *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
    exit(1);
  } else {
    char* url = rtspServer->rtspURL();
    *env << "Play this stream using the URL \"" << url << "\"\n";
    delete[] url;
  }
}
