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
// Copyright (c) 1996-2001 Live Networks, Inc.  All rights reserved.
// Helper routines to implement 'group sockets'
// Implementation

#include "GroupsockHelper.hh"

#if defined(__WIN32__) || defined(_WIN32)
#include <time.h>
extern "C" int initializeWinsockIfNecessary();
#define _close closesocket
#else
#include <stdarg.h>
#include <time.h>
#include <fcntl.h>
#define initializeWinsockIfNecessary() 1
#define _close close
#endif
#include <stdio.h>

// By default, use INADDR_ANY for the sending and receiving interfaces:
unsigned SendingInterfaceAddr = INADDR_ANY;
unsigned ReceivingInterfaceAddr = INADDR_ANY;

static void socketErr(UsageEnvironment& env, char* errorMsg) {
#if defined(__WIN32__) || defined(_WIN32)
	// On Windows, sometimes system calls fail, but with "errno" == 0
	// Treat this especially, so it can be handled at a higher level:
	if (errno == 0) {
		errno = WSAGetLastError();
	}
	if (errno == 0) {
		env.setResultMsg("no_error ", errorMsg);
		return;
	}
#endif
	env.setResultErrMsg(errorMsg);
}

int setupDatagramSocket(UsageEnvironment& env, Port port,
#ifdef IP_MULTICAST_LOOP
			Boolean setLoopback
#else
			Boolean
#endif
) {
  if (!initializeWinsockIfNecessary()) {
    socketErr(env, "Failed to initialize 'winsock': ");
    return -1;
  }
  
  int newSocket = socket(AF_INET, SOCK_DGRAM, 0);
  if (newSocket < 0) {
    socketErr(env, "unable to create datagram socket: ");
    return newSocket;
  }
  
  const int reuseFlag = 1;
  if (setsockopt(newSocket, SOL_SOCKET, SO_REUSEADDR,
		 (const char*)&reuseFlag, sizeof reuseFlag) < 0) {
    socketErr(env, "setsockopt(SO_REUSEADDR) error: ");
    _close(newSocket);
    return -1;
  }
  
#if defined(__WIN32__) || defined(_WIN32)
  // Windoze doesn't handle SO_REUSEPORT or IP_MULTICAST_LOOP
#else
#ifdef SO_REUSEPORT
  if (setsockopt(newSocket, SOL_SOCKET, SO_REUSEPORT,
		 (const char*)&reuseFlag, sizeof reuseFlag) < 0) {
    socketErr(env, "setsockopt(SO_REUSEPORT) error: ");
    _close(newSocket);
    return -1;
  }
#endif
  
#ifdef IP_MULTICAST_LOOP
  const u_char loop = (u_char)setLoopback;
  if (setsockopt(newSocket, IPPROTO_IP, IP_MULTICAST_LOOP,
		 (const char*)&loop, sizeof loop) < 0) {
    socketErr(env, "setsockopt(IP_MULTICAST_LOOP) error: ");
    _close(newSocket);
    return -1;
  }
#endif
#endif
  
  // Note: Windoze requires binding, even if the port number is 0
#if defined(__WIN32__) || defined(_WIN32)
#else
  if (port.num() != 0) {
#endif
    struct sockaddr_in name;
    name.sin_family = AF_INET;
    name.sin_port = port.num();
    name.sin_addr.s_addr = ReceivingInterfaceAddr;
    if (bind(newSocket, (struct sockaddr*)&name, sizeof name) != 0) {
      char tmpBuffer[100];
      sprintf(tmpBuffer, "bind() error (port number: %d): ",
	      ntohs(port.num()));
      socketErr(env, tmpBuffer);
      _close(newSocket);
      return -1;
    }
#if defined(__WIN32__) || defined(_WIN32)
#else
  }
#endif
  
  // Set the sending interface for multicasts, if it's not the default:
  if (SendingInterfaceAddr != INADDR_ANY) {
    struct in_addr addr;
    addr.s_addr = SendingInterfaceAddr;
    
    if (setsockopt(newSocket, IPPROTO_IP, IP_MULTICAST_IF,
		   (const char*)&addr, sizeof addr) < 0) {
      socketErr(env, "error setting outgoing multicast interface: ");
      _close(newSocket);
      return -1;
    }
  }
  
  return newSocket;
}

