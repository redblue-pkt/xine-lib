/* 
 * Copyright (C) 2000, 2001 the xine project
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
 * along with self program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: audio_out.c,v 1.4 2001/08/22 10:51:05 jcdutton Exp $
 * 
 * 22-8-2001 James imported some useful AC3 sections from the previous alsa driver.
 *   (c) 2001 Andy Lo A Foe <andy@alsaplayer.org>
 * 20-8-2001 First implementation of Audio sync and Audio driver separation.
 *   (c) 2001 James Courtier-Dutton James@superbug.demon.co.uk
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
 * Currently, James has tested with OSS: Standard stereo out, SPDIF PCM, SPDIF AC3
 *                                 ALSA: Standard stereo out
 * No testing has been done of ALSA SPDIF AC3 or any 4,5,5.1 channel output.
 * Currently, I don't think resampling functions, as I cannot test it.
 */

/* required for swab() */
#define _XOPEN_SOURCE 500
/* required for FNDELAY decl */
#define _BSD_SOURCE 1

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
#include <soundcard.h>
#elif defined(__FreeBSD__)
#include <machine/soundcard.h>
#else
#if defined(__linux__)
#include <linux/config.h> /* Check for DEVFS */
#endif
#include <sys/soundcard.h>
#endif
#include <sys/ioctl.h>
#include <inttypes.h>

#include "xine_internal.h"
#include "monitor.h"
#include "audio_out.h"
#include "resample.h"
#include "metronom.h"
#include "utils.h"

#ifndef AFMT_S16_NE
# if defined(sparc) || defined(__sparc__) || defined(PPC)
/* Big endian machines */
#  define AFMT_S16_NE AFMT_S16_BE
# else
#  define AFMT_S16_NE AFMT_S16_LE
# endif
#endif

#ifndef AFMT_AC3
#       define AFMT_AC3         0x00000400      /* Dolby Digital AC3 */
#endif

#define AO_OUT_OSS_IFACE_VERSION 1

#define AUDIO_NUM_FRAGMENTS     15
#define AUDIO_FRAGMENT_SIZE   8192

/* bufsize must be a multiple of 3 and 5 for 5.0 and 5.1 channel playback! */
#define ZERO_BUF_SIZE        15360

#define GAP_TOLERANCE         5000
#define MAX_GAP              90000

#ifdef CONFIG_DEVFS_FS
#define DSP_TEMPLATE "/dev/sound/dsp%d"
#else
#define DSP_TEMPLATE "/dev/dsp%d"
#endif

struct frmsize_s
{
                uint16_t bit_rate;
                uint16_t frm_size[3];
};


static const struct frmsize_s frmsizecod_tbl[64] =
{
                { 32  ,{64   ,69   ,96   } },
                { 32  ,{64   ,70   ,96   } },
                { 40  ,{80   ,87   ,120  } },
                { 40  ,{80   ,88   ,120  } },
                { 48  ,{96   ,104  ,144  } },
                { 48  ,{96   ,105  ,144  } },
                { 56  ,{112  ,121  ,168  } },
                { 56  ,{112  ,122  ,168  } },
                { 64  ,{128  ,139  ,192  } },
                { 64  ,{128  ,140  ,192  } },
                { 80  ,{160  ,174  ,240  } },
                { 80  ,{160  ,175  ,240  } },
                { 96  ,{192  ,208  ,288  } },
                { 96  ,{192  ,209  ,288  } },
                { 112 ,{224  ,243  ,336  } },
                { 112 ,{224  ,244  ,336  } },
                { 128 ,{256  ,278  ,384  } },
                { 128 ,{256  ,279  ,384  } },
                { 160 ,{320  ,348  ,480  } },
                { 160 ,{320  ,349  ,480  } },
                { 192 ,{384  ,417  ,576  } },
                { 192 ,{384  ,418  ,576  } },
                { 224 ,{448  ,487  ,672  } },
                { 224 ,{448  ,488  ,672  } },
                { 256 ,{512  ,557  ,768  } },
                { 256 ,{512  ,558  ,768  } },
                { 320 ,{640  ,696  ,960  } },
                { 320 ,{640  ,697  ,960  } },
                { 384 ,{768  ,835  ,1152 } },
                { 384 ,{768  ,836  ,1152 } },
                { 448 ,{896  ,975  ,1344 } },
                { 448 ,{896  ,976  ,1344 } },
                { 512 ,{1024 ,1114 ,1536 } },
                { 512 ,{1024 ,1115 ,1536 } },
                { 576 ,{1152 ,1253 ,1728 } },
                { 576 ,{1152 ,1254 ,1728 } },
                { 640 ,{1280 ,1393 ,1920 } },
                { 640 ,{1280 ,1394 ,1920 } }
};

/*
 * open the audio device for writing to
 */
