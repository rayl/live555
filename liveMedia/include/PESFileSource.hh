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
// C++ header

#ifndef _PES_FILE_SOURCE_HH
#define _PES_FILE_SOURCE_HH

#ifndef _FRAMED_FILE_SOURCE_HH
#include "FramedFileSource.hh"
#endif

#ifndef _MPEG_1OR2_DEMUX_HH
#include "MPEG1or2Demux.hh" // for SCR
#endif

class PESFileSource: public FramedFileSource {
public:
  static PESFileSource* createNew(UsageEnvironment& env,
				  char const* fileName,
				  unsigned char mpegVersion = 2);

  MPEG1or2Demux::SCR lastSeenSCR(u_int8_t stream_id) const {
    return fLastSeenSCR[stream_id];
  }

  unsigned char mpegVersion() const { return fMPEGVersion; }

private:
  PESFileSource(UsageEnvironment& env, FILE* fid, unsigned char mpegVersion);
	// called only by createNew()

  virtual ~PESFileSource();

private:
  // redefined virtual functions:
  virtual void doGetNextFrame();

private:
  unsigned char fMPEGVersion;
  MPEG1or2Demux::SCR fLastSeenSCR[256]; // indexed by the "stream_id" field
};

#endif
