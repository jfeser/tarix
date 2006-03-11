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

/* test case for simple tar write stream */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>

#include "tstream.h"

#define IFILE "bin/test/data.gz"
#define OFILE "bin/test/data"

void ptserr(const char *msg, off64_t rv, t_streamp tsp) {
  if (rv == TS_ERR_ZLIB) {
    printf("zlib inflate error: %d\n  %s\n", tsp->zlib_err, tsp->zsp->msg);
  } else if (rv == TS_ERR_BADMODE) {
    printf("invalid mode\n");
  } else if (rv == -1) {
    perror("zlib read");
  } else {
    printf("unknown error: %lld\n", rv);
  }
}

int main (int argc, char **argv) {
  int ofd, ifd;
  if ((ifd = open(IFILE, O_RDONLY)) < 0) {
    perror("open input");
    return 1;
  }
  if ((ofd = open(OFILE, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
    perror("open output");
    return 1;
  }
  
  t_streamp tsp = init_trs(NULL, ifd, 3);
  if (tsp->zlib_err != Z_OK) {
    printf("zlib init error: %d\n", tsp->zlib_err);
    return 1;
  }
  
  int rv;
  
  Bytef buf[1024];
  
  rv = ts_read(tsp, buf, 1024);
  if (rv < 0) {
    ptserr("ts_read", rv, tsp);
    return 1;
  } else {
    rv = write(ofd, buf, rv);
    if (rv < 0) {
      perror("write");
      return 1;
    }
  }
  
  rv = ts_close(tsp, 1);
  if (rv == 0) {
    /* do nothing */
  } else if (rv == -1) {
    perror("ts_close");
    return 1;
  } else if (rv == TS_ERR_ZLIB) {
    printf("zlib end error\n");
    return 1;
  } else if (rv == TS_ERR_BADMODE) {
    printf("bad mode error\n");
    return 1;
  } else {
    printf("unknown return from ts_close: %d\n", rv);
  }
  
  return 0;
}