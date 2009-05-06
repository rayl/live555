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
// A filter that breaks up an MPEG 1 or 2 video elementary stream into
//   frames for: Video_Sequence_Header, GOP_Header, Picture_Header
// Implementation

#include "MPEGVideoStreamFramer.hh"
#include "StreamParser.hh"
#include <GroupsockHelper.hh>
#include <string.h>

////////// MPEGVideoStreamParser definition //////////

// An enum representing the current state of the parser:
enum MPEGParseState {
  PARSING_VIDEO_SEQUENCE_HEADER,
  PARSING_VIDEO_SEQUENCE_HEADER_SEEN_CODE,
  PARSING_GOP_HEADER,
  PARSING_PICTURE_HEADER,
  PARSING_SLICE
}; 

#define VSH_MAX_SIZE 1000

class MPEGVideoStreamParser: public StreamParser {
public:
  MPEGVideoStreamParser(MPEGVideoStreamFramer* usingSource,
			FramedSource* inputSource,
			Boolean iFramesOnly, double vshPeriod);
  virtual ~MPEGVideoStreamParser();

public:
  unsigned parse();
      // returns the size of the frame that was acquired, or 0 if none was

  void registerReadInterest(unsigned char* to, unsigned maxSize);

private: // redefined virtual functions
  virtual void restoreSavedParserState();

private:
  void setParseState(MPEGParseState parseState);

  unsigned parseVideoSequenceHeader(Boolean haveSeenStartCode = False);
  unsigned parseGOPHeader();
  unsigned parsePictureHeader();
  unsigned parseSlice();

  // Record "byte" in the current output frame:
  void saveByte(unsigned char byte) {
    if (fTo >= fLimit) return; // there's no space left

    *fTo++ = byte;
  }

  void save4Bytes(unsigned word) {
    if (fTo+4 > fLimit) return; // there's no space left

    *fTo++ = word>>24; *fTo++ = word>>16; *fTo++ = word>>8; *fTo++ = word;
  }

  // Save data until we see a sync word (0x000001xx):
  void saveToNextCode(unsigned& curWord) {
    save4Bytes(curWord);
    curWord = get4Bytes();
    while ((curWord&0xFFFFFF00) != 0x00000100) {
      if ((unsigned)(curWord&0xFF) > 1) {
	// a sync word definitely doesn't begin anywhere in "curWord"
	save4Bytes(curWord);
	curWord = get4Bytes();
      } else {
	// a sync word might begin in "curWord", although not at its start
	saveByte(curWord>>24);
	unsigned char newByte = get1Byte();
	curWord = (curWord<<8)|newByte;
      }
    }
  }

  // Skip data until we see a sync word (0x000001xx):
  void skipToNextCode(unsigned& curWord) {
    curWord = get4Bytes();
    while ((curWord&0xFFFFFF00) != 0x00000100) {
      if ((unsigned)(curWord&0xFF) > 1) {
	// a sync word definitely doesn't begin anywhere in "curWord"
	curWord = get4Bytes();
      } else {
	// a sync word might begin in "curWord", although not at its start
	unsigned char newByte = get1Byte();
	curWord = (curWord<<8)|newByte;
      }
    }
  }

private:
  MPEGVideoStreamFramer* fUsingSource;
  MPEGParseState fCurrentParseState;
  unsigned fPicturesSinceLastGOP;
      // used to compute timestamp for a video_sequence_header
  unsigned short fCurPicTemporalReference;
      // used to compute slice timestamp
  unsigned char fCurrentSliceNumber; // set when parsing a slice

  // state of the frame that's currently being read:
  unsigned char* fStartOfFrame;
  unsigned char* fTo;
  unsigned char* fLimit;
  unsigned curFrameSize() { return fTo - fStartOfFrame; }
  unsigned char* fSavedTo;
  
  // A saved copy of the most recently seen 'video_sequence_header',
  // in case we need to insert it into the stream periodically:
  unsigned char fSavedVSHBuffer[VSH_MAX_SIZE];
  unsigned fSavedVSHSize;
  double fSavedVSHTimestamp;
  double fVSHPeriod;
  Boolean fIFramesOnly, fSkippingCurrentPicture;

