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
// Copyright (c) 1996-2004 Live Networks, Inc.  All rights reserved.
// A sink that generates a QuickTime file from a composite media session
// Implementation

#if defined(__WIN32__) || defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif
#include <ctype.h>

#include "QuickTimeFileSink.hh"
#include "QuickTimeGenericRTPSource.hh"
#include "GroupsockHelper.hh"
#include "H263plusVideoRTPSource.hh"

#define fourChar(x,y,z,w) ( ((x)<<24)|((y)<<16)|((z)<<8)|(w) )

////////// SubsessionIOState, ChunkDescriptor ///////////
// A structure used to represent the I/O state of each input 'subsession':

class ChunkDescriptor {
public:
  ChunkDescriptor(unsigned offsetInFile, unsigned size,
		  unsigned frameSize, unsigned frameDuration,
		  struct timeval presentationTime);
  virtual ~ChunkDescriptor();

  ChunkDescriptor* extendChunk(unsigned newOffsetInFile, unsigned newSize,
			       unsigned newFrameSize,
			       unsigned newFrameDuration,
			       struct timeval newPresentationTime);
      // this may end up allocating a new chunk instead
public:
  ChunkDescriptor* fNextChunk;
  unsigned fOffsetInFile;
  unsigned fNumFrames;
  unsigned fFrameSize;
  unsigned fFrameDuration;
  struct timeval fPresentationTime; // of the start of the data
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
  
  void setPresentationTime(struct timeval const& presentationTime) {
    fPresentationTime = presentationTime;
  }
  struct timeval const& presentationTime() const {return fPresentationTime;}

private:
  struct timeval fPresentationTime;
  unsigned char fData[20000];
  unsigned fBytesInUse;
};

// A 64-bit counter, used below:

class Count64 {
public:
  Count64() { hi = lo = 0; }

  void operator+=(unsigned arg);

  unsigned hi, lo; // each 32 bits
};

class SubsessionIOState {
public:
  SubsessionIOState(QuickTimeFileSink& sink, MediaSubsession& subsession);
  virtual ~SubsessionIOState();

  Boolean setQTstate();
  void setFinalQTstate();

  void afterGettingFrame(unsigned packetDataSize,
			 struct timeval presentationTime);
  void onSourceClosure();

  Boolean syncOK(struct timeval presentationTime);
      // returns true iff data is usable despite a sync check

  static void setHintTrack(SubsessionIOState* hintedTrack,
			   SubsessionIOState* hintTrack);
  Boolean isHintTrack() const { return fTrackHintedByUs != NULL; }
  Boolean hasHintTrack() const { return fHintTrackForUs != NULL; }

  UsageEnvironment& envir() const { return fOurSink.envir(); }

public:
  static unsigned fCurrentTrackNumber;
  unsigned fTrackID;
  SubsessionIOState* fHintTrackForUs; SubsessionIOState* fTrackHintedByUs;

  SubsessionBuffer *fBuffer, *fPrevBuffer;
  QuickTimeFileSink& fOurSink;
  MediaSubsession& fOurSubsession;

  unsigned short fLastPacketRTPSeqNum;
  Boolean fOurSourceIsActive;

  Boolean fHaveBeenSynced; // used in synchronizing with other streams
  struct timeval fSyncTime;

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
  // These next fields are derived from the ones above,
  // plus the information from each chunk:
  unsigned fQTTotNumSamples;
  unsigned fQTDurationM; // in media time units
  unsigned fQTDurationT; // in track time units
  unsigned fTKHD_durationPosn;
      // position of the duration in the output 'tkhd' atom
  unsigned fQTInitialOffsetDuration;
      // if there's a pause at the beginning

  ChunkDescriptor *fHeadChunk, *fTailChunk;
  unsigned fNumChunks;

  // Counters to be used in the hint track's 'udta'/'hinf' atom;
  struct hinf {
    Count64 trpy;
    Count64 nump;
    Count64 tpyl;
    // Is 'maxr' needed? Computing this would be a PITA. #####
    Count64 dmed;
    Count64 dimm;
    // 'drep' is always 0
    // 'tmin' and 'tmax' are always 0
    unsigned pmax;
    unsigned dmax;
  } fHINF;

private:
  void useFrame(SubsessionBuffer& buffer);
  void useFrameForHinting(unsigned frameSize,
			  struct timeval presentationTime,
			  unsigned startSampleNumber);

  // used by the above two routines:
  unsigned useFrame1(unsigned sourceDataSize,
		     struct timeval presentationTime,
		     unsigned frameDuration, unsigned destFileOffset);
      // returns the number of samples in this data

private:
  // A structure used for temporarily storing frame state:
  struct {
    unsigned frameSize;
    struct timeval presentationTime;
    unsigned destFileOffset; // used for non-hint tracks only

