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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: demux_mpeg.c,v 1.30 2001/08/18 23:30:51 guenter Exp $
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
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "monitor.h"
#include "xine_internal.h"
#include "demux.h"
#include "utils.h"

#define NUM_PREVIEW_BUFFERS 250

static uint32_t xine_debug;

typedef struct demux_mpeg_s {

  demux_plugin_t       demux_plugin;

  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;

  input_plugin_t      *input;

  pthread_t            thread;

  unsigned char        dummy_space[100000];

  int                  status;
  int                  preview_mode;

  int                  send_end_buffers;

} demux_mpeg_t ;

static uint32_t read_bytes (demux_mpeg_t *this, int n) {
  
  uint32_t res;
  uint32_t i;
  unsigned char buf[6];

  buf[4]=0;


  i = this->input->read (this->input, buf, n);

  if (i != n) {
    
    this->status = DEMUX_FINISHED;

    xprintf (VERBOSE|DEMUX, "Unexpected end of stream\n");
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
    fprintf (stderr,
	     "How how - something wrong in wonderland demux:read_bytes (%d)\n",
	     n);
    exit (1);
  }

  return res;
}

static void parse_mpeg2_packet (demux_mpeg_t *this, int nID) {

  int            nLen, i;
  uint32_t       w, flags, header_len, pts;
  buf_element_t *buf = NULL;

  nLen = read_bytes(this, 2);

  xprintf (VERBOSE|DEMUX|MPEG, "  mpeg2 packet (len=%d",nLen); 

  if (nID==0xbd) {

    int track;

    xprintf (VERBOSE|DEMUX|AC3, ",ac3");

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    nLen -= header_len + 3;

    pts=0;

    if ((flags & 0x80) == 0x80) {

      w = read_bytes(this, 1);
      pts = (w & 0x0e) << 29 ;
      w = read_bytes(this, 2); 
      pts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2); 
      pts |= (w & 0xFFFE) >> 1;

      xprintf (VERBOSE|DEMUX|VPTS, ", pts=%d",pts);

      header_len -= 5 ;
    }

    /* read rest of header */
    i = this->input->read (this->input, this->dummy_space, header_len+4);

    track = this->dummy_space[0] & 0x0F ;

    xprintf (VERBOSE|DEMUX, ", track=%02x", track);

    /* contents */

    if(this->audio_fifo)
      buf = this->input->read_block (this->input, this->audio_fifo, nLen-4);
    else {
      this->input->read (this->input, this->dummy_space, nLen);
      return;
    }
    
    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type      = BUF_AUDIO_AC3 + track;
    buf->PTS       = pts;
    buf->DTS       = 0 ; /* FIXME */
    if (this->preview_mode)
      buf->decoder_info[0] = 0;
    else
      buf->decoder_info[0] = 1;

    buf->input_pos = this->input->get_current_pos (this->input);
    
    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);

  } else if ((nID & 0xe0) == 0xc0) {
    int track = nID & 0x1f;

    xprintf (VERBOSE|DEMUX|AUDIO, ", audio #%d", track);

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    nLen -= header_len + 3;

    pts = 0;

    if ((flags & 0x80) == 0x80) {

      w = read_bytes(this, 1);
      pts = (w & 0x0e) << 29 ;
      w = read_bytes(this, 2); 
      pts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2); 
      pts |= (w & 0xFFFE) >> 1;

      xprintf (VERBOSE|DEMUX|VPTS, ", pts=%d",pts);

      header_len -= 5 ;
    }

    /* read rest of header */
    i = this->input->read (this->input, this->dummy_space, header_len);

    if(this->audio_fifo)
      buf = this->input->read_block (this->input, this->audio_fifo, nLen);

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type      = BUF_AUDIO_MPEG + track;
    buf->PTS       = pts;
    buf->DTS       = 0;   /* FIXME */
    if (this->preview_mode)
      buf->decoder_info[0] = 0;
    else
      buf->decoder_info[0] = 1;
    buf->input_pos = this->input->get_current_pos(this->input);

    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);

  } else if ((nID >= 0xbc) && ((nID & 0xf0) == 0xe0)) {

    xprintf (VERBOSE|DEMUX|VIDEO, ",video");

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    nLen -= header_len + 3;

    pts = 0;

    if ((flags & 0x80) == 0x80) {

      w = read_bytes(this, 1);
      pts = (w & 0x0e) << 29 ;
      w = read_bytes(this, 2); 
      pts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2); 
      pts |= (w & 0xFFFE) >> 1;

      xprintf (VERBOSE|DEMUX|VPTS, ", pts=%d",pts);

      header_len -= 5 ;
    }

    /* read rest of header */
    i = this->input->read (this->input, this->dummy_space, header_len);

    /* contents */

    if(this->audio_fifo)
      buf = this->input->read_block (this->input, this->audio_fifo, nLen);

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type = BUF_VIDEO_MPEG;
    buf->PTS  = pts;
    buf->DTS  = 0;
    if (this->preview_mode)
      buf->decoder_info[0] = 0;
    else
      buf->decoder_info[0] = 1;
    buf->input_pos = this->input->get_current_pos(this->input);

    this->video_fifo->put (this->video_fifo, buf);

  } else {
    xprintf (VERBOSE|DEMUX, ",unknown stream - skipped");

    i = this->input->read (this->input, this->dummy_space, nLen); 
    /* (*this->input->seek) (nLen,SEEK_CUR); */
  }

  xprintf (VERBOSE|DEMUX, ")\n");

}

