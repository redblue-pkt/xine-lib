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
 * $Id: demux_mpeg.c,v 1.91 2002/10/27 00:01:13 guenter Exp $
 *
 * demultiplexer for mpeg 1/2 program streams
 * reads streams of variable blocksizes
 *
 * currently only used for mpeg-1-files
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>

#include "xine_internal.h"
#include "demux.h"
#include "xineutils.h"

#define NUM_PREVIEW_BUFFERS 150

#define WRAP_THRESHOLD       120000

#define PTS_AUDIO 0
#define PTS_VIDEO 1

typedef struct demux_mpeg_s {

  demux_plugin_t       demux_plugin;

  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;

  xine_stream_t	      *stream;
  input_plugin_t      *input;

  pthread_t            thread;
  int                  thread_running;
  pthread_mutex_t      mutex;

  unsigned char        dummy_space[100000];

  int                  status;
  int                  preview_mode;

  int                  rate;

  int                  send_end_buffers;

  int64_t              last_pts[2];
  int                  send_newpts;
  int                  buf_flag_seek;
  int                  has_pts;

} demux_mpeg_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_mpeg_class_t;

/*
 * borrow a little knowledge from the Quicktime demuxer
 */
#include "bswap.h"

#define QT_ATOM( ch0, ch1, ch2, ch3 )                                \
        ( (long)(unsigned char)(ch3) | ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | ( (long)(unsigned char)(ch0) << 24 ) )

/* these are the known top-level QT atoms */
#define FREE_ATOM QT_ATOM('f', 'r', 'e', 'e')
#define JUNK_ATOM QT_ATOM('j', 'u', 'n', 'k')
#define MDAT_ATOM QT_ATOM('m', 'd', 'a', 't')
#define MOOV_ATOM QT_ATOM('m', 'o', 'o', 'v')
#define PNOT_ATOM QT_ATOM('p', 'n', 'o', 't')
#define SKIP_ATOM QT_ATOM('s', 'k', 'i', 'p')
#define WIDE_ATOM QT_ATOM('w', 'i', 'd', 'e')

#define ATOM_PREAMBLE_SIZE 8

/* a little something for dealing with RIFF headers */

#define FOURCC_TAG(ch0, ch1, ch2, ch3) QT_ATOM(ch0, ch1, ch2, ch3)

#define RIFF_TAG FOURCC_TAG('R', 'I', 'F', 'F')
#define WAVE_TAG FOURCC_TAG('W', 'A', 'V', 'E')
#define AVI_TAG FOURCC_TAG('A', 'V', 'I', ' ')

/* arbitrary number of initial file bytes to check for an MPEG marker */
#define RIFF_CHECK_KILOBYTES 1024

#define MPEG_MARKER FOURCC_TAG( 0x00, 0x00, 0x01, 0xBA )

/*
 * This function traverses a file and looks for a mdat atom. Upon exit:
 * *mdat_offset contains the file offset of the beginning of the mdat
 *  atom (that means the offset  * of the 4-byte length preceding the
 *  characters 'mdat')
 * *mdat_size contains the 4-byte size preceding the mdat characters in
 *  the atom. Note that this will be 1 in the case of a 64-bit atom.
 * Both mdat_offset and mdat_size are set to -1 if not mdat atom was
 * found.
 *
 * Note: Do not count on the input stream being positioned anywhere in
 * particular when this function is finished.
 */
