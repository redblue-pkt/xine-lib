/*
 * decode.c
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
 *
 * xine-specific version by G. Bartsch
 *
 */

#include "config.h"

#include <stdio.h>
#include <string.h>	/* memcpy/memset, try to remove */
#include <stdlib.h>
#include <inttypes.h>

#include "video_out.h"
#include "mpeg2.h"
#include "mpeg2_internal.h"
#include "cpu_accel.h"
#include "utils.h"

#define BUFFER_SIZE (224 * 1024)

mpeg2_config_t config;

void mpeg2_init (mpeg2dec_t * mpeg2dec, 
		 vo_instance_t * output)
{
    static int do_init = 1;

    if (do_init) {
	do_init = 0;
	config.flags = mm_accel();
	idct_init ();
	motion_comp_init ();
    }

    mpeg2dec->chunk_buffer = xmalloc_aligned (16, BUFFER_SIZE + 4);
    mpeg2dec->picture = xmalloc_aligned (16, sizeof (picture_t));

    mpeg2dec->shift = 0xffffff00;
    mpeg2dec->is_sequence_needed = 1;
    mpeg2dec->frames_to_drop = 0;
    mpeg2dec->drop_frame = 0;
    mpeg2dec->in_slice = 0;
    mpeg2dec->output = output;
    mpeg2dec->chunk_ptr = mpeg2dec->chunk_buffer;
    mpeg2dec->code = 0xb4;

    memset (mpeg2dec->picture, 0, sizeof (picture_t));

    /* initialize supstructures */
    header_state_init (mpeg2dec->picture);
}

static inline int parse_chunk (mpeg2dec_t * mpeg2dec, int code,
			       uint8_t * buffer)
{
    picture_t * picture;
    int is_frame_done;

    /* wait for sequence_header_code */
    if (mpeg2dec->is_sequence_needed && (code != 0xb3))
	return 0;

    stats_header (code, buffer);

    picture = mpeg2dec->picture;
    is_frame_done = mpeg2dec->in_slice && ((!code) || (code >= 0xb0));

    if (is_frame_done) {
	mpeg2dec->in_slice = 0;

	if (((picture->picture_structure == FRAME_PICTURE) ||
	     (picture->second_field)) ) {

	    picture->current_frame->bFrameBad = mpeg2dec->drop_frame;

	    if (picture->picture_coding_type == B_TYPE) {
	      mpeg2dec->frames_to_drop = picture->current_frame->draw (picture->current_frame);
	      picture->current_frame->free (picture->current_frame);
	      picture->current_frame = NULL;
	      picture->throwaway_frame = NULL;
	    } else {
	      mpeg2dec->frames_to_drop = picture->forward_reference_frame->draw (picture->forward_reference_frame);
	    }

#ifdef ARCH_X86
	    if (config.flags & MM_ACCEL_X86_MMX)
		emms ();
#endif
	}
    }

    switch (code) {
    case 0x00:	/* picture_start_code */
	if (header_process_picture_header (picture, buffer)) {
	    fprintf (stderr, "bad picture header\n");
	    exit (1);
	}

	/* find out if we want to skip this frame */
	mpeg2dec->drop_frame = 0;

	picture->skip_non_intra_dct = (mpeg2dec->frames_to_drop>0) ;

	    
	switch (picture->picture_coding_type) {
        case B_TYPE:
          
          if (mpeg2dec->frames_to_drop>1) {
            mpeg2dec->drop_frame = 1;
          } else if (picture->forward_reference_frame->bFrameBad 
                     || picture->backward_reference_frame->bFrameBad) {
            mpeg2dec->drop_frame = 1;
          }
	  break;

        case P_TYPE:
          
          if (mpeg2dec->frames_to_drop>2) {
            mpeg2dec->drop_frame = 1;
          } else if (picture->backward_reference_frame->bFrameBad) {
            mpeg2dec->drop_frame = 1;
	  }
	  break;

        case I_TYPE:
          
          if (mpeg2dec->frames_to_drop>4) {
            mpeg2dec->drop_frame = 1;
	  }
	  break;
	}

	break;

    case 0xb3:	/* sequence_header_code */
	if (header_process_sequence_header (picture, buffer)) {
	    fprintf (stderr, "bad sequence header\n");
	    exit (1);
	}
	if (mpeg2dec->is_sequence_needed) {
	    mpeg2dec->is_sequence_needed = 0;
	    picture->forward_reference_frame =
	        mpeg2dec->output->get_frame (mpeg2dec->output,
					     picture->coded_picture_width,
					     picture->coded_picture_height,
					     picture->aspect_ratio_information,
					     IMGFMT_YV12,
					     picture->frame_duration);
	    picture->forward_reference_frame->PTS = 0;
	    picture->backward_reference_frame =
  	        mpeg2dec->output->get_frame (mpeg2dec->output,
					     picture->coded_picture_width,
					     picture->coded_picture_height,
					     picture->aspect_ratio_information,
					     IMGFMT_YV12,
					     picture->frame_duration);
	    picture->backward_reference_frame->PTS = 0;
	    
	}
	break;

    case 0xb5:	/* extension_start_code */
	if (header_process_extension (picture, buffer)) {
	    fprintf (stderr, "bad extension\n");
	    exit (1);
	}
	break;

    default:
	if (code >= 0xb9)
	    fprintf (stderr, "stream not demultiplexed ?\n");

	if (code >= 0xb0)
	    break;

	if (!(mpeg2dec->in_slice)) {
	    mpeg2dec->in_slice = 1;

	    if (picture->second_field)
	      picture->current_frame->field(picture->current_frame, 
					    picture->picture_structure);
	    else {
		if (picture->picture_coding_type == B_TYPE)
		    picture->throwaway_frame = picture->current_frame =
		        mpeg2dec->output->get_frame (mpeg2dec->output,
						     picture->coded_picture_width,
						     picture->coded_picture_height,
						     picture->aspect_ratio_information,
						     IMGFMT_YV12,
						     picture->frame_duration);
		else {
		    picture->current_frame =
		        mpeg2dec->output->get_frame (mpeg2dec->output,
						     picture->coded_picture_width,
						     picture->coded_picture_height,
						     picture->aspect_ratio_information,
						     IMGFMT_YV12,
						     picture->frame_duration);
		    picture->forward_reference_frame->free (picture->forward_reference_frame);

		    picture->forward_reference_frame =
			picture->backward_reference_frame;
		    picture->backward_reference_frame = picture->current_frame;
		}
		picture->current_frame->PTS = mpeg2dec->pts;
		mpeg2dec->pts = 0;
	    }
	}

	if (!(mpeg2dec->drop_frame)) {
	    slice_process (picture, code, buffer);

#ifdef ARCH_X86
	    if (config.flags & MM_ACCEL_X86_MMX)
		emms ();
#endif
	}
    }

    return is_frame_done;
}

