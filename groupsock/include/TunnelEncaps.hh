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
// "mTunnel" multicast access service
// Copyright (c) 1996-1998 Live Networks, Inc.  All rights reserved.
// Encapsulation trailer for tunnels
// C++ header

#ifndef _TUNNEL_ENCAPS_HH
#define _TUNNEL_ENCAPS_HH

#ifndef _NET_ADDRESS_HH
#include "NetAddress.hh"
#endif

typedef unsigned short Cookie;

class TunnelEncapsulationTrailer {
	// The trailer is layed out as follows:
	// bytes 0-1:	source 'cookie'
	// bytes 2-3:	destination 'cookie'
	// bytes 4-7:	address
	// bytes 8-9:	port
	// byte 10:	ttl
	// byte 11:	command

        // Optionally, there may also be a 4-byte 'auxilliary address'
        // (e.g., for 'source-specific multicast' preceding this)
        // bytes -4 through -1: auxilliary address

    public:
	Cookie& srcCookie()
		{ return *(Cookie*)byteOffset(0); } 
	Cookie& dstCookie()
		{ return *(Cookie*)byteOffset(2); } 
	unsigned& address()
		{ return *(unsigned*)byteOffset(4); }
	Port& port()
		{ return *(Port*)byteOffset(8); }
	unsigned char& ttl()
		{ return *(unsigned char*)byteOffset(10); }
	unsigned char& command()
		{ return *(unsigned char*)byteOffset(11); }

        unsigned& auxAddress()
                { return *(unsigned*)byteOffset(-4); }

    private:
	inline char* byteOffset(int charIndex)
		{ return ((char*)this) + charIndex; }
};

const unsigned TunnelEncapsulationTrailerSize = 12; // bytes
const unsigned TunnelEncapsulationTrailerAuxSize = 4; // bytes
const unsigned TunnelEncapsulationTrailerMaxSize
    = TunnelEncapsulationTrailerSize + TunnelEncapsulationTrailerAuxSize;

// Command codes:
// 0: unused
const unsigned char TunnelDataCmd = 1;
const unsigned char TunnelJoinGroupCmd = 2;
const unsigned char TunnelLeaveGroupCmd = 3;
const unsigned char TunnelTearDownCmd = 4;
const unsigned char TunnelProbeCmd = 5;
const unsigned char TunnelProbeAckCmd = 6;
const unsigned char TunnelProbeNackCmd = 7;
const unsigned char TunnelJoinRTPGroupCmd = 8;
const unsigned char TunnelLeaveRTPGroupCmd = 9;
// 0x0A through 0x10: currently unused.
const unsigned char TunnelExtensionFlag = 0x80; // a flag, not a cmd code
const unsigned char TunnelDataAuxCmd
    = (TunnelExtensionFlag|TunnelDataCmd);
const unsigned char TunnelJoinGroupAuxCmd
    = (TunnelExtensionFlag|TunnelJoinGroupCmd);
const unsigned char TunnelLeaveGroupAuxCmd
    = (TunnelExtensionFlag|TunnelLeaveGroupCmd);
// Note: the TearDown, Probe, ProbeAck, ProbeNack cmds have no Aux version
// 0x84 through 0x87: currently unused.
const unsigned char TunnelJoinRTPGroupAuxCmd
    = (TunnelExtensionFlag|TunnelJoinRTPGroupCmd);
const unsigned char TunnelLeaveRTPGroupAuxCmd
    = (TunnelExtensionFlag|TunnelLeaveRTPGroupCmd);
// 0x8A through 0xFF: currently unused

inline Boolean TunnelIsAuxCmd(unsigned char cmd) {
  return (cmd&TunnelExtensionFlag) != 0;
}

#endif
