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
#include "GroupsockHelper.hh"

#if defined(__WIN32__) || defined(_WIN32)
#define _close closesocket
#define snprintf _snprintf
#else
#define _close close
#endif

////////// RTSPServer //////////

RTSPServer* RTSPServer::createNew(UsageEnvironment& env,
				  ServerMediaSession& serverMediaSession,
				  Port ourPort) {
  int ourSocket = -1;
  RTSPServer* newServer = NULL;

  do {
    int ourSocket = setUpOurSocket(env, ourPort);
    if (ourSocket == -1) break;

    return new RTSPServer(env, serverMediaSession, ourSocket);
  } while (0);

  if (ourSocket != -1) ::_close(ourSocket);
  delete newServer;
  return NULL;
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
		       ServerMediaSession& serverMediaSession,
		       int ourSocket)
  : Medium(env), fOurServerMediaSession(serverMediaSession),
    fServerSocket(ourSocket), fSessionIdCounter(0) {

  // Arrange to handle connections from others:
  env.taskScheduler().turnOnBackgroundReadHandling(fServerSocket,
        (TaskScheduler::BackgroundHandlerProc*)&incomingConnectionHandler,
						   this);
}

RTSPServer::~RTSPServer() {
  // Turn off background read handling:
  envir().taskScheduler().turnOffBackgroundReadHandling(fServerSocket);

  ::_close(fServerSocket);
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
#if defined(__WIN32__) || defined(_WIN32)
    if (WSAGetLastError() != WSAEWOULDBLOCK) {
#else
    if (errno != EWOULDBLOCK) {
#endif
        envir().setResultErrMsg("accept() failed: ");
    }
    return;
  }

  // Create a new object for this RTSP session:
  // (Later, we need to do some garbage collection on sessions that #####
  //  aren't closed down via TEARDOWN) #####
  new RTSPSession(envir(), ++fSessionIdCounter,
		  fOurServerMediaSession, clientSocket);
}


////////// RTSPServer::RTSPSession //////////

RTSPServer::RTSPSession
::RTSPSession(UsageEnvironment& env, unsigned sessionId,
	      ServerMediaSession& ourServerMediaSession, int clientSocket)
  : Medium(env), fOurSessionId(sessionId),
  fOurServerMediaSession(ourServerMediaSession),
  fClientSocket(clientSocket), fSessionIsActive(True) {
  // Arrange to handle incoming requests:
  env.taskScheduler().turnOnBackgroundReadHandling(fClientSocket,
        (TaskScheduler::BackgroundHandlerProc*)&incomingRequestHandler,
						   this);
}

RTSPServer::RTSPSession::~RTSPSession() {
  // Turn off background read handling:
  envir().taskScheduler().turnOffBackgroundReadHandling(fClientSocket);

  ::_close(fClientSocket);
}

void RTSPServer::RTSPSession::incomingRequestHandler(void* instance,
						     int /*mask*/) {
  RTSPSession* session = (RTSPSession*)instance;
  session->incomingRequestHandler1();
}

void RTSPServer::RTSPSession::incomingRequestHandler1() {
  struct sockaddr_in fromAddress;
  int bytesRead = readSocket(envir(), fClientSocket,
			     fBuffer, sizeof fBuffer, fromAddress);
#ifdef DEBUG
  fprintf(stderr, "RTSPSession[%p]::incomingRequestHandler1() read %d bytes:%s\n", this, bytesRead, fBuffer);
#endif
  if (bytesRead <= 0) return;

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
    fprintf(stderr, "parse failed!\n");
#endif
    handleCmd_bad(cseq);
  } else if (strcmp(cmdName, "OPTIONS") == 0) {
    handleCmd_OPTIONS(cseq);
  } else if (strcmp(cmdName, "DESCRIBE") == 0) {
    handleCmd_DESCRIBE(cseq);
  } else if (strcmp(cmdName, "SETUP") == 0
	     || strcmp(cmdName, "TEARDOWN") == 0
	     || strcmp(cmdName, "PLAY") == 0) {
    handleCmd_subsession(cmdName, urlSuffix, cseq);
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
  = "OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY";

void RTSPServer::RTSPSession::handleCmd_bad(char const* /*cseq*/) {
  // Don't do anything with "cseq", because it might be nonsense
  sprintf((char*)fBuffer, "RTSP/1.0 400 Bad Request\r\nAllow: %s\r\n\r\n",
	  allowedCommandNames);
  fSessionIsActive = False; // triggers deletion of ourself after responding
}

void RTSPServer::RTSPSession::handleCmd_notSupported(char const* cseq) {
  sprintf((char*)fBuffer, "RTSP/1.0 405 Method Not Allowed\r\nCSeq: %s\r\nAllow: %s\r\n\r\n",
	  cseq, allowedCommandNames);
  fSessionIsActive = False; // triggers deletion of ourself after responding
}

void RTSPServer::RTSPSession::handleCmd_OPTIONS(char const* cseq) {
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\nPublic: %s\r\n\r\n",
	  cseq, allowedCommandNames);
}

