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
// Implementation

#include "QuickTimeFileSink.hh"
#include "QuickTimeGenericRTPSource.hh"
#include "GroupsockHelper.hh"
#if defined(__WIN32__) || defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif

#define fourChar(x,y,z,w) ( ((x)<<24)|((y)<<16)|((z)<<8)|(w) )

////////// SubsessionIOState, ChunkDescriptor ///////////
// A structure used to represent the I/O state of each input 'subsession':

class ChunkDescriptor {
public:
  ChunkDescriptor(unsigned offsetInFile, unsigned size,
		  unsigned frameSize);
  virtual ~ChunkDescriptor();

  ChunkDescriptor* extendChunk(unsigned newOffsetInFile, unsigned newSize,
			       unsigned newFrameSize);
      // this may end up allocating a new chunk instead
public:
  ChunkDescriptor* fNextChunk;
  unsigned fOffsetInFile;
  unsigned fNumFrames;
  unsigned fFrameSize;
};

class SubsessionBuffer {
public:
  SubsessionBuffer() { reset(); }
  void reset() { fBytesInUse = 0; }
  void addBytes(unsigned numBytes) { fBytesInUse += numBytes; }

  unsigned char* dataStart() { return &fData[0]; }
  unsigned char* dataEnd() { return &fData[fBytesInUse]; }
  unsigned bytesInUse() const { return fBytesInUse; }
  unsigned bytesAvailable() const { return sizeof fData - fBytesInUse; }
  
private:
  unsigned char fData[20000];
  unsigned fBytesInUse;
};

class SubsessionIOState {
public:
  SubsessionIOState(QuickTimeFileSink& sink, MediaSubsession& subsession);
  virtual ~SubsessionIOState();

  Boolean setQTstate();
  void setFinalQTstate();

  void afterGettingFrame(unsigned packetDataSize);
  void onSourceClosure();

  Boolean syncOK(struct timeval presentationTime);
      // returns true iff data is usable despite a sync check

public:
  SubsessionBuffer *fBuffer, *fPrevBuffer;
  QuickTimeFileSink& fOurSink;
  MediaSubsession& fOurSubsession;

  unsigned short fLastPacketRTPSeqNum;
  Boolean fOurSourceIsActive;
  Boolean fUseRTPMarkerBitForFrameEnd;

  struct timeval fSyncTime; // used in synchronizing with other streams

  Boolean fQTEnableTrack;
  unsigned fQTcomponentSubtype;
  char const* fQTcomponentName;
  typedef unsigned (QuickTimeFileSink::*atomCreationFunc)();
  atomCreationFunc fQTMediaInformationAtomCreator;
  atomCreationFunc fQTMediaDataAtomCreator;
  char const* fQTAudioDataType;
  unsigned short fQTSoundSampleVersion;
  unsigned fQTTimeScale;
  unsigned fQTTimeUnitsPerSample;
  unsigned fQTBytesPerFrame;
  unsigned fQTSamplesPerFrame;
  // These next fields are derived from the ones above:
  unsigned fQTTotNumSamples;
  unsigned fQTDuration;

  ChunkDescriptor *fHeadChunk, *fTailChunk;
  unsigned fNumChunks;

private:
  void useFrame(SubsessionBuffer& buffer);
};


////////// QuickTimeFileSink implementation //////////

#ifdef BSD
static struct timezone Idunno;
#else
static int Idunno;
#endif

QuickTimeFileSink::QuickTimeFileSink(UsageEnvironment& env,
				     MediaSession& inputSession,
				     FILE* outFid,
				     unsigned short movieWidth,
				     unsigned short movieHeight,
				     unsigned movieFPS,
				     Boolean packetLossCompensate,
				     Boolean syncStreams)
  : Medium(env), fInputSession(inputSession), fOutFid(outFid),
    fPacketLossCompensate(packetLossCompensate),
    fSyncStreams(syncStreams), fAreCurrentlyBeingPlayed(False),
    fLargestRTPtimestampFrequency(0),
    fNumSubsessions(0), fNumSyncedSubsessions(0),
    fMovieWidth(movieWidth), fMovieHeight(movieHeight),
    fMovieFPS(movieFPS), fCurrentTrackNumber(0) {
  fNewestSyncTime.tv_sec = fNewestSyncTime.tv_usec = 0;

  // Set up I/O state for each input subsession:
  MediaSubsessionIterator iter(fInputSession);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    // Ignore subsessions without a data source:
    FramedSource* subsessionSource = subsession->readSource();
    if (subsessionSource == NULL) continue;

    // If "subsession's" SDP description specified screen dimension
    // or frame rate parameters, then use these.  (Note that this must
    // be done before the call to "setQTState()" below.)
    if (subsession->videoWidth() != 0) {
      fMovieWidth = subsession->videoWidth();
    }
    if (subsession->videoHeight() != 0) {
      fMovieHeight = subsession->videoHeight();
    }
    if (subsession->videoFPS() != 0) {
      fMovieFPS = subsession->videoFPS();
    }

    SubsessionIOState* ioState
      = (new SubsessionIOState(*this, *subsession));
    if (ioState == NULL || !ioState->setQTstate()) {
      // We're not able to output a QuickTime track for this subsession
      delete ioState; ioState = NULL;
      continue;
    }
    subsession->miscPtr = (void*)ioState;

    // Also set a 'BYE' handler for this subsession's RTCP instance:
    if (subsession->rtcpInstance() != NULL) {
      subsession->rtcpInstance()->setByeHandler(onRTCPBye, ioState);
    }

    unsigned rtpTimestampFrequency = subsession->rtpTimestampFrequency();
    if (rtpTimestampFrequency > fLargestRTPtimestampFrequency) {
      fLargestRTPtimestampFrequency = rtpTimestampFrequency;
    }

    ++fNumSubsessions;
  }

  // Use the current time as the file's creation and modification
  // time.  Use Apple's time format: seconds since January 1, 1904

  gettimeofday(&fStartTime, &Idunno);
  fAppleCreationTime = fStartTime.tv_sec - 0x83dac000;

  // Begin by writing a "mdat" atom at the start of the file.
  // (Later, when we've finished copying data to the file, we'll come
  // back and fill in its size.)
  fMDATposition = ftell(fOutFid);
  addAtomHeader("mdat");
}

