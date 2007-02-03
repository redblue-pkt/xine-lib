/* 
 * Copyright (C) 2000-2007 the xine project
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
 * $Id: audio_pulse_out.c,v 1.11 2007/02/03 10:46:14 dgp85 Exp $
 *
 * ao plugin for pulseaudio (rename of polypaudio):
 * http://0pointer.de/lennart/projects/pulsaudio/
 *
 * originally written for polypaudio simple api. Lennart then suggested
 * using the async api for better control (such as volume), therefore, a lot
 * of this code comes from Lennart's patch to mplayer.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>

#include <pulse/pulseaudio.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "audio_out.h"
#include "bswap.h"

#define GAP_TOLERANCE        AO_MAX_GAP

/* CHECKME: should this be conditional on autotools? */
extern const char *__progname;

typedef struct pulse_driver_s {

  ao_driver_t    ao_driver;

  xine_t        *xine;

  /** The host to connect to */
  char *host;

  /** The sink to connect to */
  char *sink;

  /** Pulseaudio playback stream object */
  struct pa_stream *stream;

  /** Pulseaudio connection context */
  struct pa_context *context;

  /** Main event loop object */
  struct pa_threaded_mainloop *mainloop;

  pa_volume_t    swvolume;
  pa_cvolume     cvolume;

  int            capabilities;
  int            mode;

  int32_t        sample_rate;
  uint32_t       num_channels;
  uint32_t       bits_per_sample;
  uint32_t       bytes_per_frame;

  uint32_t       frames_written;

} pulse_driver_t;

typedef struct {
  audio_driver_class_t  driver_class;

  xine_t               *xine;

  /** Pulseaudio connection context */
  struct pa_context *context;

  /** Main event loop object */
  struct pa_threaded_mainloop *mainloop;
} pulse_class_t;

/**
 * @brief Callback function called when a stream operation succeed
 * @param stream Stream which operation has succeeded
 * @param success The success value for the operation (ignored)
 * @param this_Gen pulse_driver_t pointer for the PulseAudio output
 *        instance.
 */
static void __xine_pa_stream_success_callback(pa_stream *const stream, const int success,
					      void *const this_gen)
{
  pulse_driver_t *const this = (pulse_driver_t*)this_gen;

  _x_assert(stream); _x_assert(this);
  _x_assert(stream == this->stream);

  pa_threaded_mainloop_signal(this->mainloop, 0);
}

/**
 * @brief Callback function called when a context operation succeed
 * @param ctx Context which operation has succeeded
 * @param success The success value for the operation (ignored)
 * @param this_gen pulse_driver_t pointer for the PulseAudio output
 *        instance.
 */
static void __xine_pa_context_success_callback(pa_context *const ctx, const int success,
					      void *const this_gen)
{
  pulse_driver_t *const this = (pulse_driver_t*)this_gen;

  _x_assert(ctx); _x_assert(this);
  _x_assert(ctx == this->context);

  pa_threaded_mainloop_signal(this->mainloop, 0);
}

/**
 * @brief Callback function called when the information on the
 *        context's sink is retrieved.
 * @param ctx Context which operation has succeeded
 * @param info Structure containing the sink's information
 * @param this_gen pulse_driver_t pointer for the PulseAudio output
 *        instance.
 *
 * This function saves the volume field of the passed structure to the
 * @c cvolume variable of the output instance.
 */
static void __xine_pa_sink_info_callback(pa_context *const ctx, const pa_sink_input_info *const info,
					 const int is_last, void *const userdata) {

  pulse_driver_t *const this = (pulse_driver_t *) userdata;

  if (is_last < 0) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: Failed to get sink input info: %s\n",
             pa_strerror(pa_context_errno(this->context)));
    return;
  }

  if (!info)
      return;

  this->cvolume = info->volume;

  __xine_pa_context_success_callback(ctx, 0, this);
}

static int wait_for_operation(pulse_driver_t *this, pa_operation *o)
{
  _x_assert(this && o && this->mainloop);

  pa_threaded_mainloop_lock(this->mainloop);
  
  while (pa_operation_get_state(o) == PA_OPERATION_RUNNING)
    pa_threaded_mainloop_wait(this->mainloop);
  
  pa_threaded_mainloop_unlock(this->mainloop);

  return 0;
}

/*
 * open the audio device for writing to
 */
