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
// A test program that reads a VOB file
// splits it into Audio (AC3) and Video (MPEG) Elementary Streams,
// and streams both using RTP.
// main program

#include "liveMedia.hh"
#include "AC3AudioStreamFramer.hh"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"

// To set up an internal RTSP server, uncomment the following:
#define IMPLEMENT_RTSP_SERVER 1
// (Note that this RTSP server works for multicast only)

char const* programName;
// Whether to stream *only* "I" (key) frames
// (e.g., to reduce network bandwidth):
Boolean iFramesOnly = False;

char const* inputFileName;

UsageEnvironment* env;
MPEGDemux* mpegDemux;
AC3AudioStreamFramer* audioSource;
FramedSource* videoSource;
RTPSink* audioSink = NULL;
RTPSink* videoSink = NULL;
#ifdef IMPLEMENT_RTSP_SERVER
RTSPServer* rtspServer = NULL;
unsigned short const defaultRTSPServerPortNum = 554;
unsigned short rtspServerPortNum = defaultRTSPServerPortNum;
#endif

void usage() {
  fprintf(stderr, "Usage: %s [-i] "
#ifdef IMPLEMENT_RTSP_SERVER
	  "[-p <RTSP-server-port-number>] "
#endif
	  "<path-to-VOB-file>\n", programName);
  exit(1);
}

void play(); // forward

int main(int argc, char** argv) {
  // Parse command-line options:
  // (Unfortunately we can't use getopt() here, as Windoze doesn't have it)
  programName = argv[0];
  while (argc > 2) {
    char* const opt = argv[1];
    if (opt[0] != '-') usage();
    switch (opt[1]) {

    case 'i': { // transmit video I-frames only
      iFramesOnly = True;
      break;
    }

    case 'p': { // specify port number for built-in RTSP server
      int portArg;
      if (sscanf(argv[2], "%d", &portArg) != 1) {
        usage();
      }
      if (portArg <= 0 || portArg >= 65536) {
        fprintf(stderr, "bad port number: %d "
		"(must be in the range (0,65536))\n", portArg);
        usage();
      }
      rtspServerPortNum = (unsigned short)portArg;
      ++argv; --argc;
      break;
    }

    default: {
      usage();
      break;
    }
    }

    ++argv; --argc;
  }
  if (argc != 2) usage();
  inputFileName = argv[1];

  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  // Create 'groupsocks' for RTP and RTCP:
  char* destinationAddressStr = "239.255.42.42";
  // Note: This is a multicast address.  If you wish to stream using
  // unicast instead, then replace this string with the unicast address
  // of the (single) destination.  (You may also need to make a similar
  // change to the receiver program.)

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

  // Create an 'AC3 Audio RTP' sink from the RTP 'groupsock':
  audioSink
    = AC3AudioRTPSink::createNew(*env, &rtpGroupsockAudio, 96, 0);
  // set the RTP timestamp frequency 'for real' later

  // Create (and start) a 'RTCP instance' for this RTP sink:
  const unsigned totalSessionBandwidthAudio = 160; // in kbps; for RTCP b/w share
  const unsigned maxCNAMElen = 100;
  unsigned char CNAME[maxCNAMElen+1];
  gethostname((char*)CNAME, maxCNAMElen);
  CNAME[maxCNAMElen] = '\0'; // just in case
  RTCPInstance::createNew(*env, &rtcpGroupsockAudio,
			  totalSessionBandwidthAudio, CNAME,
			  audioSink, NULL /* we're a server */);
  // Note: This starts RTCP running automatically

  // Create a 'MPEG Video RTP' sink from the RTP 'groupsock':
  videoSink = MPEGVideoRTPSink::createNew(*env, &rtpGroupsockVideo);

  // Create (and start) a 'RTCP instance' for this RTP sink:
  const unsigned totalSessionBandwidthVideo = 4500; // in kbps; for RTCP b/w share
  RTCPInstance::createNew(*env, &rtcpGroupsockVideo,
			  totalSessionBandwidthVideo, CNAME,
			  videoSink, NULL /* we're a server */);
  // Note: This starts RTCP running automatically

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
  FramedSource* audioES = mpegDemux->newElementaryStream(0xBD);
      // Because, in a VOB file, the AC3 audio has stream id 0xBD
  FramedSource* videoES = mpegDemux->newVideoStream();

  // Create a framer for each Elementary Stream:
  audioSource
    = AC3AudioStreamFramer::createNew(*env, audioES, 0x80);

  videoSource
    = MPEGVideoStreamFramer::createNew(*env, videoES, iFramesOnly);

  // Finally, start playing each sink.
  // (Start playing video first, to ensure that any video sequence header
  // at the start of the file gets read.)
  fprintf(stderr, "Beginning to read from file...\n");
  videoSink->startPlaying(*videoSource, afterPlaying, videoSink);

  audioSink->setRTPTimestampFrequency(audioSource->samplingRate());
  audioSink->startPlaying(*audioSource, afterPlaying, audioSink);

#ifdef IMPLEMENT_RTSP_SERVER
  if (rtspServer == NULL) {
    PassiveServerMediaSession* serverMediaSession = PassiveServerMediaSession
      ::createNew(*env, "Session streamed by \"testMPEGAudioVideoStreamer\"");
    serverMediaSession->addSubsession(*audioSink);
    serverMediaSession->addSubsession(*videoSink);
    rtspServer = RTSPServer::createNew(*env, *serverMediaSession,
				       rtspServerPortNum);
    if (rtspServer != NULL) {
      fprintf(stderr, "Created RTSP server.\n");

      // Display our "rtsp://" URL, for clients to connect to:
      struct in_addr ourIPAddress;
      ourIPAddress.s_addr = ourSourceAddressForMulticast(*env);
      char portStr[10];
      if (rtspServerPortNum == defaultRTSPServerPortNum) {
	portStr[0] = '\0';
      } else {
	sprintf(portStr, ":%d", rtspServerPortNum);
      }
      fprintf(stderr, "Access this stream using the URL:\n"
	      "\trtsp://%s%s/\n", our_inet_ntoa(ourIPAddress), portStr);
    } else {
      fprintf(stderr, "Failed to create RTSP server: %s\n",
	      env->getResultMsg());
      exit(1);
    }
  }
#endif
}