    // The remaining fields are used for hint tracks only:
    unsigned startSampleNumber;
    unsigned rtpHeader;
    unsigned char numSpecialHeaders; // used when our RTP source is H.263+
    unsigned specialHeaderBytesLength; // ditto
    unsigned char specialHeaderBytes[SPECIAL_HEADER_BUFFER_SIZE]; // ditto
    unsigned packetSizes[256];
  } fPrevFrameState;
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
				     Boolean syncStreams,
				     Boolean generateHintTracks)
  : Medium(env), fInputSession(inputSession), fOutFid(outFid),
    fPacketLossCompensate(packetLossCompensate),
    fSyncStreams(syncStreams), fAreCurrentlyBeingPlayed(False),
    fLargestRTPtimestampFrequency(0),
    fNumSubsessions(0), fNumSyncedSubsessions(0),
    fHaveCompletedOutputFile(False),
    fMovieWidth(movieWidth), fMovieHeight(movieHeight),
    fMovieFPS(movieFPS), fMaxTrackDurationM(0) {
  fNewestSyncTime.tv_sec = fNewestSyncTime.tv_usec = 0;
  fFirstDataTime.tv_sec = fFirstDataTime.tv_usec = (unsigned)(~0);

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
      = new SubsessionIOState(*this, *subsession);
    if (ioState == NULL || !ioState->setQTstate()) {
      // We're not able to output a QuickTime track for this subsession
      delete ioState; ioState = NULL;
      continue;
    }
    subsession->miscPtr = (void*)ioState;

    if (generateHintTracks) {
      // Also create a hint track for this track:
      SubsessionIOState* hintTrack
	= new SubsessionIOState(*this, *subsession);
      SubsessionIOState::setHintTrack(ioState, hintTrack);
      if (!hintTrack->setQTstate()) {
	delete hintTrack;
	SubsessionIOState::setHintTrack(ioState, NULL);
      }
    }

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

  // Then, delete each active "SubsessionIOState":
  MediaSubsessionIterator iter(fInputSession);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    SubsessionIOState* ioState
      = (SubsessionIOState*)(subsession->miscPtr); 
    if (ioState == NULL) continue;

    delete ioState->fHintTrackForUs; // if any
    delete ioState;
  }
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
			     Boolean syncStreams,
			     Boolean generateHintTracks) {
  QuickTimeFileSink* newSink = NULL;

  do {
    FILE* fid = openFileByName(env, outputFileName);
    if (fid == NULL) break;

    return new QuickTimeFileSink(env, inputSession, fid,
				 movieWidth, movieHeight, movieFPS,
				 packetLossCompensate, syncStreams,
				 generateHintTracks);
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
		    unsigned /*numTruncatedBytes*/,
		    struct timeval presentationTime,
		    unsigned /*durationInMicroseconds*/) {
  SubsessionIOState* ioState = (SubsessionIOState*)clientData;
  if (!ioState->syncOK(presentationTime)) {
    // Ignore this data:
    ioState->fOurSink.continuePlaying();
    return;
  }
  ioState->afterGettingFrame(packetDataSize, presentationTime);
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
  ioState->envir() << "Received RTCP \"BYE\" on \""
		   << subsession.mediumName()
		   << "/" << subsession.codecName()
		   << "\" subsession (after "
		   << secsDiff << " seconds)\n";

  // Handle the reception of a RTCP "BYE" as if the source had closed:
  ioState->onSourceClosure();
}

static Boolean timevalGE(struct timeval const& tv1,
			 struct timeval const& tv2) {
  return (unsigned)tv1.tv_sec > (unsigned)tv2.tv_sec
    || (tv1.tv_sec == tv2.tv_sec
	&& (unsigned)tv1.tv_usec >= (unsigned)tv2.tv_usec);
}

void QuickTimeFileSink::completeOutputFile() {
  if (fHaveCompletedOutputFile || fOutFid == NULL) return;

  // Begin by filling in the initial "mdat" atom with the current
  // file size:
  unsigned curFileSize = ftell(fOutFid);
  setWord(fMDATposition, curFileSize);

  // Then, note the time of the first received data:
  MediaSubsessionIterator iter(fInputSession);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    SubsessionIOState* ioState
      = (SubsessionIOState*)(subsession->miscPtr); 
    if (ioState == NULL) continue;

    ChunkDescriptor* const headChunk = ioState->fHeadChunk;
    if (headChunk != NULL
	&& timevalGE(fFirstDataTime, headChunk->fPresentationTime)) {
      fFirstDataTime = headChunk->fPresentationTime;
    }
  }

  // Then, update the QuickTime-specific state for each active track:
  iter.reset();
  while ((subsession = iter.next()) != NULL) {
    SubsessionIOState* ioState
      = (SubsessionIOState*)(subsession->miscPtr); 
    if (ioState == NULL) continue;

    ioState->setFinalQTstate();
    // Do the same for a hint track (if any):
    if (ioState->hasHintTrack()) {
      ioState->fHintTrackForUs->setFinalQTstate();
    }
  }

  // Then, add a "moov" atom for the file metadata:
  addAtom_moov();

  // We're done:
  fHaveCompletedOutputFile = True;
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

unsigned SubsessionIOState::fCurrentTrackNumber = 0;

SubsessionIOState::SubsessionIOState(QuickTimeFileSink& sink,
				     MediaSubsession& subsession)
  : fHintTrackForUs(NULL), fTrackHintedByUs(NULL),
    fOurSink(sink), fOurSubsession(subsession),
    fLastPacketRTPSeqNum(0), fHaveBeenSynced(False), fQTTotNumSamples(0),
    fHeadChunk(NULL), fTailChunk(NULL), fNumChunks(0) {
  fTrackID = ++fCurrentTrackNumber;

  fBuffer = new SubsessionBuffer;
  fPrevBuffer = sink.fPacketLossCompensate ? new SubsessionBuffer : NULL;

  FramedSource* subsessionSource = subsession.readSource();
  fOurSourceIsActive = subsessionSource != NULL;

  fPrevFrameState.presentationTime.tv_sec = 0;
  fPrevFrameState.presentationTime.tv_usec = 0;
}

SubsessionIOState::~SubsessionIOState() {
  delete fBuffer; delete fPrevBuffer;
  delete fHeadChunk;
}

Boolean SubsessionIOState::setQTstate() {
  char const* noCodecWarning1 = "Warning: We don't implement a QuickTime ";
  char const* noCodecWarning2 = " Media Data Type for the \"";
  char const* noCodecWarning3 = "\" track, so we'll insert a dummy \"???\" Media Data Atom instead.  A separate, codec-specific editing pass will be needed before this track can be played.\n";
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
    if (isHintTrack()) {
      // Hint tracks are treated specially
      fQTEnableTrack = False; // hint tracks are marked as inactive
      fQTcomponentSubtype = fourChar('h','i','n','t');
      fQTcomponentName = "hint media handler";
      fQTMediaInformationAtomCreator = &QuickTimeFileSink::addAtom_gmhd;
      fQTMediaDataAtomCreator = &QuickTimeFileSink::addAtom_rtp;
    } else if (strcmp(fOurSubsession.mediumName(), "audio") == 0) {
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
	envir() << noCodecWarning1 << "Audio" << noCodecWarning2
		<< fOurSubsession.codecName() << noCodecWarning3;
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
      } else if (strcmp(fOurSubsession.codecName(), "H263-1998") == 0 ||
		 strcmp(fOurSubsession.codecName(), "H263-2000") == 0) {
	fQTMediaDataAtomCreator = &QuickTimeFileSink::addAtom_h263;
	fQTTimeScale = 600;
	fQTTimeUnitsPerSample = fQTTimeScale/fOurSink.fMovieFPS;
      } else {
	envir() << noCodecWarning1 << "Video" << noCodecWarning2
		<< fOurSubsession.codecName() << noCodecWarning3;
	fQTMediaDataAtomCreator = &QuickTimeFileSink::addAtom_dummy;
	fQTEnableTrack = False; // disable this track in the movie
      }
    } else {
      envir() << "Warning: We don't implement a QuickTime Media Handler for media type \""
	      << fOurSubsession.mediumName() << "\"";
      break;
    }

    if (supportPartiallyOnly) {
      envir() << "Warning: We don't have sufficient codec-specific information (e.g., sample sizes) to fully generate the \""
	      << fOurSubsession.mediumName() << "/"
	      << fOurSubsession.codecName()
	      << "\" track, so we'll disable this track in the movie.  A separate, codec-specific editing pass will be needed before this track can be played\n";
      fQTEnableTrack = False; // disable this track in the movie
    }

    return True;
  } while (0);

  envir() << ", so a track for the \"" << fOurSubsession.mediumName()
	  << "/" << fOurSubsession.codecName()
	  << "\" subsession will not be included in the output QuickTime file\n";
  return False; 
}

