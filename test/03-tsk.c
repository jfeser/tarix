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

int main (int argc, char **argv) {
  int ofd, ifd, rv;
  t_streamp tsp;
  Bytef buf[1024];
  
  if ((ifd = open(IFILE, O_RDONLY)) < 0) {
    perror("open input");
    return 1;
  }
  if ((ofd = open(OFILE, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
    perror("open output");
    return 1;
  }
  
  tsp = init_trs(NULL, ifd, 0, 0, 3);
  if (tsp->zlib_err != Z_OK) {
    printf("zlib init error: %d\n", tsp->zlib_err);
    return 1;
  }
  
  /* 30 is the observed checkpoint address */
  rv = ts_seek(tsp, 0x30);
  if (rv < 0) {
    ptserror("ts_seek", rv, tsp);
    return 1;
  }
  
  rv = ts_read(tsp, buf, 1024);
  if (rv < 0) {
    ptserror("ts_read", rv, tsp);
    return 1;
  } else {
    rv = write(ofd, buf, rv);
    if (rv < 0) {
      perror("write");
      return 1;
    }
  }
  
  rv = ts_close(tsp, 1);
  if (rv == 0)
    printf("OK\n");
  else {
    ptserror("ts_close", rv, tsp);
    return 1;
  }
  
  return 0;
}
