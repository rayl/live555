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
// A 'ServerMediaSubsession' object that creates new, unicast, "RTPSink"s
// on demand, from a MP3 audio file.
// (Actually, any MPEG-1 or MPEG-2 audio file should work.)
// Implementation

#include "MP3AudioFileServerMediaSubsession.hh"
#include "MPEG1or2AudioRTPSink.hh"
#include "MP3ADURTPSink.hh"
#include "MP3FileSource.hh"
#include "MP3ADU.hh"

MP3AudioFileServerMediaSubsession* MP3AudioFileServerMediaSubsession
::createNew(UsageEnvironment& env, char const* fileName, Boolean reuseFirstSource,
	    Boolean useADUs, Interleaving* interleaving) {
  return new MP3AudioFileServerMediaSubsession(env, fileName, reuseFirstSource,
					       useADUs, interleaving);
}

MP3AudioFileServerMediaSubsession
::MP3AudioFileServerMediaSubsession(UsageEnvironment& env,
				    char const* fileName, Boolean reuseFirstSource,
				    Boolean useADUs,
				    Interleaving* interleaving)
  : FileServerMediaSubsession(env, fileName, reuseFirstSource),
    fUseADUs(useADUs), fInterleaving(interleaving), fFileDuration(0.0) {
}

MP3AudioFileServerMediaSubsession
::~MP3AudioFileServerMediaSubsession() {
  delete fInterleaving;
}

FramedSource* MP3AudioFileServerMediaSubsession
::createNewStreamSource(unsigned /*clientSessionId*/, unsigned& estBitrate) {
  estBitrate = 128; // kbps, estimate

  FramedSource* streamSource;
  do {
    MP3FileSource* mp3Source;
    streamSource = mp3Source = MP3FileSource::createNew(envir(), fFileName);
    if (streamSource == NULL) break;
    fFileDuration = mp3Source->filePlayTime();

    if (fUseADUs) {
      // Add a filter that converts the source MP3s to ADUs:
      streamSource = ADUFromMP3Source::createNew(envir(), streamSource);
      if (streamSource == NULL) break;

      if (fInterleaving != NULL) {
	// Add another filter that interleaves the ADUs before packetizing:
	streamSource = MP3ADUinterleaver::createNew(envir(), *fInterleaving,
						    streamSource);
	if (streamSource == NULL) break;
      }
    }
  } while (0);

  return streamSource;
}

RTPSink* MP3AudioFileServerMediaSubsession
::createNewRTPSink(Groupsock* rtpGroupsock,
		   unsigned char rtpPayloadTypeIfDynamic,
		   FramedSource* /*inputSource*/) {
  if (fUseADUs) {
    return MP3ADURTPSink::createNew(envir(), rtpGroupsock,
				    rtpPayloadTypeIfDynamic);
  } else {
    return MPEG1or2AudioRTPSink::createNew(envir(), rtpGroupsock);
  }
}

float MP3AudioFileServerMediaSubsession::duration() const {
  return fFileDuration;
}
