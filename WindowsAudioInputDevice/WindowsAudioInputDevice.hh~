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
// Copyright (c) 1996-2003 Live Networks, Inc.  All rights reserved.
// Windows implementation of a generic audio input device
// C++ header
//
// To use this, call "AudioInputDevice::createNew()".
// You can also call "AudioInputDevice::getPortNames()" to get a list
// of port names. 

#ifndef _WINDOWS_AUDIO_INPUT_DEVICE_HH
#define _WINDOWS_AUDIO_INPUT_DEVICE_HH

#ifndef _AUDIO_INPUT_DEVICE_HH
#include "AudioInputDevice.hh"
#endif

class WindowsAudioInputDevice: public AudioInputDevice {
protected:
  WindowsAudioInputDevice(UsageEnvironment& env, int inputPortNumber,
	unsigned char bitsPerSample, unsigned char numChannels,
	unsigned samplingFrequency, unsigned granularityInMS,
	Boolean& success);
	// called only by createNew()

  virtual ~WindowsAudioInputDevice();

private:
  // redefined virtual functions:
  virtual void doGetNextFrame();
  virtual float getPlayTime(unsigned numFrames) const;
  virtual void doStopGettingFrames();
  virtual Boolean setInputPort(int portIndex);
  virtual double getAverageLevel() const;

private:
  friend class AudioInputDevice; friend class Mixer;
  static void initializeIfNecessary();
  static void audioReadyPoller(void* clientData);

  void audioReadyPoller1();
  void onceAudioIsReady();

  // Audio input buffering:
  static Boolean waveIn_open(unsigned uid, WAVEFORMATEX& wfx);
  static void waveIn_close();
  static void waveIn_reset(); // used to implement both of the above
  static unsigned readFromBuffers(unsigned char* to, unsigned numBytesWanted, struct timeval& creationTime);
  static void releaseHeadBuffer(); // from the input header queue

public:
  static void waveInProc(WAVEHDR* hdr); // Windows audio callback function 

private:
  static unsigned numMixers;
  static class Mixer* ourMixers;
  static unsigned numInputPortsTotal;

  static HWAVEIN shWaveIn;
  static unsigned blockSize, numBlocks;
  static unsigned char* readData; // buffer for incoming audio data
  static DWORD bytesUsedAtReadHead; // number of bytes that have already been read at head
  static double uSecsPerByte; // used to adjust the time for # bytes consumed since arrival
  static double averageLevel;
  static WAVEHDR *readHdrs, *readHead, *readTail; // input header queue
  static struct timeval* readTimes;
  static HANDLE hAudioReady; // audio ready event

  int fCurMixerId, fCurPortIndex;
  unsigned fTotalPollingDelay; // uSeconds
  Boolean fHaveStarted;
};

#endif
