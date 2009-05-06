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
    fRTPPayloadFormatName(rtpPayloadFormatName == NULL
			  ? NULL : strDup(rtpPayloadFormatName)),
    fNumChannels(numChannels) {
  gettimeofday(&fCreationTime, &Idunno);
  fTotalOctetCountStartTime = fCreationTime;

  fSeqNo = (unsigned short)our_random();
  fSSRC = (unsigned)our_random();
  fTimestampBase = (unsigned)our_random();
  fCurrentTimestamp = fTimestampBase;
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
