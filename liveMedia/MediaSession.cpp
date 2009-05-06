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
// A data structure that represents a session that consists of
// potentially multiple (audio and/or video) sub-sessions
// Implementation

#include "liveMedia.hh"
#include "GroupsockHelper.hh"

////////// MediaSession //////////

MediaSession* MediaSession::createNew(UsageEnvironment& env,
				      char const* sdpDescription) {
  MediaSession* newSession = new MediaSession(env);
  if (newSession != NULL) {
    if (!newSession->initializeWithSDP(sdpDescription)) {
      delete newSession;
      return NULL;
    }
  }

  return newSession;
}

Boolean MediaSession::lookupByName(UsageEnvironment& env,
				   char const* instanceName,
				   MediaSession*& resultSession) {
  resultSession = NULL; // unless we succeed

  Medium* medium;
  if (!Medium::lookupByName(env, instanceName, medium)) return False;

  if (!medium->isMediaSession()) {
    env.setResultMsg(instanceName, " is not a 'MediaSession' object");
    return False;
  }

  resultSession = (MediaSession*)medium;
  return True;
}

MediaSession::MediaSession(UsageEnvironment& env)
  : Medium(env), fSubsessionsHead(NULL), fSubsessionsTail(NULL),
    fConnectionEndpointName(NULL), fMaxPlayEndTime(0.0) {
  // Get our host name, and use this for the RTCP CNAME:
  const unsigned maxCNAMElen = 100;
  char CNAME[maxCNAMElen+1];
  gethostname((char*)CNAME, maxCNAMElen);
  CNAME[maxCNAMElen] = '\0'; // just in case
  fCNAME = strdup(CNAME);
}

MediaSession::~MediaSession() {
  delete fSubsessionsHead;
  delete fCNAME;
  delete fConnectionEndpointName;
}

Boolean MediaSession::isMediaSession() const {
  return True;
}

Boolean MediaSession::initializeWithSDP(char const* sdpDescription) {
  if (sdpDescription == NULL) return False;

  // Begin by processing all SDP lines until we see the first "m="
  char const* sdpLine = sdpDescription;
  char const* nextSDPLine;
  while (1) {
    if (!parseSDPLine(sdpLine, nextSDPLine)) return False;
    //##### We should really check for: 
    // - "a=control:" attributes (to set the URL for aggregate control)
    // - the correct SDP version (v=0)
    // - a "c=..." line specifying a session-level multicast address
    if (sdpLine[0] == 'm') break;
    sdpLine = nextSDPLine;
    if (sdpLine == NULL) break; // there are no m= lines at all 

    // Check for various special SDP lines that we understand:
    if (parseSDPLine_c(sdpLine)) continue;
    if (parseSDPAttribute_range(sdpLine)) continue;
  }
    
  while (sdpLine != NULL) {
    // We have a "m=" line, representing a new sub-session:
    MediaSubsession* subsession = new MediaSubsession(*this);
    if (subsession == NULL) {
      envir().setResultMsg("Unable to create new MediaSubsession");
      return False;
    }

    // Insert this subsession at the end of the list:
    if (fSubsessionsTail == NULL) {
      fSubsessionsHead = fSubsessionsTail = subsession;
    } else {
      fSubsessionsTail->setNext(subsession);
      fSubsessionsTail = subsession;
    }

    // Parse the line as "m=<medium_name> <client_portNum> RTP/AVP <fmt>"
    // (Should we be checking for >1 payload format number here??)#####
    char* mediumName = strdup(sdpLine); // ensures we have enough space
    unsigned payloadFormat;
    if (sscanf(sdpLine, "m=%s %hu RTP/AVP %u",
	       mediumName, &subsession->fClientPortNum, &payloadFormat) != 3
	|| payloadFormat > 127) {
      char* sdpLineStr;
      if (nextSDPLine == NULL) {
	sdpLineStr = (char*)sdpLine;
      } else {
	sdpLineStr = strdup(sdpLine);
	sdpLineStr[nextSDPLine-sdpLine] = '\0';
      }
      envir().setResultMsg("Bad SDP \"m=\" line: ", sdpLineStr);
      if (sdpLineStr != (char*)sdpLine) delete sdpLineStr;
      delete mediumName;
      return False;
    }
    subsession->serverPortNum = subsession->fClientPortNum; // by default

    char const* mStart = sdpLine;
    subsession->fSavedSDPLines = strdup(mStart);

    subsession->fMediumName = strdup(mediumName);
    delete mediumName;
    subsession->fRTPPayloadFormat = payloadFormat;

    // Process the following SDP lines, up until the next "m=":
    while (1) {
      sdpLine = nextSDPLine;
      if (sdpLine == NULL) break; // we've reached the end
      if (!parseSDPLine(sdpLine, nextSDPLine)) return False;

      if (sdpLine[0] == 'm') break; // we've reached the next subsession

      // Check for various special SDP lines that we understand:
      if (subsession->parseSDPLine_c(sdpLine)) continue;
      if (subsession->parseSDPAttribute_rtpmap(sdpLine)) continue;
      if (subsession->parseSDPAttribute_control(sdpLine)) continue;
      if (subsession->parseSDPAttribute_range(sdpLine)) continue;
      if (subsession->parseSDPAttribute_fmtp(sdpLine)) continue;
      if (subsession->parseSDPAttribute_x_mct_slap(sdpLine)) continue;
      if (subsession->parseSDPAttribute_x_dimensions(sdpLine)) continue;
      if (subsession->parseSDPAttribute_x_framerate(sdpLine)) continue;

      // (Later, check for malformed lines, and other valid SDP lines#####)
    }
    if (sdpLine != NULL) subsession->fSavedSDPLines[sdpLine-mStart] = '\0';

    // If we don't yet know the codec name, try looking it up from the
    // list of static payload types:
    if (subsession->fCodecName == NULL) {
      subsession->fCodecName
	= lookupPayloadFormat(subsession->fRTPPayloadFormat,
			      subsession->fRTPTimestampFrequency);
      if (subsession->fCodecName == NULL) {
	char typeStr[20];
	sprintf(typeStr, "%d", subsession->fRTPPayloadFormat);
	envir().setResultMsg("Unknown codec name for RTP payload type ",
			     typeStr);
	return False;
      }
    }

    // If we don't yet know this subsession's RTP timestamp frequency
    // (because it uses a dynamic payload type and the corresponding
    // SDP "rtpmap" attribute erroneously didn't specify it),
    // then guess it now:
    if (subsession->fRTPTimestampFrequency == 0) {
      subsession->fRTPTimestampFrequency
	= guessRTPTimestampFrequency(subsession->fMediumName,
				     subsession->fCodecName);
    }
  }

  return True;
}

