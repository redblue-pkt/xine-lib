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
 * FLI Video Decoder by Mike Melanson (melanson@pcisys.net) and
 * Roberto Togni <rtogni@bresciaonline.it>
 * For more information on the FLI format, as well as various traps to
 * avoid when implementing a FLI decoder, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 * 
 * $Id: fli.c,v 1.3 2002/08/28 03:37:17 tmmm Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "video_out.h"
#include "buffer.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "bswap.h"

#define VIDEOBUFSIZE 128*1024

#define PALETTE_SIZE (256 * 4)

#define LE_16(x) (le2me_16(*(uint16_t *)(x)))
#define LE_32(x) (le2me_32(*(uint32_t *)(x)))

#define FLI_256_COLOR 4
#define FLI_DELTA     7
#define FLI_COLOR     11
#define FLI_LC        12
#define FLI_BLACK     13
#define FLI_BRUN      15
#define FLI_COPY      16
#define FLI_MINI      18

/**************************************************************************
 * fli specific decode functions
 *************************************************************************/

typedef struct fli_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  /* these are traditional variables in a video decoder object */
  vo_instance_t    *video_out;   /* object that will receive frames */
  uint64_t          video_step;  /* frame duration in pts units */
  int               decoder_ok;  /* current decoder status */
  int               skipframes;

  unsigned char    *buf;         /* the accumulated buffer data */
  int               bufsize;     /* the maximum size of buf */
  int               size;        /* the current size of buf */

  int               width;       /* the width of a video frame */
  int               height;      /* the height of a video frame */

  /* FLI decoding parameters */
  unsigned char     yuv_palette[PALETTE_SIZE];
  yuv_planes_t      yuv_planes;
  unsigned char    *ghost_image;
  
} fli_decoder_t;

