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

    mpeg2dec->chunk_buffer = xmalloc_aligned (16, 224 * 1024 + 4);
    mpeg2dec->picture = xmalloc_aligned (16, sizeof (picture_t));

    mpeg2dec->shift = 0;
    mpeg2dec->is_sequence_needed = 1;
    mpeg2dec->frames_to_drop = 0;
    mpeg2dec->skip_slices = 0;
    mpeg2dec->in_slice = 0;
    mpeg2dec->chunk_ptr = mpeg2dec->chunk_buffer;
    mpeg2dec->code = 0xff;
    mpeg2dec->output = output;

    memset (mpeg2dec->picture, 0, sizeof (picture_t));

    /* initialize supstructures */
    header_state_init (mpeg2dec->picture);

    output->open (output);
}

void decode_free_image_buffers (mpeg2dec_t * mpeg2dec) {

    picture_t *picture = mpeg2dec->picture;

    if (picture->forward_reference_frame) {
      picture->forward_reference_frame->free (picture->forward_reference_frame);
      picture->forward_reference_frame = NULL;
    }

    if (picture->backward_reference_frame) {
      picture->backward_reference_frame->free (picture->backward_reference_frame);
      picture->backward_reference_frame = NULL;
    }

    if (picture->throwaway_frame) {
      picture->throwaway_frame->free (picture->throwaway_frame);
      picture->throwaway_frame = NULL;
    }
}

static void decode_reorder_frames (mpeg2dec_t * mpeg2dec)
{
    picture_t *picture = mpeg2dec->picture;

    if (picture->picture_coding_type != B_TYPE) {

        if (picture->forward_reference_frame)
          picture->forward_reference_frame->free (picture->forward_reference_frame);

        /*
         * make the backward reference frame the new forward reference frame
         */

        picture->forward_reference_frame = picture->backward_reference_frame;

        /*
         * allocate new backward reference frame
         */


        picture->backward_reference_frame = mpeg2dec->output->get_frame (mpeg2dec->output,
									 picture->coded_picture_width,
									 picture->coded_picture_height, 
									 picture->aspect_ratio_information,
									 IMGFMT_YV12, 
									 picture->frame_duration);;
        picture->backward_reference_frame->PTS       = 0;
        /*picture->backward_reference_frame->bFrameBad = 1; */

	
	if (!picture->forward_reference_frame) {

	  picture->forward_reference_frame = mpeg2dec->output->get_frame (mpeg2dec->output,
									  picture->coded_picture_width,
									  picture->coded_picture_height, 
									  picture->aspect_ratio_information,
									  IMGFMT_YV12, 
									  picture->frame_duration);;
	  picture->forward_reference_frame->PTS       = 0;
	  /*picture->forward_reference_frame->bFrameBad = 1; */
	}
	

        /*
         * make it the current frame
         */

        picture->current_frame = picture->backward_reference_frame;

    } else {

        /*
         * allocate new throwaway frame
         */

        picture->throwaway_frame = mpeg2dec->output->get_frame (mpeg2dec->output,
								picture->coded_picture_width,
								picture->coded_picture_height, 
								picture->aspect_ratio_information,
								IMGFMT_YV12, 
								picture->frame_duration);;
        picture->throwaway_frame->PTS       = 0;
        /*picture->throwaway_frame->bFrameBad = 1; */

        /*
         * make it the current frame
         */

        picture->current_frame = picture->throwaway_frame;
    }
}



