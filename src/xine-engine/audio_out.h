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
 * $Id: audio_out.h,v 1.5 2001/06/18 10:49:31 guenter Exp $
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
 * ao_functions_s contains the functions every audio output
 * driver plugin has to implement.
 */

typedef struct ao_functions_s ao_functions_t;

struct ao_functions_s {

  /* 
   *
   * find out what output modes + capatilities are supported by 
   * this plugin (constants for the bit vector to return see above)
   *
   * See AO_CAP_* bellow.
   */
  uint32_t (*get_capabilities) (ao_functions_t *this);

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

  /*
   * Get, Set a property of audio driver.
   *
   * get_property() return 1 in success, 0 on failure.
   * set_property() return value on success, ~value on failure.
   *
   * See AC_PROP_* bellow for available properties.
   */
  int (*get_property) (ao_functions_t *this, int property);

  int (*set_property) (ao_functions_t *this,  int property, int value);

};


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

/*
 * audio output modes + capabilities
 */

#define AO_CAP_NOCAP            0x00000000 /* driver has no capabilities    */
#define AO_CAP_MODE_AC3         0x00000001 /* driver supports AC3 output    */
#define AO_CAP_MODE_AC5         0x00000002 /* driver supports AC5 output    */
/* 1 sample ==  2 bytes */
#define AO_CAP_MODE_MONO        0x00000004 /* driver supports mono output   */
 /* 1 sample ==  4 bytes */
#define AO_CAP_MODE_STEREO      0x00000008 /* driver supports stereo output */
 /* 1 sample ==  8 bytes */
#define AO_CAP_MODE_4CHANNEL    0x00000010 /* driver supports 4 channels    */
/* 1 sample == 10 bytes */
#define AO_CAP_MODE_5CHANNEL    0x00000020 /* driver supports 5 channels    */
#define AO_CAP_MIXER_VOL        0x00000040 /* driver supports mixer control */
#define AO_CAP_PCM_VOL          0x00000080 /* driver supports pcm control   */
#define AO_CAP_MUTE_VOL         0x00000100 /* driver can mute volume        */

/* properties supported by get/set_property() */
#define AO_PROP_MIXER_VOL       0
#define AO_PROP_PCM_VOL         1
#define AO_PROP_MUTE_VOL        2

typedef struct ao_info_s {

  int     interface_version;
  char   *id;
  char   *description;
  int     priority;
} ao_info_t ;

#endif
