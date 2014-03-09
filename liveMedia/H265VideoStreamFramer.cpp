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
// Copyright (c) 1996-2014 Live Networks, Inc.  All rights reserved.
// A filter that breaks up a H.265 Video Elementary Stream into NAL units.
// Implementation

#include "H265VideoStreamFramer.hh"
#include "H264VideoRTPSource.hh" // for "parseSPropParameterSets()"

H265VideoStreamFramer* H265VideoStreamFramer
::createNew(UsageEnvironment& env, FramedSource* inputSource, Boolean includeStartCodeInOutput) {
  return new H265VideoStreamFramer(env, inputSource, True, includeStartCodeInOutput);
}

H265VideoStreamFramer
::H265VideoStreamFramer(UsageEnvironment& env, FramedSource* inputSource, Boolean createParser, Boolean includeStartCodeInOutput)
  : H264or5VideoStreamFramer(265, env, inputSource, createParser, includeStartCodeInOutput) {
}

H265VideoStreamFramer::~H265VideoStreamFramer() {
}

Boolean H265VideoStreamFramer::isH265VideoStreamFramer() const {
  return True;
}

void H265VideoStreamFramer::setVPSandSPSandPPS(char const* sPropParameterSetsStr) {
  unsigned numSPropRecords;
  SPropRecord* sPropRecords = parseSPropParameterSets(sPropParameterSetsStr, numSPropRecords);
  for (unsigned i = 0; i < numSPropRecords; ++i) {
    if (sPropRecords[i].sPropLength == 0) continue; // bad data
    u_int8_t nal_unit_type = ((sPropRecords[i].sPropBytes[0])&0x7E)>>1;
    if (nal_unit_type == 32/*VPS*/) {
      saveCopyOfVPS(sPropRecords[i].sPropBytes, sPropRecords[i].sPropLength);
    } else if (nal_unit_type == 33/*SPS*/) {
      saveCopyOfSPS(sPropRecords[i].sPropBytes, sPropRecords[i].sPropLength);
    } else if (nal_unit_type == 34/*PPS*/) {
      saveCopyOfPPS(sPropRecords[i].sPropBytes, sPropRecords[i].sPropLength);
    }
  }
  delete[] sPropRecords;
}
