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
 * $Id: xine_decoder.c,v 1.5 2003/06/13 00:52:47 jcdutton Exp $
 *
 * stuff needed to turn libmpeg2 into a xine decoder plugin
 */


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include "./include/mpeg2.h"
#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"


#define LOG

#define LOG_FRAME_ALLOC_FREE


typedef struct {
  video_decoder_class_t   decoder_class;
} mpeg2_class_t;


typedef struct mpeg2_video_decoder_s {
  video_decoder_t  video_decoder;
  mpeg2dec_t      *mpeg2dec;
  mpeg2_class_t   *class;
  xine_stream_t   *stream;
  int32_t         force_aspect;
  int32_t         aspect_ratio;
  int32_t         aspect_ratio_float;
  uint32_t        img_state[30];
  uint32_t	  frame_number;
  
} mpeg2_video_decoder_t;


static void mpeg2_video_print_bad_state(uint32_t * img_state) {
  int32_t n,m;
  m=0;
  for(n=0;n<30;n++) {
    if (img_state[n]>0) {
      printf("%d = %u\n",n, img_state[n]);
      m++;
    }
  }
  if (m > 3) assert(0);
  if (m == 0) printf("NO FRAMES\n");
} 

static void mpeg2_video_print_fbuf(const mpeg2_fbuf_t * fbuf) {
  printf("%p",fbuf);
  vo_frame_t * img;
  if (fbuf) {
    img = (vo_frame_t *) fbuf->id;
    if (img) {
      printf (", img=%p, (id=%d)\n",
             img, img->id);
    } else {
      printf (", img=NULL\n");
    }
  } else {
    printf ("\n");
  }
}

