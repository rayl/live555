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
// Network Addresses
// Implementation

#include "NetAddress.hh"
#include "GroupsockHelper.hh"

#include <stddef.h>
#if defined(__WIN32__) || defined(_WIN32)
#else
#ifndef INADDR_NONE
#define INADDR_NONE 0xFFFFFFFF
#endif
#endif

////////// NetAddress //////////

NetAddress::NetAddress(unsigned char const* data, unsigned length) {
	assign(data, length);
}

NetAddress::NetAddress(unsigned length) {
	fData = new unsigned char[length];
	if (fData == NULL) return;

	for (unsigned i = 0; i < length; ++i)
		fData[i] = 0;
	fLength = length;
}

NetAddress::NetAddress(NetAddress const& orig) {
	assign(orig.data(), orig.length());
}

NetAddress& NetAddress::operator=(NetAddress const& rightSide) {
	if (&rightSide != this) {
		clean();
		assign(rightSide.data(), rightSide.length());
	}
	return *this;
}

NetAddress::~NetAddress() {
	clean();
}

void NetAddress::assign(unsigned char const* data, unsigned length) {
	fData = new unsigned char[length];
	if (fData == NULL) return;

	for (unsigned i = 0; i < length; ++i)
		fData[i] = data[i];
	fLength = length;
}

void NetAddress::clean() {
	delete fData;
	fLength = 0;
}


////////// NetAddressList //////////

NetAddressList::NetAddressList(char const* hostname)
  : fNumAddresses(0), fAddressArray(NULL) {
    struct hostent* host;

    // Check first whether "hostname" is an IP address string:
    unsigned addr = our_inet_addr((char*)hostname);
    if (addr != INADDR_NONE) { // yes it was an IP address string
      //##### host = gethostbyaddr((char*)&addr, 4, AF_INET);
      host = NULL; // don't bother calling gethostbyaddr(); we only want 1 addr
      
      if (host == NULL) {
	// For some unknown reason, gethostbyaddr() failed, so just
	// return a 1-element list with the address we were given:
	fNumAddresses = 1;
	fAddressArray = new NetAddress*[fNumAddresses];
	if (fAddressArray == NULL) return;
	
	fAddressArray[0] = new NetAddress((unsigned char*)&addr, 4);
	    // would need to be fixed for IPv6
	return;
      }
    } else { // Try resolving "hostname" as a real host name
      host = our_gethostbyname((char*)hostname);
      
      if (host == NULL) {
	// It was a host name, and we couldn't resolve it.  We're SOL.
	return;
      }
    }

    unsigned char const** const hAddrPtr
      = (unsigned char const**)host->h_addr_list;
    if (hAddrPtr != NULL) {
      // First, count the number of addresses:
      unsigned char const** hAddrPtr1 = hAddrPtr;
      while (*hAddrPtr1 != NULL) {
	++fNumAddresses;
	++hAddrPtr1;
      }
      
      // Next, set up the list:
      fAddressArray = new NetAddress*[fNumAddresses];
      if (fAddressArray == NULL) return;
      
      for (unsigned i = 0; i < fNumAddresses; ++i) {
	fAddressArray[i]
	  = new NetAddress(hAddrPtr[i], host->h_length);
      }
    }
}

NetAddressList::NetAddressList(NetAddressList const& orig) {
	assign(orig.numAddresses(), orig.fAddressArray);
}

NetAddressList& NetAddressList::operator=(NetAddressList const& rightSide) {
	if (&rightSide != this) {
		clean();
		assign(rightSide.numAddresses(), rightSide.fAddressArray);
	}
	return *this;
}

NetAddressList::~NetAddressList() {
	clean();
}

void NetAddressList::assign(unsigned numAddresses, NetAddress** addressArray) {
	fAddressArray = new NetAddress*[numAddresses];
	if (fAddressArray == NULL) return;

	for (unsigned i = 0; i < numAddresses; ++i)
		fAddressArray[i] = new NetAddress(*addressArray[i]);
	fNumAddresses = numAddresses;
}

void NetAddressList::clean() {
	while (fNumAddresses-- > 0)
		delete fAddressArray[fNumAddresses];
	delete fAddressArray;
}

NetAddress const* NetAddressList::firstAddress() const {
	if (fNumAddresses == 0) return NULL;

	return fAddressArray[0];
}

////////// NetAddressList::Iterator //////////
NetAddressList::Iterator::Iterator(NetAddressList const& addressList)
	: fAddressList(addressList), fNextIndex(0) {}

NetAddress const* NetAddressList::Iterator::nextAddress() {
	if (fNextIndex >= fAddressList.numAddresses())
		return NULL; // no more
	return fAddressList.fAddressArray[fNextIndex++];
}


////////// Port //////////

Port::Port(portNumBits num /* in host byte order */) {
	fPortNum = htons(num);
}

ostream& operator<<(ostream& s, const Port& p) {
	return s << ntohs(p.num());
}


////////// AddressPortLookupTable //////////

AddressPortLookupTable::AddressPortLookupTable()
	: fTable(HashTable::create(3)) { // three-word keys are used
}

AddressPortLookupTable::~AddressPortLookupTable() {
	delete fTable;
}

void* AddressPortLookupTable::Add(unsigned address1, unsigned address2,
				  Port port, void* value) {
	int key[3];
	key[0] = (int)address1;
	key[1] = (int)address2;
	key[2] = (int)port.num();
	return fTable->Add((char*)key, value);
}

void* AddressPortLookupTable::Lookup(unsigned address1, unsigned address2,
				     Port port) {
	int key[3];
	key[0] = (int)address1;
	key[1] = (int)address2;
	key[2] = (int)port.num();
	return fTable->Lookup((char*)key);
}

Boolean AddressPortLookupTable::Remove(unsigned address1, unsigned address2,
				       Port port) {
	int key[3];
	key[0] = (int)address1;
	key[1] = (int)address2;
	key[2] = (int)port.num();
	return fTable->Remove((char*)key);
}

AddressPortLookupTable::Iterator::Iterator(AddressPortLookupTable& table)
	: fIter(HashTable::Iterator::create(*(table.fTable))) {
}

AddressPortLookupTable::Iterator::~Iterator() {
	delete fIter;
}

void* AddressPortLookupTable::Iterator::next() {
	char const* key; // dummy
	return fIter->next(key);
}

////////// Misc. //////////

Boolean IsMulticastAddress(unsigned address) {
	// Note: We return False for addresses in the range 224.0.0.0
	// through 224.0.0.255, because these are non-routable
	unsigned addressInHostOrder = ntohl(address);
	return addressInHostOrder >  0xE00000FF &&
	       addressInHostOrder <= 0xEFFFFFFF;
}
