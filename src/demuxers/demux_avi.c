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
 * $Id: demux_avi.c,v 1.98 2002/06/25 03:37:53 tmmm Exp $
 *
 * demultiplexer for avi streams
 *
 * part of the code is taken from
 * avilib (C) 1999 Rainer Johanni <Rainer@Johanni.de>
 *
 */

/*
 * Ian Goldberg <ian@cypherpunks.ca> modified this code so that it can
 * handle "streaming" AVI files.  By that I mean real seekable files, but
 * ones that are growing as we're displaying them.  Examples include
 * AVI's you're downloading, or ones you're writing in real time using
 * xawtv streamer, or whatever.  This latter is really useful, for
 * example,  for doing the PVR trick of starting the streamer to record
 * TV, starting to watch it ~10 minutes later, and skipping the
 * commercials to catch up to real time.  If you accidentally hit the
 * end of the stream, just hit your "back 15 seconds" key, and all is
 * good.
 *
 * Theory of operation: the video and audio indices have been separated
 * out of the main avi_t and avi_audio_t structures into separate
 * structures that can grow during playback.  We use the idx_grow_t
 * structure to keep track of the offset into the AVI file where we
 * expect to find the next A/V frame.  We periodically check if we can
 * read data from the file at that offset.  If we can, we append index
 * data for as many frames as we can read at the time.
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
#include <stdlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"

#define	WINE_TYPEDEFS_ONLY
#include "libw32dll/wine/avifmt.h"
#include "libw32dll/wine/windef.h"
#include "libw32dll/wine/vfw.h"

#define MAX_AUDIO_STREAMS 8

#define VALID_ENDS "avi"

/* The following variable indicates the kind of error */

typedef struct
{
  off_t pos;
  long len;
  long flags;
} video_index_entry_t;

typedef struct
{
  off_t pos;
  long len;
  off_t tot;
} audio_index_entry_t;

/* These next three are the video and audio structures that can grow
 * during the playback of a streaming file. */

typedef struct
{
  long   video_frames;      /* Number of video frames */
  long   alloc_frames;      /* Allocated number of frames */
  video_index_entry_t   *vindex;
} video_index_t;

typedef struct
{
  long   audio_chunks;      /* Chunks of audio data in the file */
  long   alloc_chunks;      /* Allocated number of chunks */
  audio_index_entry_t   *aindex;
} audio_index_t;

typedef struct
{
  off_t  nexttagoffset;     /* The offset into the AVI file where we expect */
                            /* to find the next A/V frame */
} idx_grow_t;


typedef struct
{
  long   dwScale_audio, dwRate_audio;
  long   dwSampleSize;

  uint32_t audio_type;      /* BUF_AUDIO_xxx type */

  long   audio_strn;        /* Audio stream number */
  char   audio_tag[4];      /* Tag of audio data */
  long   audio_posc;        /* Audio position: chunk */
  long   audio_posb;        /* Audio position: byte within chunk */

  xine_waveformatex *wavex;
  int    wavex_len;

  audio_index_t  audio_idx;

} avi_audio_t;

typedef struct
{
  long   width;             /* Width  of a video frame */
  long   height;            /* Height of a video frame */
  long   dwScale, dwRate;
  double fps;               /* Frames per second */

  char   compressor[8];     /* Type of compressor, 4 bytes + padding for 0 byte */
  long   video_strn;        /* Video stream number */
  char   video_tag[4];      /* Tag of video data */
  long   video_posf;        /* Number of next frame to be read
			       (if index present) */
  long   video_posb;        /* Video position: byte within frame */


  avi_audio_t		*audio[MAX_AUDIO_STREAMS];
  int			n_audio;
  off_t   audio_tot;         /* Total number of audio bytes */

  uint32_t video_type;      /* BUF_VIDEO_xxx type */

  long                   n_idx;    /* number of index entries actually filled */
  long                   max_idx;  /* number of index entries actually allocated */
  unsigned char        (*idx)[16]; /* index entries (AVI idx1 tag) */
  video_index_t          video_idx;
  xine_bmiheader         bih;
  off_t                  movi_start;

  int                    palette_count;
  palette_entry_t        palette[256];
} avi_t;

typedef struct demux_avi_s {
  demux_plugin_t       demux_plugin;

  xine_t              *xine;

  config_values_t     *config;

  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;

  input_plugin_t      *input;

  avi_t               *avi;

  pthread_t            thread;
  int                  thread_running;
  pthread_mutex_t      mutex;

  int                  status;

  int                  no_audio;
  int                  have_spu;

  uint32_t             video_step;
  uint32_t             AVI_errno;

  int                  send_end_buffers;

  char                 last_mrl[1024];

  idx_grow_t           idx_grow;
} demux_avi_t ;

#define AVI_ERR_SIZELIM      1     /* The write of the data would exceed
                                      the maximum size of the AVI file.
                                      This is more a warning than an error
                                      since the file may be closed safely */

#define AVI_ERR_OPEN         2     /* Error opening the AVI file - wrong path
                                      name or file nor readable/writable */

#define AVI_ERR_READ         3     /* Error reading from AVI File */

#define AVI_ERR_WRITE        4     /* Error writing to AVI File,
                                      disk full ??? */

#define AVI_ERR_WRITE_INDEX  5     /* Could not write index to AVI file
                                      during close, file may still be
                                      usable */

#define AVI_ERR_CLOSE        6     /* Could not write header to AVI file
                                      or not truncate the file during close,
                                      file is most probably corrupted */

#define AVI_ERR_NOT_PERM     7     /* Operation not permitted:
                                      trying to read from a file open
                                      for writing or vice versa */

#define AVI_ERR_NO_MEM       8     /* malloc failed */

#define AVI_ERR_NO_AVI       9     /* Not an AVI file */

#define AVI_ERR_NO_HDRL     10     /* AVI file has no header list,
                                      corrupted ??? */

#define AVI_ERR_NO_MOVI     11     /* AVI file has no MOVI list,
                                      corrupted ??? */

#define AVI_ERR_NO_VIDS     12     /* AVI file contains no video data */

#define AVI_ERR_NO_IDX      13     /* The file has been opened with
                                      getIndex==0, but an operation has been
                                      performed that needs an index */

