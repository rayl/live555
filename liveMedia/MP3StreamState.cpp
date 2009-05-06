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
// A class encapsulating the state of a MP3 stream
// Implementation

#include "MP3StreamState.hh"
#include "GroupsockHelper.hh"

#if defined(__WIN32__) || defined(_WIN32)
#include <windows.h>
#define _close closesocket
#define snprintf _snprintf
#if defined(_WINNT)
#include <ws2tcpip.h>
#endif
#else
#include <unistd.h>
#define _close close
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#endif

#define MILLION 1000000

MP3StreamState::MP3StreamState()
  : fFid(NULL) {
}

MP3StreamState::~MP3StreamState() {
  // Close our open file or socket:
  if (fFid != NULL && fFid != stdin) {
    if (fFidIsReallyASocket) {
	_close((int)fFid);
    } else {
      fclose(fFid);
    }
  }
}

#ifdef BSD
static struct timezone Idunno;
#else
static int Idunno;
#endif

void MP3StreamState::assignStream(FILE* fid, unsigned fileSize) {
  fFid = fid;

  if (fileSize == (unsigned)(-1)) { /*HACK#####*/
    fFidIsReallyASocket = 1;
    fFileSize = 0;
  } else {
    fFidIsReallyASocket = 0;
    fFileSize = fileSize;
  }

  // Set the first frame's 'presentation time' to the current wall time:
  gettimeofday(&fNextFramePresentationTime, &Idunno);
}

struct timeval MP3StreamState::currentFramePlayTime() const {
  unsigned const numSamples = 1152;
  unsigned const freq = fr().samplingFreq*(1 + fr().isMPEG2);

  // result is numSamples/freq
  unsigned const uSeconds
    = ((numSamples*2*MILLION)/freq + 1)/2; // rounds to nearest integer 

  struct timeval result;
  result.tv_sec = uSeconds/MILLION;
  result.tv_usec = uSeconds%MILLION;
  return result;
}

unsigned MP3StreamState::filePlayTime() const {
  // Note: The following doesn't work for VBR files - need to fix #####
  unsigned numFrames = fFileSize/(4 + fCurrentFrame.frameSize);
  float playTime = getPlayTime(numFrames);
  return (unsigned)(playTime + 0.5); // rounds to nearest integer
}

unsigned MP3StreamState::findNextHeader(struct timeval& presentationTime) {
  presentationTime = fNextFramePresentationTime;

  if (!findNextFrame()) return 0;

  // From this frame, figure out the *next* frame's presentation time:
  struct timeval framePlayTime = currentFramePlayTime();
  fNextFramePresentationTime.tv_usec += framePlayTime.tv_usec;
  fNextFramePresentationTime.tv_sec
    += framePlayTime.tv_sec + fNextFramePresentationTime.tv_usec/MILLION;
  fNextFramePresentationTime.tv_usec %= MILLION;

  return fr().hdr;
}

Boolean MP3StreamState::readFrame(unsigned char* outBuf, unsigned outBufSize,
				  unsigned& resultFrameSize) {
  /* We assume that "mp3FindNextHeader()" has already been called */

  resultFrameSize = 4 + fr().frameSize;

  if (outBufSize < resultFrameSize) {
    fprintf(stderr, "Insufficient buffer size for reading input frame (%d, need %d)\n",
	    outBufSize, resultFrameSize);
    if (outBufSize < 4) outBufSize = 0;
    resultFrameSize = outBufSize;

    return False;
  }

  if (resultFrameSize >= 4) {
    unsigned& hdr = fr().hdr;
    *outBuf++ = (unsigned char)(hdr>>24);
    *outBuf++ = (unsigned char)(hdr>>16);
    *outBuf++ = (unsigned char)(hdr>>8);
    *outBuf++ = (unsigned char)(hdr);

    memmove(outBuf, fr().frameBytes, resultFrameSize-4);
  }

  return True;
}

