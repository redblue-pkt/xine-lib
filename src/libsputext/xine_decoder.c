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
 * $Id: xine_decoder.c,v 1.47 2003/01/11 15:31:45 miguelfreitas Exp $
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <iconv.h>

#include "buffer.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "osd.h"

/*
#define LOG 1
*/
#define SUB_MAX_TEXT  5

#define SUB_BUFSIZE 1024


typedef enum {
  SUBTITLE_SIZE_SMALL = 0,
  SUBTITLE_SIZE_NORMAL,
  SUBTITLE_SIZE_LARGE,

  SUBTITLE_SIZE_NUM        /* number of values in enum */
} subtitle_size;


typedef struct sputext_class_s {
  spu_decoder_class_t class;

  xine_t            *xine;


} sputext_class_t;


typedef struct sputext_decoder_s {
  spu_decoder_t      spu_decoder;

  sputext_class_t   *class;
  xine_stream_t     *stream;

  int                lines;
  char               text[SUB_MAX_TEXT][SUB_BUFSIZE];

  int                width;          /* frame width                */
  int                height;         /* frame height               */
  int                font_size;
  int                line_height;


  char              *font;
  subtitle_size      subtitle_size;
  int                time_offset;       /* offset in 1/100sec to add to vpts */

  osd_renderer_t    *renderer;
  osd_object_t      *osd;

  int64_t            img_duration;
  int64_t            last_subtitle_end; /* no new subtitle before this vpts */
} sputext_decoder_t;


static void update_font_size (sputext_decoder_t *this) {
  static int sizes[SUBTITLE_SIZE_NUM][4] = {
    { 16, 16, 16, 20 }, /* SUBTITLE_SIZE_SMALL  */
    { 16, 16, 20, 24 }, /* SUBTITLE_SIZE_NORMAL */
    { 16, 20, 24, 32 }, /* SUBTITLE_SIZE_LARGE  */
  };

  int *vec = sizes[this->subtitle_size];
  int y;

  if( this->width >= 512 )
    this->font_size = vec[3];
  else if( this->width >= 384 )
    this->font_size = vec[2];
  else if( this->width >= 320 )
    this->font_size = vec[1];
  else
    this->font_size = vec[0];
  
  this->line_height = this->font_size + 10;

  y = this->height - (SUB_MAX_TEXT * this->line_height) - 5;
  
  if( this->osd )
    this->renderer->free_object (this->osd);

  if(this->renderer) {
    this->osd = this->renderer->new_object (this->renderer, 
					    this->width,
					    SUB_MAX_TEXT * this->line_height);
    
    this->renderer->set_font (this->osd, this->font, this->font_size);
    this->renderer->set_position (this->osd, 0, y);
  }
}


static void draw_subtitle(sputext_decoder_t *this, int64_t sub_start, int64_t sub_end ) {
  
  int line, y;
  int font_size;
  
  this->renderer->filled_rect (this->osd, 0, 0, this->width-1, this->line_height * SUB_MAX_TEXT - 1, 0);
  
  y = (SUB_MAX_TEXT - this->lines) * this->line_height;
  font_size = this->font_size;
        
  for (line=0; line<this->lines; line++) {
    int w,h,x;
          
    while(1) {
      this->renderer->get_text_size( this->osd, this->text[line], 
                                     &w, &h);
      x = (this->width - w) / 2;
            
      if( w > this->width && font_size > 16 ) {
        font_size -= 4;
        this->renderer->set_font (this->osd, this->font, font_size);
      } else {
        break;
      }
    }
          
    this->renderer->render_text (this->osd, x, y + line*this->line_height,
                                 this->text[line], OSD_TEXT1);
  }
         
  if( font_size != this->font_size )
    this->renderer->set_font (this->osd, this->font, this->font_size);
  
  if( this->last_subtitle_end && sub_start < this->last_subtitle_end ) {
    sub_start = this->last_subtitle_end;
  }
  this->last_subtitle_end = sub_end;
          
  this->renderer->set_text_palette (this->osd, -1, OSD_TEXT1);
  this->renderer->show (this->osd, sub_start);
  this->renderer->hide (this->osd, sub_end);
  
//#ifdef LOG
  printf ("sputext: scheduling subtitle >%s< at %lld until %lld, current time is %lld\n",
          this->text[0], sub_start, sub_end, 
          this->stream->xine->clock->get_current_time (this->stream->xine->clock));
//#endif
}