/* Append an index entry for a newly-found video frame */
static int video_index_append(avi_t *AVI, off_t pos, long len, long flags)
{
  video_index_t *vit = &(AVI->video_idx);

  /* Make sure there's room */
  if (vit->video_frames == vit->alloc_frames) {
    long newalloc = vit->alloc_frames + 4096;
    video_index_entry_t *newindex =
      realloc(vit->vindex, newalloc*sizeof(video_index_entry_t));
    if (!newindex) return -1;
    vit->vindex = newindex;
    vit->alloc_frames = newalloc;
  }

  /* Set the new index entry */
  vit->vindex[vit->video_frames].pos = pos;
  vit->vindex[vit->video_frames].len = len;
  vit->vindex[vit->video_frames].flags = flags;
  vit->video_frames += 1;

  return 0;
}

/* Append an index entry for a newly-found audio frame */
static int audio_index_append(avi_t *AVI, int stream, off_t pos, long len,
    off_t tot)
{
  audio_index_t *ait = &(AVI->audio[stream]->audio_idx);

  /* Make sure there's room */
  if (ait->audio_chunks == ait->alloc_chunks) {
    long newalloc = ait->alloc_chunks + 4096;
    audio_index_entry_t *newindex =
      realloc(ait->aindex, newalloc*sizeof(audio_index_entry_t));
    if (!newindex) return -1;
    ait->aindex = newindex;
    ait->alloc_chunks = newalloc;
  }

  /* Set the new index entry */
  ait->aindex[ait->audio_chunks].pos = pos;
  ait->aindex[ait->audio_chunks].len = len;
  ait->aindex[ait->audio_chunks].tot = tot;
  ait->audio_chunks += 1;

  return 0;
}

static unsigned long str2ulong(unsigned char *str)
{
  return ( str[0] | (str[1]<<8) | (str[2]<<16) | (str[3]<<24) );
}

static unsigned long str2ushort(unsigned char *str)
{
  return ( str[0] | (str[1]<<8) );
}

static void long2str(unsigned char *dst, int n)
{
  dst[0] = (n    )&0xff;
  dst[1] = (n>> 8)&0xff;
  dst[2] = (n>>16)&0xff;
  dst[3] = (n>>24)&0xff;
}

#define PAD_EVEN(x) ( ((x)+1) & ~1 )

static int64_t get_audio_pts (demux_avi_t *this, int track, long posc,
	off_t postot, long posb) {

  if (this->avi->audio[track]->dwSampleSize==0)
    return (int64_t) posc * (double) this->avi->audio[track]->dwScale_audio /
      this->avi->audio[track]->dwRate_audio * 90000.0;
  else
    return (postot+posb)/
      this->avi->audio[track]->dwSampleSize * (double) this->avi->audio[track]->dwScale_audio /
      this->avi->audio[track]->dwRate_audio * 90000.0;
}

static int64_t get_video_pts (demux_avi_t *this, long pos) {
  return (int64_t) pos * (double) this->avi->dwScale /
      this->avi->dwRate * 90000.0;
}

/* Some handy stopper tests for idx_grow, below. */

/* Use this one to ensure the current video frame is in the index. */
static long video_pos_stopper(demux_avi_t *this, void *data)
{
  if (this->avi->video_posf >= this->avi->video_idx.video_frames) {
    return -1;
  }
  return 1;
}

/* Use this one to ensure the current audio chunk is in the index. */
static long audio_pos_stopper(demux_avi_t *this, void *data)
{
  avi_audio_t *AVI_A = (avi_audio_t *)data;

  if (AVI_A->audio_posc >= AVI_A->audio_idx.audio_chunks) {
    return -1;
  }
  return 1;
}

/* Use this one to ensure that a video frame with the given position
 * is in the index. */
static long start_pos_stopper(demux_avi_t *this, void *data)
{
  off_t start_pos = *(off_t *)data;
  long maxframe = this->avi->video_idx.video_frames - 1;

  while( maxframe >= 0 && this->avi->video_idx.vindex[maxframe].pos >= start_pos ) {
    if ( this->avi->video_idx.vindex[maxframe].flags & AVIIF_KEYFRAME )
      return 1;
    maxframe--;
  }
  return -1;
}

/* Use this one to ensure that a video frame with the given timestamp
 * is in the index. */
static long start_time_stopper(demux_avi_t *this, void *data)
{
  int64_t video_pts = *(int64_t *)data;

  if (this->avi->video_idx.video_frames > 0) {
    long maxframe = this->avi->video_idx.video_frames - 1;
    if (get_video_pts (this, maxframe) >= video_pts) {
      return 1;
    }
  }
  return -1;
}

static void demux_avi_stop (demux_plugin_t *this_gen);

/* This is called periodically to check if there's more file now than
 * there was before.  If there is, we constuct the index for (just) the
 * new part, and append it to the index we've got so far.  We stop
 * slurping in the new part when stopper(this, stopdata) returns a
 * non-negative value, or there's no more file to read.  If we're taking
 * a long time slurping in the new part, use the on-screen display to
 * notify the user.  Returns -1 if EOF was reached, the non-negative
 * return value of stopper otherwise. */
static long idx_grow(demux_avi_t *this, long (*stopper)(demux_avi_t *, void *),
	void *stopdata)
{
  unsigned long n;
  long i;
  long retval = -1;
  long num_read = 0;
  off_t ioff = 8;
  char data[256];
  int did_osd = 0;
  off_t savepos = this->input->seek(this->input, 0, SEEK_CUR);
  this->input->seek(this->input, this->idx_grow.nexttagoffset, SEEK_SET);

  while ((retval = stopper(this, stopdata)) < 0) {

    num_read += 1;

    if (num_read % 1000 == 0) {
      /* Update the user using the OSD */
      off_t file_len = this->input->get_length (this->input);
      char str[60];

      sprintf(str, "Building index (%3lld%%)",
	      100 * this->idx_grow.nexttagoffset / file_len);

      this->xine->osd_renderer->filled_rect (this->xine->osd,
	  0, 0, 299, 99, 0);
      this->xine->osd_renderer->render_text (this->xine->osd,
	  5, 30, str, OSD_TEXT1);
      this->xine->osd_renderer->show (this->xine->osd, 0);
      did_osd = 1;
    }

    if( this->input->read(this->input, data,8) != 8 )
      break;
    n = str2ulong(data+4);

    /* Dive into RIFF and LIST entries */
    if(strncasecmp(data, "LIST", 4) == 0 ||
	strncasecmp(data, "RIFF", 4) == 0) {
      this->idx_grow.nexttagoffset =
	this->input->seek(this->input, 4,SEEK_CUR);
      continue;
    }

    /* Check if we got a tag ##db, ##dc or ##wb */

    if (strncasecmp(data, this->avi->video_tag, 3) == 0 &&
	  (data[3]=='b' || data[3]=='B' || data[3]=='c' || data[3]=='C') ) {
      long flags = AVIIF_KEYFRAME;
      off_t pos = this->idx_grow.nexttagoffset + ioff;
      long len = n;
      if (video_index_append(this->avi, pos, len, flags) == -1) {
	/* If we're out of memory, we just don't grow the index, but
	 * nothing really bad happens. */
      }
    }
    for(i=0; i < this->avi->n_audio; ++i) {
      if (strncasecmp(data, this->avi->audio[i]->audio_tag, 4) == 0) {
	off_t pos = this->idx_grow.nexttagoffset + ioff;
	long len = n;
	if (audio_index_append(this->avi, i, pos, len,
	    this->avi->audio_tot) == -1) {
	  /* As above. */
	}
        this->avi->audio_tot += len;
      }
    }

    this->idx_grow.nexttagoffset =
      this->input->seek(this->input, PAD_EVEN(n), SEEK_CUR);

  }

  /* Clear the OSD */
  if (did_osd) {
      this->xine->osd_renderer->hide (this->xine->osd, 0);
  }

  this->input->seek(this->input, savepos, SEEK_SET);

  if (retval < 0) retval = -1;
  return retval;
}

