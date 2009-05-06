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
// A filter for converting a stream of MPEG PES packets to a MPEG-2 Transport Stream
// C++ header

#ifndef _MPEG2_TRANSPORT_STREAM_FROM_PES_SOURCE_HH
#define _MPEG2_TRANSPORT_STREAM_FROM_PES_SOURCE_HH

#ifndef _FRAMED_FILTER_HH
#include "FramedFilter.hh"
#endif
#ifndef _MPEG_1OR2_DEMUXED_ELEMENTARY_STREAM_HH
#include "MPEG1or2DemuxedElementaryStream.hh"
#endif

#define PID_TABLE_SIZE 256

class MPEG2TransportStreamFromPESSource: public FramedFilter {
public:
  static MPEG2TransportStreamFromPESSource*
  createNew(UsageEnvironment& env, MPEG1or2DemuxedElementaryStream* inputSource);

protected:
  MPEG2TransportStreamFromPESSource(UsageEnvironment& env,
				    MPEG1or2DemuxedElementaryStream* inputSource);
      // called only by createNew()
  virtual ~MPEG2TransportStreamFromPESSource();

private:
  // Redefined virtual functions:
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

  void deliverDataToClient(u_int8_t pid, unsigned char* buffer, unsigned bufferSize,
			   unsigned& startPositionInBuffer);

  void deliverPATPacket();
  void deliverPMTPacket(Boolean hasChanged);

private:
  unsigned fOutgoingPacketCounter;
  unsigned fProgramMapVersion;
  struct {
    unsigned counter;
    u_int8_t streamType; // for use in Program Maps
  } fPIDState[PID_TABLE_SIZE];
  u_int8_t fPCR_PID, fCurrentPID;
      // Note: We map 8-bit stream_ids directly to PIDs 
  unsigned char fPCRHighBit;
  u_int32_t fPCRRemainingBits;
  u_int16_t fPCRExtension;
  unsigned char* fInputBuffer;
  unsigned fInputBufferSize, fInputBufferBytesUsed;
};

#endif
