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
// A sink that generates a QuickTime file from a composite media session
// C++ header

#ifndef _QUICKTIME_FILE_SINK_HH
#define _QUICKTIME_FILE_SINK_HH

#ifndef _MEDIA_SESSION_HH
#include "MediaSession.hh"
#endif

class QuickTimeFileSink: public Medium {
public:
  static QuickTimeFileSink* createNew(UsageEnvironment& env,
				      MediaSession& inputSession,
				      char const* outputFileName,
				      unsigned short movieWidth = 240,
				      unsigned short movieHeight = 180,
				      unsigned movieFPS = 15,
				      Boolean packetLossCompensate = False,
				      Boolean syncStreams = False,
				      Boolean generateHintTracks = False);

  typedef void (afterPlayingFunc)(void* clientData);
  Boolean startPlaying(afterPlayingFunc* afterFunc,
                       void* afterClientData);

  unsigned numActiveSubsessions() const { return fNumSubsessions; }

private:
  QuickTimeFileSink(UsageEnvironment& env, MediaSession& inputSession,
		    FILE* outFid,
		    unsigned short movieWidth, unsigned short movieHeight,
		    unsigned movieFPS, Boolean packetLossCompensate,
		    Boolean syncStreams, Boolean generateHintTracks);
      // called only by createNew()
  virtual ~QuickTimeFileSink();

  Boolean continuePlaying();
  static void afterGettingFrame(void* clientData, unsigned frameSize,
				struct timeval presentationTime);
  static void onSourceClosure(void* clientData);
  void onSourceClosure1();
  static void onRTCPBye(void* clientData);
  void completeOutputFile();

private:
  friend class SubsessionIOState;
  MediaSession& fInputSession;
  FILE* fOutFid;
  Boolean fPacketLossCompensate;
  Boolean fSyncStreams;
  struct timeval fNewestSyncTime, fFirstDataTime;
  double fMaxTrackDuration;
  Boolean fAreCurrentlyBeingPlayed;
  afterPlayingFunc* fAfterFunc;
  void* fAfterClientData;
  unsigned fAppleCreationTime;
  unsigned fLargestRTPtimestampFrequency;
  unsigned fNumSubsessions, fNumSyncedSubsessions;
  struct timeval fStartTime;

private:
  ///// Definitions specific to the QuickTime file format:

  unsigned addWord(unsigned word);
  unsigned addHalfWord(unsigned short halfWord);
  unsigned QuickTimeFileSink::addByte(unsigned char byte) {
    putc(byte, fOutFid);
    return 1;
  }
  unsigned addZeroWords(unsigned numWords);
  unsigned add4ByteString(char const* str);
  unsigned addArbitraryString(char const* str,
			      Boolean oneByteLength = True);
  unsigned addAtomHeader(char const* atomName);
      // strlen(atomName) must be 4
  void setWord(unsigned filePosn, unsigned size);

  // Define member functions for outputting various types of atom:
#define _atom(name) unsigned addAtom_##name()
  _atom(moov);
  _atom(mvhd);
      _atom(trak);
          _atom(tkhd);
          _atom(edts);
              _atom(elst);
          _atom(tref);
              _atom(hint);
          _atom(mdia);
              _atom(mdhd);
              _atom(hdlr);
              _atom(minf);
                  _atom(smhd);
                  _atom(vmhd);
                  _atom(gmhd);
                      _atom(gmin);
                  unsigned addAtom_hdlr2();
                  _atom(dinf);
                      _atom(dref);
                          _atom(alis);
                  _atom(stbl);
                      _atom(stsd);
                          unsigned addAtom_genericMedia();
                          unsigned addAtom_soundMediaGeneral();
                          _atom(ulaw);
                          _atom(alaw);
                          _atom(Qclp);
                              _atom(wave);
                                  _atom(frma);
                                  _atom(Fclp);
                                  _atom(Hclp);
                          _atom(h263);
                          _atom(rtp);
                              _atom(tims);
                      _atom(stts);
                      _atom(stsc);
                      _atom(stsz);
                      _atom(stco);
          _atom(udta);
              _atom(name);
              _atom(hnti);
                  _atom(sdp);
              _atom(hinf);
                  _atom(payt);
  unsigned addAtom_dummy();

private:
  unsigned short fMovieWidth, fMovieHeight;
  unsigned fMovieFPS;
  unsigned fMDATposition;
  MediaSubsession* fCurrentSubsession;
  class SubsessionIOState* fCurrentIOState;
};

#endif