/* Fetch the current video index entry, growing the index if necessary. */
static video_index_entry_t *video_cur_index_entry(demux_avi_t *this)
{
  avi_t *AVI = this->avi;
  if (AVI->video_posf >= AVI->video_idx.video_frames) {
    /* We don't have enough frames; see if the file's bigger yet. */
    if (idx_grow(this, video_pos_stopper, NULL) < 0) {
      /* We still don't have enough frames.  Oh, well. */
      return NULL;
    }
  }
  return &(AVI->video_idx.vindex[AVI->video_posf]);
}

/* Fetch the current audio index entry, growing the index if necessary. */
static audio_index_entry_t *audio_cur_index_entry(demux_avi_t *this,
    avi_audio_t *AVI_A)
{
  if (AVI_A->audio_posc >= AVI_A->audio_idx.audio_chunks) {
    /* We don't have enough chunks; see if the file's bigger yet. */
    if (idx_grow(this, audio_pos_stopper, AVI_A) < 0) {
      /* We still don't have enough chunks.  Oh, well. */
      return NULL;
    }
  }
  return &(AVI_A->audio_idx.aindex[AVI_A->audio_posc]);
}

static void AVI_close(avi_t *AVI)
{
  int i;

  if(AVI->idx) free(AVI->idx);
  if(AVI->video_idx.vindex) free(AVI->video_idx.vindex);

  for(i=0; i<AVI->n_audio; i++) {
    if(AVI->audio[i]->audio_idx.aindex) free(AVI->audio[i]->audio_idx.aindex);
    if(AVI->audio[i]->wavex) free(AVI->audio[i]->wavex);
    free(AVI->audio[i]);
  }
  free(AVI);
}

#define ERR_EXIT(x)	\
do {			\
   this->AVI_errno = x; \
   free (AVI);  \
   return 0;		\
} while(0)

static int avi_sampsize(avi_t *AVI, int track)
{
  int s;
  s = ((AVI->audio[track]->wavex->wBitsPerSample+7)/8)*
        AVI->audio[track]->wavex->nChannels;
  if (s==0)
    s=1; /* avoid possible zero divisions */
  return s;
}

static int avi_add_index_entry(demux_avi_t *this, avi_t *AVI, unsigned char *tag,
                               long flags, long pos, long len)
{
  void *ptr;

  if(AVI->n_idx>=AVI->max_idx) {
    ptr = realloc((void *)AVI->idx,(AVI->max_idx+4096)*16);
    if(ptr == 0) {
      this->AVI_errno = AVI_ERR_NO_MEM;
      return -1;
    }
    AVI->max_idx += 4096;
    AVI->idx = (unsigned char((*)[16]) ) ptr;
  }

  /* Add index entry */

  memcpy(AVI->idx[AVI->n_idx],tag,4);
  long2str(AVI->idx[AVI->n_idx]+ 4,flags);
  long2str(AVI->idx[AVI->n_idx]+ 8,pos);
  long2str(AVI->idx[AVI->n_idx]+12,len);

  /* Update counter */

  AVI->n_idx++;

  return 0;
}

static void gen_index_show_progress (demux_avi_t *this, int percent) {

  char str[60];

  sprintf (str, "recons. index %3d%%", percent);

  this->xine->osd_renderer->filled_rect (this->xine->osd, 0, 0, 299, 99, 0);
  this->xine->osd_renderer->render_text (this->xine->osd, 5, 30, str, OSD_TEXT1);
  this->xine->osd_renderer->show (this->xine->osd, 0);


}

