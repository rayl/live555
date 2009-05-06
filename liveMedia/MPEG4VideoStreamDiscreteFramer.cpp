// A simplified version of "MPEG4VideoStreamFramer" that takes only complete,
// discrete frames (rather than an arbitrary byte stream) as input.
// This avoids the parsing and data copying overhead of the full
// "MPEG4VideoStreamFramer".
// Implementation

#include "MPEG4VideoStreamDiscreteFramer.hh"

MPEG4VideoStreamDiscreteFramer*
MPEG4VideoStreamDiscreteFramer::createNew(UsageEnvironment& env,
				  FramedSource* inputSource) {
  // Need to add source type checking here???  #####
  return new MPEG4VideoStreamDiscreteFramer(env, inputSource);
}

MPEG4VideoStreamDiscreteFramer
::MPEG4VideoStreamDiscreteFramer(UsageEnvironment& env,
				 FramedSource* inputSource)
  : MPEG4VideoStreamFramer(env, inputSource, False/*don't create a parser*/) {
}

MPEG4VideoStreamDiscreteFramer::~MPEG4VideoStreamDiscreteFramer() {
}

void MPEG4VideoStreamDiscreteFramer::doGetNextFrame() {
  // Arrange to read data (which should be a complete MPEG-4 video frame)
  // from our data source, directly into the client's input buffer.
  // After reading this, we'll do some parsing on the frame.
  fInputSource->getNextFrame(fTo, fMaxSize,
                             afterGettingFrame, this,
                             FramedSource::handleClosure, this);
}

void MPEG4VideoStreamDiscreteFramer
::afterGettingFrame(void* clientData, unsigned frameSize,
                    unsigned numTruncatedBytes,
                    struct timeval presentationTime,
                    unsigned durationInMicroseconds) {
  MPEG4VideoStreamDiscreteFramer* source = (MPEG4VideoStreamDiscreteFramer*)clientData;
  source->afterGettingFrame1(frameSize, numTruncatedBytes,
                             presentationTime, durationInMicroseconds);
}

void MPEG4VideoStreamDiscreteFramer
::afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
                     struct timeval presentationTime,
                     unsigned durationInMicroseconds) {
  // Check that the first 4 bytes are a system code:
  if (frameSize >= 4 && fTo[0] == 0 && fTo[1] == 0 && fTo[2] == 1) {
    fPictureEndMarker = True; // Assume that we have a complete 'picture' here
    if (fTo[3] == 0xB0) { // VISUAL_OBJECT_SEQUENCE_START_CODE
      // The next byte is the "profile_and_level_indication":
      if (frameSize >= 5) fProfileAndLevelIndication = fTo[4];

      // The start of this frame - up to the first GROUP_VOP_START_CODE
      // or VOP_START_CODE - is stream configuration information.  Save this:
      unsigned i;
      for (i = 7; i < frameSize; ++i) {
	if ((fTo[i] == 0xB3 /*GROUP_VOP_START_CODE*/ ||
	     fTo[i] == 0xB6 /*VOP_START_CODE*/)
	    && fTo[i-1] == 1 && fTo[i-2] == 0 && fTo[i-3] == 0) {
	  break; // The configuration information ends here
	}
      }
      fNumConfigBytes = i-3;
      delete[] fConfigBytes; fConfigBytes = new unsigned char[fNumConfigBytes];
      for (i = 0; i < fNumConfigBytes; ++i) fConfigBytes[i] = fTo[i];
    }
  }

  // Complete delivery to the client:
  fFrameSize = frameSize;
  fNumTruncatedBytes = numTruncatedBytes;
  fPresentationTime = presentationTime; // change for B-frames??? #####
  fDurationInMicroseconds = durationInMicroseconds;
  afterGetting(this);
}
