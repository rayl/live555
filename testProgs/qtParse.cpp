// Parses QuickTime files

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdlib.h>

static unsigned parseAtom(unsigned char const* ptr, unsigned size);
// forward

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <quickTimeFileName>\n", argv[0]);
    exit(1);
  }
  char const* fileName = argv[1];

  FILE* fid = fopen(argv[1], "rb");
  if (fid == NULL) {
    fprintf(stderr, "Failed to open \"%s\"\n", fileName);
    exit(1);
  }

  // Figure out the file's size
  struct stat sb;
  unsigned fileSize;
  if (stat(fileName, &sb) == 0) {
    fileSize = sb.st_size;
  }

  fprintf(stderr, "File size: %d\n", fileSize);

  // Map the file into memory:
  unsigned char* mappedFile
    = (unsigned char*)mmap(0, fileSize, PROT_READ, 0, fileno(fid), 0);

  // Begin parsing:
  unsigned offset = 0;
  do {
    unsigned atomSize = parseAtom(&mappedFile[offset], fileSize);
    offset += atomSize;
    fileSize -= atomSize;
  } while (fileSize > 0);
}

static unsigned getWord(unsigned char const*& ptr) {
  unsigned result = (ptr[0]<<24)|(ptr[1]<<16)|(ptr[2]<<8)|ptr[3];
  ptr += 4;
  return result;
}

static int isKnownAtom(unsigned atomType, unsigned atomSize,
		       unsigned& offsetToEnclosedAtoms);

static unsigned level = 0;

void levelPrint() {
  if (level == 0) return;

  fprintf(stderr, "%d:", level);
  for (unsigned i = 0; i < level; ++i) {
    fprintf(stderr, "\t");
  }
}

#define at(x,y,z,w) ( ((x)<<24)|((y)<<16)|((z)<<8)|(w) )

static unsigned parseAtom(unsigned char const* ptr, unsigned size) {
  if (size < 8) {
    levelPrint();
    fprintf(stderr, "Size %d too small to be an atom!\n", size);
    exit(1);
  }

  unsigned atomSize = getWord(ptr);
  if (atomSize < 8 || atomSize > size) {
    levelPrint();
    fprintf(stderr, "Saw bad atom size %d (expected >= 8 and <= %d)!\n",
	    atomSize, size);
    exit(1);
  }
  unsigned atomType = getWord(ptr);
  levelPrint();
  fprintf(stderr, "%c%c%c%c (%d bytes)\n",
	  atomType>>24, atomType>>16, atomType>>8, atomType, atomSize);

  // Check whether we know about the structure of this atom.
  // If we do, recursively parse its interior:
  unsigned offsetToEnclosedAtoms;
  if (isKnownAtom(atomType, atomSize, offsetToEnclosedAtoms)) {
    if (8+offsetToEnclosedAtoms > atomSize) {
      levelPrint();
      fprintf(stderr, "Atom size %d is not large enough to hold this atom (need %d)!\n", atomSize, 8+offsetToEnclosedAtoms);
      exit(1);
    }

    if (offsetToEnclosedAtoms > 0) {
      levelPrint();
      if (offsetToEnclosedAtoms <= 2000) {
	for (int i = 0; i < offsetToEnclosedAtoms; ++i) {
	  if (i%4 == 0) fprintf(stderr, ":");
	  fprintf(stderr, "%02x", ptr[i]);
	}
	fprintf(stderr, "\n");
      } else {
	fprintf(stderr, " <%d interior bytes>\n", offsetToEnclosedAtoms);
      }
      ptr += offsetToEnclosedAtoms;
    }

    unsigned bytesForEnclosedAtoms = atomSize - (8+offsetToEnclosedAtoms);
    while (bytesForEnclosedAtoms > 0) {
      // Hack for 'udta' atom; it can end with a trailing 32-bit word
      if (atomType == at('u','d','t','a') && bytesForEnclosedAtoms == 4) {
	ptr += 4; bytesForEnclosedAtoms = 0;
	break;
      }

      ++level;
      unsigned enclosedAtomSize = parseAtom(ptr, bytesForEnclosedAtoms);
      --level;
      ptr += enclosedAtomSize;
      bytesForEnclosedAtoms -= enclosedAtomSize;
    }
  } else {
    fprintf(stderr, "UNKNOWN atom type %c%c%c%c\n",
	    atomType>>24, atomType>>16, atomType>>8, atomType);
    exit(1);//#####
  }

  return atomSize;
}

