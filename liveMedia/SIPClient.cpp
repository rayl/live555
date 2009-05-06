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
// A generic SIP client
// Implementation

#include "SIPClient.hh"
#include "our_md5.h"
#include "GroupsockHelper.hh"

#if defined(__WIN32__) || defined(_WIN32)
#include <windows.h>
#define _strncasecmp strncmp
#define snprintf _snprintf
#if defined(_WINNT)
#include <ws2tcpip.h>
#endif
#else
#include <unistd.h>
#if defined(_QNX4)
#define _strncasecmp strncmp
#else
#define _strncasecmp strncasecmp
#endif
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#endif

////////// SIPClient //////////

SIPClient* SIPClient::createNew(UsageEnvironment& env,
				int verbosityLevel,
				char const* applicationName) {
  return new SIPClient(env, verbosityLevel, applicationName);
}

SIPClient::SIPClient(UsageEnvironment& env,
		     int verbosityLevel, char const* applicationName)
  : Medium(env),
    fVerbosityLevel(verbosityLevel), fCSeq(0), fOurPortNum(5060),
    fURL(NULL), fURLSize(0), fToTagStr(NULL), fToTagStrSize(0),
    fCurrentAuthenticator(NULL) {
  if (applicationName == NULL) applicationName = "";
  fApplicationName = strdup(applicationName);
  fApplicationNameSize = strlen(fApplicationName);

  fOurAddress.s_addr = ourSourceAddressForMulticast(env); // hack
  fOurAddressStr = strdup(our_inet_ntoa(fOurAddress));
  fOurAddressStrSize = strlen(fOurAddressStr);

  fOurSocket = new Groupsock(env, fOurAddress, fOurPortNum, 255);
  if (fOurSocket == NULL) {
    fprintf(stderr,
	    "ERROR: Failed to create socket for addr %s, port %d: %s\n",
	    our_inet_ntoa(fOurAddress), fOurPortNum, env.getResultMsg());
  }

  // Set various headers to be used in each request:
  char const* formatStr;
  unsigned headerSize;

  // Set the "User-Agent:" header:
  char const* const libName = "LIVE.COM Streaming Media v";
  char const* const libVersionStr = LIVEMEDIA_LIBRARY_VERSION_STRING;
  char const* libPrefix; char const* libSuffix;
  if (applicationName == NULL || applicationName[0] == '\0') {
    applicationName = libPrefix = libSuffix = "";
  } else {
    libPrefix = " (";
    libSuffix = ")";
  }
  formatStr = "User-Agent: %s%s%s%s%s\r\n";
  headerSize
    = strlen(formatStr) + fApplicationNameSize + strlen(libPrefix)
    + strlen(libName) + strlen(libVersionStr) + strlen(libSuffix);
  fUserAgentHeaderStr = new char[headerSize];
  sprintf(fUserAgentHeaderStr, formatStr,
	  applicationName, libPrefix, libName, libVersionStr, libSuffix);
  fUserAgentHeaderStrSize = strlen(fUserAgentHeaderStr);

  reset();
}

SIPClient::~SIPClient() {
  reset();

  delete fUserAgentHeaderStr;
  delete fOurSocket;
  delete (char*)fOurAddressStr;
  delete (char*)fApplicationName;
}

void SIPClient::reset() {
  delete (char*)fUserName; fUserName = strdup(fApplicationName);
  fUserNameSize = strlen(fUserName);

  resetCurrentAuthenticator();

  delete (char*)fToTagStr; fToTagStr = NULL; fToTagStrSize = 0;
  fServerPortNum = 0;
  fServerAddress.s_addr = 0;
  delete (char*)fURL; fURL = NULL; fURLSize = 0;
}

void SIPClient::setProxyServer(unsigned proxyServerAddress,
			       unsigned short proxyServerPortNum) {
  fServerAddress.s_addr = proxyServerAddress;
  fServerPortNum = proxyServerPortNum;
  if (fOurSocket != NULL) {
    fOurSocket->changeDestinationParameters(fServerAddress,
					    fServerPortNum, 255);
  }
}

static char* getLine(char* startOfLine) {
  // returns the start of the next line, or NULL if none
  for (char* ptr = startOfLine; *ptr != '\0'; ++ptr) {
    if (*ptr == '\r' || *ptr == '\n') {
      // We found the end of the line
      *ptr++ = '\0';
      if (*ptr == '\n') ++ptr;
      return ptr;
    }
  }

  return NULL;
}

