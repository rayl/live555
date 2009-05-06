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
// Copyright (c) 1996-2003 Live Networks, Inc.  All rights reserved.

// JPEG Video RTP Sources
// Implementation
// 09 26 2002 - Initial Implementation : Giom
// Copyright (c) 1990-2002 Morgan Multimedia  All rights reserved.

// 02/2003: Cleaned up to add the synthesized JPEG header to the start
// of each outgoing frame.
// Copyright (c) 1996-2003 Live Networks, Inc.  All rights reserved.

#include "JPEGVideoRTPSource.hh"

////////// JPEGBufferedPacket and JPEGBufferedPacketFactory //////////

class JPEGBufferedPacket: public BufferedPacket {
private:
  // Redefined virtual functions:
  virtual void reset();
};

class JPEGBufferedPacketFactory: public BufferedPacketFactory {
private: // redefined virtual functions
  virtual BufferedPacket* createNew();
};

////////// JPEGVideoRTPSource implementation //////////

#define BYTE unsigned char
#define WORD unsigned
#define DWORD unsigned long

JPEGVideoRTPSource*
JPEGVideoRTPSource::createNew(UsageEnvironment& env, Groupsock* RTPgs,
			      unsigned char rtpPayloadFormat,
			      unsigned rtpTimestampFrequency) {
  return new JPEGVideoRTPSource(env, RTPgs, rtpPayloadFormat,
				rtpTimestampFrequency);
}

JPEGVideoRTPSource::JPEGVideoRTPSource(UsageEnvironment& env,
				       Groupsock* RTPgs,
				       unsigned char rtpPayloadFormat,
				       unsigned rtpTimestampFrequency)
  : MultiFramedRTPSource(env, RTPgs,
			 rtpPayloadFormat, rtpTimestampFrequency,
			 new JPEGBufferedPacketFactory),
  detected(False),
  type(0),
  width(0),
  height(0),
  quality(0),
  dri(0),
  qtables(NULL),
  qtlen(0),
  hdrlen(0),
  framesize(0) {
  memset(header, 0, sizeof(header));
}

JPEGVideoRTPSource::~JPEGVideoRTPSource() {
  if (qtables) delete [] qtables;
}

static int MjpegHeader(unsigned char *buf, unsigned type,
		       unsigned w, unsigned h,
		       unsigned char *qtables, unsigned qtlen, unsigned dri) {
  unsigned char *ptr = buf;

  // MARKER_SOI:
  *ptr++ = 0xFF;
  *ptr++ = MARKER_SOI;
  
  // MARKER_APP_FIRST:
  *ptr++ = 0xFF;
  *ptr++ = MARKER_APP_FIRST;
  *ptr++ = 0x00;
  *ptr++ = 0x10; // size of chunck
  *ptr++ = 'A';
  *ptr++ = 'V';
  *ptr++ = 'I';
  *ptr++ = '1';
  *ptr++ = 0x00;
  // field number: 0=Not field-interleaved, 1=First field, 2=Second field
  *ptr++ = 0x00;
  
  *ptr++ = 0x00;
  *ptr++ = 0x00;
  *ptr++ = 0x00;
  *ptr++ = 0x00; // field_size, should be updated later
  
  *ptr++ = 0x00;
  *ptr++ = 0x00;
  *ptr++ = 0x00;
  *ptr++ = 0x00; // field_size_less_padding, should be updated later 
  
  // MARKER_DRI:
  //if (dri)
  {
    *ptr++ = 0xFF;
    *ptr++ = MARKER_DRI;
    *ptr++ = 0x00;
    *ptr++ = 0x04; // size of chunck
    *ptr++ = (BYTE)(dri >> 8);
    *ptr++ = (BYTE)(dri); // restart interval
  }
  
  // MARKER_DQT:
  *ptr++ = 0xFF;
  *ptr++ = MARKER_DQT;
  *ptr++ = 0x00;
  *ptr++ = 0x84; // size of chunck
  *ptr++ = 0x00; // precision(0), table id(0)
  memcpy(ptr, qtables, qtlen / 2);
  qtables += qtlen / 2;
  ptr += qtlen / 2;
  *ptr++ = 0x01; // precision(0), table id(1)
  memcpy(ptr, qtables, qtlen / 2);
  qtables += qtlen / 2;
  ptr += qtlen / 2;
  
  // MARKER_SOF0:
  *ptr++ = 0xFF;
  *ptr++ = MARKER_SOF0;
  *ptr++ = 0x00;
  *ptr++ = 0x11; // size of chunck
  *ptr++ = 0x08; // sample precision
  *ptr++ = (BYTE)(h >> 8);
  *ptr++ = (BYTE)(h); // number of lines, multiple of 8
  *ptr++ = (BYTE)(w >> 8);
  *ptr++ = (BYTE)(w); // sample per line, multiple of 16
  *ptr++ = 0x03; // number of components
  *ptr++ = 0x01; // id of component
  *ptr++ = type ? 0x22 : 0x21; // sampling ratio (h,v)
  *ptr++ = 0x00; // quant table id
  *ptr++ = 0x02; // id of component
  *ptr++ = 0x11; // sampling ratio (h,v)
  *ptr++ = 0x01; // quant table id
  *ptr++ = 0x03; // id of component
  *ptr++ = 0x11; // sampling ratio (h,v)
  *ptr++ = 0x01; // quant table id
  
  // MARKER_SOS:
  *ptr++ = 0xFF;
  *ptr++ = MARKER_SOS;
  *ptr++ = 0x00;
  *ptr++ = 0x0C; // size of chunck
  *ptr++ = 0x03; // number of components
  *ptr++ = 0x01; // id of component
  *ptr++ = 0x00; // huffman table id (DC, AC)
  *ptr++ = 0x02; // id of component
  *ptr++ = 0x11; // huffman table id (DC, AC)
  *ptr++ = 0x03; // id of component
  *ptr++ = 0x11; // huffman table id (DC, AC)
  *ptr++ = 0x00; // start of spectral
  *ptr++ = 0x3F; // end of spectral
  *ptr++ = 0x00; // successive approximation bit position (high, low)
  
  return (ptr - buf);
}

