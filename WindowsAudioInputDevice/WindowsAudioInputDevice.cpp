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
// Copyright (c) 2001-2003 Live Networks, Inc.  All rights reserved.
// Windows implementation of a generic audio input device
// Implementation

#include <WindowsAudioInputDevice.hh>
#include <GroupsockHelper.hh>

////////// Mixer and AudioInputPort definition //////////

class AudioInputPort {
public:
	int tag;
	char name[MIXER_LONG_NAME_CHARS];
};

class Mixer {
public:
	Mixer();
	virtual ~Mixer();

	void open(unsigned numChannels, unsigned samplingFrequency, unsigned granularityInMS);
	void open(); // open with default parameters
	void getPortsInfo();
	Boolean enableInputPort(unsigned portIndex, char const*& errReason, MMRESULT& errCode);
	void close();

	unsigned index;
	HMIXER hMixer; // valid when open
	DWORD dwRecLineID; // valid when open
	unsigned numPorts;
	AudioInputPort* ports;
	char name[MAXPNAMELEN];
};


////////// AudioInputDevice (remaining) implementation //////////

AudioInputDevice*
AudioInputDevice::createNew(UsageEnvironment& env, int inputPortNumber,
			    unsigned char bitsPerSample,
			    unsigned char numChannels,
			    unsigned samplingFrequency,
			    unsigned granularityInMS) {
  Boolean success;
  WindowsAudioInputDevice* newSource
    = new WindowsAudioInputDevice(env, inputPortNumber,
					bitsPerSample, numChannels,
					samplingFrequency, granularityInMS,
					success);
  if (!success) {delete newSource; newSource = NULL;}

  return newSource;
}

AudioPortNames* AudioInputDevice::getPortNames() {
	WindowsAudioInputDevice::initializeIfNecessary();

	AudioPortNames* portNames = new AudioPortNames;
	portNames->numPorts = WindowsAudioInputDevice::numInputPortsTotal;
	portNames->portName = new char*[WindowsAudioInputDevice::numInputPortsTotal];
	 
	// If there's more than one mixer, print only the port name.
	// If there's two or more mixers, also include the mixer name
	// (to disambiguate	port names that may be the same name in different mixers)
	char portNameBuffer[2*MAXPNAMELEN+10/*slop*/];
	char mixerNameBuffer[MAXPNAMELEN];
	char const* portNameFmt;
	if (WindowsAudioInputDevice::numMixers <= 1) {
		portNameFmt = "%s";
	} else {
		portNameFmt = "%s (%s)";
	}

	unsigned curPortNum = 0;
	for (unsigned i = 0; i < WindowsAudioInputDevice::numMixers; ++i) {
		Mixer& mixer = WindowsAudioInputDevice::ourMixers[i];

		if (WindowsAudioInputDevice::numMixers <= 1) {
			mixerNameBuffer[0] = '\0';
		} else {
			strncpy(mixerNameBuffer, mixer.name, sizeof mixerNameBuffer);
#if 0
			// Hack: Simplify the mixer name, by truncating after the first space character:
			for (int k = 0; k < sizeof mixerNameBuffer && mixerNameBuffer[k] != '\0'; ++k) {
				if (mixerNameBuffer[k] == ' ') {
					mixerNameBuffer[k] = '\0';
					break;
				}
			}
#endif
		}

		for (unsigned j = 0; j < mixer.numPorts; ++j) {
			sprintf(portNameBuffer, portNameFmt, mixer.ports[j].name, mixerNameBuffer);
			portNames->portName[curPortNum++] = strDup(portNameBuffer);
		}
    }

	return portNames;
}


////////// WindowsAudioInputDevice implementation //////////

static unsigned _bitsPerSample = 16;

