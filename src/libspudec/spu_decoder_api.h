/*
 * spu_decoder_api.h
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU Make; see the file COPYING. If not, write to
 * the Free Software Foundation, 
 *
 */

#ifndef HAVE_SPU_API_H
#define HAVE_SPU_API_H

 /*
 * generic xine spu decoder plugin interface
 *
 * for a dynamic plugin make sure you provide this function call:
 * spu_decoder_t *init_spu_decoder_plugin (int iface_version,
 *                                             config_values_t *cfg);
 */

typedef struct spu_decoder_s spu_decoder_t;

struct spu_decoder_s {

  int interface_version;

  int (*can_handle) (spu_decoder_t *this, int buf_type);

  void (*init) (spu_decoder_t *this, vo_instance_t *video_out);

  void (*decode_data) (spu_decoder_t *this, buf_element_t *buf);

  void (*event) (spu_decoder_t *this, spu_event_t *event);

  void (*close) (spu_decoder_t *this);

  char* (*get_identifier) (void);

  int priority;

  metronom_t *metronom;

};



typedef struct spudec_s spudec_t;

struct spudec_s {

  /*
   * reset spudec for a new stream
   *
   * clut     : pointer to array of 16 cluts for palette info 
   */

  void (*spudec_start) (spudec_t *this,	clut_t *clut);

};

#define SPU_EVENT_BUTTON 0x100
typedef struct spu_button_s spu_button_t;
struct spu_button_s {
  int show;
  uint8_t color[4];
  uint8_t trans[4];
  int left, right;
  int top, bottom;
};

#define SPU_EVENT_CLUT 0x101
typedef struct spu_cltbl_s spu_cltbl_t;
struct spu_cltbl_s {
  uint32_t clut[16];
};

#endif /* HAVE_SPUDEC_H */