int setupStreamSocket(UsageEnvironment& env,
                      Port port, Boolean makeNonBlocking) {
  if (!initializeWinsockIfNecessary()) {
    socketErr(env, "Failed to initialize 'winsock': ");
    return -1;
  }
  
  int newSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (newSocket < 0) {
    socketErr(env, "unable to create stream socket: ");
    return newSocket;
  }
  
  const int reuseFlag = 1;
  if (setsockopt(newSocket, SOL_SOCKET, SO_REUSEADDR,
		 (const char*)&reuseFlag, sizeof reuseFlag) < 0) {
    socketErr(env, "setsockopt(SO_REUSEADDR) error: ");
    _close(newSocket);
    return -1;
  }
  
#if defined(__WIN32__) || defined(_WIN32)
    // Windoze doesn't handle SO_REUSEPORT
#else
#ifdef SO_REUSEPORT
  if (setsockopt(newSocket, SOL_SOCKET, SO_REUSEPORT,
		 (const char*)&reuseFlag, sizeof reuseFlag) < 0) {
    socketErr(env, "setsockopt(SO_REUSEPORT) error: ");
    _close(newSocket);
    return -1;
  }
#endif
#endif

  // Note: Windoze requires binding, even if the port number is 0
#if defined(__WIN32__) || defined(_WIN32)
#else
  if (port.num() != 0) {
#endif
    struct sockaddr_in name;
    name.sin_family = AF_INET;
    name.sin_port = port.num();
    name.sin_addr.s_addr = ReceivingInterfaceAddr;
    if (bind(newSocket, (struct sockaddr*)&name, sizeof name) != 0) {
      char tmpBuffer[100];
      sprintf(tmpBuffer, "bind() error (port number: %d): ",
	      ntohs(port.num()));
      socketErr(env, tmpBuffer);
      _close(newSocket);
      return -1;
    }
#if defined(__WIN32__) || defined(_WIN32)
#else
  }
#endif

  if (makeNonBlocking) {
    // Make the socket non-blocking:
#if defined(__WIN32__) || defined(_WIN32)
    u_long FAR arg = 1;
    if (ioctlsocket(newSocket, FIONBIO, &arg) != 0) {
#else
    int curFlags = fcntl(newSocket, F_GETFL, 0);
    if (fcntl(newSocket, F_SETFL, curFlags|O_NONBLOCK) < 0) {
#endif
      socketErr(env, "failed to make non-blocking: ");
      _close(newSocket);
      return -1;
    }
  }

  return newSocket;
}

int readSocket(UsageEnvironment& env,
	       int socket, unsigned char* buffer, unsigned bufferSize,
	       struct sockaddr_in& fromAddress,
	       struct timeval* timeout) {
	int bytesRead = -1; 
	do {
		fd_set rd_set;
		FD_ZERO(&rd_set);
		if (socket < 0) break;
		FD_SET((unsigned) socket, &rd_set);
		const unsigned numFds = socket+1;
    		
		int selectResult
			= select(numFds, &rd_set, NULL, NULL, timeout);
		if (selectResult == 0 && timeout != NULL) {
			bytesRead = 0;
			break;
		} else if (selectResult <= 0) {
			socketErr(env, "select() error: ");
			break;
		}

		if (!FD_ISSET(socket, &rd_set)) {
			socketErr(env, "select() error - !FD_ISSET");
			break;
		}
    		
		SOCKLEN_T addressSize = sizeof fromAddress;
		bytesRead = recvfrom(socket, (char*)buffer, bufferSize, 0,
							 (struct sockaddr*)&fromAddress,
							 &addressSize);
		if (bytesRead < 0) {
		    //##### HACK to work around bugs in Linux and Windows:
			if (errno == 111 /*ECONNREFUSED (Linux)*/
#if defined(__WIN32__) || defined(_WIN32)
			// What a piece of crap Windows is.  Sometimes
			// recvfrom() returns -1, but with errno == 0.
			// This appears not to be a real error; just treat
			// it as if it were a read of zero bytes, and hope
			// we don't have to do anything else to 'reset'
			// this alleged error:
				|| errno == 0
#else
				|| errno == EAGAIN
#endif
				|| errno == 113 /*EHOSTUNREACH (Linux)*/) {
			        //Why does Linux return this for datagram sock?
			        fromAddress.sin_addr.s_addr = 0;
			        return 0;
			}
			//##### END HACK
			socketErr(env, "recvfrom() error: ");
			break;
		}
	} while (0);

	return bytesRead;
}

Boolean writeSocket(UsageEnvironment& env,
		    int socket, struct in_addr address, Port port,
		    unsigned char ttlArg,
		    unsigned char* buffer, unsigned bufferSize) {
#ifdef DEBUG_PRINT
  fprintf(stderr, "Writing %d-byte packet to \"%s\", port %d (host order)\n", bufferSize, our_inet_ntoa(address), ntohs(port.num()));
#endif
	do {
		if (ttlArg != 0) {
			// Before sending, set the socket's TTL:
#if defined(__WIN32__) || defined(_WIN32)
#define TTL_TYPE int
#else
#define TTL_TYPE u_char
#endif
			TTL_TYPE ttl = (TTL_TYPE)ttlArg;
			if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_TTL,
				       (const char*)&ttl, sizeof ttl) < 0) {
				socketErr(env, "setsockopt(IP_MULTICAST_TTL) error: ");
				break;
			}
		}

		struct sockaddr_in dest;
    		dest.sin_family = AF_INET;
		dest.sin_port = port.num();
		dest.sin_addr = address;

		int bytesSent = sendto(socket, (char*)buffer, bufferSize, 0,
			               (struct sockaddr*)&dest, sizeof dest);
		if (bytesSent != (int)bufferSize) {
			char tmpBuf[100];
			sprintf(tmpBuf, "writeSocket(%d), sendTo() error: wrote %d bytes instead of %u: ", socket, bytesSent, bufferSize);
			socketErr(env, tmpBuf);
			break;
		}

		return True;
	} while (0);

	return False;
}

