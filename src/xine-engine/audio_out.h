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
 * $Id: audio_out.h,v 1.38 2002/10/17 17:43:44 mroi Exp $
 */
#ifndef HAVE_AUDIO_OUT_H
#define HAVE_AUDIO_OUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

#if defined(XINE_COMPILE)
#include "metronom.h"
#include "configfile.h"
#else
#include <xine/metronom.h>
#include <xine/configfile.h>
#endif


#define AUDIO_OUT_IFACE_VERSION  5

/*
 * ao_driver_s contains the driver every audio output
 * driver plugin has to implement.
 */

struct xine_ao_driver_s {

  /* 
   *
   * find out what output modes + capatilities are supported by 
   * this plugin (constants for the bit vector to return see above)
   *
   * See AO_CAP_* bellow.
   */
  uint32_t (*get_capabilities) (xine_ao_driver_t *this);

  /*
   * open the driver and make it ready to receive audio data 
   * buffers may be flushed(!)
   *
   * return value: 0 : failure, >0 : output sample rate
   */
  int (*open)(xine_ao_driver_t *this, uint32_t bits, uint32_t rate, int mode);

  /* return the number of audio channels
   */
  int (*num_channels)(xine_ao_driver_t *self_gen);

  /* return the number of bytes per frame.
   * A frame is equivalent to one sample being output on every audio channel.
   */
  int (*bytes_per_frame)(xine_ao_driver_t *self_gen);

  /* return the delay is frames measured by 
   * looking at pending samples in the audio output device
   */
  int (*delay)(xine_ao_driver_t *self_gen);

  /* 
   * return gap tolerance (in pts) needed for this driver
   */
  int (*get_gap_tolerance) (xine_ao_driver_t *self_gen);

  /*
   * write audio data to output buffer 
   * audio driver must sync sample playback with metronom
   * return value: 
   *   1 => audio samples were processed ok
   *   0 => audio samples were not yet processed, 
   *        call write_audio_data with the _same_ samples again
   */
  int (*write)(xine_ao_driver_t *this,
	       int16_t* audio_data, uint32_t num_samples);

  /*
   * this is called when the decoder no longer uses the audio
   * output driver - the driver should get ready to get opened() again
   */
  void (*close)(xine_ao_driver_t *this);

  /*
   * shut down this audio output driver plugin and
   * free all resources allocated
   */
  void (*exit) (xine_ao_driver_t *this);

  /*
   * Get, Set a property of audio driver.
   *
   * get_property() return 1 in success, 0 on failure.
   * set_property() return value on success, ~value on failure.
   *
   * See AO_PROP_* below for available properties.
   */
  int (*get_property) (xine_ao_driver_t *this, int property);

  int (*set_property) (xine_ao_driver_t *this, int property, int value);


  /*
   * misc control operations on the audio device.
   *
   * See AO_CTRL_* below.
   */
  int (*control) (xine_ao_driver_t *this, int cmd, /* arg */ ...);

  void *node;
};

/*
 * ao_instance_s contains the instance every audio decoder talks to
 */
typedef struct audio_fifo_s audio_fifo_t;

typedef struct audio_buffer_s audio_buffer_t;

struct audio_buffer_s {

  audio_buffer_t    *next;

  int16_t           *mem;
  int                mem_size;
  int                num_frames;

  int64_t            vpts;
  uint32_t           frame_header_count;
  uint32_t           first_access_unit;
};

typedef struct ao_format_s ao_format_t;

struct ao_format_s {
  uint32_t bits; 
  uint32_t rate;
  int mode;
};

typedef struct ao_instance_s ao_instance_t;

struct ao_instance_s {
  uint32_t (*get_capabilities) (ao_instance_t *this); /* for constants see below */

  /*
   * Get/Set audio property
   *
   * See AO_PROP_* bellow
   */
  int (*get_property) (ao_instance_t *this, int property);
  int (*set_property) (ao_instance_t *this, int property, int value);

  /* open audio driver for audio output 
   * return value: 0:failure, >0:output sample rate
   */
  int (*open) (ao_instance_t *this,
	       uint32_t bits, uint32_t rate, int mode);

  /*
   * get a piece of memory for audio data 
   */

  audio_buffer_t * (*get_buffer) (ao_instance_t *this);

  /*
   * append a buffer filled with audio data to the audio fifo
   * for output
   */