char* SIPClient::invite(char const* url, AuthRecord* authenticator) {
  if (!processURL(url)) return NULL;

  delete (char*)fURL; fURL = strdup(url);
  fURLSize = strlen(fURL);

  fCallId = our_random();
  fFromTag = our_random();

  return invite1(authenticator);
}

char* SIPClient::invite1(AuthRecord* authenticator) {
  do {
    // Send the INVITE command:

    // First, construct an authenticator string:
    resetCurrentAuthenticator();
    char* authenticatorStr
      = createAuthenticatorString(authenticator, "INVITE", fURL);

    // Then, construct the SDP description to be sent in the INVITE:
    char* const inviteSDPFmt = 
      "v=0\r\n"
      "o=- %u %u IN IP4 %s\r\n"
      "s=%s session\r\n"
      "c=IN IP4 %s\r\n"
      "t=0 0\r\n"
      "m=audio %u RTP/AVP 0\r\n";// PCMU HARDWIRED - TRY GSM ALSO #####
    unsigned inviteSDPFmtSize = strlen(inviteSDPFmt)
      + 20 /* max int len */ + 20 * fOurAddressStrSize
      + fApplicationNameSize
      + fOurAddressStrSize
      + 5 /* max short len */;
    char* inviteSDPDescription = new char[inviteSDPFmtSize];
    sprintf(inviteSDPDescription, inviteSDPFmt,
	    fCallId, fCSeq, fOurAddressStr,
	    fApplicationName,
	    fOurAddressStr,
	    fClientStartPortNum);
    unsigned inviteSDPSize = strlen(inviteSDPDescription);

    char* const cmdFmt =
      "INVITE %s SIP/2.0\r\n"
      "From: %s <sip:%s@%s>;tag=%u\r\n"
      "Via: SIP/2.0/UDP %s:%u\r\n"
      "To: %s\r\n"
      "Contact: sip:%s@%s:%u\r\n"
      "Call-ID: %u@%s\r\n"
      "CSeq: %d INVITE\r\n"
      "Content-Type: application/sdp\r\n"
      "%s" /* Proxy-Authorization: line (if any) */
      "%s" /* User-Agent: line */
      "Content-length: %d\r\n\r\n"
      "%s";
    unsigned writeBufSize = strlen(cmdFmt)
      + fURLSize
      + 2*fUserNameSize + fOurAddressStrSize + 20 /* max int len */
      + fOurAddressStrSize + 5 /* max port len */
      + fURLSize
      + fUserNameSize + fOurAddressStrSize + 5
      + 20 + fOurAddressStrSize
      + 20
      + strlen(authenticatorStr)
      + fUserAgentHeaderStrSize
      + 20
      + inviteSDPSize;
    char* writeBuf = new char[writeBufSize];
    sprintf(writeBuf, cmdFmt,
	    fURL,
	    fUserName, fUserName, fOurAddressStr, fFromTag,
	    fOurAddressStr, fOurPortNum,
	    fURL,
	    fUserName, fOurAddressStr, fOurPortNum,
	    fCallId, fOurAddressStr,
	    ++fCSeq,
	    authenticatorStr,
	    fUserAgentHeaderStr,
	    inviteSDPSize,
	    inviteSDPDescription);
    // NOTE: We return this SDP description, not the one from the server:
    char* resultSDPDescription = strdup(inviteSDPDescription);
    // ##### Later: match the codecs in the response (offer, answer) #####
    delete inviteSDPDescription;
    delete authenticatorStr;

    if (!sendRequest(writeBuf)) {
      envir().setResultErrMsg("INVITE send() failed: ");
      break;
    }

    // Get the response from the server:
    unsigned const readBufSize = 10000;
    char readBuf[readBufSize+1];
    //#####SHOULD CHECK FOR UDP RECEIVE TIMEOUT
    char* firstLine = NULL;
    char* nextLineStart = NULL;
    unsigned responseCode = 0;
    int bytesRead;
    Boolean responseSuccess = False;
    do {
      bytesRead = getResponse(readBuf, readBufSize);
      if (bytesRead < 0) break;
      if (fVerbosityLevel >= 1) {
	fprintf(stderr, "Received INVITE response: %s\n", readBuf);
	fflush(stderr);
      }

      // Inspect the first line to check whether it's a result code 200
      firstLine = readBuf;
      nextLineStart = getLine(firstLine);
      if (!parseResponseCode(firstLine, responseCode)) break;
      responseSuccess = True;
    }  while (responseCode >= 100 && responseCode < 200);
    if (!responseSuccess) break;

    if (responseCode != 200) {
      if ((responseCode == 401 || responseCode == 407)
	  && authenticator != NULL) {
	// We have an authentication failure, so fill in "authenticator"
	// using the contents of a following "Proxy-Authenticate:" line.
	// (Once we compute a 'response' for "authenticator", it can be
	//  used in a subsequent request - that will hopefully succeed.)
	char* lineStart;
	while (1) {
	  lineStart = nextLineStart;
	  if (lineStart == NULL) break;

	  nextLineStart = getLine(lineStart);
	  if (lineStart[0] == '\0') break; // this is a blank line

	  char* realm = strdup(lineStart); char* nonce = strdup(lineStart);
	  // ##### Check for the format of "Proxy-Authenticate:" lines from
	  // ##### known server types.
	  // ##### This is a crock! We need to make the parsing more general
	  if (
	      // Asterisk #####
	      sscanf(lineStart, "Proxy-Authenticate: DIGEST realm=\"%[^\"]\", nonce=\"%[^\"]\"",
		     realm, nonce) == 2 ||
	      // Cisco ATA #####
	      sscanf(lineStart, "Proxy-Authenticate: Digest algorithm=MD5,domain=\"%*[^\"]\",nonce=\"%[^\"]\", realm=\"%[^\"]\"",
		     nonce, realm) == 2) {
	    authenticator->realm = realm;
	    authenticator->nonce = nonce;
	    break;
	  } else {
	    delete realm; delete nonce;
	  }
	} 
      }
      envir().setResultMsg("cannot handle INVITE response: ", firstLine);
      break;
    }

    // Skip every subsequent header line, until we see a blank line.
    // While doing so, check for "To:" and "Content-Length:" lines.
    // The remaining data is assumed to be the SDP descriptor that we want.
    // We should really do some more checking on the headers here - e.g., to
    // check for "Content-type: application/sdp", "CSeq", etc. #####
    char* toTagStr = strdup(readBuf);
    int contentLength = -1;
    char* lineStart;
    while (1) {
      lineStart = nextLineStart;
      if (lineStart == NULL) break;

      nextLineStart = getLine(lineStart);
      if (lineStart[0] == '\0') break; // this is a blank line

      if (sscanf(lineStart, "To:%*[^;]; tag=%s", toTagStr) == 1) {
	delete (char*)fToTagStr; fToTagStr = strdup(toTagStr);
	fToTagStrSize = strlen(fToTagStr);
      }
 
      if (sscanf(lineStart, "Content-Length: %d", &contentLength) == 1
          || sscanf(lineStart, "Content-length: %d", &contentLength) == 1) {
        if (contentLength < 0) {
          envir().setResultMsg("Bad \"Content-length:\" header: \"",
                               lineStart, "\"");
          break;
        }
      }
    }
    delete toTagStr;

    // We're now at the end of the response header lines
    if (lineStart == NULL) {
      envir().setResultMsg("no content following header lines: ", readBuf);
      break;
    }

    // Use the remaining data as the SDP descr, but first, check
    // the "Content-length:" header (if any) that we saw.  We may need to
    // read more data, or we may have extraneous data in the buffer.
    char* bodyStart = nextLineStart;
    if (contentLength >= 0) {
      // We saw a "Content-length:" header
      unsigned numBodyBytes = &readBuf[bytesRead] - bodyStart;
      if (contentLength > (int)numBodyBytes) {
        // We need to read more data.  First, make sure we have enough
        // space for it:
        unsigned numExtraBytesNeeded = contentLength - numBodyBytes;
        unsigned remainingBufferSize = readBufSize - bytesRead;
        if (numExtraBytesNeeded > remainingBufferSize) {
          char tmpBuf[200];
          sprintf(tmpBuf, "Read buffer size (%d) is too small for \"Content-length:\" %d (need a buffer size of >= %d bytes\n",
                  readBufSize, contentLength,
                  readBufSize + numExtraBytesNeeded - remainingBufferSize);
          envir().setResultMsg(tmpBuf);
          break;
        }

        // Keep reading more data until we have enough:
        if (fVerbosityLevel >= 1) {
          fprintf(stderr, "Need to read %d extra bytes\n",
                  numExtraBytesNeeded); fflush(stderr);
        }
        while (numExtraBytesNeeded > 0) {
          char* ptr = &readBuf[bytesRead];
	  unsigned bytesRead2;
	  struct sockaddr_in fromAddr;
	  Boolean readSuccess
	    = fOurSocket->handleRead((unsigned char*)ptr,
				     numExtraBytesNeeded,
				     bytesRead2, fromAddr);
          if (!readSuccess) break;
          ptr[bytesRead2] = '\0';
          if (fVerbosityLevel >= 1) {
            fprintf(stderr, "Read %d extra bytes: %s\n", bytesRead2, ptr); fflush(stderr);
          }

          bytesRead += bytesRead2;
          numExtraBytesNeeded -= bytesRead2;
        }
        if (numExtraBytesNeeded > 0) break; // one of the reads failed
      }

      bodyStart[contentLength] = '\0'; // trims any extra data
    }

    //#####return strdup(bodyStart);
    return resultSDPDescription;
  } while (0);

  return NULL;
}

