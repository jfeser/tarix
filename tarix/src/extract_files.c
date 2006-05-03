/*
 *  tarix - a GNU/POSIX tar indexer
 *  Copyright (C) 2006 Matthew "Cheetah" Gabeler-Lee
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "debug.h"
#include "tar.h"
#include "tarix.h"
#include "tstream.h"

struct index_entry
{
  unsigned long start;
  unsigned long length;
  char *filename;
};

int extract_files(const char *indexfile, const char *tarfile, int use_mt,
    int zlib_level, int debug_messages, int argc, char *argv[],
    int firstarg) {
  int *arglens;
  int n, nread;
  char *linebuf = (char*)malloc(TARBLKSZ);
  int rdoff = 0;
  int linebufsz = TARBLKSZ;
  int linebufavail = linebufsz;
  char *nlpos;
  int linelen;
  char *iparse;
  unsigned long ioffset, ilen;
  /* curpose always tracks block offsets */
  off64_t zoffset, destoff, curpos = 0;
  int gotheader = 0;
  char passbuf[TARBLKSZ];
  int index, tar;
  t_streamp tsp = NULL;
  int use_new = 0;
  int outfd = 1;
  int version;
  
  /* the basic idea:
   * read the index an entry at a time
   * scan the index entry against each arg and pass through the file if
   * it matches
   */
  
  /* open the index file */
  if ((index = open(indexfile, O_RDONLY, 0666)) < 0) {
    perror("open indexfile");
    return 1;
  }
  /* maybe warn user about no largefile on stdin? */
  if (tarfile == NULL) {
    /* stdin */
    tar = 0;
  } else {
    if ((tar = open(tarfile, O_RDONLY|P_O_LARGEFILE)) < 0) {
      perror("open tarfile");
      return 1;
    }
  }
  
  /* tstream handles base offset */
  tsp = init_trs(NULL, tar, use_mt, TARBLKSZ, zlib_level);
  if (tsp->zlib_err != Z_OK) {
    fprintf(stderr, "zlib init error: %d\n", tsp->zlib_err);
    return 1;
  }
  
  /* prep step: calculate string lengths of args only once */
  arglens = calloc(argc - firstarg, sizeof(int));
  for (n = firstarg; n < argc; ++n)
    arglens[n-firstarg] = strlen(argv[n]);
  
  /* our solution to line-based reading in a portable way:
   * it would be nice to use the bsd fgetln func, but that's not portable
   * so we just allocate a buffer and do sequential reads and memmoves
   */
  
  /* linebufavail - 1: make sure there's room to drop a '\0' in */
  while ((nread = read(index, linebuf + rdoff, linebufavail - 1)) > 0) {
    rdoff += nread;
    linebufavail -= nread;
    linebuf[rdoff] = 0; /* null terminate the input buffer */
    
    /* process any whole lines we've read */
    while ((nlpos = strchr(linebuf, '\n')) != NULL) {
      /* prep, parse the read line */
      linelen = nlpos - linebuf + 1;
      /* make the line a null-terminated string */
      *nlpos = 0;
      if (!gotheader) {
        /* process the header line */
        if (sscanf(linebuf, "TARIX INDEX v%d GENERATED BY ", &version) != 1) {
          fprintf(stderr, "Index header not recognized\n");
          return 1;
        }
        if (version == 0)
          use_new = 0;
        else if (version == 1)
          use_new = 1;
        else {
          fprintf(stderr, "Index version %d not supported\n", version);
          return 1;
        }
        gotheader = 1;
      } else {
        int fnpos, ssret, extract = 0;
        if (use_new) {
          /* games with longlong to avoid warnings on 64bit */
          long long lltmp;
          ssret = sscanf(linebuf, "%ld %lld %ld %n", &ioffset, &lltmp,
            &ilen, &fnpos);
          zoffset = lltmp;
        } else {
          ssret = sscanf(linebuf, "%ld %ld %n", &ioffset, &ilen, &fnpos);
        }
        n = use_new ? 3 : 2;
        if (ssret != n) {
          fprintf(stderr, "index format error: v%d expects %d, got %d\n",
            version, n, ssret);
          return 1;
        }
        iparse = linebuf + fnpos;

        /* take action on the line */
        for (n = firstarg; n < argc; ++n) {
          if (strncmp(argv[n], iparse, arglens[n-firstarg]) == 0) {
            extract = 1;
            break;
          }
        }
        if (extract) {
          DMSG("extracting %s\n", iparse);
          /* seek to the record start and then pass the record through */
          /* don't actually seek if we're already there */
          if (curpos != ioffset) {
            destoff = zlib_level ? zoffset : (off64_t)ioffset * TARBLKSZ;
            DMSG("seeking to %lld\n", (long long)destoff);
            if (ts_seek(tsp, destoff) != 0) {
              fprintf(stderr, "seek error\n");
              return 1;
            }
            curpos = ioffset;
          }
          DMSG("reading %ld records\n", ilen);
          /* this destroys ilen, but that's ok since we're only gonna
           * extract the file once
           */
          for (; ilen > 0; --ilen) {
            if ((n = ts_read(tsp, passbuf, TARBLKSZ)) < TARBLKSZ) {
              if (n >= 0)
                perror("partial tarfile read");
              else
                ptserror("read tarfile", n, tsp);
              return 2;
            }
            DMSG("read a rec, now at %ld, %ld left\n", curpos, ilen-1);
            ++curpos;
            if ((n = write(outfd, passbuf, TARBLKSZ)) < TARBLKSZ) {
              perror((n > 0) ? "partial tarfile write" : "write tarfile");
              return 2;
            }
            DMSG("wrote rec\n");
          }
        } /* if extract */
      } /* if gotheader */
      
      /* move the line out of the memory buffer, adjust variables */
      /* this will move the null we put at the end of the buffer too */
      memmove(linebuf, nlpos+1, rdoff - linelen + 1);
      linebufavail += linelen;
      rdoff -= linelen;
    }
    
    /* if, after processing any lines, we don't have much space left in the
     * buffer, we increase it by a block
     */
    if (linebufavail < TARBLKSZ) {
      linebuf = realloc(linebuf, linebufsz + TARBLKSZ);
      linebufsz += TARBLKSZ;
      linebufavail += TARBLKSZ;
    }
  }
  return 0;
}