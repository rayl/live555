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
// A test program that splits a MPEG Program Stream file into
// video and audio output files.
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include <stdlib.h>

char const* inputFileName = "in.mpg";
char const* outputFileName_video = "out_video.mpg";
char const* outputFileName_audio = "out_audio.mpg";

void afterPlaying(void* clientData); // forward

// A structure to hold the state of the current session.
// It is used in the "afterPlaying()" function to clean up the session.
struct sessionState_t {
  MPEGDemux* baseDemultiplexor;
  MediaSource* videoSource;
  MediaSource* audioSource;
  FileSink* videoSink;
  FileSink* audioSink;
} sessionState;

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

  // Open the input file as a 'byte-stream file source':
  ByteStreamFileSource* inputSource
    = ByteStreamFileSource::createNew(*env, inputFileName);
  if (inputSource == NULL) {
    fprintf(stderr, "Unable to open file \"%s\" as a byte-stream file source\n",
	    inputFileName);
    exit(1);
  }
  
  // Create a MPEG demultiplexor that reads from that source.
  sessionState.baseDemultiplexor = MPEGDemux::createNew(*env, inputSource);

  // Create, from this, our own sources (video and audio):
  sessionState.videoSource = sessionState.baseDemultiplexor->newVideoStream();
  sessionState.audioSource = sessionState.baseDemultiplexor->newAudioStream();

  // Create the data sinks (output files):
  sessionState.videoSink = FileSink::createNew(*env, outputFileName_video);
  sessionState.audioSink = FileSink::createNew(*env, outputFileName_audio);

  // Finally, start playing each sink.
  // (Start playing video first, to ensure that any video sequence header
  // at the start of the file gets read.)
  fprintf(stderr, "Beginning to read...\n");
  sessionState.videoSink->startPlaying(*sessionState.videoSource,
				       afterPlaying, sessionState.videoSink);
  sessionState.audioSink->startPlaying(*sessionState.audioSource,
				       afterPlaying, sessionState.audioSink);

  env->taskScheduler().blockMyself(); // does not return

  return 0; // only to prevent compiler warning
}

void afterPlaying(void* clientData) {
  Medium* finishedSink = (Medium*)clientData;

  if (finishedSink == sessionState.videoSink) {
    fprintf(stderr, "No more video\n");
    Medium::close(sessionState.videoSink);
    Medium::close(sessionState.videoSource);
    sessionState.videoSink = NULL;
  } else if (finishedSink == sessionState.audioSink) {
    fprintf(stderr, "No more audio\n");
    Medium::close(sessionState.audioSink);
    Medium::close(sessionState.audioSource);
    sessionState.audioSink = NULL;
  }

  if (sessionState.videoSink == NULL && sessionState.audioSink == NULL) {
    fprintf(stderr, "...finished reading\n");

    Medium::close(sessionState.baseDemultiplexor);

    exit(0);
  }
}