Boolean MediaSession::parseSDPLine(char const* inputLine,
				   char const*& nextLine){
  // Begin by finding the start of the next line (if any):
  nextLine = NULL;
  for (char const* ptr = inputLine; *ptr != '\0'; ++ptr) {
    if (*ptr == '\r' || *ptr == '\n') {
      // We found the end of the line
      ++ptr;
      while (*ptr == '\r' || *ptr == '\n') ++ptr;
      nextLine = ptr;
      if (nextLine[0] == '\0') nextLine = NULL; // special case for end
      break;
    }
  }

  // Then, check that this line is a SDP line of the form <char>=<etc>
  if (strlen(inputLine) < 2 || inputLine[1] != '='
      || inputLine[0] < 'a' || inputLine[0] > 'z') {
    envir().setResultMsg("Invalid SDP line: ", inputLine);
    return False;
  }

  return True;
}

static char* parseCLine(char const* sdpLine) {
  char* resultStr = NULL;
  char* buffer = strdup(sdpLine); // ensures we have enough space
  if (sscanf(sdpLine, "c=IN IP4 %[^/ ]", buffer) == 1) {
    // Later, handle the optional /<ttl> and /<numAddresses> #####
    resultStr = strdup(buffer);
  }
  delete buffer;

  return resultStr;
}

Boolean MediaSession::parseSDPLine_c(char const* sdpLine) {
  // Check for "c=IN IP4 <connection-endpoint>"
  // or "c=IN IP4 <connection-endpoint>/<ttl+numAddresses>"
  // (Later, do something with <ttl+numAddresses> also #####)
  char* connectionEndpointName = parseCLine(sdpLine);
  if (connectionEndpointName != NULL) {
    delete fConnectionEndpointName;
    fConnectionEndpointName = connectionEndpointName;
    return True;
  }

  return False;
}

static Boolean parseRangeAttribute(char const* sdpLine, float& endTime) {
  return sscanf(sdpLine, "a=range: npt = %*g - %g", &endTime) == 1;
}