static int ao_pulse_open(ao_driver_t *this_gen,
		   uint32_t bits, uint32_t rate, int mode)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  struct pa_sample_spec ss;
  struct pa_buffer_attr a;
  pa_stream_state_t streamstate;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
	   "audio_pulse_out: ao_open bits=%d rate=%d, mode=%d\n", bits, rate, mode);

  if ( (mode & this->capabilities) == 0 ) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: unsupported mode %08x\n", mode);
    return 0;
  }

  if (this->stream) {

    if ( mode == this->mode && rate == this->sample_rate &&
         bits == this->bits_per_sample )
      return this->sample_rate;

    this_gen->close(this_gen);
  }

  this->mode                   = mode;
  this->sample_rate            = rate;
  this->bits_per_sample        = bits;
  this->num_channels           = _x_ao_mode2channels( mode );
  this->bytes_per_frame        = (this->bits_per_sample*this->num_channels)/8;

  ss.rate = rate;
  ss.channels = this->num_channels;
  switch (bits) {
    case 8:
      ss.format = PA_SAMPLE_U8;
      break;
    case 16:
#ifdef WORDS_BIGENDIAN
      ss.format = PA_SAMPLE_S16BE;
#else
      ss.format = PA_SAMPLE_S16LE;
#endif
      break;
    case 32:
      ss.format = PA_SAMPLE_FLOAT32;
      break;
  }

  if (!pa_sample_spec_valid(&ss)) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: Invalid sample spec\n");
    goto fail;
  }

  this->stream = pa_stream_new(this->context, "audio stream", &ss, NULL);
  _x_assert(this->stream);

  a.maxlength = pa_bytes_per_second(&ss)*1;
  a.tlength = a.maxlength*9/10;
  a.prebuf = a.tlength/2;
  a.minreq = a.tlength/10;

  pa_cvolume_set(&this->cvolume, pa_stream_get_sample_spec(this->stream)->channels, this->swvolume);
  pa_stream_connect_playback(this->stream, this->sink, &a,
                             PA_STREAM_INTERPOLATE_TIMING|PA_STREAM_AUTO_TIMING_UPDATE, 
                             &this->cvolume, NULL);

  do {
    xine_usec_sleep (100);

    streamstate = pa_stream_get_state(this->stream);
  } while (streamstate < PA_STREAM_READY);
     
  if (streamstate != PA_STREAM_READY) {
    xprintf (this->xine, XINE_VERBOSITY_LOG, "audio_pulse_out: Failed to connect to server: %s\n",
             pa_strerror(pa_context_errno(this->context)));
    goto fail;
  }
  this->frames_written = 0;

  return this->sample_rate;

fail:
  this_gen->close(this_gen);
  return 0;
}


static int ao_pulse_num_channels(ao_driver_t *this_gen)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_pulse_bytes_per_frame(ao_driver_t *this_gen)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  return this->bytes_per_frame;
}

static int ao_pulse_get_gap_tolerance (ao_driver_t *this_gen)
{
  return GAP_TOLERANCE;
}

static int ao_pulse_write(ao_driver_t *this_gen, int16_t *data,
                         uint32_t num_frames)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  int size = num_frames * this->bytes_per_frame;
  int ret = 0;
  
  _x_assert(this->stream && this->context);

  if (pa_stream_get_state(this->stream) == PA_STREAM_READY) {

    while (size > 0) {
      size_t l;

      while (!(l = pa_stream_writable_size(this->stream))) {
        xine_usec_sleep (10000);
      }

      if (l > size)
        l = size;
        
      pa_stream_write(this->stream, data, l, NULL, 0, PA_SEEK_RELATIVE);
      data = (int16_t *) ((uint8_t*) data + l);
      size -= l;
    }

    this->frames_written += num_frames;

    if (pa_stream_get_state(this->stream) == PA_STREAM_READY)
      ret = 1;
  }

  return ret;
}


static int ao_pulse_delay (ao_driver_t *this_gen)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  pa_usec_t latency = 0;
  int delay_frames;

  for (;;) {
    if (pa_stream_get_latency(this->stream, &latency, NULL) >= 0)
      break;

    if (pa_context_errno(this->context) != PA_ERR_NODATA) {
      /* error */
    }
  }

  /* convert latency (us) to frame units. */
  delay_frames = (int)(latency * this->sample_rate / 1000000);

  if( delay_frames > this->frames_written )
    return this->frames_written;
  else
    return delay_frames;
}

