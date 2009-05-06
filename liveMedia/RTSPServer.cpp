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
// Copyright (c) 1996-2003 Live Networks, Inc.  All rights reserved.
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

  for (unsigned i = 0; i < fNumStreamStates; ++i) {
    if (fStreamStates[i].subsession != NULL) {
      fStreamStates[i].subsession->deleteStream(fStreamStates[i].streamToken);
    }
  }
  delete[] fStreamStates;
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
  char urlSuffix[PARAM_STRING_MAX];
  char cseq[PARAM_STRING_MAX];
  if (!parseRequestString((char*)fBuffer, bytesRead,
			  cmdName, sizeof cmdName,
			  urlSuffix, sizeof urlSuffix,
			  cseq, sizeof cseq)) {
#ifdef DEBUG
    fprintf(stderr, "parseRequestString() failed!\n");
#endif
    handleCmd_bad(cseq);
  } else if (strcmp(cmdName, "OPTIONS") == 0) {
    handleCmd_OPTIONS(cseq);
  } else if (strcmp(cmdName, "DESCRIBE") == 0) {
    handleCmd_DESCRIBE(cseq, urlSuffix);
  } else if (strcmp(cmdName, "SETUP") == 0
	     || strcmp(cmdName, "TEARDOWN") == 0
	     || strcmp(cmdName, "PLAY") == 0
	     || strcmp(cmdName, "PAUSE") == 0) {
    handleCmd_subsession(cmdName, urlSuffix, cseq, (char const*)fBuffer);
  } else {
    handleCmd_notSupported(cseq);
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

void RTSPServer::RTSPClientSession::handleCmd_OPTIONS(char const* cseq) {
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\nPublic: %s\r\n\r\n",
	  cseq, allowedCommandNames);
}

void RTSPServer::RTSPClientSession
::handleCmd_DESCRIBE(char const* cseq, char const* urlSuffix) {
  // We should really check that the request contains an "Accept:" #####
  // for "application/sdp", because that's what we're sending back #####

  // Begin by looking up the "ServerMediaSession" object for the
  // specified "urlSuffix":
  fOurServerMediaSession = (ServerMediaSession*)
    (fOurServer.fServerMediaSessions->Lookup(urlSuffix));
  if (fOurServerMediaSession == NULL) {
    sprintf((char*)fBuffer, "RTSP/1.0 404 Stream Not Found\r\nCSeq: %s\r\n\r\n", cseq);
    return;
  }

  // Then, assemble a SDP description for this session:
  char const* sdpDescription
    = fOurServerMediaSession->generateSDPDescription();
  if (sdpDescription == NULL) {
    // This usually means that a file name that was specified for a
    // "ServerMediaSubsession" does not exist.
    sprintf((char*)fBuffer, "RTSP/1.0 404 File Not Found, Or In Incorrect Format\r\nCSeq: %s\r\n\r\n", cseq);
    return;
  }

  // Set up our array of states for this client session's streams:
  ServerMediaSubsessionIterator iter(*fOurServerMediaSession);
  for (fNumStreamStates = 0; iter.next() != NULL; ++fNumStreamStates) {}
  delete[] fStreamStates;
  fStreamStates = new struct streamState[fNumStreamStates];
  iter.reset();
  ServerMediaSubsession* subsession;
  for (unsigned i = 0; i < fNumStreamStates; ++i) {
    subsession = iter.next();
    fStreamStates[i].subsession = subsession;
    fStreamStates[i].streamToken = NULL; // for now; reset by SETUP
  }

  unsigned sdpDescriptionSize = strlen(sdpDescription);
  if (sdpDescriptionSize > sizeof fBuffer - 200) { // sanity check
    sprintf((char*)fBuffer, "RTSP/1.0 500 Internal Server Error\r\nCSeq: %s\r\n\r\n",
	    cseq);
    return;
  }
  
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\nContent-Type: application/sdp\r\nContent-Length: %d\r\n\r\n%s",
	  cseq, sdpDescriptionSize, sdpDescription);
}

void RTSPServer::RTSPClientSession
::handleCmd_subsession(char const* cmdName,
		       char const* urlSuffix, char const* cseq,
		       char const* fullRequestStr) {
  // Look up the media subsession whose track id is "urlSuffix":
  ServerMediaSubsessionIterator iter(*fOurServerMediaSession);
  ServerMediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    if (strcmp(subsession->trackId(), urlSuffix) == 0) break; // success
  }

  if (strcmp(cmdName, "SETUP") == 0) {
    handleCmd_SETUP(subsession, cseq, fullRequestStr);
  } else if (strcmp(cmdName, "TEARDOWN") == 0) {
    handleCmd_TEARDOWN(subsession, cseq);
  } else if (strcmp(cmdName, "PLAY") == 0) {
    handleCmd_PLAY(subsession, cseq);
  } else if (strcmp(cmdName, "PAUSE") == 0) {
    handleCmd_PAUSE(subsession, cseq);
  }
}