Boolean MediaSession::parseSDPAttribute_range(char const* sdpLine) {
  // Check for a "a=range:npt=<startTime>-<endTime>" line:
  // (Later handle other kinds of "a=range" attributes also???#####)
  Boolean parseSuccess = False;

  float playEndTime;
  if (parseRangeAttribute(sdpLine, playEndTime)) {
    parseSuccess = True;
    if (playEndTime > fMaxPlayEndTime) {
      fMaxPlayEndTime = playEndTime;
    }
  }

  return parseSuccess;
}

char* MediaSession::lookupPayloadFormat(unsigned char rtpPayloadType,
					unsigned& rtpTimestampFrequency) {
  // Look up the codec name and timestamp frequency for known (static)
  // RTP payload formats.
  char* temp = NULL;
  switch (rtpPayloadType) {
  case 0: {temp = "PCMU"; rtpTimestampFrequency = 8000; break;}
  case 2: {temp = "G726-32"; rtpTimestampFrequency = 8000; break;}
  case 3: {temp = "GSM"; rtpTimestampFrequency = 8000; break;}
  case 4: {temp = "G723"; rtpTimestampFrequency = 8000; break;}
  case 5: {temp = "DVI4"; rtpTimestampFrequency = 8000; break;}
  case 6: {temp = "DVI4"; rtpTimestampFrequency = 16000; break;}
  case 7: {temp = "LPC"; rtpTimestampFrequency = 8000; break;}
  case 8: {temp = "PCMA"; rtpTimestampFrequency = 8000; break;}
  case 9: {temp = "G722"; rtpTimestampFrequency = 8000; break;}
  case 10: {temp = "L16"; rtpTimestampFrequency = 44100; break;}
  case 11: {temp = "L16"; rtpTimestampFrequency = 44100; break;}
  case 12: {temp = "QCELP"; rtpTimestampFrequency = 8000; break;}
  case 14: {temp = "MPA"; rtpTimestampFrequency = 90000; break;}
  case 15: {temp = "G728"; rtpTimestampFrequency = 8000; break;}
  case 16: {temp = "DVI4"; rtpTimestampFrequency = 11025; break;}
  case 17: {temp = "DVI4"; rtpTimestampFrequency = 22050; break;}
  case 18: {temp = "G729"; rtpTimestampFrequency = 8000; break;}
  case 25: {temp = "CELB"; rtpTimestampFrequency = 90000; break;}
  case 26: {temp = "JPEG"; rtpTimestampFrequency = 90000; break;}
  case 28: {temp = "NV"; rtpTimestampFrequency = 90000; break;}
  case 31: {temp = "H261"; rtpTimestampFrequency = 90000; break;}
  case 32: {temp = "MPV"; rtpTimestampFrequency = 90000; break;}
  case 33: {temp = "MP2T"; rtpTimestampFrequency = 90000; break;}
  case 34: {temp = "H263"; rtpTimestampFrequency = 90000; break;}
  };

  if (temp != NULL) return strdup(temp);
  else return NULL;
}

unsigned MediaSession::guessRTPTimestampFrequency(char const* mediumName,
						  char const* codecName) {
  // By default, we assume that audio sessions use a frequency of 8000,
  // and that video sessions use a frequency of 90000.
  // Begin by checking for known exceptions to this rule
  // (where the frequency is known unambiguously (e.g., not like "DVI4"))
  if (strcmp(codecName, "L16") == 0) return 44100;
  if (strcmp(codecName, "MPA") == 0
      || strcmp(codecName, "MPA-ROBUST") == 0
      || strcmp(codecName, "X-MP3-DRAFT-00")) return 90000;

  // Now, guess default values:
  if (strcmp(mediumName, "video") == 0) return 90000;
  return 8000; // for "audio", and any other medium
}

static unsigned computeSeqNumStagger(unsigned staggerSeconds) {
  // To compute the sequence number stagger, assume
  // - one MP3 frame per packet
  // - 1152 samples per MP3 frame (defined by MP3 standard)
  // - a sampling frequency of 44100 Hz
  // (Later, allow this to be parameterized)#####
  unsigned const samplesPerFrame = 1152;
  unsigned samplesPerSecond = 44100;
  double secondsPerFrame = (double)samplesPerFrame/samplesPerSecond;
  return (unsigned)(staggerSeconds/secondsPerFrame);
}

