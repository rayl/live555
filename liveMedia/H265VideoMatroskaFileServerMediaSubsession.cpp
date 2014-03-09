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
// Copyright (c) 1996-2014 Live Networks, Inc.  All rights reserved.
// A 'ServerMediaSubsession' object that creates new, unicast, "RTPSink"s
// on demand, from an H265 video track within a Matroska file.
// Implementation

#include "H265VideoMatroskaFileServerMediaSubsession.hh"
#include "H265VideoStreamDiscreteFramer.hh"
#include "MatroskaDemuxedTrack.hh"

H265VideoMatroskaFileServerMediaSubsession* H265VideoMatroskaFileServerMediaSubsession
::createNew(MatroskaFileServerDemux& demux, unsigned trackNumber) {
  return new H265VideoMatroskaFileServerMediaSubsession(demux, trackNumber);
}

#define CHECK_PTR if (ptr >= limit) return
#define NUM_BYTES_REMAINING (unsigned)(limit - ptr)

H265VideoMatroskaFileServerMediaSubsession
::H265VideoMatroskaFileServerMediaSubsession(MatroskaFileServerDemux& demux, unsigned trackNumber)
  : H265VideoFileServerMediaSubsession(demux.envir(), demux.fileName(), False),
    fOurDemux(demux), fTrackNumber(trackNumber),
    fVPSSize(0), fVPS(NULL), fSPSSize(0), fSPS(NULL), fPPSSize(0), fPPS(NULL) {
  // Our track's 'Codec Private' data should contain VPS, SPS, and PPS NAL units.  Copy these:
  unsigned numVPS_SPS_PPSBytes = 0;
  u_int8_t* VPS_SPS_PPSBytes = NULL;
  MatroskaTrack* track = fOurDemux.lookup(fTrackNumber);

  if (track->codecPrivateUsesH264FormatForH265) {
    // The data uses the H.264-style format (but including VPS NAL unit(s)).
    // The VPS,SPS,PPS NAL unit information starts at byte #5:
    if (track->codecPrivateSize >= 6) {
      numVPS_SPS_PPSBytes = track->codecPrivateSize - 5;
      VPS_SPS_PPSBytes = &track->codecPrivate[5];
    }
  } else {
    // The data uses the proper H.265-style format.
    // The VPS,SPS,PPS NAL unit information starts at byte #22:
    if (track->codecPrivateSize >= 23) {
      numVPS_SPS_PPSBytes = track->codecPrivateSize - 22;
      VPS_SPS_PPSBytes = &track->codecPrivate[22];
    }
  }

  // Extract, from "VPS_SPS_PPSBytes", one VPS NAL unit, one SPS NAL unit, and one PPS NAL unit.
  // (I hope one is all we need of each.)
  if (numVPS_SPS_PPSBytes == 0 || VPS_SPS_PPSBytes == NULL) return; // sanity check
  unsigned i;
  u_int8_t* ptr = VPS_SPS_PPSBytes;
  u_int8_t* limit = &VPS_SPS_PPSBytes[numVPS_SPS_PPSBytes];

  if (track->codecPrivateUsesH264FormatForH265) {
    // The data uses the H.264-style format (but including VPS NAL unit(s)).
    while (NUM_BYTES_REMAINING > 0) {
      unsigned numNALUnits = (*ptr++)&0x1F; CHECK_PTR;
      for (i = 0; i < numNALUnits; ++i) {
	unsigned nalUnitLength = (*ptr++)<<8; CHECK_PTR;
	nalUnitLength |= *ptr++; CHECK_PTR;

	if (nalUnitLength > NUM_BYTES_REMAINING) return;
	u_int8_t nal_unit_type = (ptr[0]&0x7E)>>1;
	if (nal_unit_type == 32) { // VPS
	  fVPSSize = nalUnitLength;
	  delete[] fVPS; fVPS = new u_int8_t[nalUnitLength];
	  memmove(fVPS, ptr, nalUnitLength);
	} else if (nal_unit_type == 33) { // SPS
	  fSPSSize = nalUnitLength;
	  delete[] fSPS; fSPS = new u_int8_t[nalUnitLength];
	  memmove(fSPS, ptr, nalUnitLength);
	} else if (nal_unit_type == 34) { // PPS
	  fPPSSize = nalUnitLength;
	  delete[] fPPS; fPPS = new u_int8_t[nalUnitLength];
	  memmove(fPPS, ptr, nalUnitLength);
	}
	ptr += nalUnitLength;
      }
    }
  } else {
    // The data uses the proper H.265-style format.
    unsigned numOfArrays = *ptr++; CHECK_PTR;
    for (unsigned j = 0; j < numOfArrays; ++j) {
      ++ptr; CHECK_PTR; // skip the 'array_completeness'|'reserved'|'NAL_unit_type' byte

      unsigned numNalus = (*ptr++)<<8; CHECK_PTR;
      numNalus |= *ptr++; CHECK_PTR;

      for (i = 0; i < numNalus; ++i) {
	unsigned nalUnitLength = (*ptr++)<<8; CHECK_PTR;
	nalUnitLength |= *ptr++; CHECK_PTR;

	if (nalUnitLength > NUM_BYTES_REMAINING) return;
	u_int8_t nal_unit_type = (ptr[0]&0x7E)>>1;
	if (nal_unit_type == 32) { // VPS
	  fVPSSize = nalUnitLength;
	  delete[] fVPS; fVPS = new u_int8_t[nalUnitLength];
	  memmove(fVPS, ptr, nalUnitLength);
	} else if (nal_unit_type == 33) { // SPS
	  fSPSSize = nalUnitLength;
	  delete[] fSPS; fSPS = new u_int8_t[nalUnitLength];
	  memmove(fSPS, ptr, nalUnitLength);
	} else if (nal_unit_type == 34) { // PPS
	  fPPSSize = nalUnitLength;
	  delete[] fPPS; fPPS = new u_int8_t[nalUnitLength];
	  memmove(fPPS, ptr, nalUnitLength);
	}
	ptr += nalUnitLength;
      }
    }
  }
}

