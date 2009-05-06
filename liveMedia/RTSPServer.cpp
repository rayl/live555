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
// A RTSP server
// Implementation

#include "RTSPServer.hh"
#include <GroupsockHelper.hh>

////////// RTSPServer //////////

RTSPServer*
RTSPServer::createNew(UsageEnvironment& env, Port ourPort) {
  int ourSocket = -1;
  RTSPServer* newServer = NULL;

  do {
    int ourSocket = setUpOurSocket(env, ourPort);
    if (ourSocket == -1) break;

    return new RTSPServer(env, ourSocket, ourPort);
  } while (0);

  if (ourSocket != -1) ::_close(ourSocket);
  delete newServer;
  return NULL;
}

Boolean RTSPServer::lookupByName(UsageEnvironment& env,
				 char const* instanceName,
				 RTSPServer*& resultServer) {
  resultServer = NULL; // unless we succeed

  Medium* medium;
  if (!Medium::lookupByName(env, instanceName, medium)) return False;

  if (!medium->isRTSPServer()) {
    env.setResultMsg(instanceName, " is not a RTSP server");
    return False;
  }

  resultServer = (RTSPServer*)medium;
  return True;
}

void RTSPServer
::addServerMediaSession(ServerMediaSession* serverMediaSession) {
  char const* sessionName = serverMediaSession->streamName();
  if (sessionName == NULL) sessionName = "";
  ServerMediaSession* existingSession
    = (ServerMediaSession*)
    (fServerMediaSessions->Add(sessionName,
			       (void*)serverMediaSession));
  delete existingSession; // if any
}

char* RTSPServer
::rtspURL(ServerMediaSession const* serverMediaSession) const {
  struct in_addr ourAddress;
  ourAddress.s_addr = ourSourceAddressForMulticast(envir()); // hack

  char const* sessionName = serverMediaSession->streamName();
  unsigned sessionNameLength = strlen(sessionName);

  char* urlBuffer = new char[100 + sessionNameLength];
  char* resultURL;

  portNumBits portNumHostOrder = ntohs(fServerPort.num());
  if (portNumHostOrder == 554 /* the default port number */) {
    sprintf(urlBuffer, "rtsp://%s/%s", our_inet_ntoa(ourAddress),
	    sessionName);
  } else {
    sprintf(urlBuffer, "rtsp://%s:%hu/%s",
	    our_inet_ntoa(ourAddress), portNumHostOrder,
	    sessionName);
  }

  resultURL = strDup(urlBuffer);
  delete urlBuffer;
  return resultURL;
}

#define LISTEN_BACKLOG_SIZE 20

int RTSPServer::setUpOurSocket(UsageEnvironment& env, Port& ourPort) {
  int ourSocket = -1;

  do {
    ourSocket = setupStreamSocket(env, ourPort);
    if (ourSocket < 0) break;

    // Make sure we have a big send buffer:
    if (!increaseSendBufferTo(env, ourSocket, 50*1024)) break;

    // Allow multiple simultaneous connections:
    if (listen(ourSocket, LISTEN_BACKLOG_SIZE) < 0) {
      env.setResultErrMsg("listen() failed: ");
      break;
    }

    if (ourPort.num() == 0) {
      // bind() will have chosen a port for us; return it also:
      if (!getSourcePort(env, ourSocket, ourPort)) break;
    }

    return ourSocket;
  } while (0);  

  if (ourSocket != -1) ::_close(ourSocket);
  return -1;
}

RTSPServer::RTSPServer(UsageEnvironment& env,
		       int ourSocket, Port ourPort)
  : Medium(env),
    fServerSocket(ourSocket), fServerPort(ourPort),
    fServerMediaSessions(HashTable::create(STRING_HASH_KEYS)), 
    fSessionIdCounter(0) {
  // Arrange to handle connections from others:
  env.taskScheduler().turnOnBackgroundReadHandling(fServerSocket,
        (TaskScheduler::BackgroundHandlerProc*)&incomingConnectionHandler,
						   this);
}

RTSPServer::~RTSPServer() {
  // Turn off background read handling:
  envir().taskScheduler().turnOffBackgroundReadHandling(fServerSocket);

  ::_close(fServerSocket);

  // Delete all server media sessions:
  while (1) {
    ServerMediaSession* namedSession
      = (ServerMediaSession*)fServerMediaSessions->RemoveNext();
    if (namedSession == NULL) break;
    delete namedSession;
  }

  // Finally, delete the session table itself:
  delete fServerMediaSessions;
}