Boolean MediaSession
::initiateByMediaType(char const* mimeType,
		      MediaSubsession*& resultSubsession,
		      PrioritizedRTPStreamSelector*& resultMultiSource,
		      int& resultMultiSourceSessionId,
		      int useSpecialRTPoffset) {
  // Look through this session's subsessions for media that match "mimeType"
  resultSubsession = NULL;
  resultMultiSource = NULL;
  resultMultiSourceSessionId = 0;
  unsigned maxStaggerSeconds = 0;
  MediaSubsessionIterator iter(*this);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    if (resultMultiSourceSessionId != 0
	&& subsession->mctSLAPSessionId() != resultMultiSourceSessionId) {
      // We're using a multi-source SLAP session, but this subsession
      // isn't part of it
      continue;
    }

    // Try to create a source for this subsession:
    if (!subsession->initiate(useSpecialRTPoffset)) return False;

    // Make sure the source's MIME type is one that we handle:
    if (strcmp(subsession->readSource()->MIMEtype(), mimeType) != 0) {
      subsession->deInitiate();
      continue;
    }

    if (subsession->mctSLAPSessionId() == 0) {
      // Normal case: a single session
      resultSubsession = subsession;
      break; // use this
    } else {
      // Special case: a multi-source SLAP session
      resultMultiSourceSessionId = subsession->mctSLAPSessionId();
      unsigned subsessionStaggerSeconds = subsession->mctSLAPStagger();
      if (subsessionStaggerSeconds > maxStaggerSeconds) {
	maxStaggerSeconds = subsessionStaggerSeconds;
      }
    }
  }

  if (resultSubsession == NULL && resultMultiSourceSessionId == 0) {
    envir().setResultMsg("Session has no usable media subsession");
    return False;
  }

  if (resultMultiSourceSessionId != 0) {
    // We have a multi-source MCT SLAP session; create a selector for it:
    unsigned seqNumStagger = computeSeqNumStagger(maxStaggerSeconds);
    resultMultiSource
      = PrioritizedRTPStreamSelector::createNew(envir(), seqNumStagger);
    if (resultMultiSource == NULL) return False;
    // Note: each subsession has its own RTCP instance; we don't return them

    // Then run through the subsessions again, adding each of the sources:
    iter.reset();
    while ((subsession = iter.next()) != NULL) {
      if (subsession->mctSLAPSessionId() == resultMultiSourceSessionId) {
	resultMultiSource->addInputRTPStream(subsession->rtpSource(),
					     subsession->rtcpInstance());
      }
    }
  }

  return True;
}


////////// MediaSubsessionIterator //////////

MediaSubsessionIterator::MediaSubsessionIterator(MediaSession& session)
  : fOurSession(session) {
  reset();
}

MediaSubsessionIterator::~MediaSubsessionIterator() {
}

MediaSubsession* MediaSubsessionIterator::next() {
  MediaSubsession* result = fNextPtr;

  if (fNextPtr != NULL) fNextPtr = fNextPtr->fNext;

  return result;
}

void MediaSubsessionIterator::reset() {
  fNextPtr = fOurSession.fSubsessionsHead;
}

////////// MediaSubsession //////////

MediaSubsession::MediaSubsession(MediaSession& parent)
  : sessionId(NULL), serverPortNum(0), sink(NULL), miscPtr(NULL),
    fParent(parent), fNext(NULL),
    fConnectionEndpointName(NULL),
    fClientPortNum(0), fRTPPayloadFormat(0xFF),
    fSavedSDPLines(NULL), fMediumName(NULL), fCodecName(NULL),
    fRTPTimestampFrequency(0), fControlPath(NULL),
    fCpresent(False), fConfig(NULL), fPlayEndTime(0.0),
    fMCT_SLAP_SessionId(0), fMCT_SLAP_Stagger(0),
    fVideoWidth(0), fVideoHeight(0), fVideoFPS(0), fNumChannels(1),
    fRTPSocket(NULL), fRTCPSocket(NULL),
    fRTPSource(NULL), fRTCPInstance(NULL), fReadSource(NULL) {
}

MediaSubsession::~MediaSubsession() {
  deInitiate();

  delete fConnectionEndpointName;
  delete fSavedSDPLines; delete fMediumName; delete fCodecName;
  delete fControlPath; delete fConfig;

  delete fNext;
}

// Eventually make this a general library routine #####
static Boolean getPortNumber(UsageEnvironment& env, int sock,
			     unsigned short& resultPortNum /*host order*/) {
  Port clientPort(0);
  if (!getSourcePort(env, sock, clientPort) || clientPort.num() == 0) {
    // Hack - call bind(), then try again:
    struct sockaddr_in name;
    name.sin_family = AF_INET;
    name.sin_port = 0;
    name.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (struct sockaddr*)&name, sizeof name);
    if (!getSourcePort(env, sock, clientPort) || clientPort.num() == 0) {
      return False;
    }
  }

  resultPortNum = ntohs(clientPort.num());
  return True;
}