static void ao_pulse_close(ao_driver_t *this_gen)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  
  if (this->stream) {
    if (pa_stream_get_state(this->stream) == PA_STREAM_READY)
      wait_for_operation(this, pa_stream_drain(this->stream, __xine_pa_stream_success_callback, this));
    pa_stream_disconnect(this->stream);
    pa_stream_unref(this->stream);
    this->stream = NULL;
  }
}

static uint32_t ao_pulse_get_capabilities (ao_driver_t *this_gen) {
  pulse_driver_t *this = (pulse_driver_t *) this_gen;
  return this->capabilities;
}

static void ao_pulse_exit(ao_driver_t *this_gen)
{
  pulse_driver_t *this = (pulse_driver_t *) this_gen;

  free (this);
}

static int ao_pulse_get_property (ao_driver_t *this_gen, int property) {
  pulse_driver_t *this = (pulse_driver_t *) this_gen;

  switch(property) {
  case AO_PROP_PCM_VOL:
  case AO_PROP_MIXER_VOL:
    if( this->stream && this->context )
      wait_for_operation(this,
        pa_context_get_sink_input_info(this->context, pa_stream_get_index(this->stream),
				       __xine_pa_sink_info_callback, this));
    return (int) (pa_sw_volume_to_linear(this->swvolume)*100);
    break;
  case AO_PROP_MUTE_VOL:
    break;
  }
  
  return 0;
}

static int ao_pulse_set_property (ao_driver_t *this_gen, int property, int value) {
  pulse_driver_t *this = (pulse_driver_t *) this_gen;

  switch(property) {
  case AO_PROP_PCM_VOL:
  case AO_PROP_MIXER_VOL:
    this->swvolume = pa_sw_volume_from_linear((double)value/100.0);
    if( this->stream && this->context ) {
      pa_cvolume_set(&this->cvolume, pa_stream_get_sample_spec(this->stream)->channels, this->swvolume);
      wait_for_operation(this,
        pa_context_set_sink_input_volume(this->context, pa_stream_get_index(this->stream),
        &this->cvolume, __xine_pa_context_success_callback, this));
    }
    break;
  case AO_PROP_MUTE_VOL:
    break;
  }
  
  return 0;
}

static int ao_pulse_ctrl(ao_driver_t *this_gen, int cmd, ...) {
  pulse_driver_t *this = (pulse_driver_t *) this_gen;

  switch (cmd) {

  case AO_CTRL_PLAY_PAUSE:
    _x_assert(this->stream && this->context );
    if(pa_stream_get_state(this->stream) == PA_STREAM_READY)
      wait_for_operation(this,pa_stream_cork(this->stream, 1, __xine_pa_stream_success_callback, this));
    break;

  case AO_CTRL_PLAY_RESUME:
    _x_assert(this->stream && this->context);
    if(pa_stream_get_state(this->stream) == PA_STREAM_READY) {
        struct pa_operation *o2, *o1;
        o1 = pa_stream_prebuf(this->stream, __xine_pa_stream_success_callback, this);
	_x_assert(o1); wait_for_operation(this, o1);

        o2 = pa_stream_cork(this->stream, 0, __xine_pa_stream_success_callback, this);
        _x_assert(o2); wait_for_operation(this,o2);
    }
    break;

  case AO_CTRL_FLUSH_BUFFERS:
    _x_assert(this->stream && this->context);
    if(pa_stream_get_state(this->stream) == PA_STREAM_READY)
      wait_for_operation(this,pa_stream_flush(this->stream, __xine_pa_stream_success_callback, this));
    this->frames_written = 0;
    break;
  }

  return 0;
}

