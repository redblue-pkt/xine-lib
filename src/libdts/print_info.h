/*
 * Copyright (C) 2000-2003 the xine project
 *
 * This file is part of xine, a unix video player.
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
 * $Id: print_info.h,v 1.1 2003/08/05 11:30:56 jcdutton Exp $
 *
 * 04-08-2003 DTS software decode (C) James Courtier-Dutton
 *
 */

#ifndef DTS_PRINT_INFO_H
#define DTS_PRINT_INFO_H 1

#include "decoder_internal.h"

void dts_print_decoded_data(decoder_data_t *decode_data);

#endif /* DTS_PRINT_INFO_H */