static avi_t *AVI_init(demux_avi_t *this)  {

  avi_t *AVI;
  long i, j, n, idx_type;
  unsigned char *hdrl_data;
  long hdrl_len=0;
  off_t ioff;
  int lasttag = 0;
  int vids_strh_seen = 0;
  int vids_strf_seen = 0;
  int auds_strh_seen = 0;
  int auds_strf_seen = 0;
  int num_stream = 0;
  char data[256];

  /* Create avi_t structure */

  AVI = (avi_t *) xine_xmalloc(sizeof(avi_t));
  if(AVI==NULL) {
    this->AVI_errno = AVI_ERR_NO_MEM;
    return 0;
  }
  memset((void *)AVI,0,sizeof(avi_t));

  /* Read first 12 bytes and check that this is an AVI file */

  this->input->seek(this->input, 0, SEEK_SET);
  if( this->input->read(this->input, data,12) != 12 ) ERR_EXIT(AVI_ERR_READ) ;

  if( strncasecmp(data  ,"RIFF",4) !=0 ||
      strncasecmp(data+8,"AVI ",4) !=0 )
    ERR_EXIT(AVI_ERR_NO_AVI) ;
  /* Go through the AVI file and extract the header list,
     the start position of the 'movi' list and an optionally
     present idx1 tag */

  hdrl_data = NULL;

  while(1) {

    /* Keep track of the last place we tried to read something. */
    this->idx_grow.nexttagoffset =
      this->input->seek(this->input, 0, SEEK_CUR);

    if (this->input->read(this->input, data,8) != 8 )
      break; /* We assume it's EOF */

    n = str2ulong(data+4);
    n = PAD_EVEN(n);

    if(strncasecmp(data,"LIST",4) == 0) {
      if( this->input->read(this->input, data,4) != 4 ) ERR_EXIT(AVI_ERR_READ);
      n -= 4;

      if(strncasecmp(data,"hdrl",4) == 0) {

        hdrl_len = n;
        hdrl_data = (unsigned char *) xine_xmalloc(n);
        if(hdrl_data==0)
          ERR_EXIT(AVI_ERR_NO_MEM);
        if (this->input->read(this->input, hdrl_data,n) != n )
          ERR_EXIT(AVI_ERR_READ);

      } else if(strncasecmp(data,"movi",4) == 0)  {

        AVI->movi_start = this->input->seek(this->input, 0,SEEK_CUR);
        this->input->seek(this->input, n, SEEK_CUR);
      } else
        this->input->seek(this->input, n, SEEK_CUR);

    } else if(strncasecmp(data,"idx1",4) == 0 ||
              strncasecmp(data,"iddx",4) == 0) {
      
      /* n must be a multiple of 16, but the reading does not
      break if this is not the case */

      AVI->n_idx = AVI->max_idx = n/16;
      free(AVI->idx);  /* On the off chance there are multiple index chunks */
      AVI->idx = (unsigned  char((*)[16]) ) xine_xmalloc(n);
      if (AVI->idx==0)
        ERR_EXIT(AVI_ERR_NO_MEM);

      if (this->input->read(this->input, (char *)AVI->idx, n) != n )
      {
        xine_log (this->xine, XINE_LOG_FORMAT, _("demux_avi: avi index is broken\n"));
        free (AVI->idx);	/* Index is broken, reconstruct */
        AVI->idx = NULL;
        AVI->n_idx = AVI->max_idx = 0;
        break; /* EOF */
      }

    } else
      this->input->seek(this->input, n, SEEK_CUR);
  }

  if(!hdrl_data) ERR_EXIT(AVI_ERR_NO_HDRL) ;
  if(!AVI->movi_start) ERR_EXIT(AVI_ERR_NO_MOVI) ;

  /* Interpret the header list */

  for (i=0;i<hdrl_len;) {
    /* List tags are completly ignored */

    if (strncasecmp(hdrl_data+i,"LIST",4)==0) {
      i+= 12;
      continue;
    }

    n = str2ulong(hdrl_data+i+4);
    n = PAD_EVEN(n);

    /* Interpret the tag and its args */

    if(strncasecmp(hdrl_data+i,"strh",4)==0) {

      i += 8;
      if(strncasecmp(hdrl_data+i,"vids",4) == 0 && !vids_strh_seen) {

        memcpy(AVI->compressor,hdrl_data+i+4,4);
        AVI->compressor[4] = 0;
        AVI->dwScale = str2ulong(hdrl_data+i+20);
        AVI->dwRate  = str2ulong(hdrl_data+i+24);

        if(AVI->dwScale!=0)
          AVI->fps = (double)AVI->dwRate/(double)AVI->dwScale;

        this->video_step = (long) (90000.0 / AVI->fps);

        AVI->video_strn = num_stream;
        vids_strh_seen = 1;
        lasttag = 1; /* vids */
      } else if (strncasecmp (hdrl_data+i,"auds",4) ==0 /* && ! auds_strh_seen*/) {
        if(AVI->n_audio < MAX_AUDIO_STREAMS) {
          avi_audio_t *a = (avi_audio_t *) xine_xmalloc(sizeof(avi_audio_t));
          if(a==NULL) {
            this->AVI_errno = AVI_ERR_NO_MEM;
            return 0;
          }
          memset((void *)a,0,sizeof(avi_audio_t));
          AVI->audio[AVI->n_audio] = a;

          a->audio_strn    = num_stream;
          a->dwScale_audio = str2ulong(hdrl_data+i+20);
          a->dwRate_audio  = str2ulong(hdrl_data+i+24);
          a->dwSampleSize  = str2ulong(hdrl_data+i+44);
          auds_strh_seen = 1;
          lasttag = 2; /* auds */
          AVI->n_audio++;
        }
      } else
        lasttag = 0;
      num_stream++;
    } else if(strncasecmp(hdrl_data+i,"strf",4)==0) {
      i += 8;
      if(lasttag == 1) {
        /* printf ("size : %d\n",sizeof(AVI->bih)); */
        memcpy (&AVI->bih, hdrl_data+i, sizeof(AVI->bih));
        xine_bmiheader_le2me( &AVI->bih );
        
        /* stream_read(demuxer->stream,(char*) &avi_header.bih,MIN(size2,sizeof(avi_header.bih))); */
        AVI->width  = AVI->bih.biWidth;
        AVI->height = AVI->bih.biHeight;

        /*
          printf ("size : %d x %d (%d x %d)\n", AVI->width, AVI->height, AVI->bih.biWidth, AVI->bih.biHeight);
          printf("  biCompression %d='%.4s'\n", AVI->bih.biCompression,
                 &AVI->bih.biCompression);
        */
        vids_strf_seen = 1;

        /* load the palette, if there is one */
        AVI->palette_count = AVI->bih.biClrUsed;
        if (AVI->palette_count > 256) {
          printf ("demux_avi: number of colors exceeded 256 (%d)",
            AVI->palette_count);
          AVI->palette_count = 256;
        }
        for (j = 0; j < AVI->palette_count; j++) {
          AVI->palette[j].b = *(hdrl_data + i + sizeof(AVI->bih) + j * 4 + 0);
          AVI->palette[j].g = *(hdrl_data + i + sizeof(AVI->bih) + j * 4 + 1);
          AVI->palette[j].r = *(hdrl_data + i + sizeof(AVI->bih) + j * 4 + 2);
        }

      } else if(lasttag == 2) {

        AVI->audio[AVI->n_audio-1]->wavex=(xine_waveformatex *)malloc(n);
        AVI->audio[AVI->n_audio-1]->wavex_len=n;
        
        memcpy((void *)AVI->audio[AVI->n_audio-1]->wavex, hdrl_data+i, n);
        xine_waveformatex_le2me( AVI->audio[AVI->n_audio-1]->wavex );
        auds_strf_seen = 1;
      }
      lasttag = 0;
    } else {
      i += 8;
      lasttag = 0;
    }

    i += n;
  }

  if( hdrl_data )
    free( hdrl_data );
  hdrl_data = NULL;

  /* somehow ffmpeg doesn't specify the number of frames here */
  /* if (!vids_strh_seen || !vids_strf_seen || AVI->video_frames==0) */
  if (!vids_strh_seen || !vids_strf_seen)
    ERR_EXIT(AVI_ERR_NO_VIDS);


  AVI->video_tag[0] = AVI->video_strn/10 + '0';
  AVI->video_tag[1] = AVI->video_strn%10 + '0';
  AVI->video_tag[2] = 'd';
  AVI->video_tag[3] = 'b';


  for(i = 0; i < AVI->n_audio; i++) {
    /* Audio tag is set to "99wb" if no audio present */
    if(!AVI->audio[i]->wavex->nChannels) AVI->audio[i]->audio_strn = 99;

    AVI->audio[i]->audio_tag[0] = AVI->audio[i]->audio_strn/10 + '0';
    AVI->audio[i]->audio_tag[1] = AVI->audio[i]->audio_strn%10 + '0';
    AVI->audio[i]->audio_tag[2] = 'w';
    AVI->audio[i]->audio_tag[3] = 'b';
  }

  this->input->seek(this->input, AVI->movi_start, SEEK_SET);

  /* if the file has an idx1, check if this is relative
     to the start of the file or to the start of the movi list */

  idx_type = 0;

  if(AVI->idx) {
    off_t  pos;
    long   len;

    /* Search the first videoframe in the idx1 and look where
       it is in the file */

    for(i=0;i<AVI->n_idx;i++)
      if( strncasecmp(AVI->idx[i],AVI->video_tag,3)==0 ) break;
    if (i>=AVI->n_idx) {
      ERR_EXIT(AVI_ERR_NO_VIDS);
    }

    pos = str2ulong(AVI->idx[i]+ 8);
    len = str2ulong(AVI->idx[i]+12);

    this->input->seek(this->input, pos, SEEK_SET);
    if(this->input->read(this->input, data, 8)!=8) ERR_EXIT(AVI_ERR_READ) ;

    if( strncasecmp(data,AVI->idx[i],4)==0 && str2ulong(data+4)==len ) {
      idx_type = 1; /* Index from start of file */

    } else {

      this->input->seek(this->input, pos+AVI->movi_start-4, SEEK_SET);
      if(this->input->read(this->input, data, 8)!=8)
        ERR_EXIT(AVI_ERR_READ) ;
      if( strncasecmp(data,AVI->idx[i],4)==0 && str2ulong(data+4)==len ) {
        idx_type = 2; /* Index from start of movi list */
      }
    }
    /* idx_type remains 0 if neither of the two tests above succeeds */
  }

  if (idx_type != 0) {
    /* Now generate the video index and audio index arrays from the
     * idx1 record. */

    AVI->audio_tot = 0;
    ioff = idx_type == 1 ? 8 : AVI->movi_start+4;

    for(i=0;i<AVI->n_idx;i++) {
      if(strncasecmp(AVI->idx[i],AVI->video_tag,3) == 0)	{
	off_t pos = str2ulong(AVI->idx[i]+ 8)+ioff;
	long len = str2ulong(AVI->idx[i]+12);
	long flags = str2ulong(AVI->idx[i]+4);
	if (video_index_append(AVI, pos, len, flags) == -1) {
	  ERR_EXIT(AVI_ERR_NO_MEM) ;
	}
      }
      for(n = 0; n < AVI->n_audio; n++) {
	if(strncasecmp(AVI->idx[i],AVI->audio[n]->audio_tag,4) == 0) {
	  off_t pos = str2ulong(AVI->idx[i]+ 8)+ioff;
	  long len = str2ulong(AVI->idx[i]+12);
	  if (audio_index_append(AVI, n, pos, len, AVI->audio_tot) == -1) {
	    ERR_EXIT(AVI_ERR_NO_MEM) ;
	  }
	  AVI->audio_tot += len;
	}
      }
    }
  } else {
    /* We'll just dynamically grow the index as needed. */
    this->idx_grow.nexttagoffset = AVI->movi_start;
  }

  /* Reposition the file */

  this->input->seek(this->input, AVI->movi_start, SEEK_SET);
  AVI->video_posf = 0;
  AVI->video_posb = 0;

  return AVI;
}

