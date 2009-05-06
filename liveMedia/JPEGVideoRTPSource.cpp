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
// Copyright (c) 1996-2001 Live Networks, Inc.  All rights reserved.

// JPEG Video RTP Sources
// Implementation
// 09 26 2002 - Initial Implementation : Giom
// Copyright (c) 1990-2002 Morgan Multimedia  All rights reserved.

#include "JPEGVideoRTPSource.hh"
#include "GroupsockHelper.hh"

JPEGVideoRTPSource*
JPEGVideoRTPSource::createNew(UsageEnvironment& env, Groupsock* RTPgs,
			      unsigned char rtpPayloadFormat,
			      unsigned rtpTimestampFrequency) {
  JPEGVideoRTPSource* newSource = NULL;

  do {
    newSource = new JPEGVideoRTPSource(env, RTPgs, rtpPayloadFormat,
				       rtpTimestampFrequency);
    if (newSource == NULL) break;

    // Try to use a big receive buffer for RTP:
    increaseReceiveBufferTo(env, RTPgs->socketNum(), 50*1024);

    return newSource;
  } while (0);

  delete newSource;
  return NULL;
}

JPEGVideoRTPSource::JPEGVideoRTPSource(UsageEnvironment& env,
				       Groupsock* RTPgs,
				       unsigned char rtpPayloadFormat,
				       unsigned rtpTimestampFrequency)
  : MultiFramedRTPSource(env, RTPgs,
			 rtpPayloadFormat, rtpTimestampFrequency),
  width(0),
  height(0),
  quality(0),
  qtables(NULL),
  qtlen(0),
  dri(0),
  hdrlen(0),
  detected(False),
  framesize(0)
{
	fCurrentPacketCompletesFrame = False;
}

JPEGVideoRTPSource::~JPEGVideoRTPSource() {
  if (qtables)
	  delete [] qtables;
  qtables = NULL;
}

int MjpegHeader(unsigned char *buf, unsigned type, unsigned w, unsigned h, unsigned char *qtables, unsigned qtlen, unsigned dri)
{
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
	*ptr++ = (BYTE)(type);
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

  unsigned Offset = (unsigned)((DWORD)headerStart[1] << 16 | (DWORD)headerStart[2] << 8 | (DWORD)headerStart[3]);
  unsigned Type = (unsigned)headerStart[4];
  unsigned Q = (unsigned)headerStart[5];
  unsigned Width = (unsigned)headerStart[6] * 8;
  unsigned Height = (unsigned)headerStart[7] * 8;

  width = Width;
  height = Height;

  if (Type > 63)
  {
	  // Restart Marker header present
	  /*
		0                   1                   2                   3
		0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |       Restart Interval        |F|L|       Restart Count       |
	   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	  */
	  if (packetSize < resultSpecialHeaderSize + 4) 
		  return False;

	  unsigned RestartInterval = (unsigned)((WORD)headerStart[resultSpecialHeaderSize] << 8 | (WORD)headerStart[resultSpecialHeaderSize + 1]);
	  dri = RestartInterval;
	  resultSpecialHeaderSize += 4;

  }

  if (Offset == 0)
  {
	  framesize = 0;

	  if (Q > 127)
	  {
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
		  if (packetSize < resultSpecialHeaderSize + 4) 
			  return False;

		  unsigned MBZ = (unsigned)headerStart[resultSpecialHeaderSize];
		  if (MBZ == 0)
		  {
			  unsigned Precision = (unsigned)headerStart[resultSpecialHeaderSize + 1];
			  unsigned Length = (unsigned)((WORD)headerStart[resultSpecialHeaderSize + 2] << 8 | (WORD)headerStart[resultSpecialHeaderSize + 3]);

			  //ASSERT(Length == 128);

			  resultSpecialHeaderSize += 4;

			  if (packetSize < resultSpecialHeaderSize + Length) 
				  return False;

			  if (qtables)
				  delete [] qtables;

			  qtlen = Length;
			  qtables = new unsigned char[Length];
			  memcpy(qtables, &headerStart[resultSpecialHeaderSize], Length);

			  unsigned newhdrlen = MjpegHeader(header, type, width, height, qtables, qtlen, dri);
			  if (hdrlen != newhdrlen)
				hdrlen = newhdrlen;

			  resultSpecialHeaderSize += Length;
		  }
	  }
  }

  // The RTP "M" (marker) bit indicates the last fragment of a frame:
  fCurrentPacketCompletesFrame = rtpMarkerBit;

  framesize += packetSize - resultSpecialHeaderSize;

  return True;
}    

char const* JPEGVideoRTPSource::MIMEtype() const {
  return "video/jpeg";
}

#if 0
//
// RFC 2435 : RTP Payload Format for JPEG-compressed Video
//
/*
Appendix A

   The following code can be used to create a quantization table from a
   Q factor:
*/