static void find_mdat_atom(input_plugin_t *input, off_t *mdat_offset,
  int64_t *mdat_size) {

  off_t atom_size;
  unsigned int atom;
  unsigned char atom_preamble[ATOM_PREAMBLE_SIZE];

  /* init the passed variables */
  *mdat_offset = *mdat_size = -1;

  /* take it from the top */
  if (input->seek(input, 0, SEEK_SET) != 0)
    return;

  /* traverse through the input */
  while (*mdat_offset == -1) {
    if (input->read(input, atom_preamble, ATOM_PREAMBLE_SIZE) !=
      ATOM_PREAMBLE_SIZE)
      break;

    atom_size = BE_32(&atom_preamble[0]);
    atom = BE_32(&atom_preamble[4]);

    if (atom == MDAT_ATOM) {
      *mdat_offset = input->get_current_pos(input) - ATOM_PREAMBLE_SIZE;
      *mdat_size = atom_size;
      break;
    }

    /* make sure the atom checks out as some other top-level atom before
     * proceeding */
    if ((atom != FREE_ATOM) &&
        (atom != JUNK_ATOM) &&
        (atom != MOOV_ATOM) &&
        (atom != PNOT_ATOM) &&
        (atom != SKIP_ATOM) &&
        (atom != WIDE_ATOM))
      break;

    /* 64-bit length special case */
    if (atom_size == 1) {
      if (input->read(input, atom_preamble, ATOM_PREAMBLE_SIZE) !=
        ATOM_PREAMBLE_SIZE)
        break;

      atom_size = BE_32(&atom_preamble[0]);
      atom_size <<= 32;
      atom_size |= BE_32(&atom_preamble[4]);
      atom_size -= ATOM_PREAMBLE_SIZE * 2;
    } else
      atom_size -= ATOM_PREAMBLE_SIZE;

    
    input->seek(input, atom_size, SEEK_CUR);
  }
}

static uint32_t read_bytes (demux_mpeg_t *this, int n) {

  uint32_t res;
  uint32_t i;
  unsigned char buf[6];

  buf[4]=0;

  i = this->input->read (this->input, buf, n);

  if (i != n) {

    this->status = DEMUX_FINISHED;
  }

  switch (n)  {
  case 1:
    res = buf[0];
    break;
  case 2:
    res = (buf[0]<<8) | buf[1];
    break;
  case 3:
    res = (buf[0]<<16) | (buf[1]<<8) | buf[2];
    break;
  case 4:
    res = (buf[2]<<8) | buf[3] | (buf[1]<<16) | (buf[0] << 24);
    break;
  default:
    printf ("demux_mpeg: how how - something wrong in wonderland demux:read_bytes (%d)\n", n);
    abort();
  }

  return res;
}

/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( (x<0) ? (-x) : (x) )

static void check_newpts( demux_mpeg_t *this, int64_t pts, int video )
{
  int64_t diff;

  diff = pts - this->last_pts[video];

  if( !this->preview_mode && pts &&
      (this->send_newpts || (this->last_pts[video] && abs(diff)>WRAP_THRESHOLD) ) ) {

    if (this->buf_flag_seek) {
      xine_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      xine_demux_control_newpts(this->stream, pts, 0);
    }
    this->send_newpts = 0;
    this->last_pts[1-video] = 0;
  }

  if( !this->preview_mode && pts )
    this->last_pts[video] = pts;
}

static void parse_mpeg2_packet (demux_mpeg_t *this, int stream_id, int64_t scr) {

  int            len, i;
  uint32_t       w, flags, header_len;
  int64_t        pts;
  buf_element_t *buf = NULL;

  len = read_bytes(this, 2);

  if (stream_id==0xbd) {

    int track;

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    len -= header_len + 3;

    pts=0;

    if ((flags & 0x80) == 0x80) {

      w = read_bytes(this, 1);
      pts = (w & 0x0e) << 29 ;
      w = read_bytes(this, 2);
      pts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2);
      pts |= (w & 0xFFFE) >> 1;

      header_len -= 5 ;
    }

    /* read rest of header */
    i = this->input->read (this->input, this->dummy_space, header_len+4);

    track = this->dummy_space[0] & 0x0F ;

    /* contents */

    if(this->audio_fifo)
      buf = this->input->read_block (this->input, this->audio_fifo, len-4);
    else {
      this->input->read (this->input, this->dummy_space, len-4);
      return;
    }

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }

    buf->type      = BUF_AUDIO_A52 + track;
    buf->pts       = pts;
    check_newpts( this, pts, PTS_AUDIO );

    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;

    buf->input_pos = this->input->get_current_pos (this->input);

    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);

  } else if ((stream_id & 0xe0) == 0xc0) {
    int track = stream_id & 0x1f;

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    len -= header_len + 3;

    pts = 0;

    if ((flags & 0x80) == 0x80) {

      w = read_bytes(this, 1);
      pts = (w & 0x0e) << 29 ;
      w = read_bytes(this, 2);
      pts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2);
      pts |= (w & 0xFFFE) >> 1;

      header_len -= 5 ;
    }

    /* read rest of header */
    i = this->input->read (this->input, this->dummy_space, header_len);

    if(this->audio_fifo)
      buf = this->input->read_block (this->input, this->audio_fifo, len);
    else {
      this->input->read (this->input, this->dummy_space, len);
      return;
    }

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }

    buf->type      = BUF_AUDIO_MPEG + track;
    buf->pts       = pts;
    check_newpts( this, pts, PTS_AUDIO );

    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;
    
    buf->input_pos = this->input->get_current_pos(this->input);

    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);

  } else if ((stream_id >= 0xbc) && ((stream_id & 0xf0) == 0xe0)) {

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    len -= header_len + 3;

    pts = 0;

    if ((flags & 0x80) == 0x80) {

      w = read_bytes(this, 1);
      pts = (w & 0x0e) << 29 ;
      w = read_bytes(this, 2);
      pts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2);
      pts |= (w & 0xFFFE) >> 1;

      header_len -= 5 ;
    }

    /* read rest of header */
    i = this->input->read (this->input, this->dummy_space, header_len);

    /* contents */

    buf = this->input->read_block (this->input, this->video_fifo, len);

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type = BUF_VIDEO_MPEG;
    buf->pts  = pts;
    check_newpts( this, pts, PTS_VIDEO );

    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;
    
    buf->input_pos = this->input->get_current_pos(this->input);

    this->video_fifo->put (this->video_fifo, buf);

  } else {

    i = this->input->read (this->input, this->dummy_space, len);
    /* (*this->input->seek) (len,SEEK_CUR); */
  }

}

