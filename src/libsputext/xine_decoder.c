/*
 * Copyright (C) 2000-2004 the xine project
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
 * $Id: xine_decoder.c,v 1.78 2004/03/13 13:59:19 mroi Exp $
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

#define LOG_MODULE "libsputext"
#define LOG_VERBOSE
/*
#define LOG
*/

/* FIXME: evil, evil, evil! */
#define XINE_ENGINE_INTERNAL

#include "buffer.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "osd.h"

#define SUB_MAX_TEXT  5      /* lines */
#define SUB_BUFSIZE   256    /* chars per line */


typedef enum {
  SUBTITLE_SIZE_TINY = 0,
  SUBTITLE_SIZE_SMALL,
  SUBTITLE_SIZE_NORMAL,
  SUBTITLE_SIZE_LARGE,
  SUBTITLE_SIZE_VERY_LARGE,
  SUBTITLE_SIZE_HUGE,

  SUBTITLE_SIZE_NUM        /* number of values in enum */
} subtitle_size;

#define FONTNAME_SIZE 100

typedef struct sputext_class_s {
  spu_decoder_class_t class;

  subtitle_size      subtitle_size;   /* size of subtitles */
  int                vertical_offset;
  char               font[FONTNAME_SIZE]; /* subtitle font */
  char              *src_encoding;    /* encoding of subtitle file */
  int                use_unscaled;    /* use unscaled OSD if possible */

  xine_t            *xine;

} sputext_class_t;


typedef struct sputext_decoder_s {
  spu_decoder_t      spu_decoder;

  sputext_class_t   *class;
  xine_stream_t     *stream;

  int                ogm;
  int                lines;
  char               text[SUB_MAX_TEXT][SUB_BUFSIZE];

  /* below 3 variables are the same from class. use to detect
   * when something changes.
   */
  subtitle_size      subtitle_size;   /* size of subtitles */
  int                vertical_offset;
  char               font[FONTNAME_SIZE]; /* subtitle font */

  int                width;          /* frame width                */
  int                height;         /* frame height               */
  int                font_size;
  int                line_height;
  int                started;
  int                finished;

  osd_renderer_t    *renderer;
  osd_object_t      *osd;

  int64_t            img_duration;
  int64_t            last_subtitle_end; /* no new subtitle before this vpts */
  int                unscaled;          /* use unscaled OSD */
  
  int                last_lines;        /* number of lines of the previous subtitle */
} sputext_decoder_t;


static void update_font_size (sputext_decoder_t *this, int force_update) {
  static int sizes[SUBTITLE_SIZE_NUM] = { 16, 20, 24, 32, 48, 64 };

  int  y;

  if ((this->subtitle_size != this->class->subtitle_size) ||
      (this->vertical_offset != this->class->vertical_offset) ||
      force_update) {
  
    this->subtitle_size = this->class->subtitle_size;
    this->vertical_offset = this->class->vertical_offset;
    this->last_lines = 0;

    this->font_size = sizes[this->class->subtitle_size];

    this->line_height = this->font_size + 10;

    y = this->height - (SUB_MAX_TEXT * this->line_height) - 5;

    if(((y - this->class->vertical_offset) >= 0) && ((y - this->class->vertical_offset) <= this->height))
      y -= this->class->vertical_offset;

    if( this->osd )
      this->renderer->free_object (this->osd);

    if(this->renderer) {
      this->osd = this->renderer->new_object (this->renderer, 
                                              this->width,
                                              SUB_MAX_TEXT * this->line_height);

      this->renderer->set_font (this->osd, this->class->font, this->font_size);
      this->renderer->set_position (this->osd, 0, y);
    }
  }
}

