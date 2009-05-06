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

class SIPClient: public Medium {
public:
  static SIPClient* createNew(UsageEnvironment& env,
			      int verbosityLevel = 0,
			      char const* applicationName = NULL);

  void setProxyServer(unsigned proxyServerAddress,
		      unsigned short proxyServerPortNum);

  void setClientStartPortNum(unsigned short clientStartPortNum) {
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

protected:
  virtual ~SIPClient();

private:
  SIPClient(UsageEnvironment& env, int verbosityLevel,
	    char const* applicationName);
      // called only by createNew();

  void reset();

  char* invite1(AuthRecord* authenticator);
  Boolean processURL(char const* url);
  Boolean parseURL(char const* url,
		   NetAddress& address, unsigned short& portNum);
  char* createAuthenticatorString(AuthRecord const* authenticator,
				  char const* cmd, char const* url);
  void useAuthenticator(AuthRecord const* authenticator);
      // in future reqs
  void resetCurrentAuthenticator();
  Boolean sendRequest(char const* requestString);
  int getResponse(char* responseBuffer, unsigned responseBufferSize);
  Boolean parseResponseCode(char const* line, unsigned& responseCode);

private:
  // Set for all calls:
  int fVerbosityLevel;
  unsigned fCSeq; // sequence number, used in consecutive requests
  char const* fApplicationName;
      unsigned fApplicationNameSize;
  struct in_addr fOurAddress;
      char const* fOurAddressStr;
      unsigned fOurAddressStrSize;
  unsigned short fOurPortNum;
  Groupsock* fOurSocket;
  char* fUserAgentHeaderStr;
      unsigned fUserAgentHeaderStrSize;

  // Set for each call:
  char const* fURL;
      unsigned fURLSize;
  struct in_addr fServerAddress;
  unsigned short fServerPortNum; // in host order 
  unsigned short fClientStartPortNum; // in host order
  unsigned fCallId, fFromTag; // set by us
  char const* fToTagStr; // set by the responder
      unsigned fToTagStrSize;
  AuthRecord* fCurrentAuthenticator; // if any
  char const* fUserName; // 'user' name used in "From:" & "Contact:" lines
      unsigned fUserNameSize;
};

#endif