static int parse_chunk (mpeg2dec_t * mpeg2dec, int code, uint8_t * buffer, uint32_t pts)
{
    picture_t * picture;
    int is_frame_done;
    int bFlipPage;

    /* wait for sequence_header_code */
    if (mpeg2dec->is_sequence_needed && (code != 0xb3))
	return 0;

    stats_header (code, buffer);

    picture = mpeg2dec->picture;
    is_frame_done = mpeg2dec->in_slice && ((!code) || (code >= 0xb0));

    if (is_frame_done) {

	mpeg2dec->in_slice = 0;

	if ((picture->picture_structure == FRAME_PICTURE) ||
	    (picture->second_field)) {
	  if (picture->picture_coding_type == B_TYPE) {
	    picture->throwaway_frame->bFrameBad = !mpeg2dec->drop_frame;
	    picture->throwaway_frame->draw (picture->throwaway_frame);
            picture->throwaway_frame->free (picture->throwaway_frame);
 	  } else {
	    picture->forward_reference_frame->bFrameBad = !mpeg2dec->drop_frame;
	    picture->forward_reference_frame->draw (picture->forward_reference_frame);
	  }
	  bFlipPage = 1;
	} else
	  bFlipPage = 0;
    }

    switch (code) {
    case 0x00:	/* picture_start_code */
	if (header_process_picture_header (picture, buffer)) {
	    fprintf (stderr, "bad picture header\n");
	    exit (1);
	}

	if (bFlipPage)
	  decode_reorder_frames (mpeg2dec);
	
	if (mpeg2dec->pts) {
	  picture->current_frame->PTS = mpeg2dec->pts;
	  mpeg2dec->pts = 0;
	}


	/*
	 * find out if we want to skip this frame
	 */
	
	mpeg2dec->drop_frame = 0;
        switch (picture->picture_coding_type) {
        case B_TYPE:
	  
          if (mpeg2dec->frames_to_drop>0) {
            mpeg2dec->drop_frame = 1;
	    mpeg2dec->frames_to_drop--;
          } else if (!picture->forward_reference_frame
		     || !picture->backward_reference_frame
		     || picture->forward_reference_frame->bFrameBad 
		     || picture->backward_reference_frame->bFrameBad) {
            mpeg2dec->drop_frame = 1;
	    mpeg2dec->frames_to_drop--;
          }
	  
          break;
        case P_TYPE:
	  
          if (mpeg2dec->frames_to_drop>2) {
            mpeg2dec->drop_frame = 1;
	    mpeg2dec->frames_to_drop--;
          } else if (!picture->forward_reference_frame
		     || picture->forward_reference_frame->bFrameBad) {
            mpeg2dec->drop_frame = 1;
	    mpeg2dec->frames_to_drop--;
          }
	  
          break;
        case I_TYPE:
	  
          if (mpeg2dec->frames_to_drop>4) {
            mpeg2dec->drop_frame = 1;
	    mpeg2dec->frames_to_drop--;
          }
	  
          break;
        }

	break;

    case 0xb3:	/* sequence_header_code */
	if (header_process_sequence_header (picture, buffer)) {
	    fprintf (stderr, "bad sequence header\n");
	    exit (1);
	}
	if (mpeg2dec->is_sequence_needed) 
	    mpeg2dec->is_sequence_needed = 0;

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
		picture->current_frame->field (picture->current_frame, 
					       picture->picture_structure);
	}

	if (!mpeg2dec->drop_frame) {
	    slice_process (picture, code, buffer);
	}
    }

    return is_frame_done;
}

int mpeg2_decode_data (mpeg2dec_t * mpeg2dec, uint8_t * current, uint8_t * end,
		       uint32_t pts)
{
    uint32_t shift;
    uint8_t * chunk_ptr;
    uint8_t byte;
    int ret = 0;

    shift = mpeg2dec->shift;
    chunk_ptr = mpeg2dec->chunk_ptr;
    mpeg2dec->pts = pts;

    printf ("mpeg2dec: decode_data...\n");

    while (current != end) {
	while (1) {
	    byte = *current++;
	    if (shift != 0x00000100) {
		*chunk_ptr++ = byte;
		shift = (shift | byte) << 8;
		if (current != end)
		    continue;
		mpeg2dec->chunk_ptr = chunk_ptr;
		mpeg2dec->shift = shift;
#ifdef ARCH_X86
		if (config.flags & MM_ACCEL_X86_MMX)
                    emms();
#endif

		return ret;
	    }
	    break;
	}

	/* found start_code following chunk */

	ret += parse_chunk (mpeg2dec, mpeg2dec->code, mpeg2dec->chunk_buffer, pts);

	/* done with header or slice, prepare for next one */

	mpeg2dec->code = byte;
	chunk_ptr = mpeg2dec->chunk_buffer;
	shift = 0xffffff00;
    }

    printf ("mpeg2dec: decode_data finished\n");

    mpeg2dec->chunk_ptr = chunk_ptr;
    mpeg2dec->shift = shift;
#ifdef ARCH_X86
    if (config.flags & MM_ACCEL_X86_MMX)
     emms();
#endif
    return ret;
}

void mpeg2_close (mpeg2dec_t * mpeg2dec)
{
    static uint8_t finalizer[] = {0,0,1,0};

    mpeg2_decode_data (mpeg2dec, finalizer, finalizer+4, 0);

    if (! (mpeg2dec->is_sequence_needed))
	mpeg2dec->picture->backward_reference_frame->draw (mpeg2dec->picture->backward_reference_frame);

    mpeg2dec->output->close (mpeg2dec->output);

    free (mpeg2dec->chunk_buffer);
    free (mpeg2dec->picture);

}

void mpeg2_skip_frames (mpeg2dec_t * mpeg2dec, int num_frames)
{
    mpeg2dec->frames_to_drop = num_frames;
}