static void parse_mpeg1_packet (demux_mpeg_t *this, int nID) 
{
  int             nLen;
  uint32_t        w;
  int             i;
  int             pts;
  buf_element_t  *buf = NULL;

  xprintf (VERBOSE|DEMUX, "  packet (");

  nLen = read_bytes(this, 2);

  xprintf (VERBOSE|DEMUX, "len=%d",nLen);

  pts=0;

  if (nID != 0xbf) {

    w = read_bytes(this, 1); nLen--;

    while ((w & 0x80) == 0x80)   {

      if (this->status != DEMUX_OK)
	return;

      /* stuffing bytes */
      w = read_bytes(this, 1); nLen--;
    }

    if ((w & 0xC0) == 0x40) {

      if (this->status != DEMUX_OK)
	return;

      /* buffer_scale, buffer size */
      w = read_bytes(this, 1); nLen--;
      w = read_bytes(this, 1); nLen--;
    }

    if ((w & 0xF0) == 0x20) {

      if (this->status != DEMUX_OK)
	return;

      pts = (w & 0xe) << 29 ;
      w = read_bytes(this, 2); nLen -= 2;

      pts |= (w & 0xFFFE) << 14;

      w = read_bytes(this, 2); nLen -= 2;
      pts |= (w & 0xFFFE) >> 1;

      xprintf (VERBOSE|DEMUX|VPTS, ", pts=%d",pts);

      /* pts = 0; */

    } else if ((w & 0xF0) == 0x30) {

      if (this->status != DEMUX_OK)
	return;

      pts = (w & 0x0e) << 29 ;
      w = read_bytes(this, 2); nLen -= 2;
      
      pts |= (w & 0xFFFE) << 14;

      w = read_bytes(this, 2); nLen -= 2;

      pts |= (w & 0xFFFE) >> 1;

/*       printf ("pts2=%d\n",pts); */
      xprintf (VERBOSE|DEMUX|VPTS, ", pts2=%d",pts);

      /* Decoding Time Stamp */
      w = read_bytes(this, 3); nLen -= 3;
      w = read_bytes(this, 2); nLen -= 2;
    } else {
      xprintf (VERBOSE|DEMUX, ", w = %02x",w);
      if (w != 0x0f)
	xprintf (VERBOSE|DEMUX, " ERROR w (%02x) != 0x0F ",w);
    }

  }

  if ((nID & 0xe0) == 0xc0) {
    int track = nID & 0x1f;

    xprintf (VERBOSE|DEMUX|AUDIO, ", audio #%d", track);

    if(this->audio_fifo) {
      buf = this->input->read_block (this->input, this->audio_fifo, nLen);
    } else {
      this->input->read (this->input, this->dummy_space, nLen);
      return;
    }

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type      = BUF_AUDIO_MPEG + track ;
    buf->PTS       = pts;
    buf->DTS       = 0;   /* FIXME */
    if (this->preview_mode)
      buf->decoder_info[0] = 0;
    else
      buf->decoder_info[0] = 1;
    buf->input_pos = this->input->get_current_pos(this->input);

    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);

  } else if ((nID & 0xf0) == 0xe0) {

    xprintf (VERBOSE|DEMUX|VIDEO, ", video #%d", nID & 0x0f);

    if(this->input->read_block)
      buf = this->input->read_block (this->input, this->video_fifo, nLen);

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type = BUF_VIDEO_MPEG;
    buf->PTS  = pts;
    buf->DTS  = 0; /* FIXME */
    if (this->preview_mode)
      buf->decoder_info[0] = 0;
    else
      buf->decoder_info[0] = 1;
    buf->input_pos = this->input->get_current_pos(this->input);

    this->video_fifo->put (this->video_fifo, buf);

  } else if (nID == 0xbd) {
    xprintf (VERBOSE|DEMUX|AC3, ", ac3");
    i = this->input->read (this->input, this->dummy_space, nLen);
  } else {
    xprintf (VERBOSE|DEMUX, ", unknown (nID = %d)",nID);
    this->input->read (this->input, this->dummy_space, nLen);
  }

  xprintf (VERBOSE|DEMUX, ")\n");
}

