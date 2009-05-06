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
// RTP source for a common kind of payload format: Those that pack multiple,
// complete codec frames (as many as possible) into each RTP packet.
// Implementation

#include "MultiFramedRTPSource.hh"
#include "GroupsockHelper.hh"
#include <string.h>

////////// ReorderingPacketBuffer definition //////////

class ReorderingPacketBuffer {
public:
  ReorderingPacketBuffer(BufferedPacketFactory* packetFactory);
  virtual ~ReorderingPacketBuffer();

  BufferedPacket* getFreePacket();
  void storePacket(BufferedPacket* bPacket);
  BufferedPacket* getNextCompletedPacket(Boolean& packetLossPreceded);
  void releaseUsedPacket(BufferedPacket* packet);
  void freePacket(BufferedPacket* packet) {
    if (packet != fSavedPacket) delete packet;
  }

  void setThresholdTime(unsigned uSeconds) { fThresholdTime = uSeconds; }

private:
  BufferedPacketFactory* fPacketFactory;
  unsigned fThresholdTime; // uSeconds
  Boolean fHaveSeenFirstPacket; // used to set initial "fNextExpectedSeqNo"
  unsigned short fNextExpectedSeqNo;
  BufferedPacket* fHeadPacket;
  BufferedPacket* fSavedPacket;
      // to avoid calling new/free in the common case
};


////////// MultiFramedRTPSource implementation //////////

MultiFramedRTPSource
::MultiFramedRTPSource(UsageEnvironment& env, Groupsock* RTPgs,
		       unsigned char rtpPayloadFormat,
		       unsigned rtpTimestampFrequency,
		       BufferedPacketFactory* packetFactory)
  : RTPSource(env, RTPgs, rtpPayloadFormat, rtpTimestampFrequency),
    fCurrentPacketBeginsFrame(True/*by default*/),
    fCurrentPacketCompletesFrame(True/*by default*/),
    fAreDoingNetworkReads(False), fNeedDelivery(False),
    fPacketLossInFragmentedFrame(False) {
  fReorderingBuffer = new ReorderingPacketBuffer(packetFactory);

  // Try to use a big receive buffer for RTP:
  increaseReceiveBufferTo(env, RTPgs->socketNum(), 50*1024);
}

MultiFramedRTPSource::~MultiFramedRTPSource() {
  fRTPInterface.stopNetworkReading();
  delete fReorderingBuffer;
}

Boolean MultiFramedRTPSource
::processSpecialHeader(unsigned char* /*headerStart*/,
		       unsigned /*packetSize*/,
		       Boolean /*rtpMarkerBit*/,
		       unsigned& resultSpecialHeaderSize) {
  // Default implementation: Assume no special header:
  resultSpecialHeaderSize = 0;
  return True;
}

Boolean MultiFramedRTPSource
::packetIsUsableInJitterCalculation(unsigned char* /*packet*/,
				    unsigned /*packetSize*/) {
  // Default implementation:
  return True;
}

void MultiFramedRTPSource::doGetNextFrame() {
  if (!fAreDoingNetworkReads) {
    // Turn on background read handling of incoming packets:
    fAreDoingNetworkReads = True;
    TaskScheduler::BackgroundHandlerProc* handler
      = (TaskScheduler::BackgroundHandlerProc*)&networkReadHandler;
    fRTPInterface.startNetworkReading(handler);
  }

  fSavedTo = fTo;
  fSavedMaxSize = fMaxSize;
  fFrameSize = 0; // for now
  fNeedDelivery = True;
  doGetNextFrame1();
}

