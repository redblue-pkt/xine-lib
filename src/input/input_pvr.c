/*
 * Copyright (C) 2000-2003 the xine project
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
 * pvr input plugin for WinTV-PVR 250/350 pci cards using driver from:
 *   http://ivtv.sf.net
 *
 * features:
 *   - play live mpeg2 stream (realtime mode) while recording
 *   - can pause, play slow, fast and seek back into the recorded stream
 *   - switches back to realtime mode if played until the end
 *   - may erase files as they get old
 *
 * requires:
 *   - audio.av_sync_method=resample
 *   - ivtv driver must be set to send dvd-like mpeg2 stream.
 *   - the stream must start with the mpeg marker (00 00 01 ba)
 *
 * todo:
 *   - event processing code to switch channels, start and stop recording.
 *
 * usage:
 *   xine pvr:/<path_to_store_files>
 *
 * $Id: input_pvr.c,v 1.3 2003/03/05 03:01:59 tmmm Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "input_plugin.h"

#define PVR_DEVICE        "/dev/ivtv0"
#define PVR_BLOCK_SIZE    2048			/* pvr works with dvd-like data */
#define BLOCKS_PER_PAGE   10000                 /* 200MB per page. each session can have several pages */
#define PVR_FILENAME      "%s/%08d-%04d.vob"
#define PVR_FILENAME_SIZE 1+8+1+4+4+1

#define LOG 1

typedef struct pvrscr_s pvrscr_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
  config_values_t  *config;
  
} pvr_input_class_t;


typedef struct {
  input_plugin_t     input_plugin;

  pvr_input_class_t *class;
  
  xine_stream_t     *stream;
                   
  pvrscr_t          *scr;
  int                scr_tunning;
    
  uint32_t           session;		/* session number used to identify the pvr file */
  
  int                dev_fd;		/* fd of the mpeg2 encoder device */
  int                rec_fd;		/* fd of the current recording file (session/page) */
  int                play_fd;		/* fd of the current playback (-1 when realtime) */

  uint32_t           rec_blk;		/* next block to record */
  uint32_t           rec_page;		/* page of current rec_fd file */
  uint32_t           play_blk;		/* next block to play */
  uint32_t           play_page;		/* page of current play_fd file */
  uint32_t           first_page;	/* first page available (not erased yet) */
  uint32_t           max_page_age;	/* max age to retire (erase) pages */
  
  char              *mrl;
  char              *path;
  
  /* buffer to pass data from pvr thread to xine */
  uint8_t            data[PVR_BLOCK_SIZE];
  int                valid_data;
  int                want_data;
  
  pthread_mutex_t    lock;
  pthread_cond_t     has_valid_data;
  pthread_cond_t     wake_pvr;
  pthread_t          pvr_thread;
  int                pvr_running;
  
} pvr_input_plugin_t;



/*
 * ***************************************************
 * unix System Clock Reference + fine tunning
 *
 * on an ideal world we would be using scr from mpeg2
 * encoder just like dxr3 does. 
 * unfortunately it is not supported by ivtv driver,
 * and perhaps not even possible with wintv cards.
 *
 * the fine tunning option is used to change play
 * speed in order to regulate fifo usage, that is,
 * trying to match the rate of generated data.
 *
 * OBS: use with audio.av_sync_method=resample
 * ***************************************************
 */

struct pvrscr_s {
  scr_plugin_t     scr;

  struct timeval   cur_time;
  int64_t          cur_pts;
  int              xine_speed;
  double           speed_factor;
  double           speed_tunning;

  pthread_mutex_t  lock;

};

static int pvrscr_get_priority (scr_plugin_t *scr) {
  return 10; /* high priority */
}

/* Only call this when already mutex locked */
static void pvrscr_set_pivot (pvrscr_t *this) {

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

  return ;
}

static int pvrscr_set_speed (scr_plugin_t *scr, int speed) {
  pvrscr_t *this = (pvrscr_t*) scr;

  pthread_mutex_lock (&this->lock);

  pvrscr_set_pivot( this );
  this->xine_speed   = speed;
  this->speed_factor = (double) speed * 90000.0 / 4.0 * 
                       this->speed_tunning;

  pthread_mutex_unlock (&this->lock);

  return speed;
}

