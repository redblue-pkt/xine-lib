/*
 * Copyright (C) 2003 the xine project
 * Copyright (C) 2003 J.Asselman <j.asselman@itsec-ps.nl>
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
 * v4l input plugin
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/videodev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>


/* Used to capture the audio data */
#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

#define XINE_ENABLE_EXPERIMENTAL_FEATURES

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"

#define NUM_FRAMES  15

/* Our CPU can't handle de-interlacing at 768. */
#define MAX_RES 640

static struct {
	int width;
	int height;
} resolutions[] = {
	{ 768, 576 },
	{ 640, 480 },
	{ 384, 288 },
	{ 320, 240 },
	{ 160, 120 },
};

#define NUM_RESOLUTIONS (sizeof(resolutions)/sizeof(resolutions[0]))
#define RADIO_DEV "/dev/v4l/radio0"
#define VIDEO_DEV "/dev/v4l/video0"
/*
#define LOG
*/
#define PLUGIN "input_v4l"

#ifdef LOG
#define DBGPRINT(args...) printf(PLUGIN ": " args); fflush(stdout)
#else
#define DBGPRINT(args...) {}
#endif

#define PRINT(args...) printf(PLUGIN ": " args)

#if !defined(NDELAY) && defined(O_NDELAY)
#define FNDELAY O_NDELAY
#endif

typedef struct pvrscr_s pvrscr_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
} v4l_input_class_t;

typedef struct {
  input_plugin_t   input_plugin;

  xine_stream_t   *stream;
  char            *mrl;

  off_t            curpos;

  int		  old_interlace;
  int		  old_zoomx;
  int		  old_zoomy;
  int		  audio_only;
  
  /* Audio */
  buf_element_t   *aud_frames;
  pthread_mutex_t  aud_frames_lock;
  pthread_cond_t   aud_frame_freed;

#ifdef HAVE_ALSA
  /* Handle for the PCM device */
  snd_pcm_t	  *pcm_handle;
  
  /* Record stream (via line 1) */
  snd_pcm_stream_t   pcm_stream;
  
  /* Information and configuration for the PCM stream */
  snd_pcm_hw_params_t	*pcm_hwparams;

  /* Name of the PCM device, plughw:0,0?=>soundcard,device*/
  char		  *pcm_name;  
 
  /* Use alsa to capture the sound (for a/v sync) */
  char		  audio_capture;
  
  int exact_rate;         /* Actual sample rate
                             sndpcm_hw_params_set_rate_near */
  int dir;                /* exact rate == rate --> dir =  0
                             exact rate  < rate --> dir = -1
                             exact rate  > rate --> dir =  1 */

  unsigned char *pcm_data;

  int64_t   pts_aud_start;
#endif

/* Video */
  buf_element_t   *vid_frames;
  pthread_mutex_t  vid_frames_lock;
  pthread_cond_t   vid_frame_freed;

  int              video_fd;
  int		   radio_fd;

  int              input;
  int              tuner;
  unsigned long	   frequency;
  unsigned long    calc_frequency;
  char		   *tuner_name;

   int		  radio;   /* ask for a radio channel */
   int		  channel; /* channel number */

  struct video_channel     video_channel;
  struct video_tuner       video_tuner;
  struct video_capability  video_cap;
  struct video_audio       audio;
  struct video_audio       audio_saved;
  struct video_mbuf        gb_buffers;
  int                      frame_format;
  int                      frame_size;
  int                      use_mmap;
  uint8_t                 *video_buf;
  int                      gb_frame;
  struct video_mmap        gb_buf;
  int64_t                  start_time;

  xine_event_queue_t *event_queue;

  pvrscr_t  *scr;
  int	    scr_tunning;

} v4l_input_plugin_t;

/*
 * ***************************************************
 * unix System Clock Reference + fine tunning
 *
 * This code is copied and paste from the input_pvr.c
 *
 * the fine tunning option is used to change play
 * speed in order to regulate fifo usage, that is,
 * trying to match the rate of generated data.
 *
 * OBS: use with audio.av_sync_method=resample
 * ***************************************************
 */

#define SCR_PAUSED -2
#define SCR_FW -3
#define SCR_SKIP -4

struct pvrscr_s {
   scr_plugin_t     scr;

   struct timeval   cur_time;
   int64_t          cur_pts;
   int              xine_speed;
   double           speed_factor;
   double           speed_tunning;

   pthread_mutex_t  lock;
};

static int pvrscr_get_priority(scr_plugin_t *scr)
{
   return 10; /* high priority */
}

/* Only call this when already mutex locked */
static void pvrscr_set_pivot(pvrscr_t *this)
{
   struct   timeval tv;
   int64_t pts;
   double   pts_calc;
   
   gettimeofday(&tv, NULL);
   pts_calc = (tv.tv_sec  - this->cur_time.tv_sec) * this->speed_factor;
   pts_calc += (tv.tv_usec - this->cur_time.tv_usec) * this->speed_factor / 1e6;
   pts = this->cur_pts + pts_calc;
   
   /* This next part introduces a one off inaccuracy
    * to the scr due to rounding tv to pts.
    */
   this->cur_time.tv_sec=tv.tv_sec;
   this->cur_time.tv_usec=tv.tv_usec;
   this->cur_pts=pts;
   
   return;
}

static int pvrscr_set_speed (scr_plugin_t *scr, int speed)
{
   pvrscr_t *this = (pvrscr_t*) scr;
   
   pthread_mutex_lock (&this->lock);
   
   pvrscr_set_pivot( this );
   this->xine_speed   = speed;
   this->speed_factor = (double) speed * 90000.0 / 4.0 *
      this->speed_tunning;
   
   pthread_mutex_unlock (&this->lock);
   
   return speed;
}

static void pvrscr_speed_tunning (pvrscr_t *this, double factor)
{
   pthread_mutex_lock (&this->lock);
   
   pvrscr_set_pivot( this );
   this->speed_tunning = factor;
   this->speed_factor = (double) this->xine_speed * 90000.0 / 4.0 *
      this->speed_tunning;
   
   pthread_mutex_unlock (&this->lock);
}

static void pvrscr_adjust (scr_plugin_t *scr, int64_t vpts)
{
   pvrscr_t *this = (pvrscr_t*) scr;
   struct   timeval tv;
   
   pthread_mutex_lock (&this->lock);
   
   gettimeofday(&tv, NULL);
   this->cur_time.tv_sec=tv.tv_sec;
   this->cur_time.tv_usec=tv.tv_usec;
   this->cur_pts = vpts;
   
   pthread_mutex_unlock (&this->lock);
}

static void pvrscr_start (scr_plugin_t *scr, int64_t start_vpts)
{
   pvrscr_t *this = (pvrscr_t*) scr;
   
   pthread_mutex_lock (&this->lock);
   
   gettimeofday(&this->cur_time, NULL);
   this->cur_pts = start_vpts;
   
   pthread_mutex_unlock (&this->lock);
   
   pvrscr_set_speed (&this->scr, XINE_SPEED_NORMAL);
}

static int64_t pvrscr_get_current (scr_plugin_t *scr)
{
   pvrscr_t *this = (pvrscr_t*) scr;
   
   struct   timeval tv;
   int64_t pts;
   double   pts_calc;
   pthread_mutex_lock (&this->lock);
   
   gettimeofday(&tv, NULL);
   
   pts_calc = (tv.tv_sec  - this->cur_time.tv_sec) * this->speed_factor;
   pts_calc += (tv.tv_usec - this->cur_time.tv_usec) * this->speed_factor / 1e6;
   
   pts = this->cur_pts + pts_calc;
   
   pthread_mutex_unlock (&this->lock);
   
   return pts;
}

static void pvrscr_exit (scr_plugin_t *scr)
{
   pvrscr_t *this = (pvrscr_t*) scr;
   
   pthread_mutex_destroy (&this->lock);
   free(this);
}

