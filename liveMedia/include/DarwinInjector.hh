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
// An object that redirects one or more RTP/RTCP streams - forming a single
// multimedia session - into a 'Darwin Streaming Server' (for subsequent
// reflection to potentially arbitrarily many remote RTSP clients).
// C++ header

#ifndef _DARWIN_INJECTOR_HH
#define _DARWIN_INJECTOR_HH

#ifndef _RTSP_CLIENT_HH
#include <RTSPClient.hh>
#endif

#ifndef _RTCP_HH
#include <RTCP.hh>
#endif

// To use a "DarwinInjector":
//   1/ Create RTP sinks and RTCP instances for each audio or video subsession.
//      (Note: These can use 0.0.0.0 for the address of each 'groupsock')
//   2/ Call "addStream()" for each.
//   3/ Call "setDestination()" to specify the remote Darwin Streaming Server.
//   4/ Call "startPlaying" on each RTP sink (from the corresponding data source). 

class SubstreamDescriptor; // forward

class DarwinInjector: public Medium {
public:
  static DarwinInjector* createNew(UsageEnvironment& env,
				   char const* applicationName = NULL,
				   int verbosityLevel = 0);

  void addStream(RTPSink* rtpSink, RTCPInstance* rtcpInstance);

  Boolean setDestination(char const* remoteRTSPServerNameOrAddress, 
			 char const* remoteFileName,
			 char const* sessionName = "",
			 char const* sessionInfo = "",
			 portNumBits remoteRTSPServerPortNumber = 554,
			 char const* remoteUserName = "",
			 char const* remotePassword = "");

private:
  DarwinInjector(UsageEnvironment& env,
		 char const* applicationName, int verbosityLevel);
      // called only by createNew()

  virtual ~DarwinInjector();

private:
  char const* fApplicationName;
  int fVerbosityLevel;
  RTSPClient* fRTSPClient;
  unsigned fSubstreamSDPSizes;
  SubstreamDescriptor* fHeadSubstream;
  SubstreamDescriptor* fTailSubstream;
};

#endif
