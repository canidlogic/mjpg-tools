/*
 * mjpg_index.c
 * 
 * Create an index of all frames in a raw Motion-JPEG stream.
 * 
 * Syntax:
 * 
 *   mjpg_index [path]
 * 
 * Parameters:
 * 
 *   [path] - the path of the raw Motion-JPEG file
 * 
 * Operation:
 * 
 *   This program only works with M-JPEG files that are in the "raw"
 *   stream format.  (M-JPEG files encapsulated in AVI or Quicktime MOV
 *   files will not work.)  If you pass a simple JPEG file, this program
 *   will treat it as though it were an M-JPEG file with a single frame.
 * 
 *   The output is written to a file that is the passed [path] with
 *   ".index" suffixed to it.  If this file already exists, it is
 *   overwritten.  The output is an array of 64-bit integers in big
 *   endian ordering.  The first integer stores how many frames there
 *   are, which is always one or greater.  This is followed by one
 *   integer per frame.  Each integer is a byte offset within the
 *   Motion-JPEG sequence of the start of the JPEG frame.  These offsets
 *   are in strictly ascending order.
 * 
 * Compilation:
 * 
 *   This program uses its own parser.  libjpeg is *not* required.
 * 
 *   Compile with 64-bit file offset support if you're going to try this
 *   on huge M-JPEG files.  Define _FILE_OFFSET_BITS=64
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The unsigned byte value used to signal markers.
 */
#define JPEG_PREMARK (0xff)

/*
 * JPEG marker definitions.
 */
#define JPEG_TEM      (0x01)    /* Temporary for arithmetic coding */

#define JPEG_SOF_0    (0xC0)    /* Start Of Frame, Type 0 */
#define JPEG_SOF_1    (0xC1)    /* Start Of Frame, Type 1 */
#define JPEG_SOF_2    (0xC2)    /* Start Of Frame, Type 2 */
#define JPEG_SOF_3    (0xC3)    /* Start Of Frame, Type 3 */

#define JPEG_SOF_5    (0xC5)    /* Start Of Frame, Type 5 */
#define JPEG_SOF_6    (0xC6)    /* Start Of Frame, Type 6 */
#define JPEG_SOF_7    (0xC7)    /* Start Of Frame, Type 7 */

#define JPEG_SOF_9    (0xC9)    /* Start Of Frame, Type 9 */
#define JPEG_SOF_10   (0xCA)    /* Start Of Frame, Type 10 */
#define JPEG_SOF_11   (0xCB)    /* Start Of Frame, Type 11 */

#define JPEG_SOF_13   (0xCD)    /* Start Of Frame, Type 13 */
#define JPEG_SOF_14   (0xCE)    /* Start Of Frame, Type 14 */
#define JPEG_SOF_15   (0xCF)    /* Start Of Frame, Type 15 */

#define JPEG_DHT      (0xC4)    /* Define Huffman Table */
#define JPEG_DAC      (0xCC)    /* Define Arithmetic Coding cond. */
#define JPEG_DQT      (0xDB)    /* Define Quantization Tables */
#define JPEG_DRI      (0xDD)    /* Define Restart Interval */
#define JPEG_DHP      (0xDE)    /* Define Hierarchical Progression */
#define JPEG_EXP      (0xDF)    /* Expand Reference Components */
#define JPEG_COM      (0xFE)    /* Comment */

#define JPEG_APP_MIN  (0xE0)    /* Application-specific data zero */
#define JPEG_APP_MAX  (0xEF)    /* Application-specific data fifteen */

#define JPEG_RST_MIN  (0xD0)    /* Restart Marker zero */
#define JPEG_RST_MAX  (0xD7)    /* Restart Marker seven */
#define JPEG_SOI      (0xD8)    /* Start Of Image */
#define JPEG_EOI      (0xD9)    /* End Of Image */
#define JPEG_SOS      (0xDA)    /* Start Of Scan */
#define JPEG_DNL      (0xDC)    /* Define Number Of Lines */

/* Function prototypes */
static int jpeg_isStandAlone(int c);
static int jpeg_isImmediate(int c);
static void writeInt64BE(FILE *pOut, int64_t val);
static char *suffix(const char *pa, const char *pb);

/*
 * Return whether the given marker is a stand-alone JPEG marker.
 * 
 * A stand-alone JPEG marker carries no data and has no data length
 * following it.
 * 
 * c is the marker byte, which must be in range 0x00-0xFE.
 * 
 * Parameters:
 * 
 *   c - the marker byte to check
 * 
 * Return:
 * 
 *   non-zero if this is a stand-alone marker, zero if not
 */