static pvrscr_t* pvrscr_init (void)
{
   pvrscr_t *this;
   
   this = malloc(sizeof(*this));
   memset(this, 0, sizeof(*this));
   
   this->scr.interface_version = 2;
   this->scr.get_priority      = pvrscr_get_priority;
   this->scr.set_speed         = pvrscr_set_speed;
   this->scr.adjust            = pvrscr_adjust;
   this->scr.start             = pvrscr_start;
   this->scr.get_current       = pvrscr_get_current;
   this->scr.exit              = pvrscr_exit;
   
   pthread_mutex_init (&this->lock, NULL);

   pvrscr_speed_tunning(this, 1.0 );
   pvrscr_set_speed (&this->scr, XINE_SPEED_PAUSE);
#ifdef SCRLOG
   printf("input_v4l: scr init complete\n");
#endif
   
   return this;
}

/*** END COPY AND PASTE from PVR**************************/

/*** The following is copy and past from net_buf_ctrl ****/
static void report_progress (xine_stream_t *stream, int p)
{
   xine_event_t             event;
   xine_progress_data_t     prg;
  
   if (p == SCR_PAUSED) {
      prg.description = _("Buffer underrun...");
      p = 0;
   } else
   if (p == SCR_FW) {
      prg.description = _("Buffer overrun...");
      p = 100;
   } else
      prg.description = _("Adjusting...");
   
   prg.percent = (p>100)?100:p;
   
   event.type = XINE_EVENT_PROGRESS;
   event.data = &prg;
   event.data_length = sizeof (xine_progress_data_t);
   
   xine_event_send (stream, &event);
}

/**** END COPY AND PASTE from net_buf_ctrl ***************/

int rate = 44100;	/* Sample rate */
int dir;		/* exact rate == rate --> dir =  0
			   exact rate  < rate --> dir = -1
			   exact rate  > rate --> dir =  1 */
int periods = 2;	/* Number of periods */
int periodsize = 2 * 8192;	/* Periodsize in bytes */
int bits = 16;

static int search_by_tuner(v4l_input_plugin_t *this, char *input_source);
static int search_by_channel(v4l_input_plugin_t *this, char *input_source);

static void v4l_event_handler(v4l_input_plugin_t *this);

/**
 * Allocate an audio frame.
 */
inline static buf_element_t *alloc_aud_frame (v4l_input_plugin_t *this)
{
   buf_element_t *frame;
   
   DBGPRINT("alloc_aud_frame. trying to get lock...\n");

   pthread_mutex_lock (&this->aud_frames_lock) ;
   
   DBGPRINT("got the lock\n");
 
   while (!this->aud_frames) {
      DBGPRINT ("no audio frame available...\n");
      pthread_cond_wait (&this->aud_frame_freed, &this->aud_frames_lock);
   }
   
   frame = this->aud_frames;
   this->aud_frames = this->aud_frames->next;
   
   pthread_mutex_unlock (&this->aud_frames_lock);
   
   DBGPRINT("alloc_vid_frame done\n");
 
   return frame;
}

/**
 * Stores an audio frame.
 */
static void store_aud_frame (buf_element_t *frame)
{
   v4l_input_plugin_t *this = (v4l_input_plugin_t *) frame->source;

   DBGPRINT("store_aud_frame\n");

   pthread_mutex_lock (&this->aud_frames_lock) ;
   
   frame->next  = this->aud_frames;
   this->aud_frames = frame;
   
   pthread_cond_signal (&this->aud_frame_freed);
   pthread_mutex_unlock (&this->aud_frames_lock);
}

/**
 * Allocate a video frame.
 */
inline static buf_element_t *alloc_vid_frame (v4l_input_plugin_t *this)
{

  buf_element_t *frame;

  DBGPRINT("alloc_vid_frame. trying to get lock...\n");

  pthread_mutex_lock (&this->vid_frames_lock) ;

  DBGPRINT("got the lock\n");
	
  while (!this->vid_frames) {
    DBGPRINT ("no video frame available...\n");
    pthread_cond_wait (&this->vid_frame_freed, &this->vid_frames_lock);
  }
  
  frame = this->vid_frames;
  this->vid_frames = this->vid_frames->next;
  
  pthread_mutex_unlock (&this->vid_frames_lock);
  
  DBGPRINT("alloc_vid_frame done\n");
	
  return frame;
}

/**
 * Stores a video frame.
 */
static void store_vid_frame (buf_element_t *frame)
{

  v4l_input_plugin_t *this = (v4l_input_plugin_t *) frame->source;
   
  DBGPRINT("input_v4l: store_vid_frame\n");
	
  pthread_mutex_lock (&this->vid_frames_lock) ;

  frame->next  = this->vid_frames;
  this->vid_frames = frame;
 
  pthread_cond_signal (&this->vid_frame_freed);
  
  pthread_mutex_unlock (&this->vid_frames_lock);
}

static int extract_mrl(v4l_input_plugin_t *this, char *mrl) 
{
   char               *tuner_name = NULL;
   int                frequency = 0;
   char               *locator = NULL;
   char               *begin = NULL;

   if (mrl == NULL) {
      DBGPRINT("Someone passed an empty mrl\n");
      return 0;
   }

   for (locator = mrl; *locator != '\0' && *locator !=  '/' ; locator++);
   
   /* Get tuner name */
   if (*locator == '/') {
      begin = ++locator;
      
      for (; *locator != '\0' && *locator != '/' ; locator++);
      
      tuner_name = (char *) strndup(begin, locator - begin);
      
      /* Get frequency, if available */  
      sscanf(locator, "/%d", &frequency);
      DBGPRINT("v4l: Tuner name: '%s' freq: %d\r\n", tuner_name, frequency);
   } else {
      PRINT("v4l: No tuner name given. Expected syntac: v4l:/tuner/frequency\r\n");
      PRINT("v4l: Using currently tuned settings\r\n");
   }

  this->frequency = frequency;
  this->tuner_name = tuner_name;

  return 1;
}

static int set_frequency(v4l_input_plugin_t *this, unsigned long frequency)
{
   int ret = 0;
   int fd;

   if (this->video_fd > 0)
      fd = this->video_fd;
   else
      fd = this->radio_fd;
   
   if (frequency != 0) {
      if (this->video_tuner.flags & VIDEO_TUNER_LOW) {
	 this->calc_frequency = frequency * 16;
      } else {
	 this->calc_frequency = (frequency * 16) / 1000;
      }

      
      ret = ioctl(fd, VIDIOCSFREQ, &this->calc_frequency);
#ifdef LOG
      DBGPRINT("IOCTL set frequency (%ld) returned: %d\r\n", frequency, ret);
   } else {
      DBGPRINT("v4l: No frequency given. Won't be set\r\n");
      DBGPRINT("v4l: Syntax is: v4l:/tuner_name/frequency\r\n");
#endif
   }

   this->frequency = frequency;

   if (ret < 0)
      return ret;
   else
      return 1;
}

static int set_input_source(v4l_input_plugin_t *this, char *input_source)
{
   int ret = 0;
   
   if ((ret = search_by_channel(this, input_source)) != 1) {
      ret = search_by_tuner(this, input_source);
   }

   return ret;
}

static int search_by_tuner(v4l_input_plugin_t *this, char *input_source)
{
   int ret = 0;
   int fd = 0;
   int cur_tuner = 0;
   
   if (this->video_fd > 0)
      fd = this->video_fd;
   else
      fd = this->radio_fd;
  
   this->video_tuner.tuner = cur_tuner;
   ioctl(fd, VIDIOCGCAP, &this->video_cap);
   
   DBGPRINT("This device has %d channel(s)\r\n", this->video_cap.channels);

   for (ret = ioctl(fd, VIDIOCGTUNER, &this->video_tuner);
	 ret == 0 && this->video_cap.channels > cur_tuner && strstr(this->video_tuner.name, input_source) == NULL;
	 cur_tuner++) {
      
      this->video_tuner.tuner = cur_tuner;

      DBGPRINT("(%d) V4L device currently set to: \r\n", ret);
      DBGPRINT("Tuner:  %d\r\n", this->video_tuner.tuner);
      DBGPRINT("Name:   %s\r\n", this->video_tuner.name);
      if (this->video_tuner.flags & VIDEO_TUNER_LOW) {
         DBGPRINT("Range:  %ld - %ld\r\n", this->video_tuner.rangelow / 16,  this->video_tuner.rangehigh * 16);
      } else {
         DBGPRINT("Range:  %ld - %ld\r\n", this->video_tuner.rangelow * 1000 / 16, this->video_tuner.rangehigh * 1000 / 16);
      }
   }
   
   DBGPRINT("(%d) V4L device final: \r\n", ret);
   DBGPRINT("Tuner:  %d\r\n", this->video_tuner.tuner);
   DBGPRINT("Name:   %s\r\n", this->video_tuner.name);
   if (this->video_tuner.flags & VIDEO_TUNER_LOW) {
      DBGPRINT("Range:  %ld - %ld\r\n", this->video_tuner.rangelow / 16,  this->video_tuner.rangehigh * 16);
   } else {
      DBGPRINT("Range:  %ld - %ld\r\n", this->video_tuner.rangelow * 1000 / 16, this->video_tuner.rangehigh * 1000 / 16);
   }
   
   if (strstr(this->video_tuner.name, input_source) == NULL)
      return -1;
   
   return 1;
}