static char* computeDigestResponse(AuthRecord const& authenticator,
				   char const* cmd, char const* url) {
  // The "response" field is computed as:
  //    md5(md5(<username>:<realm>:<password>):<nonce>:md5(<cmd>:<url>))
  unsigned const ha1DataLen = strlen(authenticator.username) + 1
    + strlen(authenticator.realm) + 1 + strlen(authenticator.password);
  unsigned char* ha1Data = new unsigned char[ha1DataLen+1];
  sprintf((char*)ha1Data, "%s:%s:%s",
	  authenticator.username, authenticator.realm,
	  authenticator.password);
  char ha1Buf[33];
  our_MD5Data(ha1Data, ha1DataLen, ha1Buf);

  unsigned const ha2DataLen = strlen(cmd) + 1 + strlen(url);
  unsigned char* ha2Data = new unsigned char[ha2DataLen+1];
  sprintf((char*)ha2Data, "%s:%s", cmd, url);
  char ha2Buf[33];
  our_MD5Data(ha2Data, ha2DataLen, ha2Buf);

  unsigned const digestDataLen
    = 32 + 1 + strlen(authenticator.nonce) + 1 + 32;
  unsigned char* digestData = new unsigned char[digestDataLen+1];
  sprintf((char*)digestData, "%s:%s:%s",
	  ha1Buf, authenticator.nonce, ha2Buf);
  return our_MD5Data(digestData, digestDataLen, NULL);
}

