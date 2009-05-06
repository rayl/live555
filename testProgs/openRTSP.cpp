// Comment out the following to stop this program expiring:
#define EXPIRATION 1036137600 /* 11/1/2002 */

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
// A RTSP client test program that opens a RTSP URL argument,
// and extracts the data from each incoming RTP stream.
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"
#if defined(__WIN32__) || defined(_WIN32)
#define snprintf _snprintf
#else
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#define USE_SIGNALS 1
#endif

// Forward function definitions:
void setupRTSPStreams();
void startPlayingRTSPStreams();
void tearDownRTSPStreams();
void closeMediaSinks();
void subsessionAfterPlaying(void* clientData);
void subsessionByeHandler(void* clientData);
void sessionAfterPlaying(void* clientData = NULL);
void sessionTimerHandler(void* clientData);
void shutdown(int exitCode = 1);
void signalHandlerShutdown(int sig);
void checkForPacketArrival(void* clientData);

char const* progName;
UsageEnvironment* env;
RTSPClient* rtspClient = NULL;
MediaSession* session = NULL;
TaskToken currentTimerTask = NULL;
Boolean createReceivers = True;
Boolean outputQuickTimeFile = False;
QuickTimeFileSink* qtOut = NULL;
Boolean audioOnly = False;
Boolean videoOnly = False;
char const* singleMedium = NULL;
float endTime = 0;
float endTimeSlop = 5.0; // extra seconds to delay
Boolean playContinuously = False;
int simpleRTPoffsetArg = -1;
Boolean notifyOnPacketArrival = False;
Boolean streamUsingTCP = False;
char* username = NULL;
char* password = NULL;
unsigned short movieWidth = 240;
unsigned short movieHeight = 180;
unsigned movieFPS = 15;
Boolean packetLossCompensate = False;
Boolean syncStreams = False;

#ifdef BSD
static struct timezone Idunno;
#else
static int Idunno;
#endif
struct timeval startTime;

void usage() {
  fprintf(stderr, "Usage: %s [-p <startPortNum>] [-r|-q] [-a|-v] [-V] [-e <endTime>] [-c] [-s <offset>] [-n] [-t] [-u <username> <password>] [-w <width> -h <height>] [-f <frames-per-second>] [-y] <url>\n", progName);
  shutdown();
}