Boolean JPEGVideoRTPSource
::processSpecialHeader(unsigned char* headerStart, unsigned packetSize,
		       Boolean rtpMarkerBit,
		       unsigned& resultSpecialHeaderSize) {
  // There's at least 8-byte video-specific header
  /*
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Type-specific |              Fragment Offset                  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|      Type     |       Q       |     Width     |     Height    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  */
  if (packetSize < 8) return False;

  resultSpecialHeaderSize = 8;
  detected = True;

  unsigned Offset = (unsigned)((DWORD)headerStart[1] << 16 | (DWORD)headerStart[2] << 8 | (DWORD)headerStart[3]);
  unsigned Type = (unsigned)headerStart[4];
  type = Type & 1;
  quality = (unsigned)headerStart[5];
  width = (unsigned)headerStart[6] * 8;
  height = (unsigned)headerStart[7] * 8;

  if (Type > 63) {
    // Restart Marker header present
    /*
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Restart Interval        |F|L|       Restart Count       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    if (packetSize < resultSpecialHeaderSize + 4) return False;

    unsigned RestartInterval = (unsigned)((WORD)headerStart[resultSpecialHeaderSize] << 8 | (WORD)headerStart[resultSpecialHeaderSize + 1]);
    dri = RestartInterval;
    resultSpecialHeaderSize += 4;
  }

  if (Offset == 0) {
    framesize = 0;
    
    if (quality > 127) {
      // Quantization Table header present
/*
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|      MBZ      |   Precision   |             Length            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Quantization Table Data                    |
|                              ...                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
      if (packetSize < resultSpecialHeaderSize + 4) return False;

      unsigned MBZ = (unsigned)headerStart[resultSpecialHeaderSize];
      if (MBZ == 0) {
	// unsigned Precision = (unsigned)headerStart[resultSpecialHeaderSize + 1];
	unsigned Length = (unsigned)((WORD)headerStart[resultSpecialHeaderSize + 2] << 8 | (WORD)headerStart[resultSpecialHeaderSize + 3]);

	//ASSERT(Length == 128);

	resultSpecialHeaderSize += 4;

	if (packetSize < resultSpecialHeaderSize + Length) return False;

	if (qtables) delete [] qtables;

	qtlen = Length;
	qtables = new unsigned char[Length];
	memcpy(qtables, &headerStart[resultSpecialHeaderSize], Length);
	
	hdrlen
	  = MjpegHeader(header, type, width, height, qtables, qtlen, dri);
	
	resultSpecialHeaderSize += Length;
      }
    }
  }

  framesize += packetSize - resultSpecialHeaderSize;

  // Hack: If this is the first (or only) fragment of a JPEG frame,
  // then we need to prepend the incoming data with the JPEG header that we've
  // just synthesized.  We can do this because we allowed space for it in
  // our special "JPEGBufferedPacket" subclass.  We also adjust
  // "resultSpecialHeaderSize" to compensate for this, by subtracting
  // the size of the synthesized header.  Note that this will cause
  // "resultSpecialHeaderSize" to become negative, but the code that called
  // us (in "MultiFramedRTPSource") will handle this properly.
  if (Offset == 0) {
    resultSpecialHeaderSize -= hdrlen; // goes negative
    headerStart += resultSpecialHeaderSize; // goes backward

    memmove(headerStart, header, hdrlen); // prepends synthesized header
  }

  // The RTP "M" (marker) bit indicates the last fragment of a frame:
  fCurrentPacketCompletesFrame = rtpMarkerBit;

  return True;
}    

char const* JPEGVideoRTPSource::MIMEtype() const {
  return "video/jpeg";
}

////////// JPEGBufferedPacket and JPEGBufferedPacketFactory implementation

void JPEGBufferedPacket::reset() {
  BufferedPacket::reset();

  // Move our "fHead" and "fTail" forward, to allow space for a synthesized
  // JPEG header to precede the RTP data that comes in over the network.
  unsigned offset = MAX_JPEG_HEADER_SIZE;
  if (offset > fPacketSize) offset = fPacketSize; // shouldn't happen
  fHead = fTail = offset;
}

BufferedPacket* JPEGBufferedPacketFactory::createNew() {
  return new JPEGBufferedPacket;
}
