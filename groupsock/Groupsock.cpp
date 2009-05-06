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
// LIVE.COM Streaming Media Libraries
// Copyright (c) 1996-1999 Live Networks, Inc.  All rights reserved.
// 'Group sockets'
// Implementation

#include "Groupsock.hh"
#include "GroupsockHelper.hh"
//##### Eventually fix the following #include; we shouldn't know about tunnels
#include "TunnelEncaps.hh"

#if defined(__WIN32__) || defined(_WIN32)
#include <strstrea.h>
#else
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <unistd.h>
#include <strstream.h>
#endif
#include <stdio.h>

///////// OutputSocket //////////

OutputSocket::OutputSocket(UsageEnvironment& env)
  : Socket(env, 0 /* let kernel choose port */),
    fSourcePort(0), fLastSentTTL(0) {
}

OutputSocket::OutputSocket(UsageEnvironment& env, Port port)
  : Socket(env, port),
    fSourcePort(0), fLastSentTTL(0) {
}

OutputSocket::~OutputSocket() {
}

Boolean OutputSocket::write(unsigned address, Port port, unsigned char ttl,
			    unsigned char* buffer, unsigned bufferSize) {
  if (ttl == fLastSentTTL) {
    // Optimization: So we don't do a 'set TTL' system call again
    ttl = 0;
  } else {
    fLastSentTTL = ttl;
  }
  struct in_addr destAddr; destAddr.s_addr = address;
  if (!writeSocket(env(), socketNum(), destAddr, port, ttl,
		   buffer, bufferSize))
    return False;
  
  if (sourcePortNum() == 0) {
    // Now that we've sent a packet, we can find out what the
    // kernel chose as our ephemeral source port number:
    if (!getSourcePort(env(), socketNum(), fSourcePort)) {
      if (DebugLevel >= 1)
	cerr << *this
	     << ": failed to get source port: "
	     << env().getResultMsg() << endl;
      return False;
    }
  }
  
  return True;
}

// By default, we don't do reads:
Boolean OutputSocket
::handleRead(unsigned char* /*buffer*/, unsigned /*bufferMaxSize*/,
	     unsigned& /*bytesRead*/, struct sockaddr_in& /*fromAddress*/) {
  return True;
}


///////// Groupsock //////////

NetInterfaceTrafficStats Groupsock::statsIncoming;
NetInterfaceTrafficStats Groupsock::statsOutgoing;
NetInterfaceTrafficStats Groupsock::statsRelayedIncoming;
NetInterfaceTrafficStats Groupsock::statsRelayedOutgoing;

// Constructor for a source-independent multicast group
Groupsock::Groupsock(UsageEnvironment& env, struct in_addr const& groupAddr,
		     Port port, unsigned char ttl)
  : OutputSocket(env, port),
    deleteIfNoMembers(False), isSlave(False),
    fIncomingGroupEId(groupAddr, port.num(), ttl),
    fOutgoingGroupEId(groupAddr, port.num(), ttl),
    fDestPort(port) {
  if (!socketJoinGroup(env, socketNum(), groupAddr.s_addr)) {
    if (DebugLevel >= 1) {
      cerr << *this << ": failed to join group: "
	   << env.getResultMsg() << endl;
    }
  }
  
  // Make sure we can get our source address:
  if (ourSourceAddressForMulticast(env) == 0) {
    if (DebugLevel >= 0) { // this is a fatal error
      cerr << "Unable to determine our source address: "
	   << env.getResultMsg() << endl;
    }
  }
  
  if (DebugLevel >= 2) cerr << *this << ": created" << endl;
}

// Constructor for a source-specific multicast group
Groupsock::Groupsock(UsageEnvironment& env, struct in_addr const& groupAddr,
		     struct in_addr const& sourceFilterAddr,
		     Port port)
  : OutputSocket(env, port),
    deleteIfNoMembers(False), isSlave(False),
    fIncomingGroupEId(groupAddr, sourceFilterAddr, port.num()),
    fOutgoingGroupEId(groupAddr, sourceFilterAddr, port.num()),
    fDestPort(port) {
  // First try a SSM join.  If that fails, try a regular join:
  if (!socketJoinGroupSSM(env, socketNum(), groupAddr.s_addr,
			  sourceFilterAddr.s_addr)) {
    if (DebugLevel >= 3) {
      cerr << *this << ": SSM join failed: "
	   << env.getResultMsg();
      cerr << "- trying regular join instead" << endl;
    }
    if (!socketJoinGroup(env, socketNum(), groupAddr.s_addr)) {
      if (DebugLevel >= 1) {
	cerr << *this << ": failed to join group: "
	     << env.getResultMsg() << endl;
      }
    }
  }
  
  if (DebugLevel >= 2) cerr << *this << ": created" << endl;
}

