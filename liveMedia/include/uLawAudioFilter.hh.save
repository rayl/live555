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
// Copyright (c) 1996-2003 Live Networks, Inc.  All rights reserved.
// Filters for converting between raw PCM audio and uLaw
// C++ header

#ifndef _ULAW_AUDIO_FILTER_HH
#define _ULAW_AUDIO_FILTER_HH

#ifndef _FRAMED_FILTER_HH
#include "FramedFilter.hh"
#endif

////////// 16-bit PCM (in host order) -> 8-bit u-Law //////////

class uLawFromPCMAudioSource: public FramedFilter {
public:
  static uLawFromPCMAudioSource*
  createNew(UsageEnvironment& env, FramedSource* inputSource);

protected:
  uLawFromPCMAudioSource(UsageEnvironment& env,
			 FramedSource* inputSource);
      // called only by createNew()
  virtual ~uLawFromPCMAudioSource();

private:
  // Redefined virtual functions:
  virtual void doGetNextFrame();

private:
  static void afterGettingFrame(void* clientData, unsigned frameSize,
				struct timeval presentationTime);
  void afterGettingFrame1(unsigned frameSize,
			 struct timeval presentationTime);

private:
  unsigned char* fInputBuffer;
  unsigned fInputBufferSize;
};


////////// u-Law -> 16-bit PCM (in host order) //////////

class PCMFromuLawAudioSource: public FramedFilter {
public:
  static PCMFromuLawAudioSource*
  createNew(UsageEnvironment& env, FramedSource* inputSource);

protected:
  PCMFromuLawAudioSource(UsageEnvironment& env,
			 FramedSource* inputSource);
      // called only by createNew()
  virtual ~PCMFromuLawAudioSource();

private:
  // Redefined virtual functions:
  virtual void doGetNextFrame();

private:
  static void afterGettingFrame(void* clientData, unsigned frameSize,
				struct timeval presentationTime);
  void afterGettingFrame1(unsigned frameSize,
			 struct timeval presentationTime);

private:
  unsigned char* fInputBuffer;
  unsigned fInputBufferSize;
};


////////// 16-bit values (in host order) -> 16-bit network order //////////

class NetworkFromHostOrder16: public FramedFilter {
public:
  static NetworkFromHostOrder16*
  createNew(UsageEnvironment& env, FramedSource* inputSource);

protected:
  NetworkFromHostOrder16(UsageEnvironment& env, FramedSource* inputSource);
      // called only by createNew()
  virtual ~NetworkFromHostOrder16();

private:
  // Redefined virtual functions:
  virtual void doGetNextFrame();

private:
  static void afterGettingFrame(void* clientData, unsigned frameSize,
				struct timeval presentationTime);
  void afterGettingFrame1(unsigned frameSize,
			 struct timeval presentationTime);
};


////////// 16-bit values (in network order) -> 16-bit host order //////////

class HostFromNetworkOrder16: public FramedFilter {
public:
  static HostFromNetworkOrder16*
  createNew(UsageEnvironment& env, FramedSource* inputSource);

protected:
  HostFromNetworkOrder16(UsageEnvironment& env, FramedSource* inputSource);
      // called only by createNew()
  virtual ~HostFromNetworkOrder16();

private:
  // Redefined virtual functions:
  virtual void doGetNextFrame();

private:
  static void afterGettingFrame(void* clientData, unsigned frameSize,
				struct timeval presentationTime);
  void afterGettingFrame1(unsigned frameSize,
			 struct timeval presentationTime);
};

#endif