  void saveCurrentVSH();
  Boolean needToUseSavedVSH();
  unsigned useSavedVSH(); // returns the size of the saved VSH
};


////////// TimeCode implementation //////////

TimeCode::TimeCode()
  : days(0), hours(0), minutes(0), seconds(0), pictures(0) {
}

TimeCode::~TimeCode() {
}

int TimeCode::operator==(TimeCode const& arg2) {
  return pictures == arg2.pictures && seconds == arg2.seconds
    && minutes == arg2.minutes && hours == arg2.hours && days == arg2.days;
}


////////// MPEGVideoStreamFramer implementation //////////

#ifdef BSD
static struct timezone Idunno;
#else
static int Idunno;
#endif

MPEGVideoStreamFramer::MPEGVideoStreamFramer(UsageEnvironment& env,
					     FramedSource* inputSource,
					     Boolean iFramesOnly,
					     double vshPeriod)
  : FramedFilter(env, inputSource),
    fPictureEndMarker(False), fPictureCount(0),
    fFrameRate(0.0) /* until we learn otherwise (from a video seq hdr) */,
    fPicturesAdjustment(0), fPictureTimeBase(0.0), fTcSecsBase(0),
    fHaveSeenFirstTimeCode(False) {
  // Use the current wallclock time as the base 'presentation time':
  gettimeofday(&fPresentationTimeBase, &Idunno);

  fParser
    = new MPEGVideoStreamParser(this, inputSource, iFramesOnly, vshPeriod);
}

MPEGVideoStreamFramer::~MPEGVideoStreamFramer() {
  delete fParser;
}

MPEGVideoStreamFramer*
MPEGVideoStreamFramer::createNew(UsageEnvironment& env,
				 FramedSource* inputSource,
				 Boolean iFramesOnly,
				 double vshPeriod) {
  // Need to add source type checking here???  #####
  return new MPEGVideoStreamFramer(env, inputSource, iFramesOnly, vshPeriod);
}

void MPEGVideoStreamFramer::doGetNextFrame() {
  fParser->registerReadInterest(fTo, fMaxSize);
  continueReadProcessing();
}

float MPEGVideoStreamFramer::getPlayTime(unsigned /*numFrames*/) const {
  // OK, this is going to be a bit of a hack, because the actual play time
  // for us is going to be based not on "numFrames", but instead on how
  // many *pictures* have been completed since the last call to this
  // function.  (I.e., it's a hack because we're making an assumption about
  // *why* this member function is being called.)
  double result = fPictureCount/fFrameRate;
  ((MPEGVideoStreamFramer*)this)->fPictureCount = 0; // told you it's a hack
  return (float)result;
}

void MPEGVideoStreamFramer::continueReadProcessing(void* clientData,
						   unsigned char* /*ptr*/,
						   unsigned /*size*/) {
  MPEGVideoStreamFramer* framer = (MPEGVideoStreamFramer*)clientData;
  framer->continueReadProcessing();
}

void MPEGVideoStreamFramer::continueReadProcessing() {
  unsigned acquiredFrameSize = fParser->parse();
  if (acquiredFrameSize > 0) {
    // We were able to acquire a frame from the input.
    // It has already been copied to the reader's space.
    fFrameSize = acquiredFrameSize;
    
    // Call our own 'after getting' function.  Because we're not a 'leaf'
    // source, we can call this directly, without risking infinite recursion.
    afterGetting(this);
  } else {
    // We were unable to parse a complete frame from the input, because:
    // - we had to read more data from the source stream, or
    // - the source stream has ended.
  }
}

