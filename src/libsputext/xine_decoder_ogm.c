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
 * $Id: xine_decoder_ogm.c,v 1.1 2003/04/16 23:06:38 heinchen Exp $
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


typedef struct spuogm_class_s {
  spu_decoder_class_t class;

  char              *src_encoding;  /* encoding of subtitle file */
  
  xine_t            *xine;

} spuogm_class_t;


typedef struct spuogm_decoder_s {
  spu_decoder_t      spu_decoder;

  spuogm_class_t   *class;
  xine_stream_t     *stream;

  int                lines;
  char               text[SUB_MAX_TEXT][SUB_BUFSIZE];

  int                width;          /* frame width                */
  int                height;         /* frame height               */
  int                font_size;
  int                line_height;
  int                seek_count;
  int                master_started;
  int                slave_started;

  char              *font;          /* subtitle font */
  subtitle_size      subtitle_size; /* size of subtitles */
  int                vertical_offset;

  osd_renderer_t    *renderer;
  osd_object_t      *osd;

  int64_t            img_duration;
  int64_t            last_subtitle_end; /* no new subtitle before this vpts */
} spuogm_decoder_t;


static void update_font_size (spuogm_decoder_t *this) {
  static int sizes[SUBTITLE_SIZE_NUM][4] = {
    { 16, 16, 16, 20 }, /* SUBTITLE_SIZE_SMALL  */
    { 16, 16, 20, 24 }, /* SUBTITLE_SIZE_NORMAL */
    { 16, 20, 24, 32 }, /* SUBTITLE_SIZE_LARGE  */
  };

  int *vec = sizes[this->subtitle_size];
  int  y;

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
  
  if(((y - this->vertical_offset) >= 0) && ((y - this->vertical_offset) <= this->height))
    y -= this->vertical_offset;
  
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

static int get_width(spuogm_decoder_t *this, char* text) {
  int i=0,width=0,w,dummy;
  char letter[2]={0, 0};

  while (i<=strlen(text)) {
    switch (text[i]) {
    case '<':
      if (!strncmp("<b>", text+i, 3)) {
	/*Do somethink to enable BOLD typeface*/
	i=i+3;
	break;
      } else if (!strncmp("</b>", text+i, 3)) {
	/*Do somethink to disable BOLD typeface*/
	i=i+4;
	break;
      } else if (!strncmp("<i>", text+i, 3)) {	
	/*Do somethink to enable italics typeface*/
	i=i+3;
	break;
      } else if (!strncmp("</i>", text+i, 3)) {
	/*Do somethink to disable italics typeface*/
	i=i+4;
	break;
      } else if (!strncmp("<font>", text+i, 3)) {	
	/*Do somethink to disable typing
	  fixme - no teststreams*/
	i=i+6;
	break;
      } else if (!strncmp("</font>", text+i, 3)) {
	/*Do somethink to enable typing
	  fixme - no teststreams*/
	i=i+7;
	break;
      } 
default:
      letter[0]=text[i];
      this->renderer->get_text_size( this->osd, letter, &w, &dummy);	       
      width=width+w;
      i++;
    }
  }

  return width;
}

static void render_line(spuogm_decoder_t *this, int x, int y, char* text) {
  int i=0,w,dummy;
  char letter[2]={0,0};

  while (i<=strlen(text)) {
    switch (text[i]) {
    case '<':
      if (!strncmp("<b>", text+i, 3)) {
	/*Do somethink to enable BOLD typeface*/
	i=i+3;
	break;
      } else if (!strncmp("</b>", text+i, 3)) {
	/*Do somethink to disable BOLD typeface*/
	i=i+4;
	break;
      } else if (!strncmp("<i>", text+i, 3)) {	
	/*Do somethink to enable italics typeface*/
	i=i+3;
	break;
      } else if (!strncmp("</i>", text+i, 3)) {
	/*Do somethink to disable italics typeface*/
	i=i+4;
	break;
      } else if (!strncmp("<font>", text+i, 3)) {	
	/*Do somethink to disable typing
	  fixme - no teststreams*/
	i=i+6;
	break;
      } else if (!strncmp("</font>", text+i, 3)) {
	/*Do somethink to enable typing
	  fixme - no teststreams*/
	i=i+7;
	break;
      } 
    default:
      letter[0]=text[i];
      this->renderer->render_text( this->osd, x, y, letter,
				   this->class->src_encoding,OSD_TEXT1);
      this->renderer->get_text_size( this->osd, letter, &w, &dummy);
      x=x+w;
      i++;
    }
  }
}

static void draw_subtitle(spuogm_decoder_t *this, int64_t sub_start, int64_t sub_end ) {
  
  int line, y;
  int font_size;
  
  this->renderer->filled_rect (this->osd, 0, 0, this->width-1, this->line_height * SUB_MAX_TEXT - 1, 0);
  
  y = (SUB_MAX_TEXT - this->lines) * this->line_height;
  font_size = this->font_size;
        
  for (line=0; line<this->lines; line++) {
    int w,x;
          
    while(1) {
      w=get_width( this, this->text[line]);
      x = (this->width - w) / 2;
            
      if( w > this->width && font_size > 16 ) {
        font_size -= 4;
        this->renderer->set_font (this->osd, this->font, font_size);
      } else {
        break;
      }
    }
    render_line(this, x, y + line*this->line_height, this->text[line]);
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
  
#ifdef LOG
  printf ("spuogm: scheduling subtitle >%s< at %lld until %lld, current time is %lld\n",
          this->text[0], sub_start, sub_end, 
          this->stream->xine->clock->get_current_time (this->stream->xine->clock));
#endif
}


static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {

  spuogm_decoder_t *this = (spuogm_decoder_t *) this_gen;
  int32_t start, end, diff;
  int64_t start_vpts, end_vpts;
  int64_t spu_offset;
  int i;
  uint32_t *val;
  char *str;
  extra_info_t extra_info;
  int status;
  
  /* filter unwanted streams */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;
  if ((this->stream->spu_channel & 0x1f) != (buf->type & 0x1f))
    return;

  val = (uint32_t * )buf->content;
  start = *val++;
  end = *val++;
  str = (char *)val;

  this->lines=0;


  i=0;
  while (i<strlen(str)+1) {
    switch (str[i]) {
    case 13:
      str=&str[i+1];
      this->text[ this->lines ][i]=0;
      this->lines=this->lines+1;
      i=0;
      break;
    default:
      this->text[ this->lines ][i]=str[i];
      if (i<SUB_BUFSIZE-1)
	i++;
    }
  }

#ifdef LOG
  printf("libspuogm: decoder data [%s]\n", this->text[0]);
  printf("libspuogm: timing %d->%d\n", start, end);
#endif

  if( end <= start ) {
#ifdef LOG
    printf("libspuogm: discarding subtitle with invalid timing\n");
#endif
  }
  
  spu_offset = this->stream->master->metronom->get_option (this->stream->master->metronom,
                                                           METRONOM_SPU_OFFSET);
  start += (spu_offset / 90);
  end += (spu_offset / 90);
   
  xine_get_current_info (this->stream->master, &extra_info, sizeof(extra_info) );
  
  if( !this->seek_count ) {
    this->seek_count = extra_info.seek_count;
  }
   
  while(this->seek_count == extra_info.seek_count) {
  
    /* initialize decoder if needed */
    if( !this->width || !this->height || !this->img_duration || !this->osd ) {
      
      if( this->stream->video_out->status(this->stream->video_out, NULL,
                                           &this->width, &this->height, &this->img_duration )) {
                                             
        if( this->width && this->height && this->img_duration ) {
          this->renderer = this->stream->osd_renderer;
        
          update_font_size (this);
        }
      }
    }
    
    if( this->osd ) {
        
      diff = end - extra_info.input_time;
        
      /* discard old subtitles */
      if( diff < 0 ) {
#ifdef LOG
	printf("libspuogm: discarding old\n");
#endif
	return;
      }
          
      diff = start - extra_info.input_time;

      /* draw it if less than 2 seconds left */
      if( diff < 2000 ) {
	start_vpts = extra_info.vpts + diff * 90;
	end_vpts = start_vpts + (end-start) * 90;
  
	draw_subtitle(this, start_vpts, end_vpts);
	return;     
      }
    }
    
    status = xine_get_status (this->stream->master);
   
    if( this->master_started && (status == XINE_STATUS_QUIT || 
                                 status == XINE_STATUS_STOP) ) {
#ifdef LOG
      printf("libspuogm: master stopped\n");
#endif
      this->width = this->height = 0;
      return;
    }
    if( status == XINE_STATUS_PLAY )
      this->master_started = 1;
    
    status = xine_get_status (this->stream);
   
    if( this->slave_started && (status == XINE_STATUS_QUIT || 
                                status == XINE_STATUS_STOP) ) {
#ifdef LOG
      printf("libspuogm: slave stopped\n");
#endif
      this->width = this->height = 0;
      return;
    }
    if( status == XINE_STATUS_PLAY )
      this->slave_started = 1;

    xine_usec_sleep (50000);
            
    xine_get_current_info (this->stream->master, &extra_info, sizeof(extra_info) );
  }
#ifdef LOG
  printf("libspuogm: seek_count mismatch\n");
#endif
}  


static void spudec_reset (spu_decoder_t *this_gen) {
  spuogm_decoder_t *this = (spuogm_decoder_t *) this_gen;
  
  this->width = this->height = 0;
  this->seek_count = 0;
}

static void spudec_discontinuity (spu_decoder_t *this_gen) {
  /* spuogm_decoder_t *this = (spuogm_decoder_t *) this_gen; */

}

static void spudec_dispose (spu_decoder_t *this_gen) {
  spuogm_decoder_t *this = (spuogm_decoder_t *) this_gen;

  if (this->osd) {
    this->renderer->free_object (this->osd);
    this->osd = NULL;
  }
  free(this);
}

static void update_vertical_offset(void *this_gen, xine_cfg_entry_t *entry)
{
  spuogm_decoder_t *this = (spuogm_decoder_t *)this_gen;

  this->vertical_offset = entry->num_value;
  update_font_size(this);
}

static void update_osd_font(void *this_gen, xine_cfg_entry_t *entry)
{
  spuogm_decoder_t *this = (spuogm_decoder_t *)this_gen;

  this->font = entry->str_value;
  
  if( this->renderer )
    this->renderer->set_font (this->osd, this->font, this->font_size);
  
  printf("libspuogm: spu_font = %s\n", this->font );
}

static void update_subtitle_size(void *this_gen, xine_cfg_entry_t *entry)
{
  spuogm_decoder_t *this = (spuogm_decoder_t *)this_gen;

  this->subtitle_size = entry->num_value;

  update_font_size (this_gen);
}

static spu_decoder_t *spuogm_class_open_plugin (spu_decoder_class_t *class_gen, xine_stream_t *stream) {

#ifdef LOG
  printf ("libspuogm: plugin opened\n");
#endif

  spuogm_class_t *class = (spuogm_class_t *)class_gen;
  spuogm_decoder_t *this ;
  static char *subtitle_size_strings[] = { "small", "normal", "large", NULL };

  this = (spuogm_decoder_t *) xine_xmalloc (sizeof (spuogm_decoder_t));

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
				"misc.spu_font", 
				"sans", 
				_("font for external subtitles"), 
				NULL, 0, update_osd_font, this);
  this->subtitle_size  = class->xine->config->register_enum(class->xine->config, 
			      "misc.spu_subtitle_size", 
			       1,
			       subtitle_size_strings,
			       _("subtitle size (relative window size)"), 
			       NULL, 0, update_subtitle_size, this);

  this->vertical_offset  = class->xine->config->register_num(class->xine->config,
			      "misc.spu_vertical_offset", 
			      0,
			      _("subtitle vertical offset (relative window size)"), 
			      NULL, 0, update_vertical_offset, this);

  return (spu_decoder_t *) this;
}

static void spuogm_class_dispose (spu_decoder_class_t *this) {
  free (this);
}

static char *spuogm_class_get_identifier (spu_decoder_class_t *this) {
  return "spuogm";
}

static char *spuogm_class_get_description (spu_decoder_class_t *this) {
  return "ogm subtitle decoder plugin";
}

static void update_src_encoding(void *this_gen, xine_cfg_entry_t *entry)
{
  spuogm_class_t *this = (spuogm_class_t *)this_gen;

  this->src_encoding = entry->str_value;
  printf("libspuogm: spu_src_encoding = %s\n", this->src_encoding );
}

static void *init_spu_decoder_plugin (xine_t *xine, void *data) {

  spuogm_class_t *this ;

#ifdef LOG
  printf("libspuogm: init class\n");
#endif
  
  this = (spuogm_class_t *) xine_xmalloc (sizeof (spuogm_class_t));

  this->class.open_plugin      = spuogm_class_open_plugin;
  this->class.get_identifier   = spuogm_class_get_identifier;
  this->class.get_description  = spuogm_class_get_description;
  this->class.dispose          = spuogm_class_dispose;

  this->xine                   = xine;

  this->src_encoding  = xine->config->register_string(xine->config, 
				"misc.spu_src_encoding", 
				"iso-8859-1", 
				_("encoding of subtitles"), 
				NULL, 10, update_src_encoding, this);

  return &this->class;
}


/* plugin catalog information */
static uint32_t supported_types[] = { BUF_SPU_OGM, 0 };

static decoder_info_t spudec_info = {
  supported_types,     /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_SPU_DECODER, 13, "spuogm", XINE_VERSION_CODE, &spudec_info, &init_spu_decoder_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