Boolean RTSPServer::isRTSPServer() const {
  return True;
}

void RTSPServer::incomingConnectionHandler(void* instance, int /*mask*/) {
  RTSPServer* server = (RTSPServer*)instance;
  server->incomingConnectionHandler1();
}

#define PARAM_STRING_MAX 100

void RTSPServer::incomingConnectionHandler1() {
  struct sockaddr_in clientAddr;
  SOCKLEN_T clientAddrLen = sizeof clientAddr;
  int clientSocket = accept(fServerSocket, (struct sockaddr*)&clientAddr,
			    &clientAddrLen);
  if (clientSocket < 0) {
    int err = envir().getErrno();
    if (err != EWOULDBLOCK) {
        envir().setResultErrMsg("accept() failed: ");
    }
    return;
  }

  // Create a new object for this RTSP session:
  // (Later, we need to do some garbage collection on sessions that #####
  //  aren't closed down via TEARDOWN) #####
  new RTSPClientSession(*this, ++fSessionIdCounter,
			clientSocket, clientAddr);
}


////////// RTSPServer::RTSPClientSession //////////

RTSPServer::RTSPClientSession
::RTSPClientSession(RTSPServer& ourServer, unsigned sessionId,
	      int clientSocket, struct sockaddr_in clientAddr)
  : fOurServer(ourServer), fOurSessionId(sessionId),
    fOurServerMediaSession(NULL),
    fClientSocket(clientSocket), fClientAddr(clientAddr),
    fSessionIsActive(True), fNumStreamStates(0), fStreamStates(NULL) {
  // Arrange to handle incoming requests:
  envir().taskScheduler().turnOnBackgroundReadHandling(fClientSocket,
     (TaskScheduler::BackgroundHandlerProc*)&incomingRequestHandler,
						   this);
}

RTSPServer::RTSPClientSession::~RTSPClientSession() {
  // Turn off background read handling:
  envir().taskScheduler().turnOffBackgroundReadHandling(fClientSocket);

  ::_close(fClientSocket);

  reclaimStreamStates();
}

void RTSPServer::RTSPClientSession::reclaimStreamStates() {
  for (unsigned i = 0; i < fNumStreamStates; ++i) {
    if (fStreamStates[i].subsession != NULL) {
      fStreamStates[i].subsession->deleteStream(fStreamStates[i].streamToken);
    }
  }
  delete[] fStreamStates; fStreamStates = NULL;
  fNumStreamStates = 0;
}

void RTSPServer::RTSPClientSession
::incomingRequestHandler(void* instance, int /*mask*/) {
  RTSPClientSession* session = (RTSPClientSession*)instance;
  session->incomingRequestHandler1();
}

void RTSPServer::RTSPClientSession::incomingRequestHandler1() {
  struct sockaddr_in dummy; // 'from' address, meaningless in this case
  int bytesRead = readSocket(envir(), fClientSocket,
			     fBuffer, sizeof fBuffer, dummy);
#ifdef DEBUG
  fprintf(stderr, "RTSPClientSession[%p]::incomingRequestHandler1() read %d bytes:%s\n", this, bytesRead, fBuffer);
#endif
  if (bytesRead <= 0) {
    // The client socket has apparently died - kill it:
    delete this;
    return;
  }
  fBuffer[bytesRead] = '\0';

  // Parse the request string into command name and 'CSeq',
  // then handle the command:
  char cmdName[PARAM_STRING_MAX];
  char urlPreSuffix[PARAM_STRING_MAX];
  char urlSuffix[PARAM_STRING_MAX];
  char cseq[PARAM_STRING_MAX];
  if (!parseRequestString((char*)fBuffer, bytesRead,
			  cmdName, sizeof cmdName,
			  urlPreSuffix, sizeof urlPreSuffix,
			  urlSuffix, sizeof urlSuffix,
			  cseq, sizeof cseq)) {
#ifdef DEBUG
    fprintf(stderr, "parseRequestString() failed!\n");
#endif
    handleCmd_bad(cseq);
  } else {
#ifdef DEBUG
    fprintf(stderr, "parseRequestString() returned cmdName \"%s\", urlPreSuffix \"%s\", urlSuffix \"%s\"\n", cmdName, urlPreSuffix, urlSuffix);
#endif
    if (strcmp(cmdName, "OPTIONS") == 0) {
      handleCmd_OPTIONS(cseq);
    } else if (strcmp(cmdName, "DESCRIBE") == 0) {
      handleCmd_DESCRIBE(cseq, urlSuffix);
    } else if (strcmp(cmdName, "SETUP") == 0) {
      handleCmd_SETUP(cseq, urlPreSuffix, urlSuffix, (char const*)fBuffer);
    } else if (strcmp(cmdName, "TEARDOWN") == 0
	       || strcmp(cmdName, "PLAY") == 0
	       || strcmp(cmdName, "PAUSE") == 0) {
      handleCmd_withinSession(cmdName, urlPreSuffix, urlSuffix, cseq);
    } else {
      handleCmd_notSupported(cseq);
    }
  }
    
#ifdef DEBUG
  fprintf(stderr, "sending response: %s", fBuffer);
#endif
  send(fClientSocket, (char const*)fBuffer, strlen((char*)fBuffer), 0);

  if (!fSessionIsActive) delete this;
}