static void parseTransportHeader(char const* buf,
				 unsigned short& rtpPortNum,
				 unsigned short& rtcpPortNum) {
  // Note: This is a quick-and-dirty implementation.  Should improve #####
  while (*buf != '\0') {
    if (*buf == 'c') {
      // Check for "client_port="
      unsigned short p1, p2;
      if (sscanf(buf, "client_port=%hu-%hu", &p1, &p2) == 2) {
	rtpPortNum = p1;
	rtcpPortNum = p2;
	return;
      }
    }
    ++buf;
  }
}

void RTSPServer::RTSPClientSession
::handleCmd_SETUP(ServerMediaSubsession* subsession, char const* cseq,
		  char const* fullRequestStr) {
  // Figure out which of our streams this is:
  unsigned streamNum;
  for (streamNum = 0; streamNum < fNumStreamStates; ++streamNum) {
    if (fStreamStates[streamNum].subsession == subsession) break;
  }
  if (subsession == NULL || streamNum >= fNumStreamStates) {
    // The specified track id doesn't exist, so this request fails:
    sprintf((char*)fBuffer, "RTSP/1.0 404 Not Found\r\nCSeq: %s\r\n\r\n",
	    cseq);
    return;
  }

  // Look for a "Transport:" header in the request string,
  // to extract client RTP and RTCP ports, if present:
  unsigned short rtpPortNum = 0; // default
  unsigned short rtcpPortNum = 1; // default
  parseTransportHeader(fullRequestStr, rtpPortNum, rtcpPortNum);

  GroupEId groupEId;
  Boolean isMulticast;
  subsession->getStreamParameters(fClientAddr, rtpPortNum, rtcpPortNum,
				  groupEId, isMulticast,
				  fStreamStates[streamNum].streamToken);
  if (isMulticast) {
    sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\nTransport: RTP/AVP;multicast;destination=%s;port=%d;ttl=%d\r\nSession: %d\r\n\r\n",
	    cseq, our_inet_ntoa(groupEId.groupAddress()),
	    groupEId.portNum(), groupEId.scope().ttl(), fOurSessionId);
  } else {
    sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\nTransport: RTP/AVP;unicast\r\nSession: %d\r\n\r\n",
	    cseq, fOurSessionId);
  }
}

void RTSPServer::RTSPClientSession
  ::handleCmd_PLAY(ServerMediaSubsession* subsession, char const* cseq) {
  for (unsigned i = 0; i < fNumStreamStates; ++i) {
    if (subsession == 0 /* means: aggregate operation */
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
    if (subsession == 0 /* means: aggregate operation */
	|| subsession == fStreamStates[i].subsession) {
      fStreamStates[i].subsession->pauseStream(fStreamStates[i].streamToken);
    }
  }
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\nSession: %d\r\n\r\n",
	  cseq, fOurSessionId);
}

void RTSPServer::RTSPClientSession
  ::handleCmd_TEARDOWN(ServerMediaSubsession* subsession,
		       char const* cseq) {
  for (unsigned i = 0; i < fNumStreamStates; ++i) {
    if (subsession == 0 /* means: aggregate operation */
	|| subsession == fStreamStates[i].subsession) {
      fStreamStates[i].subsession->endStream(fStreamStates[i].streamToken);
    }
  }
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\n\r\n", cseq);
  fSessionIsActive = False; // triggers deletion of ourself after responding
}

Boolean
RTSPServer::RTSPClientSession
  ::parseRequestString(char const* reqStr,
		       unsigned reqStrSize,
		       char* resultCmdName,
		       unsigned resultCmdNameMaxSize,
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
      
  // Look for the URL suffix (between "/" and " RTSP/"):
  parseSucceeded = False;
  for (unsigned k = i+1; k < reqStrSize-6; ++k) {
    if (reqStr[k] == ' ' && reqStr[k+1] == 'R' && reqStr[k+2] == 'T' &&
	reqStr[k+3] == 'S' && reqStr[k+4] == 'P' && reqStr[k+5] == '/') {
      while (reqStr[k] == ' ') --k; // skip over all spaces
      unsigned k1 = k;
      while (reqStr[k1] != '/' && reqStr[k1] != ' ') --k1;
      ++k1; // the URL suffix comes from [k1,k]
      if (k - k1 +2 > resultURLSuffixMaxSize) return False; // no space

      parseSucceeded = True;
      unsigned n = 0;
      while (k1 <= k) resultURLSuffix[n++] = reqStr[k1++];
      resultURLSuffix[n] = '\0';
      i = k + 7;
      break;
    }
  }
  if (!parseSucceeded) return False;

  // Look for "CSeq: ", then read everything up to the next \r as 'CSeq':
  parseSucceeded = False;
  for (unsigned j = i; j < reqStrSize-6; ++j) {
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