  void (*put_buffer) (ao_instance_t *this, audio_buffer_t *buf);

  /* audio driver is no longer used by decoder => close */
  void (*close) (ao_instance_t *self);

  /* called on xine exit */
  void (*exit) (ao_instance_t *this);

  /*
   * misc control operations on the audio device.
   *
   * See AO_CTRL_* below.
   */
  int (*control) (ao_instance_t *this, int cmd, /* arg */ ...);

  
  /* private stuff */

  xine_ao_driver_t    *driver;
  pthread_mutex_t      driver_lock;
  metronom_t          *metronom;
  xine_stream_t       *stream;
  xine_t              *xine;

  int             audio_loop_running;
  int             audio_paused;
  pthread_t       audio_thread;

  int             audio_step;           /* pts per 32 768 samples (sample = #bytes/2) */
  int32_t         frames_per_kpts;      /* frames per 1024/90000 sec                  */
  
  ao_format_t     input, output;        /* format conversion done at audio_out.c */
  double          frame_rate_factor;
  
  int             resample_conf;
  int             force_rate;           /* force audio output rate to this value if non-zero */
  int             do_resample;
  int             gap_tolerance;
  audio_fifo_t   *free_fifo;
  audio_fifo_t   *out_fifo;
  int64_t         last_audio_vpts;
  
  audio_buffer_t *frame_buf[2];         /* two buffers for "stackable" conversions */
  int16_t        *zero_space;

  int64_t         passthrough_offset;
};

typedef struct audio_driver_class_s audio_driver_class_t;

struct audio_driver_class_s {

  /*
   * open a new instance of this plugin class
   */
  xine_ao_driver_t* (*open_plugin) (audio_driver_class_t *this, const void *data);
  
  /*
   * return short, human readable identifier for this plugin class
   */
  char* (*get_identifier) (audio_driver_class_t *this);

  /*
   * return human readable (verbose = 1 line) description for 
   * this plugin class
   */
  char* (*get_description) (audio_driver_class_t *this);

  /*
   * free all class-related resources
   */

  void (*dispose) (audio_driver_class_t *this);
};

/* 
 * this initiates the audio_out sync routines
 * found in ./src/xine-engine/audio_out.c
 */
ao_instance_t *ao_new_instance (xine_ao_driver_t *driver, 
				xine_stream_t *stream) ;

/*
 * audio output modes + capabilities
 */

#define AO_CAP_NOCAP            0x00000000 /* driver has no capabilities    */
#define AO_CAP_MODE_A52         0x00000001 /* driver supports A/52 output   */
#define AO_CAP_MODE_AC5         0x00000002 /* driver supports AC5 output    */
/* 1 sample ==  2 bytes (C)               */
#define AO_CAP_MODE_MONO        0x00000004 /* driver supports mono output   */
/* 1 sample ==  4 bytes (L,R)             */
#define AO_CAP_MODE_STEREO      0x00000008 /* driver supports stereo output */
/* 1 sample ==  8 bytes (L,R,LR,RR)       */
#define AO_CAP_MODE_4CHANNEL    0x00000010 /* driver supports 4 channels    */
/* 1 sample == 10 bytes (L,R,LR,RR,C)     */
#define AO_CAP_MODE_5CHANNEL    0x00000020 /* driver supports 5 channels    */
/* 1 sample == 12 bytes (L,R,LR,RR,C,LFE) */
#define AO_CAP_MODE_5_1CHANNEL  0x00000040 /* driver supports 5.1 channels  */
#define AO_CAP_MIXER_VOL        0x00000080 /* driver supports mixer control */
#define AO_CAP_PCM_VOL          0x00000100 /* driver supports pcm control   */
#define AO_CAP_MUTE_VOL         0x00000200 /* driver can mute volume        */
#define AO_CAP_8BITS            0x00000400 /* driver support 8-bit samples  */

/* properties supported by get/set_property() */
#define AO_PROP_MIXER_VOL       0
#define AO_PROP_PCM_VOL         1
#define AO_PROP_MUTE_VOL        2


/* audio device control ops */
#define AO_CTRL_PLAY_PAUSE	0
#define AO_CTRL_PLAY_RESUME	1
#define	AO_CTRL_FLUSH_BUFFERS	2

/* above that value audio frames are discarded */
#define AO_MAX_GAP              15000

#ifdef __cplusplus
}
#endif

#endif