static void AVI_seek_start(avi_t *AVI)
{
  int i;

  AVI->video_posf = 0;
  AVI->video_posb = 0;

  for(i = 0; i < AVI->n_audio; i++) {
    AVI->audio[i]->audio_posc = 0;
    AVI->audio[i]->audio_posb = 0;
  }
}

static long AVI_read_audio(demux_avi_t *this, avi_audio_t *AVI_A, char *audbuf,
                           long bytes, int *buf_flags) {

  off_t nr, pos, left, todo;
  audio_index_entry_t *aie = audio_cur_index_entry(this, AVI_A);

  if(!aie)  {
    this->AVI_errno = AVI_ERR_NO_IDX;   return -1;
  }

  nr = 0; /* total number of bytes read */

  /* printf ("avi audio package len: %d\n", AVI_A->audio_index[AVI_A->audio_posc].len); */


  while(bytes>0) {
    left = aie->len - AVI_A->audio_posb;
    if(left==0) {
      AVI_A->audio_posc++;
      AVI_A->audio_posb = 0;
      aie = audio_cur_index_entry(this, AVI_A);
      if (!aie) {
	this->AVI_errno = AVI_ERR_NO_IDX;
	return -1;
      }
      if (nr>0) {
        *buf_flags = BUF_FLAG_FRAME_END;
        return nr;
      }
      left = aie->len - AVI_A->audio_posb;
    }
    if(bytes<left)
      todo = bytes;
    else
      todo = left;
    pos = aie->pos + AVI_A->audio_posb;
    /* printf ("demux_avi: read audio from %lld\n", pos); */
    if (this->input->seek (this->input, pos, SEEK_SET)<0)
      return -1;
    if (this->input->read(this->input, audbuf+nr,todo) != todo) {
      this->AVI_errno = AVI_ERR_READ;
      *buf_flags = 0;
      return -1;
    }
    bytes -= todo;
    nr    += todo;
    AVI_A->audio_posb += todo;
  }

  left = aie->len - AVI_A->audio_posb;
  if (left==0)
    *buf_flags = BUF_FLAG_FRAME_END;
  else
    *buf_flags = 0;

  return nr;
}

