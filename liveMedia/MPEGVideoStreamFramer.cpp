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
// A filter that breaks up an MPEG video elementary stream into
//   headers and frames
// Implementation

#include "MPEGVideoStreamFramer.hh"

////////// TimeCode implementation //////////

TimeCode::TimeCode()
  : days(0), hours(0), minutes(0), seconds(0), pictures(0) {
}

TimeCode::~TimeCode() {
}

int TimeCode::operator==(TimeCode const& arg2) {
  return pictures == arg2.pictures && seconds == arg2.seconds
    && minutes == arg2.minutes && hours == arg2.hours && days == arg2.days;
}


////////// MPEGVideoStreamFramer implementation //////////

MPEGVideoStreamFramer::MPEGVideoStreamFramer(UsageEnvironment& env,
					     FramedSource* inputSource)
  : FramedFilter(env, inputSource),
    fPictureEndMarker(False) {
}

MPEGVideoStreamFramer::~MPEGVideoStreamFramer() {
}