void SubsessionIOState::setFinalQTstate() {
  // Compute derived parameters, by running through the list of chunks:
  fQTDurationT = 0;

  ChunkDescriptor* chunk = fHeadChunk;
  while (chunk != NULL) {
    unsigned const numFrames = chunk->fNumFrames;
    unsigned const dur
      = numFrames*chunk->fFrameDuration/fOurSubsession.numChannels();

    fQTDurationT += dur;

    chunk = chunk->fNextChunk;
  }

  // Convert this duration from track to movie time scale:
  double scaleFactor = fOurSink.movieTimeScale()/(double)fQTTimeScale;
  fQTDurationM = (unsigned)(fQTDurationT*scaleFactor);

  if (fQTDurationM > fOurSink.fMaxTrackDurationM) {
    fOurSink.fMaxTrackDurationM = fQTDurationM;
  }
}

void SubsessionIOState::afterGettingFrame(unsigned packetDataSize,
					  struct timeval presentationTime) {
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
  if (fBuffer->bytesInUse() == 0) {
    fBuffer->setPresentationTime(presentationTime);
  }
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

  useFrame(*fBuffer);
  if (fOurSink.fPacketLossCompensate) {
    // Save this frame, in case we need it for recovery:
    SubsessionBuffer* tmp = fPrevBuffer; // assert: != NULL
    fPrevBuffer = fBuffer;
    fBuffer = tmp;
  }
  fBuffer->reset(); // for the next input

  // Now, try getting more frames:
  fOurSink.continuePlaying();
}

void SubsessionIOState::useFrame(SubsessionBuffer& buffer) {
  unsigned char* const frameSource = buffer.dataStart();
  unsigned const frameSize = buffer.bytesInUse();
  struct timeval const& presentationTime = buffer.presentationTime();
  unsigned const destFileOffset = ftell(fOurSink.fOutFid);
  unsigned sampleNumberOfFrameStart = fQTTotNumSamples + 1;

  // If we're not syncing streams, or this subsession is not video, then
  // just give this frame a fixed duration:
  if (!fOurSink.fSyncStreams
      || fQTcomponentSubtype != fourChar('v','i','d','e')) {
    unsigned const frameDuration = fQTTimeUnitsPerSample*fQTSamplesPerFrame;

    fQTTotNumSamples += useFrame1(frameSize, presentationTime,
				  frameDuration, destFileOffset);
  } else {
    // For synced video streams, we use the difference between successive
    // frames' presentation times as the 'frame duration'.  So, record
    // information about the *previous* frame:
    struct timeval const& ppt = fPrevFrameState.presentationTime; //abbrev
    if (ppt.tv_sec != 0 || ppt.tv_usec != 0) {
      // There has been a previous frame.
      double duration = (presentationTime.tv_sec - ppt.tv_sec)
	+ (presentationTime.tv_usec - ppt.tv_usec)/1000000.0;
      if (duration < 0.0) duration = 0.0;
      unsigned frameDuration
	= (unsigned)((2*duration*fQTTimeScale+1)/2); // round

      unsigned numSamples
	= useFrame1(fPrevFrameState.frameSize, ppt,
		    frameDuration, fPrevFrameState.destFileOffset);
      fQTTotNumSamples += numSamples;
      sampleNumberOfFrameStart = fQTTotNumSamples + 1;
    }

    // Remember the current frame for next time:
    fPrevFrameState.frameSize = frameSize;
    fPrevFrameState.presentationTime = presentationTime;
    fPrevFrameState.destFileOffset = destFileOffset;
  }

  // Write the data into the file:
  fwrite(frameSource, frameSize, 1, fOurSink.fOutFid);

  // If we have a hint track, then write to it also:
  if (hasHintTrack()) {
    // Because presentation times are used for RTP packet timestamps,
    // we don't starting writing to the hint track until we've been synced:
    if (!fHaveBeenSynced) {
      fHaveBeenSynced
	= fOurSubsession.rtpSource()->hasBeenSynchronizedUsingRTCP();
    }
    if (fHaveBeenSynced) {
      fHintTrackForUs->useFrameForHinting(frameSize, presentationTime,
					  sampleNumberOfFrameStart);
    }
  }
}

