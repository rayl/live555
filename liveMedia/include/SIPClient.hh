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
// A generic SIP client
// C++ header

#ifndef _SIP_CLIENT_HH
#define _SIP_CLIENT_HH

#ifndef _MEDIA_SESSION_HH
#include "MediaSession.hh"
#endif
#ifndef _NET_ADDRESS_HH
#include "NetAddress.hh"
#endif
#include "RTSPClient.hh" // for AuthRecord #####

// Possible states in the "INVITE" transition diagram (RFC 3261, Figure 5)
enum inviteClientState { Calling, Proceeding, Completed, Terminated };

class SIPClient: public Medium {
public:
  static SIPClient* createNew(UsageEnvironment& env,
			      unsigned char desiredAudioRTPPayloadFormat,
			      int verbosityLevel = 0,
			      char const* applicationName = NULL);

  void setProxyServer(unsigned proxyServerAddress,
		      portNumBits proxyServerPortNum);

  void setClientStartPortNum(portNumBits clientStartPortNum) {
    fClientStartPortNum = clientStartPortNum;
  }

  char* invite(char const* url, AuthRecord* authenticator = NULL);
      // Issues a SIP "INVITE" command
      // Returns the session SDP description if this command succeeds
  char* inviteWithPassword(char const* url,
			   char const* username, char const* password);
      // Uses "invite()" to do an "INVITE" - first
      // without using "password", then (if we get an Unauthorized
      // response) with an authentication response computed from "password"

  Boolean sendACK(); // on current call
  Boolean sendBYE(); // on current call

  static Boolean parseSIPURL(UsageEnvironment& env, char const* url,
			     NetAddress& address, portNumBits& portNum);

protected:
  virtual ~SIPClient();

private:
  SIPClient(UsageEnvironment& env,
	    unsigned char desiredAudioRTPPayloadFormat,
	    int verbosityLevel,
	    char const* applicationName);
      // called only by createNew();

  void reset();

  // Routines used to implement invite*():
  char* invite1(AuthRecord* authenticator);
  Boolean processURL(char const* url);
  Boolean sendINVITE();
  static void inviteResponseHandler(void* clientData, int mask);
  void doInviteStateMachine(unsigned responseCode); 
  void doInviteStateTerminated(unsigned responseCode);
  TaskToken fTimerA, fTimerB, fTimerD;
  static void timerAHandler(void* clientData);
  static void timerBHandler(void* clientData);
  static void timerDHandler(void* clientData);
  unsigned const fT1; // in microseconds
  unsigned fTimerALen; // in microseconds; initially fT1, then doubles
  unsigned fTimerACount;

  // Routines used to implement all commands:
  char* createAuthenticatorString(AuthRecord const* authenticator,
				  char const* cmd, char const* url);
  void useAuthenticator(AuthRecord const* authenticator);
      // in future reqs
  void resetValidAuthenticator();
  Boolean sendRequest(char const* requestString, unsigned requestLength);
  unsigned getResponseCode();
  int getResponse(char*& responseBuffer, unsigned responseBufferSize);
  Boolean parseResponseCode(char const* line, unsigned& responseCode);

private:
  // Set for all calls:
  unsigned char fDesiredAudioRTPPayloadFormat;
  int fVerbosityLevel;
  unsigned fCSeq; // sequence number, used in consecutive requests
  char const* fApplicationName;
      unsigned fApplicationNameSize;
  struct in_addr fOurAddress;
      char const* fOurAddressStr;
      unsigned fOurAddressStrSize;
  portNumBits fOurPortNum;
  Groupsock* fOurSocket;
  char* fUserAgentHeaderStr;
      unsigned fUserAgentHeaderStrSize;

  // Set for each call:
  char const* fURL;
      unsigned fURLSize;
  struct in_addr fServerAddress;
  portNumBits fServerPortNum; // in host order 
  portNumBits fClientStartPortNum; // in host order
  unsigned fCallId, fFromTag; // set by us
  char const* fToTagStr; // set by the responder
      unsigned fToTagStrSize;
  AuthRecord* fValidAuthenticator; // if any
  char const* fUserName; // 'user' name used in "From:" & "Contact:" lines
      unsigned fUserNameSize;

  char* fInviteSDPDescription;
  char* fInviteCmd;
      unsigned fInviteCmdSize;
  AuthRecord* fWorkingAuthenticator;
  inviteClientState fInviteClientState;
  char fEventLoopStopFlag;
};

#endif