static int search_by_channel(v4l_input_plugin_t *this, char *input_source)
{
   int ret = 0;
   int fd = 0;
   this->input = 0;
   
   if (this->video_fd > 0)
      fd = this->video_fd;
   else
      fd = this->radio_fd;
   
   /* Tune into channel */
   ret = ioctl(fd, VIDIOCGCHAN, &this->video_channel);
   DBGPRINT("(%d) V4L device currently set to:\r\n", ret);
   DBGPRINT("Channel: %d\r\n", this->video_channel.channel);
   DBGPRINT("Name:    %s\r\n", this->video_channel.name);
   DBGPRINT("Tuners:  %d\r\n", this->video_channel.tuners);
   DBGPRINT("Flags:   %d\r\n", this->video_channel.flags);
   DBGPRINT("Type:    %d\r\n", this->video_channel.type);
   DBGPRINT("Norm:    %d\r\n", this->video_channel.norm);
   
   if (strlen(input_source) > 0) {
      while (strstr(this->video_channel.name, input_source) == NULL &&
	    ioctl(fd, VIDIOCGCHAN, &this->video_channel) == 0) {
	 
	 DBGPRINT("V4L device currently set to:\r\n");
	 DBGPRINT("Channel: %d\r\n", this->video_channel.channel);
	 DBGPRINT("Name:    %s\r\n", this->video_channel.name);
	 DBGPRINT("Tuners:  %d\r\n", this->video_channel.tuners);
	 DBGPRINT("Flags:   %d\r\n", this->video_channel.flags);
	 DBGPRINT("Type:    %d\r\n", this->video_channel.type);
	 DBGPRINT("Norm:    %d\r\n", this->video_channel.norm);
	 this->video_channel.channel = ++this->input;
      }
      
      if (strstr(this->video_channel.name, input_source) == NULL) {
	 if (this->stream->xine->verbosity >= XINE_VERBOSITY_LOG)
	    PRINT("Tuner name not found\n");
	 return -1;
      }
   
      this->tuner_name = input_source;
      ret = ioctl(fd, VIDIOCSCHAN, &this->input);
      
      DBGPRINT("(%d) Set channel to %d\r\n", ret, this->input);
      
      /* FIXME: Don't assume tuner 0 ? */
      
      this->tuner = 0;

      ret = ioctl(fd, VIDIOCSTUNER, &this->tuner);
      
      DBGPRINT("(%d) Response on set tuner to %d\r\n", ret, this->tuner);
      
      this->video_tuner.tuner = this->tuner;
   } else {
      PRINT("v4l: Not setting video source. No source given\r\n");
   }
   ret = ioctl(fd, VIDIOCGTUNER, &this->video_tuner);
#ifdef LOG
   DBGPRINT("(%d) Flags %d\r\n", ret, this->video_tuner.flags);
   
   DBGPRINT("VIDEO_TUNER_PAL %s set\r\n", this->video_tuner.flags & VIDEO_TUNER_PAL ? "" : "not");
   DBGPRINT("VIDEO_TUNER_NTSC %s set\r\n", this->video_tuner.flags & VIDEO_TUNER_NTSC ? "" : "not");
   DBGPRINT("VIDEO_TUNER_SECAM %s set\r\n", this->video_tuner.flags & VIDEO_TUNER_SECAM ? "" : "not");
   DBGPRINT("VIDEO_TUNER_LOW %s set\r\n", this->video_tuner.flags & VIDEO_TUNER_LOW ? "" : "not");
   DBGPRINT("VIDEO_TUNER_NORM %s set\r\n", this->video_tuner.flags & VIDEO_TUNER_NORM ? "" : "not");
   DBGPRINT("VIDEO_TUNER_STEREO_ON %s set\r\n", this->video_tuner.flags & VIDEO_TUNER_STEREO_ON ? "" : "not");
   DBGPRINT("VIDEO_TUNER_RDS_ON %s set\r\n", this->video_tuner.flags & VIDEO_TUNER_RDS_ON ? "" : "not");
   DBGPRINT("VIDEO_TUNER_MBS_ON %s set\r\n", this->video_tuner.flags & VIDEO_TUNER_MBS_ON ? "" : "not");
   
   switch (this->video_tuner.mode) {
      case VIDEO_MODE_PAL:
	 DBGPRINT("The tuner is in PAL mode\r\n");
	 break;
      case VIDEO_MODE_NTSC:
	 DBGPRINT("The tuner is in NTSC mode\r\n");
	 break;
      case VIDEO_MODE_SECAM:
	 DBGPRINT("The tuner is in SECAM mode\r\n");
	 break;
      case VIDEO_MODE_AUTO:
	 DBGPRINT("The tuner is in AUTO mode\r\n");
	 break;
   }
#endif		
   return 1;   
}

int open_radio_capture_device(v4l_input_plugin_t *this)
{
   int tuner_found = 0;
   int i = 0;

   /*
    * pre-alloc a bunch of frames
    */

   pthread_mutex_init (&this->vid_frames_lock, NULL);
   pthread_cond_init  (&this->vid_frame_freed, NULL);
   pthread_mutex_init (&this->aud_frames_lock, NULL);
   pthread_cond_init  (&this->aud_frame_freed, NULL); 

   DBGPRINT("Opening radio device\n");

   this->radio_fd = open("/dev/v4l/radio0", O_RDWR);

   if (this->radio_fd < 0)
      return 0; 
   
   DBGPRINT("Device opened, radio %d\n", this->radio_fd);

   if (set_input_source(this, this->tuner_name) > 0)
      tuner_found = 1;
 
   this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] = periods;
   this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] = bits;
   this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] = rate;
   this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
   this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
 
   /* 
    * Pre allocate some frames for audio and video. This way this hasn't to be
    * done during capture.
    */
   for (i=0; i<NUM_FRAMES; i++) {
      buf_element_t *frame;
       
      /* Audio frame */
      frame = xine_xmalloc (sizeof (buf_element_t));
      
      frame->decoder_info[1] = periodsize;
      frame->content         = xine_xmalloc(periodsize);
      frame->type	     = BUF_AUDIO_RAWPCM;
      frame->source          = this;
      frame->free_buffer     = store_aud_frame;
      frame->extra_info      = xine_xmalloc(sizeof(extra_info_t));
      
      store_aud_frame(frame);
   }

   this->audio_only = 1;
 
   /* Unmute audio off video capture device */
   ioctl(this->radio_fd, VIDIOCGAUDIO, &this->audio);
   memcpy(&this->audio_saved, &this->audio, sizeof(this->audio));
   this->audio.flags &= ~VIDEO_AUDIO_MUTE;
   this->audio.volume=0x8000;
   DBGPRINT("Setting audio volume\r\n");
   ioctl(this->radio_fd, VIDIOCSAUDIO, &this->audio);
   
   set_frequency(this, this->frequency); 
   
   if (tuner_found)
      return 1;
   else
      return 2;
}

int close_radio_capture_device(v4l_input_plugin_t *this)
{
   if (this->radio_fd > 0)
      close(this->radio_fd);
   else
      /* Radio device probably never opened. So nothing left to cleanup. */
      return 0;
   
   this->radio_fd = 0;

   return 1;
}