static long AVI_read_video(demux_avi_t *this, avi_t *AVI, char *vidbuf,
                           long bytes, int *buf_flags) {

  off_t nr, pos, left, todo;
  video_index_entry_t *vie = video_cur_index_entry(this);

  if (!vie) {
    this->AVI_errno = AVI_ERR_NO_IDX;
    return -1;
  }

  nr = 0; /* total number of bytes read */

  while(bytes>0) {

    left = vie->len - AVI->video_posb;

    if(left==0) {
      AVI->video_posf++;
      AVI->video_posb = 0;
      vie = video_cur_index_entry(this);
      if (!vie) {
	this->AVI_errno = AVI_ERR_NO_IDX;
	return -1;
      }
      if (nr>0) {
        *buf_flags = BUF_FLAG_FRAME_END;
        return nr;
      }
      left = vie->len - AVI->video_posb;
    }
    if(bytes<left)
      todo = bytes;
    else
      todo = left;
    pos = vie->pos + AVI->video_posb;
    /* printf ("demux_avi: read video from %lld\n", pos); */
    if (this->input->seek (this->input, pos, SEEK_SET)<0)
      return -1;
    if (this->input->read(this->input, vidbuf+nr,todo) != todo) {
      this->AVI_errno = AVI_ERR_READ;
      *buf_flags = 0;
      return -1;
    }
    bytes -= todo;
    nr    += todo;
    AVI->video_posb += todo;
  }

  left = vie->len - AVI->video_posb;
  if (left==0)
    *buf_flags = BUF_FLAG_FRAME_END;
  else
    *buf_flags = 0;

  return nr;
}


static int demux_avi_next (demux_avi_t *this) {

  int            i;
  buf_element_t *buf = NULL;
  int64_t        audio_pts, video_pts;
  int            do_read_video = (this->avi->n_audio == 0);

  /* Try to grow the index, in case more of the avi file has shown up
   * since we last checked.  If it's still too small, well then we're at
   * the end. */
  if (this->avi->video_idx.video_frames <= this->avi->video_posf) {
    if (idx_grow(this, video_pos_stopper, NULL) < 0) {
      return 0;
    }
  }

  for (i=0; i < this->avi->n_audio; i++) {
    if (!this->no_audio && (this->avi->audio[i]->audio_idx.audio_chunks <=
			      this->avi->audio[i]->audio_posc)) {
      if (idx_grow(this, audio_pos_stopper, this->avi->audio[i]) < 0) {
	return 0;
      }
    }
  }


  video_pts = get_video_pts (this, this->avi->video_posf);

  for (i=0; i < this->avi->n_audio; i++) {
    avi_audio_t *audio = this->avi->audio[i];
    audio_index_entry_t *aie = audio_cur_index_entry(this, audio);

    /* The tests above mean aie should never be NULL, but just to be
     * safe. */
    if (!aie) {
      return 0;
    }

    audio_pts =
      get_audio_pts (this, i, audio->audio_posc, aie->tot, audio->audio_posb);
    if (!this->no_audio && (audio_pts < video_pts)) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->content = buf->mem;

      /* read audio */

      buf->pts    = audio_pts;
      buf->size   = AVI_read_audio (this, audio, buf->mem, 2048, &buf->decoder_flags);

      if (buf->size<0) {
        buf->free_buffer (buf);
        return 0;
      }

      buf->input_pos  = 0;
      buf->input_time = 0;

      buf->type = audio->audio_type | i;
      buf->decoder_info[1] = audio->wavex->nSamplesPerSec; /* audio Rate */
      buf->decoder_info[2] = audio->wavex->wBitsPerSample; /* audio bits */
      buf->decoder_info[3] = audio->wavex->nChannels; /* audio channels */

      if(this->audio_fifo) {
        this->audio_fifo->put (this->audio_fifo, buf);
      } else {
        buf->free_buffer (buf);
      }
    } else
      do_read_video = 1;
  }

  if (do_read_video) {

    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->content = buf->mem;

    /* read video */

    buf->pts        = video_pts;
    buf->size       = AVI_read_video (this, this->avi, buf->mem, 2048, &buf->decoder_flags);
    buf->type       = this->avi->video_type;

    buf->input_time = video_pts / 90000;
    buf->input_pos  = this->input->get_current_pos(this->input);

    if (buf->size<0) {
      buf->free_buffer (buf);
      return 0;
    }

    /*
      printf ("demux_avi: adding buf %d to video fifo, decoder_info[0]: %d\n",
      buf, buf->decoder_info[0]);
    */

    this->video_fifo->put (this->video_fifo, buf);

    /*
     * send packages to inform & drive text spu decoder
     */

    if (this->have_spu && (buf->decoder_flags & BUF_FLAG_FRAME_END)) {
      buf_element_t *buf;
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

      buf->decoder_flags = BUF_FLAG_FRAME_END;
      buf->type          = BUF_SPU_TEXT;
      buf->pts           = video_pts;

      buf->decoder_info[1] = this->avi->video_posf;

      this->video_fifo->put (this->video_fifo, buf);
    }
  }

  if( buf ) {
    return (buf->size>0);
  } else {
    return 0;
  }
}

