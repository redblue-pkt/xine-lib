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
 * Id CIN Video Decoder by Dr. Tim Ferguson. For more information about
 * the Id CIN format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 * 
 * $Id: idcinvideo.c,v 1.8 2002/11/12 18:40:54 miguelfreitas Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "buffer.h"
#include "xine_internal.h"
#include "video_out.h"
#include "xineutils.h"
#include "bswap.h"

#define VIDEOBUFSIZE 128*1024

typedef struct {
  video_decoder_class_t   decoder_class;
} idcinvideo_class_t;

typedef struct idcinvideo_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  idcinvideo_class_t *class;
  xine_stream_t    *stream;

  /* these are traditional variables in a video decoder object */
  uint64_t          video_step;  /* frame duration in pts units */
  int               decoder_ok;  /* current decoder status */
  int               skipframes;

  unsigned char    *buf;         /* the accumulated buffer data */
  int               bufsize;     /* the maximum size of buf */
  int               size;        /* the current size of buf */

  int               width;       /* the width of a video frame */
  int               height;      /* the height of a video frame */

  unsigned char     yuv_palette[256 * 4];
  yuv_planes_t      yuv_planes;
  
} idcinvideo_decoder_t;

/**************************************************************************
 * idcinvideo specific decode functions
 *************************************************************************/

#define HUF_TOKENS 256

typedef struct
{
  long rate;
  long width;
  long channels;
} wavinfo_t;

typedef struct
{
  int count;
  unsigned char used;
  int children[2];
} hnode_t;

static hnode_t huff_nodes[256][HUF_TOKENS*2];
static int num_huff_nodes[256];

/*
 *  Decodes input Huffman data using the Huffman table.
 */
void huff_decode(idcinvideo_decoder_t *this) {
  hnode_t *hnodes;
  long i;
  int prev;
  unsigned char v = 0;
  int bit_pos, node_num, dat_pos;
  int plane_ptr = 0;

  prev = bit_pos = dat_pos = 0;
  for(i = 0; i < (this->width * this->height); i++) {
    node_num = num_huff_nodes[prev];
    hnodes = huff_nodes[prev];

    while(node_num >= HUF_TOKENS) {
      if(!bit_pos) {
        if(dat_pos > this->size) {
          printf("Huffman decode error.\n");
          return;
        }
        bit_pos = 8;
        v = this->buf[dat_pos++];
      }

      node_num = hnodes[node_num].children[v & 0x01];
      v = v >> 1;
      bit_pos--;
    }

    this->yuv_planes.y[plane_ptr] = this->yuv_palette[node_num * 4 + 0];
    this->yuv_planes.u[plane_ptr] = this->yuv_palette[node_num * 4 + 1];
    this->yuv_planes.v[plane_ptr] = this->yuv_palette[node_num * 4 + 2];
    plane_ptr++;

    prev = node_num;
  }
}

/*
 *  Find the lowest probability node in a Huffman table, and mark it as
 *  being assigned to a higher probability.
 *  Returns the node index of the lowest unused node, or -1 if all nodes
 *  are used.
 */
int huff_smallest_node(hnode_t *hnodes, int num_hnodes) {
  int i;
  int best, best_node;

  best = 99999999;
  best_node = -1;
  for(i = 0; i < num_hnodes; i++) {
    if(hnodes[i].used)
      continue;
    if(!hnodes[i].count)
      continue;
    if(hnodes[i].count < best) {
      best = hnodes[i].count;
      best_node = i;
    }
  }

  if(best_node == -1) 
    return -1;
  hnodes[best_node].used = 1;
  return best_node;
}

/*
 *  Build the Huffman tree using the generated/loaded probabilities histogram.
 *
 *  On completion:
 *   huff_nodes[prev][i < HUF_TOKENS] - are the nodes at the base of the tree.
 *   huff_nodes[prev][i >= HUF_TOKENS] - are used to construct the tree.
 *   num_huff_nodes[prev] - contains the index to the root node of the tree.
 *     That is: huff_nodes[prev][num_huff_nodes[prev]] is the root node.
 */