void MultiFramedRTPSource::doGetNextFrame1() {
  // If we already have packet data available, then deliver it now.
  if (fNeedDelivery) {
    Boolean packetLossPrecededThis;
    BufferedPacket* nextPacket
      = fReorderingBuffer->getNextCompletedPacket(packetLossPrecededThis);
    if (nextPacket != NULL) {
      fNeedDelivery = False;

      if (nextPacket->useCount() == 0) {
	// Before using the packet, check whether it has a special header
	// that needs to be processed:
	unsigned specialHeaderSize;
	if (!processSpecialHeader(nextPacket->data(),
				  nextPacket->dataSize(),
				  nextPacket->rtpMarkerBit(),
				  specialHeaderSize)) {
	  // Something's wrong with the header; reject the packet:
	  fReorderingBuffer->releaseUsedPacket(nextPacket);
	  fNeedDelivery = True;
	  return;
	}
	nextPacket->skip(specialHeaderSize);
      }

      // Check whether we're part of a multi-packet frame, and whether
      // there was packet loss that would render this packet unusable:
      if (fCurrentPacketBeginsFrame) {
	if (packetLossPrecededThis || fPacketLossInFragmentedFrame) {
	  // We didn't get all of the previous frame.
	  // Forget any data that we used from it:
	  fTo = fSavedTo; fMaxSize = fSavedMaxSize;
	  fFrameSize = 0;
	}
	fPacketLossInFragmentedFrame = False;
      } else if (packetLossPrecededThis) {
	// We're in a multi-packet frame, with preceding packet loss
	fPacketLossInFragmentedFrame = True;
      }
      if (fPacketLossInFragmentedFrame) {
	// This packet is unusable; reject it:
	  fReorderingBuffer->releaseUsedPacket(nextPacket);
	  fNeedDelivery = True;
	  return;
      }

      // The packet is usable. Deliver all or part of it to our caller:
      unsigned frameSize;
      nextPacket->use(fTo, fMaxSize, frameSize,
		      fCurPacketRTPSeqNum, fCurPacketRTPTimestamp,
		      fPresentationTime,fCurPacketMarkerBit);
      fFrameSize += frameSize;
    
      if (!nextPacket->hasUsableData()) {
	// We're completely done with this packet now
	fReorderingBuffer->releaseUsedPacket(nextPacket);
      }

      if (frameSize >= fMaxSize || fCurrentPacketCompletesFrame) {
	// We have all the data that the client wants.
	// Call our own 'after getting' function.  Because we're preceded
	// by a network read, we can call this directly, without risking
	// infinite recursion.
	afterGetting(this);
      } else {
	// This packet contained fragmented data, and does not complete
	// the data that the client wants.  Keep getting data:
	fTo += frameSize; fMaxSize -= frameSize;
	fNeedDelivery = True;
	doGetNextFrame1();
      }
    }
  }
}

void MultiFramedRTPSource
::setPacketReorderingThresholdTime(unsigned uSeconds) {
  fReorderingBuffer->setThresholdTime(uSeconds);
}

#ifdef BSD
static struct timezone Idunno;
#else
static int Idunno;
#endif

#define ADVANCE(n) do { bPacket->skip(n); } while (0)

void MultiFramedRTPSource::networkReadHandler(MultiFramedRTPSource* source,
					      int /*mask*/) {
  // Get a free BufferedPacket descriptor to hold the new network packet:
  BufferedPacket* bPacket = source->fReorderingBuffer->getFreePacket();

  // Read the network packet, and perform sanity checks on the RTP header:
  Boolean readSuccess = False;
  do {
    if (!bPacket->fillInData(source->fRTPInterface)) break;
    
    // Check for the 12-byte RTP header:
    if (bPacket->dataSize() < 12) break;
    unsigned rtpHdr = ntohl(*(unsigned*)(bPacket->data())); ADVANCE(4);
    Boolean rtpMarkerBit = (rtpHdr&0x00800000) >> 23;
    unsigned rtpTimestamp = ntohl(*(unsigned*)(bPacket->data()));ADVANCE(4);
    unsigned rtpSSRC = ntohl(*(unsigned*)(bPacket->data())); ADVANCE(4);
    
    // Check the RTP version number (it should be 2):
    if ((rtpHdr&0xC0000000) != 0x80000000) break;
    
    // Skip over any CSRC identifiers in the header:
    unsigned cc = (rtpHdr>>24)&0xF;
    if (bPacket->dataSize() < cc) break;
    ADVANCE(cc);
    
    // Check for (& ignore) any RTP header extension
    if (rtpHdr&0x10000000) {
      if (bPacket->dataSize() < 4) break;
      unsigned extHdr = ntohl(*(unsigned*)(bPacket->data())); ADVANCE(4);
      unsigned remExtSize = 4*(extHdr&0xFFFF);
      if (bPacket->dataSize() < remExtSize) break;
      ADVANCE(remExtSize);
    }
    
    // Discard any padding bytes:
    if (rtpHdr&0x20000000) {
      if (bPacket->dataSize() == 0) break;
      unsigned numPaddingBytes
	= (unsigned)(bPacket->data())[bPacket->dataSize()-1];
      if (bPacket->dataSize() < numPaddingBytes) break;
      bPacket->removePadding(numPaddingBytes);
    }    
    // Check the Payload Type.
    if ((unsigned char)((rtpHdr&0x007F0000)>>16)
	!= source->rtpPayloadFormat()) {
      break;
    }
    
    // The rest of the packet is the usable data.  Record and save it:
    unsigned short rtpSeqNo = (unsigned short)(rtpHdr&0xFFFF);
    Boolean usableInJitterCalculation
      = source->packetIsUsableInJitterCalculation((bPacket->data()),
						  bPacket->dataSize());
    struct timeval presentationTime; // computed by:
    source->receptionStatsDB()
      .noteIncomingPacket(rtpSSRC, rtpSeqNo, rtpTimestamp,
			  source->timestampFrequency(),
			  usableInJitterCalculation, presentationTime);
  
    // Fill in the rest of the packet descriptor, and store it:
    struct timeval timeNow;
    gettimeofday(&timeNow, &Idunno);
    bPacket->assignMiscParams(rtpSeqNo, rtpTimestamp, presentationTime,
			      rtpMarkerBit, timeNow);
    source->fReorderingBuffer->storePacket(bPacket);

    readSuccess = True;
  } while (0);
  if (!readSuccess) source->fReorderingBuffer->freePacket(bPacket);

  source->doGetNextFrame1();
  // If we didn't get proper data this time, we'll get another chance
}