static void pvrscr_speed_tunning (pvrscr_t *this, double factor) {
  pthread_mutex_lock (&this->lock);

  pvrscr_set_pivot( this );
  this->speed_tunning = factor;
  this->speed_factor = (double) this->xine_speed * 90000.0 / 4.0 * 
                       this->speed_tunning;

  pthread_mutex_unlock (&this->lock);
}

static void pvrscr_adjust (scr_plugin_t *scr, int64_t vpts) {
  pvrscr_t *this = (pvrscr_t*) scr;
  struct   timeval tv;

  pthread_mutex_lock (&this->lock);

  gettimeofday(&tv, NULL);
  this->cur_time.tv_sec=tv.tv_sec;
  this->cur_time.tv_usec=tv.tv_usec;
  this->cur_pts = vpts;

  pthread_mutex_unlock (&this->lock);
}

static void pvrscr_start (scr_plugin_t *scr, int64_t start_vpts) {
  pvrscr_t *this = (pvrscr_t*) scr;

  pthread_mutex_lock (&this->lock);

  gettimeofday(&this->cur_time, NULL);
  this->cur_pts = start_vpts;

  pthread_mutex_unlock (&this->lock);
  
  pvrscr_set_speed (&this->scr, XINE_SPEED_NORMAL);
}