static int jpeg_isStandAlone(int c) {
  
  int result = 0;
  
  /* Check parameter */
  if ((c < 0) || (c > 0xfe)) {
    abort();
  }
  
  /* Check for stand-alone types */
  if ((c == JPEG_TEM) || ((c >= JPEG_RST_MIN) && (c <= JPEG_RST_MAX)) ||
      (c == JPEG_SOI) || (c == JPEG_EOI)) {
    result = 1;
  
  } else {
    result = 0;
  }
  
  /* Return result */
  return result;
}

/*
 * Return whether a given marker is an "immediate" JPEG marker.
 * 
 * Immediate markers can occur within compressed data.  These are the
 * RST markers and the DNL marker.  The special zero-escape marker is
 * not considered an immediate because it is not a real marker.
 * 
 * c is the marker byte, which must be in range 0x00-0xFE.
 * 
 * Parameters:
 * 
 *   c - the marker byte to check
 * 
 * Return:
 * 
 *   non-zero if this is an immediate marker, zero if not
 */
static int jpeg_isImmediate(int c) {
  
  int result = 0;
  
  /* Check parameter */
  if ((c < 0) || (c > 0xfe)) {
    abort();
  }
  
  /* Check for immediate types */
  if ((c == JPEG_DNL) || ((c >= JPEG_RST_MIN) && (c <= JPEG_RST_MAX))) {
    result = 1;
  
  } else {
    result = 0;
  }
  
  /* Return result */
  return result;
}

/*
 * Write a 64-bit integer in big endian to the given output file.
 * 
 * pOut is the handle to write to.  It must be open for writing in
 * binary mode or undefined behavior occurs.  Writing is fully
 * sequential.  The integer is written starting at the current file
 * position.
 * 
 * val is the integer value to write.  It must be zero or greater.
 * 
 * A fault occurs if there is any write error.
 * 
 * Parameters:
 * 
 *   pOut - the file handle to write to
 */
static void writeInt64BE(FILE *pOut, int64_t val) {
  
  int i = 0;
  int c = 0;
  
  /* Check parameters */
  if ((pOut == NULL) || (val < 0)) {
    abort();
  }
  
  /* Write bytes in big-endian order */
  for(i = 56; i >= 0; i -= 8) {
    /* Get current byte */
    c = (int) ((val >> i) & 0xff);
    
    /* Write byte */
    if (putc(c, pOut) != c) {
      fprintf(stderr, "I/O error on write!\n");
      abort();
    }
  }
}

/*
 * Given two nul-terminated strings, allocate a new nul-terminated
 * string that is the concatenation of the two strings.
 * 
 * Parameters:
 * 
 *   pa - the first string
 * 
 *   pb - the second string
 * 
 * Return:
 * 
 *   a newly allocated string that is the concatenation
 */
static char *suffix(const char *pa, const char *pb) {
  
  long full_size = 0;
  long sz = 0;
  size_t szt = 0;
  char *pr = NULL;
  
  /* Check parameters */
  if ((pa == NULL) || (pb == NULL)) {
    abort();
  }
  
  /* Determine combined length (without terminating nuls) and watch for
   * overflow */
  szt = strlen(pa);
  if (szt >= LONG_MAX) {
    abort();
  }
  sz = (long) szt;
  
  if (full_size <= LONG_MAX - sz) {
    full_size += sz;
  } else {
    abort();
  }
  
  szt = strlen(pb);
  if (szt >= LONG_MAX) {
    abort();
  }
  sz = (long) szt;
  
  if (full_size <= LONG_MAX - sz) {
    full_size += sz;
  } else {
    abort();
  }
  
  /* Add one more byte for the terminating nul */
  if (full_size < LONG_MAX) {
    full_size++;
  } else {
    abort();
  }
  
  /* Allocate the buffer and clear to zero */
  pr = (char *) calloc((size_t) full_size, 1);
  if (pr == NULL) {
    abort();
  }
  
  /* Copy the strings in */
  strcat(pr, pa);
  strcat(pr, pb);
  
  /* Return the new string */
  return pr;
}