Groupsock::~Groupsock() {
  if (isSSM()) {
    if (!socketLeaveGroupSSM(env(), socketNum(), groupAddress().s_addr,
			     sourceFilterAddress().s_addr)) {
      socketLeaveGroup(env(), socketNum(), groupAddress().s_addr);
    }
  } else {
    socketLeaveGroup(env(), socketNum(), groupAddress().s_addr);
  }
  
  if (DebugLevel >= 2) cerr << *this << ": deleting" << endl;
}

void
Groupsock::changeDestinationParameters(struct in_addr const& newDestAddr,
				       Port newDestPort, int newDestTTL) {
  struct in_addr destAddr = fOutgoingGroupEId.groupAddress();
  if (newDestAddr.s_addr != 0 && newDestAddr.s_addr != destAddr.s_addr) {
    socketLeaveGroup(env(), socketNum(), destAddr.s_addr);
    destAddr.s_addr = newDestAddr.s_addr;
    socketJoinGroup(env(), socketNum(), destAddr.s_addr);
  }

  unsigned short destPortNum = fOutgoingGroupEId.portNum();
  if (newDestPort.num() != 0) {
    destPortNum = newDestPort.num();
    fDestPort = newDestPort;
  }

  unsigned char destTTL = ttl();
  if (newDestTTL != ~0) destTTL = (unsigned char)newDestTTL;

  fOutgoingGroupEId = GroupEId(destAddr, destPortNum, destTTL);
}

Boolean Groupsock::output(UsageEnvironment& env, unsigned char ttlToSend,
			  unsigned char* buffer, unsigned bufferSize,
			  DirectedNetInterface* interfaceNotToFwdBackTo) {
  do {
    // First, do the datagram send:
    if (!write(destAddress().s_addr, destPort(), ttlToSend,
	       buffer, bufferSize))
      break;
    statsOutgoing.countPacket(bufferSize);
    statsGroupOutgoing.countPacket(bufferSize);
    
    // Then, forward to our members:
    int numMembers =
      outputToAllMembersExcept(interfaceNotToFwdBackTo,
			       ttlToSend, buffer, bufferSize,
			       ourSourceAddressForMulticast(env));
    if (numMembers < 0) break;
    
    if (DebugLevel >= 3) {
      cerr << *this << ": wrote " << bufferSize << " bytes, ttl "
	   << (unsigned)ttlToSend;
      if (numMembers > 0) {
	cerr << "; relayed to " << numMembers << " members";
      }
      cerr << endl;
    }
    return True;
  } while (0);
  
  if (DebugLevel >= 0) { // this is a fatal error
    ostrstream out;
    out << *this << ": write failed: " << env.getResultMsg() << endl;
    env.setResultMsg(out.str());
  }
  return False;
}

Boolean Groupsock::handleRead(unsigned char* buffer, unsigned bufferMaxSize,
			      unsigned& bytesRead,
			      struct sockaddr_in& fromAddress) {
  // Read data from the socket, and relay it across any attached tunnels
  //##### later make this code more general - independent of tunnels
  
  bytesRead = 0;
  
  int maxBytesToRead = bufferMaxSize - TunnelEncapsulationTrailerMaxSize;
  int numBytes = readSocket(env(), socketNum(),
			    buffer, maxBytesToRead, fromAddress);
  if (numBytes < 0) {
    if (DebugLevel >= 0) { // this is a fatal error
      ostrstream out;
      out << *this
	  << ": read failed: " << env().getResultMsg()
	  << endl;
      env().setResultMsg(out.str());
    }
    return False;
  }
  
  // If we're a SSM group, make sure the source address matches:
  if (isSSM()
      && fromAddress.sin_addr.s_addr != sourceFilterAddress().s_addr) {
    return True;
  }
  
  // We'll handle this data.
  // Also write it (with the encapsulation trailer) to each member,
  // unless the packet was originally sent by us to begin with.
  bytesRead = numBytes;
  
  int numMembers = 0;
  if (!wasLoopedBackFromUs(env(), fromAddress)) {
    statsIncoming.countPacket(numBytes);
    statsGroupIncoming.countPacket(numBytes);
    numMembers =
      outputToAllMembersExcept(NULL, ttl(),
			       buffer, bytesRead,
			       fromAddress.sin_addr.s_addr);
    if (numMembers > 0) {
      statsRelayedIncoming.countPacket(numBytes);
      statsGroupRelayedIncoming.countPacket(numBytes);
    }
  }
  if (DebugLevel >= 3) {
    cerr << *this << ": read " << bytesRead << " bytes from ";
    cerr << our_inet_ntoa(fromAddress.sin_addr);
    if (numMembers > 0) {
      cerr << "; relayed to " << numMembers << " members";
    }
    cerr << endl;
  }
  
  return True;
}

