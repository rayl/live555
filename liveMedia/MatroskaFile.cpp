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
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2011 Live Networks, Inc.  All rights reserved.
// A class that encapsulates a Matroska file.
// Implementation

#include "MatroskaFileParser.hh"
#include "MatroskaDemuxedTrack.hh"
#include <ByteStreamFileSource.hh>

void MatroskaFile
::createNew(UsageEnvironment& env, char const* fileName, onCreationFunc* onCreation, void* onCreationClientData,
	    char const* preferredLanguage) {
  new MatroskaFile(env, fileName, onCreation, onCreationClientData, preferredLanguage);
}

MatroskaFile::MatroskaFile(UsageEnvironment& env, char const* fileName, onCreationFunc* onCreation, void* onCreationClientData,
			   char const* preferredLanguage)
  : Medium(env),
    fFileName(strDup(fileName)), fOnCreation(onCreation), fOnCreationClientData(onCreationClientData),
    fPreferredLanguage(strDup(preferredLanguage)),
    fTimecodeScale(1000000), fSegmentDuration(0.0), fSegmentDataOffset(0), fClusterOffset(0), fCuesOffset(0),
    fChosenVideoTrackNumber(0), fChosenAudioTrackNumber(0), fChosenSubtitleTrackNumber(0) {
  fDemuxesTable = HashTable::create(ONE_WORD_HASH_KEYS);

  // Initialize ourselves by parsing the file's 'Track' headers:
  fParserForInitialization
    = new MatroskaFileParser(*this, ByteStreamFileSource::createNew(envir(), fileName),
			     handleEndOfTrackHeaderParsing, this, NULL);
}

MatroskaFile::~MatroskaFile() {
  delete fParserForInitialization;

  // Delete any outstanding "MatroskaDemux"s, and the table for them:
  MatroskaDemux* demux;
  while ((demux = (MatroskaDemux*)fDemuxesTable->RemoveNext()) != NULL) {
    delete demux;
  }
  delete fDemuxesTable;

  delete[] (char*)fPreferredLanguage;
  delete[] (char*)fFileName;
}

void MatroskaFile::handleEndOfTrackHeaderParsing(void* clientData) {
  ((MatroskaFile*)clientData)->handleEndOfTrackHeaderParsing();
}

class TrackChoiceRecord {
public:
  unsigned trackNumber;
  u_int8_t trackType;
  unsigned choiceFlags;
};

