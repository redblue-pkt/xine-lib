/*
 * Copyright (C) 2003 the xine project
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

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/videodev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"

#define NUM_FRAMES  10
#define GRAB_WIDTH  768
#define GRAB_HEIGHT 576
/*
#define GRAB_WIDTH  384
#define GRAB_HEIGHT 288
*/

/*
#define LOG
*/

#if !defined(NDELAY) && defined(O_NDELAY)
#define FNDELAY O_NDELAY
#endif

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
} v4l_input_class_t;

typedef struct {
  input_plugin_t   input_plugin;

  char            *mrl;

  off_t            curpos;

  buf_element_t   *frames;
  pthread_mutex_t  frames_lock;
  pthread_cond_t   frame_freed;

  int              video_fd;
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

} v4l_input_plugin_t;

static buf_element_t *alloc_frame (v4l_input_plugin_t *this) {

  buf_element_t *frame;
  
#ifdef LOG
  printf ("input_v4l: alloc_frame. trying to get lock...\n");
#endif
	
  pthread_mutex_lock (&this->frames_lock) ;

#ifdef LOG
  printf ("input_v4l: got the lock\n");
#endif
	
  while (!this->frames) {
#ifdef LOG
    printf ("input_v4l: no frame available...\n");
#endif
    pthread_cond_wait (&this->frame_freed, &this->frames_lock);
  }
  
  frame = this->frames;
  this->frames = this->frames->next;
  
  pthread_mutex_unlock (&this->frames_lock);

#ifdef LOG
  printf ("input_v4l: alloc_frame done\n");
#endif
	
  return frame;
}

static void store_frame (buf_element_t *frame) {

  v4l_input_plugin_t *this = (v4l_input_plugin_t *) frame->source;
	
#ifdef LOG
  printf ("input_v4l: store_frame\n");
#endif
	
  pthread_mutex_lock (&this->frames_lock) ;

  frame->next  = this->frames;
  this->frames = frame;
 
  pthread_cond_signal (&this->frame_freed);
  
  pthread_mutex_unlock (&this->frames_lock);
}

static off_t v4l_plugin_read (input_plugin_t *this_gen, 
                              char *buf, off_t len) {
  return 0;
}

int64_t get_time() {
  struct timeval tv;
  gettimeofday(&tv,NULL);

  return (int64_t) tv.tv_sec * 90000 + (int64_t) tv.tv_usec * 9 / 100;
}


static buf_element_t *v4l_plugin_read_block (input_plugin_t *this_gen, 
                                             fifo_buffer_t *fifo, off_t todo) {
  v4l_input_plugin_t   *this = (v4l_input_plugin_t *) this_gen;
  buf_element_t        *buf;
  uint8_t *ptr;


#ifdef LOG
  printf ("input_v4l: %lld bytes...\n",
          todo);
#endif

  buf = alloc_frame (this);
  
  this->gb_buf.frame = this->gb_frame;

#ifdef LOG
  printf ("input_v4l: VIDIOCMCAPTURE\n");
#endif


  while (ioctl(this->video_fd, VIDIOCMCAPTURE, &this->gb_buf) < 0) {
    printf("input_v4l: upper while loop\n");
    if (errno == EAGAIN)
      printf ("input_v4l: cannot sync\n");
    else {
      perror("VIDIOCMCAPTURE");
      return NULL;
    }
  }

  this->gb_frame = (this->gb_frame + 1) % this->gb_buffers.frames;

  while (ioctl(this->video_fd, VIDIOCSYNC, &this->gb_frame) < 0 &&
	 (errno == EAGAIN || errno == EINTR))
  {
    printf("input_v4l: waiting for videosync\n");
  }

  if (this->start_time == 0) 
    this->start_time = get_time();
  buf->pts = (get_time() - this->start_time)+50*3600;
  
  /* printf ("grabbing frame #%d\n", frame_num); */

  ptr = this->video_buf + this->gb_buffers.offsets[this->gb_frame];

  xine_fast_memcpy (buf->content, ptr, this->frame_size); 

#ifdef LOG
  printf("input_v4l: read block done\n");
#endif

  return buf;
}

