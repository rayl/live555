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
// Demultiplexer for a MPEG 1 or 2 Program Stream
// C++ header

#ifndef _MPEG_DEMUX_HH
#define _MPEG_DEMUX_HH

#ifndef _FRAMED_SOURCE_HH
#include "FramedSource.hh"
#endif

class MPEGDemuxedElementaryStream; // forward

class MPEGDemux: public Medium {
public:
  static MPEGDemux* createNew(UsageEnvironment& env,
			      FramedSource* inputSource);

  MPEGDemuxedElementaryStream* newElementaryStream(unsigned char streamIdTag);

  // Specialized versions of the above for audio and video:
  MPEGDemuxedElementaryStream* newAudioStream();
  MPEGDemuxedElementaryStream* newVideoStream();

  typedef void (afterGettingFunc)(void* clientData, unsigned frameSize,
                                  struct timeval presentationTime);
  typedef void (onCloseFunc)(void* clientData);
  void getNextFrame(unsigned char streamIdTag,
		    unsigned char* to, unsigned maxSize,
		    afterGettingFunc* afterGettingFunc,
		    void* afterGettingClientData,
		    onCloseFunc* onCloseFunc,
		    void* onCloseClientData);
      // similar to FramedSource::getNextFrame(), except that it also
      // takes a stream id tag as parameter.

  void stopGettingFrames(unsigned char streamIdTag);
      // similar to FramedSource::stopGettingFrames(), except that it also
      // takes a stream id tag as parameter.

  static void handleClosure(void* clientData);
      // This should be called (on ourself) if the source is discovered
      // to be closed (i.e., no longer readable)

  FramedSource* inputSource() const { return fInputSource; }

private:
  MPEGDemux(UsageEnvironment& env,
	    FramedSource* inputSource);
      // called only by createNew()
  virtual ~MPEGDemux();

  void registerReadInterest(unsigned char streamIdTag,
			    unsigned char* to, unsigned maxSize,
			    afterGettingFunc* afterGettingFunc,
			    void* afterGettingClientData,
			    onCloseFunc* onCloseFunc,
			    void* onCloseClientData);

  static void continueReadProcessing(void* clientData);
  void continueReadProcessing();

private:
  FramedSource* fInputSource;

  unsigned char fNextAudioStreamNumber;
  unsigned char fNextVideoStreamNumber;

  // A descriptor for each possible stream id tag:
  typedef struct OutputDescriptor {
    // input parameters
    unsigned char* to; unsigned maxSize;
    afterGettingFunc* fAfterGettingFunc;
    void* afterGettingClientData;
    onCloseFunc* fOnCloseFunc;
    void* onCloseClientData;

    // output parameters
    unsigned frameSize; struct timeval presentationTime;

    // status parameters
    Boolean isCurrentlyActive;
    Boolean isCurrentlyAwaitingData;
  } OutputDescriptor_t;
  OutputDescriptor_t fOutput[256];

  unsigned fNumPendingReads;
  Boolean fHaveUndeliveredData;

private: // parsing state
  class MPEGProgramStreamParser* fParser;
  friend class MPEGProgramStreamParser; // hack
};

#endif