// Handler routines for specific RTSP commands:

static char const* allowedCommandNames
  = "OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE";

void RTSPServer::RTSPClientSession::handleCmd_bad(char const* /*cseq*/) {
  // Don't do anything with "cseq", because it might be nonsense
  sprintf((char*)fBuffer, "RTSP/1.0 400 Bad Request\r\nAllow: %s\r\n\r\n",
	  allowedCommandNames);
  fSessionIsActive = False; // triggers deletion of ourself after responding
}

void RTSPServer::RTSPClientSession::handleCmd_notSupported(char const* cseq) {
  sprintf((char*)fBuffer, "RTSP/1.0 405 Method Not Allowed\r\nCSeq: %s\r\nAllow: %s\r\n\r\n",
	  cseq, allowedCommandNames);
  fSessionIsActive = False; // triggers deletion of ourself after responding
}

void RTSPServer::RTSPClientSession::handleCmd_notFound(char const* cseq) {
  sprintf((char*)fBuffer, "RTSP/1.0 404 Stream Not Found\r\nCSeq: %s\r\n\r\n", cseq);
  fSessionIsActive = False; // triggers deletion of ourself after responding
}

void RTSPServer::RTSPClientSession::handleCmd_OPTIONS(char const* cseq) {
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\nPublic: %s\r\n\r\n",
	  cseq, allowedCommandNames);
}

void RTSPServer::RTSPClientSession
::handleCmd_DESCRIBE(char const* cseq, char const* urlSuffix) {
  char* sdpDescription = NULL;
  char* rtspURL = NULL;
  do {
    // We should really check that the request contains an "Accept:" #####
    // for "application/sdp", because that's what we're sending back #####

    // Begin by looking up the "ServerMediaSession" object for the
    // specified "urlSuffix":
    ServerMediaSession* session
      = (ServerMediaSession*)(fOurServer.fServerMediaSessions->Lookup(urlSuffix));
    if (session == NULL) {
      handleCmd_notFound(cseq);
      break;
    }

    // Then, assemble a SDP description for this session:
    sdpDescription = session->generateSDPDescription();
    if (sdpDescription == NULL) {
      // This usually means that a file name that was specified for a
      // "ServerMediaSubsession" does not exist.
      sprintf((char*)fBuffer, "RTSP/1.0 404 File Not Found, Or In Incorrect Format\r\nCSeq: %s\r\n\r\n", cseq);
     break;
    }
    unsigned sdpDescriptionSize = strlen(sdpDescription);

    // Also, generate out RTSP URL, for the "Content-Base:" header
    // (which is necessary to ensure that the correct URL gets used in
    // subsequent "SETUP" requests).
    rtspURL = fOurServer.rtspURL(session);
    unsigned rtspURLSize = strlen(rtspURL); 

    if (sdpDescriptionSize + rtspURLSize > sizeof fBuffer - 200) { // sanity check
      sprintf((char*)fBuffer,
	      "RTSP/1.0 500 Internal Server Error\r\nCSeq: %s\r\n\r\n", cseq);
      break;
    }
  
    sprintf((char*)fBuffer,
	    "RTSP/1.0 200 OK\r\nCSeq: %s\r\n"
	    "Content-Base: %s/\r\n"
	    "Content-Type: application/sdp\r\n"
	    "Content-Length: %d\r\n\r\n"
	    "%s",
	    cseq,
	    rtspURL,
	    sdpDescriptionSize,
	    sdpDescription);
  } while (0);

  delete[] sdpDescription;
  delete[] rtspURL;
}