char* SIPClient::inviteWithPassword(char const* url, char const* username,
				    char const* password) {
  delete (char*)fUserName; fUserName = strdup(username);
  fUserNameSize = strlen(fUserName);

  AuthRecord authenticator;
  authenticator.realm = authenticator.nonce = NULL;
  authenticator.username = username; authenticator.password = password;
  char* inviteResult = invite(url, &authenticator);
  if (inviteResult != NULL) {
    // We are already authorized
    return inviteResult;
  }

  // The "realm" and "nonce" fields should have been filled in:
  if (authenticator.realm == NULL || authenticator.nonce == NULL) {
    // We haven't been given enough information to try again, so fail:
    return NULL;
  }

  // Try again (but with the same CallId):
  inviteResult = invite1(&authenticator);
  if (inviteResult != NULL) {
    // The authenticator worked, so use it in future requests:
    useAuthenticator(&authenticator);
  }

  // The "realm" and "nonce" fields were dynamically
  // allocated; free them now:
  delete (char*)authenticator.realm;
  delete (char*)authenticator.nonce;

  return inviteResult;
}

Boolean SIPClient::sendACK() {
  char* const cmdFmt =
    "ACK %s SIP/2.0\r\n"
    "From: %s <sip:%s@%s>;tag=%u\r\n"
    "Via: SIP/2.0/UDP %s:%u\r\n"
    "To: %s;tag=%s\r\n"
    "Call-ID: %u@%s\r\n"
    "CSeq: %d ACK\r\n"
    "Content-length: 0\r\n\r\n";
    unsigned writeBufSize = strlen(cmdFmt)
      + fURLSize
      + 2*fUserNameSize + fOurAddressStrSize + 20 /* max int len */
      + fOurAddressStrSize + 5 /* max port len */
      + fURLSize + fToTagStrSize
      + 20 + fOurAddressStrSize
      + 20;
    char* writeBuf = new char[writeBufSize];
    sprintf(writeBuf, cmdFmt,
	    fURL,
	    fUserName, fUserName, fOurAddressStr, fFromTag,
	    fOurAddressStr, fOurPortNum,
	    fURL, fToTagStr,
	    fCallId, fOurAddressStr,
	    fCSeq /* note: it's the same as before; not incremented */);

    if (!sendRequest(writeBuf)) {
      envir().setResultErrMsg("ACK send() failed: ");
      return False;
    }

  return True;
}