void huff_build_tree(int prev) {
  hnode_t *node, *hnodes;
  int num_hnodes, i;

  num_hnodes = HUF_TOKENS;
  hnodes = huff_nodes[prev];
  for(i = 0; i < HUF_TOKENS * 2; i++) 
    hnodes[i].used = 0;

  while (1) {
    node = &hnodes[num_hnodes];             /* next free node */

    /* pick two lowest counts */
    node->children[0] = huff_smallest_node(hnodes, num_hnodes);
    if(node->children[0] == -1) 
      break;      /* reached the root node */

    node->children[1] = huff_smallest_node(hnodes, num_hnodes);
    if(node->children[1] == -1) 
      break;      /* reached the root node */

    /* combine nodes probability for new node */
    node->count = hnodes[node->children[0]].count +
      hnodes[node->children[1]].count;
    num_hnodes++;
  }

  num_huff_nodes[prev] = num_hnodes - 1;
}


/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void idcinvideo_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  idcinvideo_decoder_t *this = (idcinvideo_decoder_t *) this_gen;
  palette_entry_t *palette;
  unsigned char *histograms;
  int i, j, histogram_index = 0;

  vo_frame_t *img; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  /* load the palette */
  if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
      (buf->decoder_info[1] == BUF_SPECIAL_PALETTE)) {
    palette = (palette_entry_t *)buf->decoder_info[3];
    for (i = 0; i < buf->decoder_info[2]; i++) {
      this->yuv_palette[i * 4 + 0] =
        COMPUTE_Y(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 1] =
        COMPUTE_U(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 2] =
        COMPUTE_V(palette[i].r, palette[i].g, palette[i].b);
    }
  }

  /* initialize the Huffman tables */
  if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
      (buf->decoder_info[1] == BUF_SPECIAL_IDCIN_HUFFMAN_TABLE)) {
    histograms = (unsigned char *)buf->decoder_info[2];
    for (i = 0; i < 256; i++) {
      for(j = 0; j < HUF_TOKENS; j++)
        huff_nodes[i][j].count = histograms[histogram_index++];
      huff_build_tree(i);
    }

  }

  if (buf->decoder_flags & BUF_FLAG_HEADER) { /* need to initialize */
    this->stream->video_out->open (this->stream->video_out);

    if(this->buf)
      free(this->buf);

    this->width = (buf->content[0] << 8) | buf->content[1];
    this->height = (buf->content[2] << 8) | buf->content[3];
    this->video_step = buf->decoder_info[1];

    if (this->buf)
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);
    this->size = 0;

    init_yuv_planes(&this->yuv_planes, this->width, this->height);

    this->stream->video_out->open (this->stream->video_out);
    this->decoder_ok = 1;

    /* load the stream/meta info */
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC] = strdup("Id CIN Video");
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 1;

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

      img = this->stream->video_out->get_frame (this->stream->video_out,
                                        this->width, this->height,
                                        42, XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

      huff_decode(this);
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
static void idcinvideo_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void idcinvideo_reset (video_decoder_t *this_gen) {
  idcinvideo_decoder_t *this = (idcinvideo_decoder_t *) this_gen;

  this->size = 0;
}

static void idcinvideo_discontinuity (video_decoder_t *this_gen) {
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void idcinvideo_dispose (video_decoder_t *this_gen) {

  idcinvideo_decoder_t *this = (idcinvideo_decoder_t *) this_gen;

  if (this->buf) {
    free (this->buf);
    this->buf = NULL;
  }

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->stream->video_out->close(this->stream->video_out);
  }

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  idcinvideo_decoder_t  *this ;

  this = (idcinvideo_decoder_t *) xine_xmalloc (sizeof (idcinvideo_decoder_t));

  this->video_decoder.decode_data         = idcinvideo_decode_data;
  this->video_decoder.flush               = idcinvideo_flush;
  this->video_decoder.reset               = idcinvideo_reset;
  this->video_decoder.discontinuity       = idcinvideo_discontinuity;
  this->video_decoder.dispose             = idcinvideo_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (idcinvideo_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  return &this->video_decoder;
}

static char *get_identifier (video_decoder_class_t *this) {
  return "Id CIN Video";
}

static char *get_description (video_decoder_class_t *this) {
  return "Id Quake II Cinematic video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  idcinvideo_class_t *this;

  this = (idcinvideo_class_t *) xine_xmalloc (sizeof (idcinvideo_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/* plugin catalog information */
static uint32_t supported_types[] = { BUF_VIDEO_IDCIN, 0 };

static decoder_info_t video_decoder_info = {
  supported_types,     /* supported types */
  5                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 12, "idcinvideo", XINE_VERSION_CODE, &video_decoder_info, &init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