static void *demux_avi_loop (void *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;

  pthread_mutex_lock( &this->mutex );
  /* do-while needed to seek after demux finished */
  do {

    /* main demuxer loop */
    while(this->status == DEMUX_OK) {

      if (!demux_avi_next(this)) {
        this->status = DEMUX_FINISHED;
      }

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
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
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }

  printf ("demux_avi: demux loop finished.\n");

  this->thread_running = 0;
  pthread_mutex_unlock( &this->mutex );

  pthread_exit(NULL);

  return NULL;
}

static void demux_avi_stop (demux_plugin_t *this_gen) {

  demux_avi_t   *this = (demux_avi_t *) this_gen;
  void *p;

  pthread_mutex_lock( &this->mutex );

  if (!this->thread_running) {
    printf ("demux_avi: stop...ignored\n");
    pthread_mutex_unlock( &this->mutex );
    return;
  }

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;

  pthread_mutex_unlock( &this->mutex );
  pthread_join (this->thread, &p);

  xine_demux_flush_engine(this->xine);
  /*
    AVI_close (this->avi);
    this->avi = NULL;
  */

  xine_demux_control_end(this->xine, BUF_FLAG_END_USER);
}

static void demux_avi_close (demux_plugin_t *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;

  if (this->avi)
    AVI_close (this->avi);

  free(this);
}

static int demux_avi_get_status (demux_plugin_t *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;

  return (this->thread_running?DEMUX_OK:DEMUX_FINISHED);
}

static int demux_avi_start (demux_plugin_t *this_gen,
                            fifo_buffer_t *video_fifo,
                            fifo_buffer_t *audio_fifo,
                            off_t start_pos, int start_time) {

  demux_avi_t    *this = (demux_avi_t *) this_gen;
  int             i;
  buf_element_t  *buf;
  int64_t         video_pts = 0;
  int             err;
  unsigned char  *sub;
  video_index_entry_t *vie = NULL;
  int             status;

  pthread_mutex_lock( &this->mutex );

  this->status = DEMUX_OK;

  if( !this->thread_running ) {
    this->audio_fifo   = audio_fifo;
    this->video_fifo   = video_fifo;

    xine_log (this->xine, XINE_LOG_FORMAT, _("demux_avi: video format = %s\n"),
              this->avi->compressor);
    xine_log (this->xine, XINE_LOG_FORMAT, _("demux_avi: video frame size %ld x %ld\n"),
              this->avi->width, this->avi->height);
    for(i=0; i < this->avi->n_audio; i++)
      xine_log (this->xine, XINE_LOG_FORMAT, _("demux_avi: audio format[%d] = 0x%lx\n"),
                i, this->avi->audio[i]->wavex->wFormatTag);
    this->no_audio = 0;

    for(i=0; i < this->avi->n_audio; i++) {
      this->avi->audio[i]->audio_type = formattag_to_buf_audio (this->avi->audio[i]->wavex->wFormatTag);

      if( !this->avi->audio[i]->audio_type ) {
        xine_log (this->xine, XINE_LOG_FORMAT, _("demux_avi: unknown audio type 0x%lx\n"),
                  this->avi->audio[i]->wavex->wFormatTag);
        xine_report_codec( this->xine, XINE_CODEC_AUDIO, this->avi->audio[i]->wavex->wFormatTag, 0, 0);
        this->no_audio  = 1;
        this->avi->audio[i]->audio_type     = BUF_CONTROL_NOP;
      }
      else
        xine_log (this->xine, XINE_LOG_FORMAT, _("demux_avi: audio type %s (wFormatTag 0x%x)\n"),
                  buf_audio_name(this->avi->audio[i]->audio_type),
                  (int)this->avi->audio[i]->wavex->wFormatTag);
    }
  }

  AVI_seek_start (this->avi);

  /*
   * seek to start pos / time
   */

  printf ("demux_avi: start pos is %lld, start time is %d\n", start_pos, start_time);

  /* Seek video.  We do a single idx_grow at the beginning rather than
   * incrementally growing the index in a loop, so that if the index
   * grow is going to take a while, the user is notified via the OSD
   * (which only shows up if >= 1000 index entries are added at a time). */
  if (start_pos) {

    if (idx_grow(this, start_pos_stopper, &start_pos) < 0) {
      /* We read as much of the file as we could, and didn't reach our
       * starting point.  Too bad. */
      this->status = DEMUX_FINISHED;
      printf ("demux_avi: video seek to start failed\n");
    } else {
      /* We know for sure the last index entry is past our starting
       * point; find the lowest index entry that's past our starting
       * point.  This could in theory be turned into a binary search,
       * but it's a linear search for now. */
      while(1) {
	vie = video_cur_index_entry(this);
	if ((vie->pos >= start_pos) && (vie->flags & AVIIF_KEYFRAME)) {
	  break;
	}
	this->avi->video_posf += 1;
      }
    }

    video_pts = get_video_pts (this, this->avi->video_posf);

  } else if (start_time) {

    video_pts = start_time * 90000;

    if (idx_grow(this, start_time_stopper, &video_pts) < 0) {
      /* We read as much of the file as we could, and didn't reach our
       * starting point.  Too bad. */
      this->status = DEMUX_FINISHED;
      printf ("demux_avi: video seek to start failed\n");
    } else {
      /* We know for sure the last index entry is past our starting
       * point; find the lowest index entry that's past our starting
       * point.  This could in theory be turned into a binary search,
       * but it's a linear search for now. */
      while(1) {
	if (get_video_pts (this, this->avi->video_posf) >= video_pts) {
	  break;
	}
	this->avi->video_posf += 1;
      }
    }
  }

  /* Seek audio.  We can do this incrementally, on the theory that the
   * audio position we're looking for will be pretty close to the video
   * position we've already found, so we won't be seeking though the
   * file much at this point. */
  if (!this->no_audio && this->status == DEMUX_OK) {
    for(i=0; i < this->avi->n_audio; i++) {
      while (1) {
	audio_index_entry_t *aie =
	  audio_cur_index_entry(this, this->avi->audio[i]);

	if (aie && get_audio_pts(this, i, this->avi->audio[i]->audio_posc,
	    aie->tot, 0) >= video_pts) {
	  break;
	}
        if (!aie) {
          this->status = DEMUX_FINISHED;

          printf ("demux_avi: audio seek to start failed\n");
          break;
        }
        this->avi->audio[i]->audio_posc++;
      }
    }
  }

  /*
   * send start buffers
   */
  if( !this->thread_running && (this->status == DEMUX_OK) ) {
    xine_demux_control_start(this->xine);
  } else {
    xine_demux_flush_engine(this->xine);
  }

  if( this->status == DEMUX_OK )
  {
    xine_demux_control_newpts(this->xine, video_pts, BUF_FLAG_SEEK);
  }

  if( !this->thread_running && (this->status == DEMUX_OK) ) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->content = buf->mem;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[1] = this->video_step;
    memcpy (buf->content, &this->avi->bih, sizeof (this->avi->bih));
    buf->size = sizeof (this->avi->bih);

    this->avi->video_type = fourcc_to_buf_video(this->avi->bih.biCompression);

    if( !this->avi->video_type )
      this->avi->video_type = fourcc_to_buf_video(*(uint32_t *)this->avi->compressor);

    if ( !this->avi->video_type ) {
      xine_log (this->xine, XINE_LOG_FORMAT, _("demux_avi: unknown video codec '%.4s'\n"),
                (char*)&this->avi->bih.biCompression);
      buf->free_buffer (buf);
    
      xine_report_codec( this->xine, XINE_CODEC_VIDEO, this->avi->bih.biCompression, 0, 0);

      this->status = DEMUX_FINISHED;
    } else {
      buf->type = this->avi->video_type;
      xine_log (this->xine, XINE_LOG_FORMAT, _("demux_avi: video codec is '%s'\n"),
                buf_video_name(buf->type));

      this->video_fifo->put (this->video_fifo, buf);

      /* send off the palette, if there is one */
      if (this->avi->palette_count) {
        buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
        buf->content = buf->mem;
        buf->decoder_flags = BUF_FLAG_SPECIAL;
        buf->decoder_info[1] = BUF_SPECIAL_PALETTE;
        buf->decoder_info[2] = this->avi->palette_count;
        buf->decoder_info[3] = (unsigned int)&this->avi->palette;
        buf->size = 0;
        buf->type = this->avi->video_type;
        this->video_fifo->put (this->video_fifo, buf);
      }

      if(this->audio_fifo) {
        for(i=0; i<this->avi->n_audio; i++) {
          avi_audio_t *a = this->avi->audio[i];

          buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
          buf->content = buf->mem;
          buf->decoder_flags = BUF_FLAG_HEADER;
          memcpy (buf->content, a->wavex, a->wavex_len);
          buf->size = a->wavex_len;
          buf->type = a->audio_type | i;
          buf->decoder_info[0] = 0; /* first package, containing wavex */
          buf->decoder_info[1] = a->wavex->nSamplesPerSec; /* Audio Rate */
          buf->decoder_info[2] = a->wavex->wBitsPerSample; /* Audio bits */
          buf->decoder_info[3] = a->wavex->nChannels; /* Audio bits */
          this->audio_fifo->put (this->audio_fifo, buf);
        }
      }

      /*
       * send external spu file pointer, if present
       */

      if (this->input->get_optional_data (this->input, &sub, INPUT_OPTIONAL_DATA_TEXTSPU0)) {

        buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
        buf->content = sub;

        buf->type = BUF_SPU_TEXT;

        buf->decoder_flags   = BUF_FLAG_HEADER;
        buf->decoder_info[1] = this->avi->width;
        buf->decoder_info[2] = this->avi->height;

        this->video_fifo->put (this->video_fifo, buf);

        this->have_spu = 1;

        printf ("demux_avi: text subtitle file available\n");

      } else
        this->have_spu = 0;

      this->send_end_buffers = 1;
      this->thread_running = 1;
      if ((err = pthread_create (&this->thread, NULL, demux_avi_loop, this)) != 0) {
        printf ("demux_avi: can't create new thread (%s)\n",
                strerror(err));
        abort();
      }
    }
  }

  /* this->status is saved because we can be interrupted between
   * pthread_mutex_unlock and return
   */
  status = this->status;
  pthread_mutex_unlock( &this->mutex );
  return status;
}


