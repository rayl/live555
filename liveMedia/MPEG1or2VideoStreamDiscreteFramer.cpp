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
// A simplified version of "MPEG1or2VideoStreamFramer" that takes only
// complete, discrete frames (rather than an arbitrary byte stream) as input.
// This avoids the parsing and data copying overhead of the full
// "MPEG1or2VideoStreamFramer".
// Implementation

#include "MPEG1or2VideoStreamDiscreteFramer.hh"

MPEG1or2VideoStreamDiscreteFramer*
MPEG1or2VideoStreamDiscreteFramer::createNew(UsageEnvironment& env,
                                             FramedSource* inputSource,
                                             Boolean iFramesOnly,
                                             double vshPeriod) {
  // Need to add source type checking here???  #####
  return new MPEG1or2VideoStreamDiscreteFramer(env, inputSource,
                                               iFramesOnly, vshPeriod);
}
                                                                                
MPEG1or2VideoStreamDiscreteFramer
::MPEG1or2VideoStreamDiscreteFramer(UsageEnvironment& env,
                                    FramedSource* inputSource,
                                    Boolean iFramesOnly, double vshPeriod)
  : MPEG1or2VideoStreamFramer(env, inputSource, iFramesOnly, vshPeriod,
                              False/*don't create a parser*/) {
}
                                                                                
MPEG1or2VideoStreamDiscreteFramer::~MPEG1or2VideoStreamDiscreteFramer() {
}
                                                                                
void MPEG1or2VideoStreamDiscreteFramer::doGetNextFrame() {
  // Arrange to read data (which should be a complete MPEG-1 or 2 video frame)
  // from our data source, directly into the client's input buffer.
  // After reading this, we'll do some parsing on the frame.
  fInputSource->getNextFrame(fTo, fMaxSize,
                             afterGettingFrame, this,
                             FramedSource::handleClosure, this);
}
                                                                                
void MPEG1or2VideoStreamDiscreteFramer
::afterGettingFrame(void* clientData, unsigned frameSize,
                    unsigned numTruncatedBytes,
                    struct timeval presentationTime,
                    unsigned durationInMicroseconds) {
  MPEG1or2VideoStreamDiscreteFramer* source
    = (MPEG1or2VideoStreamDiscreteFramer*)clientData;
  source->afterGettingFrame1(frameSize, numTruncatedBytes,
                             presentationTime, durationInMicroseconds);
}
                                                                                
void MPEG1or2VideoStreamDiscreteFramer
::afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
                     struct timeval presentationTime,
                     unsigned durationInMicroseconds) {
  fPictureEndMarker = True; // Assume that we have a complete 'picture' here
  // ##### Later:
  // - do "iFramesOnly" if requested
  // - handle "vshPeriod"
 
  // Complete delivery to the client:
  fFrameSize = frameSize;
  fNumTruncatedBytes = numTruncatedBytes;
  fPresentationTime = presentationTime; // change for B-frames??? #####
  fDurationInMicroseconds = durationInMicroseconds;
  afterGetting(this);
}
