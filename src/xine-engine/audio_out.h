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
 * $Id: audio_out.h,v 1.2 2001/04/27 10:42:38 f1rmb Exp $
 */
#ifndef HAVE_AUDIO_OUT_H
#define HAVE_AUDIO_OUT_H

#include <inttypes.h>

#if defined(XINE_COMPILE)
#include "metronom.h"
#include "configfile.h"
#endif


#define AUDIO_OUT_PLUGIN_IFACE_VERSION  1

/*
 * audio output modes  Used as Bitfield in AC3 decoder
 */

#define AO_MODE_AC3      1
#define AO_MODE_MONO     2  /* 1 sample ==  2 bytes */
#define AO_MODE_STEREO   4  /* 1 sample ==  4 bytes */
#define AO_MODE_4CHANNEL 8  /* 1 sample ==  8 bytes */
#define AO_MODE_5CHANNEL 16 /* 1 sample == 10 bytes */

typedef struct ao_functions_s
{

  /*
   * plugin interface version, lower versions _may_ be supported
   */

  int interface_version;

  /*
   * find out if desired output mode is supported by
   * this driver
   */

  int (*is_mode_supported) (int mode);

  /*
   * init device - buffer will be flushed(!)
   * return value: <=0 : failure, 1 : ok
   */

  int (*open)(metronom_t *metronom, uint32_t bits, uint32_t rate, int mode);

  /*
   * write audio data to output buffer - may block
   * audio driver must sync sample playback with metronom
   */

  void (*write_audio_data)(metronom_t *metronom, 
			   int16_t* audio_data, uint32_t num_samples, 
			   uint32_t pts);

  /*
   * close the audio driver
   */

  void (*close)(void);

  /*
   * return human readable identifier for this plugin
   */

  char* (*get_identifier) (void);

} ao_functions_t;

/*
 * available drivers:
 */

#define AO_DRIVER_UNSET -1
#define AO_DRIVER_NULL   0
#define AO_DRIVER_OSS    1
#if defined(HAVE_ALSA)
# define AO_DRIVER_ALSA  2
# if defined(HAVE_ESD)
#  define AO_DRIVER_ESD  3
# endif
#else /* no ALSA */
# if defined(HAVE_ESD)
#  define AO_DRIVER_ESD  2
# endif
#endif

/*
 * find right device driver, init it
 */

//ao_functions_t *ao_init(char *driver_name) ;

ao_functions_t *init_audio_out_plugin(int iface, config_values_t *cfg);

char *ao_get_available_drivers ();

#endif