QuickTimeFileSink::~QuickTimeFileSink() {
  completeOutputFile();
}

static FILE* openFileByName(UsageEnvironment& env, char const* fileName);
    // forward

QuickTimeFileSink*
QuickTimeFileSink::createNew(UsageEnvironment& env,
			     MediaSession& inputSession,
			     char const* outputFileName,
			     unsigned short movieWidth,
			     unsigned short movieHeight,
			     unsigned movieFPS,
			     Boolean packetLossCompensate,
			     Boolean syncStreams) {
  QuickTimeFileSink* newSink = NULL;

  do {
    FILE* fid = openFileByName(env, outputFileName);
    if (fid == NULL) break;

    return new QuickTimeFileSink(env, inputSession, fid,
				 movieWidth, movieHeight, movieFPS,
				 packetLossCompensate, syncStreams);
  } while (0);

  delete newSink;
  return NULL;
}

Boolean QuickTimeFileSink::startPlaying(afterPlayingFunc* afterFunc,
					void* afterClientData) {
  // Make sure we're not already being played:
  if (fAreCurrentlyBeingPlayed) {
    envir().setResultMsg("This sink has already been played");
    return False;
  }

  fAreCurrentlyBeingPlayed = True;
  fAfterFunc = afterFunc;
  fAfterClientData = afterClientData;

  return continuePlaying();
}

Boolean QuickTimeFileSink::continuePlaying() {
  // Run through each of our input session's 'subsessions',
  // asking for a frame from each one:
  Boolean haveActiveSubsessions = False; 
  MediaSubsessionIterator iter(fInputSession);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    FramedSource* subsessionSource = subsession->readSource();
    if (subsessionSource == NULL) continue;

    if (subsessionSource->isCurrentlyAwaitingData()) continue;

    SubsessionIOState* ioState
      = (SubsessionIOState*)(subsession->miscPtr); 
    if (ioState == NULL) continue;

    haveActiveSubsessions = True;
    unsigned char* toPtr = ioState->fBuffer->dataEnd();
    unsigned toSize = ioState->fBuffer->bytesAvailable();
    subsessionSource->getNextFrame(toPtr, toSize,
				   afterGettingFrame, ioState,
				   onSourceClosure, ioState);
  }
  if (!haveActiveSubsessions) {
    envir().setResultMsg("No subsessions are currently active");
    return False;
  }

  return True;
}

void QuickTimeFileSink
::afterGettingFrame(void* clientData, unsigned packetDataSize,
		    struct timeval presentationTime) {
  SubsessionIOState* ioState = (SubsessionIOState*)clientData;
  if (!ioState->syncOK(presentationTime)) {
    // Ignore this data:
    ioState->fOurSink.continuePlaying();
    return;
  }
  ioState->afterGettingFrame(packetDataSize);
}

void QuickTimeFileSink::onSourceClosure(void* clientData) {
  SubsessionIOState* ioState = (SubsessionIOState*)clientData;
  ioState->onSourceClosure();
}

void QuickTimeFileSink::onSourceClosure1() {
  // Check whether *all* of the subsession sources have closed.
  // If not, do nothing for now:
  MediaSubsessionIterator iter(fInputSession);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    SubsessionIOState* ioState
      = (SubsessionIOState*)(subsession->miscPtr); 
    if (ioState == NULL) continue;

    if (ioState->fOurSourceIsActive) return; // this source hasn't closed
  }

  completeOutputFile();

  // Call our specified 'after' function:
  if (fAfterFunc != NULL) {
    (*fAfterFunc)(fAfterClientData);
  }
}

void QuickTimeFileSink::onRTCPBye(void* clientData) {
  SubsessionIOState* ioState = (SubsessionIOState*)clientData;

  struct timeval timeNow;
  gettimeofday(&timeNow, &Idunno);
  unsigned secsDiff
    = timeNow.tv_sec - ioState->fOurSink.fStartTime.tv_sec;

  MediaSubsession& subsession = ioState->fOurSubsession;
  fprintf(stderr, "Received RTCP \"BYE\" on \"%s/%s\" subsession (after %d seconds)\n",
          subsession.mediumName(), subsession.codecName(), secsDiff);

  // Handle the reception of a RTCP "BYE" as if the source had closed:
  ioState->onSourceClosure();
}

void QuickTimeFileSink::completeOutputFile() {
  if (fOutFid == NULL) return;

  // Begin by filling in the initial "mdat" atom with the current
  // file size:
  unsigned curFileSize = ftell(fOutFid);
  setWord(fMDATposition, curFileSize);

  // Then, update the QuickTime-specific state for each active track:
  MediaSubsessionIterator iter(fInputSession);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    SubsessionIOState* ioState
      = (SubsessionIOState*)(subsession->miscPtr); 
    if (ioState == NULL) continue;

    ioState->setFinalQTstate();
  }

  // Then, add a "moov" atom for the file metadata:
  addAtom_moov();
}

