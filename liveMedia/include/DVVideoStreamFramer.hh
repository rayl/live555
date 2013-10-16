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
// Copyright (c) 1996-2010 Live Networks, Inc.  All rights reserved.
// A filter that parses a DV input stream into DV frames to deliver to the downstream object
// C++ header

#ifndef _DV_VIDEO_STREAM_FRAMER_HH
#define _DV_VIDEO_STREAM_FRAMER_HH

#ifndef _FRAMED_FILTER_HH
#include "FramedFilter.hh"
#endif

#define DV_DIF_BLOCK_SIZE 80
#define DV_SAVED_INITIAL_BLOCKS_SIZE (6*DV_DIF_BLOCK_SIZE) /* enough initial data to parse the profile */

class DVVideoStreamFramer: public FramedFilter {
public:
  static DVVideoStreamFramer*
  createNew(UsageEnvironment& env, FramedSource* inputSource);

  char const* profileName();

protected:
  DVVideoStreamFramer(UsageEnvironment& env,
		      FramedSource* inputSource);
      // called only by createNew(), or by subclass constructors
  virtual ~DVVideoStreamFramer();

private:
  // redefined virtual functions:
  virtual Boolean isDVVideoStreamFramer() const;
  virtual void doGetNextFrame();

private:
  static void afterGettingFrame(void* clientData, unsigned frameSize,
                                unsigned numTruncatedBytes,
                                struct timeval presentationTime,
                                unsigned durationInMicroseconds);
  void afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes);

private:
  void const* fOurProfile;
  struct timeval fNextFramePresentationTime;
  unsigned char fSavedInitialBlocks[DV_SAVED_INITIAL_BLOCKS_SIZE];
  char fInitialBlocksPresent;
};

#endif
