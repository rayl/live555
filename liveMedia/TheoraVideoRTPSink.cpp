/*
 * Theora Video RTP packetizer
 * Copied from live555's VorbisAudioRTPSink
 */

#include "TheoraVideoRTPSink.hh"
#include "Base64.hh"

TheoraVideoRTPSink::TheoraVideoRTPSink(UsageEnvironment& env, Groupsock* RTPgs,
				       u_int8_t rtpPayloadFormat,
				       u_int32_t rtpTimestampFrequency,
				       unsigned width, unsigned height, enum PixFmt pf,
				       u_int8_t* identificationHeader, unsigned identificationHeaderSize,
				       u_int8_t* commentHeader, unsigned commentHeaderSize,
				       u_int8_t* setupHeader, unsigned setupHeaderSize,
				       u_int32_t identField)
  : VideoRTPSink(env, RTPgs, rtpPayloadFormat, rtpTimestampFrequency, "theora"),
    fIdent(identField), fFmtpSDPLine(NULL) {
  static const char *pf_to_str[] = {
    "YCbCr-4:2:0",
    "YCbCr-4:2:2",
    "YCbCr-4:4:4",
  };
  
  // Create packed configuration headers, and encode this data into a "a=fmtp:" SDP line that we'll use to describe it:
  
  // First, count how many headers (<=3) are included, and how many bytes will be used to encode these headers' sizes:
  unsigned numHeaders = 0;
  unsigned sizeSize[2]; // The number of bytes used to encode the lengths of the first two headers (but not the length of the 3rd)
  sizeSize[0] = sizeSize[1] = 0;
  if (identificationHeaderSize > 0) {
    sizeSize[numHeaders++] = identificationHeaderSize < 128 ? 1 : identificationHeaderSize < 16384 ? 2 : 3;
  }
  if (commentHeaderSize > 0) {
    sizeSize[numHeaders++] = commentHeaderSize < 128 ? 1 : commentHeaderSize < 16384 ? 2 : 3;
  }
  if (setupHeaderSize > 0) {
    ++numHeaders;
  } else {
    sizeSize[1] = 0; // We have at most two headers, so the second one's length isn't encoded
  }
  if (numHeaders == 0) return; // With no headers, we can't set up a configuration
  if (numHeaders == 1) sizeSize[0] = 0; // With only one header, its length isn't encoded
  
  // Then figure out the size of the packed configuration headers, and allocate space for this:
  unsigned length = identificationHeaderSize + commentHeaderSize + setupHeaderSize; // The "length" field in the packed headers
  if (length > (unsigned)0xFFFF) return; // too big for a 16-bit field; we can't handle this
  unsigned packedHeadersSize
    = 4 // "Number of packed headers" field
    + 3 // "ident" field
    + 2 // "length" field
    + 1 // "n. of headers" field
    + sizeSize[0] + sizeSize[1] // "length1" and "length2" (if present) fields
    + length;
  u_int8_t* packedHeaders = new u_int8_t[packedHeadersSize];
  if (packedHeaders == NULL) return;
  
  // Fill in the 'packed headers':
  u_int8_t* p = packedHeaders;
  *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 1; // "Number of packed headers": 1
  *p++ = fIdent>>16; *p++ = fIdent>>8; *p++ = fIdent; // "Ident" (24 bits)
  *p++ = length>>8; *p++ = length; // "length" (16 bits)
  *p++ = numHeaders-1; // "n. of headers"
  if (numHeaders > 1) {
    // Fill in the "length1" header:
    unsigned length1 = identificationHeaderSize > 0 ? identificationHeaderSize : commentHeaderSize;
    if (length1 >= 16384) {
      *p++ = 0x80; // flag, but no more, because we know length1 <= 32767
    }
    if (length1 >= 128) {
      *p++ = 0x80|((length1&0x3F80)>>7); // flag + the second 7 bits
    }
    *p++ = length1&0x7F; // the low 7 bits
    
    if (numHeaders > 2) { // numHeaders == 3
      // Fill in the "length2" header (for the 'Comment' header):
      unsigned length2 = commentHeaderSize;
      if (length2 >= 16384) {
	*p++ = 0x80; // flag, but no more, because we know length2 <= 32767
      }
      if (length2 >= 128) {
	*p++ = 0x80|((length2&0x3F80)>>7); // flag + the second 7 bits
      }
      *p++ = length2&0x7F; // the low 7 bits
    }
  }
  // Copy each header:
  if (identificationHeader != NULL) memmove(p, identificationHeader, identificationHeaderSize); p += identificationHeaderSize;
  if (commentHeader != NULL) memmove(p, commentHeader, commentHeaderSize); p += commentHeaderSize;
  if (setupHeader != NULL) memmove(p, setupHeader, setupHeaderSize);
  
  // Having set up the 'packed configuration headers', Base-64-encode this, and put it in our "a=fmtp:" SDP line:
  char* base64PackedHeaders = base64Encode((char const*)packedHeaders, packedHeadersSize);
  delete[] packedHeaders;
  
  unsigned fmtpSDPLineMaxSize = 200 + strlen(base64PackedHeaders);// 200 => more than enough space
  fFmtpSDPLine = new char[fmtpSDPLineMaxSize];
  sprintf(fFmtpSDPLine, "a=fmtp:%d sampling=%s;width=%u;height=%u;delivery-method=out_band/rtsp;configuration=%s\r\n", rtpPayloadType(), pf_to_str[pf], width, height, base64PackedHeaders);
  delete[] base64PackedHeaders;
}

