/* This is the standard xine header: */
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
 * $Id: roqvideo.c,v 1.13 2002/11/11 05:55:52 tmmm Exp $
 */

/* And this is the header that came with the RoQ video decoder: */
/* ------------------------------------------------------------------------
 * Id Software's RoQ video file format decoder
 *
 * Dr. Tim Ferguson, 2001.
 * For more details on the algorithm:
 *         http://www.csse.monash.edu.au/~timf/videocodec.html
 *
 * This is a simple decoder for the Id Software RoQ video format.  In
 * this format, audio samples are DPCM coded and the video frames are
 * coded using motion blocks and vector quantisation.
 *
 * Note: All information on the RoQ file format has been obtained through
 *   pure reverse engineering.  This was achieved by giving known input
 *   audio and video frames to the roq.exe encoder and analysing the
 *   resulting output text and RoQ file.  No decompiling of the Quake III
 *   Arena game was required.
 *
 * You may freely use this source code.  I only ask that you reference its
 * source in your projects documentation:
 *       Tim Ferguson: http://www.csse.monash.edu.au/~timf/
 * ------------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"
#include "xineutils.h"

#define VIDEOBUFSIZE 128*1024

#define RoQ_INFO              0x1001
#define RoQ_QUAD_CODEBOOK     0x1002
#define RoQ_QUAD_VQ           0x1011
#define RoQ_SOUND_MONO        0x1020
#define RoQ_SOUND_STEREO      0x1021

#define RoQ_ID_MOT              0x00
#define RoQ_ID_FCC              0x01
#define RoQ_ID_SLD              0x02
#define RoQ_ID_CCC              0x03

#define get_byte(in_buffer) *(in_buffer++)
#define get_word(in_buffer) ((unsigned short)(in_buffer += 2, \
  (in_buffer[-1] << 8 | in_buffer[-2])))
#define get_long(in_buffer) ((unsigned long)(in_buffer += 4, \
  (in_buffer[-1] << 24 | in_buffer[-2] << 16 | in_buffer[-3] << 8 | in_buffer[-4])))

typedef struct {
  unsigned char y0, y1, y2, y3, u, v;
} roq_cell;

typedef struct {
  int idx[4];
} roq_qcell;

typedef struct {
  video_decoder_class_t   decoder_class;
} roqvideo_class_t;

typedef struct roq_decoder_s {
  video_decoder_t   video_decoder;

  roqvideo_class_t *class;
  xine_stream_t    *stream;


  int               video_step;
  int               skipframes;
  unsigned char    *buf;
  int               bufsize;
  int               size;
  int               width;
  int               height;

  roq_cell          cells[256];
  roq_qcell         qcells[256];
  long              roq_start, aud_pos, vid_pos;
  long             *frame_offset;
  unsigned long     num_frames, num_audio_bytes;
  unsigned char    *y[2], *u[2], *v[2];
  int               y_size;
  int               c_size;

  unsigned char    *cur_y, *cur_u, *cur_v;
  unsigned char    *prev_y, *prev_u, *prev_v;

  /* this is either 0 or 1 indicating the cur_y points to y[0] or y[1],
   * same for u and v */
  int               current_planes;

} roqvideo_decoder_t;

/**************************************************************************
 * RoQ video specific decode functions
 *************************************************************************/

static void apply_vector_2x2(roqvideo_decoder_t *ri, int x, int y, roq_cell *cell) {
  unsigned char *yptr;

  yptr = ri->cur_y + (y * ri->width) + x;
  *yptr++ = cell->y0;
  *yptr++ = cell->y1;
  yptr += (ri->width - 2);
  *yptr++ = cell->y2;
  *yptr++ = cell->y3;
  ri->cur_u[(y/2) * (ri->width/2) + x/2] = cell->u;
  ri->cur_v[(y/2) * (ri->width/2) + x/2] = cell->v;
}

static void apply_vector_4x4(roqvideo_decoder_t *ri, int x, int y, roq_cell *cell) {
  unsigned long row_inc, c_row_inc;
  register unsigned char y0, y1, u, v;
  unsigned char *yptr, *uptr, *vptr;

  yptr = ri->cur_y + (y * ri->width) + x;
  uptr = ri->cur_u + (y/2) * (ri->width/2) + x/2;
  vptr = ri->cur_v + (y/2) * (ri->width/2) + x/2;

  row_inc = ri->width - 4;
  c_row_inc = (ri->width/2) - 2;
  *yptr++ = y0 = cell->y0; *uptr++ = u = cell->u; *vptr++ = v = cell->v;
  *yptr++ = y0;
  *yptr++ = y1 = cell->y1; *uptr++ = u; *vptr++ = v;
  *yptr++ = y1;

  yptr += row_inc;

  *yptr++ = y0;
  *yptr++ = y0;
  *yptr++ = y1;
  *yptr++ = y1;

  yptr += row_inc; uptr += c_row_inc; vptr += c_row_inc;

  *yptr++ = y0 = cell->y2; *uptr++ = u; *vptr++ = v;
  *yptr++ = y0;
  *yptr++ = y1 = cell->y3; *uptr++ = u; *vptr++ = v;
  *yptr++ = y1;

  yptr += row_inc;

  *yptr++ = y0;
  *yptr++ = y0;
  *yptr++ = y1;
  *yptr++ = y1;
}