void decode_fli_frame(fli_decoder_t *this) {
  int stream_ptr = 0;
  int stream_ptr_after_color_chunk;
  int pixel_ptr;
  int palette_ptr1;
  int palette_ptr2;
  unsigned char palette_idx1;
  unsigned char palette_idx2;

  unsigned int frame_size;
  int num_chunks;

  unsigned int chunk_size;
  int chunk_type;

  int i, j;

  int color_packets;
  int color_changes;
  int color_shift;
  unsigned r, g, b;

  int lines, x;
  int compressed_lines;
  int starting_line;
  signed short line_packets;
  int y_ptr;
  signed char byte_run;
  int pixel_skip;
  int update_whole_frame = 0;   /* palette change flag */
  int ghost_pixel_ptr;
  int ghost_y_ptr;

  frame_size = LE_32(&this->buf[stream_ptr]);
  stream_ptr += 6;  /* skip the magic number */
  num_chunks = LE_16(&this->buf[stream_ptr]);
  stream_ptr += 10;  /* skip padding */

  /* iterate through the chunks */
  frame_size -= 16;
  while ((frame_size > 0) && (num_chunks > 0)) {
    chunk_size = LE_32(&this->buf[stream_ptr]);
    stream_ptr += 4;
    chunk_type = LE_16(&this->buf[stream_ptr]);
    stream_ptr += 2;

    switch (chunk_type) {
    case FLI_256_COLOR:
    case FLI_COLOR:
      stream_ptr_after_color_chunk = stream_ptr + chunk_size - 6;
      if (chunk_type == FLI_COLOR)
        color_shift = 2;
      else
        color_shift = 0;
      /* set up the palette */
      color_packets = LE_16(&this->buf[stream_ptr]);
      stream_ptr += 2;
      palette_ptr1 = 0;
      for (i = 0; i < color_packets; i++) {
        /* first byte is how many colors to skip */
        palette_ptr1 += (this->buf[stream_ptr++] * 4);
        /* wrap around, for good measure */
        if (palette_ptr1 >= PALETTE_SIZE)
          palette_ptr1 = 0;
        /* next byte indicates how many entries to change */
        color_changes = this->buf[stream_ptr++];
        /* if there are 0 color changes, there are actually 256 */
        if (color_changes == 0)
          color_changes = 256;
        for (j = 0; j < color_changes; j++) {
          r = this->buf[stream_ptr + 0] << color_shift;
          g = this->buf[stream_ptr + 1] << color_shift;
          b = this->buf[stream_ptr + 2] << color_shift;

          this->yuv_palette[palette_ptr1++] = COMPUTE_Y(r,g, b);
          this->yuv_palette[palette_ptr1++] = COMPUTE_U(r,g, b);
          this->yuv_palette[palette_ptr1++] = COMPUTE_V(r,g, b);

          palette_ptr1++;
          stream_ptr += 3;
        }
      }

      /* color chunks sometimes have weird 16-bit alignment issues;
       * therefore, take the hardline approach and set the stream_ptr
       * to the value calculate w.r.t. the size specified by the color
       * chunk header */
      stream_ptr = stream_ptr_after_color_chunk;

      /* palette has changed, must update frame */
      update_whole_frame = 1;
      break;

    case FLI_DELTA:
      y_ptr = ghost_y_ptr = 0;
      compressed_lines = LE_16(&this->buf[stream_ptr]);
      stream_ptr += 2;
      while (compressed_lines > 0) {
        line_packets = LE_16(&this->buf[stream_ptr]);
        stream_ptr += 2;
        if (line_packets < 0) {
          line_packets = -line_packets;
          y_ptr += (line_packets * this->yuv_planes.row_width);
          ghost_y_ptr += (line_packets * this->width);
        } else {
          pixel_ptr = y_ptr;
          ghost_pixel_ptr = ghost_y_ptr;
          for (i = 0; i < line_packets; i++) {
            /* account for the skip bytes */
            pixel_skip = this->buf[stream_ptr++];
            pixel_ptr += pixel_skip;
            ghost_pixel_ptr += pixel_skip;
            byte_run = this->buf[stream_ptr++];
            if (byte_run < 0) {
              byte_run = -byte_run;
              palette_ptr1 = (palette_idx1 = this->buf[stream_ptr++]) * 4;
              palette_ptr2 = (palette_idx2 = this->buf[stream_ptr++]) * 4;
              for (j = 0; j < byte_run; j++) {
                this->ghost_image[ghost_pixel_ptr++] = palette_idx1;
                this->yuv_planes.y[pixel_ptr] = 
                  this->yuv_palette[palette_ptr1 + 0];
                this->yuv_planes.u[pixel_ptr] = 
                  this->yuv_palette[palette_ptr1 + 1];
                this->yuv_planes.v[pixel_ptr] = 
                  this->yuv_palette[palette_ptr1 + 2];
                pixel_ptr++;

                this->ghost_image[ghost_pixel_ptr++] = palette_idx2;
                this->yuv_planes.y[pixel_ptr] = 
                  this->yuv_palette[palette_ptr2 + 0];
                this->yuv_planes.u[pixel_ptr] = 
                  this->yuv_palette[palette_ptr2 + 1];
                this->yuv_planes.v[pixel_ptr] = 
                  this->yuv_palette[palette_ptr2 + 2];
                pixel_ptr++;
              }
            } else {
              for (j = 0; j < byte_run * 2; j++) {
                palette_ptr1 = (palette_idx1 = this->buf[stream_ptr++]) * 4;
                this->ghost_image[ghost_pixel_ptr++] = palette_idx1;
                this->yuv_planes.y[pixel_ptr] = 
                  this->yuv_palette[palette_ptr1 + 0];
                this->yuv_planes.u[pixel_ptr] = 
                  this->yuv_palette[palette_ptr1 + 1];
                this->yuv_planes.v[pixel_ptr] = 
                  this->yuv_palette[palette_ptr1 + 2];
                pixel_ptr++;
              }
            }
          }

          y_ptr += this->yuv_planes.row_width;
          ghost_y_ptr += this->width;
          compressed_lines--;
        }
      }
      break;

    case FLI_LC:
      /* line compressed */
      starting_line = LE_16(&this->buf[stream_ptr]);
      stream_ptr += 2;
      y_ptr = starting_line * this->yuv_planes.row_width;
      ghost_y_ptr = starting_line * this->width;

      compressed_lines = LE_16(&this->buf[stream_ptr]);
      stream_ptr += 2;
      while (compressed_lines > 0) {
        pixel_ptr = y_ptr;
        ghost_pixel_ptr = ghost_y_ptr;
        line_packets = this->buf[stream_ptr++];
        if (line_packets > 0) {
          for (i = 0; i < line_packets; i++) {
            /* account for the skip bytes */
            pixel_skip = this->buf[stream_ptr++];
            pixel_ptr += pixel_skip;
            ghost_pixel_ptr += pixel_skip;
            byte_run = this->buf[stream_ptr++];
            if (byte_run > 0) {
              for (j = 0; j < byte_run; j++) {
                palette_ptr1 = (palette_idx1 = this->buf[stream_ptr++]) * 4;
                this->ghost_image[ghost_pixel_ptr++] = palette_idx1;
                this->yuv_planes.y[pixel_ptr] = 
                  this->yuv_palette[palette_ptr1 + 0];
                this->yuv_planes.u[pixel_ptr] = 
                  this->yuv_palette[palette_ptr1 + 1];
                this->yuv_planes.v[pixel_ptr] = 
                  this->yuv_palette[palette_ptr1 + 2];
                pixel_ptr++;
              }
            } else {
              byte_run = -byte_run;
              palette_ptr1 = (palette_idx1 = this->buf[stream_ptr++]) * 4;
              for (j = 0; j < byte_run; j++) {
                this->ghost_image[ghost_pixel_ptr++] = palette_idx1;
                this->yuv_planes.y[pixel_ptr] = 
                  this->yuv_palette[palette_ptr1 + 0];
                this->yuv_planes.u[pixel_ptr] = 
                  this->yuv_palette[palette_ptr1 + 1];
                this->yuv_planes.v[pixel_ptr] = 
                  this->yuv_palette[palette_ptr1 + 2];
                pixel_ptr++;
              }
            }
          }
        }

        y_ptr += this->yuv_planes.row_width;
        ghost_y_ptr += this->width;
        compressed_lines--;
      }
      break;

    case FLI_BLACK:
      /* set the whole frame to color 0 (which is usually black) by
       * clearing the ghost image and trigger a full frame update */
      memset(this->ghost_image, 0, 
        this->width * this->height * sizeof(unsigned char));
      update_whole_frame = 1;
      break;

    case FLI_BRUN:
      /* byte run compression */
      y_ptr = 0;
      ghost_y_ptr = 0;
      for (lines = 0; lines < this->height; lines++) {
        pixel_ptr = y_ptr;
        ghost_pixel_ptr = ghost_y_ptr;
        line_packets = this->buf[stream_ptr++];
        for (i = 0; i < line_packets; i++) {
          byte_run = this->buf[stream_ptr++];
          if (byte_run > 0) {
            palette_ptr1 = (palette_idx1 = this->buf[stream_ptr++]) * 4;
            for (j = 0; j < byte_run; j++) {
              this->ghost_image[ghost_pixel_ptr++] = palette_idx1;
              this->yuv_planes.y[pixel_ptr] = 
                this->yuv_palette[palette_ptr1 + 0];
              this->yuv_planes.u[pixel_ptr] = 
                this->yuv_palette[palette_ptr1 + 1];
              this->yuv_planes.v[pixel_ptr] = 
                this->yuv_palette[palette_ptr1 + 2];
              pixel_ptr++;
            }
          } else {  /* copy bytes if byte_run < 0 */
            byte_run = -byte_run;
            for (j = 0; j < byte_run; j++) {
              palette_ptr1 = (palette_idx1 = this->buf[stream_ptr++]) * 4;
              this->ghost_image[ghost_pixel_ptr++] = palette_idx1;
              this->yuv_planes.y[pixel_ptr] = 
                this->yuv_palette[palette_ptr1 + 0];
              this->yuv_planes.u[pixel_ptr] = 
                this->yuv_palette[palette_ptr1 + 1];
              this->yuv_planes.v[pixel_ptr] = 
                this->yuv_palette[palette_ptr1 + 2];
              pixel_ptr++;
            }
          }
        }

        y_ptr += this->yuv_planes.row_width;
        ghost_y_ptr += this->width;
      }
      break;

    case FLI_COPY:
      /* copy the chunk (uncompressed frame) to the ghost image and
       * schedule the whole frame to be updated */
      if (chunk_size - 6 > this->width * this->height) {
        printf(
         _("FLI: in chunk FLI_COPY : source data (%d bytes) bigger than" \
           " image, skipping chunk\n"),
         chunk_size - 6);
         break;
      } else
        memcpy(this->ghost_image, &this->buf[stream_ptr], chunk_size - 6);
      stream_ptr += chunk_size - 6;
      update_whole_frame = 1;
      break;

    case FLI_MINI:
      /* some sort of a thumbnail? disregard this chunk... */
      stream_ptr += chunk_size - 6;
      break;

    default:
      printf (_("FLI: Unrecognized chunk type: %d\n"), chunk_type);
      break;
    }

    frame_size -= chunk_size;
    num_chunks--;
  }

  if (update_whole_frame) {

    pixel_ptr = ghost_pixel_ptr = 0;
    for (lines = 0; lines < this->height; lines++) {
      for (x = 0; x < this->width; x++) {
        palette_ptr1 = this->ghost_image[ghost_pixel_ptr++] * 4;
        this->yuv_planes.y[pixel_ptr] = this->yuv_palette[palette_ptr1 + 0];
        this->yuv_planes.u[pixel_ptr] = this->yuv_palette[palette_ptr1 + 1];
        this->yuv_planes.v[pixel_ptr] = this->yuv_palette[palette_ptr1 + 2];
        pixel_ptr++;
      }

      pixel_ptr += 2;
    }
  }

  /* by the end of the chunk, the stream ptr should equal the frame 
   * size (minus 1, possibly); if it doesn't, issue a warning */
  if ((stream_ptr != this->size) && (stream_ptr != this->size - 1))
    printf (
      _("  warning: processed FLI chunk where chunk size = %d\n" \
        "  and final chunk ptr = %d\n"),
      this->size, stream_ptr);
}

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

