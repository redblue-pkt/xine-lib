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
 * $Id: metronom.h,v 1.13 2001/11/10 13:48:03 guenter Exp $
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

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <sys/time.h>
#include <pthread.h>

typedef struct metronom_s metronom_t ;
typedef struct scr_plugin_s scr_plugin_t;

struct metronom_s {

  /*
   * this is called to tell metronom to prepare for a new video stream
   */

  void (*video_stream_start) (metronom_t *this);
  void (*video_stream_end) (metronom_t *this);

  /*
   * this is called to tell metronom to prepare for a new audio stream
   */

  void (*audio_stream_start) (metronom_t *this);
  void (*audio_stream_end) (metronom_t *this);

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
   *           scr: system clock reference, may be 0 or == pts if unknown
   *
   * return value: virtual pts for frame
   *
   */
  
  uint32_t (*got_video_frame) (metronom_t *this, uint32_t pts, uint32_t scr);

  /*
   * called by audio output driver whenever audio samples are delivered to it
   *
   * parameter pts      : pts for audio data if known, 0 otherwise
   *           nsamples : number of samples delivered
   *           scr      : system clock reference, may be 0 or == pts if unknown
   *
   * return value: virtual pts for audio data
   *
   */

  uint32_t (*got_audio_samples) (metronom_t *this, uint32_t pts, uint32_t nsamples, uint32_t scr); 

  /*
   * called by SPU decoder whenever a packet is delivered to it
   *
   * parameter pts      : pts for SPU packet if known, 0 otherwise
   *           scr      : system clock reference, may be 0 or == pts if unknown
   *
   * return value: virtual pts for SPU packet
   *
   */

  uint32_t (*got_spu_packet) (metronom_t *this, uint32_t pts, uint32_t duration,
			      uint32_t scr); 

  /*
   * tell metronom about discontinuities
   */
  void (*expect_audio_discontinuity) (metronom_t *this);
  void (*expect_video_discontinuity) (metronom_t *this);

  /*
   * manually correct audio <-> video sync
   */
  void (*set_av_offset) (metronom_t *this, int32_t pts);

  int32_t (*get_av_offset) (metronom_t *this);

  /*
   * system clock reference (SCR) functions
   */

  /*
   * start metronom clock (no clock reset)
   * at given pts
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
   * set clock speed
   * for constants see xine_internal.h
   */

  int (*set_speed) (metronom_t *this, int speed);

  /*
   * (un)register a System Clock Reference provider at the metronom
   */
  int    (*register_scr) (metronom_t *this, scr_plugin_t *scr);
  void (*unregister_scr) (metronom_t *this, scr_plugin_t *scr);

  /*
   * metronom internal stuff
   */

  uint32_t        pts_per_frame;
  uint32_t        pts_per_smpls;

  int32_t         audio_pts_delta;

  uint32_t        video_vpts;
  uint32_t        spu_vpts;
  uint32_t        audio_vpts;

  int32_t         video_wrap_offset;
  int32_t         audio_wrap_offset;
  int             wrap_diff_counter;

  uint32_t        last_video_pts;
  uint32_t        last_video_scr;
  int             num_video_vpts_guessed;
  int32_t         video_pts_delta;

  uint32_t        last_audio_pts;
  uint32_t        last_audio_scr;
  int             num_audio_samples_guessed;

  int32_t         av_offset;

  scr_plugin_t*   scr_master;
  scr_plugin_t**  scr_list;
  pthread_t       sync_thread;

  pthread_mutex_t lock;

  int             have_audio;
  int             video_stream_starting;
  int             video_stream_running;
  int             audio_stream_starting;
  int             audio_stream_running;
  int             video_discontinuity;
  int             video_discontinuity_count;
  int             audio_discontinuity;
  int             audio_discontinuity_count;
  pthread_cond_t  video_discontinuity_reached;
  pthread_cond_t  audio_discontinuity_reached;
  pthread_cond_t  video_started;
  pthread_cond_t  audio_started;
  pthread_cond_t  video_ended;
  pthread_cond_t  audio_ended;

};

metronom_t *metronom_init (int have_audio);

/*
 * SCR plugins
 */

struct scr_plugin_s
{
  int interface_version;

  int (*get_priority) (scr_plugin_t *this);

  /* 
   * set/get clock speed 
   *
   * for speed constants see xine_internal.h
   * returns actual speed
   */

  int (*set_speed) (scr_plugin_t *this, int speed);

  void (*adjust) (scr_plugin_t *this, uint32_t vpts);

  void (*start) (scr_plugin_t *this, uint32_t start_vpts);

  uint32_t (*get_current) (scr_plugin_t *this);

  metronom_t *metronom;
};

#ifdef __cplusplus
}
#endif

#endif
