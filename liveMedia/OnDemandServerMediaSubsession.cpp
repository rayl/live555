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
// A 'ServerMediaSubsession' object that creates new, unicast, "RTPSink"s
// on demand.
// Implementation

#include "OnDemandServerMediaSubsession.hh"
#include "RTCP.hh"
#include <GroupsockHelper.hh>

OnDemandServerMediaSubsession
::OnDemandServerMediaSubsession(UsageEnvironment& env,
				Boolean reuseFirstSource)
  : ServerMediaSubsession(env),
    fReuseFirstSource(reuseFirstSource), fLastStreamToken(NULL),
    fSDPLines(NULL) {
  fGroupsockHashTable = HashTable::create(ONE_WORD_HASH_KEYS);
  gethostname(fCNAME, sizeof fCNAME);
  fCNAME[sizeof fCNAME-1] = '\0'; // just in case
}

class Groupsocks {
public:
  Groupsocks(Groupsock* rtpGS, Groupsock* rtcpGS)
    : rtpGS(rtpGS), rtcpGS(rtcpGS) {
  }
  virtual ~Groupsocks() {
    delete rtpGS; delete rtcpGS;
  }

public:
  Groupsock* rtpGS;
  Groupsock* rtcpGS;
};

OnDemandServerMediaSubsession::~OnDemandServerMediaSubsession() {
  delete[] fSDPLines;

  // Clean out the groupsock hash table:
  while (1) {
    Groupsocks* groupsocks = (Groupsocks*)(fGroupsockHashTable->RemoveNext());
    if (groupsocks == NULL) break;
    delete groupsocks;
  }
  delete[] fGroupsockHashTable;
}

char const* OnDemandServerMediaSubsession::sdpLines() {
  if (fSDPLines == NULL) {
    // We need to construct a set of SDP lines that describe this
    // subsession (as a unicast stream).  To do so, we first create
    // dummy (unused) source and "RTPSink" objects,
    // whose parameters we use for the SDP lines: 
    unsigned estBitrate; // unused
    FramedSource* inputSource = createNewStreamSource(0, estBitrate);
    if (inputSource == NULL) return NULL; // file not found

    struct in_addr dummyAddr;
    dummyAddr.s_addr = 0;
    Groupsock dummyGroupsock(envir(), dummyAddr, 0, 0);
    unsigned char rtpPayloadType = 96 + trackNumber()-1; // if dynamic
    RTPSink* dummyRTPSink
      = createNewRTPSink(&dummyGroupsock, rtpPayloadType, inputSource);

    setSDPLinesFromRTPSink(dummyRTPSink, inputSource);
    Medium::close(dummyRTPSink);
    Medium::close(inputSource);
  }

  return fSDPLines;
}

// A class that represents the state of an ongoing stream
class StreamState {
public:
  StreamState(Port const& serverRTPPort, Port const& serverRTCPPort,
	      RTPSink* rtpSink,
	      int tcpSocketNum, // -1 means: use UDP
	      unsigned char rtpChannelId,
	      unsigned char rtcpChannelId,
	      unsigned totalBW, char* CNAME,
	      FramedSource* mediaSource);
  virtual ~StreamState();

  void startPlaying(Groupsock* rtpGS, Groupsock* rtcpGS);
  void pause();
  void endPlaying(Groupsock* rtpGS, Groupsock* rtcpGS);
  void reclaim();

  unsigned& referenceCount() { return fReferenceCount; }

  Port const& serverRTPPort() const { return fServerRTPPort; }
  Port const& serverRTCPPort() const { return fServerRTCPPort; }

private:
  unsigned fReferenceCount;

  Port fServerRTPPort, fServerRTCPPort;

  RTPSink* fRTPSink;

  int fTCPSocketNum;
  unsigned char fRTPChannelId, fRTCPChannelId;
  unsigned fTotalBW; char* fCNAME; RTCPInstance* fRTCPInstance;

  FramedSource* fMediaSource;
};