static int64_t pvrscr_get_current (scr_plugin_t *scr) {
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

static void pvrscr_exit (scr_plugin_t *scr) {
  pvrscr_t *this = (pvrscr_t*) scr;

  pthread_mutex_destroy (&this->lock);
  free(this);
}

static pvrscr_t* pvrscr_init (void) {
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
#ifdef LOG
  printf("input_pvr: scr init complete\n");
#endif

  return this;
}



static uint32_t pvr_plugin_get_capabilities (input_plugin_t *this_gen) {

  /*pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;*/

  return INPUT_CAP_BLOCK | INPUT_CAP_SEEKABLE;
}


static off_t pvr_plugin_read (input_plugin_t *this_gen, char *buf, off_t len) {
  /*pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;*/

  /* FIXME: Tricking the demux_mpeg_block plugin */
  buf[0] = 0;
  buf[1] = 0;
  buf[2] = 0x01;
  buf[3] = 0xba;
  return 4;
}


/* 
 * this function will adjust playback speed to control buffer utilization.
 * we must avoid:
 * - overflow: buffer runs full. no data is read from the mpeg2 card so it will discard
 *             mpeg2 packets and get out of sync with the block size.
 * - underrun: buffer gets empty. playback will suffer a pausing effect, also discarding
 *             video frames.
 *
 * OBS: use with audio.av_sync_method=resample
 */
static void pvr_adjust_realtime_speed(pvr_input_plugin_t *this, fifo_buffer_t *fifo, int speed ) {
  
  int num_used, num_free;
  int scr_tunning = this->scr_tunning;
  
  num_used = fifo->size(fifo);
  num_free = fifo->num_free(fifo);
  
  if( num_used == 0 && scr_tunning != -2 ) {
    
    /* buffer is empty. pause it for a while */
    this->scr_tunning = -2; /* marked as paused */
    pvrscr_speed_tunning(this->scr, 0.0);
#ifdef LOG
    printf("input_pvr: buffer empty, pausing playback\n" );
#endif
  
  } else if( scr_tunning == -2 ) {
    
    /* currently paused, revert to normal if 1/3 full */
    if( 2*num_used > num_free ) {
      this->scr_tunning = 0;
      
      pvrscr_speed_tunning(this->scr, 1.0 );
#ifdef LOG
      printf("input_pvr: resuming playback\n" );
#endif
    }
  
  } else if( speed == XINE_SPEED_NORMAL && this->play_fd == -1 ) {
  
    /* when playing realtime, adjust the scr to make xine buffers half full */
    if( num_used > 2*num_free )
      scr_tunning = +1; /* play faster */  
    else if( num_free > 2*num_used )
      scr_tunning = -1; /* play slower */
    else if( (scr_tunning > 0 && num_free > num_used) ||
             (scr_tunning < 0 && num_used > num_free) )
      scr_tunning = 0;
    
    if( scr_tunning != this->scr_tunning ) {
      this->scr_tunning = scr_tunning;
#ifdef LOG
      printf("input_pvr: scr_tunning = %d (used: %d free: %d)\n", scr_tunning, num_used, num_free );
#endif
      
      /* make it play .5% faster or slower */
      pvrscr_speed_tunning(this->scr, 1.0 + (0.005 * scr_tunning) );
    }
  
  } else if( this->scr_tunning ) {
    this->scr_tunning = 0;
      
    pvrscr_speed_tunning(this->scr, 1.0 );
  }
}

/*
 * check the status of recording file, open new one as needed and write the current data. 
 */
static int pvr_rec_file(pvr_input_plugin_t *this) {
  
  off_t pos;

  if( this->session == -1 )
    return 1;
  
  /* check if it's time to change page/file */
  if( (this->rec_blk / BLOCKS_PER_PAGE) != this->rec_page || this->rec_fd == -1) {
    char filename[strlen(this->path) + PVR_FILENAME_SIZE];
     
    if( this->rec_fd != -1 && this->rec_fd != this->play_fd ) {
      close(this->rec_fd);  
    }
     
    this->rec_page = this->rec_blk / BLOCKS_PER_PAGE;
    sprintf(filename, PVR_FILENAME, this->path, this->session, this->rec_page);
     
#ifdef LOG
    printf("input_pvr: opening pvr file for writing (%s)\n", filename);
#endif
     
    this->rec_fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0666 );
    if( this->rec_fd == -1 ) {
      printf("input_pvr: error creating pvr file (%s)\n", filename);
      return 0;
    }
     
    /* erase first page if old */
    if( this->max_page_age != -1 && 
        this->rec_page - this->max_page_age == this->first_page ) {
      sprintf(filename, PVR_FILENAME, this->path, this->session, this->first_page);

#ifdef LOG
      printf("input_pvr: erasing old pvr file (%s)\n", filename);
#endif
       
      this->first_page++;
      if(this->play_blk / BLOCKS_PER_PAGE < this->first_page) {
        this->play_blk = this->first_page * BLOCKS_PER_PAGE;
        if( this->play_fd != -1 )
          close(this->play_fd);
        this->play_fd = -1;
      }
       
      remove(filename);
    }     
  }
  pos = (off_t)(this->rec_blk % BLOCKS_PER_PAGE) * PVR_BLOCK_SIZE;
  if( lseek (this->rec_fd, pos, SEEK_SET) != pos ) {
    printf("input_pvr: error setting position for writing %lld\n", pos);
    return 0;
  }
  if( this->rec_fd != -1 ) {
    if( write(this->rec_fd, this->data, PVR_BLOCK_SIZE) < PVR_BLOCK_SIZE ) {
      printf("input_pvr: short write to pvr file\n");
      return 0;
    }
    this->rec_blk++;
  }
 
  return 1;
}

/* 
 * check for playback mode, switching realtime <-> non-realtime.
 * gets data from file in non-realtime mode.
 */
