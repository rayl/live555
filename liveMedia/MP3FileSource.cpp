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
// Copyright (c) 1996-2004 Live Networks, Inc.  All rights reserved.
// MP3 File Sources
// Implementation

#include <sys/stat.h>
#if defined(__WIN32__) || defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif

#include "MP3FileSource.hh"
#include "MP3StreamState.hh"

////////// MP3FileSource //////////

MP3FileSource::MP3FileSource(UsageEnvironment& env, FILE* fid)
  : FramedFileSource(env, fid),
    fStreamState(new MP3StreamState(env)) {
}

MP3FileSource::~MP3FileSource() {
  delete fStreamState;
}

char const* MP3FileSource::MIMEtype() const {
  return "audio/mpeg";
}

MP3FileSource* MP3FileSource::createNew(UsageEnvironment& env, char const* fileName) {
  MP3FileSource* newSource = NULL;

  do {
    FILE* fid;
    unsigned fileSize = 0;

    // Check for a special case file name: "stdin"
    if (strcmp(fileName, "stdin") == 0) {
      fid = stdin;
#if defined(__WIN32__) || defined(_WIN32)
      _setmode(_fileno(stdin), _O_BINARY); // convert to binary mode
#endif
    } else { 
      fid = fopen(fileName, "rb");
      if (fid == NULL) {
	env.setResultMsg("unable to open file \"",fileName, "\"");
	break;
      }
      struct stat sb;
      if (stat(fileName, &sb) == 0) {
	fileSize = sb.st_size;
      }
    }

    newSource = new MP3FileSource(env, fid);
    if (newSource == NULL) break;

    newSource->assignStream(fid, fileSize);
    if (!newSource->initializeStream()) break;

    return newSource;
  } while (0);

  delete newSource;
  return NULL;
}

void MP3FileSource::getAttributes() const {
  char buffer[200];
  fStreamState->getAttributes(buffer, sizeof buffer);
  envir().setResultMsg(buffer);
}

void MP3FileSource::doGetNextFrame() {
  if (!doGetNextFrame1()) {
    handleClosure(this);
    return;
  }

  // Switch to another task:
#if defined(__WIN32__) || defined(_WIN32)
  // HACK: liveCaster/lc uses an implementation of scheduleDelayedTask()
  // that performs very badly (chewing up lots of CPU time, apparently polling)
  // on Windows.  Until this is fixed, we just call our "afterGetting()"
  // function directly.  This avoids infinite recursion, as long as our sink
  // is discontinuous, which is the case for the RTP sink that liveCaster/lc
  // uses. #####
  afterGetting(this);
#else
  nextTask() = envir().taskScheduler().scheduleDelayedTask(0,
				(TaskFunc*)afterGetting, this);
#endif
}

Boolean MP3FileSource::doGetNextFrame1() {
  if (!fHaveJustInitialized) {
    if (fStreamState->findNextHeader(fPresentationTime) == 0) return False;
  } else {
    fPresentationTime = fFirstFramePresentationTime;
    fHaveJustInitialized = False;
  }
  
  if (!fStreamState->readFrame(fTo, fMaxSize, fFrameSize, fDurationInMicroseconds)) {
    char tmp[200];
    sprintf(tmp,
	    "Insufficient buffer size %d for reading MPEG audio frame (needed %d)\n",
	    fMaxSize, fFrameSize);
    envir().setResultMsg(tmp);
    fFrameSize = fMaxSize;
    return False;
  }

  return True;
}

void MP3FileSource::assignStream(FILE* fid, unsigned fileSize) {
  fStreamState->assignStream(fid, fileSize);
}


Boolean MP3FileSource::initializeStream() {
  // Make sure the file has an appropriate header near the start:
  if (fStreamState->findNextHeader(fFirstFramePresentationTime) == 0) {
      envir().setResultMsg("not an MPEG audio file");
      return False;
  }

  fStreamState->checkForXingHeader(); // in case this is a VBR file

  fHaveJustInitialized = True;
  return True;
}
