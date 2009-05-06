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
// A template for a MediaSource encapsulating an audio/video input device
// Implementation

#include "DeviceSource.hh"

DeviceSource*
DeviceSource::createNew(UsageEnvironment& env,
			DeviceParameters params) {
  return new DeviceSource(env, params);
}

DeviceSource::DeviceSource(UsageEnvironment& env,
			   DeviceParameters params)
  : FramedSource(env), fParams(params) {
  // Any initialization of the device would be done here
}

DeviceSource::~DeviceSource() {
}

float DeviceSource::getPlayTime(unsigned numFrames) const {
  return 0.0; // Replace this with the actual playing time, in seconds
}

void DeviceSource::doGetNextFrame() {

  // Arrange here for our "deliverFrame" member function to be called
  // when the next frame of data becomes available from the device.
  // This must be done in a non-blocking fashion - i.e., so that we
  // return immediately from this function even if no data is
  // currently available.
  //
  // If the device can be implemented as a readable socket, then one easy
  // way to do this is using a call to
  //     envir().taskScheduler().turnOnBackgroundReadHandling( ... )
  // (See examples of this call in the "liveMedia" directory.)

  // If, for some reason, the source device stops being readable
  // (e.g., it gets closed), then you do the following:
  if (0 /* the source stops being readable */) {
    handleClosure(this);
    return;
  }
}

void DeviceSource::deliverFrame() {
  // This would be called when new frame data is available from the device.
  // This function should deliver the next frame of data from the device,
  // using the following parameters (class members):
  // fTo: The frame data is copied here
  // fMaxSize: This is the maximum number of bytes that can be copied
  //     (If the actual frame is larger than this, then it should
  //      be truncated.)
  // fFrameSize: The resulting frame size is copied here 
  // fPresentationTime: The frame's relative presentation time
  //     (seconds, microseconds) is copied here

  // Deliver the data here:

  // After delivering the data, switch to another task, and inform
  // the reader that he has data:
  nextTask()
    = envir().taskScheduler().scheduleDelayedTask(0, (TaskFunc*)afterGetting,
						  this);
}