static void mpeg2_video_decode_data (video_decoder_t *this_gen, buf_element_t *buf_element) {
  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;
  uint8_t * current = buf_element->content;
  uint8_t * end = buf_element->content + buf_element->size;
  const mpeg2_info_t * info;
  uint32_t pts;
  mpeg2_state_t state;
  vo_frame_t * img;
  /* handle aspect hints from xine-dvdnav */
  if (buf_element->decoder_flags & BUF_FLAG_SPECIAL) {
    if (buf_element->decoder_info[1] == BUF_SPECIAL_ASPECT) {
      this->force_aspect = buf_element->decoder_info[2];
      if (buf_element->decoder_info[3] == 0x1 && buf_element->decoder_info[2] == XINE_VO_ASPECT_ANAMORPHIC)
        /* letterboxing is denied, we have to do pan&scan */
        this->force_aspect = XINE_VO_ASPECT_PAN_SCAN;
    }
    printf("libmpeg2:decode_data: forced aspect to=%d\n", this->force_aspect);
    
    return;
  }

  if (buf_element->decoder_flags != 0) return;

  printf ("libmpeg2:decode_data:buffer\n");
  pts=buf_element->pts;

  mpeg2_buffer (this->mpeg2dec, current, end);

  info = mpeg2_info (this->mpeg2dec);
  while ((state = mpeg2_parse (this->mpeg2dec)) != STATE_BUFFER) {
    printf("libmpeg2:decode_data:current_fbuf=");
    mpeg2_video_print_fbuf(info->current_fbuf);
    printf("libmpeg2:decode_data:display_fbuf=");
    mpeg2_video_print_fbuf(info->display_fbuf);
    printf("libmpeg2:decode_data:discard_fbuf=");
    mpeg2_video_print_fbuf(info->discard_fbuf);
 
    switch (state) {
      case STATE_SEQUENCE:
        /* might set nb fbuf, convert format, stride */
        /* might set fbufs */
        printf ("libmpeg2:decode_data:STATE_SEQUENCE\n");
        this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]     = info->sequence->picture_width;
        this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT]    = info->sequence->picture_height;
        this->stream->stream_info[XINE_STREAM_INFO_FRAME_DURATION]  = info->sequence->frame_period / 300;
        if (this->force_aspect > 0) {
          this->aspect_ratio = this->force_aspect;
          switch (info->sequence->pixel_width) {
            case XINE_VO_ASPECT_PAN_SCAN:
            case XINE_VO_ASPECT_ANAMORPHIC:
              this->aspect_ratio_float = 10000 * 16.0 /9.0;
              break;
            case XINE_VO_ASPECT_DVB:         /* 2.11:1 */
              this->aspect_ratio_float = 10000 * 2.11/1.0;
              break;
            case XINE_VO_ASPECT_SQUARE:      /* square pels */
              this->aspect_ratio_float = 10000;
              break;
            default:
              this->aspect_ratio_float = 10000 * 4.0 / 3.0;
              break;
          }
        } else {
          this->aspect_ratio_float = (10000 * info->sequence->pixel_width) / info->sequence->pixel_height;
          printf("libmpeg2:decode_data: pixel_width=%d, height=%d\n",
                  info->sequence->pixel_width,
                  info->sequence->pixel_height);
          if (this->aspect_ratio_float > 20000) {
            this->aspect_ratio = XINE_VO_ASPECT_DVB;
          } else if (this->aspect_ratio_float > 15000) {
            this->aspect_ratio = XINE_VO_ASPECT_ANAMORPHIC;
          } else if (this->aspect_ratio_float > 12000) {
            this->aspect_ratio = XINE_VO_ASPECT_4_3;
          } else {
            this->aspect_ratio = XINE_VO_ASPECT_SQUARE;
          }
        }
        this->stream->stream_info[XINE_STREAM_INFO_VIDEO_RATIO] = this->aspect_ratio_float;


        if (info->sequence->flags & SEQ_FLAG_MPEG2) {
          this->stream->meta_info[XINE_META_INFO_VIDEOCODEC]  = strdup ("MPEG 2 (libmpeg2new)");
        } else {
          this->stream->meta_info[XINE_META_INFO_VIDEOCODEC]  = strdup ("MPEG 1 (libmpeg2new)");
        }

        break;
      case STATE_PICTURE:
        /* might skip */
        /* might set fbuf */
        printf ("libmpeg2:decode_data:STATE_PICTURE\n");
        if (info->current_picture) {
          printf ("libmpeg2:decode_data:current picture nb_fields = %d, flags = %x type = %d\n",
                 info->current_picture->nb_fields,
                 info->current_picture->flags,
                 info->current_picture->flags & 7);
        }
        if (info->current_picture_2nd) {
        printf ("libmpeg2:decode_data:current2 picture nb_fields = %d, flags = %x\n",
                 info->current_picture_2nd->nb_fields,
                 info->current_picture_2nd->flags);
        }
        if (info->display_picture) {
        printf ("libmpeg2:decode_data:display picture nb_fields = %d, flags = %x\n",
                 info->display_picture->nb_fields,
                 info->display_picture->flags);
        }
        if (info->display_picture_2nd) {
        printf ("libmpeg2:decode_data:display2 picture nb_fields = %d, flags = %x\n",
                 info->display_picture_2nd->nb_fields,
                 info->display_picture_2nd->flags);
        }
        img = this->stream->video_out->get_frame (this->stream->video_out,
                                              info->sequence->picture_width,
                                              info->sequence->picture_height,
                                              this->aspect_ratio,  /* Aspect ratio */
                                              XINE_IMGFMT_YV12,
                                              //picture->picture_structure);
                                              0);
        this->frame_number++;
        printf("libmpeg2:frame_number=%u\n",this->frame_number); 
        //img->pts=buf_element->pts;
#ifdef LOG_FRAME_ALLOC_FREE
        printf ("libmpeg2:decode_data:get_frame %p (id=%d)\n", img,img->id);
#endif
        if (this->img_state[img->id] != 0) {
          printf ("libmpeg2:decode_data:get_frame id=%d BAD STATE:%d\n", img->id, this->img_state[img->id]);
          assert(0);
        }

        this->img_state[img->id] = 1;
        mpeg2_set_buf (this->mpeg2dec, img->base, img);
        break;
      case STATE_SLICE:
      case STATE_END:
        printf ("libmpeg2:decode_data:STATE_SLICE/END\n");
        /* draw current picture */
        /* might free frame buffer */
        if (info->current_picture) {
          printf ("libmpeg2:decode_data:current picture nb_fields = %d, flags = %x type = %d\n",
                 info->current_picture->nb_fields,
                 info->current_picture->flags,
                 info->current_picture->flags & 7);
        }
        if (info->current_picture_2nd) {
        printf ("libmpeg2:decode_data:current2 picture nb_fields = %d, flags = %x\n",
                 info->current_picture_2nd->nb_fields,
                 info->current_picture_2nd->flags);
        }
        if (info->display_picture) {
        printf ("libmpeg2:decode_data:display picture nb_fields = %d, flags = %x\n",
                 info->display_picture->nb_fields,
                 info->display_picture->flags);
        }
        if (info->display_picture_2nd) {
        printf ("libmpeg2:decode_data:display2 picture nb_fields = %d, flags = %x\n",
                 info->display_picture_2nd->nb_fields,
                 info->display_picture_2nd->flags);
        }
        if (info->display_fbuf && info->display_fbuf->id) {
          img = (vo_frame_t *) info->display_fbuf->id;
          img->duration=info->sequence->frame_period / 300;
#ifdef LOG_FRAME_ALLOC_FREE
          printf ("libmpeg2:decode_data:draw_frame %p, id=%d \n", info->display_fbuf, img->id);
#endif
          if (this->img_state[img->id] != 1) {
            printf ("libmpeg2:decode_data:draw_frame id=%d BAD STATE:%d\n", img->id, this->img_state[img->id]);
            assert(0);
          }
          if (this->img_state[img->id] == 1) {
            img->draw (img, this->stream);
            this->img_state[img->id] = 2;
          }

        }
        if (info->discard_fbuf && !info->discard_fbuf->id) {
          printf ("libmpeg2:decode_data:BAD free_frame discard_fbuf=%p\n", info->discard_fbuf);
          assert(0);
        }
        if (info->discard_fbuf && info->discard_fbuf->id) {
          img = (vo_frame_t *) info->discard_fbuf->id;
#ifdef LOG_FRAME_ALLOC_FREE
          printf ("libmpeg2:decode_data:free_frame %p,id=%d\n", info->discard_fbuf, img->id);
#endif
          if (this->img_state[img->id] != 2) {
            printf ("libmpeg2:decode_data:free_frame id=%d BAD STATE:%d\n", img->id, this->img_state[img->id]);
            assert(0);
          }
          if (this->img_state[img->id] == 2) {
            img->free(img);
            this->img_state[img->id] = 0;
          }
        }
        mpeg2_video_print_bad_state(this->img_state);
        break;
      case STATE_GOP:
        printf ("libmpeg2:decode_data:STATE_GOP\n");
        break;
      default:
        printf ("libmpeg2:decode_data:UNKNOWN STATE!!!=%d\n", state);
        break;
   }

 }

}

