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
// A filter that breaks up a H.264 Video Elementary Stream into NAL units.
// Implementation

#include "H264VideoStreamFramer.hh"
#include "MPEGVideoStreamParser.hh"

#define DEBUG 1 //#####@@@@@
////////// H264VideoStreamParser definition //////////

class H264VideoStreamParser: public MPEGVideoStreamParser {
public:
  H264VideoStreamParser(H264VideoStreamFramer* usingSource, FramedSource* inputSource);
  virtual ~H264VideoStreamParser();

private: // redefined virtual functions:
#if 0
  virtual void flushInput();
#endif
  virtual unsigned parse();

private:
  Boolean fHaveSeenFirstStartCode;
};


////////// H264VideoStreamFramer implementation //////////

H264VideoStreamFramer* H264VideoStreamFramer::createNew(UsageEnvironment& env, FramedSource* inputSource) {
  return new H264VideoStreamFramer(env, inputSource);
}

H264VideoStreamFramer::H264VideoStreamFramer(UsageEnvironment& env, FramedSource* inputSource, Boolean createParser)
  : MPEGVideoStreamFramer(env, inputSource) {
  fParser = createParser
    ? new H264VideoStreamParser(this, inputSource)
    : NULL;
}

H264VideoStreamFramer::~H264VideoStreamFramer() {
}

Boolean H264VideoStreamFramer::isH264VideoStreamFramer() const {
  return True;
}


////////// H264VideoStreamParser implementation //////////

H264VideoStreamParser::H264VideoStreamParser(H264VideoStreamFramer* usingSource, FramedSource* inputSource)
  : MPEGVideoStreamParser(usingSource, inputSource),
    fHaveSeenFirstStartCode(False) {
}

H264VideoStreamParser::~H264VideoStreamParser() {
}

#define NAL_UNIT_START_CODE 0x00000001

unsigned H264VideoStreamParser::parse() {
  try {
    // The stream must start with a NAL_UNIT_START_CODE:
    if (!fHaveSeenFirstStartCode) {
      fprintf(stderr, "#####@@@@@parse()1: test4Bytes():0x%08x\n", test4Bytes());
      // Skip over any input bytes that precede the first NAL_UNIT_START_CODE:
      u_int32_t first4Bytes;
      while ((first4Bytes = test4Bytes()) != NAL_UNIT_START_CODE) {
	get1Byte(); setParseState(); // ensures that we progress over bad data
      }
      fprintf(stderr, "#####@@@@@parse()2: first4Bytes():0x%08x\n", first4Bytes);
      skipBytes(4); // skip this initial code
      
      setParseState();
      fHaveSeenFirstStartCode = True; // from now on
    }
    
    // Then save everything up until the next code:
    u_int32_t curWord = get4Bytes();
    while (curWord != NAL_UNIT_START_CODE) {
      if ((unsigned)(curWord&0xFF) > 1) {
        // a sync word definitely doesn't begin anywhere in "curWord"
        save4Bytes(curWord);
        curWord = get4Bytes();
      } else {
        // a sync word might begin in "curWord", although not at its start
        saveByte(curWord>>24);
        u_int8_t newByte = get1Byte();
        curWord = (curWord<<8)|newByte;
      }
    }
    setParseState();
    static unsigned totBytes = 0; totBytes += 4+curFrameSize();//#####@@@@@
    fprintf(stderr, "#####@@@@@parse()4: test4Bytes():0x%08x, returning %d bytes (total %d == 0x%x)\n", test4Bytes(), curFrameSize(), totBytes, totBytes);

    return curFrameSize();//#####@@@@@
  } catch (int /*e*/) {
#ifdef DEBUG
    fprintf(stderr, "H264VideoStreamParser::parse() EXCEPTION (This is normal behavior - *not* an error)\n");
#endif
    return 0;  // the parsing got interrupted
  }
}