unsigned increaseBufferTo(UsageEnvironment& env, int bufOptName,
			  int socket, unsigned requestedSize) {
	// First, get the current buffer size.  If it's already at leas
	// as big as what we're requesting, do nothing.
	unsigned curSize;
	SOCKLEN_T sizeSize = sizeof curSize;
	if (getsockopt(socket, SOL_SOCKET, bufOptName,
		       (char*)&curSize, &sizeSize) < 0) {
		socketErr(env, "increaseBufferTo() error: ");
		return 0;
	}

	// Next, try to increase the buffer to the requested size,
	// or to some smaller size, if that's not possible:
	while (requestedSize > curSize) {
		if (setsockopt(socket, SOL_SOCKET, bufOptName,
		    (char*)&requestedSize, sizeSize) >= 0) {
			// success
			return requestedSize;
		}
		requestedSize = (requestedSize+curSize)/2;
	}

	return curSize;
}

unsigned increaseSendBufferTo(UsageEnvironment& env,
			      int socket, unsigned requestedSize) {
	return increaseBufferTo(env, SO_SNDBUF, socket, requestedSize);
}
unsigned increaseReceiveBufferTo(UsageEnvironment& env,
				 int socket, unsigned requestedSize) {
	return increaseBufferTo(env, SO_RCVBUF, socket, requestedSize);
}

Boolean socketJoinGroup(UsageEnvironment& env, int socket, unsigned groupAddress){
  if (!IsMulticastAddress(groupAddress)) return True; // ignore this case

  struct ip_mreq imr;
  imr.imr_multiaddr.s_addr = groupAddress;
  imr.imr_interface.s_addr = ReceivingInterfaceAddr;
  if (setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		 (const char*)&imr, sizeof (struct ip_mreq)) < 0) {
    socketErr(env, "setsockopt(IP_ADD_MEMBERSHIP) error: ");
    return False;
  }
  
  return True;
}