static void parseTransportHeader(char const* buf,
				 unsigned short& clientRTPPortNum,
				 unsigned short& clientRTCPPortNum) {
  // Note: This is a quick-and-dirty implementation.  Should improve #####
  while (*buf != '\0') {
    if (*buf == 'c') {
      // Check for "client_port="
      unsigned short p1, p2;
      if (sscanf(buf, "client_port=%hu-%hu", &p1, &p2) == 2) {
	clientRTPPortNum = p1;
	clientRTCPPortNum = p2;
	return;
      }
    }
    ++buf;
  }
}

void RTSPServer::RTSPClientSession
::handleCmd_SETUP(char const* cseq,
		  char const* urlPreSuffix, char const* urlSuffix,
		  char const* fullRequestStr) {
  // "urlPreSuffix" should be the session (stream) name, and
  // "urlSuffix" should be the subsession (track) name.

  // Check whether we have existing session state, and, if so, whether it's
  // for the session that's named in "urlPreSuffix".  (Note that we don't
  // support more than one concurrent session on the same client connection.) #####
  if (fOurServerMediaSession != NULL
      && strcmp(urlPreSuffix, fOurServerMediaSession->streamName()) != 0) {
    fOurServerMediaSession = NULL;
  }
  if (fOurServerMediaSession == NULL) {
    // Set up this session's state.

    // Look up the "ServerMediaSession" object for the specified "urlPreSuffix":
    fOurServerMediaSession = (ServerMediaSession*)
      (fOurServer.fServerMediaSessions->Lookup(urlPreSuffix));
    if (fOurServerMediaSession == NULL) {
      handleCmd_notFound(cseq);
      return;
    }

    // Set up our array of states for this session's subsessions (tracks):
    reclaimStreamStates();
    ServerMediaSubsessionIterator iter(*fOurServerMediaSession);
    for (fNumStreamStates = 0; iter.next() != NULL; ++fNumStreamStates) {}
    fStreamStates = new struct streamState[fNumStreamStates];
    iter.reset();
    ServerMediaSubsession* subsession;
    for (unsigned i = 0; i < fNumStreamStates; ++i) {
      subsession = iter.next();
      fStreamStates[i].subsession = subsession;
      fStreamStates[i].streamToken = NULL; // for now; reset by SETUP later
    }
  }

  // Look up information for the subsession (track) named "urlSuffix":
  ServerMediaSubsession* subsession;
  unsigned streamNum;
  for (streamNum = 0; streamNum < fNumStreamStates; ++streamNum) {
    subsession = fStreamStates[streamNum].subsession;
    if (subsession != NULL && strcmp(urlSuffix, subsession->trackId()) == 0) break;
  }
  if (streamNum >= fNumStreamStates) {
    // The specified track id doesn't exist, so this request fails:
    handleCmd_notFound(cseq);
    return;
  }
  // ASSERT: subsession != NULL

  // Look for a "Transport:" header in the request string,
  // to extract client RTP and RTCP ports, if present:
  unsigned short clientRTPPortNum = 0; // default
  unsigned short clientRTCPPortNum = 1; // default
  parseTransportHeader(fullRequestStr, clientRTPPortNum, clientRTCPPortNum);
  Port clientRTPPort(clientRTPPortNum);
  Port clientRTCPPort(clientRTCPPortNum);

  Boolean isMulticast;
  netAddressBits destinationAddress = 0;
  u_int8_t destinationTTL = 255;
  Port serverRTPPort(0);
  Port serverRTCPPort(0);
  subsession->getStreamParameters(fClientAddr.sin_addr.s_addr,
				  clientRTPPort, clientRTCPPort,
				  isMulticast, destinationAddress, destinationTTL,
				  serverRTPPort, serverRTCPPort,
				  fStreamStates[streamNum].streamToken);
  if (isMulticast) {
    struct in_addr destinationAddr; destinationAddr.s_addr = destinationAddress;
    sprintf((char*)fBuffer,
	    "RTSP/1.0 200 OK\r\n"
	    "CSeq: %s\r\n"
	    "Transport: RTP/AVP;multicast;destination=%s;port=%d;ttl=%d\r\n"
	    "Session: %d\r\n\r\n",
	    cseq,
	    our_inet_ntoa(destinationAddr), ntohs(serverRTPPort.num()), destinationTTL,
	    fOurSessionId);
  } else {
    sprintf((char*)fBuffer,
	    "RTSP/1.0 200 OK\r\n"
	    "CSeq: %s\r\n"
	    "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d\r\n"
	    "Session: %d\r\n\r\n",
	    cseq,
	    ntohs(clientRTPPort.num()), ntohs(clientRTCPPort.num()), ntohs(serverRTPPort.num()), ntohs(serverRTCPPort.num()),
	    fOurSessionId);
  }
}