static uint32_t parse_pack(demux_mpeg_t *this)
{
  uint32_t buf ;
  char scratch[1024];
  int mpeg_version;

  xprintf (VERBOSE|DEMUX, "pack {\n");

  /* system_clock_reference */
  buf = read_bytes (this, 1);
  xprintf (VERBOSE|DEMUX|VIDEO, "  mpeg version : %02x",buf>>4);

  if ((buf>>4) == 4) {
     xprintf (VERBOSE|DEMUX|VIDEO, "  => mpeg II \n");
     buf = read_bytes(this, 2);
     mpeg_version = 2;
  } else {
     xprintf (VERBOSE|DEMUX|VIDEO, "  => mpeg I \n");
     mpeg_version = 1;
  }

  buf = read_bytes (this, 2);
  buf = read_bytes (this, 2);

  /* mux_rate */

  buf = read_bytes (this, 3) ;

  /* printf ("  mux_rate = %06x\n",buf); */

  /* system header */

  buf = read_bytes (this, 4) ;

  /* printf ("  code = %08x\n",buf);*/

  if (buf == 0x000001bb) {
    buf = read_bytes (this, 2);
    xprintf (VERBOSE|DEMUX, "  system_header (%d +6 bytes)\n",buf);

    this->input->read (this->input, scratch, buf);

    buf = read_bytes (this, 4) ;
  }
  
  /* printf ("  code = %08x\n",buf); */

  while ( ((buf & 0xFFFFFF00) == 0x00000100)
	  && ((buf & 0xff) != 0xba) ) {

    if (this->status != DEMUX_OK)
      return buf;

    if (mpeg_version == 1)
      parse_mpeg1_packet (this, buf & 0xFF);
    else
      parse_mpeg2_packet (this, buf & 0xFF);

    buf = read_bytes (this, 4);
    xprintf (VERBOSE|DEMUX, "  code = %08x\n",buf); 
  }
  
  xprintf (VERBOSE|DEMUX, "}\n");

  return buf;

}