static void mpeg2_video_flush (video_decoder_t *this_gen) {
  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;

#ifdef LOG
  printf ("libmpeg2: flush\n");
#endif

/*  mpeg2_flush (&this->mpeg2); */
}

static void mpeg2_video_reset (video_decoder_t *this_gen) {
  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;

#ifdef LOG
  printf ("libmpeg2: reset\n");
#endif
/*  mpeg2_reset (&this->mpeg2dec); */
}

static void mpeg2_video_discontinuity (video_decoder_t *this_gen) {
  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;

#ifdef LOG
  printf ("libmpeg2: dicontinuity\n");
#endif
/*  mpeg2_discontinuity (&this->mpeg2dec); */
}

static void mpeg2_video_dispose (video_decoder_t *this_gen) {

  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;

#ifdef LOG
  printf ("libmpeg2: close\n");
#endif

  mpeg2_close (this->mpeg2dec);

  this->stream->video_out->close(this->stream->video_out, this->stream);

  free (this);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {
  mpeg2_video_decoder_t *this ;
  int32_t n;

  this = (mpeg2_video_decoder_t *) malloc (sizeof (mpeg2_video_decoder_t));
  memset(this, 0, sizeof (mpeg2_video_decoder_t));

  this->video_decoder.decode_data         = mpeg2_video_decode_data;
  this->video_decoder.flush               = mpeg2_video_flush;
  this->video_decoder.reset               = mpeg2_video_reset;
  this->video_decoder.discontinuity       = mpeg2_video_discontinuity;
  this->video_decoder.dispose             = mpeg2_video_dispose;
  this->stream                            = stream;
  this->class                             = (mpeg2_class_t *) class_gen;
  this->frame_number=0;

  this->mpeg2dec = mpeg2_init ();
  mpeg2_custom_fbuf (this->mpeg2dec, 1);  /* <- Force libmpeg2 to use xine frame buffers. */
  stream->video_out->open(stream->video_out, stream);
  this->force_aspect = 0;
  for(n=0;n<30;n++) this->img_state[n]=0;

  return &this->video_decoder;
}

/*
 * mpeg2 plugin class
 */

static char *get_identifier (video_decoder_class_t *this) {
  return "mpeg2new";
}

static char *get_description (video_decoder_class_t *this) {
  return "mpeg2 based video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  mpeg2_class_t *this;

  this = (mpeg2_class_t *) malloc (sizeof (mpeg2_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}
/*
 * exported plugin catalog entry
 */

static uint32_t supported_types[] = { BUF_VIDEO_MPEG, 0 };

static decoder_info_t dec_info_mpeg2 = {
  supported_types,     /* supported types */
  6                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 14, "mpeg2new", XINE_VERSION_CODE, &dec_info_mpeg2, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