void MPEGVideoStreamFramer
::computeTimestamp(unsigned numAdditionalPictures) {
  // Computes "fPresentationTime" from the most recent GOP's
  // time_code, along with the "numAdditionalPictures" parameter:
  TimeCode& tc = fCurGOPTimeCode;

  double pictureTime
    = (tc.pictures + fPicturesAdjustment + numAdditionalPictures)/fFrameRate
    - fPictureTimeBase;
  unsigned pictureSeconds = (unsigned)pictureTime;
  double pictureFractionOfSecond = pictureTime - (float)pictureSeconds;

  unsigned tcSecs
    = (((tc.days*24)+tc.hours)*60+tc.minutes)*60+tc.seconds - fTcSecsBase;
  fPresentationTime = fPresentationTimeBase;
  fPresentationTime.tv_sec += tcSecs + pictureSeconds;
  fPresentationTime.tv_usec += (long)(pictureFractionOfSecond*1000000.0);
  if (fPresentationTime.tv_usec >= 1000000) {
    fPresentationTime.tv_usec -= 1000000;
    ++fPresentationTime.tv_sec;
  }
#ifdef DEBUG_COMPUTE_TIMESTAMPS
  fprintf(stderr, "MPEGVideoStreamFramer::computeTimestamp(%d) -> %d.%06d\n", numAdditionalPictures, fPresentationTime.tv_sec, fPresentationTime.tv_usec);
#endif
}

void MPEGVideoStreamFramer::setTimeCodeBaseParams() {
  TimeCode& tc = fCurGOPTimeCode;
  fPictureTimeBase = tc.pictures/fFrameRate;
  fTcSecsBase = (((tc.days*24)+tc.hours)*60+tc.minutes)*60+tc.seconds;
  fHaveSeenFirstTimeCode = True;
}

double MPEGVideoStreamFramer::getCurrentTimestamp() const {
  return fPresentationTime.tv_sec + fPresentationTime.tv_usec/1000000.0;
}

Boolean MPEGVideoStreamFramer::isMPEGVideoStreamFramer() const {
  return True;
}

////////// MPEGVideoStreamParser implementation //////////

MPEGVideoStreamParser
::MPEGVideoStreamParser(MPEGVideoStreamFramer* usingSource,
			FramedSource* inputSource,
			Boolean iFramesOnly, double vshPeriod)
  : StreamParser(inputSource, FramedSource::handleClosure, usingSource,
		 &MPEGVideoStreamFramer::continueReadProcessing, usingSource),
  fUsingSource(usingSource),
  fCurrentParseState(PARSING_VIDEO_SEQUENCE_HEADER),
  fPicturesSinceLastGOP(0), fCurPicTemporalReference(0),
  fCurrentSliceNumber(0), fSavedVSHSize(0), fVSHPeriod(vshPeriod),
  fIFramesOnly(iFramesOnly), fSkippingCurrentPicture(False) {
}

MPEGVideoStreamParser::~MPEGVideoStreamParser() {
}

void MPEGVideoStreamParser::restoreSavedParserState() {
  StreamParser::restoreSavedParserState();
  fTo = fSavedTo;
}

void MPEGVideoStreamParser::setParseState(MPEGParseState parseState) {
  fCurrentParseState = parseState;
  fSavedTo = fTo;
  saveParserState();
}

unsigned MPEGVideoStreamParser::parse() {
  try {
    switch (fCurrentParseState) {
    case PARSING_VIDEO_SEQUENCE_HEADER: {
      return parseVideoSequenceHeader();
    }
    case PARSING_VIDEO_SEQUENCE_HEADER_SEEN_CODE: {
      return parseVideoSequenceHeader(True);
    }
    case PARSING_GOP_HEADER: {
      return parseGOPHeader();
    }
    case PARSING_PICTURE_HEADER: {
      return parsePictureHeader();
    }
    case PARSING_SLICE: {
      return parseSlice();
    }
    default: {
      return 0; // shouldn't happen
    }
    }
  } catch (int /*e*/) {
#ifdef DEBUG
    fprintf(stderr, "MPEGVideoStreamParser::parse() EXCEPTION\n");
#endif
    return 0;  // the parsing got interrupted
  }
}

void MPEGVideoStreamParser::registerReadInterest(unsigned char* to,
						 unsigned maxSize) {
  fStartOfFrame = fTo = fSavedTo = to;
  fLimit = to + maxSize;
}