static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {

  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;
  int uses_time;
  int32_t start, end, diff;
  int64_t start_vpts, end_vpts;
  int i;
  uint32_t *val;
  char *str;
  extra_info_t extra_info;
  int seek_count;
  int status;
  
  val = (uint32_t * )buf->content;
  this->lines = *val++;
  uses_time = *val++;
  start = *val++;
  end = *val++;
  str = (char *)val;
  for (i = 0; i < this->lines; i++, str+=strlen(str)+1) {
    strcpy( this->text[i], str );
  }
  
  printf("libsputext: decoder data [%s]\n", this->text[0]);
  printf("libsputext: mode %d timing %d->%d\n", uses_time, start, end);

  if( end <= start ) {
    printf("libsputext: discarding subtitle with invalid timing\n");
  }
  
  if( this->stream->master_stream )
    xine_get_current_info (this->stream->master_stream, &extra_info, sizeof(extra_info) );
  else
    xine_get_current_info (this->stream, &extra_info, sizeof(extra_info) );
  seek_count = extra_info.seek_count;
   
  do {
  
    /* initialize decoder if needed */
    if( !this->width || !this->height || !this->img_duration ) {
      
      if( this->stream->video_out->status(this->stream->video_out, NULL,
                                           &this->width, &this->height, &this->img_duration )) {
                                             
        if( this->width && this->height && this->img_duration ) {
          this->renderer = this->stream->osd_renderer;
        
          this->osd = NULL;
        
          update_font_size (this);
        }
      }
    }
    
    if( this->osd ) {
      
      /* try to use frame number mode */
      if( !uses_time && extra_info.frame_number ) {
        
        diff = end - extra_info.frame_number;
        
        /* discard old subtitles */
        if( diff < 0 )
          return;
          
        diff = start - extra_info.frame_number;
        
        /* draw it if less than 2 seconds left */
        if( diff < 2*90000 / this->img_duration ) {
          start_vpts = extra_info.vpts + diff * this->img_duration;
          end_vpts = start_vpts + (end-start) * this->img_duration;
     
          draw_subtitle(this, start_vpts, end_vpts);
          return;     
        }
        
      } else {
        
        if( !uses_time ) {
          start = start * this->img_duration / 90;
          end = end * this->img_duration / 90;
          uses_time = 1;
        }
        
        diff = end - extra_info.input_time;
        
        /* discard old subtitles */
        if( diff < 0 )
          return;
          
        diff = start - extra_info.input_time;
        
        /* draw it if less than 2 seconds left */
        if( diff < 2000 ) {
          start_vpts = extra_info.vpts + diff * 90;
          end_vpts = start_vpts + (end-start) * 90;
          
          draw_subtitle(this, start_vpts, end_vpts);
          return;     
        }
      }
    }
    
    if( this->stream->master_stream )
      status = xine_get_status (this->stream->master_stream);
    else
      status = xine_get_status (this->stream);
    
    if( status == XINE_STATUS_QUIT || status == XINE_STATUS_STOP )
      return;

    xine_usec_sleep (50000);
            
    if( this->stream->master_stream )
      xine_get_current_info (this->stream->master_stream, &extra_info, sizeof(extra_info) );
    else
      xine_get_current_info (this->stream, &extra_info, sizeof(extra_info) );

  } while(seek_count == extra_info.seek_count);
}  