static void apply_motion_4x4(roqvideo_decoder_t *ri, int x, int y, unsigned char mv,
  char mean_x, char mean_y)
{
  int i, mx, my;
  unsigned char *pa, *pb;

  mx = x + 8 - (mv >> 4) - mean_x;
  my = y + 8 - (mv & 0xf) - mean_y;

  pa = ri->cur_y + (y * ri->width) + x;
  pb = ri->prev_y + (my * ri->width) + mx;
  for(i = 0; i < 4; i++) {
    pa[0] = pb[0];
    pa[1] = pb[1];
    pa[2] = pb[2];
    pa[3] = pb[3];
    pa += ri->width;
    pb += ri->width;
  }

  pa = ri->cur_u + (y/2) * (ri->width/2) + x/2;
  pb = ri->prev_u + (my/2) * (ri->width/2) + (mx + 1)/2;
  for(i = 0; i < 2; i++) {
    pa[0] = pb[0];
    pa[1] = pb[1];
    pa += ri->width/2;
    pb += ri->width/2;
  }

  pa = ri->cur_v + (y/2) * (ri->width/2) + x/2;
  pb = ri->prev_v + (my/2) * (ri->width/2) + (mx + 1)/2;
  for(i = 0; i < 2; i++) {
    pa[0] = pb[0];
    pa[1] = pb[1];
    pa += ri->width/2;
    pb += ri->width/2;
  }
}

static void apply_motion_8x8(roqvideo_decoder_t *ri, int x, int y, 
  unsigned char mv, char mean_x, char mean_y) {

  int mx, my, i;
  unsigned char *pa, *pb;

  mx = x + 8 - (mv >> 4) - mean_x;
  my = y + 8 - (mv & 0xf) - mean_y;

  pa = ri->cur_y + (y * ri->width) + x;
  pb = ri->prev_y + (my * ri->width) + mx;
  for(i = 0; i < 8; i++) {
    pa[0] = pb[0];
    pa[1] = pb[1];
    pa[2] = pb[2];
    pa[3] = pb[3];
    pa[4] = pb[4];
    pa[5] = pb[5];
    pa[6] = pb[6];
    pa[7] = pb[7];
    pa += ri->width;
    pb += ri->width;
  }

  pa = ri->cur_u + (y/2) * (ri->width/2) + x/2;
  pb = ri->prev_u + (my/2) * (ri->width/2) + (mx + 1)/2;
  for(i = 0; i < 4; i++) {
    pa[0] = pb[0];
    pa[1] = pb[1];
    pa[2] = pb[2];
    pa[3] = pb[3];
    pa += ri->width/2;
    pb += ri->width/2;
  }

  pa = ri->cur_v + (y/2) * (ri->width/2) + x/2;
  pb = ri->prev_v + (my/2) * (ri->width/2) + (mx + 1)/2;
  for(i = 0; i < 4; i++) {
    pa[0] = pb[0];
    pa[1] = pb[1];
    pa[2] = pb[2];
    pa[3] = pb[3];
    pa += ri->width/2;
    pb += ri->width/2;
  }
}