static void parse_mpeg1_packet (demux_mpeg_t *this, int stream_id, int64_t scr) {

  int             len;
  uint32_t        w;
  int             i;
  int64_t         pts;
  buf_element_t  *buf = NULL;

  len = read_bytes(this, 2);

  pts=0;

  if (stream_id != 0xbf) {

    w = read_bytes(this, 1); len--;

    while ((w & 0x80) == 0x80)   {

      if (this->status != DEMUX_OK)
        return;

      /* stuffing bytes */
      w = read_bytes(this, 1); len--;
    }

    if ((w & 0xC0) == 0x40) {

      if (this->status != DEMUX_OK)
        return;

      /* buffer_scale, buffer size */
      w = read_bytes(this, 1); len--;
      w = read_bytes(this, 1); len--;
    }

    if ((w & 0xF0) == 0x20) {

      if (this->status != DEMUX_OK)
        return;

      pts = (w & 0xe) << 29 ;
      w = read_bytes(this, 2); len -= 2;

      pts |= (w & 0xFFFE) << 14;

      w = read_bytes(this, 2); len -= 2;
      pts |= (w & 0xFFFE) >> 1;

      /* pts = 0; */

    } else if ((w & 0xF0) == 0x30) {

      if (this->status != DEMUX_OK)
        return;

      pts = (w & 0x0e) << 29 ;
      w = read_bytes(this, 2); len -= 2;

      pts |= (w & 0xFFFE) << 14;

      w = read_bytes(this, 2); len -= 2;

      pts |= (w & 0xFFFE) >> 1;

/*       printf ("pts2=%lld\n",pts); */

      /* Decoding Time Stamp */
      w = read_bytes(this, 3); len -= 3;
      w = read_bytes(this, 2); len -= 2;
    } else {

      /*
      if (w != 0x0f)
        xprintf (VERBOSE|DEMUX, " ERROR w (%02x) != 0x0F ",w);
      */
    }

  }

  if (pts && !this->has_pts) {
#ifdef LOG
    printf("demux_mpeg: this stream has pts\n");
#endif
    this->has_pts = 1;
  } else if (scr && !this->has_pts) {
#ifdef LOG
    printf("demux_mpeg: use scr\n");
#endif
    pts = scr;
  }
  
  if ((stream_id & 0xe0) == 0xc0) {
    int track = stream_id & 0x1f;

    if(this->audio_fifo) {
      buf = this->input->read_block (this->input, this->audio_fifo, len);
    } else {
      this->input->read (this->input, this->dummy_space, len);
      return;
    }

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type      = BUF_AUDIO_MPEG + track ;
    buf->pts       = pts;
    
    check_newpts( this, pts, PTS_AUDIO );

    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;
    
    buf->input_pos = this->input->get_current_pos(this->input);
    if (this->rate)
      buf->input_time = buf->input_pos / (this->rate * 50);

    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);

  } else if ((stream_id & 0xf0) == 0xe0) {

    buf = this->input->read_block (this->input, this->video_fifo, len);

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type = BUF_VIDEO_MPEG;
    buf->pts  = pts;
    
    check_newpts( this, pts, PTS_VIDEO );

    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;
    
    buf->input_pos = this->input->get_current_pos(this->input);
    if (this->rate)
      buf->input_time = buf->input_pos / (this->rate * 50);

    this->video_fifo->put (this->video_fifo, buf);

  } else if (stream_id == 0xbd) {

    i = this->input->read (this->input, this->dummy_space, len);
  } else {

    this->input->read (this->input, this->dummy_space, len);
  }

}

