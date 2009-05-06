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
// IO event handlers
// Implementation

#include "IOHandlers.hh"

void socketReadHandler(Socket* sock, int /*mask*/) {
	// It's possible for Tcl's event handling code to call this
	// routine even after "sock" has been deleted.
	// Check for this first:
	//##### (RSF 9/27/97: I think this bug has now been fixed, but just to
	//#####  be on the safe side, we retain this "sanityHack" check.)
	if (sock->sanityHack != 42+sock->socketNum())
		return;

	unsigned char* data;
	unsigned bytesRead;
	UsageEnvironment& saveEnv = sock->env();
		// because handleRead(), if it fails, may delete "sock"
	struct sockaddr_in fromAddress;
	if (!sock->handleRead(data, bytesRead, fromAddress)) {
		saveEnv.reportBackgroundError();
	}
}
