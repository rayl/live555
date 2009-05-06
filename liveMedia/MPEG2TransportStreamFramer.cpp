// A filter that passes through (unchanged) chunks that contain an integral number
// of MPEG-2 Transport Stream packets, but returning (in "fDurationInMicroseconds")
// an updated estimate of the time gap between chunks.
// Implementation

#include "MPEG2TransportStreamFramer.hh"

#define TRANSPORT_PACKET_SIZE 188
#define NEW_DURATION_WEIGHT 0.05
  // How much weight to give to the latest duration measurement (must be <= 1)

////////// PIDStatus //////////

class PIDStatus {
public:
  PIDStatus() : lastClock(0.0), lastPacketNum(0), hasJustStarted(True) {}

  double lastClock;
  unsigned lastPacketNum;
  Boolean hasJustStarted;
};


////////// MPEG2TransportStreamFramer //////////

MPEG2TransportStreamFramer* MPEG2TransportStreamFramer
::createNew(UsageEnvironment& env, FramedSource* inputSource,
	    unsigned numTSPacketsPerChunk) {
  return new MPEG2TransportStreamFramer(env, inputSource, numTSPacketsPerChunk);
}

MPEG2TransportStreamFramer
::MPEG2TransportStreamFramer(UsageEnvironment& env, FramedSource* inputSource,
			     unsigned numTSPacketsPerChunk)
  : FramedFilter(env, inputSource),
    fNumTSPacketsPerChunk(numTSPacketsPerChunk),
    fTSPacketCount(0), fTSPacketDurationEstimate(0.0) {
  fPIDStatusTable = HashTable::create(ONE_WORD_HASH_KEYS);
}

MPEG2TransportStreamFramer::~MPEG2TransportStreamFramer() {
  PIDStatus* pidStatus;
  while ((pidStatus = (PIDStatus*)fPIDStatusTable->RemoveNext()) != NULL) {
    delete pidStatus;
  }
  delete fPIDStatusTable;
}

void MPEG2TransportStreamFramer::doGetNextFrame() {
  // Read directly from our input source into our client's buffer:
  fInputSource->getNextFrame(fTo, fMaxSize,
			     afterGettingFrame, this,
			     FramedSource::handleClosure, this);
}

void MPEG2TransportStreamFramer
::afterGettingFrame(void* clientData, unsigned frameSize,
		    unsigned /*numTruncatedBytes*/,
		    struct timeval presentationTime,
		    unsigned /*durationInMicroseconds*/) {
  MPEG2TransportStreamFramer* framer = (MPEG2TransportStreamFramer*)clientData;
  framer->afterGettingFrame1(frameSize, presentationTime);
}

void MPEG2TransportStreamFramer::afterGettingFrame1(unsigned frameSize,
						    struct timeval presentationTime) {
  if (frameSize < fNumTSPacketsPerChunk*TRANSPORT_PACKET_SIZE) {
    // We read less than expected; assume that the input source has closed.
    // (This may lose a few TS packets at the end.)
    handleClosure(this);
    return;
  }

  fFrameSize = frameSize;
  fPresentationTime = presentationTime;

  // Scan through the TS packets that we read, and update our estimate of
  // the duration of each packet:
  for (unsigned i = 0; i < fNumTSPacketsPerChunk; ++i) {
    updateTSPacketDurationEstimate(&fTo[i*TRANSPORT_PACKET_SIZE]);
  }

  fDurationInMicroseconds
    = fNumTSPacketsPerChunk * (unsigned)(fTSPacketDurationEstimate*1000000);

  // Complete the delivery to our client:
  afterGetting(this);
}

void MPEG2TransportStreamFramer::updateTSPacketDurationEstimate(unsigned char* pkt) {
  // Sanity check: Make sure we start with the sync byte:
  if (pkt[0] != 0x47) {
    envir() << "Missing sync byte!\n";
    return;
  }

  ++fTSPacketCount;

  // If this packet doesn't contain a PCR, then we're not interested in it:
  u_int8_t const adaptation_field_control = (pkt[3]&0x30)>>4;
  if (adaptation_field_control != 2 && adaptation_field_control != 3) return;
      // there's no adaptation_field

  u_int8_t const adaptation_field_length = pkt[4];
  if (adaptation_field_length == 0) return;

  u_int8_t const pcrFlag = pkt[5]&0x10;
  if (pcrFlag == 0) return; // no PCR

  // There's a PCR.  Get it, and the PID:
  u_int32_t pcrBaseHigh = (pkt[6]<<24)|(pkt[7]<<16)|(pkt[8]<<8)|pkt[9];
  double clock = pcrBaseHigh/45000.0;
  if ((pkt[10]&0x80) != 0) clock += 1/90000.0; // add in low-bit (if set)
  unsigned short pcrExt = ((pkt[10]&0x01)<<8) | pkt[11];
  clock += pcrExt/27000000.0;

  unsigned pid = ((pkt[1]&0x1F)<<8) | pkt[2];

  // Check whether we already have a record of a PCR for this PID:
  PIDStatus* pidStatus = (PIDStatus*)(fPIDStatusTable->Lookup((char*)pid));
  if (pidStatus == NULL) {
    // We're seeing this PID's PCR for the first time:
    pidStatus = new PIDStatus;
    fPIDStatusTable->Add((char*)pid, pidStatus);
  } else {
    // We've seen this PID's PCR before; update our per-packet duration estimate:
    double durationPerPacket
      = (clock - pidStatus->lastClock)/(fTSPacketCount - pidStatus->lastPacketNum);
    if (pidStatus->hasJustStarted) {
      fTSPacketDurationEstimate = durationPerPacket;
      pidStatus->hasJustStarted = False;
    } else {
      fTSPacketDurationEstimate
	= durationPerPacket*NEW_DURATION_WEIGHT
	+ fTSPacketDurationEstimate*(1-NEW_DURATION_WEIGHT);
    }
#ifdef DEBUG_PCR
    fprintf(stderr, "PCR 0x%08x+%d == %f => this duration %f, new estimate %f\n", pcrBaseHigh, pkt[10]>>7, clock, durationPerPacket, fTSPacketDurationEstimate);
#endif
  }

  pidStatus->lastClock = clock;
  pidStatus->lastPacketNum = fTSPacketCount;
}