static void update_output_size (sputext_decoder_t *this) {
  int unscaled;

  unscaled = this->class->use_unscaled &&
             (this->stream->video_out->get_capabilities(this->stream->video_out) &
              VO_CAP_UNSCALED_OVERLAY);
  if( unscaled != this->unscaled ) {
    this->unscaled = unscaled;
    this->width = 0; /* force update */
  }

  /* initialize decoder if needed */
  if( this->unscaled ) {
    if( this->width != this->stream->video_out->get_property(this->stream->video_out, 
                                                             VO_PROP_WINDOW_WIDTH) ||
        this->height != this->stream->video_out->get_property(this->stream->video_out, 
                                                             VO_PROP_WINDOW_HEIGHT) ||
        !this->img_duration || !this->osd ) {

      int width, height; /* dummy */
        
      if( this->stream->video_out->status(this->stream->video_out, NULL,
                                           &width, &height, &this->img_duration )) {

        this->width = this->stream->video_out->get_property(this->stream->video_out,
                                                             VO_PROP_WINDOW_WIDTH);
        this->height = this->stream->video_out->get_property(this->stream->video_out,
                                                             VO_PROP_WINDOW_HEIGHT);

        if( this->width && this->height && this->img_duration ) {
          this->renderer = this->stream->osd_renderer;
          
          update_font_size (this, 1);
        }
      }
    }
  } else {
    if( !this->width || !this->height || !this->img_duration || !this->osd ) {
        
      if( this->stream->video_out->status(this->stream->video_out, NULL,
                                           &this->width, &this->height, &this->img_duration )) {
                                               
        if( this->width && this->height && this->img_duration ) {
          this->renderer = this->stream->osd_renderer;
          
          update_font_size (this, 1);
        }
      }
    }
  }
}

static int ogm_get_width(sputext_decoder_t *this, char* text) {
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
      this->renderer->get_text_size(this->osd, letter, &w, &dummy);
      width=width+w;
      i++;
    }
  }

  return width;
}

static void ogm_render_line(sputext_decoder_t *this, int x, int y, char* text) {
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
      this->renderer->render_text(this->osd, x, y, letter, OSD_TEXT1);
      this->renderer->get_text_size(this->osd, letter, &w, &dummy);
      x=x+w;
      i++;
    }
  }
}

static void draw_subtitle(sputext_decoder_t *this, int64_t sub_start, int64_t sub_end ) {
  
  int line, y;
  int font_size;

  update_font_size(this, 0);
  
  if( strcmp(this->font, this->class->font) ) {
    strcpy(this->font, this->class->font);
    if( this->renderer )
      this->renderer->set_font (this->osd, this->class->font, this->font_size);
  }
  
  if (this->last_lines)
    this->renderer->filled_rect (this->osd, 0, this->line_height * (SUB_MAX_TEXT - this->last_lines),
                                 this->width - 1, this->line_height * SUB_MAX_TEXT - 1, 0);
  this->last_lines = this->lines;
  
  y = (SUB_MAX_TEXT - this->lines) * this->line_height;
  font_size = this->font_size;

  this->renderer->set_encoding(this->osd, this->class->src_encoding);
  
  for (line = 0; line < this->lines; line++) {
    int w, h, x;
          
    while(1) {
      if( this->ogm )
        w = ogm_get_width( this, this->text[line]);
      else
        this->renderer->get_text_size( this->osd, this->text[line], 
                                       &w, &h);
      x = (this->width - w) / 2;
            
      if( w > this->width && font_size > 16 ) {
        font_size -= 4;
        this->renderer->set_font (this->osd, this->class->font, font_size);
      } else {
        break;
      }
    }
    
    if( this->ogm ) {
      ogm_render_line(this, x, y + line*this->line_height, this->text[line]);
    } else  {
      this->renderer->render_text (this->osd, x, y + line * this->line_height,
                                   this->text[line], OSD_TEXT1);
    }
  }
         
  if( font_size != this->font_size )
    this->renderer->set_font (this->osd, this->class->font, this->font_size);
  
  if( this->last_subtitle_end && sub_start < this->last_subtitle_end ) {
    sub_start = this->last_subtitle_end;
  }
  this->last_subtitle_end = sub_end;
          
  this->renderer->set_text_palette (this->osd, -1, OSD_TEXT1);
  
  if (this->unscaled)
    this->renderer->show_unscaled (this->osd, sub_start);
  else
    this->renderer->show (this->osd, sub_start);
  
  this->renderer->hide (this->osd, sub_end);
  
  lprintf ("scheduling subtitle >%s< at %lld until %lld, current time is %lld\n",
	   this->text[0], sub_start, sub_end, 
	   this->stream->xine->clock->get_current_time (this->stream->xine->clock));
}