/*
 * Program entrypoint.
 * 
 * See the documentation at the top of this source file for the details
 * of how this program works.
 * 
 * argc is the number of parameters in argv.  argv is an array of
 * pointers to null-terminated string parameters.  The first parameter
 * in argv  is the module name, the second is the first actual command
 * line parameter.
 * 
 * Parameters:
 * 
 *   argc - the number of elements in argv
 * 
 *   argv - array of pointers to null-terminated string parameters
 * 
 * Return:
 * 
 *   zero if successful, one if error
 */
int main(int argc, char *argv[]) {
  
  int x = 0;
  int c = 0;
  int c2 = 0;
  int status = 1;
  int eoi_read = 0;
  long mark_len = 0;
  long frame_count = 0;
  FILE *fp = NULL;
  FILE *fi = NULL;
  char *pIPath = NULL;
  
  int64_t read_count = 0;
  
  /* Check parameters */
  if (argc < 0) {
    abort();
  }
  if ((argc > 0) && (argv == NULL)) {
    abort();
  }
  for(x = 0; x < argc; x++) {
    if (argv[x] == NULL) {
      abort();
    }
  }
  
  /* We need exactly one parameter beyond the module name */
  if (argc != 2) {
    fprintf(stderr, "Expecting exactly one parameter!\n");
    status = 0;
  }
  
  /* Generate the index file name */
  if (status) {
    pIPath = suffix(argv[1], ".index");
  }
  
  /* Open the provided file for reading */
  if (status) {
    fp = fopen(argv[1], "rb");
    if (fp == NULL) {
      fprintf(stderr, "Can't open input file!\n");
      status = 0;
    }
  }
  
  /* Open the index file for writing */
  if (status) {
    fi = fopen(pIPath, "wb");
    if (fi == NULL) {
      fprintf(stderr, "Can't create index file!\n");
      status = 0;
    }
  }
  
  /* Write a zero at the start of the index file for now -- we will fill
   * it in with the frame count at the end */
  if (status) {
    writeInt64BE(fi, 0);
  }
  
  /* Read and report all markers */
  while (status) {
    
    /* Read a byte, leaving loop if EOF */
    c = getc(fp);
    read_count++;
    if (c == EOF) {
      if (feof(fp)) {
        /* We are done reading all the markers -- make sure we read at
         * least one marker and that the last marker we read was EOI */
        if (eoi_read) {
          break;
        } else {
          fprintf(stderr, "Missing EOI marker!\n");
          status = 0;
        }
      
      } else {
        /* I/O error */
        fprintf(stderr, "I/O error!\n");
        status = 0;
      }
    }
    
    /* The byte we read must be 0xff, or the file is invalid */
    if (status) {
      if (c != JPEG_PREMARK) {
        fprintf(stderr, "Missing pre-marker byte!\n");
        status = 0;
      }
    }
    
    /* Read a sequence of zero or more 0xff bytes, until we get the
     * actual marker byte; an EOF is an error here */
    while (status) {
      
      /* Read another byte */
      c = getc(fp);
      read_count++;
      if (c == EOF) {
        if (feof(fp)) {
          /* EOF not allowed here */
          fprintf(stderr, "Missing marker byte!\n");
          status = 0;
          
        } else {
          /* I/O error */
          fprintf(stderr, "I/O error!\n");
          status = 0;
        }
      }
      
      /* We're done if we got anything besides the pre-marker */
      if (status && (c != JPEG_PREMARK)) {
        break;
      }
    }
    
    /* If the marker is SOI, then increment frame count, watching for
     * overflow, and report a frame as beginning at the byte offset
     * before this one (to include the 0xff byte before the SOI) */
    if (status && (c == JPEG_SOI)) {
      if (frame_count < LONG_MAX) {
        frame_count++;
        writeInt64BE(fi, read_count - 2);
        
      } else {
        fprintf(stderr, "Too many frames!\n");
        status = 0;
      }
    }
    
    /* If this is not a stand-alone marker, read two bytes to get the
     * length of the data payload; else, zero out marker length */
    if (status) {
      if (!jpeg_isStandAlone(c)) {
        
        /* Read the first length byte */
        c2 = getc(fp);
        read_count++;
        if (c2 == EOF) {
          if (feof(fp)) {
            /* EOF not allowed here */
            fprintf(stderr, "Missing marker length!\n");
            status = 0;
            
          } else {
            /* I/O error */
            fprintf(stderr, "I/O error!\n");
            status = 0;
          }
        }
        
        /* Set marker length with first length byte */
        if (status) {
          mark_len = (((long) c2) << 8);
        }
        
        /* Read the second length byte */
        if (status) {
          c2 = getc(fp);
          read_count++;
          if (c2 == EOF) {
            if (feof(fp)) {
              /* EOF not allowed here */
              fprintf(stderr, "Partial marker length!\n");
              status = 0;
              
            } else {
              /* I/O error */
              fprintf(stderr, "I/O error!\n");
              status = 0;
            }
          }
        }
        
        /* Get complete marker length, which must be at least two to
         * account for the two length bytes */
        if (status) {
          mark_len |= ((long) c2);
          if (mark_len < 2) {
            fprintf(stderr, "Marker length less than two!\n");
            status = 0;
          }
        }
        
        /* Subtract two from marker length because we've already read
         * the length bytes */
        if (status) {
          mark_len -= 2;
        }
        
      } else {
        /* Stand-alone marker -- set marker length to zero */
        mark_len = 0;
      }
    }
    
    /* If there is a data payload, skip over it */
    if (status && (mark_len > 0)) {
      if (fseeko(fp, (off_t) mark_len, SEEK_CUR)) {
        fprintf(stderr, "Seek failed!\n");
        status = 0;
      } else {
        read_count += (int64_t) mark_len;
      }
    }
    
    /* If we just read an SOS marker, we need to skip over compressed
     * data, reporting any embedded markers along the way */
    if (status && (c == JPEG_SOS)) {
      while (status) {
        
        /* Read a character */
        c2 = getc(fp);
        read_count++;
        if (c2 == EOF) {
          if (feof(fp)) {
            /* EOF not allowed here */
            fprintf(stderr, "EOF in compressed stream!\n");
            status = 0;
            
          } else {
            /* I/O error */
            fprintf(stderr, "I/O error!\n");
            status = 0;
          }
        }
        
        /* If we read anything but JPEG_PREMARK, go to next character */
        if (status && (c2 != JPEG_PREMARK)) {
          continue;
        }
        
        /* We read the JPEG_PREMARK character -- keep reading until we
         * get the potential marker character */
        while (status) {
          
          /* Read a character */
          c2 = getc(fp);
          read_count++;
          if (c2 == EOF) {
            if (feof(fp)) {
              /* EOF not allowed here */
              fprintf(stderr, "EOF in compressed stream!\n");
              status = 0;
            
            } else {
              /* I/O error */
              fprintf(stderr, "I/O error!\n");
              status = 0;
            }
          }
          
          /* We are done if we read something besides the pre-marker */
          if (status && (c2 != JPEG_PREMARK)) {
            break;
          }
        }
        
        /* If the marker is zero, then ignore it and continue because
         * this is simply an escape for 0xFF bytes within compressed
         * data; if the marker is immediate, then proceed; if the marker
         * is non-immediate, then backtrack to the last 0xFF byte and
         * we're done
         * 
         * We also check for the immediate DNL and raise an error in
         * that case because it's rarely used and it carries a data
         * payload, which we don't support here for immediates */
        if (status && (c2 != 0)) {
          if (jpeg_isImmediate(c2)) {
            /* Fail if DNL */
            if (c2 == JPEG_DNL) {
              fprintf(stderr, "DNL markers not supported!\n");
              status = 0;
            }
          
          } else {
            /* Not immediate -- backtrack two bytes to the last 0xFF
             * character and then we're done */
            if (fseeko(fp, (off_t) -2, SEEK_CUR)) {
              fprintf(stderr, "Seek failed!\n");
              status = 0;
            } else {
              read_count -= 2;
            }
            break;
          }
        }
      }
    }
    
    /* Set EOI flag if we just read EOF, else clear it */
    if (status) {
      if (c == JPEG_EOI) {
        eoi_read = 1;
      } else {
        eoi_read = 0;
      }
    }
  }
  
  /* Must have at least one frame */
  if (status && (frame_count < 1)) {
    status = 0;
    fprintf(stderr, "No frames found!\n");
  }
  
  /* Rewind index file and write the number of frames */
  if (status) {
    rewind(fi);
    writeInt64BE(fi, frame_count);
  }
  
  /* Close index file if open */
  if (fi != NULL) {
    fclose(fi);
    fi = NULL;
  }
  
  /* Close JPEG file if open */
  if (fp != NULL) {
    fclose(fp);
    fp = NULL;
  }
  
  /* Free index path string if allocated */
  if (pIPath != NULL) {
    free(pIPath);
    pIPath = NULL;
  }
  
  /* Invert status and return */
  if (status) {
    status = 0;
  } else {
    status = 1;
  }
  return status;
}