void OnDemandServerMediaSubsession
::getStreamParameters(unsigned clientSessionId,
		      netAddressBits clientAddress,
		      Port const& clientRTPPort,
		      Port const& clientRTCPPort,
		      int tcpSocketNum,
		      unsigned char rtpChannelId,
		      unsigned char rtcpChannelId,
		      netAddressBits& destinationAddress,
		      u_int8_t& destinationTTL,
		      Boolean& isMulticast,
		      Port& serverRTPPort,
		      Port& serverRTCPPort,
		      void*& streamToken) {
  if (destinationAddress == 0) destinationAddress = clientAddress;
  isMulticast = False;

  struct in_addr dummyAddr; dummyAddr.s_addr = 0;
  Groupsock* rtpGroupsock;
  Groupsock* rtcpGroupsock;

  if (fLastStreamToken != NULL && fReuseFirstSource) {
    // Special case: Rather than creating a new source, RTP sink, and RTCP,
    // we reuse the ones that we've already created.  However, we
    // create new, additional destination groupsocks for these:
    rtpGroupsock  = new Groupsock(envir(), dummyAddr, 0, 255);
    rtcpGroupsock = new Groupsock(envir(), dummyAddr, 0, 255);

    serverRTPPort = ((StreamState*)fLastStreamToken)->serverRTPPort();
    serverRTCPPort = ((StreamState*)fLastStreamToken)->serverRTCPPort();
    ++((StreamState*)fLastStreamToken)->referenceCount();
    streamToken = fLastStreamToken;
  } else {
    // Normal case: Create a new media source:
    unsigned streamBitrate;
    FramedSource* mediaSource
      = createNewStreamSource(clientSessionId, streamBitrate);

    // Create a new 'groupsock' for the RTP destination, and make sure that
    // its port number is even:
    Groupsock* rtpGroupsock_old = NULL;
    portNumBits serverRTPPortNum = 0;
    while (1) {
      rtpGroupsock = new Groupsock(envir(), dummyAddr, 0, 255);
      if (!getSourcePort(envir(), rtpGroupsock->socketNum(), serverRTPPort)) break;
      serverRTPPortNum = ntohs(serverRTPPort.num());

      // If the port number's even, we're done:
      if ((serverRTPPortNum&1) == 0) break;

      // Try again (while keeping the old 'groupsock' around, so that we get
      // a different socket number next time):
      delete rtpGroupsock_old;
      rtpGroupsock_old = rtpGroupsock;
    }
    delete rtpGroupsock_old;

    // Create a RTP sink for this stream:
    unsigned char rtpPayloadType = 96 + trackNumber()-1; // if dynamic
    RTPSink* rtpSink
      = createNewRTPSink(rtpGroupsock, rtpPayloadType, mediaSource);
    
    // Create a 'groupsock' for a 'RTCP instance' to be created later:
    rtcpGroupsock = new Groupsock(envir(), dummyAddr, serverRTPPortNum+1, 255);
    getSourcePort(envir(), rtcpGroupsock->socketNum(), serverRTCPPort);
    
    // Set up the state of the stream.  The stream will get started later:
    if (fReuseFirstSource) tcpSocketNum = -1; // TEMP we don't support TCP when reusing a single source #####
    streamToken = fLastStreamToken
      = new StreamState(serverRTPPort, serverRTCPPort, rtpSink,
			tcpSocketNum, rtpChannelId, rtcpChannelId,
			streamBitrate, fCNAME, mediaSource);
  }
  
  struct in_addr destinationAddr; destinationAddr.s_addr = destinationAddress;
  if (destinationTTL == 255) destinationTTL = rtpGroupsock->ttl();
  rtpGroupsock->changeDestinationParameters(destinationAddr, clientRTPPort, destinationTTL);
  rtcpGroupsock->changeDestinationParameters(destinationAddr, clientRTCPPort, destinationTTL);

  // Record these groupsocks as being for this client session id:
  Groupsocks* groupsocks = new Groupsocks(rtpGroupsock, rtcpGroupsock);
  fGroupsockHashTable->Add((char const*)clientSessionId, groupsocks);
}