float MediaSubsession::playEndTime() const {
  if (fPlayEndTime > 0) return fPlayEndTime;

  return fParent.playEndTime();
}

Boolean MediaSubsession::initiate(int useSpecialRTPoffset) {
  if (fReadSource != NULL) return True; // has already been initiated
  
  do {
    if (fCodecName == NULL) {
      env().setResultMsg("Codec is unspecified");
      break;
    }

    // Create RTP and RTCP 'Groupsocks' on which to receive incoming data.
    // (Groupsocks will work even for unicast addresses)
    Groupsock* oldGroupsock = NULL;
    Boolean success = False;
    struct in_addr tempAddr;
    tempAddr.s_addr = connectionEndpointAddress();
        // This could get changed later, as a result of a RTSP "SETUP"
    while (1) {
      unsigned short rtpPortNum = fClientPortNum&~1;
      fRTPSocket = new Groupsock(env(), tempAddr, rtpPortNum, 255);
      if (fRTPSocket == NULL) {
	env().setResultMsg("Failed to create RTP socket");
	break;
      }

      // Get the client port number, to make sure that it's even (for RTP):
      if (!getPortNumber(env(), fRTPSocket->socketNum(), fClientPortNum)) {
	break;
      }

      // If the port number's not even, try again:
      if ((fClientPortNum&1) == 0) {
	success = True;
	break;
      }
      // Try again:
      delete oldGroupsock;
      oldGroupsock = fRTPSocket;
      fClientPortNum = 0;
    }
    delete oldGroupsock;
    if (!success) break;

    // Set our RTCP port to be the RTP port +1
    unsigned short const rtcpPortNum = fClientPortNum + 1;
    fRTCPSocket = new Groupsock(env(), tempAddr, rtcpPortNum, 255);
    if (fRTCPSocket == NULL) {
      char tmpBuf[100];
      sprintf(tmpBuf, "Failed to create RTCP socket (port %d)",
	      rtcpPortNum);
      env().setResultMsg(tmpBuf);
      break;
    }

    // Check "fCodecName" against the set of codecs that we support,
    // and create our RTP source accordingly
    // (Later make this code more efficient, as this set grows #####)
    // (Also, add more fmts that can be implemented by SimpleRTPSource#####)
    Boolean createSimpleRTPSource = False;
    if (strcmp(fCodecName, "QCELP") == 0) { // QCELP audio
      fReadSource =
	QCELPAudioRTPSource::createNew(env(), fRTPSocket, fRTPSource,
				       fRTPPayloadFormat,
				       fRTPTimestampFrequency);
          // Note that fReadSource will differ from fRTPSource in this case
    } else if (strcmp(fCodecName, "MPA") == 0) { // MPEG-1 or 2 audio
      fReadSource = fRTPSource
	= MPEGAudioRTPSource::createNew(env(), fRTPSocket,
					fRTPPayloadFormat,
					fRTPTimestampFrequency);
    } else if (strcmp(fCodecName, "MPA-ROBUST") == 0) { // robust MP3 audio
      fRTPSource
	= MP3ADURTPSource::createNew(env(), fRTPSocket, fRTPPayloadFormat,
				     fRTPTimestampFrequency);
      if (fRTPSource == NULL) break;

      // Add a filter that deinterleaves the ADUs after depacketizing them:
      MP3ADUdeinterleaver* deinterleaver
	= MP3ADUdeinterleaver::createNew(env(), fRTPSource);
      if (deinterleaver == NULL) break;

      // Add another filter that converts these ADUs to MP3 frames:
      fReadSource = MP3FromADUSource::createNew(env(), deinterleaver);
    } else if (strcmp(fCodecName, "X-MP3-DRAFT-00") == 0) {
      // a non-standard variant of "MPA-ROBUST" used by RealNetworks
      // (one 'ADU'ized MP3 frame per packet; no headers)
      fRTPSource
	= SimpleRTPSource::createNew(env(), fRTPSocket, fRTPPayloadFormat,
				     90000, "audio/mpa-robust" /*hack*/);
      if (fRTPSource == NULL) break;

      // Add a filter that converts these ADUs to MP3 frames:
      fReadSource = MP3FromADUSource::createNew(env(), fRTPSource,
						False /*no ADU header*/);
    } else if (strcmp(fCodecName, "MP4A-LATM") == 0) { // MPEG-4 LATM audio
      fReadSource = fRTPSource
	= MPEG4LATMAudioRTPSource::createNew(env(), fRTPSocket,
					     fRTPPayloadFormat,
					     fRTPTimestampFrequency);
    } else if (strcmp(fCodecName, "MP4V-ES") == 0) { // MPEG-4 Elem Str vid
      fReadSource = fRTPSource
	= MPEG4ESVideoRTPSource::createNew(env(), fRTPSocket,
					   fRTPPayloadFormat,
					   fRTPTimestampFrequency);
    } else if (strcmp(fCodecName, "MPEG4-GENERIC") == 0) {
      fReadSource = fRTPSource
	= MPEG4GenericRTPSource::createNew(env(), fRTPSocket,
					   fRTPPayloadFormat,
					   fRTPTimestampFrequency,
					   fMediumName);
    } else if (strcmp(fCodecName, "MPV") == 0) { // MPEG-1 or 2 video
      fReadSource = fRTPSource
	= MPEGVideoRTPSource::createNew(env(), fRTPSocket,
					fRTPPayloadFormat,
					fRTPTimestampFrequency);
    } else if (strcmp(fCodecName, "H263-1998") == 0) { // H.263+
      fReadSource = fRTPSource
	= H263plusVideoRTPSource::createNew(env(), fRTPSocket,
					    fRTPPayloadFormat,
					    fRTPTimestampFrequency);
    } else if (strcmp(fCodecName, "JPEG") == 0) { // motion JPEG
      fReadSource = fRTPSource
	= JPEGVideoRTPSource::createNew(env(), fRTPSocket,
					fRTPPayloadFormat,
					fRTPTimestampFrequency);
    } else if (strcmp(fCodecName, "X-QT") == 0
	       || strcmp(fCodecName, "X-QUICKTIME") == 0) {
      // Generic QuickTime streams, as defined in
      // <http://developer.apple.com/quicktime/icefloe/dispatch026.html>
      char* mimeType
	= new char[strlen(mediumName()) + strlen(codecName()) + 2] ;
      sprintf(mimeType, "%s/%s", mediumName(), codecName());
      fReadSource = fRTPSource
	= QuickTimeGenericRTPSource::createNew(env(), fRTPSocket,
					       fRTPPayloadFormat,
					       fRTPTimestampFrequency,
					       mimeType);
      delete mimeType;
    } else if (strcmp(fCodecName, "X-MCT-TEXT") == 0) {
      // A UDP-packetized text stream (*not* a RTP stream)
      fReadSource = BasicUDPSource::createNew(env(), fRTPSocket);
      fRTPSource = NULL; // Note!
    } else if (  strcmp(fCodecName, "PCMU") == 0 // PCM u-law audio
	       || strcmp(fCodecName, "GSM") == 0 // GSM audio
	       || strcmp(fCodecName, "PCMA") == 0 // PCM a-law audio
	       || strcmp(fCodecName, "MP1S") == 0 // MPEG-1 System Stream
	       || strcmp(fCodecName, "MP2T") == 0 // MPEG-2 Transport Str
	       || strcmp(fCodecName, "MP2P") == 0 // MPEG-2 Program Stream
	      ) {
      createSimpleRTPSource = True;
      useSpecialRTPoffset = 0;
    } else if (useSpecialRTPoffset >= 0) {
      // We don't know this RTP payload format, but try to receive
      // it using a 'SimpleRTPSource' with the specified header offset:
      createSimpleRTPSource = True;
    } else {
      env().setResultMsg("RTP payload format unknown or not supported");
      break;
    }

    if (createSimpleRTPSource) {
      char* mimeType
	= new char[strlen(mediumName()) + strlen(codecName()) + 2] ;
      sprintf(mimeType, "%s/%s", mediumName(), codecName());
      fReadSource = fRTPSource
	= SimpleRTPSource::createNew(env(), fRTPSocket, fRTPPayloadFormat,
				     fRTPTimestampFrequency, mimeType,
				     (unsigned)useSpecialRTPoffset);
      delete mimeType;
    }

    if (fReadSource == NULL) {
      env().setResultMsg("Failed to create read source");
      break;
    }
    
    // Finally, create our RTCP instance. (It starts running automatically)
    if (fRTPSource != NULL) {
      unsigned totSessionBandwidth = 500; // HACK - later get from SDP#####
      fRTCPInstance = RTCPInstance::createNew(env(), fRTCPSocket,
					      totSessionBandwidth,
					      (unsigned char const*)
					      fParent.CNAME(),
					      NULL /* we're a client */,
					      fRTPSource);
      if (fRTCPInstance == NULL) {
	env().setResultMsg("Failed to create RTCP instance");
	break;
      }
    }

    return True;
  } while (0);

  delete fRTPSocket; fRTPSocket = NULL;
  delete fRTCPSocket; fRTCPSocket = NULL;
  Medium::close(fRTCPInstance); fRTCPInstance = NULL;
  Medium::close(fReadSource); fReadSource = fRTPSource = NULL;
  fClientPortNum = 0;
  return False;
}

