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
// (This data structure is used for media *receivers* - i.e., clients.
//  For media streamers, use "ServerMediaSession" instead.)
// C++ header

#ifndef _MEDIA_SESSION_HH
#define _MEDIA_SESSION_HH

#ifndef _RTCP_HH
#include "RTCP.hh"
#endif
#ifndef _PRIORITIZED_RTP_STREAM_SELELECTOR_HH
#include "PrioritizedRTPStreamSelector.hh"
#endif

class MediaSubsession; // forward

class MediaSession: public Medium {
public:
  static MediaSession* createNew(UsageEnvironment& env,
				 char const* sdpDescription);

  static Boolean lookupByName(UsageEnvironment& env, char const* sourceName,
			      MediaSession*& resultSession);

  Boolean hasSubsessions() const { return fSubsessionsHead != NULL; }
  float& playEndTime() { return fMaxPlayEndTime; }
  char* connectionEndpointName() const { return fConnectionEndpointName; }
  char const* CNAME() const { return fCNAME; }

  Boolean initiateByMediaType(char const* mimeType,
			      MediaSubsession*& resultSubsession,
		      PrioritizedRTPStreamSelector*& resultMultiSource,
			      int& resultMultiSourceSessionId,
			      int useSpecialRTPoffset = -1);
      // Initiates the first subsession with the specified MIME type (or
      // perhaps multiple subsessions if MCT SLAP sessions are being used)
      // Returns the resulting subsession, or 'multi source' (not both)

private: // redefined virtual functions
  virtual Boolean isMediaSession() const;

private:
  MediaSession(UsageEnvironment& env);
      // called only by createNew();
  virtual ~MediaSession();

  Boolean initializeWithSDP(char const* sdpDescription);
  Boolean parseSDPLine(char const* input, char const*& nextLine);
  Boolean parseSDPLine_c(char const* sdpLine);
  Boolean parseSDPAttribute_range(char const* sdpLine);

  static char* lookupPayloadFormat(unsigned char rtpPayloadType,
				   unsigned& rtpTimestampFrequency);
  static unsigned guessRTPTimestampFrequency(char const* mediumName,
					     char const* codecName);

private:
  friend class MediaSubsessionIterator;
  char* fCNAME; // used for RTCP

  // Linkage fields:
  MediaSubsession* fSubsessionsHead;
  MediaSubsession* fSubsessionsTail;

  // Fields set from a SDP description:
  char* fConnectionEndpointName;
  float fMaxPlayEndTime;
};


class MediaSubsessionIterator {
public:
  MediaSubsessionIterator(MediaSession& session);
  virtual ~MediaSubsessionIterator();
  
  MediaSubsession* next(); // NULL if none
  void reset();
  
private:
  MediaSession& fOurSession;
  MediaSubsession* fNextPtr;
};


class MediaSubsession {
public:
  MediaSession& parentSession() { return fParent; }
  MediaSession const& parentSession() const { return fParent; }

  unsigned short clientPortNum() const { return fClientPortNum; }
  char const* mediumName() const { return fMediumName; }
  char const* codecName() const { return fCodecName; }
  char const* controlPath() const { return fControlPath; }
  int mctSLAPSessionId() const { return fMCT_SLAP_SessionId; }
  unsigned mctSLAPStagger() const { return fMCT_SLAP_Stagger; }
  unsigned short videoWidth() const { return fVideoWidth; }
  unsigned short videoHeight() const { return fVideoHeight; }
  unsigned videoFPS() const { return fVideoFPS; }

  RTPSource* rtpSource() { return fRTPSource; }
  RTCPInstance* rtcpInstance() { return fRTCPInstance; }
  unsigned rtpTimestampFrequency() const { return fRTPTimestampFrequency; }
  FramedSource* readSource() { return fReadSource; }
    // This is the source that client sinks read from.  It is usually
    // (but not necessarily) the same as "rtpSource()"

  float playEndTime() const;

  Boolean initiate(int useSpecialRTPoffset = -1);
      // Creates a "RTPSource" for this subsession. (Has no effect if it's
      // already been created.)  Returns True iff this succeeds.
  void deInitiate(); // Destroys any previously created RTPSource
  Boolean setClientPortNum(unsigned short portNum);
      // Sets the preferred client port number that any "RTPSource" for
      // this subsession would use.  (By default, the client port number
      // is gotten from the original SDP description, or - if the SDP
      // description does not specfy a client port number - an ephemeral
      // (even) port number is chosen.)  This routine should *not* be
      // called after initiate().
  char*& connectionEndpointName() { return fConnectionEndpointName; }
  char const* connectionEndpointName() const {
    return fConnectionEndpointName;
  }

  // Public fields that external callers can use to keep state.
  // (They are responsible for all storage management on these fields)
  char const* sessionId; // used by RTSP
  unsigned short serverPortNum; // in host byte order (used by RTSP)
  unsigned char rtpChannelId, rtcpChannelId; // used by RTSP (for RTP/TCP)
  MediaSink* sink; // callers can use this to keep track of who's playing us
  void* miscPtr; // callers can use this for whatever they want

  unsigned connectionEndpointAddress() const;
      // Converts "fConnectionEndpointName" to an address (or 0 if unknown)
  void setDestinations(unsigned defaultDestAddress);
      // Uses "fConnectionEndpointName" and "serverPortNum" to set
      // the destination address and port of the RTP and RTCP objects.
      // This is typically called by RTSP clients after doing "SETUP".

private:
  friend class MediaSession;
  friend class MediaSubsessionIterator;
  MediaSubsession(MediaSession& parent);
  virtual ~MediaSubsession();

  UsageEnvironment& env() { return fParent.envir(); }
  void setNext(MediaSubsession* next) { fNext = next; }

  Boolean parseSDPLine_c(char const* sdpLine);
  Boolean parseSDPAttribute_rtpmap(char const* sdpLine);
  Boolean parseSDPAttribute_control(char const* sdpLine);
  Boolean parseSDPAttribute_range(char const* sdpLine);
  Boolean parseSDPAttribute_x_mct_slap(char const* sdpLine);
  Boolean parseSDPAttribute_x_dimensions(char const* sdpLine);
  Boolean parseSDPAttribute_x_framerate(char const* sdpLine);

private:
  // Linkage fields:
  MediaSession& fParent;
  MediaSubsession* fNext;

  // Fields set from a SDP description:
  char* fConnectionEndpointName; // may also be set by RTSP SETUP response
  unsigned short fClientPortNum; // in host byte order
      // This field is also set by initiate()
  unsigned char fRTPPayloadFormat;
  char* fMediumName;
  char* fCodecName;
  unsigned fRTPTimestampFrequency;
  char* fControlPath;
  float fPlayEndTime;
  int fMCT_SLAP_SessionId; // 0 if not part of a MCT SLAP session
  unsigned fMCT_SLAP_Stagger; // seconds (used only if the above is != 0)
  unsigned short fVideoWidth, fVideoHeight;
     // screen dimensions (set by an optional a=x-dimensions: <w>,<h> line)
  unsigned fVideoFPS;
     // frame rate (set by an optional a=x-framerate: <fps> line)

  // Fields set by initiate():
  Groupsock* fRTPSocket; Groupsock* fRTCPSocket; // works even for unicast
  RTPSource* fRTPSource; RTCPInstance* fRTCPInstance;
  FramedSource* fReadSource;
};

#endif
