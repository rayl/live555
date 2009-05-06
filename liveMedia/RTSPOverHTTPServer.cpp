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
// Copyright (c) 1996-2007 Live Networks, Inc.  All rights reserved.
// A simple HTTP server that acts solely to implement RTSP-over-HTTP tunneling
// (to a separate RTSP server).
// Implementation

#ifdef undef
THIS CODE DOESN'T YET WORK.  DON'T TRY TO USE IT!!
#include "RTSPOverHTTPServer.hh"
#include <GroupsockHelper.hh>

#if defined(__WIN32__) || defined(_WIN32) || defined(_QNX4)
#else
#include <signal.h>
#define USE_SIGNALS 1
#endif


#define DEBUG 1 //#####@@@@@
///////// RTSPOverHTTPServer implementation //////////

RTSPOverHTTPServer*
RTSPOverHTTPServer::createNew(UsageEnvironment& env, Port ourHTTPPort,
			      Port rtspServerPort, char const* rtspServerHostName) {
  int ourSocket = -1;

  do {
    int ourSocket = setUpOurSocket(env, ourHTTPPort);
    if (ourSocket == -1) break;

    return new RTSPOverHTTPServer(env, ourSocket, rtspServerPort, rtspServerHostName);
  } while (0);

  if (ourSocket != -1) ::closeSocket(ourSocket);
  return NULL;
}

#define LISTEN_BACKLOG_SIZE 20

