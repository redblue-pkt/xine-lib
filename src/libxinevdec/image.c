/*
 * Copyright (C) 2003 the xine project
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
 * $Id: image.c,v 1.4 2003/05/11 22:00:09 holstsn Exp $
 *
 * a image video decoder
 */


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <png.h>

#include "xine_internal.h"
#include "bswap.h"
#include "video_out.h"
#include "buffer.h"

/*
#define LOG
*/

typedef struct {
  video_decoder_class_t   decoder_class;

  /*
   * private variables
   */

} image_class_t;


typedef struct image_decoder_s {
  video_decoder_t   video_decoder;

  image_class_t    *cls;

  xine_stream_t    *stream;
  int               video_open;
  int               pts;

  /* png */
  png_structp       png_ptr;
  png_infop         info_ptr;
  char             *user_error_ptr;
  png_uint_32       width, height;
  int               bit_depth,
                    color_type,
		    interlace_type,
		    compression_type,
		    filter_type;
  png_bytep        *rows;
  jmp_buf           jmpbuf;
  int               passes, rowbytes;
  int               rows_valid;

} image_decoder_t;

/*
 * png stuff
 */

void info_callback(png_structp png_ptr, png_infop info);
void row_callback(png_structp png_ptr, png_bytep new_row,
    png_uint_32 row_num, int pass);
void end_callback(png_structp png_ptr, png_infop info);

int initialize_png_reader(image_decoder_t *this) {
 
  this->png_ptr = png_create_read_struct
      (PNG_LIBPNG_VER_STRING, (png_voidp)this,
       NULL, NULL);
  if (!this->png_ptr)
      return -1;
  
  this->info_ptr = png_create_info_struct(this->png_ptr);
  
  if (!this->info_ptr) {
      png_destroy_read_struct(&this->png_ptr, NULL, NULL);
      return -1;
  }

  if (setjmp(this->jmpbuf)) {
      png_destroy_read_struct(&this->png_ptr, &this->info_ptr,
	 (png_infopp)NULL);
      return -1;
  }

  png_set_progressive_read_fn(this->png_ptr, (void *)this,
      info_callback, row_callback, end_callback);

  return 0;
}

int finalize_png_reader(image_decoder_t *this) {

  png_destroy_read_struct(&this->png_ptr, &this->info_ptr,
	 (png_infopp)NULL);
  this->png_ptr = NULL;
  this->info_ptr = NULL;

}

int process_data(image_decoder_t *this, png_bytep buffer, png_uint_32 length) {
   
  if (setjmp(this->jmpbuf)) {
    png_destroy_read_struct(&this->png_ptr, &this->info_ptr, (png_infopp)NULL);
    return -1;
  }
  png_process_data(this->png_ptr, this->info_ptr, buffer, length);
  return 0;
}

 /*
  * process png header (do some conversions if necessary)
  */