/*
 * Table K.1 from JPEG spec.
 */
static const int jpeg_luma_quantizer[64] = {
        16, 11, 10, 16, 24, 40, 51, 61,
        12, 12, 14, 19, 26, 58, 60, 55,
        14, 13, 16, 24, 40, 57, 69, 56,
        14, 17, 22, 29, 51, 87, 80, 62,
        18, 22, 37, 56, 68, 109, 103, 77,
        24, 35, 55, 64, 81, 104, 113, 92,
        49, 64, 78, 87, 103, 121, 120, 101,
        72, 92, 95, 98, 112, 100, 103, 99
};

/*
 * Table K.2 from JPEG spec.
 */
static const int jpeg_chroma_quantizer[64] = {
        17, 18, 24, 47, 99, 99, 99, 99,
        18, 21, 26, 66, 99, 99, 99, 99,
        24, 26, 56, 99, 99, 99, 99, 99,
        47, 66, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99
};

/*
 * Call MakeTables with the Q factor and two u_char[64] return arrays
 */
void
MakeTables(int q, u_char *lqt, u_char *cqt)
{
  int i;
  int factor = q;

  if (q < 1) factor = 1;
  if (q > 99) factor = 99;
  if (q < 50)
    q = 5000 / factor;
  else
    q = 200 - factor*2;
  for (i=0; i < 64; i++) {
    int lq = (jpeg_luma_quantizer[i] * q + 50) / 100;
    int cq = (jpeg_chroma_quantizer[i] * q + 50) / 100;

    /* Limit the quantizers to 1 <= q <= 255 */
    if (lq < 1) lq = 1;
    else if (lq > 255) lq = 255;
    lqt[i] = lq;

    if (cq < 1) cq = 1;
    else if (cq > 255) cq = 255;
    cqt[i] = cq;
  }
}

/*
Appendix B

   The following routines can be used to create the JPEG marker segments
   corresponding to the table-specification data that is absent from the
   RTP/JPEG body.
*/

u_char lum_dc_codelens[] = {
        0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
};

u_char lum_dc_symbols[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
};

u_char lum_ac_codelens[] = {
        0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d,
};

u_char lum_ac_symbols[] = {
        0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
        0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
        0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
        0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
        0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
        0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
        0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
        0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
        0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
        0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
        0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
        0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
        0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
        0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
        0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
        0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
        0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
        0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
        0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
        0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
        0xf9, 0xfa,
};

u_char chm_dc_codelens[] = {
        0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
};

u_char chm_dc_symbols[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
};

u_char chm_ac_codelens[] = {
        0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77,
};

u_char chm_ac_symbols[] = {
        0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
        0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
        0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
        0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
        0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
        0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
        0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
        0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
        0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
        0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
        0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
        0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
        0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
        0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
        0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
        0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
        0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
        0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
        0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
        0xf9, 0xfa,
};

u_char *
MakeQuantHeader(u_char *p, u_char *qt, int tableNo)
{
        *p++ = 0xff;
        *p++ = 0xdb;            /* DQT */
        *p++ = 0;               /* length msb */
        *p++ = 67;              /* length lsb */
        *p++ = tableNo;
        memcpy(p, qt, 64);
        return (p + 64);
}

u_char *
MakeHuffmanHeader(u_char *p, u_char *codelens, int ncodes,
                  u_char *symbols, int nsymbols, int tableNo,
                  int tableClass)
{
        *p++ = 0xff;
        *p++ = 0xc4;            /* DHT */
        *p++ = 0;               /* length msb */
        *p++ = 3 + ncodes + nsymbols; /* length lsb */
        *p++ = (tableClass << 4) | tableNo;
        memcpy(p, codelens, ncodes);
        p += ncodes;
        memcpy(p, symbols, nsymbols);
        p += nsymbols;
        return (p);
}

u_char *
MakeDRIHeader(u_char *p, u_short dri) {
        *p++ = 0xff;
        *p++ = 0xdd;            /* DRI */
        *p++ = 0x0;             /* length msb */
        *p++ = 4;               /* length lsb */
        *p++ = dri >> 8;        /* dri msb */
        *p++ = dri & 0xff;      /* dri lsb */
        return (p);
}

/*
 *  Arguments:
 *    type, width, height: as supplied in RTP/JPEG header
 *    lqt, cqt: quantization tables as either derived from
 *         the Q field using MakeTables() or as specified
 *         in section 4.2.
 *    dri: restart interval in MCUs, or 0 if no restarts.
 *
 *    p: pointer to return area
 *
 *  Return value:
 *    The length of the generated headers.
 *
 *    Generate a frame and scan headers that can be prepended to the
 *    RTP/JPEG data payload to produce a JPEG compressed image in
 *    interchange format (except for possible trailing garbage and
 *    absence of an EOI marker to terminate the scan).
 */
