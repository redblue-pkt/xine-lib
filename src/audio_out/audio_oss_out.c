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
 * $Id: audio_oss_out.c,v 1.81 2002/11/16 11:39:10 mroi Exp $
 *
 * 20-8-2001 First implementation of Audio sync and Audio driver separation.
 * Copyright (C) 2001 James Courtier-Dutton James@superbug.demon.co.uk
 * 
 * General Programming Guidelines: -
 * New concept of an "audio_frame".
 * An audio_frame consists of all the samples required to fill every audio channel to a full amount of bits.
 * So, it does not mater how many bits per sample, or how many audio channels are being used, the number of audio_frames is the same.
 * E.g.  16 bit stereo is 4 bytes, but one frame.
 *       16 bit 5.1 surround is 12 bytes, but one frame.
 * The purpose of this is to make the audio_sync code a lot more readable, rather than having to multiply by the amount of channels all the time
 * when dealing with audio_bytes instead of audio_frames.
 *
 * The number of samples passed to/from the audio driver is also sent in units of audio_frames.
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
#if defined(__OpenBSD__)
# include <soundcard.h>
#elif defined (__FreeBSD__)
#  if __FreeBSD__ < 4
#   include <machine/soundcard.h>
#  else
#   include <sys/soundcard.h>
#  endif
#else
# if defined(__linux__)
#  include <linux/config.h> /* Check for DEVFS */
# endif
# include <sys/soundcard.h>
#endif
#include <sys/ioctl.h>
#include <inttypes.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "audio_out.h"

#include <sys/time.h>

/*
#define LOG
*/

#ifndef AFMT_S16_NE
# if defined(sparc) || defined(__sparc__) || defined(PPC)
/* Big endian machines */
#  define AFMT_S16_NE AFMT_S16_BE
# else
#  define AFMT_S16_NE AFMT_S16_LE
# endif
#endif

#ifndef AFMT_AC3
#       define AFMT_AC3         0x00000400  
#endif

#define AO_OUT_OSS_IFACE_VERSION 5

#define AUDIO_NUM_FRAGMENTS     15
#define AUDIO_FRAGMENT_SIZE   8192

/* bufsize must be a multiple of 3 and 5 for 5.0 and 5.1 channel playback! */
#define ZERO_BUF_SIZE        15360

#define GAP_TOLERANCE         5000
#define MAX_GAP              90000

#define OSS_SYNC_AUTO_DETECT  0
#define OSS_SYNC_GETODELAY    1
#define OSS_SYNC_GETOPTR      2
#define OSS_SYNC_SOFTSYNC     3
#define OSS_SYNC_PROBEBUFFER  4

#ifdef CONFIG_DEVFS_FS
#define DSP_TEMPLATE "/dev/sound/dsp%d"
#else
#define DSP_TEMPLATE "/dev/dsp%d"
#endif

typedef struct oss_driver_s {

  xine_ao_driver_t ao_driver;
  char             audio_dev[20];
  int              audio_fd;
  int              capabilities;
  int              mode;

  config_values_t *config;

  int32_t          output_sample_rate, input_sample_rate;
  int32_t          output_sample_k_rate;
  uint32_t         num_channels;
  uint32_t	   bits_per_sample;
  uint32_t	   bytes_per_frame;
  uint32_t         bytes_in_buffer;      /* number of bytes writen to audio hardware   */
  
  int              audio_started;
  int              sync_method;
  int              latency;
  int              buffer_size;

  struct {
    char          *name;
    int            prop;
    int            volume;
    int            mute;
  } mixer;

  struct timeval   start_time;

} oss_driver_t;

typedef struct {
  audio_driver_class_t driver_class;

  config_values_t *config;
} oss_class_t;

/*
 * open the audio device for writing to
 */