static uint32_t parse_pack(demux_mpeg_t *this) {

  uint32_t  buf ;
  int       mpeg_version;
  int64_t   scr;


  buf = read_bytes (this, 1);

  if ((buf>>4) == 4) {

    int stuffing, i;

    mpeg_version = 2;

    /* system_clock_reference */

    scr  = (buf & 0x08) << 27;
    scr  = (buf & 0x03) << 28;
    buf  = read_bytes (this, 1);
    scr |= buf << 20;
    buf  = read_bytes (this, 1);
    scr |= (buf & 0xF8) << 12 ;
    scr |= (buf & 0x03) << 13 ;
    buf  = read_bytes (this, 1);
    scr |= buf << 5;
    buf  = read_bytes (this, 1);
    scr |= (buf & 0xF8) >> 3;
    buf  = read_bytes (this, 1); /* extension */

    /* mux_rate */

    buf = read_bytes(this,3);
    if (!this->rate) {
      this->rate = (buf & 0xFFFFFC) >> 2;
    }

    /* stuffing bytes */
    buf = read_bytes(this,1);
    stuffing = buf &0x03;
    for (i=0; i<stuffing; i++)
      read_bytes (this, 1);

  } else {

     mpeg_version = 1;

     /* system_clock_reference */

     scr = (buf & 0x2) << 30;
     buf = read_bytes (this, 2);
     scr |= (buf & 0xFFFE) << 14;
     buf = read_bytes (this, 2);
     scr |= (buf & 0xFFFE) >>1;

     /* mux_rate */

     if (!this->rate) {
       buf = read_bytes (this,1);
       this->rate = (buf & 0x7F) << 15;
       buf = read_bytes (this,1);
       this->rate |= (buf << 7);
       buf = read_bytes (this,1);
       this->rate |= (buf >> 1);

       /* printf ("demux_mpeg: mux_rate = %d\n",this->rate);  */

     } else
       buf = read_bytes (this, 3) ;
  }

  /* discontinuity ? */
#if 0
  /* scr-wrap detection disabled due bad streams */
  if( scr && !this->preview_mode )
  {
    int64_t scr_diff = scr - this->last_scr;

    if (abs(scr_diff) > 60000 && !this->send_newpts) {

      buf_element_t *buf;

      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = BUF_CONTROL_DISCONTINUITY;
      buf->disc_off = scr_diff;
      this->video_fifo->put (this->video_fifo, buf);

      if (this->audio_fifo) {
        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type = BUF_CONTROL_DISCONTINUITY;
        buf->disc_off = scr_diff;
        this->audio_fifo->put (this->audio_fifo, buf);
      }
    }
    this->last_scr = scr;
  }
#endif

  /* system header */

  buf = read_bytes (this, 4) ;

  /* printf ("  code = %08x\n",buf);*/

  if (buf == 0x000001bb) {
    buf = read_bytes (this, 2);

    this->input->read (this->input, this->dummy_space, buf);

    buf = read_bytes (this, 4) ;
  }

  /* printf ("  code = %08x\n",buf); */

  while ( ((buf & 0xFFFFFF00) == 0x00000100)
          && ((buf & 0xff) != 0xba) ) {

    if (this->status != DEMUX_OK)
      return buf;

    if (mpeg_version == 1)
      parse_mpeg1_packet (this, buf & 0xFF, scr);
    else
      parse_mpeg2_packet (this, buf & 0xFF, scr);

    buf = read_bytes (this, 4);

  }

  return buf;

}