static void roqvideo_decode_frame(roqvideo_decoder_t *ri) {
  unsigned int chunk_id = 0, chunk_arg = 0;
  unsigned long chunk_size = 0;
  int i, j, k, nv1, nv2, vqflg = 0, vqflg_pos = -1;
  int vqid, bpos, xpos, ypos, xp, yp, x, y;
  int frame_stats[2][4] = {{0},{0}};
  roq_qcell *qcell;
  unsigned char *buf = ri->buf;
  unsigned char *buf_end = ri->buf + ri->size;

  while (buf < buf_end) {
    chunk_id = get_word(buf);
    chunk_size = get_long(buf);
    chunk_arg = get_word(buf);

    if(chunk_id == RoQ_QUAD_VQ) 
      break;
    if(chunk_id == RoQ_QUAD_CODEBOOK) {
      if((nv1 = chunk_arg >> 8) == 0) 
        nv1 = 256;
      if((nv2 = chunk_arg & 0xff) == 0 && nv1 * 6 < chunk_size) 
        nv2 = 256;
      for(i = 0; i < nv1; i++) {
        ri->cells[i].y0 = get_byte(buf);
        ri->cells[i].y1 = get_byte(buf);
        ri->cells[i].y2 = get_byte(buf);
        ri->cells[i].y3 = get_byte(buf);
        ri->cells[i].u = get_byte(buf);
        ri->cells[i].v = get_byte(buf);
      }
      for(i = 0; i < nv2; i++)
        for(j = 0; j < 4; j++) 
        ri->qcells[i].idx[j] = get_byte(buf);
    }
  }

  bpos = xpos = ypos = 0;
  while(bpos < chunk_size) {
    for (yp = ypos; yp < ypos + 16; yp += 8)
      for (xp = xpos; xp < xpos + 16; xp += 8) {
        if (vqflg_pos < 0) {
          vqflg = buf[bpos++]; vqflg |= (buf[bpos++] << 8);
          vqflg_pos = 7;
        }
        vqid = (vqflg >> (vqflg_pos * 2)) & 0x3;
        frame_stats[0][vqid]++;
        vqflg_pos--;

        switch(vqid) {
        case RoQ_ID_MOT: 
          apply_motion_8x8(ri, xp, yp, 0, 8, 8);
          break;
        case RoQ_ID_FCC:
          apply_motion_8x8(ri, xp, yp, buf[bpos++], chunk_arg >> 8, 
            chunk_arg & 0xff);
          break;
        case RoQ_ID_SLD:
          qcell = ri->qcells + buf[bpos++];
          apply_vector_4x4(ri, xp, yp, ri->cells + qcell->idx[0]);
          apply_vector_4x4(ri, xp+4, yp, ri->cells + qcell->idx[1]);
          apply_vector_4x4(ri, xp, yp+4, ri->cells + qcell->idx[2]);
          apply_vector_4x4(ri, xp+4, yp+4, ri->cells + qcell->idx[3]);
          break;
        case RoQ_ID_CCC:
          for (k = 0; k < 4; k++) {
            x = xp; y = yp;
            if(k & 0x01) x += 4;
            if(k & 0x02) y += 4;

            if (vqflg_pos < 0) {
              vqflg = buf[bpos++];
              vqflg |= (buf[bpos++] << 8);
              vqflg_pos = 7;
            }
            vqid = (vqflg >> (vqflg_pos * 2)) & 0x3;
            frame_stats[1][vqid]++;
            vqflg_pos--;
            switch(vqid) {
            case RoQ_ID_MOT: 
              apply_motion_4x4(ri, x, y, 0, 8, 8);
              break;
            case RoQ_ID_FCC:
              apply_motion_4x4(ri, x, y, buf[bpos++], chunk_arg >> 8,
                chunk_arg & 0xff);
              break;
            case RoQ_ID_SLD:
              qcell = ri->qcells + buf[bpos++];
              apply_vector_2x2(ri, x, y, ri->cells + qcell->idx[0]);
              apply_vector_2x2(ri, x+2, y, ri->cells + qcell->idx[1]);
              apply_vector_2x2(ri, x, y+2, ri->cells + qcell->idx[2]);
              apply_vector_2x2(ri, x+2, y+2, ri->cells + qcell->idx[3]);
              break;
            case RoQ_ID_CCC:
              apply_vector_2x2(ri, x, y, ri->cells + buf[bpos]);
              apply_vector_2x2(ri, x+2, y, ri->cells + buf[bpos+1]);
              apply_vector_2x2(ri, x, y+2, ri->cells + buf[bpos+2]);
              apply_vector_2x2(ri, x+2, y+2, ri->cells + buf[bpos+3]);
              bpos += 4;
              break;
            }
          }
          break;
          default:
            printf("Unknown vq code: %d\n", vqid);
        }
      }

      xpos += 16;
      if (xpos >= ri->width) {
        xpos -= ri->width;
        ypos += 16;
      }
      if(ypos >= ri->height)
        break;
  }
}

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

