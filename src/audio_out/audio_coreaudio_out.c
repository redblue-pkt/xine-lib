/* 
 * Copyright (C) 2000-2004 the xine project
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
 * done by Daniel Mack <xine@zonque.org>
 *
 * See http://developer.apple.com/technotes/tn2002/tn2091.html
 * and http://developer.apple.com/documentation/MusicAudio/Reference/CoreAudio/index.html
 * for conceptual documentation.
 *
 * The diffuculty here is that CoreAudio is pull-i/o while xine's internal
 * system works on push-i/o basis. So there is need of a buffer inbetween.
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

#include "xine_internal.h"
#include "xineutils.h"
#include "audio_out.h"

#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <AudioUnit/AUComponent.h>
#include <AudioUnit/AudioUnitProperties.h>
#include <AudioUnit/AudioUnitParameters.h>
#include <AudioUnit/AudioOutputUnit.h>

#define AO_OUT_COREAUDIO_IFACE_VERSION 8

#define GAP_TOLERANCE        AO_MAX_GAP 
#define BUFSIZE              0xffffff

typedef struct coreaudio_driver_s {

  ao_driver_t    ao_driver;

  xine_t        *xine;

  int            capabilities;

  int32_t        sample_rate;
  uint32_t       num_channels;
  uint32_t       bits_per_sample;
  uint32_t       bytes_per_frame;

  Component      au_component;
  Component      converter_component;
 
  AudioUnit      au_unit;
  AudioUnit      converter_unit;

  uint8_t        buf[BUFSIZE];
  uint32_t       buf_readpos;
  uint32_t       buf_writepos;
  uint32_t       last_block_size;
  
  pthread_mutex_t mutex;
  
} coreaudio_driver_t;

typedef struct {
  audio_driver_class_t  driver_class;

  config_values_t      *config;
  xine_t               *xine;
} coreaudio_class_t;

/* this function is called every time the CoreAudio sytem wants us to
 * supply some data */