void RTSPServer::RTSPSession::handleCmd_DESCRIBE(char const* cseq) {
  // We should really check that the request contains an "Accept:" #####
  // for "application/sdp", because that's what we're sending back #####

  // Begin by assembling a basic SDP description for this session:
  char const* sdpDescription
    = fOurServerMediaSession.generateSDPDescription();
  unsigned sdpDescriptionSize = strlen(sdpDescription);
  if (sdpDescriptionSize > sizeof(fBuffer) - 200) { // sanity check
    sprintf((char*)fBuffer, "RTSP/1.0 500 Internal Server Error\r\nCSeq: %s\r\n\r\n",
	    cseq);
    return;
  }
  
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\nContent-Type: application/sdp\r\nContent-Length: %d\r\n\r\n%s",
	  cseq, sdpDescriptionSize, sdpDescription);
}

void RTSPServer::RTSPSession
  ::handleCmd_subsession(char const* cmdName,
			 char const* urlSuffix, char const* cseq) {
  // Look up the media subsession whose track id is "urlSuffix":
  ServerMediaSubsessionIterator iter(fOurServerMediaSession);
  ServerMediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    if (strcmp(subsession->trackId(), urlSuffix) == 0) break; // success
  }

  if (subsession == NULL && strcmp(cmdName, "SETUP") == 0) {
    // The specified track id doesn't exist, so this request fails:
    sprintf((char*)fBuffer, "RTSP/1.0 404 Not Found\r\nCSeq: %s\r\n\r\n",
	    cseq);
    return;
  }

  if (strcmp(cmdName, "SETUP") == 0) {
    handleCmd_SETUP(subsession, cseq);
  } else if (strcmp(cmdName, "TEARDOWN") == 0) {
    handleCmd_TEARDOWN(subsession, cseq);
  } else if (strcmp(cmdName, "PLAY") == 0) {
    handleCmd_PLAY(subsession, cseq);
  }
}

void RTSPServer::RTSPSession
  ::handleCmd_SETUP(ServerMediaSubsession* subsession, char const* cseq) {
  GroupEId const& groupEId = subsession->groupEId();
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\nTransport: RTP/AVP;multicast;destination=%s;port=%d;ttl=%d\r\nSession: %d\r\n\r\n",
	  cseq, our_inet_ntoa(groupEId.groupAddress()),
	  groupEId.portNum(), groupEId.scope().ttl(), fOurSessionId);
}

void RTSPServer::RTSPSession
  ::handleCmd_TEARDOWN(ServerMediaSubsession* /*subsession*/,
		       char const* cseq) {
  // We should really check that the supplied "Session:" parameter #####
  // matches us #####
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\n\r\n", cseq);
  fSessionIsActive = False; // triggers deletion of ourself after responding
}

void RTSPServer::RTSPSession
  ::handleCmd_PLAY(ServerMediaSubsession* /*subsession*/,
		   char const* cseq) {
  // We should really check that the supplied "Session:" parameter #####
  // matches us #####
  sprintf((char*)fBuffer, "RTSP/1.0 200 OK\r\nCSeq: %s\r\nSession: %d\r\n\r\n",
	  cseq, fOurSessionId);
}

Boolean
RTSPServer::RTSPSession
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
