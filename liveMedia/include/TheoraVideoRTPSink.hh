/*
 * Theora Video RTP packetizer
 * Copied from live555's VorbisAudioRTPSink
 */

#ifndef _THEORA_VIDEO_RTP_SINK_HH
#define _THEORA_VIDEO_RTP_SINK_HH

#ifndef _VIDEO_RTP_SINK_HH
#include "VideoRTPSink.hh"
#endif

class TheoraVideoRTPSink: public VideoRTPSink {
public:
  enum PixFmt {
    YUV420,
    YUV422,
    YUV444,
  };
  
  static TheoraVideoRTPSink* createNew(UsageEnvironment& env,
				       Groupsock* RTPgs,
				       u_int8_t rtpPayloadFormat, u_int32_t rtpTimestampFrequency,
				       unsigned width, unsigned height, enum  PixFmt pf,
				       // The following headers provide the 'configuration' information, for the SDP description:
				       u_int8_t* identificationHeader, unsigned identificationHeaderSize,
				       u_int8_t* commentHeader, unsigned commentHeaderSize,
				       u_int8_t* setupHeader, unsigned setupHeaderSize, u_int32_t identField);
  
protected:
  TheoraVideoRTPSink(UsageEnvironment& env, Groupsock* RTPgs,
		     u_int8_t rtpPayloadFormat, u_int32_t rtpTimestampFrequency,
		     unsigned width, unsigned height, enum  PixFmt pf,
		     u_int8_t* identificationHeader, unsigned identificationHeaderSize,
		     u_int8_t* commentHeader, unsigned commentHeaderSize,
		     u_int8_t* setupHeader, unsigned setupHeaderSize,
		     u_int32_t identField);
  // called only by createNew()
  
  virtual ~TheoraVideoRTPSink();
  
private: // redefined virtual functions:
  virtual char const* auxSDPLine(); // for the "a=fmtp:" SDP line
  
  virtual void doSpecialFrameHandling(unsigned fragmentationOffset,
				      unsigned char* frameStart,
				      unsigned numBytesInFrame,
				      struct timeval framePresentationTime,
				      unsigned numRemainingBytes);
  virtual Boolean frameCanAppearAfterPacketStart(unsigned char const* frameStart,
						 unsigned numBytesInFrame) const;
  virtual unsigned specialHeaderSize() const;
  
private:
  u_int32_t fIdent; // "Ident" field used by this stream.  (Only the low 24 bits of this are used.)
  char* fFmtpSDPLine;
};

#endif