H265VideoMatroskaFileServerMediaSubsession
::~H265VideoMatroskaFileServerMediaSubsession() {
  delete[] fVPS;
  delete[] fSPS;
  delete[] fPPS;
}

float H265VideoMatroskaFileServerMediaSubsession::duration() const { return fOurDemux.fileDuration(); }

void H265VideoMatroskaFileServerMediaSubsession
::seekStreamSource(FramedSource* inputSource, double& seekNPT, double /*streamDuration*/, u_int64_t& /*numBytes*/) {
  // "inputSource" is a framer. *Its* source is the demuxed track that we seek on:
  H265VideoStreamDiscreteFramer* framer = (H265VideoStreamDiscreteFramer*)inputSource;

  MatroskaDemuxedTrack* demuxedTrack = (MatroskaDemuxedTrack*)(framer->inputSource());
  demuxedTrack->seekToTime(seekNPT);
}

FramedSource* H265VideoMatroskaFileServerMediaSubsession
::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) {
  // Allow for the possibility of very large NAL units being fed to our "RTPSink" objects:
  OutPacketBuffer::maxSize = 300000; // bytes
  estBitrate = 500; // kbps, estimate

  // Create the video source:
  FramedSource* baseH265VideoSource = fOurDemux.newDemuxedTrack(clientSessionId, fTrackNumber);
  if (baseH265VideoSource == NULL) return NULL;
  
  // Create a framer for the Video stream:
  H265VideoStreamDiscreteFramer* framer
    = H265VideoStreamDiscreteFramer::createNew(envir(), baseH265VideoSource);
  framer->setVPSandSPSandPPS(fVPS, fVPSSize, fSPS, fSPSSize, fPPS, fPPSSize);

  return framer;
}