void MPEGVideoStreamParser::saveCurrentVSH() {
  unsigned frameSize = curFrameSize();
  if (frameSize > sizeof fSavedVSHBuffer) return; // too big to save

  memmove(fSavedVSHBuffer, fStartOfFrame, frameSize);
  fSavedVSHSize = frameSize;
  fSavedVSHTimestamp = fUsingSource->getCurrentTimestamp();
}

Boolean MPEGVideoStreamParser::needToUseSavedVSH() {
  return fUsingSource->getCurrentTimestamp() > fSavedVSHTimestamp+fVSHPeriod
    && fSavedVSHSize > 0;
}

unsigned MPEGVideoStreamParser::useSavedVSH() {
  unsigned bytesToUse = fSavedVSHSize;
  unsigned maxBytesToUse = fLimit - fStartOfFrame;
  if (bytesToUse > maxBytesToUse) bytesToUse = maxBytesToUse;

  memmove(fStartOfFrame, fSavedVSHBuffer, bytesToUse);

  // Also reset the saved timestamp:
  fSavedVSHTimestamp = fUsingSource->getCurrentTimestamp();

#ifdef DEBUG
  fprintf(stderr, "used saved video_sequence_header (%d bytes)\n", bytesToUse);
#endif
  return bytesToUse;
}

#define VIDEO_SEQUENCE_HEADER_START_CODE 0x000001B3
#define GROUP_START_CODE                 0x000001B8
#define PICTURE_START_CODE               0x00000100
#define SEQUENCE_END_CODE                0x000001B7

static double const frameRateFromCode[] = {
  0.0,          // forbidden
  24000/1001.0, // approx 23.976
  24.0,
  25.0,
  30000/1001.0, // approx 29.97
  30.0,
  50.0,
  60000/1001.0, // approx 59.94
  60.0,
  0.0,          // reserved
  0.0,          // reserved
  0.0,          // reserved
  0.0,          // reserved
  0.0,          // reserved
  0.0,          // reserved
  0.0           // reserved
};

unsigned MPEGVideoStreamParser
::parseVideoSequenceHeader(Boolean haveSeenStartCode) {
#ifdef DEBUG
  fprintf(stderr, "parsing video sequence header\n");
#endif
  unsigned first4Bytes;
  if (!haveSeenStartCode) {
    while ((first4Bytes = get4Bytes()) != VIDEO_SEQUENCE_HEADER_START_CODE) {
#ifdef DEBUG
      fprintf(stderr, "ignoring non video sequence header: 0x%08x\n", first4Bytes);
#endif
      setParseState(PARSING_VIDEO_SEQUENCE_HEADER);
          // ensures we progress over bad data
    }
  } else {
    // We've already seen the start code
    first4Bytes = VIDEO_SEQUENCE_HEADER_START_CODE;
  }
  save4Bytes(first4Bytes);

  // Next, extract the size and rate parameters from the next 8 bytes
  unsigned paramWord1 = get4Bytes();
  save4Bytes(paramWord1);
  unsigned next4Bytes = get4Bytes();
#ifdef DEBUG
  unsigned short horizontal_size_value   = (paramWord1&0xFFF00000)>>(32-12);
  unsigned short vertical_size_value     = (paramWord1&0x000FFF00)>>8;
  unsigned char aspect_ratio_information = (paramWord1&0x000000F0)>>4;
#endif
  unsigned char frame_rate_code          = (paramWord1&0x0000000F);
  fUsingSource->fFrameRate = frameRateFromCode[frame_rate_code];
#ifdef DEBUG
  unsigned bit_rate_value                = (next4Bytes&0xFFFFC000)>>(32-18);
  unsigned vbv_buffer_size_value         = (next4Bytes&0x00001FF8)>>3;
  fprintf(stderr, "horizontal_size_value: %d, vertical_size_value: %d, aspect_ratio_information: %d, frame_rate_code: %d (=>%f fps), bit_rate_value: %d (=>%d bps), vbv_buffer_size_value: %d\n", horizontal_size_value, vertical_size_value, aspect_ratio_information, frame_rate_code, fUsingSource->fFrameRate, bit_rate_value, bit_rate_value*400, vbv_buffer_size_value);
#endif

  // Now, copy all bytes that we see, up until we reach a GROUP_START_CODE
  // or a PICTURE_START_CODE:
  do {
    saveToNextCode(next4Bytes);
  } while (next4Bytes != GROUP_START_CODE && next4Bytes != PICTURE_START_CODE);
  
  setParseState((next4Bytes == GROUP_START_CODE)
		? PARSING_GOP_HEADER : PARSING_PICTURE_HEADER);

  // Compute this frame's timestamp by noting how many pictures we've seen
  // since the last GOP header:
  fUsingSource->computeTimestamp(fPicturesSinceLastGOP);

  // Save this video_sequence_header, in case we need to insert a copy
  // into the stream later:
  saveCurrentVSH();

  return curFrameSize();
}