static int fli_can_handle (video_decoder_t *this_gen, int buf_type) {

  return (buf_type == BUF_VIDEO_FLI);
}

/*
 * This function is responsible is called to initialize the video decoder
 * for use. Initialization usually involves setting up the fields in your
 * private video decoder object.
 */
static void fli_init (video_decoder_t *this_gen, 
  vo_instance_t *video_out) {
  fli_decoder_t *this = (fli_decoder_t *) this_gen;

  /* set our own video_out object to the one that xine gives us */
  this->video_out  = video_out;

  /* indicate that the decoder is not quite ready yet */
  this->decoder_ok = 0;
}

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void fli_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  fli_decoder_t *this = (fli_decoder_t *) this_gen;
  vo_frame_t *img; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_HEADER) { /* need to initialize */
    this->video_out->open (this->video_out);

    if(this->buf)
      free(this->buf);

    this->width = (LE_16(&buf->content[8]) + 3) & ~0x03;
    this->height = (LE_16(&buf->content[10]) + 3) & ~0x03;
    this->video_step = buf->decoder_info[1];

    this->ghost_image = xine_xmalloc(this->width * this->height);
    init_yuv_planes(&this->yuv_planes, this->width, this->height);

    if (this->buf)
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);
    this->size = 0;

    this->video_out->open (this->video_out);
    this->decoder_ok = 1;

    return;
  } else if (this->decoder_ok) {

    if (this->size + buf->size > this->bufsize) {
      this->bufsize = this->size + 2 * buf->size;
      this->buf = realloc (this->buf, this->bufsize);
    }

    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size += buf->size;

    if (buf->decoder_flags & BUF_FLAG_FRAMERATE)
      this->video_step = buf->decoder_info[0];

    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {

      img = this->video_out->get_frame (this->video_out,
                                        this->width, this->height,
                                        42, IMGFMT_YUY2, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

      decode_fli_frame(this);
      yuv444_to_yuy2(&this->yuv_planes, img->base[0], img->pitches[0]);

      if (img->copy) {
	int height = img->height;
	uint8_t *src[3];

	src[0] = img->base[0];

	while ((height -= 16) >= 0) {
	  img->copy(img, src);
	  src[0] += 16 * img->pitches[0];
	}
      }

      img->draw(img);
      img->free(img);

      this->size = 0;
    }
  }
}

