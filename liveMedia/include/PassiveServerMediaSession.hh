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
// A 'ServerMediaSession' object that represents an existing 'RTPSink',
// rather than one that creates new 'RTPSink's (e.g., to handle RTSP
// commands).
// C++ header

#ifndef _PASSIVE_SERVER_MEDIA_SESSION_HH
#define _PASSIVE_SERVER_MEDIA_SESSION_HH

#ifndef _SERVER_MEDIA_SESSION_HH
#include "ServerMediaSession.hh"
#endif

class PassiveServerMediaSession: public ServerMediaSession {
public:
  static PassiveServerMediaSession*
  createNew(UsageEnvironment& env, char const* description = NULL,
	    char const* info = NULL);

  void addSubsession(RTPSink& rtpSink);
  void addSubsessionByComponents(struct in_addr const& ipAddress,
				 unsigned short portNum /* host order */,
				 unsigned char ttl,
				 unsigned rtpTimestampFrequency,
				 unsigned char rtpPayloadType,
				 char const* mediaType,
				 char const* rtpPayloadFormatName);
      // As an alternative to specifying a subsession by adding a RTP Sink,
      // you can call this routine to specify it by its components.

private:
  PassiveServerMediaSession(UsageEnvironment& env, char const* info,
		     char const* description);
      // called only by createNew();
  virtual ~PassiveServerMediaSession();
};

#endif
