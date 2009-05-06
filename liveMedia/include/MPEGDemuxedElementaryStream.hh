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
// Copyright (c) 1996-2002 Live Networks, Inc.  All rights reserved.
// A MPEG 1 or 2 Elementary Stream, demultiplexed from a Program Stream
// C++ header

#ifndef _MPEG_DEMUXED_ELEMENTARY_STREAM_HH
#define _MPEG_DEMUXED_ELEMENTARY_STREAM_HH

#ifndef _MPEG_DEMUX_HH
#include "MPEGDemux.hh"
#endif

class MPEGDemuxedElementaryStream: public FramedSource {
private: // We are created only by a MPEGDemux (a friend)
  MPEGDemuxedElementaryStream(UsageEnvironment& env,
			      unsigned char streamIdTag,
			      MPEGDemux& sourceDemux);
  virtual ~MPEGDemuxedElementaryStream();

private:
  // redefined virtual functions:
  virtual void doGetNextFrame();
  virtual void doStopGettingFrames();
  virtual char const* MIMEtype() const; 
  virtual unsigned maxFrameSize() const;
  virtual float getPlayTime(unsigned numFrames) const;

private:
  static void afterGettingFrame(void* clientData, unsigned frameSize,
				struct timeval presentationTime);

private:
  unsigned char fOurStreamIdTag;
  MPEGDemux& fOurSourceDemux;
  char const* fMIMEtype;

  friend class MPEGDemux;
};

#endif