unsigned MPEGVideoStreamParser::parseGOPHeader() {
  // First check whether we should insert a previously-saved
  // 'video_sequence_header' here:
  if (needToUseSavedVSH()) return useSavedVSH();

#ifdef DEBUG
  fprintf(stderr, "parsing GOP header\n");
#endif
  // Note that we've already read the GROUP_START_CODE
  save4Bytes(GROUP_START_CODE);

  // Next, extract the (25-bit) time code from the next 4 bytes:
  unsigned next4Bytes = get4Bytes();
  unsigned time_code = (next4Bytes&0xFFFFFF80)>>(32-25);
#if defined(DEBUG) || defined(DEBUG_TIMESTAMPS)
  Boolean drop_frame_flag     = (time_code&0x01000000) != 0;
#endif
  unsigned time_code_hours    = (time_code&0x00F80000)>>19;
  unsigned time_code_minutes  = (time_code&0x0007E000)>>13;
  unsigned time_code_seconds  = (time_code&0x00000FC0)>>6;
  unsigned time_code_pictures = (time_code&0x0000003F);
#if defined(DEBUG) || defined(DEBUG_TIMESTAMPS)
  fprintf(stderr, "time_code: 0x%07x, drop_frame %d, hours %d, minutes %d, seconds %d, pictures %d\n", time_code, drop_frame_flag, time_code_hours, time_code_minutes, time_code_seconds, time_code_pictures);
#endif
#ifdef DEBUG
  Boolean closed_gop  = (next4Bytes&0x00000040) != 0;
  Boolean broken_link = (next4Bytes&0x00000020) != 0;
  fprintf(stderr, "closed_gop: %d, broken_link: %d\n", closed_gop, broken_link);
#endif

  // Now, copy all bytes that we see, up until we reach a PICTURE_START_CODE:
  do {
    saveToNextCode(next4Bytes);
  } while (next4Bytes != PICTURE_START_CODE);
  
  setParseState(PARSING_PICTURE_HEADER);

  // Record the time code:
  TimeCode& tc = fUsingSource->fCurGOPTimeCode; // abbrev
  unsigned day = tc.days;
  if (time_code_hours < tc.hours) {
    // Assume that the 'day' has wrapped around:
    ++day;
  }
  tc.days = day;
  tc.hours = time_code_hours;
  tc.minutes = time_code_minutes;
  tc.seconds = time_code_seconds;
  tc.pictures = time_code_pictures;
  if (!fUsingSource->fHaveSeenFirstTimeCode) {
    fUsingSource->setTimeCodeBaseParams();
  } else if (fUsingSource->fCurGOPTimeCode == fUsingSource->fPrevGOPTimeCode) {
    // The time code has not changed since last time.  Adjust for this:
    fUsingSource->fPicturesAdjustment += fPicturesSinceLastGOP;
  } else {
    // Normal case: The time code changed since last time.
    fUsingSource->fPrevGOPTimeCode = tc;
    fUsingSource->fPicturesAdjustment = 0;
  }

  fPicturesSinceLastGOP = 0;

  // Compute this frame's timestamp:
  fUsingSource->computeTimestamp(0);

  return curFrameSize();
}

inline Boolean isSliceStartCode(unsigned fourBytes) {
  if ((fourBytes&0xFFFFFF00) != 0x00000100) return False;

  unsigned char lastByte = fourBytes&0xFF;
  return lastByte <= 0xAF && lastByte >= 1;
}