static off_t v4l_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

#ifdef LOG
  printf ("input_v4l: seek %lld bytes, origin %d\n",
	  offset, origin);
#endif

  return this->curpos;
}

static off_t v4l_plugin_get_length (input_plugin_t *this_gen) {

  /*
  v4l_input_plugin_t   *this = (v4l_input_plugin_t *) this_gen; 
  off_t                 length;
  */

  return -1;
}

static uint32_t v4l_plugin_get_capabilities (input_plugin_t *this_gen) {
  return 0;
}

static uint32_t v4l_plugin_get_blocksize (input_plugin_t *this_gen) {
  return 0;
}

static off_t v4l_plugin_get_current_pos (input_plugin_t *this_gen){
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

  /*
  printf ("current pos is %lld\n", this->curpos);
  */

  return this->curpos;
}

static void v4l_plugin_dispose (input_plugin_t *this_gen) {
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

  if(this->mrl)
    free(this->mrl);

  close(this->video_fd);

  free (this);
}

static char* v4l_plugin_get_mrl (input_plugin_t *this_gen) {
  v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen;

  return this->mrl;
}

static int v4l_plugin_get_optional_data (input_plugin_t *this_gen, 
                                         void *data, int data_type) {
  /* v4l_input_plugin_t *this = (v4l_input_plugin_t *) this_gen; */

  return INPUT_OPTIONAL_UNSUPPORTED;
}