Boolean socketLeaveGroup(UsageEnvironment&, int socket, unsigned groupAddress) {
  if (!IsMulticastAddress(groupAddress)) return True; // ignore this case

  struct ip_mreq imr;
  imr.imr_multiaddr.s_addr = groupAddress;
  imr.imr_interface.s_addr = ReceivingInterfaceAddr;
  if (setsockopt(socket, IPPROTO_IP, IP_DROP_MEMBERSHIP,
		 (const char*)&imr, sizeof (struct ip_mreq)) < 0) {
    return False;
  }

  return True;
}

// The source-specific join/leave operations require special setsockopt()
// commands, and a special structure (ip_mreq_source).  If the include files
// didn't define these, we do so here:
#ifndef IP_ADD_SOURCE_MEMBERSHIP
#ifdef LINUX
#define IP_ADD_SOURCE_MEMBERSHIP   39
#define IP_DROP_SOURCE_MEMBERSHIP 40
#else
#define IP_ADD_SOURCE_MEMBERSHIP   67
#define IP_DROP_SOURCE_MEMBERSHIP 68
#endif

struct ip_mreq_source {
  struct  in_addr imr_multiaddr;  /* IP multicast address of group */
  struct  in_addr imr_sourceaddr; /* IP address of source */
  struct  in_addr imr_interface;  /* local IP address of interface */
};
#endif

Boolean socketJoinGroupSSM(UsageEnvironment& env, int socket,
			   unsigned groupAddress, unsigned sourceFilterAddr) {
  if (!IsMulticastAddress(groupAddress)) return True; // ignore this case

  struct ip_mreq_source imr;
  imr.imr_multiaddr.s_addr = groupAddress;
  imr.imr_sourceaddr.s_addr = sourceFilterAddr;
  imr.imr_interface.s_addr = ReceivingInterfaceAddr;
  if (setsockopt(socket, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP,
		 (const char*)&imr, sizeof (struct ip_mreq_source)) < 0) {
    socketErr(env, "setsockopt(IP_ADD_SOURCE_MEMBERSHIP) error: ");
    return False;
  }
  
  return True;
}

Boolean socketLeaveGroupSSM(UsageEnvironment& /*env*/, int socket,
			    unsigned groupAddress, unsigned sourceFilterAddr) {
  if (!IsMulticastAddress(groupAddress)) return True; // ignore this case

  struct ip_mreq_source imr;
  imr.imr_multiaddr.s_addr = groupAddress;
  imr.imr_sourceaddr.s_addr = sourceFilterAddr;
  imr.imr_interface.s_addr = ReceivingInterfaceAddr;
  if (setsockopt(socket, IPPROTO_IP, IP_DROP_SOURCE_MEMBERSHIP,
		 (const char*)&imr, sizeof (struct ip_mreq_source)) < 0) {
    return False;
  }
  
  return True;
}

Boolean getSourcePort(UsageEnvironment& env, int socket, Port& port) {
  sockaddr_in test;
  SOCKLEN_T len = sizeof test;
  if (getsockname(socket, (struct sockaddr*)&test, &len) < 0) {
    socketErr(env, "getsockname() error: ");
    return False;
  }
  
  port = *((Port*)&test.sin_port);
  return True;
}

static Boolean badAddress(unsigned addr) {
  // Check for some possible erroneous addresses:
  unsigned hAddr = ntohl(addr);
  return (hAddr == 0x7F000001 /* 127.0.0.1 */
	  || hAddr == 0
	  || hAddr == (unsigned)0xFFFFFFFF);
}

Boolean loopbackWorks = 1;