static void spudec_reset (spu_decoder_t *this_gen) {
  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;

}

static void spudec_discontinuity (spu_decoder_t *this_gen) {
  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;

}

static void spudec_dispose (spu_decoder_t *this_gen) {
  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;

  if (this->osd) {
    this->renderer->free_object (this->osd);
    this->osd = NULL;
  }
  free(this);
}

static void update_osd_font(void *this_gen, xine_cfg_entry_t *entry)
{
  sputext_decoder_t *this = (sputext_decoder_t *)this_gen;

  this->font = entry->str_value;
  
  if( this->renderer )
    this->renderer->set_font (this->osd, this->font, this->font_size);
  
  printf("libsputext: spu_font = %s\n", this->font );
}

static void update_subtitle_size(void *this_gen, xine_cfg_entry_t *entry)
{
  sputext_decoder_t *this = (sputext_decoder_t *)this_gen;

  this->subtitle_size = entry->num_value;

  update_font_size (this_gen);
}

static void update_time_offset(void *this_gen, xine_cfg_entry_t *entry)
{
  sputext_decoder_t *this = (sputext_decoder_t *)this_gen;

  this->time_offset = entry->num_value;

  printf("libsputext: time_offset = %d\n", this->time_offset );
}

static spu_decoder_t *sputext_class_open_plugin (spu_decoder_class_t *class_gen, xine_stream_t *stream) {

  sputext_class_t *class = (sputext_class_t *)class_gen;
  sputext_decoder_t *this ;
  static char *subtitle_size_strings[] = { "small", "normal", "large", NULL };

  this = (sputext_decoder_t *) xine_xmalloc (sizeof (sputext_decoder_t));

  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.discontinuity       = spudec_discontinuity;
  this->spu_decoder.dispose             = spudec_dispose;
  this->spu_decoder.get_nav_pci         = NULL;
  this->spu_decoder.set_button          = NULL;
  this->spu_decoder.dispose             = spudec_dispose;

  this->class  = class;
  this->stream = stream;

  this->font           = class->xine->config->register_string(class->xine->config, 
				"codec.spu_font", 
				"sans", 
				_("font for avi subtitles"), 
				NULL, 0, update_osd_font, this);
  this->subtitle_size  = class->xine->config->register_enum(class->xine->config, 
			      "codec.spu_subtitle_size", 
			       1,
			       subtitle_size_strings,
			       _("subtitle size (relative window size)"), 
			       NULL, 0, update_subtitle_size, this);
  this->time_offset    = class->xine->config->register_num   (class->xine->config, 
			        "codec.spu_time_offset", 
			        0,
			        _("subtitle time offset in 1/100 sec"), 
			        NULL, 10, update_time_offset, this);


  return (spu_decoder_t *) this;
}

static void sputext_class_dispose (spu_decoder_class_t *this) {
  free (this);
}

static char *sputext_class_get_identifier (spu_decoder_class_t *this) {
  return "sputext";
}

static char *sputext_class_get_description (spu_decoder_class_t *this) {
  return "external subtitle decoder plugin";
}


static void *init_spu_decoder_plugin (xine_t *xine, void *data) {

  sputext_class_t *this ;

  printf("libsputext: init class\n");
  
  this = (sputext_class_t *) xine_xmalloc (sizeof (sputext_class_t));

  this->class.open_plugin      = sputext_class_open_plugin;
  this->class.get_identifier   = sputext_class_get_identifier;
  this->class.get_description  = sputext_class_get_description;
  this->class.dispose          = sputext_class_dispose;

  this->xine                   = xine;

  return &this->class;
}


/* plugin catalog information */
static uint32_t supported_types[] = { BUF_SPU_TEXT, 0 };

static decoder_info_t spudec_info = {
  supported_types,     /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_SPU_DECODER, 13, "sputext", XINE_VERSION_CODE, &spudec_info, &init_spu_decoder_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