void info_callback(png_structp png_ptr, png_infop info_ptr) {
  int i;
  image_decoder_t *this = png_get_progressive_ptr(png_ptr);

#ifdef LOG
  printf("image: png info cb\n");
#endif
  png_get_IHDR(png_ptr, info_ptr, &this->width, &this->height,
       &this->bit_depth, &this->color_type, &this->interlace_type,
       &this->compression_type, &this->filter_type);
   

  /* expand palette images to RGB, low-bit-depth
   * grayscale images to 8 bits, transparency chunks to full alpha channel;
   * strip 16-bit-per-sample images to 8 bits per sample; and convert
   * grayscale to RGB[A] */

  if (this->color_type == PNG_COLOR_TYPE_PALETTE)
      png_set_expand(png_ptr);
  if (this->color_type == PNG_COLOR_TYPE_GRAY && this->bit_depth < 8)
      png_set_expand(png_ptr);
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
      png_set_expand(png_ptr);
  if (this->bit_depth == 16)
      png_set_strip_16(png_ptr);
  if (this->color_type == PNG_COLOR_TYPE_GRAY ||
      this->color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
      png_set_gray_to_rgb(png_ptr);
  if (this->color_type & PNG_COLOR_MASK_ALPHA)
      png_set_strip_alpha(png_ptr);


  /* we'll let libpng expand interlaced images, too */

  this->passes = png_set_interlace_handling(png_ptr);

  /* all transformations have been registered; now update info_ptr data and
   * then get rowbytes */

  png_read_update_info(png_ptr, info_ptr);

  this->rowbytes = (int)png_get_rowbytes(png_ptr, info_ptr);

  this->rows = xine_xmalloc(sizeof(png_bytep)*this->height);

  for (i=0; i<this->height; i++) {
   this->rows[i] = xine_xmalloc(this->rowbytes);
  }
  this->rows_valid = 1;
}

void row_callback(png_structp png_ptr, png_bytep new_row,
  png_uint_32 row_num, int pass) {

  image_decoder_t *this = png_get_progressive_ptr(png_ptr);

  if (!new_row) return;

  /*
   * copy new row to this->rows
   */
  
  png_progressive_combine_row(png_ptr, this->rows[row_num], new_row);
}

/* for rgb2yuv */
#define	CENTERSAMPLE	128
#define	SCALEBITS	16
#define	FIX(x)	 	( (int32_t) ( (x) * (1<<SCALEBITS) + 0.5 ) )
#define	ONE_HALF	( (int32_t) (1<< (SCALEBITS-1)) )
#define	CBCR_OFFSET	(CENTERSAMPLE << SCALEBITS)

void end_callback(png_structp png_ptr, png_infop info) {

  vo_frame_t *img; /* video out frame */
  int row, col;
  int i;

  /*
   * libpng has read end of image, now convert rows into a video frame
   */
  
  image_decoder_t *this = png_get_progressive_ptr(png_ptr);
  finalize_png_reader(this);
#ifdef LOG
  printf("image: png end cb\n");
#endif
    
  if (this->rows_valid) {
    img = this->stream->video_out->get_frame (this->stream->video_out, this->width,
				      this->height, XINE_VO_ASPECT_DONT_TOUCH, 
				      XINE_IMGFMT_YUY2, 
				      VO_BOTH_FIELDS);

    img->pts = this->pts;
    img->duration = 3600;
    img->bad_frame = 0;
    
    for (row=0; row<this->height; row++) {

      uint16_t *out;

      out = (uint16_t *) (img->base[0] + row * img->pitches[0] );

      for (col=0; col<this->width; col++, out++) {
      
	uint8_t   r,g,b;
	uint8_t   y,u,v;

	r = *(this->rows[row]+col*3);
	g = *(this->rows[row]+col*3+1);
	b = *(this->rows[row]+col*3+2);
	y = (FIX(0.299) * r + FIX(0.587) * g + FIX(0.114) * b + ONE_HALF)
	    >> SCALEBITS;
	if (!(col & 0x0001)) {
	  /* even pixel, do u */
	  u = (- FIX(0.16874) * r - FIX(0.33126) * g + FIX(0.5) * b
	      + CBCR_OFFSET + ONE_HALF-1) >> SCALEBITS;
	  *out = ( (uint16_t) u << 8) | (uint16_t) y;
	} else {
	  /* odd pixel, do v */
	  v = (FIX(0.5) * r - FIX(0.41869) * g - FIX(0.08131) * b
	      + CBCR_OFFSET + ONE_HALF-1) >> SCALEBITS;
	  *out = ( (uint16_t) v << 8) | (uint16_t) y;
	}

	*out = le2me_16(*out);
      }
    }
    this->stream->stream_info[XINE_STREAM_INFO_FRAME_DURATION] = img->duration;
    img->draw(img, this->stream);
    img->free(img);
  }
}


/*
 * png stuff end
 */

static void image_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  image_decoder_t *this = (image_decoder_t *) this_gen;

  if (!this->png_ptr) {
    if (initialize_png_reader(this) < 0) {
      printf("image: failed to init png reader\n");
    }
  }
  if (!this->video_open) {
#ifdef LOG
    printf("image: opening video\n");
#endif
    this->stream->video_out->open(this->stream->video_out, this->stream);
    this->video_open = 1;
  }
#ifdef LOG
  printf("image: have to decode data\n");
#endif

  this->pts = buf->pts;
  if (process_data(this, buf->content, buf->size) < 0)
  {
    printf("image: error processing data\n");
  }
}


static void image_flush (video_decoder_t *this_gen) {
  /* image_decoder_t *this = (image_decoder_t *) this_gen; */
  
  /*
   * flush out any frames that are still stored in the decoder
   */
}


static void image_reset (video_decoder_t *this_gen) {
  /* image_decoder_t *this = (image_decoder_t *) this_gen; */
   
  /*
   * reset decoder after engine flush (prepare for new
   * video data not related to recently decoded data)
   */
}


static void image_discontinuity (video_decoder_t *this_gen) {
  /* image_decoder_t *this = (image_decoder_t *) this_gen; */
 
  /*
   * a time reference discontinuity has happened.
   * that is, it must forget any currently held pts value
   */
}

static void image_dispose (video_decoder_t *this_gen) {
  image_decoder_t *this = (image_decoder_t *) this_gen;

  if (this->video_open) {
#ifdef LOG
    printf("image: closing video\n");
#endif
    this->stream->video_out->close(this->stream->video_out, this->stream);
    this->video_open = 0;
  }

#ifdef LOG
  printf("image: closed\n");
#endif
  free (this);
}


static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, 
				     xine_stream_t *stream) {

  image_class_t   *cls = (image_class_t *) class_gen;
  image_decoder_t *this;

#ifdef LOG
  printf("image: opened\n");
#endif

  this = (image_decoder_t *) xine_xmalloc (sizeof (image_decoder_t));

  this->video_decoder.decode_data         = image_decode_data;
  this->video_decoder.flush               = image_flush;
  this->video_decoder.reset               = image_reset;
  this->video_decoder.discontinuity       = image_discontinuity;
  this->video_decoder.dispose             = image_dispose;
  this->cls                               = cls;
  this->stream                            = stream;

  /*
   * initialisation of privates
   */

  if (initialize_png_reader(this) < 0) {
    printf("image: failed to init png reader\n");
  }

  return &this->video_decoder;
}

/*
 * image plugin class
 */

static char *get_identifier (video_decoder_class_t *this) {
  return "imagevdec";
}

static char *get_description (video_decoder_class_t *this) {
  return "image video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this_gen) {
  image_class_t   *this = (image_class_t *) this_gen;

#ifdef LOG
  printf("image: class closed\n");
#endif
  
  free (this);
}

static void *init_class (xine_t *xine, void *data) {

  image_class_t       *this;
  /* config_values_t    *config = xine->config; */

  this = (image_class_t *) xine_xmalloc (sizeof (image_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  /*
   * initialisation of privates
   */

#ifdef LOG
  printf("image: class opened\n");
#endif
    
  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t supported_types[] = { BUF_VIDEO_IMAGE,
                                      0 };

static decoder_info_t dec_info_image = {
  supported_types,     /* supported types */
  6                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 14, "image", XINE_VERSION_CODE, &dec_info_image, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