static int ao_oss_open(xine_ao_driver_t *this_gen,
		       uint32_t bits, uint32_t rate, int mode) {

  oss_driver_t *this = (oss_driver_t *) this_gen;
  int tmp;

  printf ("audio_oss_out: ao_open rate=%d, mode=%d, dev=%s\n", 
	  rate, mode, this->audio_dev);

  if ( (mode & this->capabilities) == 0 ) {
    printf ("audio_oss_out: unsupported mode %08x\n", mode);
    return 0;
  }

  if (this->audio_fd > -1) {

    if ( (mode == this->mode) && (rate == this->input_sample_rate) ) {
      return this->output_sample_rate;
    }

    close (this->audio_fd);
  }
  
  this->mode                   = mode;
  this->input_sample_rate      = rate;
  this->bits_per_sample        = bits;
  this->bytes_in_buffer        = 0;
  this->audio_started          = 0;

  /*
   * open audio device
   */

  this->audio_fd=open(this->audio_dev,O_WRONLY|O_NONBLOCK);
  if(this->audio_fd < 0) {
    printf("audio_oss_out: Opening audio device %s: %s\n",
	   this->audio_dev, strerror(errno));
    return 0;
  }
  
  /* We wanted non blocking open but now put it back to normal */
  fcntl(this->audio_fd, F_SETFL, fcntl(this->audio_fd, F_GETFL)&~O_NONBLOCK);

  /*
   * configure audio device
   * In A52 mode, skip all other SNDCTL commands
   */
  if(!(mode & (AO_CAP_MODE_A52 | AO_CAP_MODE_AC5))) {
    tmp = (mode & AO_CAP_MODE_STEREO) ? 1 : 0;
    ioctl(this->audio_fd,SNDCTL_DSP_STEREO,&tmp);

    tmp = bits;
    ioctl(this->audio_fd,SNDCTL_DSP_SAMPLESIZE,&tmp);

    tmp = this->input_sample_rate;
    if (ioctl(this->audio_fd,SNDCTL_DSP_SPEED, &tmp) == -1) {

      printf ("audio_oss_out: warning: sampling rate %d Hz not supported, trying 44100 Hz\n", this->input_sample_rate);

      tmp = 44100;
      if (ioctl(this->audio_fd,SNDCTL_DSP_SPEED, &tmp) == -1) {
        printf ("audio_oss_out: error: 44100 Hz sampling rate not supported\n");
        return 0;
      }
    }
    this->output_sample_rate = tmp;
    this->output_sample_k_rate = this->output_sample_rate / 1000;
    printf ("audio_oss_out: audio rate : %d requested, %d provided by device/sec\n",
	    this->input_sample_rate, this->output_sample_rate);
  }
  /*
   * set number of channels / a52 passthrough
   */

  switch (mode) {
  case AO_CAP_MODE_MONO:
    tmp = 1;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_STEREO:
    tmp = 2;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_4CHANNEL:
    tmp = 4;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_5CHANNEL:
    tmp = 5;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_5_1CHANNEL:
    tmp = 6;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_A52:
  case AO_CAP_MODE_AC5:
    tmp = AFMT_AC3;
    this->num_channels = 2; /* FIXME: is this correct ? */
    this->output_sample_rate = this->input_sample_rate;
    this->output_sample_k_rate = this->output_sample_rate / 1000;
    printf ("audio_oss_out : AO_CAP_MODE_A52\n");
    break;
  }

  printf ("audio_oss_out : %d channels output\n",this->num_channels);
  this->bytes_per_frame=(this->bits_per_sample*this->num_channels)/8;
  
  /*
   * set format
   */

  switch (mode) {
  case AO_CAP_MODE_MONO:
  case AO_CAP_MODE_STEREO:
  case AO_CAP_MODE_4CHANNEL:
  case AO_CAP_MODE_5CHANNEL:
  case AO_CAP_MODE_5_1CHANNEL:
    if (bits==8)
      tmp = AFMT_U8;
    else
      tmp = AFMT_S16_NE;
    if (ioctl(this->audio_fd, SNDCTL_DSP_SETFMT, &tmp) < 0
	|| (tmp!=AFMT_S16_NE && tmp!=AFMT_U8)) {
      if (bits==8) {
	printf("audio_oss_out: SNDCTL_DSP_SETFMT failed for AFMT_U8.\n");
        if (tmp != AFMT_U8)
          printf("audio_oss_out: ioctl succeeded but set format to 0x%x.\n",tmp);
        else
          printf("audio_oss_out: The AFMT_U8 ioctl failed.\n");
        return 0;
      } else {
	printf("audio_oss_out: SNDCTL_DSP_SETFMT failed for AFMT_S16_NE.\n");
        if (tmp != AFMT_S16_NE)
          printf("audio_oss_out: ioctl succeeded but set format to 0x%x.\n",tmp);
        else
          printf("audio_oss_out: The AFMT_S16_NE ioctl failed.\n");
        return 0;
      }          
    }
    break;
  case AO_CAP_MODE_A52:
  case AO_CAP_MODE_AC5:
    tmp = AFMT_AC3;
    if (ioctl(this->audio_fd, SNDCTL_DSP_SETFMT, &tmp) < 0 || tmp != AFMT_AC3) {
      printf("audio_oss_out: AC3 SNDCTL_DSP_SETFMT failed. %d\n",tmp);
      return 0;
    }
    break;
  }



  /*
   * audio buffer size handling
   */

  /* WARNING: let's hope for good defaults here...
     tmp=0 ;
     fsize = AUDIO_FRAGMENT_SIZE;
     while (fsize>0) {
     fsize /=2;
     tmp++;
     }
     tmp--;

     tmp = (AUDIO_NUM_FRAGMENTS << 16) | tmp ;

     printf ("audio_oss_out: audio buffer fragment info : %x\n",tmp);

     ioctl(this->audio_fd,SNDCTL_DSP_SETFRAGMENT,&tmp); 
  */

  return this->output_sample_rate;
}