void SubsessionIOState::useFrameForHinting(unsigned frameSize,
					   struct timeval presentationTime,
					   unsigned startSampleNumber) {
  // Normally, assume that each frame has just a single RTP packet.
  // This simplifies the hinting code, even though it might not be
  // true in practice.  (Instead of multiple RTP packets, we'll get
  // a single, large RTP packet that may get fragmented at the IP level.)
  // However, if the source is H.263+, then we need to break the frame
  // back into separate RTP packets, because we want to reuse the special
  // header bytes that were at the start of each of the RTP packets.
  Boolean hack263 = strcmp(fOurSubsession.codecName(), "H263-1998") == 0;

  // If there has been a previous frame, then output a 'hint sample' for it.
  // (We use the current frame's presentation time to compute the previous
  // hint sample's duration.)
  RTPSource* const rs = fOurSubsession.rtpSource(); // abbrev 
  struct timeval const& ppt = fPrevFrameState.presentationTime; //abbrev
  if (ppt.tv_sec != 0 || ppt.tv_usec != 0) {
    double duration = (presentationTime.tv_sec - ppt.tv_sec)
      + (presentationTime.tv_usec - ppt.tv_usec)/1000000.0;
    if (duration < 0.0) duration = 0.0;
    unsigned msDuration = (unsigned)(duration*1000); // milliseconds
    if (msDuration > fHINF.dmax) fHINF.dmax = msDuration;
    unsigned hintSampleDuration
      = (unsigned)((2*duration*fQTTimeScale+1)/2); // round

    unsigned const hintSampleDestFileOffset = ftell(fOurSink.fOutFid);

    unsigned short numPTEntries = 1; // normal case
    unsigned char* immediateDataPtr = NULL;
    unsigned immediateDataBytesRemaining = 0;
    if (hack263) { // special case
      numPTEntries = fPrevFrameState.numSpecialHeaders;
      immediateDataPtr = fPrevFrameState.specialHeaderBytes;
      immediateDataBytesRemaining
	= fPrevFrameState.specialHeaderBytesLength;
    }
    unsigned hintSampleSize
      = fOurSink.addHalfWord(numPTEntries);// Entry count
    hintSampleSize += fOurSink.addHalfWord(0x0000); // Reserved

    unsigned offsetWithinSample = 0;
    for (unsigned i = 0; i < numPTEntries; ++i) {
      // Output a Packet Table entry (representing a single RTP packet):
      unsigned short numDTEntries = 1;
      unsigned rtpHeader = fPrevFrameState.rtpHeader;
      unsigned dataFrameSize = fPrevFrameState.frameSize;
      unsigned sampleNumber = fPrevFrameState.startSampleNumber;

      unsigned char immediateDataLen = 0;
      if (hack263) {
	++numDTEntries; // to include a Data Table entry for the special hdr
	if (immediateDataBytesRemaining > 0) {
	  immediateDataLen = *immediateDataPtr++;
	  --immediateDataBytesRemaining;
	  if (immediateDataLen > immediateDataBytesRemaining) {
	    // shouldn't happen (length byte was bad)
	    immediateDataLen = immediateDataBytesRemaining;
	  }
	}
	dataFrameSize
	  = fPrevFrameState.packetSizes[i] - immediateDataLen;

	Boolean PbitSet
	  = immediateDataLen >= 1 && (immediateDataPtr[0]&0x4) != 0;
	if (PbitSet) {
	  offsetWithinSample += 2; // to omit the two leading 0 bytes
	}

	if (i+1 < numPTEntries) {
	  // This is not the last RTP packet, so clear the marker bit:
	  rtpHeader &=~ (1<<23);
	}
      }

      // Output the Packet Table:
      hintSampleSize += fOurSink.addWord(0); // Relative transmission time
      unsigned seqNumBehind = numPTEntries - 1 - i;
      hintSampleSize += fOurSink.addWord(rtpHeader - seqNumBehind);
          // RTP header info + RTP sequence number (modified by packet #)
      hintSampleSize += fOurSink.addHalfWord(0x0000); // Flags
      hintSampleSize += fOurSink.addHalfWord(numDTEntries); // Entry count
      unsigned totalPacketSize = 0;

      // Output the Data Table:
      if (hack263) {
	//   use the "Immediate Data" format (1):
	hintSampleSize += fOurSink.addByte(1); // Source
	unsigned char len = immediateDataLen > 14 ? 14 : immediateDataLen;
	hintSampleSize += fOurSink.addByte(len); // Length
	totalPacketSize += len; fHINF.dimm += len;
	unsigned char j;
	for (j = 0; j < len; ++j) {
	  hintSampleSize += fOurSink.addByte(immediateDataPtr[j]); // Data
	}
	for (j = len; j < 14; ++j) {
	  hintSampleSize += fOurSink.addByte(0); // Data (padding)
	}

	immediateDataPtr += immediateDataLen;
	immediateDataBytesRemaining -= immediateDataLen;
      }
      //   use the "Sample Data" format (2):
      hintSampleSize += fOurSink.addByte(2); // Source
      hintSampleSize += fOurSink.addByte(0); // Track ref index
      hintSampleSize += fOurSink.addHalfWord(dataFrameSize); // Length
      totalPacketSize += dataFrameSize; fHINF.dmed += dataFrameSize;
      hintSampleSize += fOurSink.addWord(sampleNumber); // Sample number
      hintSampleSize += fOurSink.addWord(offsetWithinSample); // Offset
      // Get "bytes|samples per compression block" from the hinted track:
      unsigned short const bytesPerCompressionBlock
	= fTrackHintedByUs->fQTBytesPerFrame;
      unsigned short const samplesPerCompressionBlock
	= fTrackHintedByUs->fQTSamplesPerFrame;
      hintSampleSize += fOurSink.addHalfWord(bytesPerCompressionBlock);
      hintSampleSize += fOurSink.addHalfWord(samplesPerCompressionBlock);

      offsetWithinSample += dataFrameSize;// for the next iteration (if any)

      // Tally statistics for this packet:
      fHINF.nump += 1;
      fHINF.tpyl += totalPacketSize;
      totalPacketSize += 12; // add in the size of the RTP header
      fHINF.trpy += totalPacketSize;
      if (totalPacketSize > fHINF.pmax) fHINF.pmax = totalPacketSize;
    }

    // Make note of this completed hint sample frame:
    fQTTotNumSamples += useFrame1(hintSampleSize, ppt, hintSampleDuration,
				  hintSampleDestFileOffset);
  }
  
  // Remember this frame for next time:
  fPrevFrameState.frameSize = frameSize;
  fPrevFrameState.presentationTime = presentationTime;
  fPrevFrameState.startSampleNumber = startSampleNumber;
  fPrevFrameState.rtpHeader
    = rs->curPacketMarkerBit()<<23
    | (rs->rtpPayloadFormat()&0x7F)<<16
    | rs->curPacketRTPSeqNum();
  if (hack263) {
      H263plusVideoRTPSource* rs_263 = (H263plusVideoRTPSource*)rs;
      fPrevFrameState.numSpecialHeaders = rs_263->fNumSpecialHeaders;
      fPrevFrameState.specialHeaderBytesLength
	= rs_263->fSpecialHeaderBytesLength;
      unsigned i;
      for (i = 0; i < rs_263->fSpecialHeaderBytesLength; ++i) {
	fPrevFrameState.specialHeaderBytes[i]
	  = rs_263->fSpecialHeaderBytes[i];
      }
      for (i = 0; i < rs_263->fNumSpecialHeaders; ++i) {
	fPrevFrameState.packetSizes[i] = rs_263->fPacketSizes[i];
      }
  }
}