static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {

  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;
  int uses_time;
  int32_t start, end, diff;
  int64_t start_vpts, end_vpts;
  int64_t spu_offset;
  int i;
  uint32_t *val;
  char *str;
  extra_info_t extra_info;
  int master_status, slave_status;
  int vo_discard;

  /* filter unwanted streams */
  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    return;
  }
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;
  
  if ((this->stream->spu_channel & 0x1f) != (buf->type & 0x1f))
    return;

  if( (buf->type & 0xFFFF0000) == BUF_SPU_OGM ) {

    this->ogm = 1;
    uses_time = 1;
    val = (uint32_t * )buf->content;
    start = *val++;
    end = *val++;
    str = (char *)val;

    if (!*str) return;
    /* Empty ogm packets (as created by ogmmux) clears out old messages. We already respect the end time. */
  
    this->lines = 0;
  
    i = 0;
    while (*str && (this->lines < SUB_MAX_TEXT) && (i < SUB_BUFSIZE)) {
      if (*str == '\r' || *str == '\n') {
        if (i) {
          this->text[ this->lines ][i] = 0;
          this->lines++;
          i = 0;
        }
      } else {
        this->text[ this->lines ][i] = *str;
        if (i < SUB_BUFSIZE-1)
          i++;
      }
      str++;
    }
    if (i == SUB_BUFSIZE)
      i--;
    
    if (i) {
      this->text[ this->lines ][i] = 0;
      this->lines++;
    }

  } else {

    this->ogm = 0;
    val = (uint32_t * )buf->content;
    
    this->lines = *val++;
    uses_time = *val++;
    start = *val++;
    end = *val++;
    str = (char *)val;
    for (i = 0; i < this->lines; i++, str += strlen(str) + 1) {
      strncpy( this->text[i], str, SUB_BUFSIZE - 1);
      this->text[i][SUB_BUFSIZE - 1] = '\0';
    }

  }
  
  xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
          "libsputext: decoder data [%s]\n", this->text[0]);
  xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
          "libsputext: mode %d timing %d->%d\n", uses_time, start, end);

  if( end <= start ) {
    xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
            "libsputext: discarding subtitle with invalid timing\n");
    return;
  }
  
  spu_offset = this->stream->master->metronom->get_option (this->stream->master->metronom,
                                                           METRONOM_SPU_OFFSET);
  if( uses_time ) {
    start += (spu_offset / 90);
    end += (spu_offset / 90);
  } else {
    if( this->osd && this->img_duration ) {
      start += spu_offset / this->img_duration;
      end += spu_offset / this->img_duration;
    }
  }
   
  while( !this->finished ) {
 
    master_status = xine_get_status (this->stream->master);
    slave_status = xine_get_status (this->stream);
    vo_discard = this->stream->video_out->get_property(this->stream->video_out, 
                                                       VO_PROP_DISCARD_FRAMES);

    _x_get_current_info (this->stream->master, &extra_info, sizeof(extra_info) );
   
    lprintf("master: %d slave: %d input_pos: %lld vo_discard: %d\n", 
      master_status, slave_status, extra_info.input_pos, vo_discard);

    if( !this->started && (master_status == XINE_STATUS_PLAY &&
                           slave_status == XINE_STATUS_PLAY &&
                           extra_info.input_pos) ) {
      lprintf("started\n");

      this->width = this->height = 0;
      this->started = 1;
    }

    if( this->started ) {

      if( master_status != XINE_STATUS_PLAY || 
          slave_status != XINE_STATUS_PLAY ||
          vo_discard ) {
        lprintf("finished\n");
  
        this->width = this->height = 0;
        this->finished = 1;
        return;
      }

      update_output_size( this );
      
      if( this->osd ) {
        
        /* try to use frame number mode */
        if( !uses_time && extra_info.frame_number ) {
          
          diff = end - extra_info.frame_number;
          
          /* discard old subtitles */
          if( diff < 0 ) {
            xprintf(this->class->xine, XINE_VERBOSITY_DEBUG, 
                    "libsputext: discarding old subtitle\n");
            return;
          }
            
          diff = start - extra_info.frame_number;
          
          /* draw it if less than 1/2 second left */
          if( diff < 90000/2 / this->img_duration ) {
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
          if( diff < 0 ) {
            xprintf(this->class->xine, XINE_VERBOSITY_DEBUG,
                    "libsputext: discarding old subtitle\n");
            return;
          }
            
          diff = start - extra_info.input_time;
          
          /* draw it if less than 1/2 second left */
          if( diff < 500 || this->ogm ) {
            start_vpts = extra_info.vpts + diff * 90;
            end_vpts = start_vpts + (end-start) * 90;
            
            draw_subtitle(this, start_vpts, end_vpts);
            return;     
          }
        }
      }
    }

    /* we may never block on ogm mode because we are on the same thread
     * as the video decoder. therefore nothing will possibly happen
     * (like frames being displayed) if we hang here doing nothing. 
     * it is possible, but unlikely, that the very first ogm subtitle
     * gets dropped because of the following return.
     */
    if( this->ogm )
      return;
    
    if (this->class->xine->port_ticket->ticket_revoked)
      this->class->xine->port_ticket->renew(this->class->xine->port_ticket, 0);
    xine_usec_sleep (50000);

  }
}  