unsigned MPEGVideoStreamParser::parsePictureHeader() {
#ifdef DEBUG
  fprintf(stderr, "parsing picture header\n");
#endif
  // Note that we've already read the PICTURE_START_CODE
  // Next, extract the temporal reference from the next 4 bytes:
  unsigned next4Bytes = get4Bytes();
  unsigned short temporal_reference = (next4Bytes&0xFFC00000)>>(32-10);
  unsigned char picture_coding_type = (next4Bytes&0x00380000)>>19;
#ifdef DEBUG
  unsigned short vbv_delay          = (next4Bytes&0x0007FFF8)>>3;
  fprintf(stderr, "temporal_reference: %d, picture_coding_type: %d, vbv_delay: %d\n", temporal_reference, picture_coding_type, vbv_delay);
#endif

  fSkippingCurrentPicture = fIFramesOnly && picture_coding_type != 1;
  if (fSkippingCurrentPicture) {
    // Skip all bytes that we see, up until we reach a slice_start_code:
    do {
      skipToNextCode(next4Bytes);
    } while (!isSliceStartCode(next4Bytes));
  } else {
    // Save the PICTURE_START_CODE that we've already read:
    save4Bytes(PICTURE_START_CODE);

    // Copy all bytes that we see, up until we reach a slice_start_code:
    do {
      saveToNextCode(next4Bytes);
    } while (!isSliceStartCode(next4Bytes));
  }
  
  setParseState(PARSING_SLICE);

  fCurrentSliceNumber = next4Bytes&0xFF;

  // Record the temporal reference:
  fCurPicTemporalReference = temporal_reference;

  // Compute this frame's timestamp:
  fUsingSource->computeTimestamp(fCurPicTemporalReference);

  if (fSkippingCurrentPicture) {
    return parse(); // try again, until we get a non-skipped frame
  } else { 
    return curFrameSize();
  }
}

unsigned MPEGVideoStreamParser::parseSlice() {
#ifdef DEBUG_SLICE
  fprintf(stderr, "parsing slice\n");
#endif
  // Note that we've already read the slice_start_code:
  unsigned next4Bytes = PICTURE_START_CODE|fCurrentSliceNumber;

  if (fSkippingCurrentPicture) {
    // Skip all bytes that we see, up until we reach a code of some sort:
    skipToNextCode(next4Bytes);
  } else {
    // Copy all bytes that we see, up until we reach a code of some sort:
    saveToNextCode(next4Bytes);
  }
  
  // The next thing to parse depends on the code that we just saw:
  if (isSliceStartCode(next4Bytes)) { // common case
    setParseState(PARSING_SLICE);
    fCurrentSliceNumber = next4Bytes&0xFF;
  } else {
    // Because we don't see any more slices, we are assumed to have ended
    // the current picture:
    ++fPicturesSinceLastGOP;
    ++fUsingSource->fPictureCount;
    fUsingSource->fPictureEndMarker = True; // HACK #####

    switch (next4Bytes) {
    case SEQUENCE_END_CODE: {
      setParseState(PARSING_VIDEO_SEQUENCE_HEADER);
      break;
    }
    case VIDEO_SEQUENCE_HEADER_START_CODE: {
      setParseState(PARSING_VIDEO_SEQUENCE_HEADER_SEEN_CODE);
      break;
    }
    case GROUP_START_CODE: {
      setParseState(PARSING_GOP_HEADER);
      break;
    }
    case PICTURE_START_CODE: {
      setParseState(PARSING_PICTURE_HEADER);
      break;
    }
    default: {
      fUsingSource->envir() << "MPEGVideoStreamParser::parseSlice(): Saw unexpected code "
			    << (void*)next4Bytes << "\n";
      setParseState(PARSING_SLICE); // the safest way to recover...
      break;
    }
    }
  }

  // Compute this frame's timestamp:
  fUsingSource->computeTimestamp(fCurPicTemporalReference);

  if (fSkippingCurrentPicture) {
    return parse(); // try again, until we get a non-skipped frame
  } else { 
    return curFrameSize();
  }
}