WindowsAudioInputDevice::WindowsAudioInputDevice(UsageEnvironment& env, int inputPortNumber,
								   unsigned char bitsPerSample, unsigned char numChannels,
								   unsigned samplingFrequency, unsigned granularityInMS,
								   Boolean& success)
  : AudioInputDevice(env, bitsPerSample, numChannels, samplingFrequency, granularityInMS),
  fCurMixerId(-1), fCurPortIndex(-1), fHaveStarted(False) {
  _bitsPerSample = bitsPerSample;

  if (!setInputPort(inputPortNumber)) {
		char errMsgPrefix[100];
		sprintf(errMsgPrefix, "Failed to set audio input port number to %d: ", inputPortNumber);
		char* errMsgSuffix = strDup(env.getResultMsg());
		env.setResultMsg(errMsgPrefix, errMsgSuffix);
		delete[] errMsgSuffix;
		success = False;
	} else {
		success = True;
	}
}

WindowsAudioInputDevice::~WindowsAudioInputDevice() {
	if (fCurMixerId >= 0) ourMixers[fCurMixerId].close();

	delete[] ourMixers; ourMixers = NULL;
	numMixers = numInputPortsTotal = 0;
}

void WindowsAudioInputDevice::doGetNextFrame() {
	if (!fHaveStarted) {
		// Before reading the first audio data, flush any existing data:
		while (readHead != NULL) releaseHeadBuffer();
		fHaveStarted = True;
	}
	fTotalPollingDelay = 0;
	audioReadyPoller1();
}

float WindowsAudioInputDevice::getPlayTime(unsigned numFrames) const {
  return (float)numFrames/fSamplingFrequency;
}

Boolean WindowsAudioInputDevice::setInputPort(int portIndex) {
	initializeIfNecessary();

	if (portIndex < 0 || portIndex >= (int)numInputPortsTotal) { // bad index
		envir().setResultMsg("Bad input port index\n");
		return False;
	}

	// Find the mixer and port that corresponds to "portIndex":
	int newMixerId, portWithinMixer, portIndexCount = 0;
	for (newMixerId = 0; newMixerId < (int)numMixers; ++newMixerId) {
		int prevPortIndexCount = portIndexCount;
		portIndexCount += ourMixers[newMixerId].numPorts;
		if (portIndexCount > portIndex) { // it's with this mixer
			portWithinMixer = portIndex - prevPortIndexCount;
			break;
		}
    }

	if (newMixerId != fCurMixerId) { 
		// The mixer has changed, so close the old one and open the new one:
		if (fCurMixerId >= 0) ourMixers[fCurMixerId].close();
		fCurMixerId = newMixerId;
		ourMixers[fCurMixerId].open(fNumChannels, fSamplingFrequency, fGranularityInMS);
	}
	if (portIndex != fCurPortIndex) {
		// Change the input port:
		fCurPortIndex = portIndex;
		char const* errReason;
		MMRESULT errCode;
		if (!ourMixers[newMixerId].enableInputPort(portWithinMixer, errReason, errCode)) {
			char resultMsg[100];
			sprintf(resultMsg, "Failed to enable input port: %s failed (0x%08x)\n", errReason, errCode);
			envir().setResultMsg(resultMsg);
			return False;
		}
		// Later, may also need to transfer 'gain' to new port #####
	}
	return True;
}

double WindowsAudioInputDevice::getAverageLevel() const {
	// If the input audio queue is empty, return the previous level,
	// otherwise use the input queue to recompute "averageLevel":
	if (readHead != NULL) {
		double levelTotal = 0.0;
		unsigned totNumSamples = 0;
		WAVEHDR* curHdr = readHead;
		while (1) {
			short* samplePtr = (short*)(curHdr->lpData);
			unsigned numSamples = blockSize/2;
			totNumSamples += numSamples;

			while (numSamples-- > 0) {
				short sample = *samplePtr++;
				if (sample < 0) sample = -sample;
				levelTotal += (unsigned short)sample;
			}

			if (curHdr == readTail) break;
			curHdr = curHdr->lpNext;
		}
		averageLevel = levelTotal/(totNumSamples*(double)0x8000);
	}
	return averageLevel;
}