static OSStatus ao_coreaudio_render_proc (coreaudio_driver_t *this,
                                          AudioUnitRenderActionFlags *ioActionFlags,
                                          const AudioTimeStamp *inTimeStamp,
                                          unsigned int inBusNumber,
                                          unsigned int inNumberFrames,
                                          AudioBufferList * ioData) {
    int32_t i = 0;
    int32_t req_size = 0;

    for (i = 0; i < ioData->mNumberBuffers; i++)
        req_size += ioData->mBuffers[i].mDataByteSize;

    if (this->buf_writepos - this->buf_readpos < req_size) {
        /* not enough data available, insert the sound of silence. */
        for (i = 0; i < ioData->mNumberBuffers; i++)
            memset (ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
        return noErr;
    }

    pthread_mutex_lock (&this->mutex);
    
    for (i = 0; i < ioData->mNumberBuffers; i++) {
        memcpy (ioData->mBuffers[i].mData, &this->buf[this->buf_readpos], 
                        ioData->mBuffers[i].mDataByteSize);
        this->buf_readpos += ioData->mBuffers[i].mDataByteSize;
    }

    this->last_block_size = req_size;
	
    pthread_mutex_unlock (&this->mutex);
    
    return noErr;
}

/*
 * open the audio device for writing to
 */
static int ao_coreaudio_open(ao_driver_t *this_gen, uint32_t bits, uint32_t rate, int mode)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  unsigned int err;
  /* CoreAudio and AudioUnit related stuff */
  AURenderCallbackStruct input;
  AudioStreamBasicDescription format;
  AudioUnitConnection connection;
  ComponentDescription desc;
  
  switch (mode) {
  case AO_CAP_MODE_MONO:
    this->num_channels = 1;
    break;
  case AO_CAP_MODE_STEREO:
    this->num_channels = 2;
    break;
  }

  this->sample_rate = rate;
  this->bits_per_sample = bits;
  this->capabilities = AO_CAP_16BITS | AO_CAP_MODE_STEREO | AO_CAP_MIXER_VOL;
  this->bytes_per_frame = this->num_channels * (bits / 8);
  this->buf_readpos = 0;
  this->buf_writepos = 0;
  this->last_block_size = 0;
  pthread_mutex_init (&this->mutex, NULL);

  xprintf (this->xine, XINE_VERBOSITY_DEBUG, 
           "audio_coreaudio_out: ao_open bits=%d rate=%d, mode=%d\n", bits, rate, mode);

  /* find an audio output unit */
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_DefaultOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  desc.componentFlags = 0;
  desc.componentFlagsMask = 0;
                  
  this->au_component = FindNextComponent (NULL, &desc);

  if (this->au_component == NULL) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, 
               "audio_coreaudio_out: Unable to find a usable audio output unit component\n");
      return 0;
  }
  
  OpenAComponent (this->au_component, &this->au_unit);
  
  /* find a converter unit */
  desc.componentType = kAudioUnitType_FormatConverter;
  desc.componentSubType = kAudioUnitSubType_AUConverter;

  this->converter_component = FindNextComponent (NULL, &desc);

  if (this->converter_component == NULL) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, 
               "audio_coreaudio_out: Unable to find a usable audio converter unit component\n");
      return 0;
  }
  
  OpenAComponent (this->converter_component, &this->converter_unit);

  /* set up the render procedure */
  input.inputProc = (AURenderCallback) ao_coreaudio_render_proc;
  input.inputProcRefCon = this;

  AudioUnitSetProperty (this->converter_unit,
                        kAudioUnitProperty_SetRenderCallback,
                        kAudioUnitScope_Input,
                        0, &input, sizeof(input));

  /* connect the converter unit to the audio output unit */
  connection.sourceAudioUnit = this->converter_unit;
  connection.sourceOutputNumber = 0;
  connection.destInputNumber = 0;
  AudioUnitSetProperty (this->au_unit,
                        kAudioUnitProperty_MakeConnection,
                        kAudioUnitScope_Input, 0, 
                        &connection, sizeof(connection));

  /* set up the audio format we want to use */
  format.mSampleRate   = rate;
  format.mFormatID     = kAudioFormatLinearPCM;
  format.mFormatFlags  = kLinearPCMFormatFlagIsSignedInteger
                       | kLinearPCMFormatFlagIsBigEndian
                       | kLinearPCMFormatFlagIsPacked;
  format.mBitsPerChannel   = this->bits_per_sample;
  format.mChannelsPerFrame = this->num_channels;
  format.mBytesPerFrame    = this->bytes_per_frame;
  format.mFramesPerPacket  = 1;
  format.mBytesPerPacket   = format.mBytesPerFrame;
 
  AudioUnitSetProperty (this->converter_unit,
                        kAudioUnitProperty_StreamFormat,
                        kAudioUnitScope_Input,
                        0, &format, sizeof (format));

  /* boarding completed, now initialize and start the units... */
  err = AudioUnitInitialize (this->converter_unit);
  if (err) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "audio_coreaudio_out: failed to AudioUnitInitialize(converter_unit)\n");
      return 0;
  }

  err = AudioUnitInitialize (this->au_unit);
  if (err) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "audio_coreaudio_out: failed to AudioUnitInitialize(au_unit)\n");
      return 0;
  }

  err = AudioOutputUnitStart (this->au_unit);
  if (err) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "audio_coreaudio_out: failed to AudioOutputUnitStart(au_unit)\n");
      return 0;
  }

  return rate;
}


static int ao_coreaudio_num_channels(ao_driver_t *this_gen)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
    return this->num_channels;
}

static int ao_coreaudio_bytes_per_frame(ao_driver_t *this_gen)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  return this->bytes_per_frame;
}

static int ao_coreaudio_get_gap_tolerance (ao_driver_t *this_gen)
{
  return GAP_TOLERANCE;
}

static int ao_coreaudio_write(ao_driver_t *this_gen, int16_t *data,
                         uint32_t num_frames)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;

  pthread_mutex_lock (&this->mutex);

  if (this->buf_readpos > BUFSIZE / 2) {
      memmove (this->buf, &this->buf[this->buf_readpos], 
                 (this->buf_writepos - this->buf_readpos));
      this->buf_writepos -= this->buf_readpos;
      this->buf_readpos = 0;
  }

  if (this->buf_writepos + (num_frames * this->bytes_per_frame) > BUFSIZE) {
      /* buffer overflow */
      printf ("CoreAudio: audio buffer overflow!\n");
      pthread_mutex_unlock (&this->mutex);
      return 1;
  }

  memcpy (&this->buf[this->buf_writepos], data, num_frames * this->bytes_per_frame);
  this->buf_writepos += num_frames * this->bytes_per_frame;
  
  pthread_mutex_unlock (&this->mutex);

  return 1;
}


static int ao_coreaudio_delay (ao_driver_t *this_gen)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  return (this->buf_writepos - this->buf_readpos + this->last_block_size) 
		  / this->bytes_per_frame;
}