int MakeHeaders(u_char *p, int type, int w, int h, u_char *lqt,
                u_char *cqt, u_short dri)
{
        u_char *start = p;

        /* convert from blocks to pixels */
        w <<= 3;
        h <<= 3;
        *p++ = 0xff;
        *p++ = 0xd8;            /* SOI */

        p = MakeQuantHeader(p, lqt, 0);
        p = MakeQuantHeader(p, cqt, 1);

        if (dri != 0)
                p = MakeDRIHeader(p, dri);

        *p++ = 0xff;
        *p++ = 0xc0;            /* SOF */
        *p++ = 0;               /* length msb */
        *p++ = 17;              /* length lsb */
        *p++ = 8;               /* 8-bit precision */
        *p++ = h >> 8;          /* height msb */
        *p++ = h;               /* height lsb */
        *p++ = w >> 8;          /* width msb */
        *p++ = w;               /* wudth lsb */
        *p++ = 3;               /* number of components */
        *p++ = 0;               /* comp 0 */
        if (type == 0)
                *p++ = 0x21;    /* hsamp = 2, vsamp = 1 */
        else
                *p++ = 0x22;    /* hsamp = 2, vsamp = 2 */
        *p++ = 0;               /* quant table 0 */
        *p++ = 1;               /* comp 1 */
        *p++ = 0x11;            /* hsamp = 1, vsamp = 1 */
        *p++ = 1;               /* quant table 1 */
        *p++ = 2;               /* comp 2 */
        *p++ = 0x11;            /* hsamp = 1, vsamp = 1 */
        *p++ = 1;               /* quant table 1 */
        p = MakeHuffmanHeader(p, lum_dc_codelens,
                              sizeof(lum_dc_codelens),
                              lum_dc_symbols,
                              sizeof(lum_dc_symbols), 0, 0);
        p = MakeHuffmanHeader(p, lum_ac_codelens,
                              sizeof(lum_ac_codelens),
                              lum_ac_symbols,
                              sizeof(lum_ac_symbols), 0, 1);
        p = MakeHuffmanHeader(p, chm_dc_codelens,
                              sizeof(chm_dc_codelens),
                              chm_dc_symbols,
                              sizeof(chm_dc_symbols), 1, 0);
        p = MakeHuffmanHeader(p, chm_ac_codelens,
                              sizeof(chm_ac_codelens),
                              chm_ac_symbols,
                              sizeof(chm_ac_symbols), 1, 1);
        *p++ = 0xff;
        *p++ = 0xda;            /* SOS */
        *p++ = 0;               /* length msb */
        *p++ = 12;              /* length lsb */
        *p++ = 3;               /* 3 components */
        *p++ = 0;               /* comp 0 */
        *p++ = 0;               /* huffman table 0 */
        *p++ = 1;               /* comp 1 */
        *p++ = 0x11;            /* huffman table 1 */
        *p++ = 2;               /* comp 2 */
        *p++ = 0x11;            /* huffman table 1 */
        *p++ = 0;               /* first DCT coeff */
        *p++ = 63;              /* last DCT coeff */
        *p++ = 0;               /* sucessive approx. */

        return (p - start);
};

/*
Appendix C

   The following routine is used to illustrate the RTP/JPEG packet
   fragmentation and header creation.

   For clarity and brevity, the structure definitions are only valid for
   32-bit big-endian (most significant octet first) architectures. Bit
   fields are assumed to be packed tightly in big-endian bit order, with
   no additional padding. Modifications would be required to construct a
   portable implementation.
*/
/*
 * RTP data header from RFC1889
 */
typedef unsigned char u_int8;
typedef WORD u_int16;
typedef DWORD u_int32;

typedef struct {
	union {
        u_int16 version:2;   /* protocol version */
        u_int16 p:1;         /* padding flag */
        u_int16 x:1;         /* header extension flag */
        u_int16 cc:4;        /* CSRC count */
        u_int16 m:1;         /* marker bit */
        u_int16 pt:7;        /* payload type */
		};
        u_int16 seq;              /* sequence number */
        u_int32 ts;               /* timestamp */
        u_int32 ssrc;             /* synchronization source */
        u_int32 csrc[1];          /* optional CSRC list */
} rtp_hdr_t;

#define RTP_HDR_SZ 12

/* The following definition is from RFC1890 */
#define RTP_PT_JPEG             26

struct jpeghdr {
	union {
        u_int32 tspec:8;   /* type-specific field */
        u_int32 off:24;    /* fragment byte offset */
		};
        u_int8 type;            /* id of jpeg decoder params */
        u_int8 q;               /* quantization factor (or table id) */
        u_int8 width;           /* frame width in 8 pixel blocks */
        u_int8 height;          /* frame height in 8 pixel blocks */
};

