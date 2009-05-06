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
// RTP Sinks
// Implementation

#include "RTPSink.hh"
#include "GroupsockHelper.hh"

////////// RTPSink //////////

Boolean RTPSink::lookupByName(UsageEnvironment& env, char const* sinkName,
				RTPSink*& resultSink) {
  resultSink = NULL; // unless we succeed

  MediaSink* sink;
  if (!MediaSink::lookupByName(env, sinkName, sink)) return False;

  if (!sink->isRTPSink()) {
    env.setResultMsg(sinkName, " is not a RTP sink");
    return False;
  }

  resultSink = (RTPSink*)sink;
  return True;
}

Boolean RTPSink::isRTPSink() const {
  return True;
}

#ifdef BSD
static struct timezone Idunno;
#else
static int Idunno;
#endif

RTPSink::RTPSink(UsageEnvironment& env,
		 Groupsock* rtpGS, unsigned char rtpPayloadType,
		 unsigned rtpTimestampFrequency,
		 char const* rtpPayloadFormatName,
		 unsigned numChannels)
  : MediaSink(env), fRTPInterface(this, rtpGS),
    fRTPPayloadType(rtpPayloadType),
    fPacketCount(0), fOctetCount(0), fTotalOctetCount(0),
    fTimestampFrequency(rtpTimestampFrequency),
    fRTPPayloadFormatName(strDup(rtpPayloadFormatName)),
    fNumChannels(numChannels) {
  gettimeofday(&fCreationTime, &Idunno);
  fTotalOctetCountStartTime = fCreationTime;

  fSeqNo = (unsigned short)our_random();
  fSSRC = (unsigned)our_random();
  fTimestampBase = (unsigned)our_random();
  fCurrentTimestamp = fTimestampBase;

  fTransmissionStatsDB = new RTPTransmissionStatsDB(*this);
}

RTPSink::~RTPSink() {
  delete[] (char*)fRTPPayloadFormatName;
}

unsigned RTPSink::convertToRTPTimestamp(struct timeval timestamp) const {
  unsigned rtpTimestamp = fTimestampBase;
  rtpTimestamp += (fTimestampFrequency*timestamp.tv_sec);
  rtpTimestamp += (unsigned)
    ((2.0*fTimestampFrequency*timestamp.tv_usec + 1000000.0)/2000000);
       // note: rounding

#ifdef DEBUG_TIMESTAMPS
  fprintf(stderr, "timestamp base: %u, presentationTime: %u.%06u,\n\tRTP timestamp: 0x%08x\n",
	  fTimestampBase, timestamp.tv_sec,
	  timestamp.tv_usec, rtpTimestamp);
  fflush(stderr);
#endif

  return rtpTimestamp;
}

void RTPSink::getTotalBitrate(unsigned& outNumBytes,
			      double& outElapsedTime) {
  struct timeval timeNow;
  gettimeofday(&timeNow, &Idunno);

  outNumBytes = fTotalOctetCount;
  outElapsedTime = (double)(timeNow.tv_sec-fTotalOctetCountStartTime.tv_sec)
    + (timeNow.tv_usec-fTotalOctetCountStartTime.tv_usec)/1000000.0;

  fTotalOctetCount = 0;
  fTotalOctetCountStartTime = timeNow;
}

char const* RTPSink::sdpMediaType() const {
  return "data";
  // default SDP media (m=) type, unless redefined by subclasses
}

char const* RTPSink::auxSDPLine() {
  return NULL; // by default
}


////////// RTPTransmissionStatsDB //////////

RTPTransmissionStatsDB::RTPTransmissionStatsDB(RTPSink& rtpSink)
  : fOurRTPSink(rtpSink),
    fTable(HashTable::create(ONE_WORD_HASH_KEYS)) {
  fNumReceivers=0;
}

RTPTransmissionStatsDB::~RTPTransmissionStatsDB() {
  // First, remove and delete all stats records from the table:
  RTPTransmissionStats* stats;
  while ((stats = (RTPTransmissionStats*)fTable->RemoveNext()) != NULL) {
    delete stats;
  }

  // Then, delete the table itself:
  delete fTable;
}

void RTPTransmissionStatsDB
::noteIncomingRR(unsigned SSRC,
                 unsigned lossStats, unsigned lastPacketNumReceived,
                 unsigned jitter, unsigned lastSRTime, unsigned diffSR_RRTime) {
  RTPTransmissionStats* stats = lookup(SSRC);
  if (stats == NULL) {
    // This is the first time we've heard of this SSRC.
    // Create a new record for it:
    stats = new RTPTransmissionStats(fOurRTPSink, SSRC);
    if (stats == NULL) return;
    add(SSRC, stats);
#ifdef DEBUG_RR
    fprintf(stderr, "Adding new entry for SSRC %x in RTPTransmissionStatsDB\n", SSRC);
#endif
  }

  stats->noteIncomingRR(lossStats, lastPacketNumReceived, jitter,
                        lastSRTime, diffSR_RRTime);
}