void WindowsAudioInputDevice::initializeIfNecessary() {
	if (ourMixers != NULL) return; // we've already been initialized
	numMixers = mixerGetNumDevs();
	ourMixers = new Mixer[numMixers];

	// Initialize each mixer:
	numInputPortsTotal = 0;
	for (unsigned i = 0; i < numMixers; ++i) {
		Mixer& mixer = ourMixers[i];
		mixer.index = i;
		mixer.open();
 		if (mixer.hMixer != NULL) {
			// This device has a valid mixer.  Get information about its ports:
			mixer.getPortsInfo();
            mixer.close();

			if (mixer.numPorts == 0) continue;
        
			numInputPortsTotal += mixer.numPorts;
		} else {
			mixer.ports = NULL;
			mixer.numPorts = 0;
		}
	}
}

void WindowsAudioInputDevice::audioReadyPoller(void* clientData) {
	WindowsAudioInputDevice* inputDevice = (WindowsAudioInputDevice*)clientData;
	inputDevice->audioReadyPoller1();
}

void WindowsAudioInputDevice::audioReadyPoller1() {
	if (readHead != NULL) {
		onceAudioIsReady();
	} else {
		unsigned const maxPollingDelay = (100 + fGranularityInMS)*1000;
		if (fTotalPollingDelay > maxPollingDelay) {
			// We've waited too long for the audio device - assume it's down:
			handleClosure(this);
			return;
		}

		// Try again after a short delay:
		unsigned const uSecondsToDelay = fGranularityInMS*1000;
		fTotalPollingDelay += uSecondsToDelay;
		nextTask() = envir().taskScheduler().scheduleDelayedTask(uSecondsToDelay,
			(TaskFunc*)audioReadyPoller, this);
	}
}

void WindowsAudioInputDevice::onceAudioIsReady() {
	fFrameSize = readFromBuffers(fTo, fMaxSize, fPresentationTime);
	if (fFrameSize == 0) {
		// The source is no longer readable
		handleClosure(this);
		return;
	}

	// Call our own 'after getting' function.  Because we sometimes get here
    // after returning from a delay, we can call this directly, without risking
    // infinite recursion
    afterGetting(this);
}

static void CALLBACK waveInCallback(HWAVEIN /*hwi*/, UINT uMsg,
           DWORD /*dwInstance*/, DWORD dwParam1, DWORD /*dwParam2*/) {
    switch (uMsg) {
	case WIM_DATA:
		WAVEHDR* hdr = (WAVEHDR*)dwParam1;
		WindowsAudioInputDevice::waveInProc(hdr);
		break;
	}
}

