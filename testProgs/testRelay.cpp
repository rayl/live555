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
// Copyright (c) 1996-2001, Live Networks, Inc.  All rights reserved
// A test program that receives a UDP multicast stream
// and retransmits it to another (multicast or unicast) address & port 
// main program

#include "Groupsock.hh"
#include "GroupsockHelper.hh"
#include "BasicUsageEnvironment.hh"

UsageEnvironment* env;
void readHandler(void*, int); // forward

// To receive a "source-specific multicast" (SSM) stream, uncomment this:
//#define USE_SSM 1

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  // Create a 'groupsock' for the input multicast group,port:
  char* sessionAddressStr
#ifdef USE_SSM
    = "232.255.42.42";
#else
    = "239.255.42.42";
#endif
  struct in_addr sessionAddress;
  sessionAddress.s_addr = our_inet_addr(sessionAddressStr);

  const Port port(8888);
  const unsigned char ttl = 0; // we're only reading from this mcast group
  
#ifdef USE_SSM
  char* sourceAddressStr = "aaa.bbb.ccc.ddd";
                           // replace this with the real source address
  struct in_addr sourceFilterAddress;
  sourceFilterAddress.s_addr = our_inet_addr(sourceAddressStr);

  Groupsock inputGroupsock(*env, sessionAddress, sourceFilterAddress, port);
#else
  Groupsock inputGroupsock(*env, sessionAddress, port, ttl);
#endif
  
  // Start reading and processing incoming packets:
  env->taskScheduler()
    .turnOnBackgroundReadHandling(inputGroupsock.socketNum(),
				  readHandler, &inputGroupsock);

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}


static unsigned const maxPacketSize = 65536;
static unsigned char pkt[maxPacketSize+1];

void readHandler(void* clientData, int /*mask*/) {
  static OutputSocket* outputSocket = NULL;
  static unsigned outputAddress = 0;
  if (outputSocket == NULL) {
    // Create a socket for output:
    outputSocket = new OutputSocket(*env);

    char* outputAddressStr = "239.255.43.43"; // this could also be unicast
    outputAddress = our_inet_addr(outputAddressStr);
  }

  // Read the packet from the input socket:
  Groupsock* inputGroupsock = (Groupsock*)clientData;
  unsigned packetSize;
  struct sockaddr_in fromAddress; // not used
  if (!inputGroupsock->handleRead(pkt, maxPacketSize,
				  packetSize, fromAddress)) return;

  // And write it out to the output socket (with port 4444, TTL 255):
  outputSocket->write(outputAddress, 4444, 255, pkt, packetSize);
}
