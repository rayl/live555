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
// C++ header

#ifndef _H265_VIDEO_STREAM_FRAMER_HH
#define _H265_VIDEO_STREAM_FRAMER_HH

#ifndef _H264_OR_5_VIDEO_STREAM_FRAMER_HH
#include "H264or5VideoStreamFramer.hh"
#endif

class H265VideoStreamFramer: public H264or5VideoStreamFramer {
public:
  static H265VideoStreamFramer* createNew(UsageEnvironment& env, FramedSource* inputSource,
					  Boolean includeStartCodeInOutput = False);

  void getVPSandSPSandPPS(u_int8_t*& vps, unsigned& vpsSize,
			  u_int8_t*& sps, unsigned& spsSize,
			  u_int8_t*& pps, unsigned& ppsSize) const {
    // Returns pointers to copies of the most recently seen VPS (video parameter set)
    // SPS (sequence parameter set) and PPS (picture parameter set) NAL units.
    // (NULL pointers are returned if the NAL units have not yet been seen.)
    vps = fLastSeenVPS; vpsSize = fLastSeenVPSSize;
    sps = fLastSeenSPS; spsSize = fLastSeenSPSSize;
    pps = fLastSeenPPS; ppsSize = fLastSeenPPSSize;
  }

  void setVPSandSPSandPPS(u_int8_t* vps, unsigned vpsSize,
			  u_int8_t* sps, unsigned spsSize,
			  u_int8_t* pps, unsigned ppsSize) {
    // Assigns copies of the VPS, SPS and PPS NAL units.  If this function is not called,
    // then these NAL units are assigned only if/when they appear in the input stream.  
    saveCopyOfVPS(vps, vpsSize);
    saveCopyOfSPS(sps, spsSize);
    saveCopyOfPPS(pps, ppsSize);
  }
  void setVPSandSPSandPPS(char const* sPropParameterSetsStr);
    // As above, except that the VPS, SPS and PPS NAL units are decoded from the input string,
    // which must be a Base-64 encoding of these NAL units (in either order), separated by
    // a comma.  (This string is typically found in a SDP description, and accessed
    // using "MediaSubsession::fmtp_spropparametersets()".

protected:
  H265VideoStreamFramer(UsageEnvironment& env, FramedSource* inputSource, Boolean createParser, Boolean includeStartCodeInOutput);
      // called only by "createNew()"
  virtual ~H265VideoStreamFramer();

  // redefined virtual functions:
  virtual Boolean isH265VideoStreamFramer() const;
};

#endif