unsigned SubsessionIOState::useFrame1(unsigned sourceDataSize,
				      struct timeval presentationTime,
				      unsigned frameDuration,
				      unsigned destFileOffset) {
  // Figure out the actual frame size for this data:
  unsigned frameSize = fQTBytesPerFrame;
  if (frameSize == 0) {
    // The entire packet data is assumed to be a frame:
    frameSize = sourceDataSize;
  }
  unsigned const numFrames = sourceDataSize/frameSize;
  unsigned const numSamples = numFrames*fQTSamplesPerFrame;

  // Record the information about which 'chunk' this data belongs to:
  ChunkDescriptor* newTailChunk;
  if (fTailChunk == NULL) {
    newTailChunk = fHeadChunk
      = new ChunkDescriptor(destFileOffset, sourceDataSize,
			    frameSize, frameDuration, presentationTime);
  } else {
    newTailChunk = fTailChunk->extendChunk(destFileOffset, sourceDataSize,
					   frameSize, frameDuration,
					   presentationTime);
  }
  if (newTailChunk != fTailChunk) {
   // This data created a new chunk, rather than extending the old one
    ++fNumChunks;
    fTailChunk = newTailChunk;
  }

  return numSamples;
}
  
void SubsessionIOState::onSourceClosure() {
  fOurSourceIsActive = False;
  fOurSink.onSourceClosure1();
}

