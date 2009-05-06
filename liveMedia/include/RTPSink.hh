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
// C++ header

#ifndef _RTP_SINK_HH
#define _RTP_SINK_HH

#ifndef _MEDIA_SINK_HH
#include "MediaSink.hh"
#endif
#ifndef _RTP_INTERFACE_HH
#include "RTPInterface.hh"
#endif

class RTPSink: public MediaSink {
public:
  static Boolean lookupByName(UsageEnvironment& env, char const* sinkName,
			      RTPSink*& resultSink);

  // used by RTCP:
  unsigned SSRC() const {return fSSRC;}
     // later need a means of changing the SSRC if there's a collision #####
  unsigned convertToRTPTimestamp(struct timeval timestamp) const;
  unsigned packetCount() const {return fPacketCount;}
  unsigned octetCount() const {return fOctetCount;}

  // used by RTSP servers:
  Groupsock const& groupsockBeingUsed() const { return *(fRTPInterface.gs()); }
  Groupsock& groupsockBeingUsed() { return *(fRTPInterface.gs()); }

  unsigned char rtpPayloadType() const { return fRTPPayloadType; }
  unsigned rtpTimestampFrequency() const { return fTimestampFrequency; }
  void setRTPTimestampFrequency(unsigned freq) {
    fTimestampFrequency = freq;
  }
  char const* rtpPayloadFormatName() const {return fRTPPayloadFormatName;}

  unsigned numChannels() const { return fNumChannels; }

  virtual char const* sdpMediaType() const; // for use in SDP m= lines
  virtual char const* auxSDPLine();
      // optional SDP line (e.g. a=fmtp:...)

  unsigned short currentSeqNo() const { return fSeqNo; }
  unsigned currentTimestamp() const { return fCurrentTimestamp; }

  void setStreamSocket(int sockNum, unsigned char streamChannelId) {
    // hack to allow sending RTP over TCP (RFC 2236, section 10.12)
    fRTPInterface.setStreamSocket(sockNum, streamChannelId);
  }

  void getTotalBitrate(unsigned& outNumBytes, double& outElapsedTime);
      // returns the number of bytes sent since the last time that we
      // were called, and resets the counter.

protected:
  RTPSink(UsageEnvironment& env,
	  Groupsock* rtpGS, unsigned char rtpPayloadType,
	  unsigned rtpTimestampFrequency,
	  char const* rtpPayloadFormatName,
	  unsigned numChannels);
	// abstract base class

  virtual ~RTPSink();

  RTPInterface fRTPInterface;
  unsigned char fRTPPayloadType;
  unsigned fPacketCount, fOctetCount, fTotalOctetCount /*incl RTP hdr*/;
  struct timeval fTotalOctetCountStartTime;
  unsigned fCurrentTimestamp;
  unsigned short fSeqNo;

private:
  // redefined virtual functions:
  virtual Boolean isRTPSink() const;

private:
  unsigned fSSRC, fTimestampBase, fTimestampFrequency;
  char const* fRTPPayloadFormatName;
  unsigned fNumChannels;
  struct timeval fCreationTime;
};

#endif