static void demux_mpeg_resync (demux_mpeg_t *this, uint32_t buf) {

  while ((buf !=0x000001ba) && (this->status == DEMUX_OK)) {
    xprintf (VERBOSE|DEMUX, "resync : %08x\n",buf);
    buf = (buf << 8) | read_bytes (this, 1);
  }
}

static void *demux_mpeg_loop (void *this_gen) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;
  buf_element_t *buf;
  uint32_t w;

  do {
    w = parse_pack (this);

    if (w != 0x000001ba)
      demux_mpeg_resync (this, w);
    
  } while (this->status == DEMUX_OK) ;
  
  if (this->send_end_buffers) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_info[0] = 0; /* stream finished */
    this->video_fifo->put (this->video_fifo, buf);

    if(this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type            = BUF_CONTROL_END;
      buf->decoder_info[0] = 0; /* stream finished */
      this->audio_fifo->put (this->audio_fifo, buf);
    }
  }

  xprintf (VERBOSE|DEMUX, "demux loop finished (status: %d, buf:%x)\n",
	   this->status, w);
  printf ("demux loop finished (status: %d, buf:%x)\n",
	  this->status, w);

  pthread_exit(NULL);

  return NULL;
}

static void demux_mpeg_stop (demux_plugin_t *this_gen) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;
  buf_element_t *buf;

  printf ("demux_mpeg: stop...\n");

  if (this->status != DEMUX_OK) {

    this->video_fifo->clear(this->video_fifo);
    if(this->audio_fifo) 
      this->audio_fifo->clear(this->audio_fifo);

    return;
  }

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;

  pthread_cancel (this->thread);
  /*pthread_join (this->thread, &p);*/

  this->video_fifo->clear(this->video_fifo);
  if(this->audio_fifo) 
    this->audio_fifo->clear(this->audio_fifo);

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type            = BUF_CONTROL_END;
  buf->decoder_info[0] = 1; /* forced */
  this->video_fifo->put (this->video_fifo, buf);

  if(this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_info[0] = 1; /* forced */
    this->audio_fifo->put (this->audio_fifo, buf);
  }

}

static int demux_mpeg_get_status (demux_plugin_t *this_gen) {
  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;
  return this->status;
}

static void demux_mpeg_start (demux_plugin_t *this_gen,
			      fifo_buffer_t *video_fifo,
			      fifo_buffer_t *audio_fifo,
			      off_t pos,
			      gui_get_next_mrl_cb_t next_mrl_cb,
			      gui_branched_cb_t branched_cb) 
{
  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo    = video_fifo;
  this->audio_fifo    = audio_fifo;

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type    = BUF_CONTROL_START;
  this->video_fifo->put (this->video_fifo, buf);

  if(this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type    = BUF_CONTROL_START;
    this->audio_fifo->put (this->audio_fifo, buf);
  }

  if ((this->input->get_capabilities (this->input) & INPUT_CAP_SEEKABLE) != 0 ) {

    uint32_t w;
    int num_buffers = NUM_PREVIEW_BUFFERS;

    this->preview_mode = 1;

    this->input->seek (this->input, 4, SEEK_SET);

    this->status = DEMUX_OK ;

    do {
      w = parse_pack (this);
      
      if (w != 0x000001ba)
	demux_mpeg_resync (this, w);
      
      num_buffers --;

    } while ( (this->status == DEMUX_OK) && (num_buffers>0)) ;

    xprintf (VERBOSE|DEMUX, "=>seek to %Ld\n",pos);
    this->input->seek (this->input, pos+4, SEEK_SET);
  } else
    read_bytes(this, 4);

  this->preview_mode = 0;
  this->send_end_buffers = 1;
  this->status = DEMUX_OK ;

  pthread_create (&this->thread, NULL, demux_mpeg_loop, this) ;
}

