/* 
 * Copyright (C) 2000-2001 plitsch-platsch
 * 
 * xine_dvd_plugin is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * xine_dvd_plugin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 */

#ifndef DVD_UDF_H
#define DVD_UDF_H

#include <inttypes.h>

#define DVD_UDF_VERSION 19991115

/*
 * The length of one Logical Block of a DVD Video                             
 */

#define DVD_VIDEO_LB_LEN 2048

int UDFReadLB (int fd, off_t lb_number, size_t block_count, uint8_t *data);

off_t UDFFindFile(int fd, char *filename, off_t *size);

void UDFListDir(int fd, char *dirname, int nMaxFiles, char **file_list, int *nFiles) ;

#endif /* DVD_UDF_H */