static uint32_t parse_pack_preview (demux_mpeg_t *this, int *num_buffers)
{
  uint32_t buf ;
  int mpeg_version;

  /* system_clock_reference */
  buf = read_bytes (this, 1);

  if ((buf>>4) == 4) {
     buf = read_bytes(this, 2);
     mpeg_version = 2;
  } else {
     mpeg_version = 1;
  }

  buf = read_bytes (this, 4);

  /* mux_rate */

  if (!this->rate) {
    buf = read_bytes (this,1);
    this->rate = (buf & 0x7F) << 15;
    buf = read_bytes (this,1);
    this->rate |= (buf << 7);
    buf = read_bytes (this,1);
    this->rate |= (buf >> 1);

    /* printf ("demux_mpeg: mux_rate = %d\n",this->rate); */

  } else
    buf = read_bytes (this, 3) ;

  /* system header */

  buf = read_bytes (this, 4) ;

  if (buf == 0x000001bb) {
    buf = read_bytes (this, 2);
    this->input->read (this->input, this->dummy_space, buf);
    buf = read_bytes (this, 4) ;
  }

  while ( ((buf & 0xFFFFFF00) == 0x00000100)
          && ((buf & 0xff) != 0xba)
          && (*num_buffers > 0)) {

    if (this->status != DEMUX_OK)
      return buf;

    if (mpeg_version == 1)
      parse_mpeg1_packet (this, buf & 0xFF, 0);
    else
      parse_mpeg2_packet (this, buf & 0xFF, 0);

    buf = read_bytes (this, 4);
    *num_buffers = *num_buffers - 1;
  }

  return buf;

}

static void demux_mpeg_resync (demux_mpeg_t *this, uint32_t buf) {

  while ((buf !=0x000001ba) && (this->status == DEMUX_OK)) {

    buf = (buf << 8) | read_bytes (this, 1);
  }
}

static void *demux_mpeg_loop (void *this_gen) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;
  uint32_t w=0;

  pthread_mutex_lock( &this->mutex );
  /* do-while needed to seek after demux finished */
  do {

    /* main demuxer loop */
    while(this->status == DEMUX_OK) {
      w = parse_pack (this);
      if (w != 0x000001ba)
        demux_mpeg_resync (this, w);

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      /* give demux_*_stop a chance to interrupt us */
      sched_yield();
      pthread_mutex_lock( &this->mutex );
    }

    /* wait before sending end buffers: user might want to do a new seek */
    while(this->send_end_buffers && this->video_fifo->size(this->video_fifo) &&
          this->status != DEMUX_OK){
      pthread_mutex_unlock( &this->mutex );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &this->mutex );
    }

  } while( this->status == DEMUX_OK );

  if (this->send_end_buffers) {
    xine_demux_control_end(this->stream, BUF_FLAG_END_STREAM);
  }

  printf ("demux_mpeg: demux thread finished (status: %d, buf:%x)\n",
          this->status, w);

  this->thread_running = 0;
  pthread_mutex_unlock( &this->mutex );

  pthread_exit(NULL);

  return NULL;
}

static void demux_mpeg_stop (demux_plugin_t *this_gen) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;
  void *p;

  pthread_mutex_lock( &this->mutex );

  if (!this->thread_running) {
    printf ("demux_mpeg: stop...ignored\n");
    pthread_mutex_unlock( &this->mutex );
    return;
  }

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;

  pthread_mutex_unlock( &this->mutex );
  pthread_join (this->thread, &p);

  xine_demux_flush_engine(this->stream);

  xine_demux_control_end(this->stream, BUF_FLAG_END_USER);
}

static int demux_mpeg_get_status (demux_plugin_t *this_gen) {
  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;

  return this->status;
}