static int ao_open(ao_instance_t *self,
		   uint32_t bits, uint32_t rate, int mode)
{ 
  int result;
  if(result=self->driver->open(self->driver,bits,rate,mode)<0) {
	  printf("open failed!\n");
	  return -1;
  }; 
  self->mode                  = mode;
  self->input_frame_rate      = rate;
  self->frames_in_buffer      = 0;
  self->audio_started         = 0;
  self->last_audio_vpts       = 0;
  self->do_resample           = 0; /* Resampling currently not working. */

  self->output_frame_rate=rate;
  self->num_channels = self->driver->num_channels(self->driver); 

  self->frame_rate_factor = (double) self->output_frame_rate / (double) self->input_frame_rate; /* Alway produces 1 at the moment */
  self->audio_step         = (uint32_t) 90000 * (uint32_t) 32768
	                                   / self->input_frame_rate;
  self->frames_per_kpts     = self->output_frame_rate * self->num_channels * 2 * 1024 / 90000;
  xprintf (VERBOSE|AUDIO, "audio_out : audio_step %d pts per 32768 frames\n", self->audio_step);

  self->metronom->set_audio_rate(self->metronom, self->audio_step);


  return 1;
}

static void ao_fill_gap (ao_instance_t *self, uint32_t pts_len) {

  int num_bytes ;
  xprintf (VERBOSE|AUDIO, "audio_out : fill_gap\n");

  if (pts_len > MAX_GAP)
    pts_len = MAX_GAP;
  num_bytes = pts_len * self->frames_per_kpts / 1024;
  num_bytes = (num_bytes / (2*self->num_channels)) * (2*self->num_channels);

  if(self->mode == AO_CAP_MODE_AC3) return; /* FIXME */

  printf ("audio_out: inserting %d 0-bytes to fill a gap of %d pts\n",num_bytes, pts_len);

  self->frames_in_buffer += num_bytes;

  while (num_bytes > 0) {
    if (num_bytes > ZERO_BUF_SIZE) {
      self->driver->write(self->driver, self->zero_space, ZERO_BUF_SIZE);
      num_bytes -= ZERO_BUF_SIZE;
    } else {
      self->driver->write(self->driver, self->zero_space, num_bytes);
      num_bytes = 0;
    }
  }
}

/*
 * This routine is currently not used, but I do not want to loose it.
 * I think "(c) 2001 Andy Lo A Foe <andy@alsaplayer.org>" originally added it
 * to ./xine-lib/src/audio_out/audio_alsa_out.c before the architecture changes
 * So it has moved to here.
 */

void write_pause_burst(alsa_instance_t *this,int error)
{
#define BURST_SIZE 6144

        unsigned char buf[8192];
        unsigned short *sbuf = (unsigned short *)&buf[0];

        sbuf[0] = 0xf872;
        sbuf[1] = 0x4e1f;

        if (error == 0)
                // Audio ES Channel empty, wait for DD Decoder or pause
                sbuf[2] = 0x0003;
        else
                // user stop, skip or error
                sbuf[2] = 0x0103;

        sbuf[3] = 0x0020;
        sbuf[4] = 0x0000;
        sbuf[5] = 0x0000;

        memset(&sbuf[6], 0, BURST_SIZE - 96);
	self->driver->write(self->driver, u_char * sbuf, BURST_SIZE / 4);
}