static int demux_avi_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time) {
  demux_avi_t *this = (demux_avi_t *) this_gen;

  return demux_avi_start (this_gen, this->video_fifo, this->audio_fifo,
                          start_pos, start_time);
}

static int demux_avi_open(demux_plugin_t *this_gen,
                          input_plugin_t *input, int stage) {

  demux_avi_t *this = (demux_avi_t *) this_gen;
  printf ("demux_avi: input capabilities = %d\n",
	  input->get_capabilities(input));
  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf("demux_avi.c: not seekable, can't handle!\n");
    return DEMUX_CANNOT_HANDLE;
  }

  switch(stage) {

  case STAGE_BY_CONTENT: {
    if (input->get_blocksize(input))
      return DEMUX_CANNOT_HANDLE;

    if (!(input->get_capabilities(input) & INPUT_CAP_SEEKABLE))
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 0, SEEK_SET);

    this->input = input;

    if (this->avi)
      AVI_close (this->avi);

    this->avi = AVI_init (this);

    if (this->avi) {

      printf ("demux_avi: %ld frames\n", this->avi->video_idx.video_frames);

      strncpy(this->last_mrl, input->get_mrl (input), 1024);

      return DEMUX_CAN_HANDLE;
    } else
      printf ("demux_avi: AVI_init failed (AVI_errno: %d)\n", this->AVI_errno);

    return DEMUX_CANNOT_HANDLE;
  }
  break;

  case STAGE_BY_EXTENSION: {
    char *ending, *mrl;
    char *m, *valid_ends;

    mrl = input->get_mrl (input);

    ending = strrchr(mrl, '.');

    if(ending) {
      xine_strdupa(valid_ends,
                   this->config->register_string(this->config,
                                                 "mrl.ends_avi", VALID_ENDS,
                                                 _("valid mrls ending for avi demuxer"),
                                                 NULL, NULL, NULL));
      while((m = xine_strsep(&valid_ends, ",")) != NULL) {

        while(*m == ' ' || *m == '\t') m++;

        if(!strcasecmp((ending + 1), m)) {

          this->input = input;

          if (this->avi)
            AVI_close (this->avi);

          this->avi = AVI_init (this);

          if (this->avi) {
            strncpy(this->last_mrl, input->get_mrl (input), 1024);
            return DEMUX_CAN_HANDLE;
          } else {
            printf ("demux_avi: AVI_init failed (AVI_errno: %d)\n",
            this->AVI_errno);
            return DEMUX_CANNOT_HANDLE;
          }
        }
      }
    }
    return DEMUX_CANNOT_HANDLE;
  }
  break;

  default:
    return DEMUX_CANNOT_HANDLE;
    break;
  }

  return DEMUX_CANNOT_HANDLE;
}

static char *demux_avi_get_id(void) {
  return "AVI";
}

static char *demux_avi_get_mimetypes(void) {
  return "video/msvideo: avi: AVI animation;"
         "video/x-msvideo: avi: AVI animation;";
}

static int demux_avi_get_stream_length (demux_plugin_t *this_gen) {

  demux_avi_t *this = (demux_avi_t *) this_gen;

  if (this->avi) {
    return get_video_pts(this, this->avi->video_idx.video_frames) / 90000 ;
  }

  return 0;
}

demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {
  
  demux_avi_t     *this;
  
  if (iface != 9) {
    xine_log (xine, XINE_LOG_PLUGIN,
              _("demux_avi: this plugin doesn't support plugin API version %d.\n"
                "demux_avi: this means there's a version mismatch between xine and this "
                "demux_avi: demuxer plugin.\nInstalling current demuxer plugins should help.\n"),
	      iface);
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_avi_t));
  this->config = xine->config;
  this->xine   = xine;

  (void*) this->config->register_string(this->config,
                                        "mrl.ends_avi", VALID_ENDS,
                                        _("valid mrls ending for avi demuxer"),
                                        NULL, NULL, NULL);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_avi_open;
  this->demux_plugin.start             = demux_avi_start;
  this->demux_plugin.seek              = demux_avi_seek;
  this->demux_plugin.stop              = demux_avi_stop;
  this->demux_plugin.close             = demux_avi_close;
  this->demux_plugin.get_status        = demux_avi_get_status;
  this->demux_plugin.get_identifier    = demux_avi_get_id;
  this->demux_plugin.get_stream_length = demux_avi_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_avi_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init( &this->mutex, NULL );

  return (demux_plugin_t *) this;
}