void OnDemandServerMediaSubsession::startStream(unsigned clientSessionId,
						void* streamToken) {
  StreamState* streamState = (StreamState*)streamToken; 
  Groupsocks* groupsocks
    = (Groupsocks*)(fGroupsockHashTable->Lookup((char const*)clientSessionId));
  if (streamState != NULL && groupsocks != NULL) {
    streamState->startPlaying(groupsocks->rtpGS, groupsocks->rtcpGS);
  }
}

void OnDemandServerMediaSubsession::pauseStream(unsigned /*clientSessionId*/,
						void* streamToken) {
  // Pausing isn't allowed if multiple clients are receiving data from
  // the same source:
  if (fReuseFirstSource) return;

  StreamState* streamState = (StreamState*)streamToken; 
  if (streamState != NULL) streamState->pause();
}

void OnDemandServerMediaSubsession::deleteStream(unsigned clientSessionId,
						 void*& streamToken) {
  // Look up (and remove) the groupsocks for this client session:
  Groupsocks* groupsocks
    = (Groupsocks*)(fGroupsockHashTable->Lookup((char const*)clientSessionId));
  if (groupsocks != NULL) {
    fGroupsockHashTable->Remove((char const*)clientSessionId);
  }

  // Stop streaming to these groupsocks:
  StreamState* streamState = (StreamState*)streamToken; 
  if (streamState != NULL && groupsocks != NULL) {
    streamState->endPlaying(groupsocks->rtpGS, groupsocks->rtcpGS);
  }

  // Delete the "StreamState" structure if it's no longer being used:
  if (streamState != NULL && streamState->referenceCount() > 0) {
    --streamState->referenceCount();
    if (streamState->referenceCount() == 0) {
      delete streamState;
      if (fLastStreamToken == streamToken) fLastStreamToken = NULL; 
      streamToken = NULL;
    }
  }

  // Finally, delete the groupsocks themselves:
  delete groupsocks;
}

char const* OnDemandServerMediaSubsession
::getAuxSDPLine(RTPSink* rtpSink, FramedSource* /*inputSource*/) {
  // Default implementation:
  return rtpSink->auxSDPLine();
}

void OnDemandServerMediaSubsession
::setSDPLinesFromRTPSink(RTPSink* rtpSink, FramedSource* inputSource) {
  if (rtpSink == NULL) return;

  char const* mediaType = rtpSink->sdpMediaType();
  unsigned char rtpPayloadType = rtpSink->rtpPayloadType();
  char const* rtpPayloadFormatName = rtpSink->rtpPayloadFormatName();
  unsigned rtpTimestampFrequency = rtpSink->rtpTimestampFrequency();
  unsigned numChannels = rtpSink->numChannels();
  char* rtpmapLine;
  if (rtpPayloadType >= 96) {
    char* encodingParamsPart;
    if (numChannels != 1) {
      encodingParamsPart = new char[1 + 20 /* max int len */];
      sprintf(encodingParamsPart, "/%d", numChannels);
    } else {
      encodingParamsPart = strDup("");
    }
    char const* const rtpmapFmt = "a=rtpmap:%d %s/%d%s\r\n";
    unsigned rtpmapFmtSize = strlen(rtpmapFmt)
      + 3 /* max char len */ + strlen(rtpPayloadFormatName)
      + 20 /* max int len */ + strlen(encodingParamsPart);
    rtpmapLine = new char[rtpmapFmtSize];
    sprintf(rtpmapLine, rtpmapFmt,
	    rtpPayloadType, rtpPayloadFormatName,
	    rtpTimestampFrequency, encodingParamsPart);
    delete[] encodingParamsPart;
  } else {
    // Static payload type => no "a=rtpmap:" line
    rtpmapLine = strDup("");
  }
  unsigned rtpmapLineSize = strlen(rtpmapLine);
  char const* auxSDPLine = getAuxSDPLine(rtpSink, inputSource);
  if (auxSDPLine == NULL) auxSDPLine = "";
  unsigned auxSDPLineSize = strlen(auxSDPLine);
  
  char const* const sdpFmt =
    "m=%s 0 RTP/AVP %d\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "%s"
    "%s"
    "a=control:%s\r\n";
  unsigned sdpFmtSize = strlen(sdpFmt)
    + strlen(mediaType) + 3 /* max char len */
    + rtpmapLineSize
    + auxSDPLineSize
    + strlen(trackId());
  char* sdpLines = new char[sdpFmtSize];
  sprintf(sdpLines, sdpFmt,
	  mediaType, // m= <media>
	  rtpPayloadType, // m= <fmt list>
	  rtpmapLine, // a=rtpmap:... (if present)
	  auxSDPLine, // optional extra SDP line
	  trackId()); // a=control:<track-id>
  
  fSDPLines = strDup(sdpLines);
  delete[] sdpLines;
}