int main(int argc, char** argv) {
  progName = argv[0];

  gettimeofday(&startTime, &Idunno);
#ifdef EXPIRATION
  if (startTime.tv_sec > EXPIRATION) {
    fprintf(stderr, "\007This version of \"%s\" is out-of-date.  To download an up-to-date\nversion, visit <http://www.live.com/openRTSP/>, or build a new version from\nthe \"LIVE.COM Streaming Media\" source code at <http://live.sourceforge.net/>.  (To stop the program from expiring, comment out the '#define' at the start of \"openRTSP.cpp\".)\n", progName);
    exit(0);
  }
#endif

#ifdef USE_SIGNALS
  // Allow ourselves to be shut down gracefully by a SIGHUP or a SIGUSR1:
  signal(SIGHUP, signalHandlerShutdown);
  signal(SIGUSR1, signalHandlerShutdown);
#endif

  unsigned short desiredPortNum = 0;
  int verbosityLevel = 0;

  // unfortunately we can't use getopt() here, as Windoze doesn't have it
  while (argc > 2) {
    char* const opt = argv[1];
    if (opt[0] != '-') usage();
    switch (opt[1]) {

    case 'p': { // specify start port number
      int portArg;
      if (sscanf(argv[2], "%d", &portArg) != 1) {
	usage();
      }
      if (portArg <= 0 || portArg >= 65536 || portArg&1) {
	fprintf(stderr, "bad port number: %d (must be even, and in the range (0,65536))\n", portArg);
	usage();
      }
      desiredPortNum = (unsigned short)portArg;
      ++argv; --argc;
      break;
    }

    case 'r': { // do not receive data (instead, just 'play' the stream(s))
      createReceivers = False;
      break;
    }

    case 'q': { // output a QuickTime file (to stdout)
      outputQuickTimeFile = True;
      break;
    }

    case 'a': { // receive/record an audio stream only
      audioOnly = True;
      singleMedium = "audio";
      break;
    }

    case 'v': { // receive/record a video stream only
      videoOnly = True;
      singleMedium = "video";
      break;
    }

    case 'V': { // verbose output
      verbosityLevel = 1;
      break;
    }

    case 'e': { // specify end time, or how much to delay after end time
      float arg;
      if (sscanf(argv[2], "%g", &arg) != 1) {
	usage();
      }
      if (argv[2][0] == '-') { // in case argv[2] was "-0"
	// a 'negative' argument was specified; use this for "endTimeSlop":
	endTime = 0; // use whatever's in the SDP
	endTimeSlop = -arg;
      } else {
	endTime = arg;
	endTimeSlop = 0;
      }
      ++argv; --argc;
      break;
    }

    case 'c': { // play continuously
      playContinuously = True;
      break;
    }

    case 's': { // specify an offset to use with "SimpleRTPSource"s
      if (sscanf(argv[2], "%d", &simpleRTPoffsetArg) != 1) {
	usage();
      }
      if (simpleRTPoffsetArg < 0) {
	fprintf(stderr, "offset argument to \"-s\" must be >= 0\n");
	usage();
      }
      ++argv; --argc;
      break;
    }

    case 'n': { // notify the user when the first data packet arrives
      notifyOnPacketArrival = True;
      break;
    }

    case 't': {
      // stream RTP and RTCP over the RTSP TCP connection, using the
      // RTP-over-TCP hack (RFC 2236, section 10.12)
      streamUsingTCP = True;
      break;
    }

    case 'u': { // specify a username and password
      username = argv[2];
      password = argv[3];
      argv+=2; argc-=2;
      break;
    }

    case 'w': { // specify a width (pixels) for an output QuickTime movie
      if (sscanf(argv[2], "%hu", &movieWidth) != 1) {
	usage();
      }
      ++argv; --argc;
      break;
    }

    case 'h': { // specify a height (pixels) for an output QuickTime movie
      if (sscanf(argv[2], "%hu", &movieHeight) != 1) {
	usage();
      }
      ++argv; --argc;
      break;
    }

    case 'f': { // specify a frame rate (per second) for an output QT movie
      if (sscanf(argv[2], "%u", &movieFPS) != 1) {
	usage();
      }
      ++argv; --argc;
      break;
    }

    // Note: The following option is deprecated, and may someday be removed:
    case 'l': { // try to compensate for packet loss by repeating frames
      packetLossCompensate = True;
      break;
    }

    case 'y': { // synchronize audio and video streams
      syncStreams = True;
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
  if (!createReceivers && outputQuickTimeFile) {
    fprintf(stderr, "The -r and -q flags cannot both be used!\n");
    usage();
  }
  if (audioOnly && videoOnly) {
    fprintf(stderr, "The -a and -v flags cannot bot be used!\n");
    usage();
  }
  if (!createReceivers && notifyOnPacketArrival) {
    fprintf(stderr, "Warning: Because we're not receiving stream data, the -n flag has no effect\n");
  }

  char* url = argv[1];

  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  // Create our RTSP client object:
  rtspClient = RTSPClient::createNew(*env, verbosityLevel, "openRTSP");
  if (rtspClient == NULL) {
    fprintf(stderr, "Failed to create RTSP client: %s\n",
	    env->getResultMsg());
    shutdown();
  }

  // Open the URL, to get a SDP description:
  char* sdpDescription;
  if (username != NULL && password != NULL) {
    sdpDescription
      = rtspClient->describeWithPassword(url, username, password);
  } else {
    sdpDescription = rtspClient->describeURL(url);
  }
  if (sdpDescription == NULL) {
    fprintf(stderr, "Failed to get a SDP description from URL \"%s\": %s\n",
	    url, env->getResultMsg());
    shutdown();
  }

  fprintf(stderr, "Opened URL \"%s\", returning a SDP description:\n%s\n",
	  url, sdpDescription);

  // Create a media session object from this SDP description:
  session = MediaSession::createNew(*env, sdpDescription);
  delete sdpDescription;
  if (session == NULL) {
    fprintf(stderr, "Failed to create a MediaSession object from the SDP description: %s\n", env->getResultMsg());
    shutdown();
  } else if (!session->hasSubsessions()) {
    fprintf(stderr, "This session has no media subsessions (i.e., \"m=\" lines)\n");
    shutdown();
  }

  // Then, setup the "RTPSource"s for the session:
  MediaSubsessionIterator iter(*session);
  MediaSubsession *subsession;
  Boolean madeProgress = False;
  char const* singleMediumToTest = singleMedium;
  while ((subsession = iter.next()) != NULL) {
    // If we've asked to receive only a single medium, then check this now:
    if (singleMediumToTest != NULL) {
      if (strcmp(subsession->mediumName(), singleMediumToTest) != 0) {
	fprintf(stderr, "Ignoring \"%s/%s\" subsession, because we've asked to receive a single %s session only\n",
		subsession->mediumName(), subsession->codecName(),
		singleMedium);
	continue;
      } else {
	// Receive this subsession only
	singleMediumToTest = "xxxxx";
	    // this hack ensures that we get only 1 subsession of this type
      }
    }

    if (desiredPortNum != 0) {
      subsession->setClientPortNum(desiredPortNum);
      desiredPortNum += 2;
    }

    if (createReceivers) {
      if (!subsession->initiate(simpleRTPoffsetArg)) {
	fprintf(stderr, "Unable to create receiver for \"%s/%s\" subsession: %s\n",
		subsession->mediumName(), subsession->codecName(),
		env->getResultMsg());
      } else {
	fprintf(stderr, "Created receiver for \"%s/%s\" subsession (client ports %d-%d)\n",
		subsession->mediumName(), subsession->codecName(),
		subsession->clientPortNum(), subsession->clientPortNum()+1);
	madeProgress = True;

	// Because we're saving the incoming data, rather than playing it
	// in real time, allow an especially large time threshold (1 second)
	// for reordering misordered incoming packets:
	if (subsession->rtpSource() != NULL) {
	  unsigned const thresh = 1000000; // 1 second 
	  subsession->rtpSource()->setPacketReorderingThresholdTime(thresh);
	}
      }
    } else {
      if (subsession->clientPortNum() == 0) {
	fprintf(stderr, "No client port was specified for the \"%s/%s\" subsession.  (Try adding the \"-p <portNum>\" option.)\n",
		subsession->mediumName(), subsession->codecName());
      } else {	
	madeProgress = True;
      }
    }
  }
  if (!madeProgress) shutdown(0);

  // Issue RTSP "SETUP"s on this session:
  setupRTSPStreams();

  // Create output files:
  if (createReceivers) {
    if (outputQuickTimeFile) {
      // Create a "QuickTimeFileSink", to write to 'stdout':
      qtOut = QuickTimeFileSink::createNew(*env, *session, "stdout",
					   movieWidth, movieHeight,
					   movieFPS,
					   packetLossCompensate,
					   syncStreams);
      if (qtOut == NULL) {
	fprintf(stderr,
		"Failed to create QuickTime file sink for stdout: %s",
		env->getResultMsg());
	shutdown();
      }

      qtOut->startPlaying(sessionAfterPlaying, NULL);
    } else {
      // Create and start "FileSink"s for each subsession:
      madeProgress = False;
      iter.reset();
      while ((subsession = iter.next()) != NULL) {
	if (subsession->readSource() == NULL) continue; // was not initiated
	
	// Create an output file for each desired stream:
	char outFileName[1000];
	if (singleMedium == NULL) {
	  // Output file name is "<medium_name>-<codec_name>-<counter>"
	  static unsigned streamCounter = 0;
	  snprintf(outFileName, sizeof outFileName, "%s-%s-%d",
		   subsession->mediumName(), subsession->codecName(),
		   ++streamCounter);
	} else {
	  sprintf(outFileName, "stdout");
	}
	subsession->sink = FileSink::createNew(*env, outFileName);
	if (subsession->sink == NULL) {
	  fprintf(stderr, "Failed to create FileSink for \"%s\": %s\n",
		  outFileName, env->getResultMsg());
	} else {
	  if (singleMedium == NULL) {
	    fprintf(stderr, "Created output file: \"%s\"\n", outFileName);
	  } else {
	    fprintf(stderr, "Outputting data from the \"%s/%s\" subsession to 'stdout'\n",
		    subsession->mediumName(), subsession->codecName());
	  }
	  subsession->sink->startPlaying(*(subsession->readSource()),
					 subsessionAfterPlaying,
					 subsession);
	  
	  // Also set a handler to be called if a RTCP "BYE" arrives
	  // for this subsession:
	  if (subsession->rtcpInstance() != NULL) {
	    subsession->rtcpInstance()->setByeHandler(subsessionByeHandler,
						      subsession);
	  }

	  madeProgress = True;
	}
      }
      if (!madeProgress) shutdown();
    }
  }
    
  // Finally, issue a RTSP "PLAY" command for each subsession,
  // to start the data flow:
  startPlayingRTSPStreams();

  env->taskScheduler().blockMyself(); // does not return

  return 0; // only to prevent compiler warning
}


void setupRTSPStreams(){
  MediaSubsessionIterator iter(*session);
  MediaSubsession *subsession;
  Boolean madeProgress = False;

  while ((subsession = iter.next()) != NULL) {
    if (subsession->clientPortNum() == 0) continue; // port # was not set

    if (!rtspClient->setupMediaSubsession(*subsession,
					  False, streamUsingTCP)) {
      fprintf(stderr,
	      "Failed to issue RTSP \"SETUP\" on \"%s/%s\" subsession: %s\n",
	      subsession->mediumName(), subsession->codecName(),
	      env->getResultMsg());
    } else {
      fprintf(stderr, "Issued RTSP \"SETUP\" on \"%s/%s\" subsession (client ports %d-%d)\n",
	      subsession->mediumName(), subsession->codecName(),
	      subsession->clientPortNum(), subsession->clientPortNum()+1);
      madeProgress = True;
    }
  }
  if (!madeProgress) shutdown();
}

void startPlayingRTSPStreams(){
  MediaSubsessionIterator iter(*session);
  MediaSubsession *subsession;
  Boolean madeProgress = False;
  while ((subsession = iter.next()) != NULL) {
    if (subsession->sessionId == NULL) continue; //no RTSP sess in progress

    if (!rtspClient->playMediaSubsession(*subsession)) {
      fprintf(stderr, "RTSP \"PLAY\" command on \"%s/%s\" subsession failed: %s\n",
	      subsession->mediumName(), subsession->codecName(),
	      env->getResultMsg());
      shutdown();
    } else {
      fprintf(stderr, "Issued RTSP \"PLAY\" command on \"%s/%s\" subsession\n",
	      subsession->mediumName(), subsession->codecName());
      madeProgress = True;
    }
  }
  if (!madeProgress) shutdown();

  // Figure out how long to delay (if at all) before shutting down, or
  // repeating the playing
  Boolean timerIsBeingUsed = False;
  float totalEndTime = endTime;
  if (endTime == 0) endTime = session->playEndTime(); // use SDP end time
  if (endTime > 0) {
    float const maxDelayTime = (float)( ((unsigned)0xFFFFFFFF)/1000000.0 );
    if (endTime > maxDelayTime) {
      fprintf(stderr, "Warning: specified end time %g exceeds maximum %g; will not do a delayed shutdown\n", endTime, maxDelayTime);
      endTime = 0.0;
    } else {
      timerIsBeingUsed = True;
      totalEndTime = endTime + endTimeSlop;

      int uSecsToDelay = (int)(totalEndTime*1000000.0);
      currentTimerTask = env->taskScheduler().scheduleDelayedTask(
         uSecsToDelay, (TaskFunc*)sessionTimerHandler, (void*)NULL);
    }
  }

  char const* actionString
    = createReceivers? "Receiving streamed data":"Data is being streamed";
  if (timerIsBeingUsed) {
    fprintf(stderr, "%s (for up to %.1f seconds)...\n",
	    actionString, totalEndTime);
  } else {
#ifdef USE_SIGNALS
    pid_t ourPid = getpid();
    fprintf(stderr, "%s (signal with \"kill -HUP %d\" or \"kill -USR1 %d\" to terminate)...\n",
	    actionString, (int)ourPid, (int)ourPid);
#else
    fprintf(stderr, "%s...\n", actionString);
#endif
  }

  // Watch for incoming packets (if desired):
  checkForPacketArrival(NULL);
}

void tearDownRTSPStreams() {
  MediaSubsessionIterator iter(*session);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    if (subsession->sessionId == NULL) continue; // no PLAY in progress
    
    fprintf(stderr, "Closing \"%s/%s\" subsession\n",
	    subsession->mediumName(), subsession->codecName());
    rtspClient->teardownMediaSubsession(*subsession);
  }
}

void closeMediaSinks() {
  Medium::close(qtOut);

  MediaSubsessionIterator iter(*session);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    Medium::close(subsession->sink);
    subsession->sink = NULL;
  }
}

void subsessionAfterPlaying(void* clientData) {
  // Begin by closing this media subsession:
  MediaSubsession* subsession = (MediaSubsession*)clientData;
  Medium::close(subsession->sink);
  subsession->sink = NULL;

  // Next, check whether *all* subsessions have now been closed:
  MediaSession& session = subsession->parentSession();
  MediaSubsessionIterator iter(session);
  while ((subsession = iter.next()) != NULL) {
    if (subsession->sink != NULL) return; // this subsession is still active
  }

  // All subsessions have now been closed
  sessionAfterPlaying();
}

void subsessionByeHandler(void* clientData) {
  struct timeval timeNow;
  gettimeofday(&timeNow, &Idunno);
  unsigned secsDiff = timeNow.tv_sec - startTime.tv_sec;

  MediaSubsession* subsession = (MediaSubsession*)clientData;
  fprintf(stderr, "Received RTCP \"BYE\" on \"%s/%s\" subsession (after %d seconds)\n",
	  subsession->mediumName(), subsession->codecName(), secsDiff);

  // Act now as if the subsession had closed:
  subsessionAfterPlaying(subsession);
}

void sessionAfterPlaying(void* /*clientData*/) {
  if (!playContinuously) {
    shutdown(0);
  } else {
    // We've been asked to play the RTSP stream(s) over again:
    startPlayingRTSPStreams();
  }
}

void sessionTimerHandler(void* /*clientData*/) {
  currentTimerTask = NULL;

  sessionAfterPlaying();
}

void shutdown(int exitCode) {
  if (env != NULL) {
    env->taskScheduler().unscheduleDelayedTask(currentTimerTask);
  }

  if (rtspClient != NULL) {
    // Teardown any outstanding RTSP sessions, and close media sinks:
    if (session != NULL){
      tearDownRTSPStreams();
      closeMediaSinks();
    }

    // Then shut down the RTSP client:
    Medium::close(rtspClient);
  }

  // Shutdown the Media session (and all of the RTP/RTCP subsessions):
  Medium::close(session);

  // Adios...
  exit(exitCode);
}

void signalHandlerShutdown(int /*sig*/) {
  fprintf(stderr, "Got shutdown signal\n");
  shutdown(0);
}

void checkForPacketArrival(void* clientData) {
  if (!notifyOnPacketArrival) return; // we're not checking 

  // Check each subsession, to see whether it has received data packets:
  Boolean someSubsessionsHaveReceivedDataPackets = False;
  Boolean allSubsessionsHaveReceivedDataPackets = True;
  Boolean allSubsessionsHaveBeenSynced = True;
  MediaSubsessionIterator iter(*session);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    RTPSource* src = subsession->rtpSource();
    if (src == NULL) continue;
    if (src->receptionStatsDB().numActiveSourcesSinceLastReset() > 0) {
      // At least one data packet has arrived
      someSubsessionsHaveReceivedDataPackets = True;
    } else {
      allSubsessionsHaveReceivedDataPackets = False;
    }
    if (!src->hasBeenSynchronizedUsingRTCP()) {
      // At least one subsession remains unsynchronized:
      allSubsessionsHaveBeenSynced = False;
    }
  }

  if ((!syncStreams && someSubsessionsHaveReceivedDataPackets) ||
      (syncStreams && allSubsessionsHaveReceivedDataPackets
       && allSubsessionsHaveBeenSynced)) {
    // Notify the user:
    struct timeval timeNow;
    gettimeofday(&timeNow, &Idunno);
    fprintf(stderr, "%sata packets have begun arriving [%ld%03ld]\007\n",
	    syncStreams ? "Synchronized d" : "D",
	    timeNow.tv_sec, timeNow.tv_usec/1000);
    return;
  }

  // No luck, so reschedule this check again, after a delay:
  int uSecsToDelay = 100000; // 100 ms
  currentTimerTask
    = env->taskScheduler().scheduleDelayedTask(uSecsToDelay,
			       (TaskFunc*)checkForPacketArrival, NULL);
}