Boolean SubsessionIOState::syncOK(struct timeval presentationTime) {
  QuickTimeFileSink& s = fOurSink; // abbreviation
  if (!s.fSyncStreams) return True; // we don't care

  if (s.fNumSyncedSubsessions < s.fNumSubsessions) {
    // Not all subsessions have yet been synced.  Check whether ours was
    // one of the unsynced ones, and, if so, whether it is now synced:
    if (!fHaveBeenSynced) {
      // We weren't synchronized before
      if (fOurSubsession.rtpSource()->hasBeenSynchronizedUsingRTCP()) {
	// But now we are
	fHaveBeenSynced = True;
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

void SubsessionIOState::setHintTrack(SubsessionIOState* hintedTrack,
				     SubsessionIOState* hintTrack) {
  if (hintedTrack != NULL) hintedTrack->fHintTrackForUs = hintTrack;
  if (hintTrack != NULL) hintTrack->fTrackHintedByUs = hintedTrack;
}

void Count64::operator+=(unsigned arg) {
  unsigned newLo = lo + arg;
  if (newLo < lo) { // lo has overflowed
    ++hi;
  }
  lo = newLo;
}

ChunkDescriptor
::ChunkDescriptor(unsigned offsetInFile, unsigned size,
		  unsigned frameSize, unsigned frameDuration,
		  struct timeval presentationTime)
  : fNextChunk(NULL), fOffsetInFile(offsetInFile),
    fNumFrames(size/frameSize),
    fFrameSize(frameSize), fFrameDuration(frameDuration),
    fPresentationTime(presentationTime) {
}

ChunkDescriptor::~ChunkDescriptor() {
  delete fNextChunk;
}

ChunkDescriptor* ChunkDescriptor
::extendChunk(unsigned newOffsetInFile, unsigned newSize,
	      unsigned newFrameSize, unsigned newFrameDuration,
	      struct timeval newPresentationTime) {
  // First, check whether the new space is just at the end of this
  // existing chunk:
  if (newOffsetInFile == fOffsetInFile + fNumFrames*fFrameSize) {
    // We can extend this existing chunk, provided that the frame size
    // and frame duration have not changed:
    if (newFrameSize == fFrameSize && newFrameDuration == fFrameDuration) {
      fNumFrames += newSize/fFrameSize;
      return this;
    }
  }

  // We'll allocate a new ChunkDescriptor, and link it to the end of us:
  ChunkDescriptor* newDescriptor
    = new ChunkDescriptor(newOffsetInFile, newSize,
			  newFrameSize, newFrameDuration,
			  newPresentationTime);

  fNextChunk = newDescriptor;

  return newDescriptor;
}


////////// QuickTime-specific implementation //////////

unsigned QuickTimeFileSink::addWord(unsigned word) {
  addByte(word>>24); addByte(word>>16);
  addByte(word>>8); addByte(word);

  return 4;
}

unsigned QuickTimeFileSink::addHalfWord(unsigned short halfWord) {
  addByte((unsigned char)(halfWord>>8)); addByte((unsigned char)halfWord);

  return 2;
}

unsigned QuickTimeFileSink::addZeroWords(unsigned numWords) {
  for (unsigned i = 0; i < numWords; ++i) {
    addWord(0);
  }

  return numWords*4;
}

unsigned QuickTimeFileSink::add4ByteString(char const* str) {
  addByte(str[0]); addByte(str[1]); addByte(str[2]); addByte(str[3]);

  return 4;
}

unsigned QuickTimeFileSink::addArbitraryString(char const* str,
					       Boolean oneByteLength) {
  unsigned size = 0;
  if (oneByteLength) {
    // Begin with a byte containing the string length:
    unsigned strLength = strlen(str);
    if (strLength >= 256) {
      envir() << "QuickTimeFileSink::addArbitraryString(\""
	      << str << "\") saw string longer than we know how to handle ("
	      << strLength << "\n";
    }
    size += addByte((unsigned char)strLength);
  }

  while (*str != '\0') {
    size += addByte(*str++);
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
  envir() << "QuickTimeFileSink::setWord(): fseek failed (err "
	  << envir().getErrno() << ")\n";
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

    if (fCurrentIOState->hasHintTrack()) {
      // This track has a hint track; output it also:
      fCurrentIOState = fCurrentIOState->fHintTrackForUs;
      size += addAtom_trak();
    }
  }
  iter.reset();
  while ((fCurrentSubsession = iter.next()) != NULL) {
    fCurrentIOState = (SubsessionIOState*)(fCurrentSubsession->miscPtr); 
    if (fCurrentIOState == NULL) continue;
    if (strcmp(fCurrentSubsession->mediumName(), "audio") == 0) continue;

    size += addAtom_trak();

    if (fCurrentIOState->hasHintTrack()) {
      // This track has a hint track; output it also:
      fCurrentIOState = fCurrentIOState->fHintTrackForUs;
      size += addAtom_trak();
    }
  }
addAtomEnd;

addAtom(mvhd);
  size += addWord(0x00000000); // Version + Flags
  size += addWord(fAppleCreationTime); // Creation time
  size += addWord(fAppleCreationTime); // Modification time

  // For the "Time scale" field, use the largest RTP timestamp frequency
  // that we saw in any of the subsessions.
  size += addWord(movieTimeScale()); // Time scale

  unsigned const duration = fMaxTrackDurationM;
  fMVHD_durationPosn = ftell(fOutFid);
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
  size += addWord(SubsessionIOState::fCurrentTrackNumber+1);// Next track ID
addAtomEnd;

addAtom(trak);
  size += addAtom_tkhd();

  // If we're synchronizing the media streams (or are a hint track),
  // add an edit list that helps do this:
  if (fCurrentIOState->fHeadChunk != NULL
      && (fSyncStreams || fCurrentIOState->isHintTrack())) {
    size += addAtom_edts();
  }

  // If we're generating a hint track, add a 'tref' atom:
  if (fCurrentIOState->isHintTrack()) size += addAtom_tref();

  size += addAtom_mdia();

  // If we're generating a hint track, add a 'udta' atom:
  if (fCurrentIOState->isHintTrack()) size += addAtom_udta();
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
  size += addWord(fCurrentIOState->fTrackID); // Track ID
  size += addWord(0x00000000); // Reserved

  unsigned const duration = fCurrentIOState->fQTDurationM; // movie units
  fCurrentIOState->fTKHD_durationPosn = ftell(fOutFid);
  size += addWord(duration); // Duration
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

addAtom(edts);
  size += addAtom_elst();
addAtomEnd;

#define addEdit1(duration,trackPosition) do { \
      unsigned trackDuration \
        = (unsigned) ((2*(duration)*movieTimeScale()+1)/2); \
            /* in movie time units */ \
      size += addWord(trackDuration); /* Track duration */ \
      totalDurationOfEdits += trackDuration; \
      size += addWord(trackPosition); /* Media time */ \
      size += addWord(0x00010000); /* Media rate (1x) */ \
      ++numEdits; \
} while (0)
#define addEdit(duration) addEdit1((duration),editTrackPosition)
#define addEmptyEdit(duration) addEdit1((duration),(~0))

addAtom(elst);
  size += addWord(0x00000000); // Version + Flags

  // Add a dummy "Number of entries" field
  // (and remember its position).  We'll fill this field in later:
  unsigned numEntriesPosition = ftell(fOutFid);
  size += addWord(0); // dummy for "Number of entries"
  unsigned numEdits = 0;
  unsigned totalDurationOfEdits = 0; // in movie time units

  // Run through our chunks, looking at their presentation times.
  // From these, figure out the edits that need to be made to keep
  // the track media data in sync with the presentation times.
  
  double const syncThreshold = 0.1; // 100 ms
    // don't allow the track to get out of sync by more than this

  struct timeval editStartTime = fFirstDataTime;
  unsigned editTrackPosition = 0;
  unsigned currentTrackPosition = 0;
  double trackDurationOfEdit = 0.0;
  unsigned chunkDuration = 0;

  ChunkDescriptor* chunk = fCurrentIOState->fHeadChunk;
  while (chunk != NULL) {
    struct timeval const& chunkStartTime = chunk->fPresentationTime;
    double movieDurationOfEdit
      = (chunkStartTime.tv_sec - editStartTime.tv_sec)
      + (chunkStartTime.tv_usec - editStartTime.tv_usec)/1000000.0;
    trackDurationOfEdit = (currentTrackPosition-editTrackPosition)
      / (double)(fCurrentIOState->fQTTimeScale);
      
    double outOfSync = movieDurationOfEdit - trackDurationOfEdit;

    if (outOfSync > syncThreshold) {
      // The track's data is too short, so end this edit, add a new
      // 'empty' edit after it, and start a new edit
      // (at the current track posn.):
      if (trackDurationOfEdit > 0.0) addEdit(trackDurationOfEdit);
      addEmptyEdit(outOfSync);

      editStartTime = chunkStartTime;
      editTrackPosition = currentTrackPosition;
    } else if (outOfSync < -syncThreshold) {
      // The track's data is too long, so end this edit, and start
      // a new edit (pointing at the current track posn.):
      if (movieDurationOfEdit > 0.0) addEdit(movieDurationOfEdit);

      editStartTime = chunkStartTime;
      editTrackPosition = currentTrackPosition;
    }

    // Note the duration of this chunk:
    unsigned numChannels = fCurrentIOState->fOurSubsession.numChannels();
    chunkDuration = chunk->fNumFrames*chunk->fFrameDuration/numChannels;
    currentTrackPosition += chunkDuration;

    chunk = chunk->fNextChunk;
  }

  // Write out the final edit
  trackDurationOfEdit
      += (double)chunkDuration/fCurrentIOState->fQTTimeScale;
  if (trackDurationOfEdit > 0.0) addEdit(trackDurationOfEdit);

  // Now go back and fill in the "Number of entries" field:
  setWord(numEntriesPosition, numEdits);

  // Also, if the sum of all of the edit durations exceeds the
  // track duration that we already computed (from sample durations),
  // then reset the track duration to this new value:
  if (totalDurationOfEdits > fCurrentIOState->fQTDurationM) {
    fCurrentIOState->fQTDurationM = totalDurationOfEdits;
    setWord(fCurrentIOState->fTKHD_durationPosn, totalDurationOfEdits);

    // Also, check whether the overall movie duration needs to change:
    if (totalDurationOfEdits > fMaxTrackDurationM) {
      fMaxTrackDurationM = totalDurationOfEdits;
      setWord(fMVHD_durationPosn, totalDurationOfEdits);
    }

    // Also, convert to track time scale:
    double scaleFactor
      = fCurrentIOState->fQTTimeScale/(double)movieTimeScale();
    fCurrentIOState->fQTDurationT
      = (unsigned)(totalDurationOfEdits*scaleFactor);
  }
addAtomEnd;

addAtom(tref);
  size += addAtom_hint();
addAtomEnd;

addAtom(hint);
  SubsessionIOState* hintedTrack = fCurrentIOState->fTrackHintedByUs;
    // Assert: hintedTrack != NULL
  size += addWord(hintedTrack->fTrackID);
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

  unsigned const duration = fCurrentIOState->fQTDurationT; // track units
  size += addWord(duration); // Duration

  size += addWord(0x00000000); // Language+Quality
addAtomEnd;

addAtom(hdlr);
  size += addWord(0x00000000); // Version + Flags
  size += add4ByteString("mhlr"); // Component type
  size += addWord(fCurrentIOState->fQTcomponentSubtype);
    // Component subtype
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

addAtom(gmhd);
  size += addAtom_gmin();
addAtomEnd;

addAtom(gmin);
  size += addWord(0x00000000); // Version + Flags
  // The following fields probably aren't used for hint tracks, so just
  // use values that I've seen in other files:
  size += addWord(0x00408000); // Graphics mode + Opcolor (1st 2 bytes)
  size += addWord(0x80008000); // Opcolor (last 4 bytes)
  size += addWord(0x00000000); // Balance + Reserved
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
  for (unsigned i = 0; i < size; ++i) addByte(from[i]);
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
  unsigned short numChannels
    = (unsigned short)(fCurrentIOState->fOurSubsession.numChannels());
  size += addHalfWord(numChannels); // Number of channels
  size += addHalfWord(0x0010); // Sample size
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

unsigned QuickTimeFileSink::addAtom_rtp() {
  unsigned initFilePosn = ftell(fOutFid);
  unsigned size = addAtomHeader("rtp ");

  size += addWord(0x00000000); // Reserved (1st 4 bytes)
  size += addWord(0x00000001); // Reserved (last 2 bytes) + Data ref index
  size += addWord(0x00010001); // Hint track version + Last compat htv
  size += addWord(1450); // Max packet size

  size += addAtom_tims();
addAtomEnd;

addAtom(tims);
  size += addWord(fCurrentIOState->fOurSubsession.rtpTimestampFrequency());
addAtomEnd;

addAtom(stts); // Time-to-Sample
  size += addWord(0x00000000); // Version+flags

  // First, add a dummy "Number of entries" field
  // (and remember its position).  We'll fill this field in later:
  unsigned numEntriesPosition = ftell(fOutFid);
  size += addWord(0); // dummy for "Number of entries"

  // Then, run through the chunk descriptors, and enter the entries
  // in this (compressed) Time-to-Sample table:
  unsigned numEntries = 0, numSamplesSoFar = 0;
  unsigned prevSampleDuration = 0;
  unsigned const samplesPerFrame = fCurrentIOState->fQTSamplesPerFrame;
  ChunkDescriptor* chunk = fCurrentIOState->fHeadChunk;
  while (chunk != NULL) {
    unsigned const sampleDuration = chunk->fFrameDuration/samplesPerFrame;
    if (sampleDuration != prevSampleDuration) {
      // This chunk will start a new table entry,
      // so write out the old one (if any):
      if (chunk != fCurrentIOState->fHeadChunk) {
	++numEntries;
	size += addWord(numSamplesSoFar); // Sample count
	size += addWord(prevSampleDuration); // Sample duration
	numSamplesSoFar = 0;
      }
    }

    unsigned const numSamples = chunk->fNumFrames*samplesPerFrame;
    numSamplesSoFar += numSamples;
    prevSampleDuration = sampleDuration;
    chunk = chunk->fNextChunk;
  }

  // Then, write out the last entry:
  ++numEntries;
  size += addWord(numSamplesSoFar); // Sample count
  size += addWord(prevSampleDuration); // Sample duration

  // Now go back and fill in the "Number of entries" field:
  setWord(numEntriesPosition, numEntries);
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
  unsigned const samplesPerFrame = fCurrentIOState->fQTSamplesPerFrame;
  ChunkDescriptor* chunk = fCurrentIOState->fHeadChunk;
  while (chunk != NULL) {
    ++chunkNumber;
    unsigned const samplesPerChunk = chunk->fNumFrames*samplesPerFrame;
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
  ChunkDescriptor* chunk = fCurrentIOState->fHeadChunk;
  while (chunk != NULL) {
    double bps
      = (double)(chunk->fFrameSize)/(fCurrentIOState->fQTSamplesPerFrame);
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
    if (fCurrentIOState->isHintTrack()
	&& fCurrentIOState->fHeadChunk != NULL) {
      sampleSize = fCurrentIOState->fHeadChunk->fFrameSize
	              / fCurrentIOState->fQTSamplesPerFrame;
    } else {
      // The following doesn't seem right, but seems to do the right thing:
      sampleSize = fCurrentIOState->fQTTimeUnitsPerSample; //???
    }
  } else {
    sampleSize = 0; // indicates a multiple-entry table
  }
  size += addWord(sampleSize); // Sample size
  unsigned const totNumSamples = fCurrentIOState->fQTTotNumSamples;
  size += addWord(totNumSamples); // Number of entries

  if (!haveSingleEntryTable) {
    // Multiple-entry table:
    // Run through the chunk descriptors, entering the sample sizes:
    ChunkDescriptor* chunk = fCurrentIOState->fHeadChunk;
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
  ChunkDescriptor* chunk = fCurrentIOState->fHeadChunk;
  while (chunk != NULL) {
    size += addWord(chunk->fOffsetInFile);

    chunk = chunk->fNextChunk;
  }
addAtomEnd;

addAtom(udta);
  size += addAtom_name();
  size += addAtom_hnti();
  size += addAtom_hinf();
addAtomEnd;

addAtom(name);
  char description[100];
  sprintf(description, "Hinted %s track",
	  fCurrentIOState->fOurSubsession.mediumName());
  size += addArbitraryString(description, False); // name of object
addAtomEnd;

addAtom(hnti);
  size += addAtom_sdp(); 
addAtomEnd;

unsigned QuickTimeFileSink::addAtom_sdp() {
  unsigned initFilePosn = ftell(fOutFid);
  unsigned size = addAtomHeader("sdp ");

  // Add this subsession's SDP lines: 
  char const* sdpLines = fCurrentIOState->fOurSubsession.savedSDPLines();
  // We need to change any "a=control:trackID=" values to be this
  // track's actual track id:
  char* newSDPLines = new char[strlen(sdpLines)+100/*overkill*/];
  char const* searchStr = "a=control:trackid=";
  Boolean foundSearchString = False;
  char const *p1, *p2, *p3;
  for (p1 = sdpLines; *p1 != '\0'; ++p1) {
    for (p2 = p1,p3 = searchStr; tolower(*p2) == *p3; ++p2,++p3) {}
    if (*p3 == '\0') {
      // We found the end of the search string, at p2.
      int beforeTrackNumPosn = p2-sdpLines;
      // Look for the subsequent track number, and skip over it: 
      int trackNumLength;
      if (sscanf(p2, " %*d%n", &trackNumLength) < 0) break;
      int afterTrackNumPosn = beforeTrackNumPosn + trackNumLength;
      
      // Replace the old track number with the correct one: 
      int i;
      for (i = 0; i < beforeTrackNumPosn; ++i) newSDPLines[i] = sdpLines[i];
      sprintf(&newSDPLines[i], "%d", fCurrentIOState->fTrackID);
      i = afterTrackNumPosn;
      int j = i + strlen(&newSDPLines[i]);
      while (1) {
	if ((newSDPLines[j] = sdpLines[i]) == '\0') break;
	++i; ++j;
      }

      foundSearchString = True;
      break;
    }
  }

  if (!foundSearchString) {
    // Because we didn't find a "a=control:trackID=<trackId>" line,
    // add one of our own:
    sprintf(newSDPLines, "%s%s%d\r\n",
	    sdpLines, searchStr, fCurrentIOState->fTrackID);
  }

  size += addArbitraryString(newSDPLines, False);
  delete[] newSDPLines;
addAtomEnd;

addAtom(hinf);
  size += addAtom_totl();
  size += addAtom_npck();
  size += addAtom_tpay();
  size += addAtom_trpy();
  size += addAtom_nump();
  size += addAtom_tpyl();
  // Is 'maxr' required? #####
  size += addAtom_dmed();
  size += addAtom_dimm();
  size += addAtom_drep();
  size += addAtom_tmin();
  size += addAtom_tmax();
  size += addAtom_pmax();
  size += addAtom_dmax();
  size += addAtom_payt(); 
addAtomEnd;

addAtom(totl);
 size += addWord(fCurrentIOState->fHINF.trpy.lo);
addAtomEnd;

addAtom(npck);
 size += addWord(fCurrentIOState->fHINF.nump.lo);
addAtomEnd;

addAtom(tpay);
 size += addWord(fCurrentIOState->fHINF.tpyl.lo);
addAtomEnd;

addAtom(trpy);
 size += addWord(fCurrentIOState->fHINF.trpy.hi);
 size += addWord(fCurrentIOState->fHINF.trpy.lo);
addAtomEnd;

addAtom(nump);
 size += addWord(fCurrentIOState->fHINF.nump.hi);
 size += addWord(fCurrentIOState->fHINF.nump.lo);
addAtomEnd;

addAtom(tpyl);
 size += addWord(fCurrentIOState->fHINF.tpyl.hi);
 size += addWord(fCurrentIOState->fHINF.tpyl.lo);
addAtomEnd;

addAtom(dmed);
 size += addWord(fCurrentIOState->fHINF.dmed.hi);
 size += addWord(fCurrentIOState->fHINF.dmed.lo);
addAtomEnd;

addAtom(dimm);
 size += addWord(fCurrentIOState->fHINF.dimm.hi);
 size += addWord(fCurrentIOState->fHINF.dimm.lo);
addAtomEnd;

addAtom(drep);
 size += addWord(0);
 size += addWord(0);
addAtomEnd;

addAtom(tmin);
 size += addWord(0);
addAtomEnd;

addAtom(tmax);
 size += addWord(0);
addAtomEnd;

addAtom(pmax);
 size += addWord(fCurrentIOState->fHINF.pmax);
addAtomEnd;

addAtom(dmax);
 size += addWord(fCurrentIOState->fHINF.dmax);
addAtomEnd;

addAtom(payt);
  MediaSubsession& ourSubsession = fCurrentIOState->fOurSubsession; 
  RTPSource* rtpSource = ourSubsession.rtpSource();
  size += addByte(rtpSource->rtpPayloadFormat());
  
  // Also, add a 'rtpmap' string: <mime-subtype>/<rtp-frequency>
  unsigned rtpmapStringLength = strlen(ourSubsession.codecName()) + 20;
  char* rtpmapString = new char[rtpmapStringLength];
  sprintf(rtpmapString, "%s/%d",
	  ourSubsession.codecName(), rtpSource->timestampFrequency());
  size += addArbitraryString(rtpmapString);
  delete[] rtpmapString;
addAtomEnd;

// A dummy atom (with name "????"):
unsigned QuickTimeFileSink::addAtom_dummy() {
    unsigned initFilePosn = ftell(fOutFid);
    unsigned size = addAtomHeader("????");
addAtomEnd;
