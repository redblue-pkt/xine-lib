/*
    $Id: directory.h,v 1.2 2004/04/11 12:20:32 miguelfreitas Exp $

    Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _DIRECTORY_H_
#define _DIRECTORY_H_

#include <libvcd/types.h>

/* Private headers */
#include "data_structures.h"

/* opaque data structure representing the ISO directory tree */
typedef VcdTree VcdDirectory;

VcdDirectory *
_vcd_directory_new (void);

void
_vcd_directory_destroy (VcdDirectory *dir);

int
_vcd_directory_mkdir (VcdDirectory *dir, const char pathname[]);

int
_vcd_directory_mkfile (VcdDirectory *dir, const char pathname[], 
                       uint32_t start, uint32_t size,
                       bool form2_flag, uint8_t filenum);

uint32_t
_vcd_directory_get_size (VcdDirectory *dir);

void 
_vcd_directory_dump_entries (VcdDirectory *dir, void *buf, uint32_t extent);

void
_vcd_directory_dump_pathtables (VcdDirectory *dir, void *ptl, void *ptm);

#endif /* _DIRECTORY_H_ */


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