Boolean SIPClient::sendBYE() {
  char* const cmdFmt =
    "BYE %s SIP/2.0\r\n"
    "From: %s <sip:%s@%s>;tag=%u\r\n"
    "Via: SIP/2.0/UDP %s:%u\r\n"
    "To: %s;tag=%s\r\n"
    "Call-ID: %u@%s\r\n"
    "CSeq: %d ACK\r\n"
    "Content-length: 0\r\n\r\n";
    unsigned writeBufSize = strlen(cmdFmt)
      + fURLSize
      + 2*fUserNameSize + fOurAddressStrSize + 20 /* max int len */
      + fOurAddressStrSize + 5 /* max port len */
      + fURLSize + fToTagStrSize
      + 20 + fOurAddressStrSize
      + 20;
    char* writeBuf = new char[writeBufSize];
    sprintf(writeBuf, cmdFmt,
	    fURL,
	    fUserName, fUserName, fOurAddressStr, fFromTag,
	    fOurAddressStr, fOurPortNum,
	    fURL, fToTagStr,
	    fCallId, fOurAddressStr,
	    ++fCSeq);

    if (!sendRequest(writeBuf)) {
      envir().setResultErrMsg("BYE send() failed: ");
      return False;
    }

  return True;
}

Boolean SIPClient::processURL(char const* url) {
  do {
    // If we don't already have a server address/port, then
    // get these by parsing the URL:
    if (fServerAddress.s_addr == 0) {
      NetAddress destAddress;
      if (!parseURL(url, destAddress, fServerPortNum)) break;
      fServerAddress.s_addr = *(unsigned*)(destAddress.data());
    
      if (fOurSocket != NULL) {
	fOurSocket->changeDestinationParameters(fServerAddress,
						fServerPortNum, 255);
      }
    }

    return True; 
  } while (0);

  return False;
}

Boolean SIPClient::parseURL(char const* url,
			    NetAddress& address,
			    unsigned short& portNum) {
  do {
    // Parse the URL as "sip:<username>@<address>:<port>/<etc>"
    // (with ":<port>" and "/<etc>" optional)
    char const* prefix = "sip:";
    unsigned const prefixLength = 4;
    if (_strncasecmp(url, prefix, prefixLength) != 0) {
      envir().setResultMsg("URL is not of the form \"", prefix, "\"");
      break;
    }

    unsigned const parseBufferSize = 100;
    char parseBuffer[parseBufferSize];
    unsigned addressStartIndex = prefixLength;
    while (url[addressStartIndex] != '\0'
	   && url[addressStartIndex++] != '@') {}
    char const* from = &url[addressStartIndex];
    char* to = &parseBuffer[0];
    unsigned i;
    for (i = 0; i < parseBufferSize; ++i) {
      if (*from == '\0' || *from == ':' || *from == '/') {
	// We've completed parsing the address
	*to = '\0';
	break;
      }
      *to++ = *from++;
    }
    if (i == parseBufferSize) {
      envir().setResultMsg("URL is too long");
      break;
    }

    NetAddressList addresses(parseBuffer);
    if (addresses.numAddresses() == 0) {
      envir().setResultMsg("Failed to find network address for \"",
			   parseBuffer, "\"");
      break;
    }
    address = *(addresses.firstAddress());

    portNum = 5060; // default value
    char nextChar = *from;
    if (nextChar == ':') {
      int portNumInt;
      if (sscanf(++from, "%d", &portNumInt) != 1) {
	envir().setResultMsg("No port number follows ':'");
	break;
      }
      if (portNumInt < 1 || portNumInt > 65535) {
	envir().setResultMsg("Bad port number");
	break;
      }
      portNum = (unsigned short)portNumInt;
    }

    return True;
  } while (0);

  return False;
}

