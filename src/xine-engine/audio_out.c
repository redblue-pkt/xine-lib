/* 
 * Copyright (C) 2000 the xine project
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
 * $Id: audio_out.c,v 1.1 2001/04/24 20:57:11 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#include <inttypes.h>
#include "audio_out.h"

const char *ao_available[] = {
  "null",
  "oss",
#ifdef HAVE_ALSA
  "alsa",
#endif
#ifdef HAVE_ESD
  "esd",
#endif
  NULL
};

ao_functions_t *gAudioOut;

/* ------------------------------------------------------------------------- */
/*
 *
 */
void audio_out_init(int driver) {
  
  switch(driver) {

  case AO_DRIVER_OSS:
    gAudioOut = audio_ossout_init();
    break;
  case AO_DRIVER_NULL:
    gAudioOut = NULL;
    break;
  case AO_DRIVER_UNSET:
#ifdef HAVE_ESD
    // Assume that the user wants ESD if ESPEAKER is set
    if(getenv("ESPEAKER") != NULL && (gAudioOut = audio_esdout_init()) != NULL) {
      printf("autodetected ESD audio driver\n");
      break;
    }
#endif
#ifdef HAVE_ALSA
    if((gAudioOut = audio_alsaout_init()) != NULL) { 
      printf("autodetected ALSA audio driver\n");
      break;
    }
#endif
    if ((gAudioOut = audio_ossout_init()) != NULL) {
      printf("autodetected OSS audio driver\n");
      break;
    }
    gAudioOut = NULL;
    break;
  default:
    fprintf(stderr, "audio_out: illegal driver (%d) selected\n"
	            "Audio output off.\n", driver);;
    break;
#ifdef HAVE_ALSA
  case AO_DRIVER_ALSA:
    gAudioOut = audio_alsaout_init();
    break;
#endif
#ifdef HAVE_ESD
  case AO_DRIVER_ESD:
    gAudioOut = audio_esdout_init();
    break;
#endif
  }

}