void MediaSubsession::deInitiate() {
  Medium::close(fRTCPInstance);
  fRTCPInstance = NULL;

  Medium::close(fReadSource); // this is assumed to also close fRTPSource
  fReadSource = NULL; fRTPSource = NULL;

  delete fRTCPSocket; delete fRTPSocket;
  fRTCPSocket = fRTPSocket = NULL;
}

Boolean MediaSubsession::setClientPortNum(unsigned short portNum) {
  if (fReadSource != NULL) {
    env().setResultMsg("A read source has already been created");
    return False;
  }

  fClientPortNum = portNum;
  return True;
}

unsigned MediaSubsession::connectionEndpointAddress() const {
  do {
    // Get the endpoint name from with us, or our parent session:
    char const* endpointString = connectionEndpointName();
    if (endpointString == NULL) {
      endpointString = parentSession().connectionEndpointName();
    }
    if (endpointString == NULL) break;

    // Now, convert this name to an address, if we can:
    NetAddressList addresses(endpointString);
    if (addresses.numAddresses() == 0) break;

    return *(unsigned*)(addresses.firstAddress()->data());
  } while (0);

  // No address known:
  return 0;
}

void MediaSubsession::setDestinations(unsigned defaultDestAddress) {
  // Get the destination address from the connection endpoint name
  // (This will be 0 if it's not known, in which case we use the default)
  unsigned destAddress = connectionEndpointAddress();
  if (destAddress == 0) destAddress = defaultDestAddress;
  struct in_addr destAddr; destAddr.s_addr = destAddress;

  // The destination TTL remains unchanged:
  int destTTL = ~0; // means: don't change

  if (fRTPSocket != NULL) {
    Port destPort(serverPortNum);
    fRTPSocket->changeDestinationParameters(destAddr, destPort, destTTL);
  }
  if (fRTCPSocket != NULL) {
    Port destPort(serverPortNum+1);
    fRTCPSocket->
      changeDestinationParameters(destAddr, destPort, destTTL);
  }
}