/**
 * Open the video capture device.
 *
 * This opens the video capture device and if given, selects a tuner from
 * which the signal should be grabbed.
 * @return 1 on success, 0 on failure.
 */
int open_video_capture_device(v4l_input_plugin_t *this)
{
   int i, j, ret, found = 0;
   int tuner_found = 0;
   
   DBGPRINT("Trying to open '%s'\n", this->mrl);

   /*
    * pre-alloc a bunch of frames
    */

   pthread_mutex_init (&this->vid_frames_lock, NULL);
   pthread_cond_init  (&this->vid_frame_freed, NULL);
   pthread_mutex_init (&this->aud_frames_lock, NULL);
   pthread_cond_init  (&this->aud_frame_freed, NULL); 

   /* Try to open the video device */
   this->video_fd = open("/dev/v4l/video0", O_RDWR);
   
   if (this->video_fd < 0) {
      DBGPRINT("(%d) Cannot open v4l device: %s\n", this->video_fd, 
	    strerror(errno));
      return 0;
   }
   DBGPRINT("Device opened, tv %d\n", this->video_fd);
   
   /* Get capabilities */
   if (ioctl(this->video_fd,VIDIOCGCAP,&this->video_cap) < 0) {
      DBGPRINT ("VIDIOCGCAP ioctl went wrong\n");
      return 0;
   }
   
   if (!(this->video_cap.type & VID_TYPE_CAPTURE)) {
      /* Capture is not supported by the device. This is a must though! */
      DBGPRINT("Grab device does not handle capture\n");
      return 0;
   }
   
   /* figure out the resolution */
   for (j=0; j<NUM_RESOLUTIONS; j++)
   {
      if (resolutions[j].width <= this->video_cap.maxwidth
      	    && resolutions[j].height <= this->video_cap.maxheight
      	    && resolutions[j].width <= MAX_RES)
      {
   	 found = 1;
   	 break;
      }
   }
   
   if (found == 0 || resolutions[j].width < this->video_cap.minwidth
	 || resolutions[j].height < this->video_cap.minheight)
   {
      /* Looks like the device does not support one of the preset resolutions */
      DBGPRINT("Grab device does not support any preset resolutions");
      return 0;
   }
  
   this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] = resolutions[j].width;
   this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = resolutions[j].height;
   this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] = periods;
   this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] = bits;
   this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] = rate;
   this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
   this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
   
   /* 
    * Pre allocate some frames for audio and video. This way this hasn't to be
    * done during capture.
    */
   for (i=0; i<NUM_FRAMES; i++) {
      buf_element_t *frame;
      
      /* Video frame */   
      frame = xine_xmalloc (sizeof (buf_element_t));
      
      frame->decoder_info[0] = resolutions[j].width;
      frame->decoder_info[1] = resolutions[j].height;
      frame->content         =
	 xine_xmalloc (frame->decoder_info[0] * frame->decoder_info[1] * 3 / 2); 
      frame->type            = BUF_VIDEO_YUV_FRAMES;
      frame->source          = this;
      frame->free_buffer     = store_vid_frame;
      frame->extra_info      = xine_xmalloc(sizeof(extra_info_t));
      
      store_vid_frame(frame);
      
      /* Audio frame */
      frame = xine_xmalloc (sizeof (buf_element_t));
      
      frame->decoder_info[1] = periodsize;
      frame->content         = xine_xmalloc(periodsize);
      frame->type	     = BUF_AUDIO_RAWPCM;
      frame->source          = this;
      frame->free_buffer     = store_aud_frame;
      frame->extra_info      = xine_xmalloc(sizeof(extra_info_t));
      
      store_aud_frame(frame);
   }
   
   /* Unmute audio off video capture device */
   ioctl(this->video_fd, VIDIOCGAUDIO, &this->audio);
   memcpy(&this->audio_saved, &this->audio, sizeof(this->audio));
   this->audio.flags &= ~VIDEO_AUDIO_MUTE;
   this->audio.volume=0xD000;
   DBGPRINT("Setting audio volume\r\n");
   ioctl(this->video_fd, VIDIOCSAUDIO, &this->audio);
  
   if (strlen(this->tuner_name) > 0) {
      /* Tune into source and given frequency */
      if (set_input_source(this, this->tuner_name) <= 0)
         return 0;
      else
	 tuner_found = 1;
   }
   
   set_frequency(this, this->frequency); 
   
   /* Test for mmap video access */
   ret = ioctl(this->video_fd,VIDIOCGMBUF, &this->gb_buffers);
   
   if (ret < 0) {
      /* Device driver does not support mmap */
      /* try to use read based access */
      struct video_picture pict;
      int val;
      
      ioctl(this->video_fd, VIDIOCGPICT, &pict);
      
      /* try to choose a suitable video format */
      pict.palette=VIDEO_PALETTE_YUV420P;
      ret = ioctl(this->video_fd, VIDIOCSPICT, &pict);
      if (ret < 0) {
   	 pict.palette=VIDEO_PALETTE_YUV422;
   	 ret = ioctl(this->video_fd, VIDIOCSPICT, &pict);
   	 if (ret < 0) {
    	    close (this->video_fd);
    	    this->video_fd = -1;
    	    DBGPRINT("Grab: no colorspace format found\n");
    	    return 0;
   	 }
   	 else
    	    DBGPRINT("Grab: format YUV 4:2:2\n");
      }
      else
   	 DBGPRINT("input_v4l: grab: format YUV 4:2:0\n");
      
      this->frame_format = pict.palette;
      
      val = 1;
      ioctl(this->video_fd, VIDIOCCAPTURE, &val);
      
      this->use_mmap = 0;
      
   } else {
      /* Good, device driver support mmap. Mmap the memory */
      DBGPRINT("input_v4l: using mmap, size %d\n", this->gb_buffers.size);
      this->video_buf = mmap(0, this->gb_buffers.size,
	    PROT_READ|PROT_WRITE, MAP_SHARED,
	    this->video_fd,0);
      if ((unsigned char*)-1 == this->video_buf) {
	 /* mmap failed. */;
   	 perror("mmap");
   	 close (this->video_fd);
   	 return 0;
      }
      this->gb_frame = 0;
      
      /* start to grab the first frame */
      this->gb_buf.frame = (this->gb_frame + 1) % this->gb_buffers.frames;
      this->gb_buf.height = resolutions[j].height;
      this->gb_buf.width = resolutions[j].width;
      this->gb_buf.format = VIDEO_PALETTE_YUV420P;
      
      ret = ioctl(this->video_fd, VIDIOCMCAPTURE, &this->gb_buf);
      if (ret < 0 && errno != EAGAIN) {
   	 /* try YUV422 */
   	 this->gb_buf.format = VIDEO_PALETTE_YUV422;
	 
   	 ret = ioctl(this->video_fd, VIDIOCMCAPTURE, &this->gb_buf);
      }
      else
   	 DBGPRINT("(%d) input_v4l: YUV420 should work\n", ret);
      
      if (ret < 0) {
   	 if (errno != EAGAIN) {
	    DBGPRINT(
		  "input_v4l: grab device does not support suitable format\n");
   	 } else {
	    DBGPRINT(
		  "input_v4l: grab device does not receive any video signal\n");
   	 }
   	 close (this->video_fd);
   	 return 0;
      }
      this->frame_format = this->gb_buf.format;
      this->use_mmap = 1;
   }
   
   switch(this->frame_format) {
      case VIDEO_PALETTE_YUV420P:
     	 this->frame_size = 
	    (resolutions[j].width * resolutions[j].height * 3) / 2;
     	 break;
      case VIDEO_PALETTE_YUV422:
     	 this->frame_size = resolutions[j].width * resolutions[j].height * 2;
     	 break;
   }
   
   /* Save dimensions */
   this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] =
      resolutions[j].width;
   this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] =
      resolutions[j].height;
 
   /* Using deinterlaceing is highly recommended. Setting to true */
   this->old_interlace = 
      xine_get_param(this->stream, XINE_PARAM_VO_DEINTERLACE);
   xine_set_param(this->stream, XINE_PARAM_VO_DEINTERLACE, 1);

   /* Strip the vbi / sync signal from the image by zooming in */ 
   this->old_zoomx = xine_get_param(this->stream, XINE_PARAM_VO_ZOOM_X);
   this->old_zoomy = xine_get_param(this->stream, XINE_PARAM_VO_ZOOM_Y);
   
   xine_set_param(this->stream, XINE_PARAM_VO_ZOOM_X, 103);
   xine_set_param(this->stream, XINE_PARAM_VO_ZOOM_Y, 103);
   
   /* If we made it here, everything went ok */ 
   this->audio_only = 0;
   if (tuner_found)
      return 1;
   else
      /* Not a real error, appart that the tuner name is unknown to us */
      return 2;
}

