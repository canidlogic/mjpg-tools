/*
 * jpgtrace.c
 * 
 * Print out all the markers in a JPEG file.
 * 
 * Syntax:
 * 
 *   jpgtrace [path]
 * 
 * Parameters:
 * 
 *   [path] - the path of the JPEG file to trace
 * 
 * Operation:
 * 
 *   This program works both with normal JPEG files and also with Motion
 *   JPEG (M-JPEG) files, but only M-JPEG files that are in the "raw"
 *   stream format.  (M-JPEG files encapsulated in AVI or Quicktime MOV
 *   files will not work.)
 * 
 *   All of the markers contained in the JPEG file are printed to
 *   standard output.
 * 
 * Compilation:
 * 
 *   This program uses its own parser.  libjpeg is *not* required.
 * 
 *   Compile with 64-bit file offset support if you're going to try this
 *   on huge M-JPEG files.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * The unsigned byte value used to signal markers.
 */
#define JPEG_PREMARK (0xff)

/*
 * JPEG marker definitions.
 */
#define JPEG_TEM      (0x01)    /* Temporary for arithmetic coding */
#define JPEG_RST_MIN  (0xD0)    /* Restart Marker zero */
#define JPEG_RST_MAX  (0xD7)    /* Restart Marker seven */
#define JPEG_SOI      (0xD8)    /* Start Of Image */
#define JPEG_EOI      (0xD9)    /* End Of Image */
#define JPEG_SOS      (0xDA)    /* Start Of Scan */
#define JPEG_DNL      (0xDC)    /* Define Number Of Lines */

/* Function prototypes */
static int jpeg_isStandAlone(int c);
static int jpeg_isImmediate(int c);

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
  FILE *fp = NULL;
  
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
  
  /* Open the provided file for reading */
  if (status) {
    fp = fopen(argv[1], "rb");
    if (fp == NULL) {
      fprintf(stderr, "Can't open input file!\n");
      status = 0;
    }
  }
  
  /* Read and report all markers */
  while (status) {
    
    /* Read a byte, leaving loop if EOF */
    c = getc(fp);
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
    
    /* Report the marker we just read */
    if (status) {
      printf("Marker 0x%02X\n", (unsigned int) c);
    }
    
    /* If this is not a stand-alone marker, read two bytes to get the
     * length of the data payload; else, zero out marker length */
    if (status) {
      if (!jpeg_isStandAlone(c)) {
        
        /* Read the first length byte */
        c2 = getc(fp);
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
      }
    }
    
    /* If we just read an SOS marker, we need to skip over compressed
     * data, reporting any embedded markers along the way */
    if (status && (c == JPEG_SOS)) {
      while (status) {
        
        /* Read a character */
        c2 = getc(fp);
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
         * data; if the marker is immediate, then report it as immediate
         * and proceed; if the marker is non-immediate, then backtrack
         * to the last 0xFF byte and we're done
         * 
         * We also check for the immediate DNL and raise an error in
         * that case because it's rarely used and it carries a data
         * payload, which we don't support here for immediates */
        if (status && (c2 != 0)) {
          if (jpeg_isImmediate(c2)) {
            /* Immediate -- report the immediate */
            printf("Immediate 0x%02X\n", (unsigned int) c2);
            
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
  
  /* Close JPEG file if open */
  if (fp != NULL) {
    fclose(fp);
    fp = NULL;
  }
  
  /* Invert status and return */
  if (status) {
    status = 0;
  } else {
    status = 1;
  }
  return status;
}