static int demux_mpeg_open(demux_plugin_t *this_gen, 
			   input_plugin_t *input, int stage) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;

  this->input = input;

  switch(stage) {
    
  case STAGE_BY_CONTENT: {
    uint8_t buf[4096];
    
    if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
      input->seek(input, 0, SEEK_SET);
      
      if(input->get_blocksize(input))
	return DEMUX_CANNOT_HANDLE;
      
      if(input->read(input, buf, 6)) {
	
	if(buf[0] || buf[1] || (buf[2] != 0x01))
	  return DEMUX_CANNOT_HANDLE;
	
	switch(buf[3]) {
	  
	case 0xba:
	  if((buf[4] & 0xf0) == 0x20) {
	    uint32_t pckbuf ;
	    
	    pckbuf = read_bytes (this, 1);
	    if ((pckbuf>>4) != 4) {
	      this->input = input;
	      return DEMUX_CAN_HANDLE;
	    }
	  }
	  break;
#if 0	  
	case 0xe0:
	  if((buf[6] & 0xc0) != 0x80) {
	    uint32_t pckbuf ;
	    
	    pckbuf = read_bytes (this, 1);
	    if ((pckbuf>>4) != 4) {
	      this->input = input;
	      return DEMUX_CAN_HANDLE;
	    }
	  }
	  break;
#endif	  
	}
      }
    }
    return DEMUX_CANNOT_HANDLE;
  }
  break;

  case STAGE_BY_EXTENSION: {
    char *media;
    char *ending;
    char *MRL = input->get_mrl(input);
    
    media = strstr(MRL, "://");
    if(media) {
      if((!(strncasecmp(MRL, "stdin", 5))) 
	 || (!(strncasecmp(MRL, "fifo", 4)))) {
	if(!(strncasecmp(media+3, "mpeg1", 5))) {
	  this->input = input;
	  return DEMUX_CAN_HANDLE;
	}
	else if(!(strncasecmp((media+3), "mpeg2", 5))) {
	  return DEMUX_CANNOT_HANDLE;
	}
	fprintf(stderr, "You should specify mpeg(mpeg1/mpeg2) stream type.\n");
	return DEMUX_CANNOT_HANDLE;
      }
      else if(strncasecmp(MRL, "file", 4)) {
	return DEMUX_CANNOT_HANDLE;
      }
    }

    ending = strrchr(MRL, '.');
    xprintf(VERBOSE|DEMUX, "demux_mpeg_can_handle: ending %s of %s\n", 
	    ending, MRL);
    
    if(!ending)
      return DEMUX_CANNOT_HANDLE;
    
    if(!strcasecmp(ending, ".mpg") 
       || (!strcasecmp(ending, ".mpeg"))) {
      this->input = input;
      return DEMUX_CAN_HANDLE;
    }
  }
  break;

  default:
    return DEMUX_CANNOT_HANDLE;
    break;
  }

  return DEMUX_CANNOT_HANDLE;
}

static char *demux_mpeg_get_id(void) {
  return "MPEG";
}

static void demux_mpeg_close (demux_plugin_t *this) {
  /* nothing */
}

demux_plugin_t *init_demuxer_plugin(int iface, config_values_t *config) {

  demux_mpeg_t *this;

  if (iface != 2) {
    printf( "demux_mpeg: plugin doesn't support plugin API version %d.\n"
	    "demux_mpeg: this means there's a version mismatch between xine and this "
	    "demux_mpeg: demuxer plugin.\nInstalling current input plugins should help.\n",
	    iface);
    return NULL;
  }

  this = xmalloc (sizeof (demux_mpeg_t));
  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_mpeg_open;
  this->demux_plugin.start             = demux_mpeg_start;
  this->demux_plugin.stop              = demux_mpeg_stop;
  this->demux_plugin.close             = demux_mpeg_close;
  this->demux_plugin.get_status        = demux_mpeg_get_status;
  this->demux_plugin.get_identifier    = demux_mpeg_get_id;
  
  return (demux_plugin_t *) this;
}
