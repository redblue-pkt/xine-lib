/*
 * Copyright (C) 2000-2021 the xine project
 * Copyright (C) 2021 Petri Hintukainen <phintuka@users.sourceforge.net>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Audio output plugin for OpenSL ES
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <math.h>

#define LOG_MODULE "audio_opensles_out"

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/audio_out.h>

#include <SLES/OpenSLES.h>
#ifdef HAVE_SLES_OPENSLES_ANDROID_H
#  include <SLES/OpenSLES_Android.h>
#endif

#define AO_OUT_OPENSLES_IFACE_VERSION 9
#define OPENSLES_BUFFERS              250 /* max buffers, 10 ms each -> 2.5 seconds */


typedef struct opensles_driver_s {

  ao_driver_t    ao_driver;

  xine_t        *xine;

  int32_t        sample_rate;
  uint32_t       num_channels;
  uint32_t       bits_per_sample;
  uint32_t       bytes_per_frame;

  /* libOpenSLES.so */
  void          *hlib;

  SLInterfaceID  SL_IID_myBUFFERQUEUE;
  SLInterfaceID  SL_IID_VOLUME;
  SLInterfaceID  SL_IID_PLAY;

  /* OpenSL ES objects */
  SLObjectItf    engine_object;
  SLObjectItf    output_mix_object;
  SLObjectItf    player_object;

  /* OpenSL ES interfaces */
  SLEngineItf    engine_if;
  SLPlayItf      player_if;
  SLVolumeItf    volume_if;
#ifdef HAVE_SLES_OPENSLES_ANDROID_H
  SLAndroidSimpleBufferQueueItf buffer_if;
#else
  SLBufferQueueItf              buffer_if;
#endif

  /* queue */
  uint8_t *buf;            /* playback buffer */
  size_t   buf_elem_size;  /* size of single buffer chunk (10 ms) */
  size_t   next_buf;       /* next free buffer */
  size_t   buf_size;       /* bytes filled in next_buf (partial buffer) */
} opensles_driver_t;

typedef struct {
  audio_driver_class_t  driver_class;
  xine_t               *xine;
} opensles_class_t;


