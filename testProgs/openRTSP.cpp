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

char* getSDPDescriptionFromURL(Medium* client, char const* url,
			       char const* username, char const* password,
			       char const* /*proxyServerName*/,
			       unsigned short /*proxyServerPortNum*/,
			       unsigned short /*clientStartPort*/) {
  RTSPClient* rtspClient = (RTSPClient*)client;
  if (username != NULL && password != NULL) {
    return rtspClient->describeWithPassword(url, username, password);
  } else {
    return rtspClient->describeURL(url);
  }
}

Boolean clientSetupSubsession(Medium* client, MediaSubsession* subsession,
			      Boolean streamUsingTCP) {
  RTSPClient* rtspClient = (RTSPClient*)client;
  return rtspClient->setupMediaSubsession(*subsession,
					  False, streamUsingTCP);
}

Boolean clientStartPlayingSubsession(Medium* client,
                                     MediaSubsession* subsession) {
  RTSPClient* rtspClient = (RTSPClient*)client;
  return rtspClient->playMediaSubsession(*subsession);
}

Boolean clientStartPlayingSession(Medium* /*client*/,
				  MediaSession* /*session*/) {
  // Do nothing; all the work's done by clientStartPlayingSubsession()
  return True;
}

Boolean clientTearDownSubsession(Medium* client,
                                 MediaSubsession* subsession) {
  RTSPClient* rtspClient = (RTSPClient*)client;
  return rtspClient->teardownMediaSubsession(*subsession);
}

Boolean clientTearDownSession(Medium* /*client*/,
			      MediaSession* /*session*/) {
  // Do nothing; all the work's done by clientTearDownSubsession()
  return True;
}

Boolean allowProxyServers = False;
Boolean controlConnectionUsesTCP = True;
char const* clientProtocolName = "RTSP";