static int pvr_play_file(pvr_input_plugin_t *this, fifo_buffer_t *fifo, uint8_t *buffer, int speed) {
  
  off_t pos;

  /* check for realtime. don't switch back unless enough buffers are
   * free to not block the pvr thread */
  if( this->play_blk >= this->rec_blk-1 && speed >= XINE_SPEED_NORMAL &&
      (this->play_fd == -1 || fifo->size(fifo) < fifo->num_free(fifo)) ) {
     
    this->play_blk = this->rec_blk-1;
     
    if( speed > XINE_SPEED_NORMAL ) {
      this->stream->xine->clock->set_speed (this->stream->xine->clock, XINE_SPEED_NORMAL);
    }
    
    if( this->play_fd != -1 ) {
      if(this->play_fd != this->rec_fd )
        close(this->play_fd);  
      this->play_fd = -1;
     
#ifdef LOG
      printf("input_pvr: switching back to realtime\n");
#endif
    }
    this->want_data = 1;
  
  } else {
    
    if( this->rec_fd == -1 )
      return 1;

    if( (this->play_blk / BLOCKS_PER_PAGE) != this->play_page || this->play_fd == -1 ) {
       char filename[strlen(this->path) + PVR_FILENAME_SIZE];
       
#ifdef LOG
       if(this->play_fd == -1)
         printf("input_pvr: switching to non-realtime\n");
#endif
       
       if( this->play_fd != -1 && this->play_fd != this->rec_fd ) {
         close(this->play_fd);  
       }
       
       this->play_page = this->play_blk / BLOCKS_PER_PAGE;
       if( this->play_page < this->first_page ) {
         this->play_page = this->first_page;
         this->play_blk = this->play_page * BLOCKS_PER_PAGE;
       }
       
       /* check if we can reuse the same handle */
       if( this->play_page == this->rec_page ) {
         this->play_fd = this->rec_fd;
       } else {
         sprintf(filename, PVR_FILENAME, this->path, this->session, this->play_page);

#ifdef LOG
         printf("input_pvr: opening pvr file for reading (%s)\n", filename);
#endif
         
         this->play_fd = open(filename, O_RDONLY );
         if( this->play_fd == -1 ) {
           printf("input_pvr: error opening pvr file (%s)\n", filename);
           return 0;
         }
      }
      this->want_data = 0;
      pthread_cond_signal (&this->wake_pvr);
    }
    
    if(speed != XINE_SPEED_PAUSE) {
      
      /* cannot run faster than the writing thread */
      while( this->play_blk >= this->rec_blk-1 )
        pthread_cond_wait (&this->has_valid_data, &this->lock);

      pos = (off_t)(this->play_blk % BLOCKS_PER_PAGE) * PVR_BLOCK_SIZE;
      if( lseek (this->play_fd, pos, SEEK_SET) != pos ) {
        printf("input_pvr: error setting position for reading %lld\n", pos);
        return 0;
      }
      if( read(this->play_fd, buffer, PVR_BLOCK_SIZE) < PVR_BLOCK_SIZE ) {
        printf("input_pvr: short read from pvr file\n");
        return 0;
      }
      this->play_blk++;
    }
  }
  return 1;
}


static int pvr_mpeg_resync (int fd) {
  uint32_t seq = 0;
  uint8_t c;

  while (seq != 0x000001ba) {
    if( read(fd, &c, 1) < 1 )
      return 0;
    seq = (seq << 8) | c;
  }
  return 1;
}

/*
 * captures data from mpeg2 encoder card to disk.
 * may wait xine to get data when in realtime mode.
 */
static void *pvr_loop (void *this_gen) {

  pvr_input_plugin_t   *this = (pvr_input_plugin_t *) this_gen;
  off_t                 num_bytes, total_bytes;
  int                   lost_sync;

  while( this->pvr_running ) {
    
    pthread_mutex_lock(&this->lock);
    this->valid_data = 0;
    pthread_mutex_unlock(&this->lock);
    
    total_bytes = 0;
    do {
      
      lost_sync = 0;
      
      while (total_bytes < PVR_BLOCK_SIZE) {
        num_bytes = read (this->dev_fd, this->data + total_bytes, PVR_BLOCK_SIZE-total_bytes);
        if (num_bytes <= 0) {
          if (num_bytes < 0) 
            printf ("input_pvr: read error (%s)\n", strerror(errno));
          this->pvr_running = 0;  
          break;
        }
        total_bytes += num_bytes;
      }
      
      if( this->data[0] || this->data[1] || this->data[2] != 1 || this->data[3] != 0xba ) {
#ifdef LOG
        printf("input_pvr: resyncing mpeg stream\n");
#endif
        if( !pvr_mpeg_resync(this->dev_fd) ) {
          this->pvr_running = 0;
          break;
        }
        lost_sync = 1;
        this->data[0] = 0; this->data[1] = 0; this->data[2] = 1; this->data[3] = 0xba;
        total_bytes = 4;
      }      
    } while( lost_sync );   
    
    pthread_mutex_lock(&this->lock);
    
    if( !pvr_rec_file(this) ) {
      this->pvr_running = 0;  
      break;
    }
    
    this->valid_data = 1;
    pthread_cond_signal (&this->has_valid_data);

    while(this->valid_data && this->play_fd == -1 && 
          this->want_data) {
      pthread_cond_wait (&this->wake_pvr, &this->lock);
    }
        
    pthread_mutex_unlock(&this->lock);
  }
  
  pthread_exit(NULL);
}