char*
SIPClient::createAuthenticatorString(AuthRecord const* authenticator,
				      char const* cmd, char const* url) {
  if (authenticator != NULL && authenticator->realm != NULL
      && authenticator->nonce != NULL && authenticator->username != NULL
      && authenticator->password != NULL) {
    // We've been provided a filled-in authenticator, so use it:
    char* const authFmt = "Proxy-Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", response=\"%s\", uri=\"%s\"\r\n";
    char const* response = computeDigestResponse(*authenticator, cmd, url);
    unsigned authBufSize = strlen(authFmt)
      + strlen(authenticator->username) + strlen(authenticator->realm)
      + strlen(authenticator->nonce) + strlen(url) + strlen(response);
    char* authenticatorStr = new char[authBufSize];
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4)
    // snprintf() isn't defined, so just use sprintf()
    // This is a security risk if the component strings
    // can come from an external user
    sprintf(authenticatorStr, authFmt,
	    authenticator->username, authenticator->realm,
	    authenticator->nonce, response, url);
#else
    snprintf(authenticatorStr, authBufSize, authFmt,
	     authenticator->username, authenticator->realm,
	     authenticator->nonce, response, url);
#endif
    delete (char*)response;

    return authenticatorStr;
  }

  return strdup("");
}

void SIPClient::useAuthenticator(AuthRecord const* authenticator) {
  resetCurrentAuthenticator();
  if (authenticator != NULL && authenticator->realm != NULL
      && authenticator->nonce != NULL && authenticator->username != NULL
      && authenticator->password != NULL) {
    fCurrentAuthenticator = new AuthRecord;
    fCurrentAuthenticator->realm = strdup(authenticator->realm);
    fCurrentAuthenticator->nonce = strdup(authenticator->nonce);
    fCurrentAuthenticator->username = strdup(authenticator->username);
    fCurrentAuthenticator->password = strdup(authenticator->password);
  }
}

void SIPClient::resetCurrentAuthenticator() {
  if (fCurrentAuthenticator == NULL) return;

  delete (char*)fCurrentAuthenticator->realm;
  delete (char*)fCurrentAuthenticator->nonce;
  delete (char*)fCurrentAuthenticator->username;
  delete (char*)fCurrentAuthenticator->password;

  delete fCurrentAuthenticator; fCurrentAuthenticator = NULL;
}

Boolean SIPClient::sendRequest(char const* requestString) {
  if (fVerbosityLevel >= 1) {
    fprintf(stderr, "Sending request: %s\n", requestString);
    fflush(stderr);
  }
  return fOurSocket->output(envir(), 255, (unsigned char*)requestString,
			    strlen(requestString));
}

int SIPClient::getResponse(char* responseBuffer,
			    unsigned responseBufferSize) {
  if (responseBufferSize == 0) return 0; // just in case...
  responseBuffer[0] = '\0'; // ditto

  // Read data from the socket until we see "\r\n\r\n"
  // (or until we fill up our buffer).  Don't read any more than this.
  int bytesRead = 0;
  while (1) {
    unsigned bytesReadNow;
    struct sockaddr_in fromAddr;
    unsigned char* toPosn = (unsigned char*)(responseBuffer+bytesRead);
    Boolean readSuccess
      = fOurSocket->handleRead(toPosn, responseBufferSize-bytesRead,
			       bytesReadNow, fromAddr);
    bytesRead += bytesReadNow;
    if (!readSuccess || bytesReadNow == 0) {
      envir().setResultMsg("SIP response was truncated");
      break;
    }
    if (bytesRead >= (int)responseBufferSize) return responseBufferSize;
    
    // Check whether we have "\r\n\r\n":
    char* lastToCheck = responseBuffer+bytesRead-4;
    if (lastToCheck < responseBuffer) continue;
    for (char* p = responseBuffer; p <= lastToCheck; ++p) {
      if (*p == '\r' && *(p+1) == '\n' &&
	  *(p+2) == '\r' && *(p+3) == '\n') {
	responseBuffer[bytesRead] = '\0';
	return bytesRead;
      }
    }
  }
  
  return 0;
}

Boolean SIPClient::parseResponseCode(char const* line, 
				      unsigned& responseCode) {
  if (sscanf(line, "%*s%u", &responseCode) != 1) {
    envir().setResultMsg("no response code in line: \"", line, "\"");
    return False;
  }

  return True;
}
