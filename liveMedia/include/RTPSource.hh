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
// RTP Sources
// C++ header

#ifndef _RTP_SOURCE_HH
#define _RTP_SOURCE_HH

#ifndef _FRAMED_SOURCE_HH
#include "FramedSource.hh"
#endif
#ifndef _RTP_INTERFACE_HH
#include "RTPInterface.hh"
#endif

class RTPReceptionStatsDB; // forward

class RTPSource: public FramedSource {
public:
  static Boolean lookupByName(UsageEnvironment& env, char const* sourceName,
			      RTPSource*& resultSource);

  unsigned short curPacketRTPSeqNum() const { return fCurPacketRTPSeqNum; }
  unsigned curPacketRTPTimestamp() const { return fCurPacketRTPTimestamp; }
  Boolean curPacketMarkerBit() const { return fCurPacketMarkerBit; }

  unsigned char rtpPayloadFormat() const { return fRTPPayloadFormat; }

  virtual Boolean hasBeenSynchronizedUsingRTCP();

  Groupsock* RTPgs() const { return fRTPInterface.gs(); }

  virtual void setPacketReorderingThresholdTime(unsigned uSeconds) = 0;

  // used by RTCP:
  unsigned SSRC() const { return fSSRC; }
      // Note: This is *our* SSRC, not the SSRC in incoming RTP packets.
     // later need a means of changing the SSRC if there's a collision #####

  unsigned timestampFrequency() const {return fTimestampFrequency;}

  RTPReceptionStatsDB& receptionStatsDB() const {
    return *fReceptionStatsDB;
  }

  unsigned lastReceivedSSRC() const { return fLastReceivedSSRC; }
  // Note: This is the SSRC in the most recently received RTP packet; not *our* SSRC

  void setStreamSocket(int sockNum, unsigned char streamChannelId) {
    // hack to allow sending RTP over TCP (RFC 2236, section 10.12)
    fRTPInterface.setStreamSocket(sockNum, streamChannelId);
  }

  void setAuxilliaryReadHandler(AuxHandlerFunc* handlerFunc,
                                void* handlerClientData) {
    fRTPInterface.setAuxilliaryReadHandler(handlerFunc,
					   handlerClientData);
  }

protected:
  RTPSource(UsageEnvironment& env, Groupsock* RTPgs,
	    unsigned char rtpPayloadFormat, unsigned rtpTimestampFrequency);
      // abstract base class
  virtual ~RTPSource();

protected:
  RTPInterface fRTPInterface;
  unsigned short fCurPacketRTPSeqNum;
  unsigned fCurPacketRTPTimestamp;
  Boolean fCurPacketMarkerBit;
  Boolean fCurPacketHasBeenSynchronizedUsingRTCP;
  unsigned fLastReceivedSSRC;

private:
  // redefined virtual functions:
  virtual Boolean isRTPSource() const;
  virtual void getAttributes() const;

private:
  unsigned char fRTPPayloadFormat;
  unsigned fTimestampFrequency;
  unsigned fSSRC;

  RTPReceptionStatsDB* fReceptionStatsDB;
};


class RTPReceptionStats; // forward

class RTPReceptionStatsDB {
public:
  unsigned totNumPacketsReceived() const { return fTotNumPacketsReceived; }
  unsigned numActiveSourcesSinceLastReset() const {
    return fNumActiveSourcesSinceLastReset;
 }

  void reset();
      // resets periodic stats (called each time they're used to
      // generate a reception report)

  class Iterator {
  public:
    Iterator(RTPReceptionStatsDB& receptionStatsDB);
    virtual ~Iterator();

    RTPReceptionStats* next(Boolean includeInactiveSources = False);
        // NULL if none

  private:
    HashTable::Iterator* fIter;
  };

  // The following is called whenever a RTP packet is received: 
  void noteIncomingPacket(unsigned SSRC, unsigned short seqNum,
			  unsigned rtpTimestamp,
			  unsigned timestampFrequency,
			  Boolean useForJitterCalculation,
			  struct timeval& resultPresentationTime,
			  Boolean& resultHasBeenSyncedUsingRTCP,
			  unsigned packetSize /* payload only */);

