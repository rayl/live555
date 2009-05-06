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
// MP3 HTTP Sources
// Implementation

#include "MP3HTTPSource.hh"
#include "GroupsockHelper.hh"
#include "MP3StreamState.hh"

#if defined(__WIN32__) || defined(_WIN32)
#define _close closesocket
#else
#define _close close
#endif


MP3HTTPSource* MP3HTTPSource::createNew(UsageEnvironment& env,
					NetAddress const& remoteAddress,
					Port remotePort,
					char const* remoteHostName,
					char const* fileName) {
  int ourSocket = -1;
  MP3HTTPSource* newSource = NULL;

  do {
    // Create a stream socket for this source.
    // Note: We don't make the socket non-blocking, because we want
    // to read from it synchronously (as we do with real file sources)
    ourSocket = setupStreamSocket(env, 0, False);
    if (ourSocket < 0) break;

    // Connect to the remote endpoint:
    struct sockaddr_in remoteName;
    remoteName.sin_family = AF_INET;
    remoteName.sin_port = remotePort.num();
    remoteName.sin_addr.s_addr = *(unsigned*)(remoteAddress.data());
    if (connect(ourSocket, (struct sockaddr*)&remoteName, sizeof remoteName)
	!= 0) {
      env.setResultErrMsg("connect() failed: ");
      break;
    }

    // Make sure we have a big receive buffer:
    if (!increaseReceiveBufferTo(env, ourSocket, 100*1024)) break;
    
    // Try to make the new socket into a FILE*:
    unsigned streamLength = 0; //#####
    FILE* fid = fdopen(ourSocket, "r+b");
    if (fid == NULL) {
      // HACK HACK HACK #####
      // We couldn't convert the socket to a FILE*; perhaps this is Windoze?
      // Instead, tell the low-level to read it directly as a socket:
      fid = (FILE*)ourSocket;
      streamLength = (unsigned)(-1);
    }

    newSource = new MP3HTTPSource(env, fid);
    if (newSource == NULL) break;
    
    newSource->assignStream(fid, streamLength);

    // Write the HTTP 'GET' command:
    newSource->writeGetCmd(remoteHostName, ntohs(remotePort.num()),
			   fileName);

    // Now read the first frame header, to finish initializing the stream:
    if (!newSource->initializeStream()) break;

    return newSource;
  } while (0);

  if (ourSocket != -1) ::_close(ourSocket);
  delete newSource;
  return NULL;
}

MP3HTTPSource::MP3HTTPSource(UsageEnvironment& env, FILE* fid)
  : MP3FileSource(env, fid) {
}

MP3HTTPSource::~MP3HTTPSource() {
}

void MP3HTTPSource::writeGetCmd(char const* hostName, unsigned portNum,
				char const* fileName) {
  streamState()->writeGetCmd(hostName, portNum, fileName);
}
