/* 
 * Copyright (C) 2000-2001 the xine project
 * 
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
 *
 * This file is part of xine, a unix video player.
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
 * $Id: xine_decoder.c,v 1.2 2001/07/04 20:32:29 uid32519 Exp $
 *
 * stuff needed to turn libspu into a xine decoder plugin
 */

/*
 * FIXME: libspu uses global variables (that are written to)
 */


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "spu.h"
#include "buffer.h"
#include "xine_internal.h"

#define FRAME_SIZE 4096


typedef struct spudec_decoder_s {
  spu_decoder_t    spu_decoder;

  uint32_t         pts;

  uint8_t          frame_buffer[FRAME_SIZE];
  uint8_t         *frame_ptr;
  int              sync_todo;
  int              frame_length, frame_todo;
  uint16_t         syncword;

  vo_instance_t   *vo_out;
  vo_overlay_t    *spu;
  int              spu_caps;
  int              bypass_mode;
  int              max_num_channels;
  int              output_sampling_rate;
  int              output_open;
  int              output_mode;

} spudec_decoder_t;

int spudec_can_handle (spu_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_SPU_PACKAGE) ;
}


void spudec_init (spu_decoder_t *this_gen, vo_instance_t *vo_out) {

  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
  printf("spudec_init %p\n",&vo_out);
  this->vo_out      = vo_out;
  this->spu_caps    = vo_out->get_capabilities(vo_out);
  this->syncword    = 0;
  this->sync_todo   = 6;
  this->output_open = 0;

//  spu_init ();

}

/* overlay_txt is just for test purposes */
u_int *overlay_txt (vo_overlay_t *spu, float o1)
{
  u_int x, y;
  u_char tmp;
  /*        u_char *clr_ptr1 = (u_char *) img1; */
  u_char *clr_ptr2;
  u_char *spu_data_ptr = (u_char *) spu->data;
  float o;
  
  /* don't know why this can happen - but it does happen */
  if ((spu->width <= 0) || (spu->height <= 0) ||
      (spu->width > 1024) || (spu->height > 1024)) {
    fprintf (stderr, "width || height out of range.\n");
    return NULL;
  }
  
  for (y = spu->y; y < (spu->height + spu->y); y++) {
    //     clr_ptr1 = (u_char *) (img1 + y * 720 + spu->x);
    for (x = spu->x; x < (spu->width + spu->x); x++) {
      o = ((float) (*spu_data_ptr>>4) / 15.0) * o1;
      //clr_ptr2 = (u_char *) &spu_clut[*spu_data_ptr&0x0f];
      *clr_ptr2 = *spu_data_ptr&0x0f;
      tmp=*spu_data_ptr;
      printf("%X%X",tmp&0x0f,((tmp>>4)&0x0f));
      spu_data_ptr ++;
      
      //   printf("%d ",(*clr_ptr2++));
      //   printf("%d ",(*clr_ptr2++));
      //   printf("%d ",(*clr_ptr2++));
      //   printf("%d \n",(*clr_ptr2++));
    }
    printf("\n");
  }
  
  return 0;
}

void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {

  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;

  uint8_t     *current = buf->content;
  /* uint8_t     *end = buf->content + buf->size; */

  printf ("spudec_decode_data\n");  

  if (!this->spu) {
    this->spu = this->vo_out->get_overlay (this->vo_out);
  }

  /* FIXME: shouldn't happen, but get_overlay function isn't implemented yet */
  if (!this->spu)
    return;

  if (!spuParseHdr (this->spu, current, buf->size)) {
    spuParseData (this->spu);
    printf("X=%d Y=%d w=%d h=%d\n",
	   this->spu->x,this->spu->y,
	   this->spu->width,this->spu->height);
    /* overlay_txt(this->spu,1.0); Just for test purposes */
    this->spu->PTS = buf->PTS;
    this->vo_out->queue_overlay (this->vo_out, this->spu);
    this->spu = NULL;
  }
  
}

void spudec_close (spu_decoder_t *this_gen) {

  /* spudec_decoder_t *this = (spudec_decoder_t *) this_gen;  */

//  if (this->output_open) 
//    this->spu_out->close (this->spu_out);

  /* close (spufile); */
}

static char *spudec_get_id(void) {
  return "spudec";
}

spu_decoder_t *init_spu_decoder_plugin (int iface_version, config_values_t *cfg) {

  spudec_decoder_t *this ;

  if (iface_version != 1)
    return NULL;

  this = (spudec_decoder_t *) malloc (sizeof (spudec_decoder_t));

  this->spu_decoder.interface_version   = 1;
  this->spu_decoder.can_handle          = spudec_can_handle;
  this->spu_decoder.init                = spudec_init;
  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.close               = spudec_close;
  this->spu_decoder.get_identifier      = spudec_get_id;
  
  return (spu_decoder_t *) this;
}