Boolean WindowsAudioInputDevice::waveIn_open(unsigned uid, WAVEFORMATEX& wfx) {
	if (shWaveIn != NULL) return True; // already open

	do {
		waveIn_reset();
		if (waveInOpen(&shWaveIn, uid, &wfx,
			(DWORD)waveInCallback, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) break;

		// Allocate read buffers, and headers:
		readData = new unsigned char[numBlocks*blockSize];
		if (readData == NULL) break;

		readHdrs = new WAVEHDR[numBlocks];
		if (readHdrs == NULL) break;
		readHead = readTail = NULL;

		readTimes = new struct timeval[numBlocks];
		if (readTimes == NULL) break;

		// Initialize headers:
		for (unsigned i = 0; i < numBlocks; ++i) {
            readHdrs[i].lpData = (char*)&readData[i*blockSize];
            readHdrs[i].dwBufferLength = blockSize;
            readHdrs[i].dwFlags = 0;
            if (waveInPrepareHeader(shWaveIn, &readHdrs[i], sizeof (WAVEHDR)) != MMSYSERR_NOERROR) break;
            if (waveInAddBuffer(shWaveIn, &readHdrs[i], sizeof (WAVEHDR)) != MMSYSERR_NOERROR) break;
		}

		if (waveInStart(shWaveIn) != MMSYSERR_NOERROR) break;

        hAudioReady = CreateEvent(NULL, TRUE, FALSE, "waveIn Audio Ready");
		return True;
	} while (0);

	waveIn_reset();
	return False;
}

void WindowsAudioInputDevice::waveIn_close() {
	if (shWaveIn == NULL) return; // already closed
        
    waveInStop(shWaveIn);
    waveInReset(shWaveIn);
        
    for (unsigned i = 0; i < numBlocks; ++i) {
		if (readHdrs[i].dwFlags & WHDR_PREPARED) {
			waveInUnprepareHeader(shWaveIn, &readHdrs[i], sizeof (WAVEHDR));
		}
	}

	waveInClose(shWaveIn);
	waveIn_reset();
}

void WindowsAudioInputDevice::waveIn_reset() {
	shWaveIn = NULL;

	delete[] readData; readData = NULL;
	bytesUsedAtReadHead = 0;

	delete[] readHdrs; readHdrs = NULL;
	readHead = readTail = NULL;

	delete[] readTimes; readTimes = NULL;

	hAudioReady = NULL;
}

unsigned WindowsAudioInputDevice::readFromBuffers(unsigned char* to, unsigned numBytesWanted, struct timeval& creationTime) {
	// Begin by computing the creation time of (the first bytes of) this returned audio data:
	if (readHead != NULL) {
		int hdrIndex = readHead - readHdrs;
		creationTime = readTimes[hdrIndex];

		// Adjust this time to allow for any data that's already been read from this buffer:
		if (bytesUsedAtReadHead > 0) {
			creationTime.tv_usec += (unsigned)(uSecsPerByte*bytesUsedAtReadHead);
			creationTime.tv_sec += creationTime.tv_usec/1000000;
			creationTime.tv_usec %= 1000000;
		}
	}

	// Then, read from each available buffer, until we have the data that we want:
	unsigned numBytesRead = 0;
    while (readHead != NULL && numBytesRead < numBytesWanted) {
		unsigned thisRead = min(readHead->dwBytesRecorded - bytesUsedAtReadHead, numBytesWanted - numBytesRead);
		memmove(&to[numBytesRead], &readHead->lpData[bytesUsedAtReadHead], thisRead);
		numBytesRead += thisRead;
		bytesUsedAtReadHead += thisRead;
		if (bytesUsedAtReadHead == readHead->dwBytesRecorded) {
			// We're finished with the block; give it back to the device:
			releaseHeadBuffer();
		}
    }

    return numBytesRead;
}

void WindowsAudioInputDevice::releaseHeadBuffer() {
	WAVEHDR* toRelease = readHead;
	if (readHead == NULL) return;

	readHead = readHead->lpNext;
	if (readHead == NULL) readTail = NULL;

	toRelease->lpNext = NULL; 
	toRelease->dwBytesRecorded = 0; 
	toRelease->dwFlags &= ~WHDR_DONE;
	waveInAddBuffer(shWaveIn, toRelease, sizeof (WAVEHDR)); 
	bytesUsedAtReadHead = 0;
}

void WindowsAudioInputDevice::waveInProc(WAVEHDR* hdr) {
	unsigned hdrIndex = hdr - readHdrs;

	// Record the time that the data arrived:
	int dontCare;
	gettimeofday(&readTimes[hdrIndex], &dontCare);

	// Add the block to the tail of the queue:
       hdr->lpNext = NULL;
	if (readTail != NULL) {
		readTail->lpNext = hdr;
		readTail = hdr;
	} else {
		readHead = readTail = hdr;
	}
	SetEvent(hAudioReady);
}

unsigned WindowsAudioInputDevice::numMixers = 0;

Mixer* WindowsAudioInputDevice::ourMixers = NULL;

unsigned WindowsAudioInputDevice::numInputPortsTotal = 0;

HWAVEIN WindowsAudioInputDevice::shWaveIn = NULL;

unsigned WindowsAudioInputDevice::blockSize = 0;
unsigned WindowsAudioInputDevice::numBlocks = 0;

unsigned char* WindowsAudioInputDevice::readData = NULL;
DWORD WindowsAudioInputDevice::bytesUsedAtReadHead = 0;
double WindowsAudioInputDevice::uSecsPerByte = 0.0;
double WindowsAudioInputDevice::averageLevel = 0.0;

WAVEHDR* WindowsAudioInputDevice::readHdrs = NULL;
WAVEHDR* WindowsAudioInputDevice::readHead = NULL;
WAVEHDR* WindowsAudioInputDevice::readTail = NULL;

struct timeval* WindowsAudioInputDevice::readTimes = NULL;

HANDLE WindowsAudioInputDevice::hAudioReady = NULL;


////////// Mixer and AudioInputPort implementation //////////

Mixer::Mixer()
: hMixer(NULL), dwRecLineID(0), numPorts(0), ports(NULL) {
}

Mixer::~Mixer() {
	delete[] ports;
}

void Mixer::open(unsigned numChannels, unsigned samplingFrequency, unsigned granularityInMS) {
	HMIXER newHMixer = NULL;
	do {
	  WindowsAudioInputDevice::uSecsPerByte
	    = (8*1e6)/(_bitsPerSample*numChannels*samplingFrequency);

        MIXERCAPS mc;
        if (mixerGetDevCaps(index, &mc, sizeof mc) != MMSYSERR_NOERROR) break;

		// Copy the mixer name:
        strncpy(name, mc.szPname, MAXPNAMELEN);
        
		// Find the correct line for this mixer:
		unsigned i, uWavIn;
        unsigned nWavIn = waveInGetNumDevs();
        for (i = 0; i < nWavIn; ++i) {
			WAVEINCAPS wic;
			if (waveInGetDevCaps(i, &wic, sizeof wic) != MMSYSERR_NOERROR) continue;
                
			MIXERLINE ml;
            ml.cbStruct = sizeof ml;
            ml.Target.dwType  = MIXERLINE_TARGETTYPE_WAVEIN;
            strncpy(ml.Target.szPname, wic.szPname, MAXPNAMELEN);
            ml.Target.vDriverVersion = wic.vDriverVersion;
            ml.Target.wMid = wic.wMid;
            ml.Target.wPid = wic.wPid;
                
            if (mixerGetLineInfo((HMIXEROBJ)index, &ml, MIXER_GETLINEINFOF_TARGETTYPE/*|MIXER_OBJECTF_MIXER*/) == MMSYSERR_NOERROR) {
				// this is the right line
				uWavIn = i;
                dwRecLineID = ml.dwLineID;
				break;
			}
        }
        if (i >= nWavIn) break; // error: we couldn't find the right line

        if (mixerOpen(&newHMixer, index, (unsigned long)NULL, (unsigned long)NULL, MIXER_OBJECTF_MIXER) != MMSYSERR_NOERROR) break;
		if (newHMixer == NULL) break;

		// Sanity check: re-call "mixerGetDevCaps()" using the mixer device handle:
        if (mixerGetDevCaps((UINT)newHMixer, &mc, sizeof mc) != MMSYSERR_NOERROR) break;
        if (mc.cDestinations < 1) break; // error: this mixer has no destinations         

        WAVEFORMATEX wfx;
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = numChannels;
        wfx.nSamplesPerSec  = samplingFrequency;
        wfx.wBitsPerSample  = _bitsPerSample;
        wfx.nBlockAlign     = (numChannels*_bitsPerSample)/8;
        wfx.nAvgBytesPerSec = samplingFrequency*wfx.nBlockAlign;
        wfx.cbSize          = 0;

		WindowsAudioInputDevice::blockSize = (wfx.nAvgBytesPerSec*granularityInMS)/1000;

		// Use a 10-second input buffer, to allow for CPU competition from video, etc.,
		// and also for some audio cards that buffer as much as 5 seconds of audio.
		unsigned const bufferSeconds = 10;
		WindowsAudioInputDevice::numBlocks = (bufferSeconds*1000)/granularityInMS;

		if (!WindowsAudioInputDevice::waveIn_open(uWavIn, wfx)) break;

        // Set this process's priority high. I'm not sure how much this is really needed,
		// but the "rat" code does this:
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

		hMixer = newHMixer;
		return;
	} while (0);

	// An error occurred:
	close(); 
}        

void Mixer::open() {
	open(1, 8000, 20);
}

void Mixer::getPortsInfo() {
	MIXERCAPS mc;
    mixerGetDevCaps((UINT)hMixer, &mc, sizeof mc);

	MIXERLINE mlt;
	unsigned i;
	for (i = 0; i < mc.cDestinations; ++i) {
		memset(&mlt, 0, sizeof mlt);
        mlt.cbStruct = sizeof mlt;
        mlt.dwDestination = i;
        if (mixerGetLineInfo(hMixer, &mlt, MIXER_GETLINEINFOF_DESTINATION) != MMSYSERR_NOERROR) continue;
        if (mlt.dwLineID == dwRecLineID) break; // this is the destination we're interested in
	}
    ports = new AudioInputPort[mlt.cConnections];
        
    numPorts = mlt.cConnections;
    for (i = 0; i < numPorts; ++i) {
		MIXERLINE mlc;
        memcpy(&mlc, &mlt, sizeof mlc);
        mlc.dwSource = i;
        mixerGetLineInfo((HMIXEROBJ)hMixer, &mlc, MIXER_GETLINEINFOF_SOURCE/*|MIXER_OBJECTF_HMIXER*/);
        ports[i].tag = mlc.dwLineID;
        strncpy(ports[i].name, mlc.szName, MIXER_LONG_NAME_CHARS);
    }

	// A cute hack borrowed from the "rat" code: Make the microphone the first port in the list:
    for (i = 1; i < numPorts; ++i) {
		if (_strnicmp("mic", ports[i].name, 3) == 0 ||
			_strnicmp("mik", ports[i].name, 3) == 0) {
			AudioInputPort tmp = ports[0];
			ports[0] = ports[i];
			ports[i] = tmp;
		}
    }
}

Boolean Mixer::enableInputPort(unsigned portIndex, char const*& errReason, MMRESULT& errCode) {
	errReason = NULL; // unless there's an error
	AudioInputPort& port = ports[portIndex];
        
    MIXERCONTROL mc;
    MIXERLINECONTROLS mlc;
#if 0 // the following doesn't seem to be needed, and can fail:
    mlc.cbStruct = sizeof mlc;
    mlc.pamxctrl = &mc;
    mlc.cbmxctrl = sizeof (MIXERCONTROL);
    mlc.dwLineID = port.tag;
    mlc.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
    if ((errCode = mixerGetLineControls(hMixer, &mlc, MIXER_GETLINECONTROLSF_ONEBYTYPE/*|MIXER_OBJECTF_HMIXER*/)) != MMSYSERR_NOERROR) {
		errReason = "mixerGetLineControls()";
		return False;
	}
#endif

    MIXERLINE ml;
    memset(&ml, 0, sizeof (MIXERLINE));
    ml.cbStruct = sizeof (MIXERLINE);
    ml.dwLineID = port.tag;
    if ((errCode = mixerGetLineInfo(hMixer, &ml, MIXER_GETLINEINFOF_LINEID)) != MMSYSERR_NOERROR) {
		errReason = "mixerGetLineInfo()1";
		return False;
	}

	char portname[MIXER_LONG_NAME_CHARS+1];
    strncpy(portname, ml.szName, MIXER_LONG_NAME_CHARS);

    memset(&ml, 0, sizeof (MIXERLINE));
    ml.cbStruct = sizeof (MIXERLINE);
    ml.dwLineID = dwRecLineID;
    if ((errCode = mixerGetLineInfo(hMixer, &ml, MIXER_GETLINEINFOF_LINEID/*|MIXER_OBJECTF_HMIXER*/)) != MMSYSERR_NOERROR) {
		errReason = "mixerGetLineInfo()2";
		return False;
	}

    // Get Mixer/MUX control information (need control id to set and get control details)
    mlc.cbStruct = sizeof mlc;
    mlc.dwLineID = ml.dwLineID;
    mlc.cControls = 1;
		mc.cbStruct = sizeof mc; // Needed???#####
		mc.dwControlID = 0xDEADBEEF; // For testing #####
    mlc.pamxctrl = &mc;
    mlc.cbmxctrl = sizeof mc;
    mlc.dwControlType = MIXERCONTROL_CONTROLTYPE_MUX; // Single Select
    if ((errCode = mixerGetLineControls(hMixer, &mlc, MIXER_GETLINECONTROLSF_ONEBYTYPE/*|MIXER_OBJECTF_HMIXER*/)) != MMSYSERR_NOERROR) {
		mlc.dwControlType = MIXERCONTROL_CONTROLTYPE_MIXER; // Multiple Select
		mixerGetLineControls(hMixer, &mlc, MIXER_GETLINECONTROLSF_ONEBYTYPE/*|MIXER_OBJECTF_HMIXER*/);
	}
        
    unsigned matchLine = 0;
	if (mc.cMultipleItems > 1) {
		// Before getting control, we need to know which line to grab.
		// We figure this out by listing the lines, and comparing names:
		MIXERCONTROLDETAILS mcd;
		mcd.cbStruct = sizeof mcd;
		mcd.cChannels = ml.cChannels;
		mcd.cMultipleItems = mc.cMultipleItems;
		MIXERCONTROLDETAILS_LISTTEXT* mcdlText = new MIXERCONTROLDETAILS_LISTTEXT[mc.cMultipleItems];        
		mcd.cbDetails = sizeof (MIXERCONTROLDETAILS_LISTTEXT);
		mcd.paDetails = mcdlText;

		if (mc.dwControlID != 0xDEADBEEF) { // we know the control id for real
			mcd.dwControlID = mc.dwControlID;
			if ((errCode = mixerGetControlDetails(hMixer, &mcd, MIXER_GETCONTROLDETAILSF_LISTTEXT/*|MIXER_OBJECTF_HMIXER*/)) != MMSYSERR_NOERROR) {
				delete[] mcdlText;
				errReason = "mixerGetControlDetails()1";
				return False;
			}
		} else {
			// Hack: We couldn't find a MUX or MIXER control, so try to guess the control id:
			for (mc.dwControlID = 0; mc.dwControlID < 32; ++mc.dwControlID) {
				mcd.dwControlID = mc.dwControlID;
				if ((errCode = mixerGetControlDetails(hMixer, &mcd, MIXER_GETCONTROLDETAILSF_LISTTEXT/*|MIXER_OBJECTF_HMIXER*/)) == MMSYSERR_NOERROR) break;
			}
			if (mc.dwControlID == 32) { // unable to guess mux/mixer control id
				delete[] mcdlText;
				errReason = "mixerGetControlDetails()2";
				return False;
			}
		}
        
		for (unsigned i = 0; i < mcd.cMultipleItems; ++i) {
			if (strcmp(mcdlText[i].szName, portname) == 0) {
				matchLine = i;
				break;
			}
		}
		delete[] mcdlText;
	}

    // Now get control itself:
	MIXERCONTROLDETAILS mcd;
    mcd.cbStruct = sizeof mcd;
    mcd.dwControlID = mc.dwControlID;
    mcd.cChannels = ml.cChannels;
    mcd.cMultipleItems = mc.cMultipleItems;
    MIXERCONTROLDETAILS_BOOLEAN* mcdbState = new MIXERCONTROLDETAILS_BOOLEAN[mc.cMultipleItems];        
    mcd.paDetails = mcdbState;
    mcd.cbDetails = sizeof (MIXERCONTROLDETAILS_BOOLEAN);
        
    if ((errCode = mixerGetControlDetails(hMixer, &mcd, MIXER_GETCONTROLDETAILSF_VALUE/*|MIXER_OBJECTF_HMIXER*/)) != MMSYSERR_NOERROR) {
		delete[] mcdbState;
		errReason = "mixerGetControlDetails()3";
		return False;
    }
        
    for (unsigned j = 0; j < mcd.cMultipleItems; ++j) {
		mcdbState[j].fValue = (j == matchLine);
    }
        
    if ((errCode = mixerSetControlDetails(hMixer, &mcd, MIXER_OBJECTF_HMIXER)) != MMSYSERR_NOERROR) {
		delete[] mcdbState;
		errReason = "mixerSetControlDetails()";
		return False;
    }
	delete[] mcdbState;
        
	return True;
}


void Mixer::close() {
	WindowsAudioInputDevice::waveIn_close();
	if (hMixer != NULL) mixerClose(hMixer);
	hMixer = NULL; dwRecLineID = 0;
}