// NOTE: The following is the same code that's in "FileSink"
// Someday we should share it #####
static FILE* openFileByName(UsageEnvironment& env, char const* fileName) {
  FILE* fid;
    
  // Check for special case 'file names': "stdout" and "stderr"
  if (strcmp(fileName, "stdout") == 0) {
    fid = stdout;
#if defined(__WIN32__) || defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY);       // convert to binary mode
#endif
  } else if (strcmp(fileName, "stderr") == 0) {
    fid = stderr;
#if defined(__WIN32__) || defined(_WIN32)
    _setmode(_fileno(stderr), _O_BINARY);       // convert to binary mode
#endif
  } else {
    fid = fopen(fileName, "wb");
  }

  if (fid == NULL) {
    env.setResultMsg("unable to open file \"", fileName, "\"");
  }

  return fid;
}


////////// SubsessionIOState, ChunkDescriptor implementation ///////////

SubsessionIOState::SubsessionIOState(QuickTimeFileSink& sink,
				     MediaSubsession& subsession)
  : fOurSink(sink), fOurSubsession(subsession),
    fLastPacketRTPSeqNum(0), fUseRTPMarkerBitForFrameEnd(False),
    fHeadChunk(NULL), fTailChunk(NULL), fNumChunks(0) {
  fBuffer = new SubsessionBuffer;
  fPrevBuffer = sink.fPacketLossCompensate ? new SubsessionBuffer : NULL;

  FramedSource* subsessionSource = subsession.readSource();
  fOurSourceIsActive = subsessionSource != NULL;

  fSyncTime.tv_sec = fSyncTime.tv_usec = 0;
}

SubsessionIOState::~SubsessionIOState() {
  delete fBuffer; delete fPrevBuffer;
  delete fHeadChunk;
}

Boolean SubsessionIOState::setQTstate() {
  char const* noCodecWarning = "Warning: We don't implement a QuickTime %s Media Data Type for the \"%s\" track, so we'll insert a dummy '???' Media Data Atom instead.  A separate, codec-specific editing pass will be needed before this track can be played.\n";
  Boolean supportPartiallyOnly = False;

  do {
    fQTEnableTrack = True; // enable this track in the movie by default
    fQTTimeScale = fOurSubsession.rtpTimestampFrequency(); // by default
    fQTTimeUnitsPerSample = 1; // by default
    fQTBytesPerFrame = 0;
        // by default - indicates that the whole packet data is a frame
    fQTSamplesPerFrame = 1; // by default

    // Make sure our subsession's medium is one that we know how to
    // represent in a QuickTime file:
    if (strcmp(fOurSubsession.mediumName(), "audio") == 0) {
      fQTcomponentSubtype = fourChar('s','o','u','n');
      fQTcomponentName = "Apple Sound Media Handler";
      fQTMediaInformationAtomCreator = &QuickTimeFileSink::addAtom_smhd;
      fQTMediaDataAtomCreator
	= &QuickTimeFileSink::addAtom_soundMediaGeneral; // by default
      fQTSoundSampleVersion = 0; // by default

      // Make sure that our subsession's codec is one that we can handle:
      if (strcmp(fOurSubsession.codecName(), "X-QT") == 0 ||
	  strcmp(fOurSubsession.codecName(), "X-QUICKTIME") == 0) {
	fQTMediaDataAtomCreator = &QuickTimeFileSink::addAtom_genericMedia;
      } else if (strcmp(fOurSubsession.codecName(), "PCMU") == 0) {
	fQTAudioDataType = "ulaw";
	fQTBytesPerFrame = 1;
      } else if (strcmp(fOurSubsession.codecName(), "GSM") == 0) {
	fQTAudioDataType = "agsm";
	fQTBytesPerFrame = 33;
	fQTSamplesPerFrame = 160;
      } else if (strcmp(fOurSubsession.codecName(), "PCMA") == 0) {
	fQTAudioDataType = "alaw";
	fQTBytesPerFrame = 1;
      } else if (strcmp(fOurSubsession.codecName(), "QCELP") == 0) {
	fQTMediaDataAtomCreator = &QuickTimeFileSink::addAtom_Qclp;
	fQTSamplesPerFrame = 160;
      } else {
	fprintf(stderr, noCodecWarning,
		"Audio", fOurSubsession.codecName());
	fQTMediaDataAtomCreator = &QuickTimeFileSink::addAtom_dummy;
	fQTEnableTrack = False; // disable this track in the movie
      }
    } else if (strcmp(fOurSubsession.mediumName(), "video") == 0) {
      fQTcomponentSubtype = fourChar('v','i','d','e');
      fQTcomponentName = "Apple Video Media Handler";
      fQTMediaInformationAtomCreator = &QuickTimeFileSink::addAtom_vmhd;

      // Make sure that our subsession's codec is one that we can handle:
      if (strcmp(fOurSubsession.codecName(), "X-QT") == 0 ||
	  strcmp(fOurSubsession.codecName(), "X-QUICKTIME") == 0) {
	fQTMediaDataAtomCreator = &QuickTimeFileSink::addAtom_genericMedia;
      } else if (strcmp(fOurSubsession.codecName(), "H263-1998") == 0) {
	fQTMediaDataAtomCreator = &QuickTimeFileSink::addAtom_h263;
	fQTTimeScale = 600;
	fQTTimeUnitsPerSample = fQTTimeScale/fOurSink.fMovieFPS;
      } else {
	fprintf(stderr, noCodecWarning,
		"Video", fOurSubsession.codecName());
	fQTMediaDataAtomCreator = &QuickTimeFileSink::addAtom_dummy;
	fQTEnableTrack = False; // disable this track in the movie
      }
    } else {
      fprintf(stderr, "Warning: We don't implement a QuickTime Media Handler for media type \"%s\"",
	      fOurSubsession.mediumName());
      break;
    }

    if (supportPartiallyOnly) {
      fprintf(stderr, "Warning: We don't have sufficient codec-specific information (e.g., sample sizes) to fully generate the \"%s/%s\" track, so we'll disable this track in the movie.  A separate, codec-specific editing pass will be needed before this track can be played.\n",
	      fOurSubsession.mediumName(), fOurSubsession.codecName());
      fQTEnableTrack = False; // disable this track in the movie
    }

    return True;
  } while (0);

  fprintf(stderr, ", so a track for the \"%s/%s\" subsession will not be included in the output QuickTime file\n",
	  fOurSubsession.mediumName(), fOurSubsession.codecName());
  return False; 
}

