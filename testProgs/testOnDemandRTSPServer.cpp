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
// Copyright (c) 1996-2004, Live Networks, Inc.  All rights reserved
// A test program that demonstrates how to stream - via unicast RTP
// - various kinds of file on demand, using a built-in RTSP server.
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

UsageEnvironment* env;

// To stream *only* MPEG-1 or 2 video "I" frames
// (e.g., to reduce network bandwidth),
// change the following "False" to "True":
Boolean iFramesOnly = False;

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  // Create the RTSP server:
  RTSPServer* rtspServer = RTSPServer::createNew(*env, 7070);
  if (rtspServer == NULL) {
    *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
    exit(1);
  }
  char const* descriptionString
    = "Session streamed by \"testOnDemandRTSPServer\"";

  // Set up each of the possible streams that can be served by the
  // RTSP server.  Each such stream is implemented using a
  // "ServerMediaSession" object, plus one or more
  // "ServerMediaSubsession" objects for each audio/video substream.

  // A MPEG-4 video elementary stream:
  {
    char const* streamName = "mpeg4ESVideoTest";
    char const* inputFileName = "test.m4v";
    ServerMediaSession* sms
      = ServerMediaSession::createNew(*env, streamName, streamName,
				      descriptionString);
    sms->addSubsession(MPEG4VideoFileServerMediaSubsession
		       ::createNew(*env, inputFileName));
    rtspServer->addServerMediaSession(sms);

    char* url = rtspServer->rtspURL(sms);
    *env << "\n\"" << streamName << "\" stream, from the file \""
	 << inputFileName << "\"\n";
    *env << "Play this stream using the URL \"" << url << "\"\n";
    delete[] url;
  }

  // A MPEG-1 or 2 audio+video program stream:
  {
    char const* streamName = "mpeg1or2AudioVideoTest";
    char const* inputFileName = "test.mpg";
    // NOTE: This *must* be a Program Stream; not an Elementary Stream
    ServerMediaSession* sms
      = ServerMediaSession::createNew(*env, streamName, streamName,
				      descriptionString);
    MPEG1or2FileServerDemux* demux
      = MPEG1or2FileServerDemux::createNew(*env, inputFileName);
    sms->addSubsession(demux->newVideoServerMediaSubsession(iFramesOnly));
    sms->addSubsession(demux->newAudioServerMediaSubsession());
    rtspServer->addServerMediaSession(sms);

    char* url = rtspServer->rtspURL(sms);
    *env << "\n\"" << streamName << "\" stream, from the file \""
	 << inputFileName << "\"\n";
    *env << "Play this stream using the URL \"" << url << "\"\n";
    delete[] url;
  }

  // A MPEG-1 or 2 video elementary stream:
  {
    char const* streamName = "mpeg1or2ESVideoTest";
    char const* inputFileName = "testv.mpg";
    // NOTE: This *must* be a Video Elementary Stream; not a Program Stream
    ServerMediaSession* sms
      = ServerMediaSession::createNew(*env, streamName, streamName,
				      descriptionString);
    sms->addSubsession(MPEG1or2VideoFileServerMediaSubsession
		       ::createNew(*env, inputFileName, iFramesOnly));
    rtspServer->addServerMediaSession(sms);

    char* url = rtspServer->rtspURL(sms);
    *env << "\n\"" << streamName << "\" stream, from the file \""
	 << inputFileName << "\"\n";
    *env << "Play this stream using the URL \"" << url << "\"\n";
    delete[] url;
  }

  // A MP3 audio stream (actually, any MPEG-1 or 2 audio file will work):
  // To stream using 'ADUs' rather than raw MP3 frames, uncomment the following:
//#define STREAM_USING_ADUS 1
  // To also reorder ADUs before streaming, uncomment the following:
//#define INTERLEAVE_ADUS 1
  // (For more information about ADUs and interleaving,
  //  see <http://www.live.com/rtp-mp3/>)
  {
    char const* streamName = "mp3AudioTest";
    char const* inputFileName = "test.mp3";
    ServerMediaSession* sms
      = ServerMediaSession::createNew(*env, streamName, streamName,
				      descriptionString);
    Boolean useADUs = False;
    Interleaving* interleaving = NULL;
#ifdef STREAM_USING_ADUS 
    useADUs = True;
#ifdef INTERLEAVE_ADUS
    unsigned char interleaveCycle[] = {0,2,1,3}; // or choose your own...
    unsigned const interleaveCycleSize
      = (sizeof interleaveCycle)/(sizeof (unsigned char));
    interleaving = new Interleaving(interleaveCycleSize, interleaveCycle); 
#endif
#endif
    sms->addSubsession(MP3AudioFileServerMediaSubsession
		       ::createNew(*env, inputFileName,
				   useADUs, interleaving));
    rtspServer->addServerMediaSession(sms);

    char* url = rtspServer->rtspURL(sms);
    *env << "\n\"" << streamName << "\" stream, from the file \""
	 << inputFileName << "\"\n";
    *env << "Play this stream using the URL \"" << url << "\"\n";
    delete[] url;
  }

  // A WAV audio stream:
  {
    char const* streamName = "wavAudioTest";
    char const* inputFileName = "test.wav";
    ServerMediaSession* sms
      = ServerMediaSession::createNew(*env, streamName, streamName,
				      descriptionString);
    // To convert 16-bit PCM data to 8-bit u-law, prior to streaming,
    // change the following to True:
    Boolean convertToULaw = False;
    sms->addSubsession(WAVAudioFileServerMediaSubsession
		       ::createNew(*env, inputFileName, convertToULaw));
    rtspServer->addServerMediaSession(sms);

    char* url = rtspServer->rtspURL(sms);
    *env << "\n\"" << streamName << "\" stream, from the file \""
	 << inputFileName << "\"\n";
    *env << "Play this stream using the URL \"" << url << "\"\n";
    delete[] url;
  }

  // An AMR audio stream:
  {
    char const* streamName = "amrAudioTest";
    char const* inputFileName = "test.amr";
    ServerMediaSession* sms
      = ServerMediaSession::createNew(*env, streamName, streamName,
				      descriptionString);
    sms->addSubsession(AMRAudioFileServerMediaSubsession
		       ::createNew(*env, inputFileName));
    rtspServer->addServerMediaSession(sms);

    char* url = rtspServer->rtspURL(sms);
    *env << "\n\"" << streamName << "\" stream, from the file \""
	 << inputFileName << "\"\n";
    *env << "Play this stream using the URL \"" << url << "\"\n";
    delete[] url;
  }

  // A 'VOB' file (e.g., from an unencrypted DVD):
  {
    char const* streamName = "vobTest";
    char const* inputFileName = "test.vob";
    ServerMediaSession* sms
      = ServerMediaSession::createNew(*env, streamName, streamName,
				      descriptionString);
    // Note: VOB files are MPEG-2 Program Stream files, but using AC-3 audio
    MPEG1or2FileServerDemux* demux
      = MPEG1or2FileServerDemux::createNew(*env, inputFileName);
    sms->addSubsession(demux->newVideoServerMediaSubsession(iFramesOnly));
    sms->addSubsession(demux->newAC3AudioServerMediaSubsession());
    rtspServer->addServerMediaSession(sms);

    char* url = rtspServer->rtspURL(sms);
    *env << "\n\"" << streamName << "\" stream, from the file \""
	 << inputFileName << "\"\n";
    *env << "Play this stream using the URL \"" << url << "\"\n";
    delete[] url;
  }

  // A MPEG-2 Transport Stream:
  {
    char const* streamName = "mpeg2TransportStreamTest";
    char const* inputFileName = "test.ts";
    ServerMediaSession* sms
      = ServerMediaSession::createNew(*env, streamName, streamName,
				      descriptionString);
    sms->addSubsession(MPEG2TransportFileServerMediaSubsession
		       ::createNew(*env, inputFileName));
    rtspServer->addServerMediaSession(sms);

    char* url = rtspServer->rtspURL(sms);
    *env << "\n\"" << streamName << "\" stream, from the file \""
	 << inputFileName << "\"\n";
    *env << "Play this stream using the URL \"" << url << "\"\n";
    delete[] url;
  }

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}