void MP3StreamState::getAttributes(char* buffer, unsigned bufferSize) const {
  char const* formatStr
    = "bandwidth %d MPEGnumber %d MPEGlayer 3 samplingFrequency %d isStereo %d playTime %d";
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4)
  /* snprintf() isn't defined, so just use sprintf() - ugh! */
  sprintf(buffer, formatStr,
	  fr().bitrate, fr().isMPEG2 ? 2 : 1, fr().samplingFreq, fr().isStereo,
	  filePlayTime());
#else
  snprintf(buffer, bufferSize, formatStr,
	  fr().bitrate, fr().isMPEG2 ? 2 : 1, fr().samplingFreq, fr().isStereo,
	  filePlayTime());
#endif
}

float MP3StreamState::getPlayTime(unsigned numFrames) const {
  struct timeval const pt = currentFramePlayTime();
  float fpt = pt.tv_sec + pt.tv_usec/(float)MILLION;

  return numFrames*fpt;
}

void MP3StreamState::writeGetCmd(char const* hostName,
				 unsigned short portNum,
				 char const* fileName) {
  char* getCmdFmt = "GET %s HTTP/1.1\r\nHost: %s:%d\r\n\r\n";

  if (fFidIsReallyASocket) {
    int sock = (int)fFid;
    char writeBuf[100];
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4)
    /* snprintf() isn't defined, so just use sprintf() */
    /* This is a security risk if filename can come from an external user */
    sprintf(writeBuf, getCmdFmt, fileName, hostName, portNum);
#else
    snprintf(writeBuf, sizeof writeBuf, getCmdFmt,
	     fileName, hostName, portNum);
#endif
    send(sock, writeBuf, strlen(writeBuf), 0);
  } else {
    fprintf(fFid, getCmdFmt, fileName, hostName, portNum);
    fflush(fFid);
  }
}

// This is crufty old code that needs to be cleaned up #####
#define HDRCMPMASK 0xfffffd00

