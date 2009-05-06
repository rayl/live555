// A simplified version of "MPEG4VideoStreamFramer" that takes only complete,
// discrete frames (rather than an arbitrary byte stream) as input.
// This avoids the parsing and data copying overhead of the full
// "MPEG4VideoStreamFramer".
// C++ header

#ifndef _MPEG4_VIDEO_STREAM_DISCRETE_FRAMER_HH
#define _MPEG4_VIDEO_STREAM_DISCRETE_FRAMER_HH

#ifndef _MPEG4_VIDEO_STREAM_FRAMER_HH
#include "MPEG4VideoStreamFramer.hh"
#endif

class MPEG4VideoStreamDiscreteFramer: public MPEG4VideoStreamFramer {
public:
  static MPEG4VideoStreamDiscreteFramer*
  createNew(UsageEnvironment& env, FramedSource* inputSource);

private:
  MPEG4VideoStreamDiscreteFramer(UsageEnvironment& env,
				 FramedSource* inputSource);
      // called only by createNew()
  virtual ~MPEG4VideoStreamDiscreteFramer();

private:
  // redefined virtual functions:
  virtual void doGetNextFrame();

private:
  static void afterGettingFrame(void* clientData, unsigned frameSize,
                                unsigned numTruncatedBytes,
                                struct timeval presentationTime,
                                unsigned durationInMicroseconds);
  void afterGettingFrame1(unsigned frameSize,
                          unsigned numTruncatedBytes,
                          struct timeval presentationTime,
                          unsigned durationInMicroseconds);
};

#endif
