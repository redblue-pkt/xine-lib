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
 * $Id: audio_out.c,v 1.19 2001/10/03 15:21:29 jkeil Exp $
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
#include <inttypes.h>

#include "xine_internal.h"
#include "monitor.h"
#include "audio_out.h"
#include "resample.h"
#include "metronom.h"
#include "utils.h"

#define ZERO_BUF_SIZE         5000

#define MAX_GAP              90000

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
static int ao_open(ao_instance_t *this,
		   uint32_t bits, uint32_t rate, int mode)
{ 
  int output_sample_rate;
  if ((output_sample_rate=this->driver->open(this->driver,bits,rate,mode)) == 0) {
    printf("audio_out: open failed!\n");
    return 0;
  }; 

  printf("audio_out: output sample rate %d\n", output_sample_rate);

  this->mode                  = mode;
  this->input_frame_rate      = rate;
  this->bits                  = bits;
  this->audio_started         = 0;
  this->last_audio_vpts       = 0;

  this->output_frame_rate     = output_sample_rate;

  switch (this->resample_conf) {
  case 1: /* force off */
    this->do_resample = 0;
    break;
  case 2: /* force on */
    this->do_resample = 1;
    break;
  default: /* AUTO */
    this->do_resample = this->output_frame_rate != this->input_frame_rate;
  }

  /* HACK: we do not have resample functions for 8-bit audio */
  if (this->bits==8)
    this->do_resample = 0;

  if (this->do_resample) 
    printf("audio_out: will resample audio from %d to %d\n",
	   this->input_frame_rate, this->output_frame_rate);

  this->num_channels      = this->driver->num_channels(this->driver); 

  this->frame_rate_factor = (double) this->output_frame_rate / (double) this->input_frame_rate; 
  this->audio_step        = (uint32_t) 90000 * (uint32_t) 32768 / this->input_frame_rate;
  this->frames_per_kpts   = this->output_frame_rate * 1024 / 90000;
  xprintf (VERBOSE|AUDIO, "audio_out : audio_step %d pts per 32768 frames\n", this->audio_step);

  this->metronom->set_audio_rate(this->metronom, this->audio_step);

  return this->output_frame_rate;
}

static void ao_fill_gap (ao_instance_t *this, uint32_t pts_len) {

  int num_frames ;
  xprintf (VERBOSE|AUDIO, "audio_out : fill_gap\n");

  if (pts_len > MAX_GAP)
    pts_len = MAX_GAP;

  num_frames = pts_len * this->frames_per_kpts / 1024;

  if ((this->mode == AO_CAP_MODE_A52) || (this->mode == AO_CAP_MODE_AC5)) return; /* FIXME */

  printf ("audio_out: inserting %d 0-frames to fill a gap of %d pts\n",num_frames, pts_len);

  while (num_frames > 0) {
    if (num_frames > ZERO_BUF_SIZE) {
      this->driver->write(this->driver, this->zero_space, ZERO_BUF_SIZE);
      num_frames -= ZERO_BUF_SIZE;
    } else {
      this->driver->write(this->driver, this->zero_space, num_frames);
      num_frames = 0;
    }
  }
}

/*
 * This routine is currently not used, but I do not want to loose it.
 * I think "(c) 2001 Andy Lo A Foe <andy@alsaplayer.org>" originally added it
 * to ./xine-lib/src/audio_out/audio_alsa_out.c before the architecture changes
 * So it has moved to here.
 */

void write_pause_burst(ao_instance_t *this, int error)
{
  unsigned char buf[8192];
  unsigned short *sbuf = (unsigned short *)&buf[0];
  
  sbuf[0] = 0xf872;
  sbuf[1] = 0x4e1f;
  
  if (error == 0)
    /* Audio ES Channel empty, wait for DD Decoder or pause */
    sbuf[2] = 0x0003;
  else
    /* user stop, skip or error */
    sbuf[2] = 0x0103;
  
  sbuf[3] = 0x0020;
  sbuf[4] = 0x0000;
  sbuf[5] = 0x0000;
  
  memset(&sbuf[6], 0, 6144 - 96);
  this->driver->write(this->driver, sbuf, 6144 / 4);
}