Boolean MP3StreamState::findNextFrame() {
  unsigned char hbuf[8];
  unsigned l; int i;
  int attempt = 0;
  
#ifdef DEBUGGING_INPUT
  /* use this debugging code to generate a copy of the input stream */
  FILE* fout;
  unsigned char c;
  fout = fopen("testOut", "w");
  while (readFromStream(&c, 1) ==1) {
    fwrite(&c, 1, 1, fout);
  }
  fclose(fout);
  exit(0);
#endif
  
 read_again:
  if (readFromStream(hbuf, 4) != 4) return False;
  
  fr().hdr =  ((unsigned long) hbuf[0] << 24)
            | ((unsigned long) hbuf[1] << 16)
            | ((unsigned long) hbuf[2] << 8)
            | (unsigned long) hbuf[3];
  
#ifdef DEBUG_PARSE
  fprintf(stderr, "fr().hdr: 0x%08x\n", fr().hdr);
#endif
  if (fr().oldHdr != fr().hdr || !fr().oldHdr) {
    i = 0;
  init_resync:
#ifdef DEBUG_PARSE
    fprintf(stderr, "init_resync: fr().hdr: 0x%08x\n", fr().hdr);
#endif
    if (   (fr().hdr & 0xffe00000) != 0xffe00000
	|| (fr().hdr & 0x00000C00) == 0x00000C00/* 'stream error' test below */
	|| (fr().hdr & 0x0000F000) == 0 /* 'free format' test below */
        || (fr().hdr & 0x00060000) != 0x00020000 /* test for layer 3 */
       ) {
      /* RSF: Do the following test even if we're not at the
	 start of the file, in case we have two or more
	 separate MP3 files cat'ed together:
      */
      /* Check for RIFF hdr */
      if (fr().hdr == ('R'<<24)+('I'<<16)+('F'<<8)+'F') {
	unsigned char buf[70 /*was: 40*/];
#ifdef DEBUG_ERRORS
	fprintf(stderr,"Skipped RIFF header\n");
#endif
	readFromStream(buf, 66); /* already read 4 */
	goto read_again;
      }
      /* Check for ID3 hdr */
      if ((fr().hdr&0xFFFFFF00) == ('I'<<24)+('D'<<16)+('3'<<8)) {
	unsigned tagSize, bytesToSkip;
	unsigned char buf[1000];
	readFromStream(buf, 6); /* already read 4 */
	tagSize = ((buf[2]&0x7F)<<21) + ((buf[3]&0x7F)<<14) + ((buf[4]&0x7F)<<7) + (buf[5]&0x7F);
	bytesToSkip = tagSize;
	while (bytesToSkip > 0) {
	  unsigned bytesToRead = sizeof buf;
	  if (bytesToRead > bytesToSkip) {
	    bytesToRead = bytesToSkip;
	  }
	  readFromStream(buf, bytesToRead);
	  bytesToSkip -= bytesToRead;
	}
#ifdef DEBUG_ERRORS
	fprintf(stderr,"Skipped %d-byte ID3 header\n", tagSize);
#endif
	goto read_again;
      }
      /* give up after 20,000 bytes */
      if (i++ < 20000/*4096*//*1024*/) {
	memmove (&hbuf[0], &hbuf[1], 3);
	if (readFromStream(hbuf+3,1) != 1) {
	  return False;
	}
	fr().hdr <<= 8;
	fr().hdr |= hbuf[3];
	fr().hdr &= 0xffffffff;
#ifdef DEBUG_PARSE
	fprintf(stderr, "calling init_resync %d\n", i);
#endif
	goto init_resync;
      }
#ifdef DEBUG_ERRORS
      fprintf(stderr,"Giving up searching valid MPEG header\n");
#endif
      return False;
      
#ifdef DEBUG_ERRORS
      fprintf(stderr,"Illegal Audio-MPEG-Header 0x%08lx at offset 0x%lx.\n",
	      fr().hdr,tell_stream(str)-4);
#endif
      /* Read more bytes until we find something that looks
	 reasonably like a valid header.  This is not a
	 perfect strategy, but it should get us back on the
	 track within a short time (and hopefully without
	 too much distortion in the audio output).  */
      do {
	attempt++;
	memmove (&hbuf[0], &hbuf[1], 7);
	if (readFromStream(&hbuf[3],1) != 1) {
	  return False;
	}
	
	/* This is faster than combining fr().hdr from scratch */
	fr().hdr = ((fr().hdr << 8) | hbuf[3]) & 0xffffffff;
	
	if (!fr().oldHdr)
	  goto init_resync;       /* "considered harmful", eh? */
	
      } while ((fr().hdr & HDRCMPMASK) != (fr().oldHdr & HDRCMPMASK)
	       && (fr().hdr & HDRCMPMASK) != (fr().firstHdr & HDRCMPMASK));
#ifdef DEBUG_ERRORS
      fprintf (stderr, "Skipped %d bytes in input.\n", attempt);
#endif
    }
    if (!fr().firstHdr) {
      fr().firstHdr = fr().hdr;
    }
    
    fr().setParamsFromHeader();
    fr().setBytePointer(fr().frameBytes, fr().frameSize);
    
    fr().oldHdr = fr().hdr;
    
    if (fr().isFreeFormat) {
#ifdef DEBUG_ERRORS
      fprintf(stderr,"Free format not supported.\n");
#endif
      return False;
    }
    
    if (fr().layer != 3) {
#ifdef DEBUG_ERRORS
      fprintf(stderr, "MPEG layer %d is not supported!\n", fr().layer);
#endif
      return False;
    }
  }
  
  if ((l = readFromStream(fr().frameBytes, fr().frameSize))
      != fr().frameSize) {
    if (l == 0) return False;
    memset(fr().frameBytes+1, 0, fr().frameSize-1);
  }
  
  return True;
}

unsigned MP3StreamState::readFromStream(unsigned char* buf,
					unsigned numChars) {
  // Hack for doing socket I/O instead of file I/O (e.g., on Windows)
  if (fFidIsReallyASocket) {
    int sock = (int)fFid;
    unsigned totBytesRead = 0;
    do {
      int bytesRead
	= recv(sock, &((char*)buf)[totBytesRead], numChars-totBytesRead, 0);
      if (bytesRead < 0) return 0;

      totBytesRead += (unsigned)bytesRead;
    } while (totBytesRead < numChars);

    return totBytesRead;
  } else {
    return fread(buf, 1, numChars, fFid);
  }
}