void SubsessionIOState::setFinalQTstate() {
  // Compute derived parameters, by running through the list of chunks:
  fQTTotNumSamples = fQTDuration = 0;

  ChunkDescriptor *chunk = fHeadChunk;
  while (chunk != NULL) {
    unsigned numSamples = chunk->fNumFrames*fQTSamplesPerFrame;

    fQTTotNumSamples += numSamples;
    fQTDuration += numSamples*fQTTimeUnitsPerSample;

    chunk = chunk->fNextChunk;
  }
}

void SubsessionIOState::afterGettingFrame(unsigned packetDataSize) {
  // Begin by checking whether there was a gap in the RTP stream.
  // If so, try to compensate for this (if desired):
  unsigned short rtpSeqNum
    = fOurSubsession.rtpSource()->curPacketRTPSeqNum();
  if (fOurSink.fPacketLossCompensate && fPrevBuffer->bytesInUse() > 0) {
    short seqNumGap = rtpSeqNum - fLastPacketRTPSeqNum;
    for (short i = 1; i < seqNumGap; ++i) {
      // Insert a copy of the previous frame, to compensate for the loss:
      useFrame(*fPrevBuffer);
    }
  }
  fLastPacketRTPSeqNum = rtpSeqNum;

  // Now, continue working with the frame that we just got
  fBuffer->addBytes(packetDataSize);

  // If our RTP source is a "QuickTimeGenericRTPSource", then
  // use its 'qtState' to set some parameters that we need:
  if (fQTMediaDataAtomCreator == &QuickTimeFileSink::addAtom_genericMedia){
    QuickTimeGenericRTPSource* rtpSource
      = (QuickTimeGenericRTPSource*)fOurSubsession.rtpSource();
    QuickTimeGenericRTPSource::QTState& qtState = rtpSource->qtState;
    fQTTimeScale = qtState.timescale;
    if (qtState.width != 0) {
      fOurSink.fMovieWidth = qtState.width;
    }
    if (qtState.height != 0) {
      fOurSink.fMovieHeight = qtState.height;
    }
    if (qtState.PCK == 3) { // frames can be split over multiple packets
      fUseRTPMarkerBitForFrameEnd = True;
    }

    // Also, if the media type in the "sdAtom" is one that we recognize
    // to have a special parameters, then fix this here:
    if (qtState.sdAtomSize >= 8) {
      char const* atom = qtState.sdAtom;
      unsigned mediaType = fourChar(atom[4],atom[5],atom[6],atom[7]);
      switch (mediaType) {
      case fourChar('a','g','s','m'): {
	fQTBytesPerFrame = 33;
	fQTSamplesPerFrame = 160;
	break;
      }
      case fourChar('Q','c','l','p'): {
	fQTBytesPerFrame = 35;
	fQTSamplesPerFrame = 160;
	break;
      }
      case fourChar('H','c','l','p'): {
	fQTBytesPerFrame = 17;
	fQTSamplesPerFrame = 160;
	break;
      }
      case fourChar('h','2','6','3'): {
	fQTTimeUnitsPerSample = fQTTimeScale/fOurSink.fMovieFPS;
	fUseRTPMarkerBitForFrameEnd = True;
	break;
      }
      }
    }
  } else if (fQTMediaDataAtomCreator == &QuickTimeFileSink::addAtom_Qclp) {
    // For QCELP data, make a note of the frame size (even though it's the
    // same as the packet data size), because it varies depending on the
    // 'rate' of the stream, and this size gets used later when setting up
    // the 'Qclp' QuickTime atom:
    fQTBytesPerFrame = packetDataSize;
  }

  // Check whether we have a complete frame:
  Boolean haveCompleteFrame = True;
  if (fUseRTPMarkerBitForFrameEnd) {
    RTPSource* rtpSource = fOurSubsession.rtpSource();
    if (rtpSource != NULL && !rtpSource->curPacketMarkerBit()) {
      haveCompleteFrame = False;
    }
  }

  if (haveCompleteFrame) {
    useFrame(*fBuffer);
    if (fOurSink.fPacketLossCompensate) {
      // Save this frame, in case we need it for recovery:
      SubsessionBuffer* tmp = fPrevBuffer; // assert: != NULL
      fPrevBuffer = fBuffer;
      fBuffer = tmp;
    }
    fBuffer->reset(); // for the next input
  }

  // Now, try getting more frames:
  fOurSink.continuePlaying();
}

