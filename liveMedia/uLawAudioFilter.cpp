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
// Filters for converting between raw PCM audio and uLaw
// Implementation

#include "uLawAudioFilter.hh"

////////// 16-bit PCM (in host order) -> 8-bit u-Law //////////

uLawFromPCMAudioSource* uLawFromPCMAudioSource
::createNew(UsageEnvironment& env, FramedSource* inputSource) {
  return new uLawFromPCMAudioSource(env, inputSource);
}

uLawFromPCMAudioSource
::uLawFromPCMAudioSource(UsageEnvironment& env,
			 FramedSource* inputSource)
  : FramedFilter(env, inputSource),
    fInputBuffer(NULL), fInputBufferSize(0) {
}    

uLawFromPCMAudioSource::~uLawFromPCMAudioSource() {
  delete[] fInputBuffer;
} 

void uLawFromPCMAudioSource::doGetNextFrame() {
  // Figure out how many bytes of input data to ask for, and increase
  // our input buffer if necessary:
  unsigned bytesToRead = fMaxSize*2; // because we're converting 16 bits->8
  if (bytesToRead > fInputBufferSize) {
    delete[] fInputBuffer; fInputBuffer = new unsigned char[bytesToRead];
    fInputBufferSize = bytesToRead;
  }

  // Arrange to read samples into the input buffer:
  fInputSource->getNextFrame(fInputBuffer, bytesToRead,
			     afterGettingFrame, this,
                             FramedSource::handleClosure, this);
}

void uLawFromPCMAudioSource
::afterGettingFrame(void* clientData, unsigned frameSize,
		    unsigned numTruncatedBytes,
		    struct timeval presentationTime,
		    unsigned durationInMicroseconds) {
  uLawFromPCMAudioSource* source = (uLawFromPCMAudioSource*)clientData;
  source->afterGettingFrame1(frameSize, numTruncatedBytes,
			     presentationTime, durationInMicroseconds);
}

#define BIAS 0x84   // the add-in bias for 16 bit samples
#define CLIP 32635

static unsigned char uLawFrom16BitLinear(short sample) {
  static int exp_lut[256] = {0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,
			     4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
			     5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
			     5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
			     6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
			     6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
			     6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
			     6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
			     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
			     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
			     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
			     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
			     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
			     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
			     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
			     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7};
  unsigned char sign = (sample >> 8) & 0x80;
  if (sign != 0) sample = -sample; // get the magnitude

  if (sample > CLIP) sample = CLIP; // clip the magnitude
  sample += BIAS;

  unsigned char exponent = exp_lut[(sample>>7) & 0xFF];
  unsigned char mantissa = (sample >> (exponent+3)) & 0x0F;
  unsigned char result = ~(sign | (exponent << 4) | mantissa);
  if (result == 0 ) result = 0x02;  // CCITT trap

  return result;
}

void uLawFromPCMAudioSource
::afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
		     struct timeval presentationTime,
		     unsigned durationInMicroseconds) {
  // Translate raw 16-bit PCM samples (in the input buffer)
  // into uLaw samples (in the output buffer).
  // Note: The input samples are assumed to be in host byte order.
  unsigned numSamples = frameSize/2;
  short* inputSample = (short*)fInputBuffer;
  for (unsigned i = 0; i < numSamples; ++i) {
    fTo[i] = uLawFrom16BitLinear(inputSample[i]);
  }

  // Complete delivery to the client:
  fFrameSize = numSamples;
  fNumTruncatedBytes = numTruncatedBytes;
  fPresentationTime = presentationTime;
  fDurationInMicroseconds = durationInMicroseconds;
  afterGetting(this);
}


////////// u-Law -> 16-bit PCM (in host order) //////////

PCMFromuLawAudioSource* PCMFromuLawAudioSource
::createNew(UsageEnvironment& env, FramedSource* inputSource) {
  return new PCMFromuLawAudioSource(env, inputSource);
}

PCMFromuLawAudioSource
::PCMFromuLawAudioSource(UsageEnvironment& env,
			 FramedSource* inputSource)
  : FramedFilter(env, inputSource),
    fInputBuffer(NULL), fInputBufferSize(0) {
}    

PCMFromuLawAudioSource::~PCMFromuLawAudioSource() {
  delete[] fInputBuffer;
} 

void PCMFromuLawAudioSource::doGetNextFrame() {
  // Figure out how many bytes of input data to ask for, and increase
  // our input buffer if necessary:
  unsigned bytesToRead = fMaxSize/2; // because we're converting 8 bits->16
  if (bytesToRead > fInputBufferSize) {
    delete[] fInputBuffer; fInputBuffer = new unsigned char[bytesToRead];
    fInputBufferSize = bytesToRead;
  }

  // Arrange to read samples into the input buffer:
  fInputSource->getNextFrame(fInputBuffer, bytesToRead,
			     afterGettingFrame, this,
                             FramedSource::handleClosure, this);
}

void PCMFromuLawAudioSource
::afterGettingFrame(void* clientData, unsigned frameSize,
		    unsigned numTruncatedBytes,
		    struct timeval presentationTime,
		    unsigned durationInMicroseconds) {
  PCMFromuLawAudioSource* source = (PCMFromuLawAudioSource*)clientData;
  source->afterGettingFrame1(frameSize, numTruncatedBytes,
			     presentationTime, durationInMicroseconds);
}

