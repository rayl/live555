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
// MPEG4-GENERIC ("audio", "video", or "application") RTP stream sources
// Implementation

#include "MPEG4GenericRTPSource.hh"
#include "BitVector.hh"

////////// MPEG4GenericBufferedPacket and MPEG4GenericBufferedPacketFactory

class MPEG4GenericBufferedPacket: public BufferedPacket {
public:
  MPEG4GenericBufferedPacket(MPEG4GenericRTPSource* ourSource);
  virtual ~MPEG4GenericBufferedPacket();

private: // redefined virtual functions
  virtual unsigned nextEnclosedFrameSize(unsigned char*& framePtr,
					 unsigned dataSize);
private:
  MPEG4GenericRTPSource* fOurSource;
};

class MPEG4GenericBufferedPacketFactory: public BufferedPacketFactory {
private: // redefined virtual functions
  virtual BufferedPacket* createNew(MultiFramedRTPSource* ourSource);
};


////////// AUHeader //////////
struct AUHeader {
  unsigned size;
  unsigned index; // indexDelta for the 2nd & subsequent headers
};


///////// MPEG4GenericRTPSource implementation ////////

//##### NOTE: INCOMPLETE!!! Support more modes, and interleaving #####

MPEG4GenericRTPSource*
MPEG4GenericRTPSource::createNew(UsageEnvironment& env, Groupsock* RTPgs,
				 unsigned char rtpPayloadFormat,
				 unsigned rtpTimestampFrequency,
				 char const* mediumName,
				 char const* mode,
				 unsigned sizeLength, unsigned indexLength,
				 unsigned indexDeltaLength
				 ) {
  return new MPEG4GenericRTPSource(env, RTPgs, rtpPayloadFormat,
				   rtpTimestampFrequency, mediumName,
				   mode, sizeLength, indexLength,
				   indexDeltaLength
				   );
}

MPEG4GenericRTPSource
::MPEG4GenericRTPSource(UsageEnvironment& env, Groupsock* RTPgs,
			unsigned char rtpPayloadFormat,
			unsigned rtpTimestampFrequency,
			char const* mediumName,
                        char const* mode,
                        unsigned sizeLength, unsigned indexLength,
                        unsigned indexDeltaLength
                        )
  : MultiFramedRTPSource(env, RTPgs,
			 rtpPayloadFormat, rtpTimestampFrequency,
			 new MPEG4GenericBufferedPacketFactory),
  fSizeLength(sizeLength), fIndexLength(indexLength),
  fIndexDeltaLength(indexDeltaLength),
  fNumAUHeaders(0), fNextAUHeader(0), fAUHeaders(NULL) {
    unsigned mimeTypeLength =
      strlen(mediumName) + 14 /* strlen("/MPEG4-GENERIC") */ + 1;
    fMIMEType = new char[mimeTypeLength];
    if (fMIMEType != NULL) {
      sprintf(fMIMEType, "%s/MPEG4-GENERIC", mediumName);
    }

    fMode = mode == NULL ? NULL : strdup(mode);
    // Check for a "mode" that we don't yet support: //#####
    if (mode == NULL || strcmp(mode, "aac-hbr") != 0) {
      fprintf(stderr, "MPEG4GenericRTPSource Warning: Unknown or unsupported \"mode\": %s\n", mode);
    }
}

MPEG4GenericRTPSource::~MPEG4GenericRTPSource() {
  delete fAUHeaders;
  delete fMIMEType;
}

Boolean MPEG4GenericRTPSource
::processSpecialHeader(unsigned char* headerStart,
		       unsigned packetSize,
		       Boolean rtpMarkerBit,
		       unsigned& resultSpecialHeaderSize) {
  fCurrentPacketBeginsFrame = fCurrentPacketCompletesFrame;
          // whether the *previous* packet ended a frame

  // The RTP "M" (marker) bit indicates the last fragment of a frame:
  fCurrentPacketCompletesFrame = rtpMarkerBit;

  // default values:
  resultSpecialHeaderSize = 0;
  fNumAUHeaders = 0;
  fNextAUHeader = 0;
  fAUHeaders = NULL;

  if (fSizeLength > 0) {
    // The packet begins with a "AU Header Section".  Parse it, to
    // determine the "AU-header"s for each frame present in this packet: 
    resultSpecialHeaderSize += 2;
    if (packetSize < resultSpecialHeaderSize) return False;

    unsigned AU_headers_length = (headerStart[0]<<8)|headerStart[1];
    unsigned AU_headers_length_bytes = (AU_headers_length+7)/8;
    if (packetSize
	< resultSpecialHeaderSize + AU_headers_length_bytes) return False;
    resultSpecialHeaderSize += AU_headers_length_bytes;

    // Figure out how many AU-headers are present in the packet:
    int bitsAvail = AU_headers_length - (fSizeLength + fIndexLength);
    if (bitsAvail >= 0 && (fSizeLength + fIndexDeltaLength) > 0) {
      fNumAUHeaders = 1 + bitsAvail/(fSizeLength + fIndexDeltaLength);
    }
    if (fNumAUHeaders > 0) {
      delete fAUHeaders; fAUHeaders = new AUHeader[fNumAUHeaders];
      // Fill in each header:
      BitVector bv(&headerStart[2], 0, AU_headers_length);
      fAUHeaders[0].size = bv.getBits(fSizeLength);
      fAUHeaders[0].index = bv.getBits(fIndexLength);

      for (unsigned i = 1; i < fNumAUHeaders; ++i) {
	fAUHeaders[i].size = bv.getBits(fSizeLength);
	fAUHeaders[i].index = bv.getBits(fIndexDeltaLength);
      }
    }

  }

  return True;
}

char const* MPEG4GenericRTPSource::MIMEtype() const {
  return fMIMEType;
}


////////// MPEG4GenericBufferedPacket
////////// and MPEG4GenericBufferedPacketFactory implementation

MPEG4GenericBufferedPacket
::MPEG4GenericBufferedPacket(MPEG4GenericRTPSource* ourSource)
  : fOurSource(ourSource) {
}

MPEG4GenericBufferedPacket::~MPEG4GenericBufferedPacket() {
}

unsigned MPEG4GenericBufferedPacket
::nextEnclosedFrameSize(unsigned char*& framePtr, unsigned dataSize) {
  // WE CURRENTLY DON'T IMPLEMENT INTERLEAVING.  FIX THIS! #####
  AUHeader* auHeader = fOurSource->fAUHeaders;
  unsigned numAUHeaders = fOurSource->fNumAUHeaders;
  if (auHeader == NULL
      || fOurSource->fNextAUHeader >= numAUHeaders) {
    fprintf(stderr, "MPEG4GenericBufferedPacket::nextEnclosedFrameSize(%d): data error (%p,%d,%d)!\n", dataSize, auHeader, fOurSource->fNextAUHeader, numAUHeaders);
    return dataSize;
  }

  auHeader = &auHeader[fOurSource->fNextAUHeader++];
  return auHeader->size <= dataSize ? auHeader->size : dataSize;
}

BufferedPacket* MPEG4GenericBufferedPacketFactory
::createNew(MultiFramedRTPSource* ourSource) {
  return new MPEG4GenericBufferedPacket((MPEG4GenericRTPSource*)ourSource);
}