void RTSPServer::RTSPClientSession
::handleCmd_withinSession(char const* cmdName,
			  char const* urlPreSuffix, char const* urlSuffix,
			  char const* cseq) {
  // This will either be:
  // - a non-aggregated operation, if "urlPreSuffix" is the session (stream) name and
  //   "urlSuffix" is the subsession (track) name, or
  // - a aggregated operation, if "urlSuffix" is the session (stream) name.
  // First, figure out which of these it is:
  if (fOurServerMediaSession == NULL) { // There wasn't a previous SETUP!
    handleCmd_notSupported(cseq);
    return;
  }
  ServerMediaSubsession* subsession;
  if (urlSuffix[0] != '\0' &&
      strcmp(fOurServerMediaSession->streamName(), urlPreSuffix) == 0) {
    // Non-aggregated operation.
    // Look up the media subsession whose track id is "urlSuffix":
    ServerMediaSubsessionIterator iter(*fOurServerMediaSession);
    while ((subsession = iter.next()) != NULL) {
      if (strcmp(subsession->trackId(), urlSuffix) == 0) break; // success
    }
    if (subsession == NULL) { // no such track!
      handleCmd_notFound(cseq);
      return;
    }
  } else if (strcmp(fOurServerMediaSession->streamName(), urlSuffix) == 0) {
    // Aggregated operation
    subsession = NULL;
  } else { // the request doesn't match a known stream and/or track at all!
    handleCmd_notFound(cseq);
    return;
  }

  if (strcmp(cmdName, "TEARDOWN") == 0) {
    handleCmd_TEARDOWN(subsession, cseq);
  } else if (strcmp(cmdName, "PLAY") == 0) {
    handleCmd_PLAY(subsession, cseq);
  } else if (strcmp(cmdName, "PAUSE") == 0) {
    handleCmd_PAUSE(subsession, cseq);
  }
}

void RTSPServer::RTSPClientSession
  ::handleCmd_TEARDOWN(ServerMediaSubsession* subsession, char const* cseq) {
  for (unsigned i = 0; i < fNumStreamStates; ++i) {
    if (subsession == NULL /* means: aggregated operation */
	|| subsession == fStreamStates[i].subsession) {
      fStreamStates[i].subsession->endStream(fStreamStates[i].streamToken);
    }
  }
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\n\r\n", cseq);
  fSessionIsActive = False; // triggers deletion of ourself after responding
}

void RTSPServer::RTSPClientSession
  ::handleCmd_PLAY(ServerMediaSubsession* subsession, char const* cseq) {
  for (unsigned i = 0; i < fNumStreamStates; ++i) {
    if (subsession == NULL /* means: aggregated operation */
	|| subsession == fStreamStates[i].subsession) {
      fStreamStates[i].subsession->startStream(fStreamStates[i].streamToken);
    }
  }
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\nSession: %d\r\n\r\n",
	  cseq, fOurSessionId);
}

void RTSPServer::RTSPClientSession
  ::handleCmd_PAUSE(ServerMediaSubsession* subsession, char const* cseq) {
  for (unsigned i = 0; i < fNumStreamStates; ++i) {
    if (subsession == NULL /* means: aggregated operation */
	|| subsession == fStreamStates[i].subsession) {
      fStreamStates[i].subsession->pauseStream(fStreamStates[i].streamToken);
    }
  }
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\nSession: %d\r\n\r\n",
	  cseq, fOurSessionId);
}

