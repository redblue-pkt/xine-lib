/* 
 * Copyright (C) 2000-2002 the xine project
 * 
 * This file is part of xine, a free video player.
 * 
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: dxr3.h,v 1.2 2002/05/24 22:09:44 miguelfreitas Exp $
 */

#ifndef HAVE_DXR3_H
#define HAVE_DXR3_H

#include <linux/em8300.h>

/* data for the device name config entry */
#define CONF_LOOKUP "dxr3.devicename"
#define CONF_DEFAULT "/dev/em8300-0"
#define CONF_NAME "Dxr3: Device Name"
#define CONF_HELP "The device file of the dxr3 mpeg decoder card control device."

/* image format used by dxr3_decoder to tag undecoded mpeg data */
#define IMGFMT_MPEG (('G'<<24)|('E'<<16)|('P'<<8)|'M')

#endif