/*
 * pvr read_block function.
 * - adjust playing speed to keep buffers half-full
 * - check current playback mode
 * - get data from file (non-realtime) or the pvr thread (realtime)
 */
static buf_element_t *pvr_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {

  pvr_input_plugin_t   *this = (pvr_input_plugin_t *) this_gen;
  buf_element_t        *buf;
  int                   speed = this->stream->xine->clock->speed;
  
  pvr_adjust_realtime_speed(this, fifo, speed);

  if( !this->pvr_running ) {
    printf("input_pvr: thread died, aborting\n");
    return NULL;  
  }
  
  buf = fifo->buffer_pool_alloc (fifo);
  buf->content = buf->mem;
    
  pthread_mutex_lock(&this->lock);
  
  if( !pvr_play_file(this, fifo, buf->content, speed) )
    return NULL;
  
  if( todo == PVR_BLOCK_SIZE && speed != XINE_SPEED_PAUSE ) {
    buf->type = BUF_DEMUX_BLOCK;
    buf->size = PVR_BLOCK_SIZE;
    
    if(this->play_fd == -1) {
      
      /* realtime mode: wait for valid data from pvr thread */
      this->want_data = 1;
      while(!this->valid_data && this->pvr_running)
        pthread_cond_wait (&this->has_valid_data, &this->lock);
      
      this->play_blk = this->rec_blk;
      xine_fast_memcpy(buf->content, this->data, PVR_BLOCK_SIZE);
    
      this->valid_data = 0;
      pthread_cond_signal (&this->wake_pvr);
    }
  } else {
    buf->type = BUF_CONTROL_NOP;
    buf->size = 0;    
  }
  
  pthread_mutex_unlock(&this->lock);
  
  return buf;
}


static off_t pvr_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;

  pthread_mutex_lock(&this->lock);
  
  switch( origin ) {
    case SEEK_SET:
      this->play_blk = (offset / PVR_BLOCK_SIZE) + (this->first_page * BLOCKS_PER_PAGE);
      break;
    case SEEK_CUR:
      this->play_blk += offset / PVR_BLOCK_SIZE;
      break;
    case SEEK_END:
      this->play_blk = this->rec_blk + (offset / PVR_BLOCK_SIZE);
      break;
  }
  pthread_mutex_unlock(&this->lock);
  
  return (off_t) (this->play_blk - this->first_page * BLOCKS_PER_PAGE)  * PVR_BLOCK_SIZE;
}

static off_t pvr_plugin_get_current_pos (input_plugin_t *this_gen){
  pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;

  return (off_t) (this->play_blk - this->first_page * BLOCKS_PER_PAGE) * PVR_BLOCK_SIZE;
}

static off_t pvr_plugin_get_length (input_plugin_t *this_gen) {

  pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;

  return (off_t) (this->rec_blk - this->first_page * BLOCKS_PER_PAGE) * PVR_BLOCK_SIZE;
}

static uint32_t pvr_plugin_get_blocksize (input_plugin_t *this_gen) {
  return PVR_BLOCK_SIZE;
}

static char* pvr_plugin_get_mrl (input_plugin_t *this_gen) {
  pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;

  return this->mrl;
}

static int pvr_plugin_get_optional_data (input_plugin_t *this_gen, 
					  void *data, int data_type) {
  
  return INPUT_OPTIONAL_UNSUPPORTED;
}

static void pvr_plugin_dispose (input_plugin_t *this_gen ) {
  pvr_input_plugin_t *this = (pvr_input_plugin_t *) this_gen;
  void          *p;

#ifdef LOG
  printf("input_pvr: finishing pvr thread\n");
#endif
  pthread_mutex_lock(&this->lock);
  this->pvr_running = 0;
  this->want_data = 0;
  pthread_cond_signal (&this->wake_pvr);
  pthread_mutex_unlock(&this->lock);
  pthread_join (this->pvr_thread, &p); 
#ifdef LOG
  printf("input_pvr: pvr thread joined\n");
#endif
  
  if (this->scr) {
    this->stream->xine->clock->unregister_scr(this->stream->xine->clock, &this->scr->scr);
    this->scr->scr.exit(&this->scr->scr);
  }

  close(this->dev_fd);

  if( this->rec_fd != -1 )
    close(this->rec_fd);  
  
  if( this->play_fd != -1 && this->play_fd != this->rec_fd )
    close(this->play_fd);
  
  free (this->mrl);

  free (this);
}