Boolean
RTSPServer::RTSPClientSession
  ::parseRequestString(char const* reqStr,
		       unsigned reqStrSize,
		       char* resultCmdName,
		       unsigned resultCmdNameMaxSize,
		       char* resultURLPreSuffix,
		       unsigned resultURLPreSuffixMaxSize,
		       char* resultURLSuffix,
		       unsigned resultURLSuffixMaxSize,
		       char* resultCSeq,
		       unsigned resultCSeqMaxSize) {
  // This parser is currently rather dumb; it should be made smarter #####

  // Read everything up to the first space as the command name:
  Boolean parseSucceeded = False;
  unsigned i;
  for (i = 0; i < resultCmdNameMaxSize-1 && i < reqStrSize; ++i) {
    char c = reqStr[i];
    if (c == ' ') {
      parseSucceeded = True;
      break;
    }

    resultCmdName[i] = c;
  }
  resultCmdName[i] = '\0';
  if (!parseSucceeded) return False;
      
  // Skip over the prefix of any "rtsp://" URL that follows:
  unsigned j = i+1;
  while (j < reqStrSize && reqStr[j] == ' ') ++j; // skip over any additional spaces
  for (j = i+1; j < reqStrSize-8; ++j) {
    if ((reqStr[j] == 'r' || reqStr[j] == 'R')
	&& (reqStr[j+1] == 't' || reqStr[j+1] == 'T')
	&& (reqStr[j+2] == 's' || reqStr[j+2] == 'S')
	&& (reqStr[j+3] == 'p' || reqStr[j+3] == 'P')
	&& reqStr[j+4] == ':' && reqStr[j+5] == '/' && reqStr[j+6] == '/') {
      j += 7;
      while (j < reqStrSize && reqStr[j] != '/' && reqStr[j] != ' ') ++j;
      i = j;
      break;
    }
  }

  // Look for the URL suffix (before the following "RTSP/"):
  parseSucceeded = False;
  for (unsigned k = i+1; k < reqStrSize-5; ++k) {
    if (reqStr[k] == 'R' && reqStr[k+1] == 'T' &&
	reqStr[k+2] == 'S' && reqStr[k+3] == 'P' && reqStr[k+4] == '/') {
      while (--k >= i && reqStr[k] == ' ') {} // go back over all spaces before "RTSP/"
      unsigned k1 = k;
      while (k1 > i && reqStr[k1] != '/' && reqStr[k1] != ' ') --k1;
      // the URL suffix comes from [k1+1,k]

      // Copy "resultURLSuffix":
      if (k - k1 + 1 > resultURLSuffixMaxSize) return False; // there's no room
      unsigned n = 0, k2 = k1+1;
      while (k2 <= k) resultURLSuffix[n++] = reqStr[k2++];
      resultURLSuffix[n] = '\0';

      // Also look for the URL 'pre-suffix' before this:
      unsigned k3 = --k1;
      while (k3 > i && reqStr[k3] != '/' && reqStr[k3] != ' ') --k3;
      // the URL pre-suffix comes from [k3+1,k1]

      // Copy "resultURLPreSuffix":
      if (k1 - k3 + 1 > resultURLPreSuffixMaxSize) return False; // there's no room
      n = 0; k2 = k3+1;
      while (k2 <= k1) resultURLPreSuffix[n++] = reqStr[k2++];
      resultURLPreSuffix[n] = '\0';

      i = k + 7; // to go past " RTSP/"
      parseSucceeded = True;
      break;
    }
  }
  if (!parseSucceeded) return False;

  // Look for "CSeq: ", then read everything up to the next \r as 'CSeq':
  parseSucceeded = False;
  for (j = i; j < reqStrSize-6; ++j) {
    if (reqStr[j] == 'C' && reqStr[j+1] == 'S' && reqStr[j+2] == 'e' &&
	reqStr[j+3] == 'q' && reqStr[j+4] == ':' && reqStr[j+5] == ' ') {
      j += 6;
      unsigned n;
      for (n = 0; n < resultCSeqMaxSize-1 && j < reqStrSize; ++n,++j) {
	char c = reqStr[j];
	if (c == '\r' || c == '\n') {
	  parseSucceeded = True;
	  break;
	}

	resultCSeq[n] = c;
      }
      resultCSeq[n] = '\0';
      break;
    }
  }
  if (!parseSucceeded) return False;

  return True;
}
