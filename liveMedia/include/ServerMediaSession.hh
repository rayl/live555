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
// A data structure that represents a session that consists of
// potentially multiple (audio and/or video) sub-sessions
// (This data structure is used for media *streamers* - i.e., servers.
//  For media receivers, use "MediaSession" instead.)
// C++ header

#ifndef _SERVER_MEDIA_SESSION_HH
#define _SERVER_MEDIA_SESSION_HH

#ifndef _MEDIA_HH
#include "Media.hh"
#endif
#ifndef _GROUPEID_HH
#include "GroupEId.hh"
#endif

class ServerMediaSubsession; // forward

class ServerMediaSession: public Medium {
public:
  static ServerMediaSession* createNew(UsageEnvironment& env,
				       char const* streamName = NULL,
				       char const* info = NULL,
				       char const* description = NULL,
				       Boolean isSSM = False);
			       

  static Boolean lookupByName(UsageEnvironment& env,
                              char const* mediumName,
                              ServerMediaSession*& resultSession);

  char* generateSDPDescription(); // based on the entire session
      // Note: The caller is responsible for freeing the returned string

  char const* streamName() const { return fStreamName; }

  virtual ~ServerMediaSession();

  Boolean addSubsession(ServerMediaSubsession* subsession);

private:
  ServerMediaSession(UsageEnvironment& env, char const* streamName,
		     char const* info, char const* description,
		     Boolean isSSM);
  // called only by "createNew()"

private: // redefined virtual functions
  virtual Boolean isServerMediaSession() const;

private:
  Boolean fIsSSM;

  // Linkage fields:
  friend class ServerMediaSubsessionIterator;
  ServerMediaSubsession* fSubsessionsHead;
  ServerMediaSubsession* fSubsessionsTail;
  unsigned fSubsessionCounter;

  char* fStreamName;
  char* fInfoSDPString;
  char* fDescriptionSDPString;
  struct timeval fCreationTime;
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


class ServerMediaSubsession: public Medium {
public:
  virtual ~ServerMediaSubsession();

  unsigned trackNumber() const { return fTrackNumber; }
  char const* trackId();
  virtual char const* sdpLines() = 0;
  virtual void getStreamParameters(netAddressBits clientAddress, // in
				   Port const& clientRTPPort, // in
				   Port const& clientRTCPPort, // in
				   Boolean& isMulticast, // out
				   netAddressBits destinationAddress,
				   u_int8_t destinationTTL,
				       // out; used only for multicast
				   Port& serverRTPPort, // out
				   Port& serverRTCPPort, // out; used only for unicast
				   void*& streamToken // out
				   ) = 0;
  virtual void startStream(void* streamToken);
  virtual void pauseStream(void* streamToken);
  virtual void endStream(void* streamToken);
  virtual void deleteStream(void* streamToken);

protected: // we're a virtual base class
  ServerMediaSubsession(UsageEnvironment& env);

private:
  friend class ServerMediaSession;
  friend class ServerMediaSubsessionIterator;
  ServerMediaSubsession* fNext;

  unsigned fTrackNumber; // within an enclosing ServerMediaSession
  char const* fTrackId;
};

#endif