int RTSPOverHTTPServer::setUpOurSocket(UsageEnvironment& env, Port& ourPort) {
  int ourSocket = -1;

  do {
    NoReuse dummy; // Don't use this socket if there's already a local server using it

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

  if (ourSocket != -1) ::closeSocket(ourSocket);
  return -1;
}

RTSPOverHTTPServer
::RTSPOverHTTPServer(UsageEnvironment& env, int ourSocket,
		     Port rtspServerPort, char const* rtspServerHostName)
  : Medium(env),
    fServerSocket(ourSocket),
    fRTSPServerPort(rtspServerPort), fRTSPServerHostName(strDup(rtspServerHostName)) {
#ifdef USE_SIGNALS
  // Ignore the SIGPIPE signal, so that clients on the same host that are killed
  // don't also kill us:
  signal(SIGPIPE, SIG_IGN);
#endif

  // Arrange to handle connections from others:
  env.taskScheduler().turnOnBackgroundReadHandling(fServerSocket,
	   (TaskScheduler::BackgroundHandlerProc*)&incomingConnectionHandler,
						   this);
}

RTSPOverHTTPServer::~RTSPOverHTTPServer() {
  delete[] fRTSPServerHostName;
}

void RTSPOverHTTPServer::incomingConnectionHandler(void* instance, int /*mask*/) {
  RTSPOverHTTPServer* server = (RTSPOverHTTPServer*)instance;
  server->incomingConnectionHandler1();
}

void RTSPOverHTTPServer::incomingConnectionHandler1() {
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
  makeSocketNonBlocking(clientSocket);
#if defined(DEBUG) || defined(DEBUG_CONNECTIONS)
  fprintf(stderr, "accept()ed connection from %s\n", our_inet_ntoa(clientAddr.sin_addr));
#endif

  // Create a new object for handling this HTTP connection:
  new HTTPClientConnection(*this, clientSocket);
}


////////// HTTPClientConnection implementation /////////

RTSPOverHTTPServer::HTTPClientConnection
::HTTPClientConnection(RTSPOverHTTPServer& ourServer, int clientSocket)
  : fOurServer(ourServer), fClientSocket(clientSocket) {
  // Arrange to handle incoming requests:
  resetRequestBuffer();
  envir().taskScheduler().turnOnBackgroundReadHandling(fClientSocket,
	       (TaskScheduler::BackgroundHandlerProc*)&incomingRequestHandler, this);
}

RTSPOverHTTPServer::HTTPClientConnection
::~HTTPClientConnection() {
  // Turn off background read handling:
  envir().taskScheduler().turnOffBackgroundReadHandling(fClientSocket);

  ::closeSocket(fClientSocket);
}

void RTSPOverHTTPServer::HTTPClientConnection::resetRequestBuffer() {
  fRequestBytesAlreadySeen = 0;
  fRequestBufferBytesLeft = sizeof fRequestBuffer;
  fLastCRLF = &fRequestBuffer[-3]; // hack
}

void RTSPOverHTTPServer::HTTPClientConnection
::incomingRequestHandler(void* instance, int /*mask*/) {
  HTTPClientConnection* connection = (HTTPClientConnection*)instance;
  connection->incomingRequestHandler1();
}

void RTSPOverHTTPServer::HTTPClientConnection::incomingRequestHandler1() {
  struct sockaddr_in dummy; // 'from' address, meaningless in this case
  Boolean endOfMsg = False;
  unsigned char* ptr = &fRequestBuffer[fRequestBytesAlreadySeen];
  
  int bytesRead = readSocket(envir(), fClientSocket,
                             ptr, fRequestBufferBytesLeft, dummy);
  if (bytesRead <= 0 || (unsigned)bytesRead >= fRequestBufferBytesLeft) {
    // Either the client socket has died, or the request was too big for us.
    // Terminate this connection:
#ifdef DEBUG
    fprintf(stderr, "HTTPClientConnection[%p]::incomingRequestHandler1() read %d bytes (of %d); terminating connection!\n", this, bytesRead, fRequestBufferBytesLeft);
#endif
    delete this;
    return;
  }
#ifdef DEBUG
  ptr[bytesRead] = '\0';
  fprintf(stderr, "HTTPClientConnection[%p]::incomingRequestHandler1() read %d bytes:%s\n",
	  this, bytesRead, ptr);
#endif

  // Look for the end of the message: <CR><LF><CR><LF>
  unsigned char *tmpPtr = ptr;
  if (fRequestBytesAlreadySeen > 0) --tmpPtr;
  // in case the last read ended with a <CR>
  while (tmpPtr < &ptr[bytesRead-1]) {
    if (*tmpPtr == '\r' && *(tmpPtr+1) == '\n') {
      if (tmpPtr - fLastCRLF == 2) { // This is it:
        endOfMsg = 1;
        break;
      }
      fLastCRLF = tmpPtr;
    }
    ++tmpPtr;
  }
  
  fRequestBufferBytesLeft -= bytesRead;
  fRequestBytesAlreadySeen += bytesRead;

  if (!endOfMsg) return; // subsequent reads will be needed to complete the request

  // Parse the request string to get the (few) parameters that we care about,
  // then handle the command:
  fRequestBuffer[fRequestBytesAlreadySeen] = '\0';
  char cmdName[HTTP_PARAM_STRING_MAX];
  char sessionCookie[HTTP_PARAM_STRING_MAX];
  char acceptStr[HTTP_PARAM_STRING_MAX];
  char contentTypeStr[HTTP_PARAM_STRING_MAX];
  if (!parseRequestString(cmdName, sizeof cmdName,
			  sessionCookie, sizeof sessionCookie,
			  acceptStr, sizeof acceptStr,
			  contentTypeStr, sizeof contentTypeStr) {
#ifdef DEBUG
    fprintf(stderr, "parseRTSPRequestString() failed!\n");
#endif
    handleCmd_bad(cseq);
  } else {
#ifdef DEBUG
    fprintf(stderr, "parseRTSPRequestString() returned cmdName \"%s\", urlPreSuffix \"%s\", urlSuffix \"%s\"\n", cmdName, urlPreSuffix, urlSuffix);
#endif
    if (strcmp(cmdName, "OPTIONS") == 0) {
      handleCmd_OPTIONS(cseq);
    } else if (strcmp(cmdName, "DESCRIBE") == 0) {
      handleCmd_DESCRIBE(cseq, urlSuffix, (char const*)fRequestBuffer);
    } else if (strcmp(cmdName, "SETUP") == 0) {
      handleCmd_SETUP(cseq, urlPreSuffix, urlSuffix, (char const*)fRequestBuffer);
    } else if (strcmp(cmdName, "TEARDOWN") == 0
               || strcmp(cmdName, "PLAY") == 0
               || strcmp(cmdName, "PAUSE") == 0
               || strcmp(cmdName, "GET_PARAMETER") == 0) {
      handleCmd_withinSession(cmdName, urlPreSuffix, urlSuffix, cseq,
                              (char const*)fRequestBuffer);
    } else {
      handleCmd_notSupported(cseq);
    }
  }
    
#ifdef DEBUG
  fprintf(stderr, "sending response: %s", fResponseBuffer);
#endif
  send(fClientSocket, (char const*)fResponseBuffer, strlen((char*)fResponseBuffer), 0);

  if (strcmp(cmdName, "SETUP") == 0 && fStreamAfterSETUP) {
    // The client has asked for streaming to commence now, rather than after a
    // subsequent "PLAY" command.  So, simulate the effect of a "PLAY" command:
    handleCmd_withinSession("PLAY", urlPreSuffix, urlSuffix, cseq,
                            (char const*)fRequestBuffer);
  }

  resetRequestBuffer(); // to prepare for any subsequent request
  if (!fSessionIsActive) delete this;
}
#endif
