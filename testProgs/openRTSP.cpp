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
// Copyright (c) 1996-2003, Live Networks, Inc.  All rights reserved
// A RTSP client test program that opens a RTSP URL argument,
// and extracts the data from each incoming RTP stream.

#include "playCommon.hh"

Medium* createClient(UsageEnvironment& env,
                     int verbosityLevel, char const* applicationName) {
  return RTSPClient::createNew(env, verbosityLevel, applicationName);
}

char* getOptionsResponse(Medium* client, char const* url) {
  RTSPClient* rtspClient = (RTSPClient*)client;
  return rtspClient->sendOptionsCmd(url);
}

char* getSDPDescriptionFromURL(Medium* client, char const* url,
			       char const* username, char const* password,
			       char const* /*proxyServerName*/,
			       unsigned short /*proxyServerPortNum*/,
			       unsigned short /*clientStartPort*/) {
  RTSPClient* rtspClient = (RTSPClient*)client;
  char* result;
  if (username != NULL && password != NULL) {
    result = rtspClient->describeWithPassword(url, username, password);
  } else {
    result = rtspClient->describeURL(url);
  }

  extern unsigned statusCode;
  statusCode = rtspClient->describeStatus();
#ifdef SUPPORT_REAL_RTSP
  extern Boolean isRealNetworksSession;
  isRealNetworksSession = rtspClient->isRealNetworksSession();
#endif
  return result;
}

Boolean clientSetupSubsession(Medium* client, MediaSubsession* subsession,
			      Boolean streamUsingTCP) {
  if (client == NULL || subsession == NULL) return False;
  RTSPClient* rtspClient = (RTSPClient*)client;
  return rtspClient->setupMediaSubsession(*subsession,
					  False, streamUsingTCP);
}

Boolean clientStartPlayingSession(Medium* client,
				  MediaSession* session) {
  if (client == NULL || session == NULL) return False;
  RTSPClient* rtspClient = (RTSPClient*)client;
  return rtspClient->playMediaSession(*session);
}

Boolean clientTearDownSession(Medium* client,
			      MediaSession* session) {
  if (client == NULL || session == NULL) return False;
  RTSPClient* rtspClient = (RTSPClient*)client;
  return rtspClient->teardownMediaSession(*session);
}

Boolean allowProxyServers = False;
Boolean controlConnectionUsesTCP = True;
Boolean supportCodecSelection = False;
char const* clientProtocolName = "RTSP";