void SubsessionIOState::useFrame(SubsessionBuffer& buffer) {
  unsigned packetDataSize = buffer.bytesInUse();

  // Figure out the bytes-per-sample for this data:
  unsigned frameSize = fQTBytesPerFrame;
  if (frameSize == 0) {
    // The entire packet data is assumed to be a frame:
    frameSize = packetDataSize;
  }

  // Record the information about which 'chunk' this data belongs to:
  FILE* outFid = fOurSink.fOutFid;
  ChunkDescriptor* newTailChunk;
  if (fTailChunk == NULL) {
    newTailChunk = fHeadChunk
      = new ChunkDescriptor(ftell(outFid), packetDataSize, frameSize);
  } else {
    newTailChunk = fTailChunk->extendChunk(ftell(outFid), packetDataSize,
					   frameSize);
  }
  if (newTailChunk != fTailChunk) {
   // This data created a new chunk, rather than extending the old one
    ++fNumChunks;
    fTailChunk = newTailChunk;
  }

  // Write the data into the file:
  fwrite(buffer.dataStart(), packetDataSize, 1, outFid);
}

void SubsessionIOState::onSourceClosure() {
  fOurSourceIsActive = False;
  fOurSink.onSourceClosure1();
}

static Boolean timevalGE(struct timeval const& tv1,
			 struct timeval const& tv2) {
  return (unsigned)tv1.tv_sec > (unsigned)tv2.tv_sec
    || (tv1.tv_sec == tv2.tv_sec
	&& (unsigned)tv1.tv_usec >= (unsigned)tv2.tv_usec);
}

Boolean SubsessionIOState::syncOK(struct timeval presentationTime) {
  QuickTimeFileSink& s = fOurSink; // abbreviation
  if (!s.fSyncStreams) return True; // we don't care

  if (s.fNumSyncedSubsessions < s.fNumSubsessions) {
    // Not all subsessions have yet been synced.  Check whether ours was
    // one of the unsynced ones, and, if so, whether it is now synced:
    if (fSyncTime.tv_sec == 0 && fSyncTime.tv_usec == 0) {
      // We weren't synchronized before
      if (fOurSubsession.rtpSource()->hasBeenSynchronizedUsingRTCP()) {
	// But now we are
	fSyncTime = presentationTime;
	++s.fNumSyncedSubsessions;

	if (timevalGE(fSyncTime, s.fNewestSyncTime)) {
	  s.fNewestSyncTime = fSyncTime;
	}
      }
    }
  }
    
  // Check again whether all subsessions have been synced:
  if (s.fNumSyncedSubsessions < s.fNumSubsessions) return False;

  // Allow this data if it is more recent than the newest sync time:
  return timevalGE(presentationTime, s.fNewestSyncTime);
}

ChunkDescriptor::ChunkDescriptor(unsigned offsetInFile, unsigned size,
				 unsigned frameSize)
  : fNextChunk(NULL), fOffsetInFile(offsetInFile),
    fNumFrames(size/frameSize), fFrameSize(frameSize) {
}

ChunkDescriptor::~ChunkDescriptor() {
  delete fNextChunk;
}

ChunkDescriptor* ChunkDescriptor::extendChunk(unsigned newOffsetInFile,
					      unsigned newSize,
					      unsigned newFrameSize) {
  // First, check whether the new space is just at the end of this
  // existing chunk:
  if (newOffsetInFile == fOffsetInFile + fNumFrames*fFrameSize) {
    // We can extend this existing chunk, provided that the frame size
    // hasn't changed:
    if (newFrameSize == fFrameSize) {
      fNumFrames += newSize/fFrameSize;
      return this;
    }
  }

  // We'll allocate a new ChunkDescriptor, and link it to the end of us:
  ChunkDescriptor* newDescriptor
    = new ChunkDescriptor(newOffsetInFile, newSize, newFrameSize);

  fNextChunk = newDescriptor;

  return newDescriptor;
}


////////// QuickTime-specific implementation //////////

unsigned QuickTimeFileSink::addWord(unsigned word) {
  putc(word>>24, fOutFid); putc(word>>16, fOutFid);
  putc(word>>8, fOutFid); putc(word, fOutFid);

  return 4;
}

unsigned QuickTimeFileSink::addHalfWord(unsigned short halfWord) {
  putc(halfWord>>8, fOutFid); putc(halfWord, fOutFid);

  return 2;
}

unsigned QuickTimeFileSink::addZeroWords(unsigned numWords) {
  for (unsigned i = 0; i < numWords; ++i) {
    addWord(0);
  }

  return numWords*4;
}

unsigned QuickTimeFileSink::add4ByteString(char const* str) {
  putc(str[0], fOutFid); putc(str[1], fOutFid); 
  putc(str[2], fOutFid); putc(str[3], fOutFid); 

  return 4;
}

unsigned QuickTimeFileSink::addArbitraryString(char const* str) {
  // Begin with a single byte representing the string's length
  // (Is there an extension mechanism here? To be safe, abort if
  //  the length is >= 128.)
  unsigned strLength = strlen(str);
  if (strLength >= 128) {
    fprintf(stderr, "QuickTimeFileSink::addArbitraryString() saw string longer than we know how to handle (%d)\n", strLength);
    exit(1);
  }
  putc((unsigned char)strLength, fOutFid);
  unsigned size = 1;

  while (*str != '\0') {
    putc(*str++, fOutFid);
    ++size;
  }

  return size;
}

unsigned QuickTimeFileSink::addAtomHeader(char const* atomName) {
  // Output a placeholder for the 4-byte size:
  addWord(0);

  // Output the 4-byte atom name:
  add4ByteString(atomName);

  return 8;
}

void QuickTimeFileSink::setWord(unsigned filePosn, unsigned size) {
  do {
    if (fseek(fOutFid, filePosn, SEEK_SET) < 0) break;
    addWord(size);
    if (fseek(fOutFid, 0, SEEK_END) < 0) break; // go back to where we were

    return;
  } while (0);

  // One of the fseek()s failed, probable because we're not a seekable file
  fprintf(stderr, "QuickTimeFileSink::setWord(): fseek failed (err %d)\n",
	  errno);
  exit(1);
}