#define CHECK_OPENSL_RESULT(errmsg, erraction)                  \
  do {                                                          \
    if (result != SL_RESULT_SUCCESS) {                          \
      xprintf (this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "  \
               errmsg ": %" PRIu32 "\n", result);               \
      erraction ;                                               \
    }                                                           \
  } while (0)

static int _opensles_open(ao_driver_t *this_gen, uint32_t bits, uint32_t rate, int mode)
{
  opensles_driver_t *this = (opensles_driver_t *) this_gen;
  SLresult result;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": "
           "ao_open bits=%d rate=%d, mode=%d\n", bits, rate, mode);

  this->sample_rate            = rate;
  this->bits_per_sample        = bits;

  switch (mode) {
    case AO_CAP_MODE_MONO:
      this->num_channels = 1;
      break;
    case AO_CAP_MODE_STEREO:
      this->num_channels = 2;
      break;
    default:
      xprintf (this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
               "unsupported mode 0x%X\n", mode);
      return 0;
  }
  if (bits != 16) {
      xprintf (this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
               "unsupported bits per sample %d\n", bits);
      return 0;
  }

  this->bytes_per_frame = (bits * this->num_channels) / 8;

  /* create audio player */

  {
#ifdef HAVE_SLES_OPENSLES_ANDROID_H
    SLDataLocator_AndroidSimpleBufferQueue loc_bufqueue = {
      SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, OPENSLES_BUFFERS
    };
#else
    SLDataLocator_BufferQueue loc_bufqueue = {
      SL_DATALOCATOR_BUFFERQUEUE, OPENSLES_BUFFERS
    };
#endif
    SLDataFormat_PCM format = {
      .formatType       = SL_DATAFORMAT_PCM,
      .numChannels      = this->num_channels,
      .samplesPerSec    = rate * 1000,
      .bitsPerSample    = SL_PCMSAMPLEFORMAT_FIXED_16,
      .containerSize    = SL_PCMSAMPLEFORMAT_FIXED_16,
      .channelMask      = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
      .endianness       = SL_BYTEORDER_LITTLEENDIAN,
    };
    SLDataSource audio_src = { &loc_bufqueue, &format };

    SLDataLocator_OutputMix loc_outputmix = {
        SL_DATALOCATOR_OUTPUTMIX, this->output_mix_object
    };
    SLDataSink audio_sink = { &loc_outputmix, NULL};

    SLInterfaceID if_ids[] = { this->SL_IID_myBUFFERQUEUE, this->SL_IID_VOLUME };
    SLboolean     if_req[] = { SL_BOOLEAN_TRUE,            SL_BOOLEAN_TRUE };

    result = (*(this->engine_if))->CreateAudioPlayer(this->engine_if, &this->player_object, &audio_src,
                                                     &audio_sink, sizeof(if_ids) / sizeof(if_ids[0]),
                                                     if_ids, if_req);
    CHECK_OPENSL_RESULT("error creating player", return 0);
  }

  result = (*(this->player_object))->Realize(this->player_object, SL_BOOLEAN_FALSE);
  CHECK_OPENSL_RESULT("error realizing player", goto fail);

  result = (*(this->player_object))->GetInterface(this->player_object, this->SL_IID_PLAY, &this->player_if);
  CHECK_OPENSL_RESULT("error getting player interface", goto fail);

  result = (*(this->player_object))->GetInterface(this->player_object, this->SL_IID_VOLUME, &this->volume_if);
  CHECK_OPENSL_RESULT("error getting volume interface", goto fail);

  result = (*(this->player_object))->GetInterface(this->player_object, this->SL_IID_myBUFFERQUEUE, &this->buffer_if);
  CHECK_OPENSL_RESULT("error getting buffer interface", goto fail);

  result = (*(this->player_if))->SetPlayState(this->player_if, SL_PLAYSTATE_PLAYING);
  CHECK_OPENSL_RESULT("error setting playing state", goto fail);

  /* initialize buffer */

  this->buf_elem_size = this->bytes_per_frame * this->sample_rate / 100;
  this->buf_size = 0;
  this->next_buf = 0;

  free(this->buf);
  this->buf = malloc(this->buf_elem_size * OPENSLES_BUFFERS);
  if (!this->buf)
    goto fail;

  return this->sample_rate;

fail:
  this_gen->close(this_gen);
  return 0;
}


static int _opensles_num_channels(ao_driver_t *this_gen)
{
  opensles_driver_t *this = (opensles_driver_t *)this_gen;
  return this->num_channels;
}

static int _opensles_bytes_per_frame(ao_driver_t *this_gen)
{
  opensles_driver_t *this = (opensles_driver_t *)this_gen;
  return this->bytes_per_frame;
}

static int _opensles_get_gap_tolerance (ao_driver_t *this_gen)
{
  (void)this_gen;
  return AO_MAX_GAP;
}

static int _opensles_write(ao_driver_t *this_gen, int16_t *data, uint32_t num_frames)
{
  opensles_driver_t *this = (opensles_driver_t *)this_gen;
  SLresult result;
  size_t   need_bufs = (num_frames * this->bytes_per_frame + this->buf_elem_size - 1) / this->buf_elem_size;

  /* wait until we have enough free buffers */

  while (1) {
#ifdef HAVE_SLES_OPENSLES_ANDROID_H
    SLAndroidSimpleBufferQueueState st;
#else
    SLBufferQueueState              st;
#endif

    result = (*(this->buffer_if))->GetState(this->buffer_if, &st);
    CHECK_OPENSL_RESULT("error querying buffer state", return -1);

    if (st.count < OPENSLES_BUFFERS - 1 - need_bufs)
      break;

    xine_usec_sleep(10000);
  }

  /* copy to buffers and queue (full) buffers */

  while (num_frames > 0) {
    uint8_t *buf  = this->buf + this->next_buf * this->buf_elem_size;
    size_t   room = this->buf_elem_size - this->buf_size;
    if (room > num_frames * this->bytes_per_frame) {
      /* partial buffer, save data and return */
      memcpy(buf + this->buf_size, data, num_frames * this->bytes_per_frame);
      this->buf_size += num_frames * this->bytes_per_frame;
      return 1;
    }

    memcpy(buf + this->buf_size, data, room);

    result = (*(this->buffer_if))->Enqueue(this->buffer_if, buf, this->buf_elem_size);
    CHECK_OPENSL_RESULT("enque failed", (void)result );

    data += room * sizeof(uint16_t) / this->bytes_per_frame;
    num_frames -= room / this->bytes_per_frame;
    this->buf_size = 0;
    if (++this->next_buf >= OPENSLES_BUFFERS)
      this->next_buf = 0;
  }

  return 1;
}

static int _opensles_delay (ao_driver_t *this_gen)
{
  opensles_driver_t *this = (opensles_driver_t *)this_gen;
#ifdef HAVE_SLES_OPENSLES_ANDROID_H
  SLAndroidSimpleBufferQueueState st;
#else
  SLBufferQueueState              st;
#endif
  SLresult result;

  if (!this->buffer_if)
    return -1;

  result = (*(this->buffer_if))->GetState(this->buffer_if, &st);
  CHECK_OPENSL_RESULT("error querying buffer state", return -1);

  lprintf("latency %u frames\n", st.count * this->buf_elem_size / this->bytes_per_frame);

  return st.count * this->buf_elem_size / this->bytes_per_frame;
}

static void _opensles_close(ao_driver_t *this_gen)
{
  opensles_driver_t *this = (opensles_driver_t *)this_gen;

  if (this->player_if) {
    (*(this->player_if))->SetPlayState(this->player_if, SL_PLAYSTATE_STOPPED);
  }
  if (this->buffer_if) {
    (*(this->buffer_if))->Clear(this->buffer_if);
  }
  if (this->player_object) {
    (*(this->player_object))->Destroy(this->player_object);
  }

  this->player_object = NULL;
  this->buffer_if = NULL;
  this->volume_if = NULL;
  this->player_if = NULL;

  _x_freep(&this->buf);
}

static uint32_t _opensles_get_capabilities (ao_driver_t *this_gen)
{
  (void)this_gen;
  return AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO | AO_CAP_16BITS | AO_CAP_MIXER_VOL | AO_CAP_MUTE_VOL;
}

static void _opensles_exit(ao_driver_t *this_gen)
{
  opensles_driver_t *this = (opensles_driver_t *)this_gen;

  _opensles_close(this_gen);

  if (this->output_mix_object)
    (*(this->output_mix_object))->Destroy(this->output_mix_object);
  if (this->engine_object)
    (*(this->engine_object))->Destroy(this->engine_object);

  if (this->hlib) {
    dlclose(this->hlib);
    this->hlib = NULL;
  }

  free (this);
}

static int _opensles_get_property (ao_driver_t *this_gen, int property)
{
  opensles_driver_t *this = (opensles_driver_t *)this_gen;
  SLresult   result;
  SLboolean  b;
  SLmillibel millibels;

  if (!this->volume_if)
    return -1;

  switch (property) {
    case AO_PROP_PCM_VOL:
    case AO_PROP_MIXER_VOL:
      result = (*(this->volume_if))->GetVolumeLevel(this->volume_if, &millibels);
      CHECK_OPENSL_RESULT("error getting volume level", return -1);
      return  lroundf(100.0 * expf(millibels * logf(10.0) / 2000.0));
    break;

    case AO_PROP_MUTE_VOL:
      result = (*(this->volume_if))->GetMute(this->volume_if, &b);
      CHECK_OPENSL_RESULT("error getting mute state", return -1);
      return !!b;
  }

  return 0;
}

static int _opensles_set_property (ao_driver_t *this_gen, int property, int value)
{
  opensles_driver_t *this = (opensles_driver_t *)this_gen;
  SLresult   result;
  SLmillibel millibels;

  if (!this->volume_if)
    return -1;

  switch (property) {
    case AO_PROP_PCM_VOL:
    case AO_PROP_MIXER_VOL:

      millibels = lroundf(2000.0 * log10f( (value < 0 ? 0 : value) / 100.0));
      if (millibels < SL_MILLIBEL_MIN)
        millibels = SL_MILLIBEL_MIN;
      else if (millibels > 0)
        millibels = 0;

      result = (*(this->volume_if))->SetVolumeLevel(this->volume_if, millibels);
      CHECK_OPENSL_RESULT("error setting volume level", return -1);
      return value;

    case AO_PROP_MUTE_VOL:
      result = (*(this->volume_if))->SetMute(this->volume_if, !!value);
      CHECK_OPENSL_RESULT("error setting mute state", return -1);
      return value;
  }

  return -1;
}

static int _opensles_ctrl(ao_driver_t *this_gen, int cmd, ...)
{
  opensles_driver_t *this = (opensles_driver_t *)this_gen;
  SLresult result;

  switch (cmd) {

    case AO_CTRL_PLAY_PAUSE:
      if (this->player_if) {
        result = (*(this->player_if))->SetPlayState(this->player_if, SL_PLAYSTATE_PAUSED);
        CHECK_OPENSL_RESULT("failed pausing playback", return -1);
      }
      break;

    case AO_CTRL_PLAY_RESUME:
      if (this->player_if) {
        result = (*(this->player_if))->SetPlayState(this->player_if, SL_PLAYSTATE_PLAYING);
        CHECK_OPENSL_RESULT("failed resuming playback", return -1);
      }
      break;

    case AO_CTRL_FLUSH_BUFFERS:
      if (this->player_if) {
        (*(this->player_if))->SetPlayState(this->player_if, SL_PLAYSTATE_STOPPED);
        (*(this->buffer_if))->Clear(this->buffer_if);
        (*(this->player_if))->SetPlayState(this->player_if, SL_PLAYSTATE_PLAYING);
      }
      this->buf_size = 0;
      this->next_buf = 0;
      break;
  }

  return 0;
}

static int _dlsym_iid(opensles_driver_t *this, const char *name, SLInterfaceID *iid)
{
  SLInterfaceID *p = dlsym(this->hlib, name);
  if (!p) {
    xprintf (this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
             "dlsym(\'%s\') failed: %s\n", name, dlerror());
    return -1;
  }
  *iid = *p;
  return 0;
}

static ao_driver_t *_opensles_open_plugin (audio_driver_class_t *class_gen, const void *data)
{
  opensles_class_t  *class = (opensles_class_t *)class_gen;
  opensles_driver_t *this;

  SLresult           result;
  SLInterfaceID      SL_IID_ENGINE;
  SLresult         (*slCreateEngine)(SLObjectItf*, SLuint32, const SLEngineOption*, SLuint32,
                                     const SLInterfaceID*, const SLboolean*);

  lprintf ("open_plugin called\n");

  (void)data;
  this = calloc(1, sizeof (opensles_driver_t));
  if (!this)
    return NULL;

  this->xine = class->xine;

  /* Load OpenSL ES */

  this->hlib = dlopen("libOpenSLES.so", RTLD_NOW);
  if (!this) {
    xprintf (this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
             "error opening libOpenSLES.so: %s\n", dlerror());
    goto fail;
  }

  slCreateEngine = dlsym(this->hlib, "slCreateEngine");
  if (!slCreateEngine) {
    xprintf (this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
             "dlsym(\'slCreateEngine\') failed: %s\n", dlerror());
    goto fail;
  }

  if (_dlsym_iid(this, "SL_IID_ENGINE", &SL_IID_ENGINE) < 0)
    goto fail;
  if (_dlsym_iid(this, "SL_IID_PLAY", &this->SL_IID_PLAY) < 0)
    goto fail;
  if (_dlsym_iid(this, "SL_IID_VOLUME", &this->SL_IID_VOLUME) < 0)
    goto fail;
#ifdef HAVE_SLES_OPENSLES_ANDROID_H
  if (_dlsym_iid(this, "SL_IID_ANDROIDSIMPLEBUFFERQUEUE", &this->SL_IID_myBUFFERQUEUE) < 0)
    goto fail;
#else
  if (_dlsym_iid(this, "SL_IID_BUFFERQUEUE", &this->SL_IID_myBUFFERQUEUE) < 0)
    goto fail;
#endif

  /* create engine */

  result = slCreateEngine(&this->engine_object, 0, NULL, 0, NULL, NULL);
  CHECK_OPENSL_RESULT("error creating engine", goto fail);

  result = (*(this->engine_object))->Realize(this->engine_object, SL_BOOLEAN_FALSE);
  CHECK_OPENSL_RESULT("error realizing engine", goto fail);

  result = (*(this->engine_object))->GetInterface(this->engine_object, SL_IID_ENGINE, &this->engine_if);
  CHECK_OPENSL_RESULT("error getting engine interface", goto fail);

  /* init output mix */

  {
    const SLInterfaceID ids1[] = { this->SL_IID_VOLUME };
    const SLboolean req1[] = { SL_BOOLEAN_FALSE };

    result = (*(this->engine_if))->CreateOutputMix(this->engine_if, &this->output_mix_object, 1, ids1, req1);
    CHECK_OPENSL_RESULT("error creating output mix", goto fail);
  }

  result = (*(this->output_mix_object))->Realize(this->output_mix_object, SL_BOOLEAN_FALSE);
  CHECK_OPENSL_RESULT("error realizing output mix", goto fail);

  this->ao_driver.get_capabilities    = _opensles_get_capabilities;
  this->ao_driver.get_property        = _opensles_get_property;
  this->ao_driver.set_property        = _opensles_set_property;
  this->ao_driver.open                = _opensles_open;
  this->ao_driver.num_channels        = _opensles_num_channels;
  this->ao_driver.bytes_per_frame     = _opensles_bytes_per_frame;
  this->ao_driver.delay               = _opensles_delay;
  this->ao_driver.write               = _opensles_write;
  this->ao_driver.close               = _opensles_close;
  this->ao_driver.exit                = _opensles_exit;
  this->ao_driver.get_gap_tolerance   = _opensles_get_gap_tolerance;
  this->ao_driver.control             = _opensles_ctrl;

  return &this->ao_driver;

fail:
  _opensles_exit(&this->ao_driver);
  return NULL;
}

static void *_opensles_init_class (xine_t *xine, const void *data)
{
  opensles_class_t *this;

  (void)data;
  this = calloc(1, sizeof (opensles_class_t));
  if (!this)
    return NULL;

  this->xine = xine;

  this->driver_class.open_plugin     = _opensles_open_plugin;
  this->driver_class.identifier      = "opensles";
#ifdef HAVE_SLES_OPENSLES_ANDROID_H
  this->driver_class.description     = N_("OpenSL ES audio output plugin (Android)");
#else
  this->driver_class.description     = N_("OpenSL ES audio output plugin");
#endif
  this->driver_class.dispose         = default_audio_driver_class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

static const ao_info_t ao_info_opensles = {
  .priority = 5,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_OUT, AO_OUT_OPENSLES_IFACE_VERSION, "opensles", XINE_VERSION_CODE, &ao_info_opensles, _opensles_init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