static int ao_write(ao_instance_t *self,
                               int16_t* output_frames, uint32_t num_frames,
                               uint32_t pts_)
{
  uint32_t         vpts, buffer_vpts;
  int32_t          gap;
  int              bDropPackage;
  int              pos;

  if (self->driver<0)
    return 1;

  vpts = self->metronom->got_audio_samples (self->metronom, pts_, num_frames);

  xprintf (VERBOSE|AUDIO, "audio_out: got %d frames, vpts=%d pts=%d\n",
           num_frames, vpts,pts_);

  if (vpts<self->last_audio_vpts) {
    /* reject self */
  xprintf (VERBOSE|AUDIO, "audio_out: rejected frame vpts=%d, last_audio_vpts=%d\n", vpts,self->last_audio_vpts)

    return 1;
  }

  self->last_audio_vpts = vpts;

  bDropPackage = 0;

  if ( self->audio_has_realtime || !self->audio_started ) {

  /*
   * where, in the timeline is the "end" of the audio buffer at the moment?
   */

  buffer_vpts = self->metronom->get_current_time (self->metronom);

  if (self->audio_started) {
    pos = self->driver->delay(self->driver);
  } else
    pos = 0;
  if ( (self->mode==AO_CAP_MODE_AC3) && (pos>10) ) pos-=10; /* External AC3 decoder delay correction */

  if (pos>self->frames_in_buffer) /* buffer ran dry */
    self->frames_in_buffer = pos;

  buffer_vpts += (self->frames_in_buffer - pos) * 1024 / self->frames_per_kpts;

 /*
   * calculate gap:
   */

  gap = vpts - buffer_vpts;
  xprintf (VERBOSE|AUDIO, "audio_out: buff=%d pos=%d buf_vpts=%d gap=%d\n",
           self->frames_in_buffer, pos,buffer_vpts,gap);

  if (gap>GAP_TOLERANCE) {
    ao_fill_gap (self, gap);

    /* keep xine responsive */

    if (gap>MAX_GAP)
      return 0;

  } else if (gap<-GAP_TOLERANCE) {
    bDropPackage = 1;
    xprintf (VERBOSE|AUDIO, "audio_out: audio package (vpts = %d %d)"
             "dropped\n", vpts, gap);
  }

  } /* has realtime */

  /*
   * resample and output frames
   */
  if(self->mode == AO_CAP_MODE_AC3) bDropPackage=0;

  if (!bDropPackage) {
    int num_output_frames = num_frames * (self->output_frame_rate) / self->input_frame_rate;

    if ((!self->do_resample) && (self->mode != AO_CAP_MODE_AC3)) {
      xprintf (VERBOSE|AUDIO, "audio_out: writing without resampling\n");
      self->driver->write(self->driver, output_frames,
            num_output_frames );
    } else switch (self->mode) {
    case AO_CAP_MODE_MONO:
      audio_out_resample_mono (output_frames, num_frames,
                               self->frame_buffer, num_output_frames);
      self->driver->write(self->driver, self->frame_buffer, num_output_frames);
      break;
    case AO_CAP_MODE_STEREO:
      audio_out_resample_stereo (output_frames, num_frames,
                                 self->frame_buffer, num_output_frames);
      self->driver->write(self->driver, self->frame_buffer, num_output_frames);
      break;
    case AO_CAP_MODE_4CHANNEL:
      audio_out_resample_4channel (output_frames, num_frames,
                                   self->frame_buffer, num_output_frames);
      self->driver->write(self->driver, self->frame_buffer, num_output_frames);
      break;
    case AO_CAP_MODE_5CHANNEL:
      audio_out_resample_5channel (output_frames, num_frames,
                                   self->frame_buffer, num_output_frames);
      self->driver->write(self->driver, self->frame_buffer, num_output_frames);
      break;
    case AO_CAP_MODE_5_1CHANNEL:
      audio_out_resample_6channel (output_frames, num_frames,
                                   self->frame_buffer, num_output_frames);
      self->driver->write(self->driver, self->frame_buffer, num_output_frames);
      break;
    case AO_CAP_MODE_AC3:
      num_output_frames = (num_frames+8)/4;
      self->frame_buffer[0] = 0xf872;  //spdif syncword
      self->frame_buffer[1] = 0x4e1f;  // .............
      self->frame_buffer[2] = 0x0001;  // AC3 data

           data = (uint8_t *)&output_samples[1]; // skip AC3 sync
           fscod = (data[2] >> 6) & 0x3;
           frmsizecod = data[2] & 0x3f;
           frame_size = frmsizecod_tbl[frmsizecod].frm_size[fscod] << 4;
           sample_buffer[3] = frame_size;

    // ac3 seems to be swabbed data
      swab(output_frames,self->frame_buffer+4,  num_frames  );
      self->driver->write(self->driver, self->zero_space, 2); /* Prevents crackle at start. */
      self->driver->write(self->driver, self->frame_buffer, num_output_frames);
      self->driver->write(self->driver, self->zero_space, 1534-num_output_frames);
      num_output_frames=num_output_frames;
      break;
    }

    xprintf (AUDIO|VERBOSE, "audio_out :audio package written\n");

    /*
     * step values
     */

    self->frames_in_buffer += num_output_frames ;
    self->audio_started    = 1;
  }

  return 1;

}


static void ao_close(ao_instance_t *self)
{
  self->driver->close(self->driver);  
}

static uint32_t ao_get_capabilities (ao_instance_t *self) {
  uint32_t result;
  result=self->driver->get_capabilities(self->driver);  
  return result;
}

ao_instance_t *ao_new_instance (ao_driver_t *driver, metronom_t *metronom) {

  ao_instance_t *self;

  self = xmalloc (sizeof (ao_instance_t)) ;
  self->driver                = driver;
  self->metronom              = metronom;

  self->open                  = ao_open;
  self->write                 = ao_write;
  self->close                 = ao_close;
  self->get_capabilities      = ao_get_capabilities;
  self->audio_loop_running    = 0;
  self->frame_buffer = malloc (40000);
  memset (self->frame_buffer, 0, 40000);
  self->zero_space = malloc (ZERO_BUF_SIZE);
  memset (self->zero_space, 0, ZERO_BUF_SIZE);
  return self;
}

