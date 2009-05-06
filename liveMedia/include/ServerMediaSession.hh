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
// A data structure that represents a session that consists of
// potentially multiple (audio and/or video) sub-sessions
// (This data structure is used for media *streamers* - i.e., servers.
//  For media receivers, use "MediaSession" instead.)
// C++ header

#ifndef _SERVER_MEDIA_SESSION_HH
#define _SERVER_MEDIA_SESSION_HH

#ifndef _RTP_SINK_HH
#include "RTPSink.hh"
#endif

class ServerMediaSubsession; // forward

class ServerMediaSession: public Medium {
public:
  static ServerMediaSession* createNew(UsageEnvironment& env,
				       char const* description = NULL,
				       char const* info = NULL);

  static Boolean lookupByName(UsageEnvironment& env,
			      char const* sourceName,
			      ServerMediaSession*& resultSession);

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

  char* generateSDPDescription(); // based on the entire session
      // Note: The caller is responsible for freeing the returned string

private: // redefined virtual functions
  virtual Boolean isServerMediaSession() const;

private:
  ServerMediaSession(UsageEnvironment& env, char const* info,
		     char const* description);
      // called only by createNew();
  virtual ~ServerMediaSession();

private:
  // Linkage fields:
  friend class ServerMediaSubsessionIterator;
  ServerMediaSubsession* fSubsessionsHead;
  ServerMediaSubsession* fSubsessionsTail;

  char* fDescriptionSDPString;
  char* fInfoSDPString;
  struct timeval fCreationTime;
  unsigned fSubsessionCounter;
};


class ServerMediaSubsessionIterator {
public:
  ServerMediaSubsessionIterator(ServerMediaSession& session);
  virtual ~ServerMediaSubsessionIterator();
  
  ServerMediaSubsession* next(); // NULL if none
  void reset();
  
private:
  ServerMediaSession& fOurSession;
  ServerMediaSubsession* fNextPtr;
};


class ServerMediaSubsession {
public:
  virtual ~ServerMediaSubsession();

  GroupEId const& groupEId() const { return fGroupEId; }
  char const* trackId() const { return fTrackId; }

private:
  ServerMediaSubsession(GroupEId const& groupEId,
			char const* trackId, char const* sdpLines);

  void setNext(ServerMediaSubsession* next) { fNext = next; }

private:
  friend class ServerMediaSession;
  friend class ServerMediaSubsessionIterator;
  ServerMediaSubsession* fNext;

  GroupEId const& fGroupEId;
  char const* fTrackId;
  char const* fSDPLines;
};

#endif
