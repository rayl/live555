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
// JPEG Video (RFC 2435) RTP Sources 
// C++ header

#ifndef _JPEG_VIDEO_RTP_SOURCE_HH
#define _JPEG_VIDEO_RTP_SOURCE_HH

#ifndef _MULTI_FRAMED_RTP_SOURCE_HH
#include "MultiFramedRTPSource.hh"
#endif

enum {
	MARKER_SOF0	= 0xc0,		// start-of-frame, baseline scan
	MARKER_SOI	= 0xd8,		// start of image
	MARKER_EOI	= 0xd9,		// end of image
	MARKER_SOS	= 0xda,		// start of scan
	MARKER_DRI	= 0xdd,		// restart interval
	MARKER_DQT	= 0xdb,		// define quantization tables
	MARKER_DHT  = 0xc4,		// huffman tables
	MARKER_APP_FIRST	= 0xe0,
	MARKER_APP_LAST		= 0xef,
	MARKER_COMMENT		= 0xfe,
};

#define MAX_JPEG_HEADER_SIZE 1024

class JPEGVideoRTPSource: public MultiFramedRTPSource {
public:
  static JPEGVideoRTPSource*
  createNew(UsageEnvironment& env, Groupsock* RTPgs,
	    unsigned char rtpPayloadFormat = 26,
	    unsigned rtpPayloadFrequency = 90000);

  Boolean detected;
  unsigned type;
  unsigned width;
  unsigned height;
  unsigned quality;
  unsigned dri;
  unsigned char* qtables;
  unsigned qtlen;
  unsigned char header[MAX_JPEG_HEADER_SIZE];
  unsigned hdrlen;
  unsigned framesize;


protected:
  virtual ~JPEGVideoRTPSource();

private:
  JPEGVideoRTPSource(UsageEnvironment& env, Groupsock* RTPgs,
		     unsigned char rtpPayloadFormat,
		     unsigned rtpTimestampFrequency);
      // called only by createNew()

private:
  // redefined virtual functions:
  virtual Boolean processSpecialHeader(unsigned char* headerStart,
                                       unsigned packetSize,
									   Boolean rtpMarkerBit,
                                       unsigned& resultSpecialHeaderSize);

					virtual char const* MIMEtype() const; 
};

#endif
