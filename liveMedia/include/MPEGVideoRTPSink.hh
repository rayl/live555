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
// RTP sink for MPEG video (RFC 2250)
// C++ header

#ifndef _MPEG_VIDEO_RTP_SINK_HH
#define _MPEG_VIDEO_RTP_SINK_HH

#ifndef _VIDEO_RTP_SINK_HH
#include "VideoRTPSink.hh"
#endif

class MPEGVideoRTPSink: public VideoRTPSink {
public:
  static MPEGVideoRTPSink* createNew(UsageEnvironment& env, Groupsock* RTPgs);

protected:
  MPEGVideoRTPSink(UsageEnvironment& env, Groupsock* RTPgs);
	// called only by createNew()

  virtual ~MPEGVideoRTPSink();

private: // redefined virtual functions:
  virtual Boolean sourceIsCompatibleWithUs(MediaSource& source);

  virtual void doSpecialFrameHandling(unsigned fragmentationOffset,
                                      unsigned char* frameStart,
                                      unsigned numBytesInFrame,
                                      struct timeval frameTimestamp,
                                      unsigned numRemainingBytes);
  virtual
  Boolean frameCanAppearAfterPacketStart(unsigned char const* frameStart,
					 unsigned numBytesInFrame) const;
  virtual unsigned specialHeaderSize() const;

private:
  // MPEG video-specific state, used to decide how to fill out the
  // video-specific header, and when to include multiple 'frames' in a
  // single outgoing RTP packet.  Eventually we should somehow get this
  // state from the source (MPEGVideoStreamFramer) instead, as the source
  // already has this info itself.
  struct {
    unsigned temporal_reference;
    unsigned char picture_coding_type;
    unsigned char vector_code_bits; // FBV,BFC,FFV,FFC from RFC 2250, sec. 3.4
  } fPictureState;
  Boolean fPreviousFrameWasSlice;
      // used to implement frameCanAppearAfterPacketStart()
  Boolean fSequenceHeaderPresent;
  Boolean fPacketBeginsSlice, fPacketEndsSlice;
};

#endif
