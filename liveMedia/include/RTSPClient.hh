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
// A generic RTSP client
// C++ header

#ifndef _RTSP_CLIENT_HH
#define _RTSP_CLIENT_HH

#ifndef _MEDIA_SESSION_HH
#include "MediaSession.hh"
#endif
#ifndef _NET_ADDRESS_HH
#include "NetAddress.hh"
#endif

// A structure used for (optional) digest authentication.
// The "realm", and "nonce" fields are supplied by the server
// (in a "401 Unauthorized" response).
// The "username" and "password" fields are supplied by the client.
struct AuthRecord {
  char const* realm; char const* nonce;
  char const* username; char const* password;
};

class RTSPClient: public Medium {
public:
  static RTSPClient* createNew(UsageEnvironment& env,
			       int verbosityLevel = 0,
			       char const* applicationName = NULL);

  int socketNum() const { return fSocketNum; }

  static Boolean lookupByName(UsageEnvironment& env,
			      char const* sourceName,
			      RTSPClient*& resultClient);

  char* describeURL(char const* url, AuthRecord* authenticator = NULL);
      // Issues a RTSP "DESCRIBE" command
      // Returns the SDP description of a session, or NULL if none
      // (This is dynamically allocated, and must later be freed
      //  by the caller)
  char* describeWithPassword(char const* url,
			       char const* username, char const* password);
      // Uses "describeURL()" to do a "DESCRIBE" - first
      // without using "password", then (if we get an Unauthorized
      // response) with an authentication response computed from "password"
  Boolean announceSDPDescription(char const* url,
				 char const* sdpDescription,
				 AuthRecord* authenticator = NULL);
      // Issues a RTSP "ANNOUNCE" command
      // Returns True iff this command succeeds
  Boolean announceWithPassword(char const* url, char const* sdpDescription,
			       char const* username, char const* password);
      // Uses "announceSDPDescription()" to do an "ANNOUNCE" - first
      // without using "password", then (if we get an Unauthorized
      // response) with an authentication response computed from "password"
  Boolean sendOptionsCmd();
      // Issues a RTSP "OPTIONS" command
      // Returns True iff this command succeeds

  Boolean setupMediaSubsession(MediaSubsession& subsession,
			       Boolean streamOutgoing = False,
			       Boolean streamUsingTCP = False);
      // Issues a RTSP "SETUP" command on "subsession".
      // Returns True iff this command succeeds
  Boolean playMediaSubsession(MediaSubsession& subsession,
			      Boolean hackForDSS = False);
      // Issues a RTSP "PLAY" command on "subsession".
      // Returns True iff this command succeeds
  Boolean recordMediaSubsession(MediaSubsession& subsession);
      // Issues a RTSP "RECORD" command on "subsession".
      // Returns True iff this command succeeds
  Boolean teardownMediaSubsession(MediaSubsession& subsession);
      // Issues a RTSP "TEARDOWN" command on "subsession".
      // Returns True iff this command succeeds

protected:
  virtual ~RTSPClient();

private: // redefined virtual functions
  virtual Boolean isRTSPClient() const;

private:
  RTSPClient(UsageEnvironment& env, int verbosityLevel,
	     char const* applicationName);
      // called only by createNew();

  void reset();

  Boolean openConnectionFromURL(char const* url);
  Boolean parseURL(char const* url,
		   NetAddress& address, unsigned short& portNum);
  char* createAuthenticatorString(AuthRecord const* authenticator,
				  char const* cmd, char const* url);
  void useAuthenticator(AuthRecord const* authenticator); // in future reqs
  void resetCurrentAuthenticator();
  Boolean sendRequest(char const* requestString);
  int getResponse(char* responseBuffer, unsigned responseBufferSize);
  Boolean parseResponseCode(char const* line, unsigned& responseCode);
  Boolean parseTransportResponse(char const* line,
				 char*& serverAddressStr,
				 unsigned short& serverPortNum,
				 unsigned char& rtpChannelId,
				 unsigned char& rtcpChannelId);
  void constructSubsessionURL(MediaSubsession const& subsession,
			      char const*& prefix,
			      char const*& separator,
			      char const*& suffix);

private:
  int fVerbosityLevel;
  char* fUserAgentHeaderStr;
  int fSocketNum;
  unsigned fServerAddress;
  unsigned fCSeq; // sequence number, used in consecutive requests
  char* fBaseURL;
  AuthRecord* fCurrentAuthenticator; // if any
  unsigned char fTCPStreamIdCount; // used for (optional) RTP/TCP
  char* fLastSessionId; // used to overcome DSS bogosity
};

#endif