struct jpeghdr_rst {
        u_int16 dri;
		union {
        u_int16 f:1;
        u_int16 l:1;
        u_int16 count:14;
		};
};

struct jpeghdr_qtable {
        u_int8  mbz;
        u_int8  precision;
        u_int16 length;
};

#define RTP_JPEG_RESTART           0x40

/* Procedure SendFrame:
 *
 *  Arguments:
 *    start_seq: The sequence number for the first packet of the current
 *               frame.
 *    ts: RTP timestamp for the current frame
 *    ssrc: RTP SSRC value
 *    jpeg_data: Huffman encoded JPEG scan data
 *    len: Length of the JPEG scan data
 *    type: The value the RTP/JPEG type field should be set to
 *    typespec: The value the RTP/JPEG type-specific field should be set
 *              to
 *    width: The width in pixels of the JPEG image
 *    height: The height in pixels of the JPEG image
 *    dri: The number of MCUs between restart markers (or 0 if there
 *         are no restart markers in the data
 *    q: The Q factor of the data, to be specified using the Independent
 *       JPEG group's algorithm if 1 <= q <= 99, specified explicitly
 *       with lqt and cqt if q >= 128, or undefined otherwise.
 *    lqt: The quantization table for the luminance channel if q >= 128
 *    cqt: The quantization table for the chrominance channels if
 *         q >= 128
 *
 *  Return value:
 *    the sequence number to be sent for the first packet of the next
 *    frame.
 *
 * The following are assumed to be defined:
 *
 * PACKET_SIZE                         - The size of the outgoing packet
 * send_packet(u_int8 *data, int len)  - Sends the packet to the network
 */

#define PACKET_SIZE 1500

void send_packet(u_int8 *data, int len)
{
}

u_int16 SendFrame(u_int16 start_seq, u_int32 ts, u_int32 ssrc,
                   u_int8 *jpeg_data, int len, u_int8 type,
                   u_int8 typespec, int width, int height, int dri,
                   u_int8 q, u_int8 *lqt, u_int8 *cqt) {
        rtp_hdr_t rtphdr;
        struct jpeghdr jpghdr;
        struct jpeghdr_rst rsthdr;
        struct jpeghdr_qtable qtblhdr;
        u_int8 packet_buf[PACKET_SIZE];
        u_int8 *ptr;
        int bytes_left = len;
        int seq = start_seq;
        int /*pkt_len, */data_len;

        /* Initialize RTP header
         */
        rtphdr.version = 2;
        rtphdr.p = 0;
        rtphdr.x = 0;
        rtphdr.cc = 0;
        rtphdr.m = 0;
        rtphdr.pt = RTP_PT_JPEG;
        rtphdr.seq = start_seq;
        rtphdr.ts = ts;
        rtphdr.ssrc = ssrc;

        /* Initialize JPEG header
         */
        jpghdr.tspec = typespec;
        jpghdr.off = 0;
        jpghdr.type = type | ((dri != 0) ? RTP_JPEG_RESTART : 0);
        jpghdr.q = q;
        jpghdr.width = width / 8;
        jpghdr.height = height / 8;

        /* Initialize DRI header
         */
        if (dri != 0) {
                rsthdr.dri = dri;
                rsthdr.f = 1;        /* This code does not align RIs */
                rsthdr.l = 1;
                rsthdr.count = 0x3fff;
        }

        /* Initialize quantization table header
         */
        if (q >= 128) {
                qtblhdr.mbz = 0;
                qtblhdr.precision = 0; /* This code uses 8 bit tables only */
                qtblhdr.length = 128;  /* 2 64-byte tables */
        }

        while (bytes_left > 0) {
                ptr = packet_buf + RTP_HDR_SZ;
                memcpy(ptr, &jpghdr, sizeof(jpghdr));
                ptr += sizeof(jpghdr);

                if (dri != 0) {
                        memcpy(ptr, &rsthdr, sizeof(rsthdr));
                        ptr += sizeof(rsthdr);
                }

                if (q >= 128 && jpghdr.off == 0) {
                        memcpy(ptr, &qtblhdr, sizeof(qtblhdr));
                        ptr += sizeof(qtblhdr);
                        memcpy(ptr, lqt, 64);
                        ptr += 64;
                        memcpy(ptr, cqt, 64);
                        ptr += 64;
                }

                data_len = PACKET_SIZE - (ptr - packet_buf);
                if (data_len >= bytes_left) {
                        data_len = bytes_left;
                        rtphdr.m = 1;
                }

                memcpy(packet_buf, &rtphdr, RTP_HDR_SZ);
                memcpy(ptr, jpeg_data + jpghdr.off, data_len);

                send_packet(packet_buf, (ptr - packet_buf) + data_len);

                jpghdr.off += data_len;
                bytes_left -= data_len;
                rtphdr.seq++;
        }
        return rtphdr.seq;
}
#endif
