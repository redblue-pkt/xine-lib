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
 * $Id: metronom.h,v 1.2 2001/04/27 10:42:38 f1rmb Exp $
 *
 * metronom: general pts => virtual calculation/assoc
 *                   
 * virtual pts: unit 1/90000 sec, always increasing
 *              can be used for synchronization
 *              video/audio frame with same pts also have same vpts
 *              but pts is likely to differ from vpts
 *
 */


#ifndef HAVE_METRONOM_H
#define HAVE_METRONOM_H

#include <inttypes.h>
#include <sys/time.h>

typedef struct metronom_s metronom_t ;

struct metronom_s {

  /*
   * clear all cached data, reset current vpts ... called if new input
   * file is reached
   */
  
  void (*reset) (metronom_t *this);

  /*
   * called by video output driver to inform metronom about current framerate
   *
   * parameter pts_per_frame : frame display duration in 1/90000 sec
   */
  void (*set_video_rate) (metronom_t *this, uint32_t pts_per_frame);

  /*
   * return current video rate (including delta corrections)
   */

  uint32_t (*get_video_rate) (metronom_t *this);

  /*
   * called by audio output driver to inform metronom about current audio
   * bitrate
   *
   * parameter pts_per_smpls : 1/90000 sec per 65536 samples
   */
  void (*set_audio_rate) (metronom_t *this, uint32_t pts_per_smpls);

  /*
   * called by video output driver for *every* frame
   *
   * parameter pts: pts for frame if known, 0 otherwise
   *
   * return value: virtual pts for frame
   *
   */
  
  uint32_t (*got_video_frame) (metronom_t *this, uint32_t pts);

  /*
   * called by audio output driver whenever audio samples are delivered to it
   *
   * parameter pts      : pts for audio data if known, 0 otherwise
   *           nsamples : number of samples delivered
   *
   * return value: virtual pts for audio data
   *
   */

  uint32_t (*got_audio_samples) (metronom_t *this, uint32_t pts, uint32_t nsamples); 

  /*
   * called by SPU decoder whenever a packet is delivered to it
   *
   * parameter pts      : pts for SPU packet if known, 0 otherwise
   *
   * return value: virtual pts for SPU packet
   *
   */

  uint32_t (*got_spu_packet) (metronom_t *this, uint32_t pts); 

  /*
   * manually correct audio <-> video sync
   */
  void (*set_av_offset) (metronom_t *this, int32_t pts);

  int32_t (*get_av_offset) (metronom_t *this);

  /*
   * ****************************************
   *       master clock functions
   * ****************************************
   */

  /*
   * start metronom clock (no clock reset)
   */
  void (*start_clock) (metronom_t *this, uint32_t pts);


  /*
   * stop metronom clock
   */
  void (*stop_clock) (metronom_t *this);


  /*
   * resume clock from where it was stopped
   */
  void (*resume_clock) (metronom_t *this);


  /*
   * get current clock value in vpts
   */
  uint32_t (*get_current_time) (metronom_t *this);


  /*
   * adjust master clock to external timer (e.g. audio hardware)
   */
  void (*adjust_clock) (metronom_t *this, uint32_t desired_pts);

  /*
   * metronom internal stuff
   */

  uint32_t        pts_per_frame;
  uint32_t        pts_per_smpls;

  int32_t         audio_pts_delta;

  uint32_t        video_vpts;
  uint32_t        audio_vpts;

  uint32_t        sync_pts;
  uint32_t        sync_vpts;

  /* video delta for wrong framerates */
  uint32_t        last_video_pts;
  uint32_t        last_video_vpts;
  int             num_video_vpts_guessed;
  int32_t         video_pts_delta;

  int             num_audio_samples_guessed;

  int32_t         av_offset;

  struct timeval  start_time;
  uint32_t        start_pts, last_pts;
  int             stopped ;
};

metronom_t *metronom_init ();

#endif
