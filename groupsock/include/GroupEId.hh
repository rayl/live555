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
// "multikit" Multicast Application Shell
// Copyright (c) 1996-1997, Live Networks, Inc.  All rights reserved
// "Group Endpoint Id"
// C++ header

#ifndef _GROUPEID_HH
#define _GROUPEID_HH

#ifndef _BOOLEAN_HH
#include "Boolean.hh"
#endif

#ifndef _NET_COMMON_H
#include "NetCommon.h"
#endif

const unsigned char MAX_TTL = 255;

class Scope {
    public:
    	Scope(unsigned char ttl = 0, const char* publicKey = NULL);
    	Scope(const Scope& orig);
    	Scope& operator=(const Scope& rightSide);
	~Scope();

	unsigned char ttl() const
		{ return fTTL; }

	const char* publicKey() const
		{ return fPublicKey; }
	unsigned publicKeySize() const;

    private:
    	void assign(unsigned char ttl, const char* publicKey);
    	void clean();

    	unsigned char fTTL;
    	char* fPublicKey;
};

class GroupEId {
public:
  GroupEId(struct in_addr const& groupAddr,
	   unsigned short portNum, Scope const& scope,
	   unsigned numSuccessiveGroupAddrs = 1);
      // used for a 'source-independent multicast' group
  GroupEId(struct in_addr const& groupAddr,
	   struct in_addr const& sourceFilterAddr,
	   unsigned short portNum,
	   unsigned numSuccessiveGroupAddrs = 1);
      // used for a 'source-specific multicast' group
  GroupEId(); // used only as a temp constructor prior to initialization 

  struct in_addr const& groupAddress() const { return fGroupAddress; }
  struct in_addr const& sourceFilterAddress() const { return fSourceFilterAddress; }

  Boolean isSSM() const;

  unsigned numSuccessiveGroupAddrs() const {
    // could be >1 for hier encoding
    return fNumSuccessiveGroupAddrs;
  }
	
  unsigned short portNum() const { return fPortNum; }

  const Scope& scope() const { return fScope; }

private:
  void init(struct in_addr const& groupAddr,
	    struct in_addr const& sourceFilterAddr,
	    unsigned short portNum,
	    Scope const& scope,
	    unsigned numSuccessiveGroupAddrs);

private:
  struct in_addr fGroupAddress;
  struct in_addr fSourceFilterAddress;
  unsigned fNumSuccessiveGroupAddrs;
  unsigned short fPortNum;
  Scope fScope;
};

#endif