static inline uint8_t * copy_chunk (mpeg2dec_t * mpeg2dec,
				    uint8_t * current, uint8_t * end)
{
    uint32_t shift;
    uint8_t * chunk_ptr;
    uint8_t * limit;
    uint8_t byte;

    shift = mpeg2dec->shift;
    chunk_ptr = mpeg2dec->chunk_ptr;
    limit = current + (mpeg2dec->chunk_buffer + BUFFER_SIZE - chunk_ptr);
    if (limit > end)
	limit = end;

    while (1) {
	byte = *current++;
	if (shift != 0x00000100) {
	    shift = (shift | byte) << 8;
	    *chunk_ptr++ = byte;
	    if (current < limit)
		continue;
	    if (current == end) {
		mpeg2dec->chunk_ptr = chunk_ptr;
		mpeg2dec->shift = shift;
		return NULL;
	    } else {
		/* we filled the chunk buffer without finding a start code */
		mpeg2dec->code = 0xb4;	/* sequence_error_code */
		mpeg2dec->chunk_ptr = mpeg2dec->chunk_buffer;
		return current;
	    }
	}
	mpeg2dec->code = byte;
	mpeg2dec->chunk_ptr = mpeg2dec->chunk_buffer;
	mpeg2dec->shift = 0xffffff00;
	return current;
    }
}

int mpeg2_decode_data (mpeg2dec_t * mpeg2dec, uint8_t * current, uint8_t * end,
		       uint32_t pts)
{
    int ret;
    uint8_t code;

    ret = 0;
    mpeg2dec->pts = pts;

    while (current != end) {
	code = mpeg2dec->code;
	current = copy_chunk (mpeg2dec, current, end);
	if (current == NULL)
	    return ret;
	ret += parse_chunk (mpeg2dec, code, mpeg2dec->chunk_buffer);
    }
    return ret;
}

void mpeg2_close (mpeg2dec_t * mpeg2dec)
{
    static uint8_t finalizer[] = {0,0,1,0xb4};
    picture_t *picture = mpeg2dec->picture;

    mpeg2_decode_data (mpeg2dec, finalizer, finalizer+4, 0);

    if (picture->throwaway_frame) 
      picture->throwaway_frame->free (picture->throwaway_frame);

    if (picture->forward_reference_frame) 
      picture->forward_reference_frame->free (picture->forward_reference_frame);

    if (picture->backward_reference_frame) 
      picture->backward_reference_frame->free (picture->backward_reference_frame);

    /* FIXME
    free (mpeg2dec->chunk_buffer);
    free (mpeg2dec->picture);
    */
}

void mpeg2_find_sequence_header (mpeg2dec_t * mpeg2dec,
				 uint8_t * current, uint8_t * end){

    uint8_t code;
    picture_t *picture = mpeg2dec->picture;

    while (current != end) {
	code = mpeg2dec->code;
	current = copy_chunk (mpeg2dec, current, end);
	if (current == NULL)
	    return ;

	if (code == 0xb3) {	/* sequence_header_code */
	    if (header_process_sequence_header (picture, mpeg2dec->chunk_buffer)) {
	        fprintf (stderr, "bad sequence header\n");
		return;
	    }
	  
	    if (mpeg2dec->is_sequence_needed) {
	        mpeg2dec->is_sequence_needed = 0;
		picture->forward_reference_frame =
		    mpeg2dec->output->get_frame (mpeg2dec->output,
						 picture->coded_picture_width,
						 picture->coded_picture_height,
						 picture->aspect_ratio_information,
						 IMGFMT_YV12,
						 picture->frame_duration);
		picture->forward_reference_frame->PTS = 0;
		picture->backward_reference_frame =
		    mpeg2dec->output->get_frame (mpeg2dec->output,
						 picture->coded_picture_width,
						 picture->coded_picture_height,
						 picture->aspect_ratio_information,
						 IMGFMT_YV12,
						 picture->frame_duration);
		picture->backward_reference_frame->PTS = 0;
	    
	    }
	}
    }
}