TheoraVideoRTPSink::~TheoraVideoRTPSink() {
  delete[] fFmtpSDPLine;
}

TheoraVideoRTPSink*
TheoraVideoRTPSink::createNew(UsageEnvironment& env, Groupsock* RTPgs,
			      u_int8_t rtpPayloadFormat, u_int32_t rtpTimestampFrequency,
			      unsigned width, unsigned height, enum PixFmt pf,
			      u_int8_t* identificationHeader, unsigned identificationHeaderSize,
			      u_int8_t* commentHeader, unsigned commentHeaderSize,
			      u_int8_t* setupHeader, unsigned setupHeaderSize,
			      u_int32_t identField) {
  return new TheoraVideoRTPSink(env, RTPgs,
				rtpPayloadFormat, rtpTimestampFrequency,
				width, height, pf,
				identificationHeader, identificationHeaderSize,
				commentHeader, commentHeaderSize,
				setupHeader, setupHeaderSize, identField);
}

char const* TheoraVideoRTPSink::auxSDPLine() {
  return fFmtpSDPLine;
}

void TheoraVideoRTPSink
::doSpecialFrameHandling(unsigned fragmentationOffset,
			 unsigned char* frameStart,
			 unsigned numBytesInFrame,
			 struct timeval framePresentationTime,
			 unsigned numRemainingBytes) {
  // Set the 4-byte "payload header", as defined in http://svn.xiph.org/trunk/theora/doc/draft-ietf-avt-rtp-theora-00.txt
  u_int8_t header[6];
  
  // The three bytes of the header are our "Ident":
  header[0] = fIdent>>16; header[1] = fIdent>>8; header[2] = fIdent;
  
  // The final byte contains the "F", "TDT", and "numPkts" fields:
  u_int8_t F; // Fragment type
  if (numRemainingBytes > 0) {
    if (fragmentationOffset > 0) {
      F = 2<<6; // continuation fragment
    } else {
      F = 1<<6; // start fragment
    }
  } else {
    if (fragmentationOffset > 0) {
      F = 3<<6; // end fragment
    } else {
      F = 0<<6; // not fragmented
    }
  }
  u_int8_t const TDT = 0<<4; // Theora Data Type (always a "Raw Theora payload")
  u_int8_t numPkts = F == 0 ? (numFramesUsedSoFar() + 1): 0; // set to 0 when we're a fragment
  header[3] = F|TDT|numPkts;
  
  // There's also a 2-byte 'frame-specific' header: The length of the
  // Theora data:
  header[4] = numBytesInFrame >>8;
  header[5] = numBytesInFrame;
  setSpecialHeaderBytes(header, sizeof(header));
  
  if (numRemainingBytes == 0) {
    // This packet contains the last (or only) fragment of the frame.
    // Set the RTP 'M' ('marker') bit:
    setMarkerBit();
  }
  
  // Important: Also call our base class's doSpecialFrameHandling(),
  // to set the packet's timestamp:
  MultiFramedRTPSink::doSpecialFrameHandling(fragmentationOffset,
					     frameStart, numBytesInFrame,
					     framePresentationTime,
					     numRemainingBytes);
}

Boolean TheoraVideoRTPSink::frameCanAppearAfterPacketStart(unsigned char const* /*frameStart*/,
							   unsigned /*numBytesInFrame*/) const {
  // Only one frame per packet:
  return False;
}

unsigned TheoraVideoRTPSink::specialHeaderSize() const {
  return 6;
}