// Methods for writing particular atoms.  Note the following macros:

#define addAtom(name) \
    unsigned QuickTimeFileSink::addAtom_##name() { \
    unsigned initFilePosn = ftell(fOutFid); \
    unsigned size = addAtomHeader("" #name "")

#define addAtomEnd \
  setWord(initFilePosn, size); \
  return size; \
}

addAtom(moov);
  size += addAtom_mvhd();

  // Add a 'trak' atom for each subsession:
  // (For some unknown reason, QuickTime Player (5.0 at least)
  //  doesn't display the movie correctly unless the audio track
  //  (if present) appears before the video track.  So ensure this here.)
  MediaSubsessionIterator iter(fInputSession);
  while ((fCurrentSubsession = iter.next()) != NULL) {
    fCurrentIOState = (SubsessionIOState*)(fCurrentSubsession->miscPtr); 
    if (fCurrentIOState == NULL) continue;
    if (strcmp(fCurrentSubsession->mediumName(), "audio") != 0) continue;

    size += addAtom_trak();
  }
  iter.reset();
  while ((fCurrentSubsession = iter.next()) != NULL) {
    fCurrentIOState = (SubsessionIOState*)(fCurrentSubsession->miscPtr); 
    if (fCurrentIOState == NULL) continue;
    if (strcmp(fCurrentSubsession->mediumName(), "audio") == 0) continue;

    size += addAtom_trak();
  }
addAtomEnd;

addAtom(mvhd);
  size += addWord(0x00000000); // Version + Flags
  size += addWord(fAppleCreationTime); // Creation time
  size += addWord(fAppleCreationTime); // Modification time

  // For the "Time scale" field, use the largest RTP timestamp frequency
  // that we saw in any of the subsessions.
  unsigned const movieTimeScale = fLargestRTPtimestampFrequency;
  size += addWord(movieTimeScale); // Time scale

  unsigned const duration
    = (unsigned)(fInputSession.playEndTime()*movieTimeScale);
  size += addWord(duration); // Duration

  size += addWord(0x00010000); // Preferred rate
  size += addWord(0x01000000); // Preferred volume + Reserved[0]
  size += addZeroWords(2); // Reserved[1-2]
  size += addWord(0x00010000); // matrix top left corner
  size += addZeroWords(3); // matrix
  size += addWord(0x00010000); // matrix center
  size += addZeroWords(3); // matrix
  size += addWord(0x40000000); // matrix bottom right corner
  size += addZeroWords(6); // various time fields
  size += addWord(fNumSubsessions+1); // Next track ID
addAtomEnd;

addAtom(trak);
  size += addAtom_tkhd();
  size += addAtom_mdia();
addAtomEnd;

addAtom(tkhd);
  if (fCurrentIOState->fQTEnableTrack) {
    size += addWord(0x0000000F); // Version +  Flags
  } else {
    // Disable this track in the movie:
    size += addWord(0x00000000); // Version +  Flags
  }
  size += addWord(fAppleCreationTime); // Creation time
  size += addWord(fAppleCreationTime); // Modification time
  size += addWord(++fCurrentTrackNumber); // Track ID
  size += addWord(0x00000000); // Reserved

  unsigned const movieTimeScale = fLargestRTPtimestampFrequency;
  unsigned const trackTimeScale = fCurrentIOState->fQTTimeScale;
  double trackToMovieTimeScale = movieTimeScale/(double)trackTimeScale;
  unsigned const duration = fCurrentIOState->fQTDuration; // in track units
  // Convert to the movie time scale:
  unsigned const mduration = (unsigned)(duration*trackToMovieTimeScale);
  size += addWord(mduration); // Duration
  size += addZeroWords(3); // Reserved+Layer+Alternate grp
  size += addWord(0x01000000); // Volume + Reserved
  size += addWord(0x00010000); // matrix top left corner
  size += addZeroWords(3); // matrix
  size += addWord(0x00010000); // matrix center
  size += addZeroWords(3); // matrix
  size += addWord(0x40000000); // matrix bottom right corner
  size += addWord(fMovieWidth<<16); // Track width
  size += addWord(fMovieHeight<<16); // Track height
addAtomEnd;

addAtom(mdia);
  size += addAtom_mdhd();
  size += addAtom_hdlr();
  size += addAtom_minf();
addAtomEnd;

addAtom(mdhd);
  size += addWord(0x00000000); // Version + Flags
  size += addWord(fAppleCreationTime); // Creation time
  size += addWord(fAppleCreationTime); // Modification time

  unsigned const timeScale = fCurrentIOState->fQTTimeScale;
  size += addWord(timeScale); // Time scale

  unsigned const duration = fCurrentIOState->fQTDuration;
  size += addWord(duration); // Duration

  size += addWord(0x00000000); // Language+Quality
addAtomEnd;

addAtom(hdlr);
  size += addWord(0x00000000); // Version + Flags
  size += add4ByteString("mhlr"); // Component type

  size += addWord(fCurrentIOState->fQTcomponentSubtype);// Component subtype
  size += add4ByteString("appl"); // Component manufacturer
  size += addWord(0x00000000); // Component flags
  size += addWord(0x00000000); // Component flags mask
  size += addArbitraryString(fCurrentIOState->fQTcomponentName);
    // Component name
addAtomEnd;

addAtom(minf);
  SubsessionIOState::atomCreationFunc mediaInformationAtomCreator
    = fCurrentIOState->fQTMediaInformationAtomCreator;
  size += (this->*mediaInformationAtomCreator)();
  size += addAtom_hdlr2();
  size += addAtom_dinf();
  size += addAtom_stbl();