static void demux_mpeg_send_headers (demux_plugin_t *this_gen) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;
  uint32_t w;
  int num_buffers = NUM_PREVIEW_BUFFERS;
    
  pthread_mutex_lock( &this->mutex );

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->rate          = 0; /* fixme */
  this->last_pts[0]   = 0;
  this->last_pts[1]   = 0;
  
  xine_demux_control_start(this->stream);

  /*
   * send preview buffers for stream/meta_info
   */
  
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;

  this->preview_mode = 1;
    
  this->input->seek (this->input, 4, SEEK_SET);
    
  this->status = DEMUX_OK ;
    
  do {

    w = parse_pack_preview (this, &num_buffers);
      
    if (w != 0x000001ba)
      demux_mpeg_resync (this, w);
      
    num_buffers --;
      
  } while ( (this->status == DEMUX_OK) && (num_buffers > 0));
    
  this->status = DEMUX_OK ;

  this->stream->stream_info[XINE_STREAM_INFO_BITRATE] = this->rate * 50 * 8;

  xine_demux_control_headers_done (this->stream);

  pthread_mutex_unlock (&this->mutex);
}

static int demux_mpeg_start (demux_plugin_t *this_gen,
			     off_t start_pos, int start_time) {

  demux_mpeg_t   *this = (demux_mpeg_t *) this_gen;
  int             err;
  int             status;

  pthread_mutex_lock( &this->mutex );

  if ((this->input->get_capabilities (this->input) & INPUT_CAP_SEEKABLE) != 0 ) {

    if ( (!start_pos) && (start_time))
      start_pos = start_time * this->rate * 50;

    this->input->seek (this->input, start_pos+4, SEEK_SET);

    if( start_pos )
      demux_mpeg_resync (this, read_bytes(this, 4) );

  } else
    read_bytes(this, 4);

  this->send_newpts = 1;
  this->status = DEMUX_OK ;

  if( !this->thread_running ) {
    this->preview_mode = 0;
    this->send_end_buffers = 1;
    this->thread_running = 1;
    this->buf_flag_seek = 0;

    if ((err = pthread_create (&this->thread,
                               NULL, demux_mpeg_loop, this)) != 0) {
      printf ("demux_mpeg: can't create new thread (%s)\n",
              strerror(err));
      abort();
    }
  }
  else {
    this->buf_flag_seek = 1;
    xine_demux_flush_engine(this->stream);
  }

  /* this->status is saved because we can be interrupted between
   * pthread_mutex_unlock and return
   */
  status = this->status;
  pthread_mutex_unlock( &this->mutex );
  return status;
}

static int demux_mpeg_seek (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time) {
  /* demux_mpeg_t *this = (demux_mpeg_t *) this_gen; */

  return demux_mpeg_start (this_gen, start_pos, start_time);
}

static void demux_mpeg_dispose (demux_plugin_t *this_gen) {

  demux_mpeg_stop (this_gen);

  free (this_gen);
}

