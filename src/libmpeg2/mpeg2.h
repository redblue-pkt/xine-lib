/*
 * mpeg2.h
 * Copyright (C) 1999-2001 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* Structure for the mpeg2dec decoder */

typedef struct mpeg2dec_s {
    vo_instance_t * output;

    /* this is where we keep the state of the decoder */
    struct picture_s * picture;
    
    uint32_t shift;
    int is_sequence_needed;
    int frames_to_drop, drop_frame;
    int in_slice;
    int seek_mode, is_frame_needed;

    /* the maximum chunk size is determined by vbv_buffer_size */
    /* which is 224K for MP@ML streams. */
    /* (we make no pretenses of decoding anything more than that) */
    /* allocated in init - gcc has problems allocating such big structures */
    uint8_t * chunk_buffer;
    /* pointer to current position in chunk_buffer */
    uint8_t * chunk_ptr;
    /* last start code ? */
    uint8_t code;

    uint32_t pts, scr;
    uint32_t last_repeat_first_field; 
    xine_t *xine;
} mpeg2dec_t ;


/* initialize mpegdec with a opaque user pointer */
void mpeg2_init (mpeg2dec_t * mpeg2dec, 
		 vo_instance_t * output);

/* destroy everything which was allocated, shutdown the output */
void mpeg2_close (mpeg2dec_t * mpeg2dec);

int mpeg2_decode_data (mpeg2dec_t * mpeg2dec,
		       uint8_t * data_start, uint8_t * data_end, 
		       uint32_t pts, uint32_t scr);

void mpeg2_find_sequence_header (mpeg2dec_t * mpeg2dec,
				 uint8_t * data_start, uint8_t * data_end);

void mpeg2_flush (mpeg2dec_t * mpeg2dec);

void process_userdata(mpeg2dec_t *mpeg2dec, uint8_t *buffer);
