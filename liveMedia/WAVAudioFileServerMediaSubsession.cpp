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
// on demand, from an WAV audio file.
// Implementation

#include "WAVAudioFileServerMediaSubsession.hh"
#include "WAVAudioFileSource.hh"
#include "uLawAudioFilter.hh"
#include "SimpleRTPSink.hh"

WAVAudioFileServerMediaSubsession* WAVAudioFileServerMediaSubsession
::createNew(UsageEnvironment& env, char const* fileName,
	    Boolean convertToULaw) {
  return new WAVAudioFileServerMediaSubsession(env, fileName, convertToULaw);
}

WAVAudioFileServerMediaSubsession
::WAVAudioFileServerMediaSubsession(UsageEnvironment& env, char const* fileName,
				    Boolean convertToULaw)
  : FileServerMediaSubsession(env, fileName),
    fConvertToULaw(convertToULaw) {
}

WAVAudioFileServerMediaSubsession
::~WAVAudioFileServerMediaSubsession() {
}

FramedSource* WAVAudioFileServerMediaSubsession
::createNewStreamSource(unsigned /*clientSessionId*/, unsigned& estBitrate) {
  FramedSource* resultSource = NULL;
  do {
    WAVAudioFileSource* pcmSource
      = WAVAudioFileSource::createNew(envir(), fFileName);
    if (pcmSource == NULL) break;

    // Get attributes of the audio source:
    fBitsPerSample = pcmSource->bitsPerSample();
    if (fBitsPerSample != 8 && fBitsPerSample !=  16) {
      envir() << "The input file contains " << fBitsPerSample
	      << " bit-per-sample audio, which we don't handle\n";
      break;
    }
    fSamplingFrequency = pcmSource->samplingFrequency();
    fNumChannels = pcmSource->numChannels();
    unsigned bitsPerSecond
      = fSamplingFrequency*fBitsPerSample*fNumChannels;

    // Add in any filter necessary to transform the data prior to streaming:
    if (fBitsPerSample == 16) {
      if (fConvertToULaw) {
	// Add a filter that converts from raw 16-bit PCM audio
	// to 8-bit u-law audio:
	resultSource
	  = uLawFromPCMAudioSource::createNew(envir(), pcmSource);
	bitsPerSecond /= 2;
      } else {
	// Add a filter that converts from host to network order: 
	resultSource = NetworkFromHostOrder16::createNew(envir(), pcmSource);
      }
    } else { // fBitsPerSample == 8
      // Don't do any transformation; send the 8-bit PCM data 'as is':
      resultSource = pcmSource;
    }

    estBitrate = (bitsPerSecond+500)/1000; // kbps
    return resultSource;
  } while (0);

  // An error occurred:
  Medium::close(resultSource);
  return NULL;
}

RTPSink* WAVAudioFileServerMediaSubsession
::createNewRTPSink(Groupsock* rtpGroupsock,
		   unsigned char rtpPayloadTypeIfDynamic,
		   FramedSource* /*inputSource*/) {
  do {
    char* mimeType;
    unsigned char payloadFormatCode;
    if (fBitsPerSample == 16) {
      if (fConvertToULaw) {
	mimeType = "PCMU";
	if (fSamplingFrequency == 8000 && fNumChannels == 1) {
	  payloadFormatCode = 0; // a static RTP payload type
	} else {
	  payloadFormatCode = 96; // a dynamic RTP payload type
	}
      } else {
	mimeType = "L16";
	if (fSamplingFrequency == 44100 && fNumChannels == 2) {
	  payloadFormatCode = 10; // a static RTP payload type
	} else if (fSamplingFrequency == 44100 && fNumChannels == 1) {
	  payloadFormatCode = 11; // a static RTP payload type
	} else {
	  payloadFormatCode = 96; // a dynamic RTP payload type
	}
      }
    } else { // fBitsPerSample == 8
      mimeType = "L8";
      payloadFormatCode = 96; // a dynamic RTP payload type
    }

    return SimpleRTPSink::createNew(envir(), rtpGroupsock,
				    payloadFormatCode, fSamplingFrequency,
				    "audio", mimeType, fNumChannels);
  } while (0);

  // An error occurred:
  return NULL;
}
