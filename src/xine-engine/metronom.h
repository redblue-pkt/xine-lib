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
 * $Id: metronom.h,v 1.38 2002/10/28 07:53:52 tmattern Exp $
 *
 * metronom: general pts => virtual calculation/assoc
 *                   
 * virtual pts: unit 1/90000 sec, always increasing
 *              can be used for synchronization
 *              video/audio frame with same pts also have same vpts
 *              but pts is likely to differ from vpts
 *
 * the basic idea is: 
 *    video_pts + video_wrap_offset = video_vpts
 *    audio_pts + audio_wrap_offset = audio_vpts
 *
 *  - video_wrap_offset should be equal to audio_wrap_offset as to have
 *    perfect audio and video sync. They will differ on brief periods due
 *    discontinuity correction.
 *  - metronom should also interpolate vpts values most of the time as
 *    video_pts and audio_vpts are not given for every frame.
 *  - corrections to the frame rate may be needed to cope with bad
 *    encoded streams.
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
#include "video_out.h"
#include "xine.h"

typedef struct metronom_s metronom_t ;
typedef struct scr_plugin_s scr_plugin_t;

  /* see below */
#define DISC_STREAMSTART 0 
#define DISC_RELATIVE    1     
#define DISC_ABSOLUTE    2
#define DISC_STREAMSEEK  3

struct metronom_s {

  /*
   * called by audio output driver to inform metronom about current audio
   * samplerate
   *
   * parameter pts_per_smpls : 1/90000 sec per 65536 samples
   */
  void (*set_audio_rate) (metronom_t *this, int64_t pts_per_smpls);

  /*
   * called by video output driver for *every* frame
   *
   * parameter frame containing pts, scr, ... information
   *
   * will set vpts field in frame
   *
   * this function will also update video_wrap_offset if a discontinuity
   * is detected (read the comentaries below about discontinuities).
   * 
   */
  
  void (*got_video_frame) (metronom_t *this, vo_frame_t *frame);

  /*
   * called by audio output driver whenever audio samples are delivered to it
   *
   * parameter pts      : pts for audio data if known, 0 otherwise
   *           nsamples : number of samples delivered
   *
   * return value: virtual pts for audio data
   *
   * this function will also update audio_wrap_offset if a discontinuity
   * is detected (read the comentaries below about discontinuities).
   *
   */

  int64_t (*got_audio_samples) (metronom_t *this, int64_t pts, 
				int nsamples); 

  /*
   * called by SPU decoder whenever a packet is delivered to it
   *
   * parameter pts      : pts for SPU packet if known, 0 otherwise
   *
   * return value: virtual pts for SPU packet
   * (this is the only pts to vpts function that cannot update the wrap_offset
   * due to the lack of regularity on spu packets)
   */

  int64_t (*got_spu_packet) (metronom_t *this, int64_t pts);

  /*
   * tell metronom about discontinuities.
   *
   * these functions are called due to a discontinuity detected at
   * demux stage.
   *
   * there are different types of discontinuities:
   *
   * DISC_STREAMSTART : new stream starts, expect pts values to start
   *                    from zero immediately
   * DISC_RELATIVE    : typically a wrap-around, expect pts with 
   *                    a specified offset from the former ones soon
   * DISC_ABSOLUTE    : typically a new menu stream (nav packets)
   *                    pts will start from given value soon
   * DISC_STREAMSEEK  : used by video and audio decoder loop,
   *                    when a buffer with BUF_FLAG_SEEK set is encountered;
   *                    applies the necessary vpts offset for the seek in
   *                    metronom, but keeps the vpts difference between
   *                    audio and video, so that metronom doesn't cough
   *
   * for DISC_RELATIVE and DISC_ABSOLUTE metronom will enter a
   * special discontinuity mode which means that it will ignore
   * pts values for some time (about 1sec) to ignore any held-back
   * reference frames that are flushed out of decoders containing
   * pts values that do not mach the new offset. Then it will
   * just switch to the new disc_offset and resume synced operation.
   *                    
   */
  void (*handle_audio_discontinuity) (metronom_t *this, int type, int64_t disc_off);
  void (*handle_video_discontinuity) (metronom_t *this, int type, int64_t disc_off);

  /*
   * set/get options for metronom, constants see below
   */
  void (*set_option) (metronom_t *this, int option, int64_t value);
  int64_t (*get_option) (metronom_t *this, int option);

  /*
   * system clock reference (SCR) functions
   */

  /*
   * start metronom clock (no clock reset)
   * at given pts
   */
  void (*start_clock) (metronom_t *this, int64_t pts);


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
  int64_t (*get_current_time) (metronom_t *this);


  /*
   * adjust master clock to external timer (e.g. audio hardware)
   */
  void (*adjust_clock) (metronom_t *this, int64_t desired_pts);


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

  void (*exit) (metronom_t *this);

  /*
   * pointer to current xine stream object. 
   */
  xine_stream_t *stream;

  /*
   * metronom internal stuff
   */

  int64_t         pts_per_smpls;

  int64_t         video_vpts;
  int64_t         spu_vpts;
  int64_t         audio_vpts;

  int64_t         vpts_offset;
  int64_t         next_vpts_offset;

  int             in_discontinuity;

  int64_t         video_drift;
  int64_t         video_drift_step;
  
  int             audio_samples;
  int64_t         audio_drift_step;

  int64_t         av_offset;

  scr_plugin_t*   scr_master;
  scr_plugin_t**  scr_list;
  pthread_t       sync_thread;
  int             scr_adjustable;

  pthread_mutex_t lock;

  int             have_audio;
  int             video_discontinuity_count;
  int             audio_discontinuity_count;
  int             discontinuity_handled_count;
  pthread_cond_t  video_discontinuity_reached;
  pthread_cond_t  audio_discontinuity_reached;
  pthread_cond_t  cancel;

  int             allow_full_ao_fill_gap;
  int             force_audio_jump;

  int64_t         img_duration;
  int             img_cpt;
  int64_t         last_video_pts;
  
};

metronom_t *metronom_init (int have_audio, xine_stream_t *stream);

/*
 * metronom options
 */

#define METRONOM_SCR_ADJUSTABLE   1
#define METRONOM_AV_OFFSET        2
#define METRONOM_ADJ_VPTS_OFFSET  3

/*
 * SCR (system clock reference) plugins
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

  void (*adjust) (scr_plugin_t *this, int64_t vpts);

  void (*start) (scr_plugin_t *this, int64_t start_vpts);

  int64_t (*get_current) (scr_plugin_t *this);

  void (*exit) (scr_plugin_t *this);

  metronom_t *metronom;
};

#ifdef __cplusplus
}
#endif

#endif