static void spudec_reset (spu_decoder_t *this_gen) {
  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;
  
  lprintf("i guess we just seeked\n");
  this->width = this->height = 0;
  this->started = this->finished = 0;
}

static void spudec_discontinuity (spu_decoder_t *this_gen) {
  /* sputext_decoder_t *this = (sputext_decoder_t *) this_gen; */

}

static void spudec_dispose (spu_decoder_t *this_gen) {
  sputext_decoder_t *this = (sputext_decoder_t *) this_gen;
  
  if (this->osd) {
    this->renderer->free_object (this->osd);
    this->osd = NULL;
  }
  free(this);
}

static void update_vertical_offset(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  class->vertical_offset = entry->num_value;
}

static void update_osd_font(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  strcpy(class->font, entry->str_value);
  
  xprintf(class->xine, XINE_VERBOSITY_DEBUG, "libsputext: spu_font = %s\n", class->font );
}

static void update_subtitle_size(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  class->subtitle_size = entry->num_value;
}

static void update_use_unscaled(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  class->use_unscaled = entry->num_value;
}

static spu_decoder_t *sputext_class_open_plugin (spu_decoder_class_t *class_gen, xine_stream_t *stream) {

  sputext_class_t *class = (sputext_class_t *)class_gen;
  sputext_decoder_t *this ;

  this = (sputext_decoder_t *) xine_xmalloc (sizeof (sputext_decoder_t));

  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.discontinuity       = spudec_discontinuity;
  this->spu_decoder.get_interact_info   = NULL;
  this->spu_decoder.set_button          = NULL;
  this->spu_decoder.dispose             = spudec_dispose;

  this->class  = class;
  this->stream = stream;

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

static void update_src_encoding(void *class_gen, xine_cfg_entry_t *entry)
{
  sputext_class_t *class = (sputext_class_t *)class_gen;

  class->src_encoding = entry->str_value;
  xprintf(class->xine, XINE_VERBOSITY_DEBUG, "libsputext: spu_src_encoding = %s\n", class->src_encoding );
}

static void *init_spu_decoder_plugin (xine_t *xine, void *data) {

  static char *subtitle_size_strings[] = { 
    "tiny", "small", "normal", "large", "very large", "huge", NULL 
  };
  sputext_class_t *this ;

  lprintf("init class\n");
  
  this = (sputext_class_t *) xine_xmalloc (sizeof (sputext_class_t));

  this->class.open_plugin      = sputext_class_open_plugin;
  this->class.get_identifier   = sputext_class_get_identifier;
  this->class.get_description  = sputext_class_get_description;
  this->class.dispose          = sputext_class_dispose;

  this->xine                   = xine;

  this->subtitle_size  = xine->config->register_enum(xine->config, 
			      "misc.spu_subtitle_size", 
			       1,
			       subtitle_size_strings,
			       _("Subtitle size (relative window size)"), 
			       NULL, 0, update_subtitle_size, this);
  this->vertical_offset  = xine->config->register_num(xine->config,
			      "misc.spu_vertical_offset", 
			      0,
			      _("Subtitle vertical offset (relative window size)"), 
			      NULL, 0, update_vertical_offset, this);
  strcpy(this->font,       xine->config->register_string(xine->config, 
				"misc.spu_font", 
				"sans", 
				_("Font for external subtitles"), 
				NULL, 0, update_osd_font, this));
  this->src_encoding  = xine->config->register_string(xine->config, 
				"misc.spu_src_encoding", 
				"iso-8859-1", 
				_("Encoding of subtitles"), 
				NULL, 10, update_src_encoding, this);
  this->use_unscaled  = xine->config->register_bool(xine->config, 
			      "misc.spu_use_unscaled_osd", 
			       1,
			       _("Use unscaled OSD if possible"), 
			       NULL, 0, update_use_unscaled, this);

  return &this->class;
}


/* plugin catalog information */
static uint32_t supported_types[] = { BUF_SPU_TEXT, BUF_SPU_OGM, 0 };

static decoder_info_t spudec_info = {
  supported_types,     /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_SPU_DECODER | PLUGIN_MUST_PRELOAD, 16, "sputext", XINE_VERSION_CODE, &spudec_info, &init_spu_decoder_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