static void roqvideo_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  roqvideo_decoder_t *this = (roqvideo_decoder_t *) this_gen;
  vo_frame_t *img; /* video out frame */

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_HEADER) { /* need to initialize */
    this->stream->video_out->open (this->stream->video_out);

    if(this->buf)
      free(this->buf);

    this->buf = xine_xmalloc(VIDEOBUFSIZE);
    this->bufsize = VIDEOBUFSIZE;
    this->size = 0;
    this->width = (buf->content[0] << 8) | buf->content[1];
    this->height = (buf->content[2] << 8) | buf->content[3];
    this->skipframes = 0;
    this->video_step = buf->decoder_info[1];
    this->current_planes = 0;

    this->y_size = this->width * this->height;
    this->c_size = (this->width * this->height) / 4;

    this->y[0] = xine_xmalloc(this->y_size);
    this->y[1] = xine_xmalloc(this->y_size);
    memset(this->y[0], 0x00, this->y_size);
    memset(this->y[1], 0x00, this->y_size);

    this->u[0] = xine_xmalloc(this->c_size);
    this->u[1] = xine_xmalloc(this->c_size);
    memset(this->u[0], 0x80, this->c_size);
    memset(this->u[1], 0x80, this->c_size);

    this->v[0] = xine_xmalloc(this->c_size);
    this->v[1] = xine_xmalloc(this->c_size);
    memset(this->v[0], 0x80, this->c_size);
    memset(this->v[1], 0x80, this->c_size);

    /* load the stream/meta info */
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC] = strdup("RoQ VQ Video");
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 1;

    return;
  }

  if( this->size + buf->size > this->bufsize ) {
    this->bufsize = this->size + 2 * buf->size;
    printf("RoQ: increasing source buffer to %d to avoid overflow.\n",
      this->bufsize);
    this->buf = realloc( this->buf, this->bufsize );
  }

  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE)
    this->video_step = buf->decoder_info[0];

  if (buf->decoder_flags & BUF_FLAG_FRAME_END)  { /* time to decode a frame */
    img = this->stream->video_out->get_frame (this->stream->video_out, 
      this->width, this->height, XINE_VO_ASPECT_SQUARE, XINE_IMGFMT_YV12,
      VO_BOTH_FIELDS);

    img->pts = buf->pts;
    img->duration = this->video_step;

    if (this->current_planes == 0) {
      this->cur_y = this->y[0];
      this->cur_u = this->u[0];
      this->cur_v = this->v[0];
      this->prev_y = this->y[1];
      this->prev_u = this->u[1];
      this->prev_v = this->v[1];
      this->current_planes = 1;
    } else {
      this->cur_y = this->y[1];
      this->cur_u = this->u[1];
      this->cur_v = this->v[1];
      this->prev_y = this->y[0];
      this->prev_u = this->u[0];
      this->prev_v = this->v[0];
      this->current_planes = 0;
    }
    roqvideo_decode_frame(this);
    xine_fast_memcpy(img->base[0], this->cur_y, this->y_size);
    xine_fast_memcpy(img->base[1], this->cur_u, this->c_size);
    xine_fast_memcpy(img->base[2], this->cur_v, this->c_size);

    if (img->copy) {
      int height = img->height;
      uint8_t *src[3];

      src[0] = img->base[0];
      src[1] = img->base[1];
      src[2] = img->base[2];

      while ((height -= 16) >= 0) {
	img->copy(img, src);
	src[0] += 16 * img->pitches[0];
	src[1] +=  8 * img->pitches[1];
	src[2] +=  8 * img->pitches[2];
      }
    }

    this->skipframes = img->draw(img);
    if( this->skipframes < 0 )
      this->skipframes = 0;
    img->free(img);

    this->size = 0;
  }
}

static void roqvideo_flush (video_decoder_t *this_gen) {
}

static void roqvideo_reset (video_decoder_t *this_gen) {
}

static void roqvideo_dispose (video_decoder_t *this_gen) {

  roqvideo_decoder_t *this = (roqvideo_decoder_t *) this_gen;

  this->stream->video_out->close(this->stream->video_out);

  free(this->y[0]);
  free(this->y[1]);
  free(this->u[0]);
  free(this->u[1]);
  free(this->v[0]);
  free(this->v[1]);

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  roqvideo_decoder_t  *this ;

  this = (roqvideo_decoder_t *) xine_xmalloc (sizeof (roqvideo_decoder_t));

  this->video_decoder.decode_data         = roqvideo_decode_data;
  this->video_decoder.flush               = roqvideo_flush;
  this->video_decoder.reset               = roqvideo_reset;
  this->video_decoder.dispose             = roqvideo_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (roqvideo_class_t *) class_gen;

  this->buf           = NULL;

  return &this->video_decoder;
}

static char *get_identifier (video_decoder_class_t *this) {
  return "RoQ Video";
}

static char *get_description (video_decoder_class_t *this) {
  return "Id RoQ video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  roqvideo_class_t *this;

  this = (roqvideo_class_t *) xine_xmalloc (sizeof (roqvideo_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t video_types[] = { 
  BUF_VIDEO_ROQ,
  0
 };

static decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 11, "roq", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