static short linear16FromuLaw(unsigned char uLawByte) {
  static int exp_lut[8] = {0,132,396,924,1980,4092,8316,16764};
  uLawByte = ~uLawByte;

  Boolean sign = (uLawByte & 0x80) != 0;
  unsigned char exponent = (uLawByte>>4) & 0x07;
  unsigned char mantissa = uLawByte & 0x0F;

  short result = exp_lut[exponent] + (mantissa << (exponent+3));
  if (sign) result = -result;
  return result;
}

void PCMFromuLawAudioSource
::afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
		     struct timeval presentationTime,
		     unsigned durationInMicroseconds) {
  // Translate uLaw samples (in the input buffer)
  // into 16-bit PCM samples (in the output buffer), in host order.
  unsigned numSamples = frameSize;
  short* outputSample = (short*)fTo;
  for (unsigned i = 0; i < numSamples; ++i) {
    outputSample[i] = linear16FromuLaw(fInputBuffer[i]);
  }

  // Complete delivery to the client:
  fFrameSize = numSamples*2;
  fNumTruncatedBytes = numTruncatedBytes;
  fPresentationTime = presentationTime;
  fDurationInMicroseconds = durationInMicroseconds;
  afterGetting(this);
}


////////// 16-bit values (in host order) -> 16-bit network order //////////

NetworkFromHostOrder16* NetworkFromHostOrder16
::createNew(UsageEnvironment& env, FramedSource* inputSource) {
  return new NetworkFromHostOrder16(env, inputSource);
}

NetworkFromHostOrder16
::NetworkFromHostOrder16(UsageEnvironment& env,
			 FramedSource* inputSource)
  : FramedFilter(env, inputSource) {
}    

NetworkFromHostOrder16::~NetworkFromHostOrder16() {
} 

void NetworkFromHostOrder16::doGetNextFrame() {
  // Arrange to read data directly into the client's buffer:
  fInputSource->getNextFrame(fTo, fMaxSize,
			     afterGettingFrame, this,
                             FramedSource::handleClosure, this);
}

void NetworkFromHostOrder16
::afterGettingFrame(void* clientData, unsigned frameSize,
		    unsigned numTruncatedBytes,
		    struct timeval presentationTime,
		    unsigned durationInMicroseconds) {
  NetworkFromHostOrder16* source = (NetworkFromHostOrder16*)clientData;
  source->afterGettingFrame1(frameSize, numTruncatedBytes,
			     presentationTime, durationInMicroseconds);
}

void NetworkFromHostOrder16
::afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
		     struct timeval presentationTime,
		     unsigned durationInMicroseconds) {
  // Translate the 16-bit values that we have just read from host
  // to network order (in-place)
  unsigned numValues = frameSize/2;
  short* value = (short*)fTo;
  for (unsigned i = 0; i < numValues; ++i) {
    value[i] = htons(value[i]);
  }

  // Complete delivery to the client:
  fFrameSize = numValues*2;
  fNumTruncatedBytes = numTruncatedBytes;
  fPresentationTime = presentationTime;
  fDurationInMicroseconds = durationInMicroseconds;
  afterGetting(this);
}


////////// 16-bit values (in network order) -> 16-bit host order //////////

HostFromNetworkOrder16* HostFromNetworkOrder16
::createNew(UsageEnvironment& env, FramedSource* inputSource) {
  return new HostFromNetworkOrder16(env, inputSource);
}

HostFromNetworkOrder16
::HostFromNetworkOrder16(UsageEnvironment& env,
			 FramedSource* inputSource)
  : FramedFilter(env, inputSource) {
}    

HostFromNetworkOrder16::~HostFromNetworkOrder16() {
} 

void HostFromNetworkOrder16::doGetNextFrame() {
  // Arrange to read data directly into the client's buffer:
  fInputSource->getNextFrame(fTo, fMaxSize,
			     afterGettingFrame, this,
                             FramedSource::handleClosure, this);
}

void HostFromNetworkOrder16
::afterGettingFrame(void* clientData, unsigned frameSize,
		    unsigned numTruncatedBytes,
		    struct timeval presentationTime,
		    unsigned durationInMicroseconds) {
  HostFromNetworkOrder16* source = (HostFromNetworkOrder16*)clientData;
  source->afterGettingFrame1(frameSize, numTruncatedBytes,
			     presentationTime, durationInMicroseconds);
}

void HostFromNetworkOrder16
::afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
		     struct timeval presentationTime,
		     unsigned durationInMicroseconds) {
  // Translate the 16-bit values that we have just read from network
  // to host order (in-place):
  unsigned numValues = frameSize/2;
  short* value = (short*)fTo;
  for (unsigned i = 0; i < numValues; ++i) {
    value[i] = ntohs(value[i]);
  }

  // Complete delivery to the client:
  fFrameSize = numValues*2;
  fNumTruncatedBytes = numTruncatedBytes;
  fPresentationTime = presentationTime;
  fDurationInMicroseconds = durationInMicroseconds;
  afterGetting(this);
}