unsigned ourSourceAddressForMulticast(UsageEnvironment& env) {
  	static unsigned ourAddress = 0;
	int sock = -1;
	struct in_addr testAddr;

	if (ourAddress == 0) do {
		// We need to find our source address

		// Get our address by sending a (0-TTL) multicast packet,
		// receiving it, and looking at the source address used.
		// (This is kinda bogus, but it provides the best guarantee
		// that other nodes will think our id is the same as we do.)
		// (This is a gross hack; there must be a better way!!!)
	
		testAddr.s_addr = our_inet_addr("228.67.43.91"); // arbitrary
		Port testPort(15947); // ditto

		sock = setupDatagramSocket(env, testPort);
		if (sock < 0) break;

		if (!socketJoinGroup(env, sock, testAddr.s_addr)) break;

		unsigned char testString[] = "hostIdTest";
		unsigned testStringLength = sizeof testString;

		if (!writeSocket(env, sock, testAddr, testPort, 0,
				 testString, testStringLength)) break;

		unsigned char readBuffer[20];
		struct sockaddr_in fromAddr;
		struct timeval timeout;
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;
		int bytesRead = readSocket(env, sock,
					   readBuffer, sizeof readBuffer,
					   fromAddr, &timeout);
		if (bytesRead == 0 // timeout occurred
		    || bytesRead != (int)testStringLength
		    || strncmp((char*)readBuffer, (char*)testString,
			       testStringLength) != 0) {
		  loopbackWorks = 0;

		  // We couldn't find our address using multicast loopback
		  // so try instead to look it up directly.
		  char hostname[100];
		  gethostname(hostname, sizeof hostname);
		  if (hostname[0] == '\0') {
			env.setResultErrMsg("initial gethostname() failed");
			break;
		  }
		  struct hostent* hstent
		    = (struct hostent*)gethostbyname(hostname);
		  if (hstent == NULL || hstent->h_length != 4) {
			env.setResultErrMsg("initial gethostbyname() failed");
			break;
		  }
		  // Take the first address that's not bad
		  // (This code, like many others, won't handle IPv6)
		  unsigned addr = 0;
		  for (unsigned i = 0; ; ++i) {
		    char* addrPtr = hstent->h_addr_list[i];
		    if (addrPtr == NULL) break;

		    unsigned a = *(unsigned*)addrPtr;
		    if (!badAddress(a)) {
		      addr = a;
		      break;
		    }
		  }
		  if (addr != 0) {
		    fromAddr.sin_addr.s_addr = addr;
		  } else {
			env.setResultMsg("no address");
			break;
		  }
		}

		// Make sure we have a good address:
		unsigned from = fromAddr.sin_addr.s_addr;
                if (badAddress(from)) {
			char tmp[100];
			sprintf(tmp,
				"This computer has an invalid IP address: 0x%x",
				(unsigned)(ntohl(from)));
			env.setResultMsg(tmp);
			break;
		}

		ourAddress = from;
	} while (0);

	if (sock >= 0) {
		socketLeaveGroup(env, sock, testAddr.s_addr);
		_close(sock);
	}

	// Use our newly-discovered IP address, and the current time,
	// to initialize the random number generator's seed:
	struct timeval timeNow;
#ifdef BSD
	struct timezone Idunno;
#else
	int Idunno;
#endif
	gettimeofday(&timeNow, &Idunno);
	unsigned seed = ourAddress^timeNow.tv_sec^timeNow.tv_usec;
	our_srandom(seed);

	return ourAddress;
}

char const* timestampString() {
	struct timeval tvNow;
#ifdef BSD
	struct timezone Idunno;
#else
	int Idunno;
#endif
	gettimeofday(&tvNow, &Idunno);

	static char timeString[9]; // holds hh:mm:ss plus trailing '\0'
	char const* ctimeResult = ctime((time_t*)&tvNow.tv_sec);
	char const* from = &ctimeResult[11];
	int i;
	for (i = 0; i < 8; ++i) {
		timeString[i] = from[i];
	}
	timeString[i] = '\0';

	return (char const*)&timeString;
}

#if defined(__WIN32__) || defined(_WIN32)
// For Windoze, we need to implement our own gettimeofday()
#include <sys/timeb.h>

int gettimeofday(struct timeval* tp, int* /*tz*/) {
	struct timeb tb;
	ftime(&tb);
	tp->tv_sec = tb.time;
	tp->tv_usec = 1000*tb.millitm;
	return 0;
}
#endif