static int ao_oss_num_channels(xine_ao_driver_t *this_gen) {

  oss_driver_t *this = (oss_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_oss_bytes_per_frame(xine_ao_driver_t *this_gen) {

  oss_driver_t *this = (oss_driver_t *) this_gen;

  return this->bytes_per_frame;
}

static int ao_oss_get_gap_tolerance (xine_ao_driver_t *this_gen){

  /* oss_driver_t *this = (oss_driver_t *) this_gen; */

  return GAP_TOLERANCE;
}

static int ao_oss_delay(xine_ao_driver_t *this_gen) {

  count_info    info;
  oss_driver_t *this = (oss_driver_t *) this_gen;
  int           bytes_left;
  int           frames;
  struct        timeval tv;

  switch (this->sync_method) {
  case OSS_SYNC_PROBEBUFFER:
    if( this->bytes_in_buffer < this->buffer_size )
      bytes_left = this->bytes_in_buffer;
    else
      bytes_left = this->buffer_size;
    break;

  case OSS_SYNC_SOFTSYNC:
    /* use system real-time clock to get pseudo audio frame position */

    gettimeofday(&tv, NULL);

    frames  = (tv.tv_usec - this->start_time.tv_usec)
                  * this->output_sample_k_rate / 1000;
    frames += (tv.tv_sec - this->start_time.tv_sec)
                  * this->output_sample_rate;
    
    frames -= this->latency * this->output_sample_k_rate;
                  
    /* calc delay */

    bytes_left = this->bytes_in_buffer - frames * this->bytes_per_frame;

    if (bytes_left<=0) /* buffer ran dry */
      bytes_left = 0;
    break;
  case OSS_SYNC_GETOPTR:
    ioctl (this->audio_fd, SNDCTL_DSP_GETOPTR, &info);
    
    bytes_left = this->bytes_in_buffer - info.bytes; /* calc delay */
      
    if (bytes_left<=0) /* buffer ran dry */
      bytes_left = 0;
    break;
  case OSS_SYNC_GETODELAY:
    ioctl (this->audio_fd, SNDCTL_DSP_GETODELAY, &bytes_left);
    break;
  }

  return bytes_left / this->bytes_per_frame;
}

 /* Write audio samples
  * num_frames is the number of audio frames present
  * audio frames are equivalent one sample on each channel.
  * I.E. Stereo 16 bits audio frames are 4 bytes.
  */
static int ao_oss_write(xine_ao_driver_t *this_gen,
			int16_t* frame_buffer, uint32_t num_frames) {

  oss_driver_t *this = (oss_driver_t *) this_gen;
  int n;

#ifdef LOG
  printf ("audio_oss_out: ao_oss_write %d frames\n", num_frames);
#endif

  if (this->sync_method == OSS_SYNC_SOFTSYNC) {
    int            simulated_bytes_in_buffer, frames ;
    struct timeval tv;
    /* check if simulated buffer ran dry */

    gettimeofday(&tv, NULL);

    frames  = (tv.tv_usec - this->start_time.tv_usec)
                  * this->output_sample_k_rate / 1000;
    frames += (tv.tv_sec - this->start_time.tv_sec)
                  * this->output_sample_rate;

    /* calc delay */

    simulated_bytes_in_buffer = frames * this->bytes_per_frame;

    if (this->bytes_in_buffer < simulated_bytes_in_buffer)
      this->bytes_in_buffer = simulated_bytes_in_buffer;
  }

  this->bytes_in_buffer += num_frames * this->bytes_per_frame;

  n = write(this->audio_fd, frame_buffer, num_frames * this->bytes_per_frame); 

#ifdef LOG
  printf ("audio_oss_out: ao_oss_write done\n");
#endif

  return n;
}

static void ao_oss_close(xine_ao_driver_t *this_gen) {

  oss_driver_t *this = (oss_driver_t *) this_gen;

  close(this->audio_fd);
  this->audio_fd = -1;
}

static uint32_t ao_oss_get_capabilities (xine_ao_driver_t *this_gen) {

  oss_driver_t *this = (oss_driver_t *) this_gen;

  return this->capabilities;
}

static void ao_oss_exit(xine_ao_driver_t *this_gen) {

  oss_driver_t    *this   = (oss_driver_t *) this_gen;

  if (this->audio_fd != -1)
    close(this->audio_fd);

  free (this);
}

static int ao_oss_get_property (xine_ao_driver_t *this_gen, int property) {

  oss_driver_t *this = (oss_driver_t *) this_gen;
  int           mixer_fd;
  int           audio_devs;

  switch(property) {
  case AO_PROP_PCM_VOL:
  case AO_PROP_MIXER_VOL:
    if(!this->mixer.mute) {
      mixer_fd = open(this->mixer.name, O_RDONLY);
      if(mixer_fd != -1) {
	int cmd = 0;
	int v;
	
	ioctl(mixer_fd, SOUND_MIXER_READ_DEVMASK, &audio_devs);
	
	if(audio_devs & SOUND_MASK_PCM)
	  cmd = SOUND_MIXER_READ_PCM;
	else if(audio_devs & SOUND_MASK_VOLUME)
	  cmd = SOUND_MIXER_READ_VOLUME;
	else {
	  close(mixer_fd);
	  return 0;
	}
	ioctl(mixer_fd, cmd, &v);
	this->mixer.volume = (((v & 0xFF00) >> 8) + (v & 0x00FF)) / 2;
	close(mixer_fd);
      }
      else
	printf("%s(): open() %s failed: %s\n", 
	       __XINE_FUNCTION__, this->mixer.name, strerror(errno));
	return -1;
    }
    return this->mixer.volume;
    break;

  case AO_PROP_MUTE_VOL:
    return this->mixer.mute;
    break;
  }

  return 0;
}

static int ao_oss_set_property (xine_ao_driver_t *this_gen, int property, int value) {

  oss_driver_t *this = (oss_driver_t *) this_gen;
  int           mixer_fd;
  int           audio_devs;

  switch(property) {
  case AO_PROP_PCM_VOL:
  case AO_PROP_MIXER_VOL:
    if(!this->mixer.mute) {
      mixer_fd = open(this->mixer.name, O_RDONLY);

      if(mixer_fd != -1) {
	int cmd = 0;
	int v;
	
	ioctl(mixer_fd, SOUND_MIXER_READ_DEVMASK, &audio_devs);
	
	if(audio_devs & SOUND_MASK_PCM)
	  cmd = SOUND_MIXER_WRITE_PCM;
	else if(audio_devs & SOUND_MASK_VOLUME)
	  cmd = SOUND_MIXER_WRITE_VOLUME;
	else {
	  close(mixer_fd);
	  return ~value;
	}
	v = (value << 8) | value;
	ioctl(mixer_fd, cmd, &v);
	close(mixer_fd);
	
	if(!this->mixer.mute)
	  this->mixer.volume = value;
	
      }
      else
	printf("%s(): open() %s failed: %s\n", 
	       __XINE_FUNCTION__, this->mixer.name, strerror(errno));
    }
    else
      this->mixer.volume = value;

    return this->mixer.volume;
    break;

  case AO_PROP_MUTE_VOL:
    this->mixer.mute = (value) ? 1 : 0;

    if(this->mixer.mute) {

      mixer_fd = open(this->mixer.name, O_RDONLY);

      if(mixer_fd != -1) {
	int cmd = 0;
	int v = 0;
	
	ioctl(mixer_fd, SOUND_MIXER_READ_DEVMASK, &audio_devs);
	
	if(audio_devs & SOUND_MASK_PCM)
	  cmd = SOUND_MIXER_WRITE_PCM;
	else if(audio_devs & SOUND_MASK_VOLUME)
	  cmd = SOUND_MIXER_WRITE_VOLUME;
	else {
	  close(mixer_fd);
	  return ~value;
	}

	ioctl(mixer_fd, cmd, &v);
	close(mixer_fd);
	
      }
      else
	printf("%s(): open() %s failed: %s\n", 
	       __XINE_FUNCTION__, this->mixer.name, strerror(errno));
    }
    else
      (void) ao_oss_set_property(&this->ao_driver, this->mixer.prop, this->mixer.volume);
    
    return value;
    break;
  }

  return ~value;
}

static int ao_oss_ctrl(xine_ao_driver_t *this_gen, int cmd, ...) {
  oss_driver_t *this = (oss_driver_t *) this_gen;

  switch (cmd) {

  case AO_CTRL_PLAY_PAUSE:
#ifdef LOG
    printf ("audio_oss_out: AO_CTRL_PLAY_PAUSE\n");
#endif
    if (this->sync_method != OSS_SYNC_SOFTSYNC)
      ioctl(this->audio_fd, SNDCTL_DSP_RESET, NULL);
    /*  Uncomment the following lines if RESET causes problems
     *  ao_oss_close(this_gen);
     *  ao_oss_open(this_gen, this->bits_per_sample, this->input_sample_rate, this->mode);
     */
    break;

  case AO_CTRL_PLAY_RESUME:
#ifdef LOG
    printf ("audio_oss_out: AO_CTRL_PLAY_RESUME\n");
#endif
    break;

  case AO_CTRL_FLUSH_BUFFERS:
#ifdef LOG
    printf ("audio_oss_out: AO_CTRL_FLUSH_BUFFERS\n");
#endif
    if (this->sync_method != OSS_SYNC_SOFTSYNC)
      ioctl(this->audio_fd, SNDCTL_DSP_RESET, NULL);
#ifdef LOG
    printf ("audio_oss_out: AO_CTRL_FLUSH_BUFFERS done\n");
#endif
    break;
  }

  return 0;
}

static xine_ao_driver_t *open_plugin (audio_driver_class_t *class_gen, const void *data) {

  oss_class_t     *class = (oss_class_t *) class_gen;
  config_values_t *config = class->config;
  oss_driver_t    *this;
  int              caps;
#ifdef CONFIG_DEVFS_FS
  char             devname[] = "/dev/sound/dsp\0\0\0";
#else
  char             devname[] = "/dev/dsp\0\0\0";
#endif
  int              best_rate;
  int              rate ;
  int              devnum;
  int              audio_fd;
  int              num_channels, bits, status, arg;
  static char     *sync_methods[] = {"auto", "getodelay", "getoptr", "softsync", "probebuffer", NULL};
  
  this = (oss_driver_t *) malloc (sizeof (oss_driver_t));

  /*
   * find best device driver/channel
   */

  printf ("audio_oss_out: Opening audio device...\n");

  best_rate = 0;
  devnum = config->register_num (config, "audio.oss_device_num", -1,
				 _("/dev/dsp# device to use for oss output, -1 => auto_detect"),
				 NULL, 10, NULL, NULL);

  if (devnum >= 0) {
    sprintf (this->audio_dev, DSP_TEMPLATE, devnum);
    devnum = 30; /* skip while loop */
  } else {
    devnum = 0;
    sprintf (this->audio_dev, "/dev/dsp");
  }

  while (devnum<16) {

    audio_fd=open(devname,O_WRONLY|O_NONBLOCK);

    if (audio_fd>0) {

      /* test bitrate capability */
      
      rate = 48000;
      ioctl(audio_fd,SNDCTL_DSP_SPEED, &rate);
      if (rate>best_rate) {
	strncpy (this->audio_dev, devname, 19);
	best_rate = rate;
      }
      
      close (audio_fd);
    } /*else
      printf("audio_oss_out: opening audio device %s failed:\n%s\n",
	     this->audio_dev, strerror(errno));
	     */

    sprintf(devname, DSP_TEMPLATE, devnum);
    devnum++;
  }

  /*
   * open that device
   */

  printf ("audio_oss_out: using device >%s<\n",
	  this->audio_dev);

  audio_fd=open(this->audio_dev, O_WRONLY|O_NONBLOCK);

  if(audio_fd < 0) {
    printf("audio_oss_out: opening audio device %s failed:\n%s\n",
	   this->audio_dev, strerror(errno));

    free (this);
    return NULL;

  } 
  /*
   * set up driver to reasonable values for capabilities tests
   */

  arg = AFMT_S16_NE; 
  status = ioctl(audio_fd, SOUND_PCM_SETFMT, &arg);
  arg = 44100;
  status = ioctl(audio_fd, SOUND_PCM_WRITE_RATE, &arg);

  /*
   * find out which sync method to use
   */

  this->sync_method = config->register_enum (config, "audio.oss_sync_method", OSS_SYNC_AUTO_DETECT,
					     sync_methods, 
					     _("A/V sync method to use by OSS, depends on driver/hardware"),
					     NULL, 20, NULL, NULL);

  if (this->sync_method == OSS_SYNC_AUTO_DETECT) {

    count_info info;

    /*
     * check if SNDCTL_DSP_GETODELAY works. if so, using it is preferred.
     */

    if (ioctl(audio_fd, SNDCTL_DSP_GETODELAY, &info) != -1) {
      printf("audio_oss_out: using SNDCTL_DSP_GETODELAY\n");
      this->sync_method = OSS_SYNC_GETODELAY;
    } else if (ioctl(audio_fd, SNDCTL_DSP_GETOPTR, &info) != -1) {
      printf("audio_oss_out: using SNDCTL_DSP_GETOPTR\n");
      this->sync_method = OSS_SYNC_GETOPTR;
    } else {
      this->sync_method = OSS_SYNC_SOFTSYNC;
    }
  }

  if (this->sync_method == OSS_SYNC_SOFTSYNC) {
    printf ("audio_oss_out: Audio driver realtime sync disabled...\n");
    printf ("audio_oss_out: ...will use system real-time clock for soft-sync instead\n");
    printf ("audio_oss_out: ...there may be audio/video synchronization issues\n");

    gettimeofday(&this->start_time, NULL);
  }
  
  if (this->sync_method == OSS_SYNC_PROBEBUFFER) {
    char *buf;
    int c;
  
    printf ("audio_oss_out: Audio driver realtime sync disabled...\n");
    printf ("audio_oss_out: ...probing output buffer size: ");
    this->buffer_size = 0;
    
    if( (buf=malloc(1024)) != NULL ) {
      memset(buf,0,1024);
     
      do {
        c = write(audio_fd,buf,1024);
        if( c != -1 )
          this->buffer_size += c;
      } while( c == 1024 );
      
      free(buf);
    }
    close(audio_fd);
    printf ("%d bytes\n", this->buffer_size );
    printf ("audio_oss_out: ...there may be audio/video synchronization issues\n");
  
    audio_fd=open(this->audio_dev, O_WRONLY|O_NONBLOCK);

    if(audio_fd < 0) 
    {
      printf("audio_oss_out: opening audio device %s failed:\n%s\n",
	   this->audio_dev, strerror(errno));

      free (this);
      return NULL;
    }
  }

  this->latency = config->register_range (config, "audio.oss_latency", 0,
					  -3000, 3000, 
					  _("Adjust a/v sync for OSS softsync"),
					  _("Use this to manually adjust a/v sync if you're using softsync"),
					  10, NULL, NULL);
  
  this->capabilities = 0;
  
  bits = 8;
  if( ioctl(audio_fd, SNDCTL_DSP_SAMPLESIZE,&bits) != -1 )
    this->capabilities |= AO_CAP_8BITS;
  
  /* switch back to 16bits, because some soundcards otherwise do not report all their capabilities */
  bits = 16;
  if (ioctl(audio_fd, SNDCTL_DSP_SAMPLESIZE, &bits) == -1) {
    printf("audio_oss_out: switching the soundcard to 16 bits mode failed\n");
    free(this);
    return NULL;
  }
    
  printf ("audio_oss_out : supported modes are ");
  num_channels = 1; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==1) ) {
    this->capabilities |= AO_CAP_MODE_MONO;
    printf ("mono ");
  }
  num_channels = 2; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==2) ) {
    this->capabilities |= AO_CAP_MODE_STEREO;
    printf ("stereo ");
  }
  num_channels = 4; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==4) ) {
    if (config->register_bool (config, "audio.four_channel", 0,
			       _("Enable 4.0 channel analog surround output"),
			       NULL, 0, NULL, NULL)) {
      this->capabilities |= AO_CAP_MODE_4CHANNEL;
      printf ("4-channel ");
    } else
      printf ("(4-channel not enabled in xine config) " );
  }
  num_channels = 5; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==5) ) {
    if (config->register_bool (config, "audio.five_channel", 0,
			       _("Enable 5.0 channel analog surround output"),
			       NULL, 0, NULL, NULL)) {
      this->capabilities |= AO_CAP_MODE_5CHANNEL;
      printf ("5-channel ");
    } else
      printf ("(5-channel not enabled in xine config) " );
  }
  num_channels = 6; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==6) ) {
    if (config->register_bool (config, "audio.five_lfe_channel", 0,
			       _("Enable 5.1 channel analog surround output"),
			       NULL, 0, NULL, NULL)) {
      this->capabilities |= AO_CAP_MODE_5_1CHANNEL;
      printf ("5.1-channel ");
    } else
      printf ("(5.1-channel not enabled in xine config) " );
  }

  ioctl(audio_fd,SNDCTL_DSP_GETFMTS,&caps);
  if (caps & AFMT_AC3) {
    if (config->register_bool (config, "audio.a52_pass_through", 0,
			       _("Enable A52 / AC5 digital audio output via spdif"),
			       NULL, 0, NULL, NULL)) {
      this->capabilities |= AO_CAP_MODE_A52;
      this->capabilities |= AO_CAP_MODE_AC5;
      printf ("a/52-pass-through ");
    } else 
      printf ("(a/52-pass-through not enabled in xine config)");
  }    

  printf ("\n");
  
  /*
   * mixer initialisation.
   */

  this->mixer.name = config->register_string(config, "audio.mixer_name", "/dev/mixer",
					     _("oss mixer device"), NULL, 
					     10, NULL, NULL);
  {
    int mixer_fd;
    int audio_devs;
    
    mixer_fd = open(this->mixer.name, O_RDONLY);

    if(mixer_fd != -1) {

      ioctl(mixer_fd, SOUND_MIXER_READ_DEVMASK, &audio_devs);
      
      if(audio_devs & SOUND_MASK_PCM) {
	this->capabilities |= AO_CAP_PCM_VOL;
	this->mixer.prop = AO_PROP_PCM_VOL;
      }
      else if(audio_devs & SOUND_MASK_VOLUME) {
	this->capabilities |= AO_CAP_MIXER_VOL;
	this->mixer.prop = AO_PROP_MIXER_VOL;
      }
      
      /*
       * This is obsolete in Linux kernel OSS 
       * implementation, so this will certainly doesn't work.
       * So we just simulate the mute stuff
       */
      /*
	if(audio_devs & SOUND_MASK_MUTE)
	this->capabilities |= AO_CAP_MUTE_VOL;
      */
      this->capabilities |= AO_CAP_MUTE_VOL;
      
      close(mixer_fd);

    } else 
      printf ("audio_oss_out: open() mixer %s failed: %s\n", 
	      this->mixer.name, strerror(errno));
    
    this->mixer.mute = 0;
    this->mixer.volume = ao_oss_get_property (&this->ao_driver, this->mixer.prop);

  }
  close (audio_fd);

  this->output_sample_rate    = 0;
  this->output_sample_k_rate  = 0;
  this->audio_fd              = -1;

  this->config                        = config;
  this->ao_driver.get_capabilities    = ao_oss_get_capabilities;
  this->ao_driver.get_property        = ao_oss_get_property;
  this->ao_driver.set_property        = ao_oss_set_property;
  this->ao_driver.open                = ao_oss_open;
  this->ao_driver.num_channels        = ao_oss_num_channels;
  this->ao_driver.bytes_per_frame     = ao_oss_bytes_per_frame;
  this->ao_driver.delay               = ao_oss_delay;
  this->ao_driver.write		      = ao_oss_write;
  this->ao_driver.close               = ao_oss_close;
  this->ao_driver.exit                = ao_oss_exit;
  this->ao_driver.get_gap_tolerance   = ao_oss_get_gap_tolerance;
  this->ao_driver.control	      = ao_oss_ctrl;

  return &this->ao_driver;
}

/*
 * class functions
 */

static char* get_identifier (audio_driver_class_t *this_gen) {
  return "oss";
}

static char* get_description (audio_driver_class_t *this_gen) {
  return _("xine audio output plugin using oss-compliant audio devices/drivers");
}

static void dispose_class (audio_driver_class_t *this_gen) {

  oss_class_t *this = (oss_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {

  oss_class_t        *this;

  this = (oss_class_t *) malloc (sizeof (oss_class_t));

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.get_identifier  = get_identifier;
  this->driver_class.get_description = get_description;
  this->driver_class.dispose         = dispose_class;

  this->config = xine->config;

  return this;
}

static ao_info_t ao_info_oss = {
  9 /* less than alsa so xine will use alsa's native interface by default */
};

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_OUT, AO_OUT_OSS_IFACE_VERSION, "oss", XINE_VERSION_CODE, &ao_info_oss, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