static void ao_coreaudio_close(ao_driver_t *this_gen)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;

  if (this->au_unit) {
      AudioOutputUnitStop (this->au_unit);
      AudioUnitUninitialize (this->au_unit);
      this->au_unit = 0;
  }
  
  if (this->converter_unit) {
      AudioUnitUninitialize (this->converter_unit);
      this->converter_unit = 0;
  }
  
  if (this->au_component) {
      CloseAComponent (this->au_component);
      this->au_component = NULL;
  }

  if (this->converter_component) {
      CloseAComponent (this->converter_component);
      this->converter_component = NULL;
  }
}

static uint32_t ao_coreaudio_get_capabilities (ao_driver_t *this_gen) {
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  return this->capabilities;
}

static void ao_coreaudio_exit(ao_driver_t *this_gen)
{
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  
  ao_coreaudio_close(this_gen);

  free (this);
}

static int ao_coreaudio_get_property (ao_driver_t *this_gen, int property) {
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  Float32 val;

  switch(property) {
    case AO_PROP_PCM_VOL:
    case AO_PROP_MIXER_VOL:
        AudioUnitGetParameter (this->au_unit,
                               kHALOutputParam_Volume,
                               kAudioUnitScope_Output,
                               0, &val);
        return (int) (val * 100);
  }

  return 0;
}

static int ao_coreaudio_set_property (ao_driver_t *this_gen, int property, int value) {
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;
  Float32 val;

  switch(property) {
    case AO_PROP_PCM_VOL:
    case AO_PROP_MIXER_VOL:
        val = value / 100.0;
        AudioUnitSetParameter (this->au_unit,
                               kHALOutputParam_Volume,
                               kAudioUnitScope_Output,
                               0, val, 0);
        return value;
  }
  
  return ~value;
}

static int ao_coreaudio_ctrl(ao_driver_t *this_gen, int cmd, ...) {
  coreaudio_driver_t *this = (coreaudio_driver_t *) this_gen;

  switch (cmd) {

  case AO_CTRL_PLAY_PAUSE:
	AudioOutputUnitStop (this->au_unit);
    break;

  case AO_CTRL_PLAY_RESUME:
	AudioOutputUnitStart (this->au_unit);
    break;

  case AO_CTRL_FLUSH_BUFFERS:
	this->buf_readpos = 0;
	this->buf_writepos = 0;
    break;
  }

  return 0;
}

static ao_driver_t *open_plugin (audio_driver_class_t *class_gen, 
                                 const void *data) {

  coreaudio_class_t     *class = (coreaudio_class_t *) class_gen;
  /* config_values_t *config = class->config; */
  coreaudio_driver_t    *this;

  lprintf ("open_plugin called\n");

  this = (coreaudio_driver_t *) xine_xmalloc (sizeof (coreaudio_driver_t));

  this->xine = class->xine;
  this->capabilities = AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO;

  this->sample_rate  = 0;

  this->ao_driver.get_capabilities    = ao_coreaudio_get_capabilities;
  this->ao_driver.get_property        = ao_coreaudio_get_property;
  this->ao_driver.set_property        = ao_coreaudio_set_property;
  this->ao_driver.open                = ao_coreaudio_open;
  this->ao_driver.num_channels        = ao_coreaudio_num_channels;
  this->ao_driver.bytes_per_frame     = ao_coreaudio_bytes_per_frame;
  this->ao_driver.delay               = ao_coreaudio_delay;
  this->ao_driver.write               = ao_coreaudio_write;
  this->ao_driver.close               = ao_coreaudio_close;
  this->ao_driver.exit                = ao_coreaudio_exit;
  this->ao_driver.get_gap_tolerance   = ao_coreaudio_get_gap_tolerance;
  this->ao_driver.control             = ao_coreaudio_ctrl;

  return &this->ao_driver;
}

/*
 * class functions
 */

static char* get_identifier (audio_driver_class_t *this_gen) {
  return "coreaudio";
}

static char* get_description (audio_driver_class_t *this_gen) {
  return _("xine output plugin for Coreaudio/MacOSX");
}

static void dispose_class (audio_driver_class_t *this_gen) {

  coreaudio_class_t *this = (coreaudio_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {

  coreaudio_class_t        *this;

  lprintf ("init class\n");

  this = (coreaudio_class_t *) xine_xmalloc (sizeof (coreaudio_class_t));

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.get_identifier  = get_identifier;
  this->driver_class.get_description = get_description;
  this->driver_class.dispose         = dispose_class;

  this->config = xine->config;
  this->xine   = xine;

  return this;
}

static ao_info_t ao_info_coreaudio = {
  1
};

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_OUT, AO_OUT_COREAUDIO_IFACE_VERSION, "coreaudio", XINE_VERSION_CODE, &ao_info_coreaudio, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