Boolean MediaSubsession::parseSDPLine_c(char const* sdpLine) {
  // Check for "c=IN IP4 <connection-endpoint>"
  // or "c=IN IP4 <connection-endpoint>/<ttl+numAddresses>"
  // (Later, do something with <ttl+numAddresses> also #####)
  char* connectionEndpointName = parseCLine(sdpLine);
  if (connectionEndpointName != NULL) {
    delete fConnectionEndpointName;
    fConnectionEndpointName = connectionEndpointName;
    return True;
  }

  return False;
}

Boolean MediaSubsession::parseSDPAttribute_rtpmap(char const* sdpLine) {
  // Check for a "a=rtpmap:<fmt> <codec>/<freq>" line:
  // (Also check without the "/<freq>"; RealNetworks omits this)
  // Also check for a trailing "/<numChannels>".
  Boolean parseSuccess = False;

  unsigned rtpmapPayloadFormat;
  char* codecName = strdup(sdpLine); // ensures we have enough space
  unsigned rtpTimestampFrequency = 0;
  unsigned numChannels = 1;
  if (sscanf(sdpLine, "a=rtpmap: %u %[^/]/%u/%u",
	     &rtpmapPayloadFormat, codecName, &rtpTimestampFrequency,
	     &numChannels) == 4
      || sscanf(sdpLine, "a=rtpmap: %u %[^/]/%u",
	     &rtpmapPayloadFormat, codecName, &rtpTimestampFrequency) == 3
      || sscanf(sdpLine, "a=rtpmap: %u %s",
		&rtpmapPayloadFormat, codecName) == 2) {
    parseSuccess = True;
    if (rtpmapPayloadFormat == fRTPPayloadFormat) {
      // This "rtpmap" matches our payload format, so set our
      // codec name and timestamp frequency:
      // (First, make sure the codec name is upper case)
      for (char* p = codecName; *p != '\0'; ++p) {
	*p = toupper(*p);
      }
      delete fCodecName; fCodecName = strdup(codecName);
      fRTPTimestampFrequency = rtpTimestampFrequency;
      fNumChannels = numChannels;
    }
  }
  delete codecName;

  return parseSuccess;
}