////////// BufferedPacket and BufferedPacketFactory implementation /////

#define MAX_PACKET_SIZE 10000

BufferedPacket::BufferedPacket()
  : fPacketSize(MAX_PACKET_SIZE),
    fBuf(new unsigned char[MAX_PACKET_SIZE]),
    fNextPacket(NULL) {
}

BufferedPacket::~BufferedPacket() {
  delete fNextPacket;
  delete fBuf;
}

void BufferedPacket::reset() {
  fHead = fTail = 0;
  fUseCount = 0;
}

unsigned BufferedPacket
::nextEnclosedFrameSize(unsigned char*& /*framePtr*/, unsigned dataSize) {
  // By default, use the entire buffered data, even though it may consist
  // of more than one frame, on the assumption that the client doesn't
  // care.  (This is more efficient than delivering a frame at a time)
  return dataSize;
}

Boolean BufferedPacket::fillInData(RTPInterface& rtpInterface) {
  reset();

  unsigned numBytesRead;
  struct sockaddr_in fromAddress;
  if (!rtpInterface.handleRead(&fBuf[fTail], fPacketSize-fTail, numBytesRead,
			       fromAddress)) {
    return False;
  }
  fTail += numBytesRead;
  return True;
}

void BufferedPacket
::assignMiscParams(unsigned short rtpSeqNo, unsigned rtpTimestamp,
		   struct timeval presentationTime, Boolean rtpMarkerBit,
		   struct timeval timeReceived) {
  fRTPSeqNo = rtpSeqNo;
  fRTPTimestamp = rtpTimestamp;
  fPresentationTime = presentationTime;
  fRTPMarkerBit = rtpMarkerBit;
  fTimeReceived = timeReceived;
}

void BufferedPacket::skip(unsigned numBytes) {
  fHead += numBytes;
  if (fHead > fTail) fHead = fTail;
}

void BufferedPacket::removePadding(unsigned numBytes) {
  if (numBytes > fTail-fHead) numBytes = fTail-fHead;
  fTail -= numBytes;
}

void BufferedPacket::use(unsigned char* to, unsigned toSize,
			 unsigned& bytesUsed, 
			 unsigned short& rtpSeqNo, unsigned& rtpTimestamp,
			 struct timeval& presentationTime,
			 Boolean& rtpMarkerBit) {
  unsigned char* origFramePtr = &fBuf[fHead];
  unsigned char* newFramePtr = origFramePtr; //may change in the call below
  unsigned frameSize = nextEnclosedFrameSize(newFramePtr, fTail - fHead);
  bytesUsed = (frameSize > toSize) ? toSize : frameSize;

  memmove(to, newFramePtr, bytesUsed);
  fHead += (newFramePtr - origFramePtr) + frameSize;
  ++fUseCount;

  rtpSeqNo = fRTPSeqNo;
  rtpTimestamp = fRTPTimestamp;
  presentationTime = fPresentationTime;
  rtpMarkerBit = fRTPMarkerBit;
}

