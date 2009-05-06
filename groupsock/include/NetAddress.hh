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
// Network Addresses
// C++ header

#ifndef _NET_ADDRESS_HH
#define _NET_ADDRESS_HH

#ifndef _HASH_TABLE_HH
#include "HashTable.hh"
#endif

#ifndef _NET_COMMON_H
#include "NetCommon.h"
#endif

#include <iostream.h>

class NetAddress {
    public:
	NetAddress(unsigned char const* data,
		   unsigned length = 4 /* default: 32 bits */);
	NetAddress(unsigned length = 4); // sets address data to all-zeros
	NetAddress(NetAddress const& orig);
	NetAddress& operator=(NetAddress const& rightSide);
	virtual ~NetAddress();

	unsigned length() const
		{ return fLength; }
	const unsigned char* data() const // always in network byte order
		{ return fData; }

    private:
	void assign(unsigned char const* data, unsigned length);
	void clean();

	unsigned fLength;
	unsigned char* fData;
};

class NetAddressList {
    public:
	NetAddressList(char const* hostname);
	NetAddressList(NetAddressList const& orig);
	NetAddressList& operator=(NetAddressList const& rightSide);
	virtual ~NetAddressList();

	unsigned numAddresses() const
		{ return fNumAddresses; }

	NetAddress const* firstAddress() const;

	// Used to iterate through the addresses in a list:
	class Iterator {
	    public:
		Iterator(NetAddressList const& addressList);
		NetAddress const* nextAddress(); // NULL iff none
	    private:
		NetAddressList const& fAddressList;
		unsigned fNextIndex;
	};

    private:
	void assign(unsigned numAddresses, NetAddress** addressArray);
	void clean();

	friend class Iterator;
	unsigned fNumAddresses;
	NetAddress** fAddressArray;	
};

class Port {
    public:
	Port(unsigned short num /* in host byte order */);

	unsigned short num() const // in network byte order
		{ return fPortNum; }

    private:
	unsigned short fPortNum; // stored in network byte order
#ifdef IRIX
	unsigned short filler; // hack to overcome a bug in IRIX C++ compiler
#endif
};

ostream& operator<<(ostream& s, const Port& p);


// A generic table for looking up objects by (address1, address2, port)
class AddressPortLookupTable {
    public:
	AddressPortLookupTable();
	virtual ~AddressPortLookupTable();

	void* Add(unsigned address1, unsigned address2, Port port,
		  void* value);
		// Returns the old value if different, otherwise 0
	Boolean Remove(unsigned address1, unsigned address2, Port port);
	void* Lookup(unsigned address1, unsigned address2, Port port);
		// Returns 0 if not found

	// Used to iterate through the entries in the table
	class Iterator {
	    public:
		Iterator(AddressPortLookupTable& table);
		virtual ~Iterator();

		void* next(); // NULL iff none

	    private:
		HashTable::Iterator* fIter;
	};

    private:
	friend class Iterator;
	HashTable* fTable;
};


Boolean IsMulticastAddress(unsigned address);

#endif