static input_plugin_t *open_plugin (input_class_t *cls_gen, xine_stream_t *stream, 
				    const char *data) {

  /* v4l_input_class_t  *cls = (v4l_input_class_t *) cls_gen; */
  v4l_input_plugin_t *this;
  int                 i, ret;
  char               *mrl = strdup(data);

#ifdef LOG
  printf ("input_v4l: trying to open '%s'\n", mrl);
#endif

  if (strncasecmp (mrl, "v4l://", 6)) {
    free (mrl);
    return NULL;
  }

  this = (v4l_input_plugin_t *) xine_xmalloc (sizeof (v4l_input_plugin_t));

  this->mrl    = mrl; 
  
  /*
   * pre-alloc a bunch of frames
   */

  pthread_mutex_init (&this->frames_lock, NULL);
  pthread_cond_init  (&this->frame_freed, NULL);
  
  for (i=0; i<NUM_FRAMES; i++) {

    buf_element_t *frame;

    frame = xine_xmalloc (sizeof (buf_element_t));

    frame->decoder_info[0] = GRAB_WIDTH;
    frame->decoder_info[1] = GRAB_HEIGHT;
    frame->content         = xine_xmalloc (frame->decoder_info[0] * frame->decoder_info[1] * 3 / 2);
    frame->type            = BUF_VIDEO_YUV_FRAMES;

    frame->source          = this;
    frame->free_buffer     = store_frame;
    frame->extra_info      = xine_xmalloc(sizeof(extra_info_t));
    
    store_frame (frame);
  }

  this->video_fd = open("/dev/video0", O_RDWR);
  if (this->video_fd < 0) {
    printf ("input_v4l: cannot open v4l device\n");
    free(this);
    return NULL;
  }
   
  if (ioctl(this->video_fd,VIDIOCGCAP,&this->video_cap) < 0) {
    printf ("input_v4l: VIDIOCGCAP ioctl went wrong\n");
    free(this);
    return NULL;
  }

  if (!(this->video_cap.type & VID_TYPE_CAPTURE)) {
    printf ("input_v4l: grab device does not handle capture\n");
    free(this);
    return NULL;
  }
  /* unmute audio */
  ioctl(this->video_fd, VIDIOCGAUDIO, &this->audio);
  memcpy(&this->audio_saved, &this->audio, sizeof(this->audio));
  this->audio.flags &= ~VIDEO_AUDIO_MUTE;
  ioctl(this->video_fd, VIDIOCSAUDIO, &this->audio);

  ret = ioctl(this->video_fd,VIDIOCGMBUF, &this->gb_buffers);
  if (ret < 0) {
    /* try to use read based access */
    struct video_picture pict;
    int val;

    ioctl(this->video_fd, VIDIOCGPICT, &pict);
#if 0
    printf("v4l: colour=%d hue=%d brightness=%d constrast=%d whiteness=%d\n",
	   pict.colour,
	   pict.hue,
	   pict.brightness,
	   pict.contrast,
	   pict.whiteness);
#endif        
    /* try to choose a suitable video format */
    pict.palette=VIDEO_PALETTE_YUV420P;
    ret = ioctl(this->video_fd, VIDIOCSPICT, &pict);
    if (ret < 0) {
      pict.palette=VIDEO_PALETTE_YUV422;
      ret = ioctl(this->video_fd, VIDIOCSPICT, &pict);
      if (ret < 0) {
	close (this->video_fd);
	this->video_fd = -1;
	printf ("input_v4l: grab: no colorspace format found\n");
	return 0;
      } else
	printf ("input_v4l: grab: format YUV 4:2:2\n");
    } else
      printf ("input_v4l: grab: format YUV 4:2:0\n");

    this->frame_format = pict.palette;

    val = 1;
    ioctl(this->video_fd, VIDIOCCAPTURE, &val);

    this->use_mmap = 0;

  } else {

    printf ("input_v4l: using mmap, size %d\n", this->gb_buffers.size);

    this->video_buf = mmap(0, this->gb_buffers.size,
			   PROT_READ|PROT_WRITE, MAP_SHARED,
			   this->video_fd,0);
    if ((unsigned char*)-1 == this->video_buf) {
      perror("mmap");
      close (this->video_fd);
      free(this);
      return NULL;
    }
    this->gb_frame = 0;
        
    /* start to grab the first frame */
    this->gb_buf.frame = (this->gb_frame + 1) % this->gb_buffers.frames;
    this->gb_buf.height = GRAB_HEIGHT;
    this->gb_buf.width = GRAB_WIDTH;
    this->gb_buf.format = VIDEO_PALETTE_YUV420P;
        
    ret = ioctl(this->video_fd, VIDIOCMCAPTURE, &this->gb_buf);
    if (ret < 0 && errno != EAGAIN) {
      /* try YUV422 */
      this->gb_buf.format = VIDEO_PALETTE_YUV422;
            
      ret = ioctl(this->video_fd, VIDIOCMCAPTURE, &this->gb_buf);
    } else
      printf ("input_v4l: YUV420 should work\n");

    if (ret < 0) {
      if (errno != EAGAIN) {
	printf("input_v4l: grab device does not support suitable format\n");
      } else {
	printf("input_v4l: grab device does not receive any video signal\n");
      }
      close (this->video_fd);
      free (this);
      return NULL;
    }
    this->frame_format = this->gb_buf.format;
    this->use_mmap = 1;
  }

  switch(this->frame_format) {
  case VIDEO_PALETTE_YUV420P:
    this->frame_size = ( GRAB_WIDTH *  GRAB_HEIGHT * 3) / 2;
    break;
  case VIDEO_PALETTE_YUV422:
    this->frame_size =  GRAB_WIDTH *  GRAB_HEIGHT * 2;
    break;
  }


  this->start_time=0;
  
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

/*
 * v4l input plugin class stuff
 */

static char *v4l_class_get_description (input_class_t *this_gen) {
  return _("v4l input plugin");
}

static char *v4l_class_get_identifier (input_class_t *this_gen) {
  return "v4l";
}

static void v4l_class_dispose (input_class_t *this_gen) {
  v4l_input_class_t  *this = (v4l_input_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {

  v4l_input_class_t  *this;

  this = (v4l_input_class_t *) xine_xmalloc (sizeof (v4l_input_class_t));

  this->xine   = xine;

  this->input_class.open_plugin        = open_plugin;
  this->input_class.get_identifier     = v4l_class_get_identifier;
  this->input_class.get_description    = v4l_class_get_description;
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
  { PLUGIN_INPUT, 11, "v4l", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};