static input_plugin_t *open_plugin (input_class_t *cls_gen, xine_stream_t *stream, 
				    const char *data) {

  pvr_input_class_t  *cls = (pvr_input_class_t *) cls_gen;
  pvr_input_plugin_t *this;
  char                *mrl = strdup(data);
  char                *path;
  int                  dev_fd;
  int64_t              time;
  int                  err;
  
  if (!strncasecmp (mrl, "pvr:/", 5)) 
    path = &mrl[5];
  else
    return NULL;
  
  if(!strlen(path))
    path = ".";

  dev_fd = open (PVR_DEVICE, O_RDWR);

  if (dev_fd == -1) {
    printf("input_pvr: error opening device %s\n", PVR_DEVICE );
    free (mrl);
    return NULL;
  }

  this = (pvr_input_plugin_t *) xine_xmalloc (sizeof (pvr_input_plugin_t));
  this->class  = cls;
  this->stream = stream;
  this->mrl    = mrl;
  this->path   = path;
  this->dev_fd = dev_fd;

  this->input_plugin.get_capabilities   = pvr_plugin_get_capabilities;
  this->input_plugin.read               = pvr_plugin_read;
  this->input_plugin.read_block         = pvr_plugin_read_block;
  this->input_plugin.seek               = pvr_plugin_seek;
  this->input_plugin.get_current_pos    = pvr_plugin_get_current_pos;
  this->input_plugin.get_length         = pvr_plugin_get_length;
  this->input_plugin.get_blocksize      = pvr_plugin_get_blocksize;
  this->input_plugin.get_mrl            = pvr_plugin_get_mrl;
  this->input_plugin.get_optional_data  = pvr_plugin_get_optional_data;
  this->input_plugin.dispose            = pvr_plugin_dispose;
  this->input_plugin.input_class        = cls_gen;

  /* register our own scr provider */   
  time = this->stream->xine->clock->get_current_time(this->stream->xine->clock);
  this->scr = pvrscr_init();
  this->scr->scr.start(&this->scr->scr, time);
  this->stream->xine->clock->register_scr(this->stream->xine->clock, &this->scr->scr);
  this->scr_tunning = 0;
    
  this->session = 0;
  this->rec_fd = -1;
  this->rec_page = -1;
  this->play_fd = -1;
  this->play_page = -1;
  this->first_page = 0;
  this->max_page_age = 3;
  
  this->pvr_running = 1;
  pthread_mutex_init (&this->lock, NULL);
  pthread_cond_init  (&this->has_valid_data,NULL);
  pthread_cond_init  (&this->wake_pvr,NULL);
  
  if ((err = pthread_create (&this->pvr_thread,
			     NULL, pvr_loop, this)) != 0) {
    fprintf (stderr, "input_pvr: can't create new thread (%s)\n",
	     strerror(err));
    abort();
  }
  
  return &this->input_plugin;
}


/*
 * plugin class functions
 */

static char *pvr_class_get_description (input_class_t *this_gen) {
  return _("WinTV-PVR 250/350 input plugin");
}

static char *pvr_class_get_identifier (input_class_t *this_gen) {
  return "pvr";
}


static void pvr_class_dispose (input_class_t *this_gen) {
  pvr_input_class_t  *this = (pvr_input_class_t *) this_gen;

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  pvr_input_class_t  *this;
  config_values_t     *config;

  this = (pvr_input_class_t *) xine_xmalloc (sizeof (pvr_input_class_t));

  this->xine   = xine;
  this->config = xine->config;
  config       = xine->config;
  
  this->input_class.open_plugin        = open_plugin;
  this->input_class.get_identifier     = pvr_class_get_identifier;
  this->input_class.get_description    = pvr_class_get_description;
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = pvr_class_dispose;
  this->input_class.eject_media        = NULL;

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_INPUT, 11, "pvr", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