static int isKnownAtom(unsigned atomType, unsigned atomSize,
		       unsigned& offsetToEnclosedAtoms) {
  switch(atomType) {
  case at('?','?','?','?'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('a','g','s','m'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('a','l','a','w'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('a','l','i','s'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('c','m','o','v'): {offsetToEnclosedAtoms = 0; break;}
  case at('c','m','v','d'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('d','c','o','m'): {offsetToEnclosedAtoms = 1*4; break;}
  case at('d','i','n','f'): {offsetToEnclosedAtoms = 0; break;}
  case at('d','i','m','m'): {offsetToEnclosedAtoms = 2*4; break;}
  case at('d','m','a','x'): {offsetToEnclosedAtoms = 1*4; break;}
  case at('d','m','e','d'): {offsetToEnclosedAtoms = 2*4; break;}
  case at('d','r','e','f'): {offsetToEnclosedAtoms = 2*4; break;}
  case at('d','r','e','p'): {offsetToEnclosedAtoms = 2*4; break;}
  case at('e','d','t','s'): {offsetToEnclosedAtoms = 0; break;}
  case at('e','l','s','t'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('e','s','d','s'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('f','r','e','e'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('f','t','y','p'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('g','m','h','d'): {offsetToEnclosedAtoms = 0; break;}
  case at('g','m','i','n'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('h','2','6','3'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('h','d','l','r'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('h','i','n','t'): {offsetToEnclosedAtoms = 1*4; break;}
  case at('h','i','n','f'): {offsetToEnclosedAtoms = 0; break;}
  case at('h','i','n','v'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('h','m','h','d'): {offsetToEnclosedAtoms = 5*4; break;}
  case at('h','n','t','i'): {offsetToEnclosedAtoms = 0; break;}
  case at('i','o','d','s'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('m','a','x','r'): {offsetToEnclosedAtoms = 2*4; break;}
  case at('M','C','P','S'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('m','d','a','t'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('m','d','h','d'): {offsetToEnclosedAtoms = 6*4; break;}
  case at('m','d','i','a'): {offsetToEnclosedAtoms = 0; break;}
  case at('m','i','n','f'): {offsetToEnclosedAtoms = 0; break;}
  case at('m','o','o','v'): {offsetToEnclosedAtoms = 0; break;}
  case at('m','p','3',' '): {offsetToEnclosedAtoms = 7*4; break;}
  case at('m','p','4','a'): {offsetToEnclosedAtoms = 11*4; break;}
  case at('m','p','4','v'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('m','v','h','d'): {offsetToEnclosedAtoms = 25*4; break;}
  case at('n','a','m','e'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('n','p','c','k'): {offsetToEnclosedAtoms = 1*4; break;}
  case at('n','u','m','p'): {offsetToEnclosedAtoms = 2*4; break;}
  case at('p','a','y','t'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('p','m','a','x'): {offsetToEnclosedAtoms = 1*4; break;}
  case at('Q','c','l','p'): {offsetToEnclosedAtoms = 11*4; break;}
  case at('Q','D','M','2'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('r','e','l','y'): {offsetToEnclosedAtoms = 1*1; break;}
  case at('r','p','z','a'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('r','t','p',' '): {offsetToEnclosedAtoms = 4*4; break;}
  case at('s','d','p',' '): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('s','m','h','d'): {offsetToEnclosedAtoms = 2*4; break;}
  case at('s','n','r','o'): {offsetToEnclosedAtoms = 1*4; break;}
  case at('s','t','b','l'): {offsetToEnclosedAtoms = 0; break;}
  case at('s','t','c','o'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('s','t','s','c'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('s','t','s','d'): {offsetToEnclosedAtoms = 2*4; break;}
  case at('s','t','s','h'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('s','t','s','s'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('s','t','s','z'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('s','t','t','s'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('S','V','Q','1'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('t','e','x','t'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('t','i','m','s'): {offsetToEnclosedAtoms = 1*4; break;}
  case at('t','k','h','d'): {offsetToEnclosedAtoms = 21*4; break;}
  case at('t','m','a','x'): {offsetToEnclosedAtoms = 1*4; break;}
  case at('t','m','i','n'): {offsetToEnclosedAtoms = 1*4; break;}
  case at('t','o','t','l'): {offsetToEnclosedAtoms = 1*4; break;}
  case at('t','p','a','y'): {offsetToEnclosedAtoms = 1*4; break;}
  case at('t','p','y','l'): {offsetToEnclosedAtoms = 2*4; break;}
  case at('t','r','p','y'): {offsetToEnclosedAtoms = 2*4; break;}
  case at('t','r','a','k'): {offsetToEnclosedAtoms = 0; break;}
  case at('t','r','e','f'): {offsetToEnclosedAtoms = 0; break;}
  case at('t','s','r','o'): {offsetToEnclosedAtoms = 1*4; break;}
  case at('t','w','o','s'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('u','d','t','a'): {offsetToEnclosedAtoms = 0; break;}
  case at('u','l','a','w'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('u','r','l',' '): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('v','m','h','d'): {offsetToEnclosedAtoms = 3*4; break;}
  case at('V','P','3','1'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('w','a','v','e'): {offsetToEnclosedAtoms = atomSize-8; break;}
  case at('w','i','d','e'): {offsetToEnclosedAtoms = 0; break;}
  case at('W','L','O','C'): {offsetToEnclosedAtoms = atomSize-8; break;}
  default: {return 0;}
  }

  return 1;
}