/*
 * This function is called when xine needs to flush the system. Not
 * sure when or if this is used or even if it needs to do anything.
 */
static void fli_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void fli_reset (video_decoder_t *this_gen) {
  fli_decoder_t *this = (fli_decoder_t *) this_gen;

  this->size = 0;
}

/*
 * This function is called when xine shuts down the decoder. It should
 * free any memory and release any other resources allocated during the
 * execution of the decoder.
 */
static void fli_close (video_decoder_t *this_gen) {
  fli_decoder_t *this = (fli_decoder_t *) this_gen;

  if (this->buf) {
    free (this->buf);
    this->buf = NULL;
  }

  free_yuv_planes(&this->yuv_planes);
  free(this->ghost_image);

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->video_out->close(this->video_out);
  }
}

/*
 * This function returns the human-readable ID string to identify 
 * this decoder.
 */
static char *fli_get_id(void) {
  return "FLI Video";
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void fli_dispose (video_decoder_t *this_gen) {
  free (this_gen);
}

/*
 * This function should be the plugin's only advertised function to the
 * outside world. It allows xine to query the plugin module for the addresses
 * to the necessary functions in the video decoder object. The video
 * decoder object also has a priority field which allows different decoder
 * plugins for the same buffer types to coexist peacefully. The higher the
 * priority number, the more precedence a decoder has. E.g., 9 beats 1.
 */
video_decoder_t *init_video_decoder_plugin (int iface_version, xine_t *xine) {

  fli_decoder_t *this ;

  if (iface_version != 10) {
    printf( "fli: plugin doesn't support plugin API version %d.\n"
            "fli: this means there's a version mismatch between xine and this "
            "fli: decoder plugin.\nInstalling current plugins should help.\n",
            iface_version);
    return NULL;
  }

  this = (fli_decoder_t *) malloc (sizeof (fli_decoder_t));
  memset(this, 0, sizeof (fli_decoder_t));

  this->video_decoder.interface_version   = iface_version;
  this->video_decoder.can_handle          = fli_can_handle;
  this->video_decoder.init                = fli_init;
  this->video_decoder.decode_data         = fli_decode_data;
  this->video_decoder.flush               = fli_flush;
  this->video_decoder.reset               = fli_reset;
  this->video_decoder.close               = fli_close;
  this->video_decoder.get_identifier      = fli_get_id;
  this->video_decoder.dispose             = fli_dispose;
  this->video_decoder.priority            = 1;

  return (video_decoder_t *) this;
}
