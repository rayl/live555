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
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2010 Live Networks, Inc.  All rights reserved.
// A filter that parses a DV input stream into DV frames to deliver to the downstream object
// Implementation

#include "DVVideoStreamFramer.hh"

////////// DVVideoStreamFramer implementation //////////

DVVideoStreamFramer::DVVideoStreamFramer(UsageEnvironment& env, FramedSource* inputSource)
  : FramedFilter(env, inputSource),
    fProfileName(NULL), fInitialBlocksPresent(False) {
}

DVVideoStreamFramer::~DVVideoStreamFramer() {
}

DVVideoStreamFramer*
DVVideoStreamFramer::createNew(UsageEnvironment& env, FramedSource* inputSource) {
  return new DVVideoStreamFramer(env, inputSource);
}

char const* DVVideoStreamFramer::profileName() {
  fprintf(stderr, "#####@@@@@DVVideoStreamFramer::profileName()1\n");
  if (fProfileName != NULL) return fProfileName;
  fprintf(stderr, "#####@@@@@DVVideoStreamFramer::profileName()2\n");

  // To determine the stream's profile, we need to first read a chunk of data that we can parse:
  fInputSource->getNextFrame(fSavedInitialBlocks, DV_SAVED_INITIAL_BLOCKS_SIZE,
			     afterGettingFrame, this, FramedSource::handleClosure, this);
  
  // Handle events until the requested data arrives:
  fprintf(stderr, "#####@@@@@DVVideoStreamFramer::profileName()3\n");
  envir().taskScheduler().doEventLoop(&fInitialBlocksPresent);
  fprintf(stderr, "#####@@@@@DVVideoStreamFramer::profileName()4\n");

  return "";//#####@@@@@
}

Boolean DVVideoStreamFramer::isDVVideoStreamFramer() const {
  return True;
}

void DVVideoStreamFramer::doGetNextFrame() {
  fprintf(stderr, "#####@@@@@DVVideoStreamFramer::doGetNextFrame()1\n");
  fFrameSize = 0; // initially, until we deliver data

  // If we have saved initial blocks, use this data first.
  if (fInitialBlocksPresent) {
    // For simplicity, we require the downstream object's buffer to be >= this data's size:
    if (fMaxSize < DV_SAVED_INITIAL_BLOCKS_SIZE) {
      fNumTruncatedBytes = fMaxSize;
      afterGetting(this);
      return;
    }

    memmove(fTo, fSavedInitialBlocks, DV_SAVED_INITIAL_BLOCKS_SIZE);
    fTo += DV_SAVED_INITIAL_BLOCKS_SIZE;
    fFrameSize = DV_SAVED_INITIAL_BLOCKS_SIZE;
    fMaxSize -= DV_SAVED_INITIAL_BLOCKS_SIZE;
    fInitialBlocksPresent = False; // for the future
    fprintf(stderr, "#####@@@@@DVVideoStreamFramer::doGetNextFrame()2\n");
  }
    
  // Arrange to read the (rest of the) requested data.
  // (Make sure that we read an integral multiple of the DV block size.)
  if (fMaxSize > DV_DIF_BLOCK_SIZE) fMaxSize -= fMaxSize%DV_DIF_BLOCK_SIZE;
  fprintf(stderr, "#####@@@@@DVVideoStreamFramer::doGetNextFrame()3\n");
  fInputSource->getNextFrame(fTo, fMaxSize, afterGettingFrame, this, FramedSource::handleClosure, this);
}

void DVVideoStreamFramer::afterGettingFrame(void* clientData, unsigned frameSize,
					    unsigned numTruncatedBytes,
					    struct timeval /*presentationTime*/, unsigned /*durationInMicroseconds*/) {
  DVVideoStreamFramer* source = (DVVideoStreamFramer*)clientData;
  source->afterGettingFrame1(frameSize, numTruncatedBytes);
}

#define DVSectionId(n) fTo[(n)*DV_DIF_BLOCK_SIZE+0]
#define DVData(n,i) fTo[(n)*DV_DIF_BLOCK_SIZE+3+(i)]

void DVVideoStreamFramer::afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes) {
  fprintf(stderr, "#####@@@@@DVVideoStreamFramer::afterGettingFrame1(%d,%d)\n", frameSize, numTruncatedBytes);
  if (fTo == fSavedInitialBlocks) {
    fprintf(stderr, "#####@@@@@DVVideoStreamFramer::afterGettingFrame2(%d,%d) signalling saved initial blocks\n", frameSize, numTruncatedBytes);
    // We read data into our special buffer; signal that it has arrived:
    fInitialBlocksPresent = True;
  }

  if (fProfileName == NULL && frameSize >= DV_SAVED_INITIAL_BLOCKS_SIZE) {
    // (Try to) parse this data enough to figure out its profile:
    fprintf(stderr, "#####@@@@@DVVideoStreamFramer::afterGettingFrame3(%d,%d) parsing data\n", frameSize, numTruncatedBytes);
    fprintf(stderr, "#####@@@@@ %02x %02x, %02x,%02x,%02x,%02x\n", DVSectionId(0), DVSectionId(5), DVData(0,0), DVData(0,1), DVData(5,45), DVData(5,48));
    //#####@@@@@ 1f 52, bf,68,60,20 CHECK GRANULARITY OF READS
  }

  // Complete delivery to the downstream object:
  fFrameSize += frameSize; // Note: +=, not =, in case we transfered saved initial blocks first
  fNumTruncatedBytes = numTruncatedBytes;
  //#####@@@@@ set fDurationInMicroseconds and fPresentationTime
  //#####@@@@@doStopGettingFrame()? clear fProfileName, reset fInitialBlocksPresent
  afterGetting(this);
}