addAtomEnd;

addAtom(smhd);
  size += addZeroWords(2); // Version+Flags+Balance+Reserved
addAtomEnd;

addAtom(vmhd);
  size += addWord(0x00000001); // Version + Flags
  size += addWord(0x00408000); // Graphics mode + Opcolor[red]
  size += addWord(0x80008000); // Opcolor[green} + Opcolor[blue]
addAtomEnd;

unsigned QuickTimeFileSink::addAtom_hdlr2() {
  unsigned initFilePosn = ftell(fOutFid);
  unsigned size = addAtomHeader("hdlr");
  size += addWord(0x00000000); // Version + Flags
  size += add4ByteString("dhlr"); // Component type
  size += add4ByteString("alis"); // Component subtype
  size += add4ByteString("appl"); // Component manufacturer
  size += addZeroWords(2); // Component flags+Component flags mask
  size += addArbitraryString("Apple Alias Data Handler"); // Component name
addAtomEnd;

addAtom(dinf);
  size += addAtom_dref();
addAtomEnd;

addAtom(dref);
  size += addWord(0x00000000); // Version + Flags
  size += addWord(0x00000001); // Number of entries
  size += addAtom_alis();
addAtomEnd;

addAtom(alis);
  size += addWord(0x00000001); // Version + Flags
addAtomEnd;

addAtom(stbl);
  size += addAtom_stsd();
  size += addAtom_stts();
  size += addAtom_stsc();
  size += addAtom_stsz();
  size += addAtom_stco();
addAtomEnd;

addAtom(stsd);
  size += addWord(0x00000000); // Version+Flags
  size += addWord(0x00000001); // Number of entries
  SubsessionIOState::atomCreationFunc mediaDataAtomCreator
    = fCurrentIOState->fQTMediaDataAtomCreator;
  size += (this->*mediaDataAtomCreator)();
addAtomEnd;

unsigned QuickTimeFileSink::addAtom_genericMedia() {
  unsigned initFilePosn = ftell(fOutFid);

  // Our source is assumed to be a "QuickTimeGenericRTPSource"
  // Use its "sdAtom" state for our contents:
  QuickTimeGenericRTPSource* rtpSource = (QuickTimeGenericRTPSource*)
    fCurrentIOState->fOurSubsession.rtpSource();
  QuickTimeGenericRTPSource::QTState& qtState = rtpSource->qtState;
  char const* from = qtState.sdAtom;
  unsigned size = qtState.sdAtomSize;
  for (unsigned i = 0; i < size; ++i) putc(from[i], fOutFid);
addAtomEnd;

unsigned QuickTimeFileSink::addAtom_soundMediaGeneral() {
  unsigned initFilePosn = ftell(fOutFid);
  unsigned size = addAtomHeader(fCurrentIOState->fQTAudioDataType);

// General sample description fields:
  size += addWord(0x00000000); // Reserved
  size += addWord(0x00000001); // Reserved+Data reference index
// Sound sample description fields:
  unsigned short const version = fCurrentIOState->fQTSoundSampleVersion;
  size += addWord(version<<16); // Version+Revision level
  size += addWord(0x00000000); // Vendor
  size += addWord(0x00010010); // Number of channels+Sample size
      // should we parameterize this??? #####
  size += addWord(0x00000000); // Compression ID+Packet size

  unsigned const timeScale = fCurrentIOState->fQTTimeScale;
  unsigned const timeUnitsPerSample
    = fCurrentIOState->fQTTimeUnitsPerSample;
  double const sampleRate = timeScale/(double)timeUnitsPerSample;
  unsigned const sampleRateIntPart = (unsigned)sampleRate;
  unsigned const sampleRateFracPart = (unsigned)(sampleRate*65536);
  unsigned const sampleRateFixedPoint
    = (sampleRateIntPart<<16) | sampleRateFracPart;
  size += addWord(sampleRateFixedPoint); // Sample rate
addAtomEnd;

unsigned QuickTimeFileSink::addAtom_Qclp() {
  // The beginning of this atom looks just like a general Sound Media atom,
  // except with a version field of 1:
  unsigned initFilePosn = ftell(fOutFid); \
  fCurrentIOState->fQTAudioDataType = "Qclp";
  fCurrentIOState->fQTSoundSampleVersion = 1;
  unsigned size = addAtom_soundMediaGeneral();

  // Next, add the four fields that are particular to version 1:
  // (Later, parameterize these #####)
  size += addWord(0x000000a0); // samples per packet
  size += addWord(fCurrentIOState->fQTBytesPerFrame); // bytes per packet
  size += addWord(fCurrentIOState->fQTBytesPerFrame); // bytes per frame
  size += addWord(0x00000002); // bytes per sample (uncompressed)

  // Other special fields are in a 'wave' atom that follows:
  size += addAtom_wave();
addAtomEnd;

addAtom(wave);
  size += addAtom_frma();
  size += addWord(0x00000014); // ???
  size += add4ByteString("Qclp"); // ???
  if (fCurrentIOState->fQTBytesPerFrame == 35) {
    size += addAtom_Fclp(); // full-rate QCELP
  } else {
    size += addAtom_Hclp(); // half-rate QCELP
  } // what about other QCELP 'rates'??? #####
  size += addWord(0x00000008); // ???
  size += addWord(0x00000000); // ???
  size += addWord(0x00000000); // ???
  size += addWord(0x00000008); // ???
addAtomEnd;

addAtom(frma);
  size += add4ByteString("Qclp"); // ???
addAtomEnd;

addAtom(Fclp);
 size += addWord(0x00000000); // ???
addAtomEnd;

addAtom(Hclp);
 size += addWord(0x00000000); // ???
addAtomEnd;

addAtom(h263);
// General sample description fields:
  size += addWord(0x00000000); // Reserved
  size += addWord(0x00000001); // Reserved+Data reference index
// Video sample description fields:
  size += addWord(0x00020001); // Version+Revision level
  size += add4ByteString("appl"); // Vendor
  size += addWord(0x00000000); // Temporal quality
  size += addWord(0x000002fc); // Spatial quality
  unsigned const widthAndHeight = (fMovieWidth<<16)|fMovieHeight;
  size += addWord(widthAndHeight); // Width+height
  size += addWord(0x00480000); // Horizontal resolution
  size += addWord(0x00480000); // Vertical resolution
  size += addWord(0x00000000); // Data size
  size += addWord(0x00010548); // Frame count+Compressor name (start)
    // "H.263"
  size += addWord(0x2e323633); // Frame count+Compressor name (cont)
  size += addZeroWords(6); // Compressor name (continued - zero)
  size += addWord(0x00000018); // Compressor name (final)+Depth
  size += addHalfWord(0xffff); // Color table id
addAtomEnd;

addAtom(stts); // Time-to-Sample
  size += addWord(0x00000000); // Version+flags

  // Assume that all samples have the same duration
  // (Are there any codecs for which this assumption doesn't hold???) #####
  size += addWord(0x00000001); // Number of entries
  size += addWord(fCurrentIOState->fQTTotNumSamples); // Sample count
  size += addWord(fCurrentIOState->fQTTimeUnitsPerSample);//Sample duration
addAtomEnd;

addAtom(stsc); // Sample-to-Chunk
  size += addWord(0x00000000); // Version+flags

  // First, add a dummy "Number of entries" field
  // (and remember its position).  We'll fill this field in later:
  unsigned numEntriesPosition = ftell(fOutFid);
  size += addWord(0); // dummy for "Number of entries"

  // Then, run through the chunk descriptors, and enter the entries
  // in this (compressed) Sample-to-Chunk table:
  unsigned numEntries = 0, chunkNumber = 0;
  unsigned prevSamplesPerChunk = ~0;
  ChunkDescriptor *chunk = fCurrentIOState->fHeadChunk;
  while (chunk != NULL) {
    ++chunkNumber;
    unsigned const samplesPerChunk
      = chunk->fNumFrames*(fCurrentIOState->fQTSamplesPerFrame);
    if (samplesPerChunk != prevSamplesPerChunk) {
      // This chunk will be a new table entry:
      ++numEntries;
      size += addWord(chunkNumber); // Chunk number
      size += addWord(samplesPerChunk); // Samples per chunk
      size += addWord(0x00000001); // Sample description ID

      prevSamplesPerChunk = samplesPerChunk;
    }
    chunk = chunk->fNextChunk;
  }

  // Now go back and fill in the "Number of entries" field:
  setWord(numEntriesPosition, numEntries);
addAtomEnd;

addAtom(stsz); // Sample Size
  size += addWord(0x00000000); // Version+flags

  // Begin by checking whether our chunks all have the same
  // 'bytes-per-sample'.  This determines whether this atom's table
  // has just a single entry, or multiple entries.
  Boolean haveSingleEntryTable = True;
  double firstBPS = 0.0;
  ChunkDescriptor *chunk = fCurrentIOState->fHeadChunk;
  while (chunk != NULL) {
    double bps = chunk->fFrameSize/(fCurrentIOState->fQTSamplesPerFrame);
    if (bps < 1.0) {
      // I don't think a multiple-entry table would make sense in
      // this case, so assume a single entry table ??? #####
      break;
    }

    if (firstBPS == 0.0) {
      firstBPS = bps;
    } else if (bps != firstBPS) {
      haveSingleEntryTable = False;
      break;
    }

    chunk = chunk->fNextChunk;
  }

  unsigned sampleSize;
  if (haveSingleEntryTable) {
    sampleSize = fCurrentIOState->fQTTimeUnitsPerSample; //???
  } else {
    sampleSize = 0; // indicates a multiple-entry table
  }
  size += addWord(sampleSize); // Sample size
  unsigned const totNumSamples = fCurrentIOState->fQTTotNumSamples;
  size += addWord(totNumSamples); // Number of entries

  if (!haveSingleEntryTable) {
    // Multiple-entry table:
    // Run through the chunk descriptors, entering the sample sizes:
    ChunkDescriptor *chunk = fCurrentIOState->fHeadChunk;
    while (chunk != NULL) {
      unsigned numSamples
	= chunk->fNumFrames*(fCurrentIOState->fQTSamplesPerFrame);
      unsigned sampleSize
	= chunk->fFrameSize/(fCurrentIOState->fQTSamplesPerFrame);
      for (unsigned i = 0; i < numSamples; ++i) {
	size += addWord(sampleSize);
      }

      chunk = chunk->fNextChunk;
    }
  }
addAtomEnd;

addAtom(stco); // Chunk Offset
  size += addWord(0x00000000); // Version+flags
  size += addWord(fCurrentIOState->fNumChunks); // Number of entries

  // Run through the chunk descriptors, entering the file offsets:
  ChunkDescriptor *chunk = fCurrentIOState->fHeadChunk;
  while (chunk != NULL) {
    size += addWord(chunk->fOffsetInFile);

    chunk = chunk->fNextChunk;
  }
addAtomEnd;

// A dummy atom (with name '????'):
unsigned QuickTimeFileSink::addAtom_dummy() {
    unsigned initFilePosn = ftell(fOutFid);
    unsigned size = addAtomHeader("????");
addAtomEnd;