Boolean Groupsock::wasLoopedBackFromUs(UsageEnvironment& env,
				       struct sockaddr_in& fromAddress) {
  if (fromAddress.sin_addr.s_addr
      == ourSourceAddressForMulticast(env)) {
    if (fromAddress.sin_port == sourcePortNum()) {
#ifdef DEBUG_LOOPBACK_CHECKING
      if (DebugLevel >= 3) {
	cerr << *this <<": got looped-back packet"
	     << endl;
      }
#endif
      return True;
    }
  }
  
  return False;
}

int Groupsock::outputToAllMembersExcept(DirectedNetInterface* exceptInterface,
					unsigned char ttlToFwd,
					unsigned char* data, unsigned size,
					unsigned sourceAddr) {
  // Don't forward TTL-0 packets
  if (ttlToFwd == 0) return 0;
  
  DirectedNetInterfaceSet::Iterator iter(members());
  unsigned numMembers = 0;
  DirectedNetInterface* interf;
  while ((interf = iter.next()) != NULL) {
    // Check whether we've asked to exclude this interface:
    if (interf == exceptInterface)
      continue;
    
    // Check that the packet's source address makes it OK to
    // be relayed across this interface:
    UsageEnvironment& saveEnv = env();
    // because the following call may delete "this"
    if (!interf->SourceAddrOKForRelaying(saveEnv, sourceAddr)) {
      if (strcmp(saveEnv.getResultMsg(), "") != 0) {
				// Treat this as a fatal error
	return -1;
      } else {
	continue;
      }
    }
    
    if (numMembers == 0) {
      // We know that we're going to forward to at least one
      // member, so fill in the tunnel encapsulation trailer.
      // (Note: Allow for it not being 4-byte-aligned.)
      TunnelEncapsulationTrailer* trailerInPacket
	= (TunnelEncapsulationTrailer*)&data[size];
      TunnelEncapsulationTrailer* trailer;
      
      Boolean misaligned = ((unsigned)trailerInPacket & 3) != 0;
      unsigned trailerOffset;
      unsigned char tunnelCmd;
      if (isSSM()) {
	// add an 'auxilliary address' before the trailer
	trailerOffset = TunnelEncapsulationTrailerAuxSize;
	tunnelCmd = TunnelDataAuxCmd;
      } else {
	trailerOffset = 0;
	tunnelCmd = TunnelDataCmd;
      }
      unsigned trailerSize = TunnelEncapsulationTrailerSize + trailerOffset;
      unsigned tmpTr[TunnelEncapsulationTrailerMaxSize];
      if (misaligned) {
	trailer = (TunnelEncapsulationTrailer*)&tmpTr;
      } else {
	trailer = trailerInPacket;
      }
      trailer += trailerOffset;
      
      trailer->address() = destAddress().s_addr;
      trailer->port() = destPort(); // structure copy, outputs in network order
      trailer->ttl() = ttlToFwd;
      trailer->command() = tunnelCmd;
      
      if (isSSM()) {
	trailer->auxAddress() = sourceFilterAddress().s_addr;
      }
      
      if (misaligned) {
	our_bcopy(trailer-trailerOffset, trailerInPacket, trailerSize);
      }
      
      size += trailerSize;
    }
    
    interf->write(data, size);
    ++numMembers;
  }
  
  return numMembers;
}

ostream& operator<<(ostream& s, const Groupsock& g) {
  ostream& s1 = s << timestampString() << " Groupsock("
		  << g.socketNum() << ": "
		  << our_inet_ntoa(g.groupAddress())
		  << ", " << g.port() << ", ";
  if (g.isSSM()) {
    return s1 << "SSM source: "
	      <<  our_inet_ntoa(g.sourceFilterAddress()) << ")";
  } else {
    return s1 << (unsigned)(g.ttl()) << ")";
  }
}



// A hash table used to index Groupsocks by socket number.
// This is shared within a process.

static HashTable* getSocketTable() {
  static HashTable* socketTable = NULL;
  
  if (socketTable == NULL) { // we need to create it
    socketTable = HashTable::create(ONE_WORD_HASH_KEYS);
  }
  
  return socketTable;
}

static Boolean unsetGroupsockBySocket(Groupsock const* groupsock) {
  do {
    if (groupsock == NULL) break;
    
    int sock = groupsock->socketNum();
    // Make sure "sock" is in bounds:
    if (sock < 0) break;
    
    HashTable* sockets = getSocketTable();
    if (sockets == NULL) break;
    
    Groupsock* gs = (Groupsock*)sockets->Lookup((char*)sock);
    if (gs == NULL || gs != groupsock) break;
    sockets->Remove((char*)sock);
    
    return True;
  } while (0);
  
  return False;
}

