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

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tar.h"
#include "tarix.h"
#include "portability.h"

enum blocks_type {
	BT_FILEDATA,
	BT_LONGNAME,
	BT_LONGLINK
};

int create_index(const char *indexfile, const char *tarfile,
		int pass_through, int zlib_level) {
	const char *headerstring;
	if (zlib_level > 0)
		headerstring = "TARIX INDEX v" TARIX_FMT_VERSION_NEW
			" GENERATED BY tarix-" TARIX_VERSION "\n";
	else
		headerstring = "TARIX INDEX v" TARIX_FMT_VERSION_OLD
			" GENERATED BY tarix-" TARIX_VERSION "\n";
	const int headerlen = strlen(headerstring);
	union tar_block inbuf;
	char *fullfname;
	int fullfname_sz;
	int index, tar;
	FILE *indexf;
	int tmp;
	unsigned long blocknum = 0;
	unsigned long filestart = 0;
	unsigned long blocks_left = 0;
	int blocks_left_type = BT_FILEDATA;
	unsigned long long size_tmp;
	int blockallnull = 0;
	
	/* prep, open output, etc. */
	if ((index = open(indexfile, O_CREAT|O_TRUNC|O_WRONLY, 0666)) < 0) {
		perror("open indexfile");
		return 1;
	}
	if ((indexf = fdopen(index, "w")) == NULL) {
		perror("fdopen index");
		return 1;
	}
	if (tarfile == NULL)
		tar = 0;
	else {
		if ((tar = open(tarfile, O_RDONLY|P_O_LARGEFILE)) < 0) {
			perror("open tarfile");
			return 1;
		}
	}

	if ((tmp = fprintf(indexf, "%s", headerstring)) < headerlen) {
		perror((tmp >= 0) ? "partial header write" : "write header");
		return 1;
	}
	
	/* pre-allocate a reasonable filename size */
	fullfname = (char*)malloc(TARBLKSZ);
	fullfname_sz = TARBLKSZ;
	fullfname[0] = 0; /* null terminate to start */
	
	/* read tar blocks */
	while ((tmp = read(tar, inbuf.buffer, TARBLKSZ)) == TARBLKSZ) {
		/* check to see if the block is all zeros, in which case it will
		 * be skipped & ignored later on */
		blockallnull = 1;
		for (tmp = 0; tmp < TARBLKSZ; ++tmp) {
			if (inbuf.buffer[tmp] > 0) {
				blockallnull = 0;
				break;
			}
		}
		if (blocks_left > 0) {
			switch(blocks_left_type) {
				case BT_LONGNAME:
					if (fullfname_sz - strlen(fullfname) - 1 < TARBLKSZ) {
						fullfname_sz += TARBLKSZ;
						fullfname = realloc(fullfname, fullfname_sz);
					}
					strncat(fullfname, inbuf.buffer, TARBLKSZ);
					break;
				case BT_LONGLINK:
				case BT_FILEDATA:
					/* don't do anything with these currently */
					break;
			}
			--blocks_left;
		}
		/* ignore totally null blocks that come when we're not in the middle
		 * of anything */
		else if (!blockallnull) {
			/* do any cleanup after the old blocks_left is done, special casing
			 * in case this is the first pass through */
			if (blocks_left_type == BT_FILEDATA) {
				filestart = blocknum;
				fullfname[0] = 0; /* clear file name for new one */
			}
			/* compute data size from header block (octal string) */
			size_tmp = strtoull(inbuf.header.size, NULL, 8);
			blocks_left = size_tmp / 512;
			if (size_tmp % 512 > 0) /* get the extra partial block */
				++blocks_left;
			switch(inbuf.header.typeflag) {
				case 'K':
					blocks_left_type = BT_LONGLINK;
					break;
				case 'L':
					blocks_left_type = BT_LONGNAME;
					break;
				default:
					/* anything else we treat as filedata, which triggers writing
					 * a record, but whose data is ignored */
					blocks_left_type = BT_FILEDATA;
					/* write out the index record */
					/* get the file name from the file record if there wasn't a 
					 * long name record previously */
					if (fullfname[0] == 0) {
						/* get filename from tar record */
						if (strncmp(inbuf.header.magic, OLDGNU_MAGIC, OLDGNU_MAGLEN) == 0)
							/* GNU archive */
							strcpy(fullfname, inbuf.header.name);
						else /* assume POSIX archive (is this good?) */
							strcat(strcpy(fullfname, inbuf.header.prefix),
								inbuf.header.name);
					}
					fprintf(indexf, "%ld %ld %s\n", filestart,
						blocknum - filestart + 1 + blocks_left, fullfname);
						/* pre-header         hdr     data */
					break;
			}
		}
		
		if (pass_through) {
			if ((tmp = write(1, inbuf.buffer, TARBLKSZ)) < TARBLKSZ) {
				perror((tmp >= 0) ? "partial block write" : "write block");
				return 2;
			}
		}
		++blocknum;
	}
	
	if (tmp > 0) {
		perror("didn't get enough bytes on read");
		return 2;
	}
	
	return 0;
}

