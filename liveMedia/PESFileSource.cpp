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
// Copyright (c) 1996-2005 Live Networks, Inc.  All rights reserved.
// A source object for a file stream of MPEG PES packets
// Implementation

#include "PESFileSource.hh"
#include "InputFile.hh"

////////// PESFileSource //////////

PESFileSource*
PESFileSource::createNew(UsageEnvironment& env, char const* fileName,
			 unsigned char mpegVersion) {
  FILE* fid = OpenInputFile(env, fileName);
  if (fid == NULL) return NULL;
  return new PESFileSource(env, fid, mpegVersion);
}

PESFileSource::PESFileSource(UsageEnvironment& env, FILE* fid,
			     unsigned char mpegVersion)
  : FramedFileSource(env, fid),
    fMPEGVersion(mpegVersion) {
}

PESFileSource::~PESFileSource() {
  CloseInputFile(fFid);
}

void PESFileSource::doGetNextFrame() {
  do {
    // Begin by reading the first 9 bytes of the PES header, and check it for validity:
    unsigned char header[9];
    if (fread(header, 1, sizeof header, fFid) < sizeof header
	|| feof(fFid) || ferror(fFid)) {
      // The input source has ended:
      break;
    }

    if (!(header[0] == 0 && header[1] == 0 && header[2] == 1
	  && (header[6]&0xC0) == 0x80)) {
      envir() << "Bad PES header!\n";
      break;
    }
    u_int8_t stream_id = header[3];
    u_int16_t PES_packet_length = (header[4]<<8)|header[5];
    u_int8_t PTS_DTS_flags = header[7]>>6;
    u_int8_t PES_header_data_length = header[8];
    if (!(PES_packet_length >= PES_header_data_length+3)) {
      envir() << "Inconsistent PES_packet_length " << PES_packet_length
	      << " and PES_header_data_length " << PES_header_data_length << "\n";
      break;
    }

    fFrameSize = 6 + PES_packet_length;
    if (fFrameSize > fMaxSize) break; // treat a too-small client buffer as an error
    unsigned bytesRemaining = fFrameSize;

    // Copy the header to the client:
    memmove(fTo, header, sizeof header);
    fTo += sizeof header;
    bytesRemaining -= sizeof header;

    // If there's a PTS in the header, save it as our 'SCR'.
    // (However, if there's both a PTS and a DTS, use the DTS instead.)
    unsigned char pts[5];
    if (PTS_DTS_flags == 0x3 && PES_header_data_length >= 2*sizeof pts) {
      // Skip over the PTS to get to the DTS, which is what we'll use:
      if (fread(pts, 1, sizeof pts, fFid) < sizeof pts
	  || feof(fFid) || ferror(fFid)) {
	// The input source has ended:
	break;
      }

      // Copy the PTS bytes to the client:
      memmove(fTo, pts, sizeof pts);
      fTo += sizeof pts;
      bytesRemaining -= sizeof pts;
    }
    MPEG1or2Demux::SCR& scr = fLastSeenSCR[stream_id]; // alias
    if ((PTS_DTS_flags&0x2) != 0 && PES_header_data_length >= sizeof pts) {
      if (fread(pts, 1, sizeof pts, fFid) < sizeof pts
	  || feof(fFid) || ferror(fFid)) {
	// The input source has ended:
	break;
      }
      scr.highBit = (pts[0]&0x08)>>3;
      scr.remainingBits = (pts[0]&0x06)<<29;
      scr.remainingBits |= pts[1]<<22;
      scr.remainingBits |= (pts[2]&0xFE)<<14;
      scr.remainingBits |= pts[3]<<7;
      scr.remainingBits |= pts[4]>>1;
      scr.extension = 0;

      // Copy the PTS bytes to the client:
      memmove(fTo, pts, sizeof pts);
      fTo += sizeof pts;
      bytesRemaining -= sizeof pts;
    } else {
      // There was no PTS (or DTS) in this data, so set our SCR to all-zeros.
      scr.highBit = 0; scr.remainingBits = 0; scr.extension = 0;
    }

    // Complete delivery to the client:
    if (fread(fTo, 1, bytesRemaining, fFid) < bytesRemaining
	|| feof(fFid) || ferror(fFid)) {
      break;
    }

    // Switch to another task, and inform the reader that he has data:
    nextTask() = envir().taskScheduler().scheduleDelayedTask(0,
				     (TaskFunc*)FramedSource::afterGetting, this);
    return;
  } while (0);

  // An error occurred.  Treat this as if the input source ended:
  handleClosure(this);
}