static int demux_mpeg_get_stream_length (demux_plugin_t *this_gen) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;

  if (this->rate)
    return this->input->get_length (this->input) / (this->rate * 50);
  else
    return 0;

}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
				    input_plugin_t *input) {
  demux_mpeg_t       *this;

  this         = xine_xmalloc (sizeof (demux_mpeg_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_mpeg_send_headers;
  this->demux_plugin.start             = demux_mpeg_start;
  this->demux_plugin.seek              = demux_mpeg_seek;
  this->demux_plugin.stop              = demux_mpeg_stop;
  this->demux_plugin.dispose           = demux_mpeg_dispose;
  this->demux_plugin.get_status        = demux_mpeg_get_status;
  this->demux_plugin.get_stream_length = demux_mpeg_get_stream_length;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;
  this->has_pts = 0;

  pthread_mutex_init( &this->mutex, NULL );


  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    uint8_t buf[4096];
    off_t mdat_atom_offset = -1;
    int64_t mdat_atom_size = -1;
    unsigned int fourcc_tag;
    int i, j;
    int ok = 0;

    if (input->get_capabilities(input) & INPUT_CAP_BLOCK ) {
      free (this);
      return NULL;
    }

    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0) {
 
      /* try preview */

      if ((input->get_capabilities(input) & INPUT_CAP_PREVIEW) == 0) {
	free (this);
	return NULL;
      }

      input->get_optional_data (input, buf, INPUT_OPTIONAL_DATA_PREVIEW);

#ifdef LOG
      printf ("demux_mpeg: %02x %02x %02x %02x\n",
	      buf[0], buf[1], buf[2], buf[3]);
#endif      

      /*
       * look for mpeg header
       */
      
      if (!buf[0] && !buf[1] && (buf[2] == 0x01) 
	  && (buf[3] == 0xba)) /* if so, take it */
	break;

      free (this);
      return NULL;
    }

    input->seek(input, 0, SEEK_SET);
    if (input->read(input, buf, 16) == 16) {

      if(!buf[0] && !buf[1] && (buf[2] == 0x01))

	switch(buf[3]) {
	case 0xba:
          if((buf[4] & 0xf0) == 0x20) {
            uint32_t pckbuf ;

	    pckbuf = read_bytes (this, 1);
	    if ((pckbuf>>4) != 4) {
	      ok = 1;
	      break;
	    }
	  }
	  break;
#if 0
	case 0xe0:
	  if((buf[6] & 0xc0) != 0x80) {
	    uint32_t pckbuf ;

	    pckbuf = read_bytes (this, 1);
	    if ((pckbuf>>4) != 4) {
	      ok = 1;
	      break;
	    }
	  }
	  break;
#endif
        }
    }

    if (ok)
      break;

    /* special case for MPEG streams hidden inside QT files; check
     * is there is an mdat atom  */
    find_mdat_atom(input, &mdat_atom_offset, &mdat_atom_size);
    if (mdat_atom_offset != -1) {
      /* seek to the start of the mdat data, which might be in different
       * depending on the size type of the atom */
      if (mdat_atom_size == 1)
	input->seek(input, mdat_atom_offset + 16, SEEK_SET);
      else
	input->seek(input, mdat_atom_offset + 8, SEEK_SET);

      /* go through the same MPEG detection song and dance */
      if (input->read(input, buf, 6)) {
	if (!buf[0] && !buf[1] && buf[2] == 0x01) {
	  switch (buf[3]) {
	  case 0xba:
	    if ((buf[4] & 0xf0) == 0x20) {
	      uint32_t pckbuf ;

	      pckbuf = read_bytes (this, 1);
	      if ((pckbuf>>4) != 4) {
		ok = 1;
	      }
	    }
	    break;
	  }
	}
      }
      if (ok)
	  break;

      free (this);
      return NULL;
    }

    /* special case for MPEG streams with a RIFF header */
    fourcc_tag = BE_32(&buf[0]);
    if (fourcc_tag == RIFF_TAG) {
      fourcc_tag = BE_32(&buf[8]);
      /* disregard the RIFF file if it is certainly a better known
       * format like AVI or WAVE */
      if ((fourcc_tag == WAVE_TAG) ||
	  (fourcc_tag == AVI_TAG))
	return DEMUX_CANNOT_HANDLE;
	
      /* Iterate through first n kilobytes of RIFF file searching for
       * MPEG video marker. No, it's not a very efficient approach, but
       * if execution has reached this special case, this is currently
       * the best chance for detecting the file automatically. Also,
       * be extra lazy and do not bother skipping over the data 
       * header. */
      for (i = 0; i < RIFF_CHECK_KILOBYTES && !ok; i++) {
	if (input->read(input, buf, 1024) != 1024)
	  break;
	for (j = 0; j < 1024 - 4; j++) {
	  if (BE_32(&buf[j]) == MPEG_MARKER) {
	    ok = 1;
	    break;
	  }
	}
      }
      if (ok)
	break;
    }
    free (this);
    return NULL;
  }

  case METHOD_BY_EXTENSION: {
    char *ending, *mrl;

    mrl = input->get_mrl (input);

    ending = strrchr(mrl, '.');
    
    if (!ending) {
      free (this);
      return NULL;
    }

    if (strncasecmp(ending, ".MPEG", 5)
	&& strncasecmp (ending, ".mpg", 4)) {
      free (this);
      return NULL;
    }
  }
    break;

  default:
    free (this);
    return NULL;
  }

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "MPEG program stream demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "MPEG";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "mpg mpeg";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return "video/mpeg: mpeg, mpg, mpe: MPEG animation;"
         "video/x-mpeg: mpeg, mpg, mpe: MPEG animation;";
}

static void class_dispose (demux_class_t *this_gen) {

  demux_mpeg_class_t *this = (demux_mpeg_class_t *) this_gen;

  free (this);
 }

static void *init_plugin (xine_t *xine, void *data) {

  demux_mpeg_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_mpeg_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 14, "mpeg", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
