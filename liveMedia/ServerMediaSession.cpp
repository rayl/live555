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
// Implementation

#include "ServerMediaSession.hh"
#include <GroupsockHelper.hh>

////////// ServerMediaSession //////////

ServerMediaSession* ServerMediaSession
::createNew(UsageEnvironment& env,
	    char const* streamName, char const* info,
	    char const* description, Boolean isSSM, char const* miscSDPLines) {
  return new ServerMediaSession(env, streamName, info, description,
				isSSM, miscSDPLines);
}

Boolean ServerMediaSession
::lookupByName(UsageEnvironment& env, char const* mediumName,
	       ServerMediaSession*& resultSession) {
  resultSession = NULL; // unless we succeed

  Medium* medium;
  if (!Medium::lookupByName(env, mediumName, medium)) return False;

  if (!medium->isServerMediaSession()) {
    env.setResultMsg(mediumName, " is not a 'ServerMediaSession' object");
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

static char const* const libNameStr = "LIVE.COM Streaming Media v";
char const* const libVersionStr = LIVEMEDIA_LIBRARY_VERSION_STRING;

ServerMediaSession::ServerMediaSession(UsageEnvironment& env,
				       char const* streamName,
				       char const* info,
				       char const* description,
				       Boolean isSSM, char const* miscSDPLines)
  : Medium(env), fIsSSM(isSSM), fSubsessionsHead(NULL),
    fSubsessionsTail(NULL), fSubsessionCounter(0),
    fDuration(0.0), fReferenceCount(0), fDeleteWhenUnreferenced(False) {
  fStreamName = strDup(streamName == NULL ? "" : streamName);
  fInfoSDPString = strDup(info == NULL ? libNameStr : info);
  fDescriptionSDPString
    = strDup(description == NULL ? libNameStr : description);
  fMiscSDPLines = strDup(miscSDPLines == NULL ? "" : miscSDPLines);

  gettimeofday(&fCreationTime, &Idunno);
}

ServerMediaSession::~ServerMediaSession() {
  delete fSubsessionsHead;
  delete[] fStreamName;
  delete[] fInfoSDPString;
  delete[] fDescriptionSDPString;
  delete[] fMiscSDPLines;
}

Boolean
ServerMediaSession::addSubsession(ServerMediaSubsession* subsession) {
  if (subsession->fTrackNumber != 0) return False; // it's already used

  float subsessionDuration = subsession->duration();
  if (fSubsessionsHead == NULL) {
    // This is the first subsession.  Use its duration for the session duration:
    fDuration = subsessionDuration;
  } else if (subsessionDuration != fDuration) {
    // The subsessions have differing durations.
    // Note this by setting the session duration to a negative value:
    //     -(the largest subsession duration)
    if (fDuration > 0.0) fDuration = -fDuration;
    if (-subsessionDuration < fDuration) fDuration = -subsessionDuration;
  }

  if (fSubsessionsTail == NULL) {
    fSubsessionsHead = subsession;
  } else {
    fSubsessionsTail->fNext = subsession;
  }
  fSubsessionsTail = subsession;

  subsession->fTrackNumber = ++fSubsessionCounter;
  return True;
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
  if (fIsSSM) {
    char const* const sourceFilterFmt =
      "a=source-filter: incl IN IP4 * %s\r\n"
      "a=rtcp:unicast reflection\r\n";
    unsigned sourceFilterFmtSize = strlen(sourceFilterFmt)
      + ourIPAddressStrSize;

    sourceFilterLine = new char[sourceFilterFmtSize];
    sprintf(sourceFilterLine, sourceFilterFmt,
            ourIPAddressStr);
  } else {
    sourceFilterLine = strDup("");
  }

  // Unless subsessions have differing durations, we also have a "a=range:" line:
  char* rangeLine;
  if (fDuration == 0.0) {
    rangeLine = strDup("a=range:npt=0-\r\n");
  } else if (fDuration > 0.0) {
    char buf[100];
    sprintf(buf, "a=range:npt=0-%.3f\r\n", fDuration);
    rangeLine = strDup(buf);
  } else { // subsessions have differing durations, so "a=range:" lines go there
    rangeLine = strDup("");
  }

  char const* const sdpPrefixFmt =
    "v=0\r\n"
    "o=- %ld%06ld %d IN IP4 %s\r\n"
    "s=%s\r\n"
    "i=%s\r\n"
    "t=0 0\r\n"
    "a=tool:%s%s\r\n"
    "a=type:broadcast\r\n"
    "a=control:*\r\n"
    "%s"
    "%s"
    "a=x-qt-text-nam:%s\r\n"
    "a=x-qt-text-inf:%s\r\n"
    "%s";
  unsigned sdpLength = strlen(sdpPrefixFmt)
    + 20 + 6 + 20 + ourIPAddressStrSize
    + strlen(fDescriptionSDPString)
    + strlen(fInfoSDPString)
    + strlen(libNameStr) + strlen(libVersionStr)
    + strlen(sourceFilterLine)
    + strlen(rangeLine)
    + strlen(fDescriptionSDPString)
    + strlen(fInfoSDPString)
    + strlen(fMiscSDPLines);
  char* sdp = NULL; // for now

  do {
    // Add in the lengths of each subsession's media-level SDP lines: 
    ServerMediaSubsession* subsession;
    for (subsession = fSubsessionsHead; subsession != NULL;
	 subsession = subsession->fNext) {
      char const* sdpLines = subsession->sdpLines(*this);
      if (sdpLines == NULL) break; // the media's not available
      sdpLength += strlen(sdpLines);
    }
    if (subsession != NULL) break; // an error occurred

    sdp = new char[sdpLength];
    if (sdp == NULL) break;

    // Generate the SDP prefix (session-level lines):
    sprintf(sdp, sdpPrefixFmt,
	    fCreationTime.tv_sec, fCreationTime.tv_usec, // o= <session id>
	    1, // o= <version> // (needs to change if params are modified)
	    ourIPAddressStr, // o= <address>
	    fDescriptionSDPString, // s= <description>
	    fInfoSDPString, // i= <info>
	    libNameStr, libVersionStr, // a=tool:
	    sourceFilterLine, // a=source-filter: incl (if a SSM session)
	    rangeLine, // a=range: line
	    fDescriptionSDPString, // a=x-qt-text-nam: line
	    fInfoSDPString, // a=x-qt-text-inf: line
	    fMiscSDPLines); // miscellaneous session SDP lines (if any)

    // Then, add the (media-level) lines for each subsession:
    char* mediaSDP = sdp;
    for (subsession = fSubsessionsHead; subsession != NULL;
	 subsession = subsession->fNext) {
      mediaSDP += strlen(mediaSDP);
      sprintf(mediaSDP, "%s", subsession->sdpLines(*this));
    }
  } while (0);

  delete[] sourceFilterLine; delete[] ourIPAddressStr;
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

ServerMediaSubsession::ServerMediaSubsession(UsageEnvironment& env)
  : Medium(env),
    fNext(NULL), fTrackNumber(0), fTrackId(NULL) {
}

ServerMediaSubsession::~ServerMediaSubsession() {
  delete[] (char*)fTrackId;
  delete fNext;
}

char const* ServerMediaSubsession::trackId() {
  if (fTrackNumber == 0) return NULL; // not yet in a ServerMediaSession

  if (fTrackId == NULL) {
    char buf[100];
    sprintf(buf, "track%d", fTrackNumber);
    fTrackId = strDup(buf);
  }
  return fTrackId;
}

void ServerMediaSubsession::pauseStream(unsigned /*clientSessionId*/,
					void* /*streamToken*/) {
  // default implementation: do nothing
}
void ServerMediaSubsession::deleteStream(unsigned /*clientSessionId*/,
					 void*& /*streamToken*/) {
  // default implementation: do nothing
}

float ServerMediaSubsession::duration() const {
  // default implementation: assume an unbounded session:
  return 0.0;
}

char const*
ServerMediaSubsession::rangeSDPLine(ServerMediaSession& parentSession) const {
  // If all of our parent's subsessions have the same duration
  // (as indicated by "parentSession.duration() < 0"), there's no "a=range:" line:
  if (parentSession.duration() >= 0.0) return strDup("");

  // Use our own duration for a "a=range:" line:
  float ourDuration = duration();
  if (ourDuration == 0.0) {
    return strDup("a=range:npt=0-\r\n");
  } else {
    char buf[100];
    sprintf(buf, "a=range:npt=0-%.3f\r\n", ourDuration);
    return strDup(buf);
  }  
}