static Boolean setGroupsockBySocket(UsageEnvironment& env, int sock,
				    Groupsock* groupsock) {
  do {
    // Make sure the "sock" parameter is in bounds:
    if (sock < 0) {
      char buf[100];
      sprintf(buf, "trying to use bad socket (%d)", sock);
      env.setResultMsg(buf);
      break;
    }
    
    HashTable* sockets = getSocketTable();
    if (sockets == NULL) break;
    
    // Make sure we're not replacing an existing Groupsock
    // That shouldn't happen
    Boolean alreadyExists
      = (sockets->Lookup((char*)sock) != 0);
    if (alreadyExists) {
      char buf[100];
      sprintf(buf,
	      "Attempting to replace an existing socket (%d",
	      sock);
      env.setResultMsg(buf);
      break;
    }
    
    sockets->Add((char*)sock, groupsock);
    return True;
  } while (0);
  
  return False;
}

static Groupsock* getGroupsockBySocket(int sock) {
  do {
    // Make sure the "sock" parameter is in bounds:
    if (sock < 0) break;
    
    HashTable* sockets = getSocketTable();
    if (sockets == NULL) break;
    
    return (Groupsock*)sockets->Lookup((char*)sock);
  } while (0);
  
  return NULL;
}

////////// GroupsockLookupTable //////////

Groupsock*
GroupsockLookupTable::Fetch(UsageEnvironment& env, unsigned groupAddress,
			    Port port, unsigned char ttl,
			    Boolean& isNew) {
  isNew = False;
  Groupsock* groupsock;
  do {
    groupsock = (Groupsock*) fTable.Lookup(groupAddress, (~0), port);
    if (groupsock == NULL) { // we need to create one:
      groupsock = AddNew(env, groupAddress, (~0), port, ttl);
      if (groupsock == NULL) break;
      isNew = True;
    }
  } while (0);
  
  return groupsock;
}

Groupsock*
GroupsockLookupTable::Fetch(UsageEnvironment& env, unsigned groupAddress,
			    unsigned sourceFilterAddr, Port port,
			    Boolean& isNew) {
  isNew = False;
  Groupsock* groupsock;
  do {
    groupsock
      = (Groupsock*) fTable.Lookup(groupAddress, sourceFilterAddr, port);
    if (groupsock == NULL) { // we need to create one:
      groupsock = AddNew(env, groupAddress, sourceFilterAddr, port, 0);
      if (groupsock == NULL) break;
      isNew = True;
    }
  } while (0);
  
  return groupsock;
}

Groupsock* GroupsockLookupTable::Lookup(unsigned groupAddress, Port port) {
  return (Groupsock*) fTable.Lookup(groupAddress, (~0), port);
}

Groupsock* GroupsockLookupTable::Lookup(unsigned groupAddress,
					unsigned sourceFilterAddr, Port port) {
  return (Groupsock*) fTable.Lookup(groupAddress, sourceFilterAddr, port);
}

Groupsock* GroupsockLookupTable::Lookup(int sock) {
  return getGroupsockBySocket(sock);
}

Boolean GroupsockLookupTable::Remove(Groupsock const* groupsock) {
  unsetGroupsockBySocket(groupsock);
  return fTable.Remove(groupsock->groupAddress().s_addr,
		       groupsock->sourceFilterAddress().s_addr,
		       groupsock->port());
}

Groupsock* GroupsockLookupTable::AddNew(UsageEnvironment& env,
					unsigned groupAddress,
					unsigned sourceFilterAddress,
					Port port, unsigned char ttl) {
  Groupsock* groupsock;
  do {
    struct in_addr groupAddr; groupAddr.s_addr = groupAddress;
    if (sourceFilterAddress == unsigned(~0)) {
      // regular, ISM groupsock
      groupsock = new Groupsock(env, groupAddr, port, ttl);
    } else {
      // SSM groupsock
      struct in_addr sourceFilterAddr;
      sourceFilterAddr.s_addr = sourceFilterAddress;
      groupsock = new Groupsock(env, groupAddr, sourceFilterAddr, port);
    }
    
    if (groupsock == NULL || groupsock->socketNum() < 0) break;
    
    if (!setGroupsockBySocket(env, groupsock->socketNum(), groupsock)) break;
    
    fTable.Add(groupAddress, sourceFilterAddress, port, (void*)groupsock);
  } while (0);
  
  return groupsock;
}

GroupsockLookupTable::Iterator::Iterator(GroupsockLookupTable& groupsocks)
  : fIter(AddressPortLookupTable::Iterator(groupsocks.fTable)) {
}

Groupsock* GroupsockLookupTable::Iterator::next() {
  return (Groupsock*) fIter.next();
};
