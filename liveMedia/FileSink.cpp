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
// File sinks
// Implementation

#if defined(__WIN32__) || defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif
#include "FileSink.hh"
#include "GroupsockHelper.hh"

////////// FileSink //////////

FileSink::FileSink(UsageEnvironment& env, FILE* fid)
  : MediaSink(env), fOutFid(fid) {
}

FileSink::~FileSink() {
  fclose(fOutFid);
}

FileSink* FileSink::createNew(UsageEnvironment& env, char const* fileName) {
  FileSink* newSink = NULL;

  do {
    FILE* fid = openFileByName(env, fileName);
    if (fid == NULL) break;

    newSink = new FileSink(env, fid);
    if (newSink == NULL) break;

    return newSink;
  } while (0);

  delete newSink;
  return NULL;
}

FILE* FileSink::openFileByName(UsageEnvironment& env,
			       char const* fileName) {
  FILE* fid;
    
  // Check for special case 'file names': "stdout" and "stderr"
  if (strcmp(fileName, "stdout") == 0) {
    fid = stdout;
#if defined(__WIN32__) || defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY);	// convert to binary mode
#endif
  } else if (strcmp(fileName, "stderr") == 0) {
    fid = stderr;
#if defined(__WIN32__) || defined(_WIN32)
    _setmode(_fileno(stderr), _O_BINARY);	// convert to binary mode
#endif
  } else {
    fid = fopen(fileName, "wb");
  }

  if (fid == NULL) {
    env.setResultMsg("unable to open file \"", fileName, "\"");
  }

  return fid;
}

Boolean FileSink::continuePlaying() {
  if (fSource == NULL) return False;

  fSource->getNextFrame(fBuffer, sizeof fBuffer,
			afterGettingFrame, this,
			onSourceClosure, this);

  return True;
}

void FileSink::afterGettingFrame(void* clientData, unsigned frameSize,
				 struct timeval /*presentationTime*/) {
  FileSink* sink = (FileSink*)clientData;

  // Write to our file:
#ifdef TEST_LOSS
  static unsigned const framesPerPacket = 10;
  static unsigned const frameCount = 0;
  static Boolean const packetIsLost;
  if ((frameCount++)%framesPerPacket == 0) {
    packetIsLost = (our_random()%10 == 0); // simulate 10% packet loss #####
  }

  if (!packetIsLost)
#endif
  fwrite(sink->fBuffer, frameSize, 1, sink->fOutFid);

  if (fflush(sink->fOutFid) == EOF) {
    // The output file has closed.  Handle this the same way as if the
    // input source had closed:
    onSourceClosure(sink);

    sink->stopPlaying();
    return;
  }
 
  // Then try getting the next frame:
  sink->continuePlaying();
}