////////// StreamState implementation //////////

static void afterPlayingStreamState(void* clientData) {
  StreamState* streamState = (StreamState*)clientData;
  streamState->reclaim();
}

StreamState::StreamState(Port const& serverRTPPort, Port const& serverRTCPPort,
			 RTPSink* rtpSink,
			 int tcpSocketNum,
			 unsigned char rtpChannelId,
			 unsigned char rtcpChannelId,
			 unsigned totalBW, char* CNAME,
			 FramedSource* mediaSource)
  : fReferenceCount(1),
    fServerRTPPort(serverRTPPort), fServerRTCPPort(serverRTCPPort),
    fRTPSink(rtpSink),
    fTCPSocketNum(tcpSocketNum),
    fRTPChannelId(rtpChannelId), fRTCPChannelId(rtcpChannelId),
    fTotalBW(totalBW), fCNAME(CNAME), fRTCPInstance(NULL) /* created later */,
    fMediaSource(mediaSource) {
}  

StreamState::~StreamState() {
  reclaim();
}

void StreamState::startPlaying(Groupsock* rtpGS, Groupsock* rtcpGS) {
  if (fRTCPInstance == NULL // we're being called for the first time
      && fRTPSink != NULL && fMediaSource != NULL) {
    fRTPSink->startPlaying(*fMediaSource, afterPlayingStreamState, this);
  }

  if (fRTCPInstance == NULL && fRTPSink != NULL) {
    // Create (and start) a 'RTCP instance' for this RTP sink:
    fRTCPInstance
      = RTCPInstance::createNew(fRTPSink->envir(), rtcpGS,
				fTotalBW, (unsigned char*)fCNAME,
				fRTPSink, NULL /* we're a server */);
    // Note: This starts RTCP running automatically
  }

  // Tell the RTP sink and RTCP instance about this destination
  // (in case they don't already have it):
  if (fRTPSink != NULL) fRTPSink->addDestination(rtpGS);
  if (fRTCPInstance != NULL) fRTCPInstance->addDestination(rtcpGS);

  if (fTCPSocketNum >= 0) {
    // Change RTP and RTCP to use the TCP socket instead of UDP:
    if (fRTPSink != NULL) fRTPSink->setStreamSocket(fTCPSocketNum, fRTPChannelId);
    if (fRTCPInstance != NULL) fRTCPInstance->setStreamSocket(fTCPSocketNum, fRTCPChannelId);
  }
}

void StreamState::pause() {
  if (fRTPSink != NULL) fRTPSink->stopPlaying();
}

void StreamState::endPlaying(Groupsock* rtpGS, Groupsock* rtcpGS) {
    // Tell the RTP sink and RTCP instance to stop using these groupsocks:
    if (fRTPSink != NULL) fRTPSink->removeDestination(rtpGS);
    if (fRTCPInstance != NULL) fRTCPInstance->removeDestination(rtcpGS);
}

void StreamState::reclaim() {
  // Delete allocated media objects
  Medium::close(fRTCPInstance) /* will send a RTCP BYE */; fRTCPInstance = NULL;
  Medium::close(fRTPSink); fRTPSink = NULL;

  Medium::close(fMediaSource); fMediaSource = NULL;

  fReferenceCount = 0;
}