static ao_driver_t *open_plugin (audio_driver_class_t *class_gen, const void *data) {
  pulse_class_t   *class = (pulse_class_t *) class_gen;
  pulse_driver_t  *this;
  char hn[128];
  char *device;
  pa_context_state_t ctxstate;

  lprintf ("audio_pulse_out: open_plugin called\n");

  this = (pulse_driver_t *) xine_xmalloc (sizeof (pulse_driver_t));
  if (!this)
    return NULL;
  this->xine = class->xine;

  /*
   * set capabilities
   */
  this->capabilities = AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO | AO_CAP_MODE_4CHANNEL |
                       AO_CAP_MODE_4_1CHANNEL | AO_CAP_MODE_5CHANNEL |
                       AO_CAP_MODE_5_1CHANNEL | AO_CAP_MIXER_VOL |
                       AO_CAP_PCM_VOL | AO_CAP_MUTE_VOL | AO_CAP_8BITS |
                       AO_CAP_16BITS | AO_CAP_FLOAT32;

  this->sample_rate  = 0;
  this->swvolume = PA_VOLUME_NORM;
  
  this->ao_driver.get_capabilities    = ao_pulse_get_capabilities;
  this->ao_driver.get_property        = ao_pulse_get_property;
  this->ao_driver.set_property        = ao_pulse_set_property;
  this->ao_driver.open                = ao_pulse_open;
  this->ao_driver.num_channels        = ao_pulse_num_channels;
  this->ao_driver.bytes_per_frame     = ao_pulse_bytes_per_frame;
  this->ao_driver.delay               = ao_pulse_delay;
  this->ao_driver.write               = ao_pulse_write;
  this->ao_driver.close               = ao_pulse_close;
  this->ao_driver.exit                = ao_pulse_exit;
  this->ao_driver.get_gap_tolerance   = ao_pulse_get_gap_tolerance;
  this->ao_driver.control	      = ao_pulse_ctrl;

  device = this->xine->config->register_string(this->xine->config,
                                               "audio.pulseaudio_device",
                                               "",
                                               _("device used for pulseaudio"),
                                               _("use 'server[:sink]' for setting the "
                                                 "pulseaudio sink device."),
                                               10, NULL,
                                               NULL);

  if (device && strlen(device)) {
    int i = strcspn(device, ":");
    if (i >= sizeof(hn))
      i = sizeof(hn)-1;

    if (i > 0) {
      strncpy(this->host = hn, device, i);
      hn[i] = 0;
    }

    if (device[i] == ':')
      this->sink = device+i+1;
  }

  xprintf (class->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: host %s sink %s\n",
           this->host ? this->host : "(null)", this->sink ? this->sink : "(null)");

  this->mainloop = class->mainloop;
  this->context = class->context;

  if ( pa_context_get_state(this->context) != PA_CONTEXT_READY ){
    pa_context_connect(this->context, this->host, 1, NULL);

    do {
      xine_usec_sleep (100);
  
      ctxstate = pa_context_get_state(this->context);
    } while (ctxstate < PA_CONTEXT_READY);

    if (ctxstate != PA_CONTEXT_READY) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: Failed to connect to server: %s\n",
	       pa_strerror(pa_context_errno(this->context)));
      goto fail;
    }
  }

  return &this->ao_driver;

 fail:
  free(this);
  xprintf (class->xine, XINE_VERBOSITY_DEBUG, "audio_pulse_out: open_plugin failed.\n");
  return NULL;
}

/*
 * class functions
 */

static char* get_identifier (audio_driver_class_t *this_gen) {
  return "pulseaudio";
}

static char* get_description (audio_driver_class_t *this_gen) {
  return _("xine audio output plugin using pulseaudio sound server");
}

static void dispose_class (audio_driver_class_t *this_gen) {

  pulse_class_t *this = (pulse_class_t *) this_gen;

  pa_context_unref(this->context);

  pa_threaded_mainloop_stop(this->mainloop);
  pa_threaded_mainloop_free(this->mainloop);

  free (this);
}

static void *init_class (xine_t *xine, void *data) {

  pulse_class_t        *this;

  lprintf ("audio_pulse_out: init class\n");

  this = (pulse_class_t *) xine_xmalloc (sizeof (pulse_class_t));
  if (!this)
    return NULL;

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.get_identifier  = get_identifier;
  this->driver_class.get_description = get_description;
  this->driver_class.dispose         = dispose_class;

  this->xine                         = xine;

  this->mainloop = pa_threaded_mainloop_new();
  _x_assert(this->mainloop);

  pa_threaded_mainloop_start(this->mainloop);

  this->context = pa_context_new(pa_threaded_mainloop_get_api(this->mainloop), __progname);
  _x_assert(this->context);

  return this;
}

static const ao_info_t ao_info_pulse = {
  6
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_OUT, 8, "pulseaudio", XINE_VERSION_CODE, &ao_info_pulse, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

