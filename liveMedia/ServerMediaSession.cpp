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
// Implementation

#include "ServerMediaSession.hh"
#include "GroupsockHelper.hh"

#if defined(__WIN32__) || defined(_WIN32)
#define snprintf _snprintf
#endif

////////// ServerMediaSession //////////

Boolean ServerMediaSession::lookupByName(UsageEnvironment& env,
				   char const* instanceName,
				   ServerMediaSession*& resultSession) {
  resultSession = NULL; // unless we succeed

  Medium* medium;
  if (!Medium::lookupByName(env, instanceName, medium)) return False;

  if (!medium->isServerMediaSession()) {
    env.setResultMsg(instanceName, " is not a 'ServerMediaSession' object");
    return False;
  }

  resultSession = (ServerMediaSession*)medium;
  return True;
}

#ifdef BSD
static struct timezone Idunno;
#else
static int Idunno;
#endif

static char const* const libraryNameString = "LIVE.COM Streaming Media";

ServerMediaSession::ServerMediaSession(UsageEnvironment& env,
				       char const* description,
				       char const* info)
  : Medium(env), fSubsessionsHead(NULL), fSubsessionsTail(NULL),
    fSubsessionCounter(0) {
  fDescriptionSDPString
    = strdup(description == NULL ? libraryNameString : description);
  fInfoSDPString = strdup(info == NULL ? libraryNameString : info);

  gettimeofday(&fCreationTime, &Idunno);
}

ServerMediaSession::~ServerMediaSession() {
  delete fSubsessionsHead;
  delete fDescriptionSDPString; delete fInfoSDPString;
}

Boolean ServerMediaSession::isServerMediaSession() const {
  return True;
}

char* ServerMediaSession::generateSDPDescription() {
  char const* sdpPrefixFormat = "v=0\r\no=- %d%06d %d IN IP4 %s\r\ns=%s\r\ni=%s\r\nt=0 0\r\na=tool:%s\r\na=type:broadcast\r\n";

  struct in_addr ourIPAddress;
  ourIPAddress.s_addr = ourSourceAddressForMulticast(envir());
  char ourIPAddressStr[100];
  strncpy(ourIPAddressStr, our_inet_ntoa(ourIPAddress),
          sizeof ourIPAddressStr - 1);

  // Compute how much space to allocate for the result SDP description:
  unsigned sdpLength = strlen(sdpPrefixFormat) + strlen(ourIPAddressStr)
    + strlen(fDescriptionSDPString) + strlen(fInfoSDPString)
    + strlen(libraryNameString) + 20 /*slop*/;
  ServerMediaSubsession* subsession;
  for (subsession = fSubsessionsHead; subsession != NULL;
       subsession = subsession->fNext) {
    sdpLength += strlen(subsession->fSDPLines);
  }

  char* sdp = new char[sdpLength];
  if (sdp == NULL) return sdp;

  // Generate the SDP prefix (session-level lines):
  sprintf(sdp, sdpPrefixFormat,
	  fCreationTime.tv_sec, fCreationTime.tv_usec, // o= <session id>
	  1, // o= <version> // (needs to change if params are modified)
	  ourIPAddressStr, // o= <address>
	  fDescriptionSDPString, // s= <description>
	  fInfoSDPString, // i= <info>
	  libraryNameString); // a=tool:

  // Then, add the (media-level) lines for each subsession:
  char* mediaSDP = sdp;
  for (subsession = fSubsessionsHead; subsession != NULL;
       subsession = subsession->fNext) {
    mediaSDP += strlen(mediaSDP);
    sprintf(mediaSDP, "%s", subsession->fSDPLines);
  }

  return sdp;
}


////////// ServerMediaSessionIterator //////////

ServerMediaSubsessionIterator
::ServerMediaSubsessionIterator(ServerMediaSession& session)
  : fOurSession(session) {
  reset();
}

ServerMediaSubsessionIterator::~ServerMediaSubsessionIterator() {
}

ServerMediaSubsession* ServerMediaSubsessionIterator::next() {
  ServerMediaSubsession* result = fNextPtr;

  if (fNextPtr != NULL) fNextPtr = fNextPtr->fNext;

  return result;
}

void ServerMediaSubsessionIterator::reset() {
  fNextPtr = fOurSession.fSubsessionsHead;
}


////////// ServerMediaSubsession //////////

ServerMediaSubsession
::ServerMediaSubsession(GroupEId const& groupEId,
			char const* trackId, char const* sdpLines)
  : fNext(NULL), fGroupEId(groupEId),
    fTrackId(trackId), fSDPLines(sdpLines) {
}

ServerMediaSubsession::~ServerMediaSubsession() {
  delete (char*)fTrackId;
  delete (char*)fSDPLines;
}
