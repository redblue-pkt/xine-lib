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

#define SPU_DECODER_IFACE_VERSION 14

/*
 * generic xine spu decoder plugin interface
 */

typedef struct spu_decoder_class_s spu_decoder_class_t;
typedef struct spu_decoder_s spu_decoder_t;

struct spu_decoder_class_s {

  /*
   * open a new instance of this plugin class
   */
  spu_decoder_t* (*open_plugin) (spu_decoder_class_t *this, xine_stream_t *stream);
  
  /*
   * return short, human readable identifier for this plugin class
   */
  char* (*get_identifier) (spu_decoder_class_t *this);

  /*
   * return human readable (verbose = 1 line) description for 
   * this plugin class
   */
  char* (*get_description) (spu_decoder_class_t *this);
  
  /*
   * free all class-related resources
   */
  void (*dispose) (spu_decoder_class_t *this);
};
  
 
struct spu_decoder_s {

  /*
   * decode data from buf and feed the overlay to overlay manager
   */  
  void (*decode_data) (spu_decoder_t *this, buf_element_t *buf);

  /*
   * reset decoder after engine flush (prepare for new
   * SPU data not related to recently decoded data)
   */
  void (*reset) (spu_decoder_t *this);
    
  /*
   * inform decoder that a time reference discontinuity has happened.
   * that is, it must forget any currently held pts value
   */
  void (*discontinuity) (spu_decoder_t *this);

  /*
   * close down, free all resources
   */
  void (*dispose) (spu_decoder_t *this);

  /*
   * When the SPU decoder also handles data used in user interaction,
   * you can query the related information here. The typical example
   * for this is DVD NAV packets which are handled by the SPU decoder
   * and can be received readily parsed from here.
   * The caller and the decoder must agree on the structure which is
   * passed here.
   * This function pointer may be NULL, if the plugin does not have
   * such functionality.
   */
  int  (*get_interact_info) (spu_decoder_t *this, void *data);

  /*
   * When the SPU decoder also handles menu overlays for user inter-
   * action, you can set a menu button here. The typical example for
   * this is DVD menus.
   * This function pointer may be NULL, if the plugin does not have
   * such functionality.
   */
  void (*set_button) (spu_decoder_t *this_gen, int32_t button, int32_t mode);

  void *node; /* used by plugin loader */
};

#endif /* HAVE_SPUDEC_H */