/**
 * Open audio capture device.
 *
 * This function opens an alsa capture device. This will be used to capture
 * audio data from.
 */
int open_audio_capture_device(v4l_input_plugin_t *this)
{
#ifdef HAVE_ALSA
   DBGPRINT("Audio    Opening PCM Device\n");
   /* Allocate the snd_pcm_hw_params_t structure on the stack. */
   snd_pcm_hw_params_alloca(&this->pcm_hwparams);
   
   /* Open the PCM device. */
   if (this->audio_only) {
      /* Open the sound device in blocking mode if we are not capturing video,
       * otherwise xine gets to many NULL bufs and doesn't seem to handle
       * them correctly
       */
      if (snd_pcm_open(&this->pcm_handle, this->pcm_name, this->pcm_stream, 
	    0) < 0) {
	 PRINT("Audio :( Error opening PCM device %s\n", this->pcm_name);
   	 this->audio_capture = 0;
      }
   } else
   if (snd_pcm_open(&this->pcm_handle, this->pcm_name, this->pcm_stream, 
	    SND_PCM_NONBLOCK) < 0) {
      /* Open the sound device in non blocking mode when capturing video data
       * too, otherwise we will loose videoframes because we keep on waiting
       * for an audio fragment
       */
      PRINT("Audio :( Error opening PCM device %s\n", this->pcm_name);
      this->audio_capture = 0;
   }
  
   /* Get parameters */
   if (this->audio_capture &&
	 (snd_pcm_hw_params_any(this->pcm_handle, this->pcm_hwparams) < 0)
      ) {
      PRINT("Audio :( Can not configure this PCM device.\n");
      this->audio_capture = 0;
   }

   /* Set access type */
   if (this->audio_capture &&
	 (snd_pcm_hw_params_set_access(this->pcm_handle, this->pcm_hwparams,
				       SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
      ) {
      PRINT("Audio :( Error setting acces.\n");
      this->audio_capture = 0;
   }

   if (this->audio_capture) {
      if (snd_pcm_hw_params_any(this->pcm_handle, this->pcm_hwparams) < 0) {
	 PRINT("Audio :( Broken configuration for this PCM: No config avail\n");         this->audio_capture = 0;
      }
   }

   if (this->audio_capture) {
      snd_pcm_access_mask_t *mask = alloca(snd_pcm_access_mask_sizeof());
      snd_pcm_access_mask_none(mask);
      snd_pcm_access_mask_set(mask, SND_PCM_ACCESS_MMAP_INTERLEAVED);
      if (snd_pcm_hw_params_set_access_mask(this->pcm_handle,
	       this->pcm_hwparams, mask) < 0) {
	 PRINT("Audio :( Error setting access mask\n");
	 this->audio_capture = 0;
      }
   }

   /* Set sample format */
   if (this->audio_capture &&
	 (snd_pcm_hw_params_set_format(this->pcm_handle, this->pcm_hwparams,
				       SND_PCM_FORMAT_S16_LE) < 0)
      ) {
      PRINT("Audio :( Error setting format.\n");
      this->audio_capture = 0;
   }
   
   /* Set sample rate */
   if (this->audio_capture) {
      this->exact_rate = snd_pcm_hw_params_set_rate_near(this->pcm_handle,
	    this->pcm_hwparams, rate, &this->dir);
      if (this->dir != 0) {
	 PRINT("Audio :s The rate %d Hz is not supported by your hardware.\n",
	       rate);
	 PRINT("Audio :s ==> Using %d instead.\n", this->exact_rate);
      }
   }

   /* Set number of channels */
   if (this->audio_capture &&
	 (snd_pcm_hw_params_set_channels(this->pcm_handle,
					 this->pcm_hwparams, 2) < 0)) {
      PRINT("Audio :( Error setting channels.\n");
      this->audio_capture = 0;
   }

   if (this->audio_capture &&
	 (snd_pcm_hw_params_set_periods(this->pcm_handle, this->pcm_hwparams,
					periods, 0) < 0)) {
      PRINT("Audio :( Error setting periods.\n");
      this->audio_capture = 0;
   }

   /* Set buffersize */
   if (this->audio_capture &&
	 (snd_pcm_hw_params_set_buffer_size(this->pcm_handle, 
					    this->pcm_hwparams,
					    (periodsize * periods) >> 2) < 0)) {
      PRINT("Audio :( Error setting buffersize.\n");
      this->audio_capture = 0;
   }
   
   /* Apply HW parameter settings */
   if (this->audio_capture &&
	 (snd_pcm_hw_params(this->pcm_handle, this->pcm_hwparams) < 0)){
      PRINT("Audio :( Error Setting HW params.\n");
      this->audio_capture = 0;
   }
 
   if (this->audio_capture) {
      DBGPRINT("Audio    Allocating memory for PCM capture :%d\n", periodsize);
      this->pcm_data = (unsigned char*) malloc(periodsize);
   } else
      this->pcm_data = NULL;   

   DBGPRINT("Audio  :) Device succesfully configured\r\n");
#endif
   return 0;
}

/**
 * Adjust realtime speed
 *
 * If xine is playing at normal speed, tries to adjust xines playing speed to
 * avoid buffer overrun and buffer underrun
 */
static int v4l_adjust_realtime_speed(v4l_input_plugin_t *this, fifo_buffer_t *fifo, int speed)
{
   int num_used, num_free;
   int scr_tunning = this->scr_tunning;
   
   if (fifo == NULL)
      return 0;
  
   num_used = fifo->size(fifo);
   num_free = NUM_FRAMES - num_used;
   
   if (!this->audio_only && num_used == 0 && scr_tunning != SCR_PAUSED) {
      /* Buffer is empty, and we did not pause playback */
      report_progress(this->stream, SCR_PAUSED);
      
      if (this->stream->xine->verbosity >= XINE_VERBOSITY_LOG)
	 PRINT("Buffer is empty, pausing playback (used: %d, num_free: %d)\r\n",
	    num_used, num_free);
      
      this->scr_tunning = SCR_PAUSED;
      pvrscr_speed_tunning(this->scr, 0.0);
      this->stream->audio_out->set_property(this->stream->audio_out, AO_PROP_PAUSED, 2);
   } else
   if (num_free <= 1 && scr_tunning != SCR_SKIP) {
      this->scr_tunning = SCR_SKIP;
      PRINT("Buffer full (used: %d, free: %d)\r\n",
	       num_used, num_free);
      return 0;
   } else
   if (scr_tunning == SCR_PAUSED) {
      if (2 * num_used > num_free) {
	 /* Playback was paused, but we have normal buffer usage again */
	 if (this->stream->xine->verbosity >= XINE_VERBOSITY_LOG)
	    PRINT("Resuming playback (used: %d, free: %d)\r\n",
	       num_used, num_free);
	 
	 this->scr_tunning = 0;

	 pvrscr_speed_tunning(this->scr, 1.0);
	 this->stream->audio_out->set_property(this->stream->audio_out, AO_PROP_PAUSED, 0);
      }
   } else
   if (scr_tunning == SCR_SKIP) {
      if (num_used < 2 * num_free) {
	 DBGPRINT("Resuming from skipping (used: %d, free %d)\r\n",
	       num_used, num_free);
	 this->scr_tunning = 0;
      } else {
	 return 0;
      }
   } else
   if (speed == XINE_SPEED_NORMAL) {
      if (num_used > 2 * num_free)
	 /* buffer used > 2/3. Increase playback speed to avoid buffer
	  * overrun */
	 scr_tunning = +1;
      else if (num_free > 2 * num_used)
	 /* Buffer used < 1/3. Decrease playback speed to avoid buffer
	  * underrun */
	 scr_tunning = -1;
      else if ((scr_tunning > 0 && num_free > num_used) ||
	    (scr_tunning < 0 && num_used > num_free))
	 /* Buffer usage is ok again. Set playback speed to normal */
   	 scr_tunning = 0;
      
      /* Check if speed adjustment should be changed */ 
      if (scr_tunning != this->scr_tunning) {
	 this->scr_tunning = scr_tunning;
	 if (this->stream->xine->verbosity >= XINE_VERBOSITY_LOG)
	    PRINT("scr tunning = %d (used: %d, free: %d)\r\n", scr_tunning, num_used, num_free);
	 pvrscr_speed_tunning(this->scr, 1.0 + (0.01 * scr_tunning));
      }
   } else
   if (this->scr_tunning) {
      /* Currently speed adjustment is on. But xine is not playing at normal
       * speed, so there is no reason why we should try to adjust our playback
       * speed
       */
      this->scr_tunning = 0;

      if (this->stream->xine->verbosity >= XINE_VERBOSITY_LOG)
	 PRINT("scr tunning resetting (used: %d, free: %d\r\n", num_used, num_free);
      
      pvrscr_speed_tunning(this->scr, 1.0);
   }

   return 1;
}

/**
 * Plugin read.
 * This function is not supported by the plugin.
 */
static off_t v4l_plugin_read (input_plugin_t *this_gen, 
                              char *buf, off_t len) {
   DBGPRINT("Read not supported\r\n");
   return 0;
}

/**
 * Get time.
 * Gets a pts time value.
 */
inline static int64_t get_time() {
  struct timeval tv;
  gettimeofday(&tv,NULL);

  return (int64_t) tv.tv_sec * 90000 + (int64_t) tv.tv_usec * 9 / 100;
}


/**
 * Plugin read block
 * Reads one data block. This is either an audio frame or an video frame
 */
static buf_element_t *v4l_plugin_read_block (input_plugin_t *this_gen, 
                                             fifo_buffer_t *fifo, off_t todo)
{
   v4l_input_plugin_t   *this = (v4l_input_plugin_t *) this_gen;
   buf_element_t        *buf = NULL;
   uint8_t *ptr;
   static char video = 0;
   int speed = this->stream->xine->clock->speed;
    
   v4l_event_handler(this); 
   
   if (!this->audio_only) {
      if (!v4l_adjust_realtime_speed(this, fifo, speed)) {
         return NULL;
      }
   }

   if (!this->audio_only) 
      video = !video;
   else
      video = 0;

   DBGPRINT("%lld bytes...\n", todo);

   if (this->start_time == 0) 
      /* Create a start pts value */
      this->start_time = get_time(); /* this->stream->xine->clock->get_current_time(this->stream->xine->clock); */
 
   if (video) {
      /* Capture video */
      buf = alloc_vid_frame (this);
      this->gb_buf.frame = this->gb_frame;
					  
      DBGPRINT("input_v4l: VIDIOCMCAPTURE\n");

      while (ioctl(this->video_fd, VIDIOCMCAPTURE, &this->gb_buf) < 0) {
   	 DBGPRINT("Upper while loop\n");
   	 if (errno == EAGAIN) {
   	    DBGPRINT("Cannot sync\n");
   	    continue;
   	 } else {
	    perror("VIDIOCMCAPTURE");
	    buf->free_buffer(buf);
   	    return NULL;
   	 }
      }
   
      this->gb_frame = (this->gb_frame + 1) % this->gb_buffers.frames;
      
      while (ioctl(this->video_fd, VIDIOCSYNC, &this->gb_frame) < 0 &&
   	    (errno == EAGAIN || errno == EINTR))
      {
   	 DBGPRINT("Waiting for videosync\n");
      }
              
      /* printf ("grabbing frame #%d\n", frame_num); */
   
      ptr = this->video_buf + this->gb_buffers.offsets[this->gb_frame];
      buf->pts = get_time(); /* this->stream->xine->clock->get_current_time(this->stream->xine->clock); */
      xine_fast_memcpy (buf->content, ptr, this->frame_size); 
   } else {

#ifdef HAVE_ALSA
      /* Record audio */
      
      int pcmreturn;
      if ((pcmreturn = snd_pcm_mmap_readi(this->pcm_handle, this->pcm_data, (periodsize)>> 2)) < 0) { 
         switch (pcmreturn) {
	    case -EAGAIN:
	       /* No data available at the moment */
     	       break;
	    case -EBADFD:     /* PCM device in wrong state */
	       PRINT("Audio :( PCM is not in the right state\n");
	       break;
	    case -EPIPE:      /* Buffer overrun */
	       PRINT("Audio :( Buffer Overrun (lost some samples)\n");
	       /* On buffer overrun we need to re prepare the capturing pcm device */
	       snd_pcm_prepare(this->pcm_handle);
	       break;
	    case -ESTRPIPE:   /* Suspend event */
	       PRINT("Audio :( Suspend event occured\n");
	       break;
	    default:	      /* Unknown */
	       PRINT("Audio :o Unknown error code: %d\n", pcmreturn);
	       snd_pcm_prepare(this->pcm_handle);
	 }
      } else {
	 /* Succesfully read audio data */
	 if (rate != this->exact_rate)
	    PRINT("HELP: Should pass sample rate %d instead of %d\r\n", this->exact_rate, rate);
	 
	 if (this->pts_aud_start)
	    buf = alloc_aud_frame (this);
	 
	 /* We want the pts on the start of the sample. As the soundcard starts
	  * sampling a new sample as soon as the read function returned with a
	  * success we will save the current pts and assign the current pts to 
	  * that sample when we read it
	  */
	 
	 /* Assign start pts to sample */
	 if (buf)
	     buf->pts = this->pts_aud_start;
	 
	 /* Save start pts */
	 this->pts_aud_start = get_time(); //this->stream->xine->clock->get_current_time(this->stream->xine->clock);
   
	 if (!buf)
	    /* Skip first sample as we don't have a good pts for this one */
	    return NULL;
 
	 DBGPRINT("Audio: Data read: %d [%d, %d]. Pos: %d\r\n",
	    pcmreturn, (int) (*this->pcm_data), (int) (*(this->pcm_data + periodsize - 3)),
	    (int) this->curpos);

 
	 /* Tell decoder the number of bytes we have read */
	 buf->decoder_info[0] = pcmreturn;	      	  
	 buf->type = BUF_AUDIO_RAWPCM;
	 
	 this->curpos++;

	 xine_fast_memcpy(buf->content, this->pcm_data, periodsize);
      }
#endif
   }

   DBGPRINT("read block done\n");

   return buf;
}

/**
 * Plugin seek.
 * Not supported by the plugin.
 */
static off_t v4l_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

  DBGPRINT("input_v4l: seek %lld bytes, origin %d\n",
	  offset, origin);

  return this->curpos;
}

/**
 * Plugin get length.
 * This is a live stream, and as such does not have an known end.
 */
static off_t v4l_plugin_get_length (input_plugin_t *this_gen) {

  /*
  v4l_input_plugin_t   *this = (v4l_input_plugin_t *) this_gen; 
  off_t                 length;
  */

  return -1;
}

/**
 * Plugin get capabilitiets.
 * This plugin does not support any special capabilities.
 */
static uint32_t v4l_plugin_get_capabilities (input_plugin_t *this_gen)
{
   v4l_input_plugin_t   *this = (v4l_input_plugin_t *) this_gen; 

   if (this->audio_only)
      return 0x10;
   else
      return 0; // 0x10: Has audio only.
}

/**
 * Plugin get block size.
 * Unsupported by the plugin.
 */
static uint32_t v4l_plugin_get_blocksize (input_plugin_t *this_gen)
{
  return 0;
}

/**
 * Plugin get current pos.
 * Unsupported by the plugin.
 */
static off_t v4l_plugin_get_current_pos (input_plugin_t *this_gen){
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

  /*
  printf ("current pos is %lld\n", this->curpos);
  */

  return this->curpos;
}

/**
 * Event handler.
 * 
 * Processes events from a frontend. This way frequencies can be changed
 * without closing the v4l plugin.
 */
static void v4l_event_handler (v4l_input_plugin_t *this) {
   xine_event_t *event;

   while ((event = xine_event_get (this->event_queue))) {
      xine_set_v4l2_data_t *v4l2_data = event->data;

      switch (event->type) {
	 case XINE_EVENT_SET_V4L2:
	    if( v4l2_data->input != this->input ||
		  v4l2_data->channel != this->channel ||
		  v4l2_data->frequency != this->frequency ) {
	       this->input = v4l2_data->input;
	       this->channel = v4l2_data->channel;
	       this->frequency = v4l2_data->frequency;
	       
	       DBGPRINT("Switching to input:%d chan:%d freq:%.2f\n",
		     v4l2_data->input,
		     v4l2_data->channel,
		     (float)v4l2_data->frequency);
	       set_frequency(this, this->frequency);
	       
	       xine_demux_flush_engine(this->stream); 
	    }
	    break;
	 case XINE_EVENT_MRL_REFERENCE:
	    DBGPRINT("Got new mrl: %s\n", (char *)event->data);
	    extract_mrl(this, event->data);
	    set_frequency(this, this->frequency);
	    xine_demux_flush_engine(this->stream);
	    break;
/*	 default:

 	    DBGPRINT("Got an event, type 0x%08x\n", event->type);
 */
      }
      
      xine_event_free (event);
   }
}   

/**
 * Dispose plugin.
 *
 * Closes the plugin, restore the V4L device in the initial state (volume) and
 * frees the allocated memory
 */
static void v4l_plugin_dispose (input_plugin_t *this_gen) {
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

  if(this->mrl)
    free(this->mrl);

  if (this->scr) {
     this->stream->xine->clock->unregister_scr(this->stream->xine->clock, &this->scr->scr);
     this->scr->scr.exit(&this->scr->scr);
  }

  /* Close and free video device */
  if (this->tuner_name)
    free(this->tuner_name);

   /* Close video device only if device was openend */
   if (this->video_fd > 0) {

      /* Restore v4l audio volume */
      DBGPRINT("Video    Restoring audio volume %d\r\n", 
	    ioctl(this->video_fd, VIDIOCSAUDIO, &this->audio_saved));
      ioctl(this->video_fd, VIDIOCSAUDIO, &this->audio_saved);
      
      /* Unmap memory */
      if (this->video_buf != NULL && 
	    munmap(this->video_buf, this->gb_buffers.size) != 0) {
   	 PRINT("Video :( Could not unmap memory, reason: %s\r\n", 
	       strerror(errno));
      } else
   	 DBGPRINT("Video :) Succesfully unmapped memory (size %d)\r\n",
	       this->gb_buffers.size);
      
      DBGPRINT("Video    Closing video filehandler %d\r\n", this->video_fd);
      
      /* Now close the video device */
      if (close(this->video_fd) != 0)
   	 PRINT("Video :( Error while closing video file handler, "
	       "reason: %s\r\n", strerror(errno));
      else
   	 DBGPRINT("Video :) Device succesfully closed\r\n");

      /* Restore interlace setting */
      xine_set_param(this->stream, XINE_PARAM_VO_DEINTERLACE,
	    this->old_interlace);

      /* Restore zoom setting */ 
      xine_set_param(this->stream, XINE_PARAM_VO_ZOOM_X, this->old_zoomx);
      xine_set_param(this->stream, XINE_PARAM_VO_ZOOM_Y, this->old_zoomy);
   }
  
   if (this->radio_fd > 0) {
     close(this->radio_fd);
   }

#ifdef HAVE_ALSA
   /* Close audio device */
   if (this->pcm_handle) {
      snd_pcm_drop(this->pcm_handle);
      snd_pcm_close(this->pcm_handle);
   }

   if (this->pcm_data) {
      free(this->pcm_data);
   }

   if (this->pcm_name) {
      free(this->pcm_name);
   }
#endif

   if (this->event_queue)
      xine_event_dispose_queue (this->event_queue);

   DBGPRINT("Freeing allocated audio frames");
   if (this->aud_frames) {
      buf_element_t *cur_frame = this->aud_frames;
      buf_element_t *next_frame = NULL;
     
      while ((next_frame = cur_frame->next) != NULL) {
#ifdef LOG
	 printf("."); fflush(stdout);
#endif
	 if (cur_frame->content)
	    free(cur_frame->content);
	 
	 if (cur_frame->extra_info)
	    free(cur_frame->extra_info);
	 
	 free(cur_frame);
	 cur_frame = next_frame;
      }
   }
#ifdef LOG
   printf("\r\n");
#endif
   
   DBGPRINT("Freeing allocated video frames");
   if (this->vid_frames) {
      buf_element_t *cur_frame = this->vid_frames;
      buf_element_t *next_frame = NULL;
     
      while ((next_frame = cur_frame->next) != NULL) {
#ifdef LOG
	 printf("."); fflush(stdout);
#endif
	 if (cur_frame->content)
	    free(cur_frame->content);
	 
	 if (cur_frame->extra_info)
	    free(cur_frame->extra_info);
	 
	 free(cur_frame);
	 cur_frame = next_frame;
      }
   }
#ifdef LOG
   printf("\r\n");
#endif
   
   free (this);

   DBGPRINT("plugin     Bye bye! \r\n");
}

/**
 * Get MRL.
 *
 * Get the current MRL used by the plugin.
 */
static char* v4l_plugin_get_mrl (input_plugin_t *this_gen) {
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

  return this->mrl;
}

static int v4l_plugin_get_optional_data (input_plugin_t *this_gen, 
                                         void *data, int data_type) {
  /* v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen; */

  return INPUT_OPTIONAL_UNSUPPORTED;
}
static int v4l_plugin_radio_open (input_plugin_t *this_gen)
{
   v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

   if(open_radio_capture_device(this) != 1)
      return 0;
      
   open_audio_capture_device(this);
 
#ifdef HAVE_ALSA
   this->start_time = 0;
   this->pts_aud_start = 0;
   this->curpos = 0;
   this->event_queue = xine_event_new_queue (this->stream);
#endif 

   return 1;
}


static int v4l_plugin_video_open (input_plugin_t *this_gen)
{
   v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;
   int64_t time;

   if(!open_video_capture_device(this))
      return 0;
      
   open_audio_capture_device(this);
 
#ifdef HAVE_ALSA
   this->pts_aud_start = 0;
#endif
   this->start_time = 0;
   this->curpos = 0;
   
   /* Register our own scr provider */
   time = this->stream->xine->clock->get_current_time(this->stream->xine->clock);
   this->scr = pvrscr_init();
   this->scr->scr.start(&this->scr->scr, time);
   this->stream->xine->clock->register_scr(this->stream->xine->clock, &this->scr->scr);
   this->scr_tunning = 0;
   
   /* enable resample method */
   this->stream->xine->config->update_num(this->stream->xine->config, "audio.av_sync_method", 1);
   
   this->event_queue = xine_event_new_queue (this->stream);
   	
   return 1;
}

/**
 * Create a new instance.
 *
 * Creates a new instance of the plugin. Doesn't initialise the V4L device,
 * does initialise the structure.
 */
static input_plugin_t *v4l_class_get_instance (input_class_t *cls_gen,
		xine_stream_t *stream, const char *data)
{
   char *locator = NULL;

   /* v4l_input_class_t  *cls = (v4l_input_class_t *) cls_gen; */
   v4l_input_plugin_t *this;
   char               *mrl = strdup(data);
   
   /* Example mrl:  v4l:/Television/62500 */
   
   if (strncasecmp (mrl, "v4l:/", 5)) {
      free (mrl);
      return NULL;
   }
   
   if (mrl != NULL) {
      for (locator = mrl; *locator != '\0' && *locator !=  '/' ; locator++);
   } else
      PRINT("EUhmz, mrl was NULL?\r\n");
   
   this = (v4l_input_plugin_t *) xine_xmalloc (sizeof (v4l_input_plugin_t));
   
   extract_mrl(this, mrl);
   
   this->stream   = stream; 
   this->mrl      = mrl; 
   this->video_buf = NULL;
   this->video_fd = -1;
   this->radio_fd = -1;
   this->event_queue = NULL;
   this->scr = NULL;
#ifdef HAVE_ALSA
   this->pcm_name = NULL;
   this->pcm_data = NULL;
   this->pcm_hwparams = NULL;
   
   /* Audio */
   this->pcm_stream = SND_PCM_STREAM_CAPTURE;
   this->pcm_name = strdup("plughw:0,0");
   this->audio_capture = 1;
#endif

   pthread_mutex_init (&this->aud_frames_lock, NULL);
   pthread_cond_init  (&this->aud_frame_freed, NULL);
   
   pthread_mutex_init (&this->vid_frames_lock, NULL);
   pthread_cond_init  (&this->vid_frame_freed, NULL);
   
   this->input_plugin.get_capabilities  = v4l_plugin_get_capabilities;
   this->input_plugin.read              = v4l_plugin_read;
   this->input_plugin.read_block        = v4l_plugin_read_block;
   this->input_plugin.seek              = v4l_plugin_seek;
   this->input_plugin.get_current_pos   = v4l_plugin_get_current_pos;
   this->input_plugin.get_length        = v4l_plugin_get_length;
   this->input_plugin.get_blocksize     = v4l_plugin_get_blocksize;
   this->input_plugin.get_mrl           = v4l_plugin_get_mrl;
   this->input_plugin.dispose           = v4l_plugin_dispose;
   this->input_plugin.get_optional_data = v4l_plugin_get_optional_data;
   this->input_plugin.input_class       = cls_gen;
  
   return &this->input_plugin;
}

static input_plugin_t *v4l_class_get_video_instance (input_class_t *cls_gen,
		xine_stream_t *stream, const char *data)
{
   int is_ok = 1;
   
   v4l_input_plugin_t *this = NULL;
  
   this = (v4l_input_plugin_t *)
      v4l_class_get_instance (cls_gen, stream, data);

   if (this)
      this->input_plugin.open              = v4l_plugin_video_open;
   else
      return NULL;
  
   /* Try to see if the MRL contains a v4l device we understand */
   if (is_ok)
      extract_mrl(this, this->mrl);

   /* Try to open the video device */
   if (is_ok)
      this->video_fd = open(VIDEO_DEV, O_RDWR);
 
   if (is_ok && this->video_fd < 0) {
      DBGPRINT("(%d) Cannot open v4l device: %s\n", this->video_fd, 
	    strerror(errno));
      xine_log(this->stream->xine, XINE_LOG_MSG, 
	    PLUGIN ": Sorry, could not open %s\n", VIDEO_DEV);
	    is_ok = 0;
   } else
      DBGPRINT("Device opened, tv %d\n", this->video_fd);
   
   /* Get capabilities */
   if (is_ok && ioctl(this->video_fd,VIDIOCGCAP,&this->video_cap) < 0) {
      xine_log(this->stream->xine, XINE_LOG_MSG, 
	    PLUGIN ": Sorry your v4l card doesn't support some features"
	    " needed by xine\n");     
      DBGPRINT ("VIDIOCGCAP ioctl went wrong\n");
      is_ok = 0;;
   }
   
   if (is_ok && !(this->video_cap.type & VID_TYPE_CAPTURE)) {
      /* Capture is not supported by the device. This is a must though! */
      xine_log(this->stream->xine, XINE_LOG_MSG, 
	    PLUGIN ": Sorry, your v4l card doesn't support frame grabbing."
	    " This is needed by xine though\n");

      DBGPRINT("Grab device does not handle capture\n");
      is_ok = 0;
   }
   
   if (is_ok && set_input_source(this, this->tuner_name) <= 0) {\
      xine_log(this->stream->xine, XINE_LOG_MSG, 
	    PLUGIN ": Could not locate the tuner name [%s] on your v4l card\n",
	    this->tuner_name);
      is_ok = 0;
   }
   
   if (is_ok && this->video_fd > 0) {
      close(this->video_fd);
      this->video_fd = -1;
   }

   if (!is_ok) {
      v4l_plugin_dispose((input_plugin_t *) this);
      return NULL;
   }
  
   
   return &this->input_plugin;
}


static input_plugin_t *v4l_class_get_radio_instance (input_class_t *cls_gen,
		xine_stream_t *stream, const char *data)
{
   int is_ok = 1;
   v4l_input_plugin_t *this = NULL;
   
   if (strstr(data, "Radio") == NULL)
      return NULL;
   
   this = (v4l_input_plugin_t *)
      v4l_class_get_instance (cls_gen, stream, data);

   if (this)
      this->input_plugin.open              = v4l_plugin_radio_open;
   else
      return NULL;

   if (is_ok)
      this->radio_fd = open(RADIO_DEV, O_RDWR);

   if (this->radio_fd < 0) {
      xine_log(this->stream->xine, XINE_LOG_MSG, 
	    PLUGIN ": Allthough normally we would be able to handle this MRL,\n"
	    PLUGIN ": I am unable to open the radio device.[%s]\n", RADIO_DEV);
      is_ok = 0;
   } else
      DBGPRINT("Device opened, radio %d\n", this->radio_fd);

   if (is_ok && set_input_source(this, this->tuner_name) <= 0) {
      xine_log(this->stream->xine, XINE_LOG_MSG, 
	    PLUGIN ": Sorry, you Radio device doesn't support this tunername\n");
      is_ok = 0;
   }
 
   if (!is_ok) {
      v4l_plugin_dispose((input_plugin_t *) this);
      return NULL;
   }
  
   close(this->radio_fd);
   
   return &this->input_plugin;
}


/*
 * v4l input plugin class stuff
 */

static char *v4l_class_get_video_description (input_class_t *this_gen) {
  return _("v4l tv input plugin");
}

static char *v4l_class_get_radio_description (input_class_t *this_gen) {
  return _("v4l radio input plugin");
}


static char *v4l_class_get_identifier (input_class_t *this_gen) {
  return "v4l";
}

static void v4l_class_dispose (input_class_t *this_gen) {
  v4l_input_class_t  *this = (v4l_input_class_t *) this_gen;
  
  free (this);
}

static void *init_video_class (xine_t *xine, void *data)
{
   v4l_input_class_t  *this;
   
   this = (v4l_input_class_t *) xine_xmalloc (sizeof (v4l_input_class_t));
   
   this->xine   = xine;
   
   this->input_class.get_instance       = v4l_class_get_video_instance;
   this->input_class.get_identifier     = v4l_class_get_identifier;
   this->input_class.get_description    = v4l_class_get_video_description;
   this->input_class.get_dir            = NULL;
   this->input_class.get_autoplay_list  = NULL;
   this->input_class.dispose            = v4l_class_dispose;
   this->input_class.eject_media        = NULL;
   
   return this;
}

static void *init_radio_class (xine_t *xine, void *data)
{
   v4l_input_class_t  *this;
   
   this = (v4l_input_class_t *) xine_xmalloc (sizeof (v4l_input_class_t));
  
   this->xine   = xine;
   
   this->input_class.get_instance       = v4l_class_get_radio_instance;
   this->input_class.get_identifier     = v4l_class_get_identifier;
   this->input_class.get_description    = v4l_class_get_radio_description;
   this->input_class.get_dir            = NULL;
   this->input_class.get_autoplay_list  = NULL;
   this->input_class.dispose            = v4l_class_dispose;
   this->input_class.eject_media        = NULL;
   
   return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_INPUT, 13, "v4l_radio", XINE_VERSION_CODE, NULL, init_radio_class },
  { PLUGIN_INPUT, 13, "v4l_tv", XINE_VERSION_CODE, NULL, init_video_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

/*
 * vim:sw=3:sts=3: 
 */