Boolean MediaSubsession::parseSDPAttribute_control(char const* sdpLine) {
  // Check for a "a=control:<control-path>" line:
  Boolean parseSuccess = False;

  char* controlPath = strdup(sdpLine); // ensures we have enough space
  if (sscanf(sdpLine, "a=control: %s", controlPath) == 1) {
    parseSuccess = True;
    delete fControlPath; fControlPath = strdup(controlPath);
  }
  delete controlPath;

  return parseSuccess;
}

Boolean MediaSubsession::parseSDPAttribute_range(char const* sdpLine) {
  // Check for a "a=range:npt=<startTime>-<endTime>" line:
  // (Later handle other kinds of "a=range" attributes also???#####)
  Boolean parseSuccess = False;

  float playEndTime;
  if (parseRangeAttribute(sdpLine, playEndTime)) {
    parseSuccess = True;
    if (playEndTime > fPlayEndTime) {
      fPlayEndTime = playEndTime;
      if (playEndTime > fParent.playEndTime()) {
	fParent.playEndTime() = playEndTime;
      }
    }
  }

  return parseSuccess;
}

Boolean MediaSubsession::parseSDPAttribute_fmtp(char const* sdpLine) {
  // Check for a "a=fmtp:" line:
  // TEMP: We check only for "cpresent=" and "config=" #####
  // Later: (i) check that payload format number matches; #####
  //        (ii) look for other parameters also (generalize?) #####  
  do {
    if (strncmp(sdpLine, "a=fmtp:", 7) != 0) break; sdpLine += 7;
    while (isdigit(*sdpLine)) ++sdpLine;

    // The remaining "sdpLine" should be a sequence of
    //     <name>=<value>;
    // parameter assignments.  Look at each of these:
    char* valueStr = strdup(sdpLine);
    while (*sdpLine != '\0' && *sdpLine != '\r' && *sdpLine != '\n') {
      int cpresent;
      if (sscanf(sdpLine, " cpresent = %d", &cpresent) == 1) {
	fCpresent = cpresent != 0;
      } else if (sscanf(sdpLine, " config = %[^; \t\r\n]", valueStr) == 1) {
	delete fConfig; fConfig = strdup(valueStr);
      }

      // Move to the next parameter assignment string:
      while (*sdpLine != '\0' && *sdpLine != '\r' && *sdpLine != '\n'
	     && *sdpLine != ';') ++sdpLine;
      while (*sdpLine == ';') ++sdpLine;
    }
    return True;
  } while (0);

  return False;
}

Boolean MediaSubsession::parseSDPAttribute_x_mct_slap(char const* sdpLine) {
  // Check for a "a=x-mct-slap:<slap-session-id> <stagger>" line:
  Boolean parseSuccess = False;

  int slapSessionId;
  int slapStagger;
  if (sscanf(sdpLine, "a=x-mct-slap: %d %d",
	     &slapSessionId, &slapStagger) == 2) {
    parseSuccess = True;
    fMCT_SLAP_SessionId = slapSessionId;
    fMCT_SLAP_Stagger = (unsigned)slapStagger;
  }

  return parseSuccess;
}

Boolean MediaSubsession::parseSDPAttribute_x_dimensions(char const* sdpLine) {
  // Check for a "a=x-dimensions:<width>,<height>" line:
  Boolean parseSuccess = False;

  int width, height;
  if (sscanf(sdpLine, "a=x-dimensions:%d,%d", &width, &height) == 2) {
    parseSuccess = True;
    fVideoWidth = (unsigned short)width;
    fVideoHeight = (unsigned short)height;
  }

  return parseSuccess;
}

Boolean MediaSubsession::parseSDPAttribute_x_framerate(char const* sdpLine) {
  // Check for a "a=x-framerate:<fps>" line:
  Boolean parseSuccess = False;

  int rate;
  if (sscanf(sdpLine, "a=x-framerate:%d", &rate) == 1) {
    parseSuccess = True;
    fVideoFPS = (unsigned)rate;
  }

  return parseSuccess;
}