static int ao_write(ao_instance_t *this,
		    int16_t* output_frames, uint32_t num_frames,
		    uint32_t pts)
{
  uint32_t    vpts, buffer_vpts;
  int32_t     gap;
  int         bDropPackage;
  int         delay;
  int         frame_size;
  int         fscod;
  int         frmsizecod;
  uint8_t    *data;
  uint32_t    cur_time;
  
  vpts = this->metronom->got_audio_samples (this->metronom, pts, num_frames);

  xprintf (VERBOSE|AUDIO, "audio_out: got %d frames, vpts=%d pts=%d\n",
           num_frames, vpts, pts);

  if (vpts<this->last_audio_vpts) {
    /* reject this */
    xprintf (VERBOSE|AUDIO, "audio_out: rejected frame vpts=%d, last_audio_vpts=%d\n", vpts,this->last_audio_vpts)

    return 1;
  }

  this->last_audio_vpts = vpts;

  bDropPackage = 0;

  /*
   * where, in the timeline is the "end" of the audio buffer at the moment?
   */
  
  cur_time = this->metronom->get_current_time (this->metronom);
  buffer_vpts = cur_time;
    
  if (this->audio_started)
    delay = this->driver->delay(this->driver);
  else
    delay = 0;

  /* External A52 decoder delay correction */
  if ((this->mode==AO_CAP_MODE_A52) || (this->mode==AO_CAP_MODE_AC5)) 
    delay+=10; 

  buffer_vpts += delay * 1024 / this->frames_per_kpts;
    
  /*
   * calculate gap:
   */
  
  gap = vpts - buffer_vpts;

  /*
  printf ("vpts : %d   buffer_vpts : %d  gap %d\n",
	  vpts, buffer_vpts, gap);
  */

  if (gap>this->gap_tolerance) {


    if (gap>15000)
      ao_fill_gap (this, gap);
    else {
      printf ("audio_out: adjusting master clock %d -> %d\n",
	      cur_time, cur_time + gap);
      this->metronom->adjust_clock (this->metronom, 
				    cur_time + gap);
    }
    
    /* keep xine responsive */
      
    if (gap>MAX_GAP)
      return 0;
      
  } else if (gap < (-1 * this->gap_tolerance)) {
    bDropPackage = 1;
    xprintf (VERBOSE|AUDIO, "audio_out: audio package (vpts = %d %d)"
	     "dropped\n", vpts, gap);
  }
    
  /*
   * resample and output frames
   */
  if ((this->mode == AO_CAP_MODE_A52) || (this->mode == AO_CAP_MODE_AC5)) 
    bDropPackage=0;

  if (!bDropPackage) {
    int num_output_frames = (double) num_frames * this->frame_rate_factor;

    if ((!this->do_resample) 
	&& (this->mode != AO_CAP_MODE_A52) 
	&& (this->mode != AO_CAP_MODE_AC5)) {
      xprintf (VERBOSE|AUDIO, "audio_out: writing without resampling\n");
      this->driver->write (this->driver, output_frames,
			   num_frames );
    } else switch (this->mode) {
    case AO_CAP_MODE_MONO:
      audio_out_resample_mono (output_frames, num_frames,
			       this->frame_buffer, num_output_frames);
      this->driver->write(this->driver, this->frame_buffer, num_output_frames);
      break;
    case AO_CAP_MODE_STEREO:
      audio_out_resample_stereo (output_frames, num_frames,
                                 this->frame_buffer, num_output_frames);
      this->driver->write(this->driver, this->frame_buffer, num_output_frames);
      break;
    case AO_CAP_MODE_4CHANNEL:
      audio_out_resample_4channel (output_frames, num_frames,
                                   this->frame_buffer, num_output_frames);
      this->driver->write(this->driver, this->frame_buffer, num_output_frames);
      break;
    case AO_CAP_MODE_5CHANNEL:
      audio_out_resample_5channel (output_frames, num_frames,
                                   this->frame_buffer, num_output_frames);
      this->driver->write(this->driver, this->frame_buffer, num_output_frames);
      break;
    case AO_CAP_MODE_5_1CHANNEL:
      audio_out_resample_6channel (output_frames, num_frames,
                                   this->frame_buffer, num_output_frames);
      this->driver->write(this->driver, this->frame_buffer, num_output_frames);
      break;
    case AO_CAP_MODE_A52:

      this->frame_buffer[0] = 0xf872;  /* spdif syncword */
      this->frame_buffer[1] = 0x4e1f;  /* .............  */
      this->frame_buffer[2] = 0x0001;  /* AC3 data       */

      data = (uint8_t *)&output_frames[1]; /* skip AC3 sync */
      fscod = (data[2] >> 6) & 0x3;
      frmsizecod = data[2] & 0x3f;
      frame_size = frmsizecod_tbl[frmsizecod].frm_size[fscod] << 4;
      this->frame_buffer[3] = frame_size;

      /* ac3 seems to be swabbed data */
      swab(output_frames,this->frame_buffer+4,  num_frames  );
      this->driver->write(this->driver, this->frame_buffer, 1536);

      break;
    case AO_CAP_MODE_AC5:
      memset(this->frame_buffer,0xff,6144);
      this->frame_buffer[0] = 0xf872;  /* spdif syncword */
      this->frame_buffer[1] = 0x4e1f;  /* .............  */
      this->frame_buffer[2] = 0x0001;  /*                */

      this->frame_buffer[3] = 0x3ee0;

      /* ac3 seems to be swabbed data */
      swab(output_frames,this->frame_buffer+4,  2014  );
      
      this->driver->write(this->driver, this->frame_buffer, 1024);
      
      break;

    }

    xprintf (AUDIO|VERBOSE, "audio_out :audio package written\n");

    /*
     * step values
     */

    this->audio_started    = 1;
  }

  return 1;

}


static void ao_close(ao_instance_t *this)
{
  this->driver->close(this->driver);  
}

static void ao_exit(ao_instance_t *this) {
  this->driver->exit(this->driver);
}

static uint32_t ao_get_capabilities (ao_instance_t *this) {
  uint32_t result;
  result=this->driver->get_capabilities(this->driver);  
  return result;
}

static int ao_get_property (ao_instance_t *this, int property) {

  return(this->driver->get_property(this->driver, property));
}

static int ao_set_property (ao_instance_t *this, int property, int value) {

  return(this->driver->set_property(this->driver, property, value));
}

ao_instance_t *ao_new_instance (ao_driver_t *driver, metronom_t *metronom, 
				config_values_t *config) {

  ao_instance_t *this;

  this = xmalloc (sizeof (ao_instance_t)) ;

  this->driver                = driver;
  this->metronom              = metronom;

  this->open                  = ao_open;
  this->write                 = ao_write;
  this->close                 = ao_close;
  this->exit                  = ao_exit;
  this->get_capabilities      = ao_get_capabilities;
  this->get_property          = ao_get_property;
  this->set_property          = ao_set_property;
  this->audio_loop_running    = 0;
  this->frame_buffer          = xmalloc (40000);
  this->zero_space            = xmalloc (ZERO_BUF_SIZE * 2 * 6);
  this->gap_tolerance         = driver->get_gap_tolerance (this->driver);

  this->resample_conf = config->lookup_int (config, "audio_resample_mode", 0);

  return this;
}

