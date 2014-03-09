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
// A filter that breaks up a H.264 or H.265 Video Elementary Stream into NAL units.
// C++ header

#ifndef _H264_OR_5_VIDEO_STREAM_FRAMER_HH
#define _H264_OR_5_VIDEO_STREAM_FRAMER_HH

#ifndef _MPEG_VIDEO_STREAM_FRAMER_HH
#include "MPEGVideoStreamFramer.hh"
#endif

class H264or5VideoStreamFramer: public MPEGVideoStreamFramer {
protected:
  H264or5VideoStreamFramer(int hNumber, UsageEnvironment& env, FramedSource* inputSource, Boolean createParser, Boolean includeStartCodeInOutput);
      // We're an abstract base class.
  virtual ~H264or5VideoStreamFramer();

  void saveCopyOfVPS(u_int8_t* from, unsigned size);
  void saveCopyOfSPS(u_int8_t* from, unsigned size);
  void saveCopyOfPPS(u_int8_t* from, unsigned size);

  void setPresentationTime() { fPresentationTime = fNextPresentationTime; }

protected:
  u_int8_t* fLastSeenVPS;
  unsigned fLastSeenVPSSize;
  u_int8_t* fLastSeenSPS;
  unsigned fLastSeenSPSSize;
  u_int8_t* fLastSeenPPS;
  unsigned fLastSeenPPSSize;
  struct timeval fNextPresentationTime; // the presentation time to be used for the next NAL unit to be parsed/delivered after this
  friend class H264or5VideoStreamParser; // hack
};

#endif
