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
 * $Id: decoder.h,v 1.1 2003/08/05 11:30:56 jcdutton Exp $
 *
 * 04-08-2003 DTS software decode (C) James Courtier-Dutton
 *
 */

#ifndef DTS_DECODER_H
#define DTS_DECODER_H 1

typedef struct {
  audio_decoder_t  audio_decoder;

  xine_stream_t    *stream;
  audio_decoder_class_t *class;

  uint32_t         rate;
  uint32_t         bits_per_sample;
  uint32_t         number_of_channels;

  int              output_open;
} dts_decoder_t;

void dts_parse_data (dts_decoder_t *this, buf_element_t *buf);

#endif /* DTS_DECODER_H */