void MatroskaFile::handleEndOfTrackHeaderParsing() {
  // Having parsed all of our track headers, iterate through the tracks to figure out which ones should be played.
  // The Matroska 'specification' is rather imprecise about this (as usual).  However, we use the following algorithm:
  // - Use one (but no more) enabled track of each type (video, audio, subtitle).  (Ignore all tracks that are not 'enabled'.)
  // - For each track type, choose the one that's 'forced'.
  //     - If more than one is 'forced', choose the first one that matches our preferred language, or the first if none matches.
  //     - If none is 'forced', choose the one that's 'default'.
  //     - If more than one is 'default', choose the first one that matches our preferred language, or the first if none matches.
  //     - If none is 'default', choose the first one that matches our preferred language, or the first if none matches.
  unsigned numTracks = fTracks.numTracks();
  if (numTracks > 0) {
    TrackChoiceRecord* trackChoice = new TrackChoiceRecord[numTracks];
    unsigned numEnabledTracks = 0;
    TrackTable::Iterator iter(fTracks);
    MatroskaTrack* track;
    while ((track = iter.next()) != NULL) {
      if (!track->isEnabled || track->trackType == 0 || track->codecID == NULL) continue; // track not enabled, or not fully-defined

      trackChoice[numEnabledTracks].trackNumber = track->trackNumber;
      trackChoice[numEnabledTracks].trackType = track->trackType;

      // Assign flags for this track so that, when sorted, the largest value becomes our choice:
      unsigned choiceFlags = 0;
      if (fPreferredLanguage != NULL && track->language != NULL && strcmp(fPreferredLanguage, track->language) == 0) {
	// This track matches our preferred language:
	choiceFlags |= 1;
      }
      if (track->isForced) {
	choiceFlags |= 4;
      } else if (track->isDefault) {
	choiceFlags |= 2;
      }
      trackChoice[numEnabledTracks].choiceFlags = choiceFlags;

      ++numEnabledTracks;
    }

    // Choose the desired track for each track type:
    for (u_int8_t trackType = 0x01; trackType != MATROSKA_TRACK_TYPE_OTHER; trackType <<= 1) {
      int bestNum = -1;
      int bestChoiceFlags = -1;
      for (unsigned i = 0; i < numEnabledTracks; ++i) {
	if (trackChoice[i].trackType == trackType && (int)trackChoice[i].choiceFlags > bestChoiceFlags) {
	  bestNum = i;
	  bestChoiceFlags = (int)trackChoice[i].choiceFlags;
	}
      }
      if (bestChoiceFlags >= 0) { // There is a track for this track type
	if (trackType == MATROSKA_TRACK_TYPE_VIDEO) fChosenVideoTrackNumber = trackChoice[bestNum].trackNumber;
	else if (trackType == MATROSKA_TRACK_TYPE_AUDIO) fChosenAudioTrackNumber = trackChoice[bestNum].trackNumber;
	else fChosenSubtitleTrackNumber = trackChoice[bestNum].trackNumber;
      }
    }

    delete[] trackChoice;
  }
  
#ifdef DEBUG
  if (fChosenVideoTrackNumber > 0) fprintf(stderr, "Chosen video track: #%d\n", fChosenVideoTrackNumber); else fprintf(stderr, "No chosen video track\n");
  if (fChosenAudioTrackNumber > 0) fprintf(stderr, "Chosen audio track: #%d\n", fChosenAudioTrackNumber); else fprintf(stderr, "No chosen audio track\n");
  if (fChosenSubtitleTrackNumber > 0) fprintf(stderr, "Chosen subtitle track: #%d\n", fChosenSubtitleTrackNumber); else fprintf(stderr, "No chosen subtitle track\n");
#endif

  // Delete our parser, because it's done its job now:
  delete fParserForInitialization; fParserForInitialization = NULL;

  // Finally, signal our caller that we've been created and initialized:
  if (fOnCreation != NULL) (*fOnCreation)(this, fOnCreationClientData);
}

MatroskaDemux* MatroskaFile::newDemux() {
  MatroskaDemux* demux = new MatroskaDemux(*this);
  fDemuxesTable->Add((char const*)demux, demux);

  return demux;
}

void MatroskaFile::removeDemux(MatroskaDemux* demux) {
  fDemuxesTable->Remove((char const*)demux);
}


////////// MatroskaFile::TrackTable implementation //////////

MatroskaFile::TrackTable::TrackTable()
  : fTable(HashTable::create(ONE_WORD_HASH_KEYS)) {
}

MatroskaFile::TrackTable::~TrackTable() {
  // Remove and delete all of our "MatroskaTrack" descriptors, and the hash table itself:
  MatroskaTrack* track;
  while ((track = (MatroskaTrack*)fTable->RemoveNext()) != NULL) {
    delete track;
  }
  delete fTable;
} 

void MatroskaFile::TrackTable::add(MatroskaTrack* newTrack, unsigned trackNumber) {
  if (newTrack != NULL && newTrack->trackNumber != 0) fTable->Remove((char const*)newTrack->trackNumber);
  MatroskaTrack* existingTrack = (MatroskaTrack*)fTable->Add((char const*)trackNumber, newTrack);
  delete existingTrack; // in case it wasn't NULL
}

MatroskaTrack* MatroskaFile::TrackTable::lookup(unsigned trackNumber) {
  return (MatroskaTrack*)fTable->Lookup((char const*)trackNumber);
}

unsigned MatroskaFile::TrackTable::numTracks() const { return fTable->numEntries(); }

MatroskaFile::TrackTable::Iterator::Iterator(MatroskaFile::TrackTable& ourTable)
  : fOurTable(ourTable) {
  fIter = HashTable::Iterator::create(*(ourTable.fTable));
}

