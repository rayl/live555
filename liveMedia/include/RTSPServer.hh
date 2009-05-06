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
// A RTSP server
// C++ header

#ifndef _RTSP_SERVER_HH
#define _RTSP_SERVER_HH

#ifndef _SERVER_MEDIA_SESSION_HH
#include "ServerMediaSession.hh"
#endif

class RTSPServer: public Medium {
public:
  static RTSPServer* createNew(UsageEnvironment& env,
			       ServerMediaSession& serverMediaSession,
			       Port ourPort = 554);
      // if ourPort.num() == 0, we'll choose (& return) port

protected:
  RTSPServer(UsageEnvironment& env,
	     ServerMediaSession& serverMediaSession, int ourSocket);
      // called only by createNew();
  virtual ~RTSPServer();

  static int setUpOurSocket(UsageEnvironment& env, Port& ourPort);

private:
  static void incomingConnectionHandler(void*, int /*mask*/);
  void incomingConnectionHandler1();

  // The state of each individual session handled by a RTSP server:
  class RTSPSession: public Medium {
  public:
    RTSPSession(UsageEnvironment& env, unsigned sessionId,
		ServerMediaSession& ourServerMediaSession,
		int clientSocket);
    virtual ~RTSPSession();
  private:
    static void incomingRequestHandler(void*, int /*mask*/);
    void incomingRequestHandler1();
    void handleCmd_bad(char const* cseq);
    void handleCmd_notSupported(char const* cseq);
    void handleCmd_OPTIONS(char const* cseq);
    void handleCmd_DESCRIBE(char const* cseq);
    void handleCmd_subsession(char const* cmdName,
			      char const* urlSuffix, char const* cseq);
    void handleCmd_SETUP(ServerMediaSubsession* subsession,
			 char const* cseq);
    void handleCmd_TEARDOWN(ServerMediaSubsession* subsession,
			    char const* cseq);
    void handleCmd_PLAY(ServerMediaSubsession* subsession,
			char const* cseq);
    Boolean parseRequestString(char const *reqStr, unsigned reqStrSize,
			       char *resultCmdName,
			       unsigned resultCmdNameMaxSize, 
			       char* resultURLSuffix,
			       unsigned resultURLSuffixMaxSize, 
			       char* resultCSeq,
			       unsigned resultCSeqMaxSize); 

  private:
    unsigned fOurSessionId;
    ServerMediaSession& fOurServerMediaSession;
    int fClientSocket;
    unsigned char fBuffer[10000];
    Boolean fSessionIsActive;
  };

private:
  ServerMediaSession& fOurServerMediaSession;
  int fServerSocket;
  unsigned fSessionIdCounter;
};

#endif
