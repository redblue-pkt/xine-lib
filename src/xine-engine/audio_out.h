/* 
 * Copyright (C) 2000-2001 the xine project
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
 * $Id: audio_out.h,v 1.3 2001/04/28 19:47:42 guenter Exp $
 */
#ifndef HAVE_AUDIO_OUT_H
#define HAVE_AUDIO_OUT_H

#include <inttypes.h>

#if defined(XINE_COMPILE)
#include "metronom.h"
#include "configfile.h"
#endif


#define AUDIO_OUT_IFACE_VERSION  1

/*
 * audio output modes  Used as Bitfield in AC3 decoder
 */

#define AO_MODE_AC3      1
#define AO_MODE_AC5      2
#define AO_MODE_MONO     4  /* 1 sample ==  2 bytes */
#define AO_MODE_STEREO   8  /* 1 sample ==  4 bytes */
#define AO_MODE_4CHANNEL 16 /* 1 sample ==  8 bytes */
#define AO_MODE_5CHANNEL 32 /* 1 sample == 10 bytes */

/*
 * ao_functions_s contains the functions every audio output
 * driver plugin has to implement.
 */

typedef struct ao_functions_s ao_functions_t;

struct ao_functions_s {

  /*
   * find out what output modes are supported by this plugin
   * (constants for the bit vector to return see above)
   */

  uint32_t (*get_supported_modes) (ao_functions_t *this);

  /*
   * connect this driver to the xine engine
   */
  void (*connect) (ao_functions_t *this, metronom_t *metronom);

  /*
   * open the driver and make it ready to receive audio data 
   * buffers may be flushed(!)
   *
   * return value: <=0 : failure, 1 : ok
   */

  int (*open)(ao_functions_t *this, uint32_t bits, uint32_t rate, int mode);

  /*
   * write audio data to output buffer - may block
   * audio driver must sync sample playback with metronom
   */

  void (*write_audio_data)(ao_functions_t *this,
			   int16_t* audio_data, uint32_t num_samples, 
			   uint32_t pts);

  /*
   * this is called when the decoder no longer uses the audio
   * output driver - the driver should get ready to get opened() again
   */

  void (*close)(ao_functions_t *this);

  /*
   * shut down this audio output driver plugin and
   * free all resources allocated
   */

  void (*exit) (ao_functions_t *this);

} ;


/*
 * to build a dynamic audio output plugin,
 * you have to implement these functions:
 *
 *
 * ao_functions_t *init_audio_out_plugin (config_values_t *config)
 *
 * init this plugin, check if device is available
 *
 * ao_info_t *get_audio_out_plugin_info ()
 *
 * peek at some (static) information about the plugin without initializing it
 *
 */

typedef struct ao_info_s {

  int     interface_version;
  char   *id;
  char   *description;
  int     priority;
} ao_info_t ;

#endif
