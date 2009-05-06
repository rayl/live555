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
::OnDemandServerMediaSubsession(UsageEnvironment& env)
  : ServerMediaSubsession(env),
    fSDPLines(NULL) {
  gethostname(fCNAME, sizeof fCNAME);
  fCNAME[sizeof fCNAME-1] = '\0'; // just in case
}

OnDemandServerMediaSubsession::~OnDemandServerMediaSubsession() {
  delete[] fSDPLines;
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
  StreamState(Groupsock* rtpGroupsock, RTPSink* rtpSink,
	      Groupsock* rtcpGroupsock, unsigned totalBW, char* CNAME,
	      FramedSource* mediaSource);
  virtual ~StreamState();

  void startPlaying();
  void pause();
  void endPlaying();

private:
  Groupsock* fRTPGroupsock;
  RTPSink* fRTPSink;

  Groupsock* fRTCPGroupsock;
  unsigned fTotalBW;
  char* fCNAME;
  RTCPInstance* fRTCPInstance;

  FramedSource* fMediaSource;
};

void OnDemandServerMediaSubsession
::getStreamParameters(unsigned clientSessionId,
		      netAddressBits clientAddress,
		      Port const& clientRTPPort,
		      Port const& clientRTCPPort,
		      Boolean& isMulticast,
		      netAddressBits& destinationAddress,
		      u_int8_t& destinationTTL,
		      Port& serverRTPPort,
		      Port& serverRTCPPort,
		      void*& streamToken) {
  // Create a new unicast RTP/RTCP stream, directed to the client:
  isMulticast = False;
  if (destinationAddress == 0) destinationAddress = clientAddress;

  // Create the media source:
  unsigned streamBitrate;
  FramedSource* mediaSource = createNewStreamSource(clientSessionId, streamBitrate);

  // Create a RTP sink for this stream:
  // First, create a 'groupsock' for it, and make sure that its port number is even:
  struct in_addr dummyAddr; dummyAddr.s_addr = 0;
  Groupsock* rtpGroupsock_old = NULL;
  Groupsock* rtpGroupsock;
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

  struct in_addr clientAddr; clientAddr.s_addr = clientAddress;
  rtpGroupsock->changeDestinationParameters(clientAddr, clientRTPPort, ~0);
  destinationTTL = rtpGroupsock->ttl();

  unsigned char rtpPayloadType = 96 + trackNumber()-1; // if dynamic
  RTPSink* rtpSink
    = createNewRTPSink(rtpGroupsock, rtpPayloadType, mediaSource);

  // Create a 'groupsock' for a 'RTCP instance' to be created later:
  Groupsock* rtcpGroupsock
    = new Groupsock(envir(), dummyAddr, serverRTPPortNum+1, 255);
  serverRTCPPort = rtcpGroupsock->port();
  rtcpGroupsock->changeDestinationParameters(clientAddr, clientRTCPPort, ~0);

  // Set up the state of the stream.  The stream will get started later:
  streamToken
    = new StreamState(rtpGroupsock, rtpSink,
		      rtcpGroupsock, streamBitrate, fCNAME, mediaSource);
}

void OnDemandServerMediaSubsession::startStream(void* streamToken) {
  StreamState* streamState = (StreamState*)streamToken; 
  if (streamState != NULL) streamState->startPlaying();
}

void OnDemandServerMediaSubsession::pauseStream(void* streamToken) {
  StreamState* streamState = (StreamState*)streamToken; 
  if (streamState != NULL) streamState->pause();
}

void OnDemandServerMediaSubsession::endStream(void* streamToken) {
  StreamState* streamState = (StreamState*)streamToken; 
  if (streamState != NULL) streamState->endPlaying();
}

void OnDemandServerMediaSubsession::deleteStream(void* streamToken) {
  StreamState* streamState = (StreamState*)streamToken; 
  delete streamState;
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
  streamState->endPlaying();
}

StreamState::StreamState(Groupsock* rtpGroupsock, RTPSink* rtpSink,
			 Groupsock* rtcpGroupsock,
			 unsigned totalBW, char* CNAME,
			 FramedSource* mediaSource)
  : fRTPGroupsock(rtpGroupsock), fRTPSink(rtpSink),
    fRTCPGroupsock(rtcpGroupsock), fTotalBW(totalBW), fCNAME(CNAME),
    fRTCPInstance(NULL) /* created later */,
    fMediaSource(mediaSource) {
  if (fRTCPGroupsock != NULL) {
    // Create (and start) a 'RTCP instance' for this RTP sink:
    fRTCPInstance
      = RTCPInstance::createNew(fRTPSink->envir(), fRTCPGroupsock,
				fTotalBW, (unsigned char*)fCNAME,
				fRTPSink, NULL /* we're a server */);
    // Note: This starts RTCP running automatically
  }
}  

StreamState::~StreamState() {
  endPlaying();
}

void StreamState::startPlaying() {
  if (fRTPSink != NULL && fMediaSource != NULL) {
    fRTPSink->startPlaying(*fMediaSource, afterPlayingStreamState, this);
  }
}

void StreamState::pause() {
  if (fRTPSink != NULL) fRTPSink->stopPlaying();
}

void StreamState::endPlaying() {
  // Delete allocated media objects, and corresponding 'groupsock's:
  Medium::close(fRTPSink); fRTPSink = NULL;
  delete fRTPGroupsock; fRTPGroupsock = NULL;
  Medium::close(fRTCPInstance); fRTCPInstance = NULL;
  delete fRTCPGroupsock /* will send a RTCP BYE */; fRTCPGroupsock = NULL;
  Medium::close(fMediaSource); fMediaSource = NULL;
}
