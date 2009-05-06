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
				       char const* info,
				       Boolean isSSM)
  : Medium(env), fIsSSM(isSSM), fSubsessionsHead(NULL),
    fSubsessionsTail(NULL), fSubsessionCounter(0) {
  fDescriptionSDPString
    = strDup(description == NULL ? libraryNameString : description);
  fInfoSDPString = strDup(info == NULL ? libraryNameString : info);

  gettimeofday(&fCreationTime, &Idunno);
}

ServerMediaSession::~ServerMediaSession() {
  delete fSubsessionsHead;
  delete[] fDescriptionSDPString; delete[] fInfoSDPString;
}

Boolean ServerMediaSession::isServerMediaSession() const {
  return True;
}

char* ServerMediaSession::generateSDPDescription() {
  struct in_addr ourIPAddress;
  ourIPAddress.s_addr = ourSourceAddressForMulticast(envir());
  char* const ourIPAddressStr
    = strDup(our_inet_ntoa(ourIPAddress));
  unsigned ourIPAddressStrSize = strlen(ourIPAddressStr);

  // For a SSM sessions, we need a "a=source-filter: incl ..." line also:
  char* sourceFilterLine;
  unsigned sourceFilterLineSize;
  if (fIsSSM) {
    char const* const sourceFilterFmt =
      "a=source-filter: incl IN IP4 * %s\r\n"
      "a=rtcp:unicast reflection\r\n";
    unsigned sourceFilterFmtSize = strlen(sourceFilterFmt)
      + ourIPAddressStrSize;

    sourceFilterLine = new char[sourceFilterFmtSize];
    sprintf(sourceFilterLine, sourceFilterFmt,
            ourIPAddressStr);
    sourceFilterLineSize = strlen(sourceFilterLine);
  } else {
    sourceFilterLine = strDup("");
    sourceFilterLineSize = 0;
  }

  char const* const sdpPrefixFmt =
    "v=0\r\n"
    "o=- %ld%06ld %d IN IP4 %s\r\n"
    "s=%s\r\n"
    "i=%s\r\n"
    "t=0 0\r\n"
    "a=tool:%s\r\n"
    "a=type:broadcast\r\n"
    "%s";
  unsigned sdpLength = strlen(sdpPrefixFmt)
    + 20 + 6 + 20 + ourIPAddressStrSize
    + strlen(fDescriptionSDPString)
    + strlen(fInfoSDPString)
    + strlen(libraryNameString)
    + sourceFilterLineSize;

  // Add in the lengths of each subsession's media-level SDP lines: 
  ServerMediaSubsession* subsession;
  for (subsession = fSubsessionsHead; subsession != NULL;
       subsession = subsession->fNext) {
    sdpLength += strlen(subsession->fSDPLines);
  }

  char* sdp = new char[sdpLength];
  if (sdp == NULL) return sdp;

  // Generate the SDP prefix (session-level lines):
  sprintf(sdp, sdpPrefixFmt,
	  fCreationTime.tv_sec, fCreationTime.tv_usec, // o= <session id>
	  1, // o= <version> // (needs to change if params are modified)
	  ourIPAddressStr, // o= <address>
	  fDescriptionSDPString, // s= <description>
	  fInfoSDPString, // i= <info>
	  libraryNameString, // a=tool:
	  sourceFilterLine); // a=source-filter: incl (if a SSM session)
  delete[] sourceFilterLine; delete[] ourIPAddressStr;

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
    fTrackId(strDup(trackId)), fSDPLines(strDup(sdpLines)) {
}

ServerMediaSubsession::~ServerMediaSubsession() {
  delete[] (char*)fTrackId;
  delete[] (char*)fSDPLines;
}