MatroskaFile::TrackTable::Iterator::~Iterator() {
  delete fIter;
}

MatroskaTrack* MatroskaFile::TrackTable::Iterator::next() {
  char const* key;
  return (MatroskaTrack*)fIter->next(key);
}


////////// MatroskaTrack implementation //////////

MatroskaTrack::MatroskaTrack()
  : trackNumber(0/*not set*/), trackType(0/*unknown*/),
    isEnabled(True), isDefault(True), isForced(False),
    defaultDuration(0),
    name(NULL), language(NULL), codecID(NULL),
    samplingFrequency(0), numChannels(2), mimeType(""),
    codecPrivateSize(0), codecPrivate(NULL), headerStrippedBytesSize(0), headerStrippedBytes(NULL),
    subframeSizeSize(0) {
}

MatroskaTrack::~MatroskaTrack() {
  delete[] name; delete[] language; delete[] codecID;
  delete[] codecPrivate;
  delete[] headerStrippedBytes;
}


////////// MatroskaDemux implementation //////////

MatroskaDemux::MatroskaDemux(MatroskaFile& ourFile)
  : Medium(ourFile.envir()),
    fOurFile(ourFile), fDemuxedTracksTable(HashTable::create(ONE_WORD_HASH_KEYS)) {
  fOurParser = new MatroskaFileParser(ourFile, ByteStreamFileSource::createNew(envir(), ourFile.fileName()),
				      handleEndOfFile, this, this);
}

MatroskaDemux::~MatroskaDemux() {
  // Begin by acting as if we've reached the end of the source file.  This should cause all of our demuxed tracks to get closed.
  handleEndOfFile();

  // Then delete our table of "MatroskaDemuxedTrack"s
  // - but not the "MatroskaDemuxedTrack"s themselves; that should have already happened:
  delete fDemuxedTracksTable;

  delete fOurParser;
  fOurFile.removeDemux(this);
}

FramedSource* MatroskaDemux::newDemuxedTrack(unsigned trackNumber) {
  FramedSource* track = new MatroskaDemuxedTrack(envir(), trackNumber, *this);
  fDemuxedTracksTable->Add((char const*)trackNumber, track);
  return track;
}

MatroskaDemuxedTrack* MatroskaDemux::lookupDemuxedTrack(unsigned trackNumber) {
  return (MatroskaDemuxedTrack*)fDemuxedTracksTable->Lookup((char const*)trackNumber);
}

void MatroskaDemux::removeTrack(unsigned trackNumber) {
  fDemuxedTracksTable->Remove((char const*)trackNumber);
  if (fDemuxedTracksTable->numEntries() == 0) {
    // We no longer have any demuxed tracks, so delete ourselves now:
    delete this;
  }
}

void MatroskaDemux::continueReading() {
  fOurParser->continueParsing();  
}

void MatroskaDemux::handleEndOfFile(void* clientData) {
  ((MatroskaDemux*)clientData)->handleEndOfFile();
}

void MatroskaDemux::handleEndOfFile() {
  // Iterate through all of our 'demuxed tracks', handling 'end of input' on each one.
  // Hack: Because this can cause the hash table to get modified underneath us, we don't call the handlers until after we've
  // first iterated through all of the tracks.
  unsigned numTracks = fDemuxedTracksTable->numEntries();
  if (numTracks == 0) return;
  MatroskaDemuxedTrack** tracks = new MatroskaDemuxedTrack*[numTracks];

  HashTable::Iterator* iter = HashTable::Iterator::create(*fDemuxedTracksTable);
  unsigned i, trackNumber;
  for (i = 0; i < numTracks; ++i) {
    tracks[i] = (MatroskaDemuxedTrack*)iter->next((char const*&)trackNumber);
  }
  delete iter;

  for (i = 0; i < numTracks; ++i) {
    if (tracks[i] == NULL) continue; // sanity check; shouldn't happen
    FramedSource::handleClosure(tracks[i]);
  }

  delete[] tracks;
}
