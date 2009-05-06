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
// A HTTP Sink specifically for MPEG Video
// Implementation

#include "MPEGVideoHTTPSink.hh"

#if defined(__WIN32__) || defined(_WIN32)
#define _close closesocket
#else
#define _close close
#endif

////////// MPEGVideoHTTPSink //////////

MPEGVideoHTTPSink* MPEGVideoHTTPSink::createNew(UsageEnvironment& env, Port ourPort) {
  int ourSocket = -1;
  MPEGVideoHTTPSink* newSink = NULL;

  do {
    int ourSocket = setUpOurSocket(env, ourPort);
    if (ourSocket == -1) break;

    MPEGVideoHTTPSink* newSink = new MPEGVideoHTTPSink(env, ourSocket);
    if (newSink == NULL) break;
    
    appendPortNum(env, ourPort);
    
    return newSink;
  } while (0);

  if (ourSocket != -1) ::_close(ourSocket);
  delete newSink;
  return NULL;
}

MPEGVideoHTTPSink::MPEGVideoHTTPSink(UsageEnvironment& env, int ourSocket)
  : HTTPSink(env, ourSocket), fHaveSeenFirstVSH(False) {
}

MPEGVideoHTTPSink::~MPEGVideoHTTPSink() {
}

#define VIDEO_SEQUENCE_HEADER_START_CODE 0x000001B3

Boolean MPEGVideoHTTPSink::isUseableFrame(unsigned char* framePtr,
					  unsigned frameSize) {
  // Some clients get confused if the data we give them does not start
  // with a 'video_sequence_header', so we ignore any frames that precede
  // the first 'video_sequence_header':

  // Sanity check: a frame with < 4 bytes is never valid:
  if (frameSize < 4) return False;

  if (fHaveSeenFirstVSH) return True;

  unsigned first4Bytes
    = (framePtr[0]<<24)|(framePtr[1]<<16)|(framePtr[2]<<8)|framePtr[3];

  if (first4Bytes == VIDEO_SEQUENCE_HEADER_START_CODE) {
    fHaveSeenFirstVSH = True; 
    return True;
  } else {
    return False;
  }
}