  // The following is called whenever a RTCP SR packet is received: 
  void noteIncomingSR(unsigned SSRC,
		      unsigned ntpTimestampMSW, unsigned ntpTimestampLSW,
		      unsigned rtpTimestamp);

private: // constructor and destructor, called only by RTPSource:
  friend class RTPSource;
  RTPReceptionStatsDB(RTPSource& rtpSource);
  virtual ~RTPReceptionStatsDB();

private:
  RTPReceptionStats* lookup(unsigned SSRC) const;
  void add(unsigned SSRC, RTPReceptionStats* stats);

private:
  friend class Iterator;
  RTPSource& fOurRTPSource;
  HashTable* fTable;
  unsigned fNumActiveSourcesSinceLastReset;
  unsigned fTotNumPacketsReceived; // for all SSRCs
};

class RTPReceptionStats {
public:
  unsigned SSRC() const { return fSSRC; }
  unsigned numPacketsReceivedSinceLastReset() const {
    return fNumPacketsReceivedSinceLastReset;
  }
  unsigned totNumPacketsReceived() const { return fTotNumPacketsReceived; }
  double totNumKBytesReceived() const;

  unsigned totNumPacketsExpected() const {
    return fHighestExtSeqNumReceived - fBaseExtSeqNumReceived;
  }

  unsigned baseExtSeqNumReceived() const { return fBaseExtSeqNumReceived; }
  unsigned lastResetExtSeqNumReceived() const {
    return fLastResetExtSeqNumReceived;
  }
  unsigned highestExtSeqNumReceived() const {
    return fHighestExtSeqNumReceived;
  }

  unsigned jitter() const;

  unsigned lastReceivedSR_NTPmsw() const { return fLastReceivedSR_NTPmsw; }
  unsigned lastReceivedSR_NTPlsw() const { return fLastReceivedSR_NTPlsw; }
  struct timeval const& lastReceivedSR_time() const {
    return fLastReceivedSR_time;
  }

  unsigned minInterPacketGapUS() const { return fMinInterPacketGapUS; }
  unsigned maxInterPacketGapUS() const { return fMaxInterPacketGapUS; }
  struct timeval const& totalInterPacketGaps() const {
    return fTotalInterPacketGaps;
  }

private:
  // called only by RTPReceptionStatsDB:
  friend class RTPReceptionStatsDB;
  RTPReceptionStats(RTPSource& rtpSource, unsigned SSRC,
		    unsigned short initialSeqNum);
  RTPReceptionStats(RTPSource& rtpSource, unsigned SSRC);
  virtual ~RTPReceptionStats();

  void noteIncomingPacket(unsigned short seqNum,
			  unsigned rtpTimestamp,
			  unsigned timestampFrequency,
			  Boolean useForJitterCalculation,
			  struct timeval& resultPresentationTime,
			  Boolean& resultHasBeenSyncedUsingRTCP,
			  unsigned packetSize /* payload only */);
  void noteIncomingSR(unsigned ntpTimestampMSW, unsigned ntpTimestampLSW,
		      unsigned rtpTimestamp);
  void init(unsigned SSRC);
  void initSeqNum(unsigned short initialSeqNum);
  void reset();
      // resets periodic stats (called each time they're used to
      // generate a reception report)

private:
  RTPSource& fOurRTPSource;
  unsigned fSSRC;
  unsigned fNumPacketsReceivedSinceLastReset;
  unsigned fTotNumPacketsReceived;
  u_int32_t fTotBytesReceived_hi, fTotBytesReceived_lo;
  Boolean fHaveSeenInitialSequenceNumber;
  unsigned fBaseExtSeqNumReceived;
  unsigned fLastResetExtSeqNumReceived;
  unsigned fHighestExtSeqNumReceived;
  int fLastTransit; // used in the jitter calculation
  unsigned fPreviousPacketRTPTimestamp;
  double fJitter;
  // The following are recorded whenever we receive a RTCP SR for this SSRC:
  unsigned fLastReceivedSR_NTPmsw; // NTP timestamp (from SR), most-signif
  unsigned fLastReceivedSR_NTPlsw; // NTP timestamp (from SR), least-signif
  struct timeval fLastReceivedSR_time;
  struct timeval fLastPacketReceptionTime;
  unsigned fMinInterPacketGapUS, fMaxInterPacketGapUS;
  struct timeval fTotalInterPacketGaps;

  // Used to convert from RTP timestamp to 'wall clock' time:
  Boolean fHasBeenSynchronized;
  unsigned fSyncTimestamp;
  struct timeval fSyncTime;
};


Boolean seqNumLT(unsigned short s1, unsigned short s2);
  // a 'less-than' on 16-bit sequence numbers

#endif