RTPTransmissionStatsDB::Iterator
::Iterator(RTPTransmissionStatsDB& receptionStatsDB)
  : fIter(HashTable::Iterator::create(*(receptionStatsDB.fTable))) {
}

RTPTransmissionStatsDB::Iterator::~Iterator() {
  delete fIter;
}

RTPTransmissionStats*
RTPTransmissionStatsDB::Iterator::next() {
  char const* key; // dummy
 
  return (RTPTransmissionStats*)(fIter->next(key));
}

RTPTransmissionStats* RTPTransmissionStatsDB::lookup(unsigned SSRC) const {
  long SSRC_long = (long)SSRC;
  return (RTPTransmissionStats*)(fTable->Lookup((char const*)SSRC_long));
}

void RTPTransmissionStatsDB::add(unsigned SSRC, RTPTransmissionStats* stats) {
  long SSRC_long = (long)SSRC;
  fTable->Add((char const*)SSRC_long, stats);
  ++fNumReceivers;
}


////////// RTPTransmissionStats //////////

RTPTransmissionStats::RTPTransmissionStats(RTPSink& rtpSink, unsigned SSRC)
  : fOurRTPSink(rtpSink), fSSRC(SSRC), fLastPacketNumReceived(0), fPacketLossRatio(0),
    fTotNumPacketsLost(0), fJitter(0), fLastSRTime(0), fDiffSR_RRTime(0),
    fFirstPacket(True) {
}

RTPTransmissionStats::~RTPTransmissionStats() {}

void RTPTransmissionStats::noteIncomingRR(unsigned lossStats,
					  unsigned lastPacketNumReceived,
					  unsigned jitter,
					  unsigned lastSRTime,
					  unsigned diffSR_RRTime) {
  if (fFirstPacket) {
    fFirstPacket = False;
    fFirstPacketNumReported = lastPacketNumReceived;
  } else {
    fOldValid = True;
    fOldLastPacketNumReceived = fLastPacketNumReceived;
    fOldTotNumPacketsLost = fTotNumPacketsLost;
  }
  gettimeofday(&fTimeReceived, &Idunno);

  fPacketLossRatio = lossStats>>24;
  fTotNumPacketsLost = lossStats&0xFFFFFF;
  fLastPacketNumReceived = lastPacketNumReceived;
  fJitter = jitter;
  fLastSRTime = lastSRTime;
  fDiffSR_RRTime = diffSR_RRTime;
#ifdef DEBUG_RR
  fprintf(stderr, "RTCP RR data (received at %lu.%06ld): lossStats 0x%08x, lastPacketNumReceived 0x%08x, jitter 0x%08x, lastSRTime 0x%08x, diffSR_RRTime 0x%08x\n",
          fTimeReceived.tv_sec, fTimeReceived.tv_usec, lossStats, lastPacketNumReceived, jitter, lastSRTime, diffSR_RRTime);
  unsigned rtd = roundTripDelay(); 
  fprintf(stderr, "=> round-trip delay: 0x%04x (== %f seconds)\n", rtd, rtd/65536.0);
#endif
}

unsigned RTPTransmissionStats::roundTripDelay() const {
  // Compute the round-trip delay that was indicated by the most recently-received
  // RTCP RR packet.  Use the method noted in the RTP/RTCP specification (RFC 3350).
  
  if (fLastSRTime == 0) {
    // Either no RTCP RR packet has been received yet, or else the
    // reporting receiver has not yet received any RTCP SR packets from us:
    return 0;
  }

  // First, convert the time that we received the last RTCP RR packet to NTP format,
  // in units of 1/65536 (2^-16) seconds:
  unsigned lastReceivedTimeNTP_high
    = fTimeReceived.tv_sec + 0x83AA7E80; // 1970 epoch -> 1900 epoch
  double fractionalPart = (fTimeReceived.tv_usec*0x0400)/15625.0; // 2^16/10^6
  unsigned lastReceivedTimeNTP
    = (unsigned)((lastReceivedTimeNTP_high<<16) + fractionalPart + 0.5);

  return lastReceivedTimeNTP - fLastSRTime - fDiffSR_RRTime; 
}

unsigned RTPTransmissionStats::packetsReceivedSinceLastRR() const {
  if (!fOldValid) return 0;

  return fLastPacketNumReceived-fOldLastPacketNumReceived;
}

u_int8_t RTPTransmissionStats::packetLossRatio() const {
  if (!fOldValid) return 0;

  return fPacketLossRatio;
}

int RTPTransmissionStats::packetsLostBetweenRR() const {
  if (!fOldValid) return 0;

  return fTotNumPacketsLost - fOldTotNumPacketsLost;
}