BufferedPacket* BufferedPacketFactory::createNew() {
  return new BufferedPacket;
}


////////// ReorderingPacketBuffer definition //////////

ReorderingPacketBuffer
::ReorderingPacketBuffer(BufferedPacketFactory* packetFactory)
  : fThresholdTime(100000) /* default reordering threshold: 100 ms */,
    fHaveSeenFirstPacket(False), fHeadPacket(NULL), fSavedPacket(NULL) {
  fPacketFactory = (packetFactory == NULL)
    ? (new BufferedPacketFactory)
    : packetFactory;
}

ReorderingPacketBuffer::~ReorderingPacketBuffer() {
  delete fHeadPacket;
  delete fPacketFactory;
}

BufferedPacket* ReorderingPacketBuffer::getFreePacket() {
  if (fSavedPacket == NULL) { // we're being called for the first time
    fSavedPacket = fPacketFactory->createNew();
  }

  return fHeadPacket == NULL ? fSavedPacket : fPacketFactory->createNew();
}

void ReorderingPacketBuffer::storePacket(BufferedPacket* bPacket) {
  unsigned short rtpSeqNo = bPacket->rtpSeqNo();

  if (!fHaveSeenFirstPacket) {
    fNextExpectedSeqNo = rtpSeqNo; // initialization
    fHaveSeenFirstPacket = True;
  }

  // Ignore this packet if its sequence number is less than the one
  // that we're looking for (in this case, it's been excessively delayed).
  // (But (sanity check) if the new packet's sequence number is a *lot*
  // less, then accept it anyway.)
  unsigned short const seqNoThreshold = 100;
  if (seqNumLT(rtpSeqNo, fNextExpectedSeqNo)
      && seqNumLT(fNextExpectedSeqNo, rtpSeqNo+seqNoThreshold)) {
    return;
  }
  
  // Figure out where the new packet will be stored in the queue:
  BufferedPacket* beforePtr = NULL;
  BufferedPacket* afterPtr = fHeadPacket;
  while (afterPtr != NULL) {
    if (seqNumLT(rtpSeqNo, afterPtr->rtpSeqNo())) break; // it comes here
    if (rtpSeqNo == afterPtr->rtpSeqNo()) {
      // This is a duplicate packet - ignore it
      return;
    }
    
    beforePtr = afterPtr;
    afterPtr = afterPtr->nextPacket();
  }
  
  // Link our new packet between "beforePtr" and "afterPtr":
  bPacket->nextPacket() = afterPtr;
  if (beforePtr == NULL) {
    fHeadPacket = bPacket;
  } else {
    beforePtr->nextPacket() = bPacket;
  }
}

void ReorderingPacketBuffer::releaseUsedPacket(BufferedPacket* packet) {
  // ASSERT: packet == fHeadPacket
  // ASSERT: fNextExpectedSeqNo == packet->rtpSeqNo()
  ++fNextExpectedSeqNo; // because we're finished with this packet now

  fHeadPacket = fHeadPacket->nextPacket();
  packet->nextPacket() = NULL;

  freePacket(packet);
}

BufferedPacket* ReorderingPacketBuffer
::getNextCompletedPacket(Boolean& packetLossPreceded) {
  if (fHeadPacket == NULL) return NULL;

  // Check whether the next packet we want is already at the head
  // of the queue:
  // ASSERT: fHeadPacket->rtpSeqNo() >= fNextExpectedSeqNo
  if (fHeadPacket->rtpSeqNo() == fNextExpectedSeqNo) {
    packetLossPreceded = False;
    return fHeadPacket;
  }

  // We're still waiting for our desired packet to arrive.  However, if
  // our time threshold has been exceeded, then forget it, and return
  // the head packet instead:
  struct timeval timeNow;
  gettimeofday(&timeNow, &Idunno);
  unsigned uSecondsSinceReceived
    = (timeNow.tv_sec - fHeadPacket->timeReceived().tv_sec)*1000000
    + (timeNow.tv_usec - fHeadPacket->timeReceived().tv_usec);
  if (uSecondsSinceReceived > fThresholdTime) {
    fNextExpectedSeqNo = fHeadPacket->rtpSeqNo();
        // we've given up on earlier packets now
    packetLossPreceded = True;
    return fHeadPacket;
  }

  // Otherwise, keep waiting for our desired packet to arrive:
  return NULL;
}
