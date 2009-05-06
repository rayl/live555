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
// Media
// Implementation

#include "Media.hh"
#include "HashTable.hh"

// A data structure for looking up a Medium by its string name
class MediaLookupTable {
public:
  MediaLookupTable();
  virtual ~MediaLookupTable();
  
  Medium* lookup(char const* name) const;
      // Returns NULL if none already exists

  void addNew(Medium* medium, char* mediumName);
  Medium* remove(char const* name);

  void generateNewName(char* mediumName, unsigned maxLen);
  
private:
  HashTable* fTable;
  unsigned fNameGenerator;
};

static MediaLookupTable* ourMedia(UsageEnvironment& env) {
  if (env.liveMediaPriv == NULL) {
    // Create a new table to record the media that are to be created in
    // this environment:
    env.liveMediaPriv = new MediaLookupTable;
  }
  return (MediaLookupTable*)(env.liveMediaPriv);
}

////////// Medium //////////

Medium::Medium(UsageEnvironment& env)
	: fEnviron(env), fNextTask(NULL) {
  // First generate a name for the new medium:
  ourMedia(env)->generateNewName(fMediumName, mediumNameMaxLen);
  env.setResultMsg(fMediumName);

  ourMedia(env)->addNew(this, fMediumName);
}

Medium::~Medium() {
	// Remove any tasks that might be pending for us:
	fEnviron.taskScheduler().unscheduleDelayedTask(fNextTask);
}

Boolean Medium::lookupByName(UsageEnvironment& env, char const* mediumName,
				  Medium*& resultMedium) {
  resultMedium = ourMedia(env)->lookup(mediumName);
  if (resultMedium == NULL) {
    env.setResultMsg("Medium ", mediumName, " does not exist");
    return False;
  }

  return True;
}

void Medium::close(UsageEnvironment& env, char const* name) {
  Medium* toDelete = ourMedia(env)->remove(name);
  delete toDelete;
}

void Medium::close(Medium* medium) {
  if (medium == NULL) return;

  Medium* toDelete = ourMedia(medium->envir())->remove(medium->name());
  if (toDelete != medium) return; // shouldn't happen

  delete toDelete;
}

Boolean Medium::isSource() const {
  return False; // default implementation
}

Boolean Medium::isSink() const {
  return False; // default implementation
}

Boolean Medium::isRTCPInstance() const {
  return False; // default implementation
}

Boolean Medium::isRTSPClient() const {
  return False; // default implementation
}

Boolean Medium::isRTSPServer() const {
  return False; // default implementation
}

Boolean Medium::isMediaSession() const {
  return False; // default implementation
}

Boolean Medium::isServerMediaSession() const {
  return False; // default implementation
}

////////// MediaLookupTable //////////

MediaLookupTable::MediaLookupTable()
  : fTable(HashTable::create(STRING_HASH_KEYS)), fNameGenerator(0) {
}

MediaLookupTable::~MediaLookupTable() {
	delete fTable;
}
  
Medium* MediaLookupTable::lookup(char const* name) const {
  return (Medium*)(fTable->Lookup(name));
}

void MediaLookupTable::addNew(Medium* medium, char* mediumName) {
  fTable->Add(mediumName, (void*)medium);
}

Medium* MediaLookupTable::remove(char const* name) {
  Medium* medium = lookup(name);
  if (medium != NULL) fTable->Remove(name);
  
  return medium;
}

void MediaLookupTable::generateNewName(char* mediumName,
				       unsigned /*maxLen*/) {
  // We should really use snprintf() here, but not all systems have it
  sprintf(mediumName, "liveMedia%d", fNameGenerator++);
}
