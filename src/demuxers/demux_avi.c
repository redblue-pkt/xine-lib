/* 
 * Copyright (C) 2000 the xine project
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
 * $Id: demux_avi.c,v 1.7 2001/04/29 23:22:32 f1rmb Exp $
 *
 * demultiplexer for avi streams
 *
 * part of the code is taken from 
 * avilib (C) 1999 Rainer Johanni <Rainer@Johanni.de>
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
#include "monitor.h"
#include "demux.h"
#include "utils.h"
#include "libw32dll/wine/mmreg.h"
#include "libw32dll/wine/avifmt.h"
#include "libw32dll/wine/vfw.h"

/* The following variable indicates the kind of error */

static uint32_t xine_debug;

typedef struct
{
  long pos;
  long len;
} video_index_entry_t;

typedef struct
{
  long pos;
  long len;
  long tot;
} audio_index_entry_t;

typedef struct
{
  long   width;             /* Width  of a video frame */
  long   height;            /* Height of a video frame */
  long   dwScale, dwRate;
  double fps;               /* Frames per second */
  
  char   compressor[8];     /* Type of compressor, 4 bytes + padding for 0 byte */
  long   video_strn;        /* Video stream number */
  long   video_frames;      /* Number of video frames */
  char   video_tag[4];      /* Tag of video data */
  long   video_posf;        /* Number of next frame to be read
			       (if index present) */
  long   video_posb;        /* Video position: byte within frame */
  
  long   a_fmt;             /* Audio format, see #defines below */
  long   a_chans;           /* Audio channels, 0 for no audio */
  long   a_rate;            /* Rate in Hz */
  long   a_bits;            /* bits per audio sample */
  long   audio_strn;        /* Audio stream number */
  long   audio_bytes;       /* Total number of bytes of audio data */
  long   audio_chunks;      /* Chunks of audio data in the file */
  char   audio_tag[4];      /* Tag of audio data */
  long   audio_posc;        /* Audio position: chunk */
  long   audio_posb;        /* Audio position: byte within chunk */
  
  long                   pos;      /* position in file */
  long                   n_idx;    /* number of index entries actually filled */
  long                   max_idx;  /* number of index entries actually allocated */
  unsigned char        (*idx)[16]; /* index entries (AVI idx1 tag) */
  video_index_entry_t   *video_index;
  audio_index_entry_t   *audio_index;
  BITMAPINFOHEADER       bih;
  char                   wavex[64];
  off_t                  movi_start;
  uint32_t               AVI_errno; 
} avi_t;

typedef struct demux_avi_s {
  demux_plugin_t       demux_plugin;

  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;

  input_plugin_t      *input;

  avi_t               *avi;

  pthread_t            thread;

  int                  status;

  uint32_t             video_step;
  uint32_t             avg_bytes_per_sec;
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

#define AVI_ERR_NO_HDRL     10     /* AVI file has no has no header list,
                                      corrupted ??? */

#define AVI_ERR_NO_MOVI     11     /* AVI file has no has no MOVI list,
                                      corrupted ??? */

#define AVI_ERR_NO_VIDS     12     /* AVI file contains no video data */

#define AVI_ERR_NO_IDX      13     /* The file has been opened with
                                      getIndex==0, but an operation has been
                                      performed that needs an index */

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

static void AVI_close(avi_t *AVI)
{
  if(AVI->idx) free(AVI->idx);
  if(AVI->video_index) free(AVI->video_index);
  if(AVI->audio_index) free(AVI->audio_index);
  free(AVI);
}

#define ERR_EXIT(x) \
{ \
   AVI->AVI_errno = x; \
   return 0; \
}

#define PAD_EVEN(x) ( ((x)+1) & ~1 )

static int avi_sampsize(avi_t *AVI)
{
  int s;
  s = ((AVI->a_bits+7)/8)*AVI->a_chans;
  if(s==0) s=1; /* avoid possible zero divisions */
  return s;
}

static int avi_add_index_entry(avi_t *AVI, unsigned char *tag, 
			       long flags, long pos, long len)
{
  void *ptr;

  if(AVI->n_idx>=AVI->max_idx)
    {
      ptr = realloc((void *)AVI->idx,(AVI->max_idx+4096)*16);
      if(ptr == 0)
	{
	  AVI->AVI_errno = AVI_ERR_NO_MEM;
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

static avi_t *AVI_init(demux_avi_t *this) 
{
  avi_t *AVI;
  long i, n, idx_type;
  unsigned char *hdrl_data;
  long hdrl_len=0;
  long nvi, nai, ioff;
  long tot;
  int lasttag = 0;
  int vids_strh_seen = 0;
  int vids_strf_seen = 0;
  int auds_strh_seen = 0;
  int auds_strf_seen = 0;
  int num_stream = 0;
  char data[256];

  /* Create avi_t structure */

  AVI = (avi_t *) xmalloc(sizeof(avi_t));
  if(AVI==NULL)
    {
      AVI->AVI_errno = AVI_ERR_NO_MEM;
      return 0;
    }
  memset((void *)AVI,0,sizeof(avi_t));

  /* Read first 12 bytes and check that this is an AVI file */

  if( this->input->read(this->input, data,12) != 12 ) ERR_EXIT(AVI_ERR_READ) ;
						
  if( strncasecmp(data  ,"RIFF",4) !=0 ||
      strncasecmp(data+8,"AVI ",4) !=0 ) ERR_EXIT(AVI_ERR_NO_AVI) ;
  /* Go through the AVI file and extract the header list,
     the start position of the 'movi' list and an optionally
     present idx1 tag */
  
  hdrl_data = 0;
  
  while(1) {
    if (this->input->read(this->input, data,8) != 8 ) break; /* We assume it's EOF */
    
    n = str2ulong(data+4);
    n = PAD_EVEN(n);
    
    if(strncasecmp(data,"LIST",4) == 0)
      {
	if( this->input->read(this->input, data,4) != 4 ) ERR_EXIT(AVI_ERR_READ)
						    n -= 4;
	if(strncasecmp(data,"hdrl",4) == 0)
	  {
	    hdrl_len = n;
	    hdrl_data = (unsigned char *) xmalloc(n);
	    if(hdrl_data==0) ERR_EXIT(AVI_ERR_NO_MEM)
			       if( this->input->read(this->input, hdrl_data,n) != n ) ERR_EXIT(AVI_ERR_READ)
										}
	else if(strncasecmp(data,"movi",4) == 0)
	  {
	    AVI->movi_start = this->input->seek(this->input, 0,SEEK_CUR);
	    this->input->seek(this->input, n, SEEK_CUR);
	  }
	else
	  this->input->seek(this->input, n, SEEK_CUR);
      }
    else if(strncasecmp(data,"idx1",4) == 0)
      {
	/* n must be a multiple of 16, but the reading does not
	   break if this is not the case */
	
	AVI->n_idx = AVI->max_idx = n/16;
	AVI->idx = (unsigned  char((*)[16]) ) xmalloc(n);
	if(AVI->idx==0) ERR_EXIT(AVI_ERR_NO_MEM)
			  if( this->input->read(this->input, (char *)AVI->idx, n) != n ) ERR_EXIT(AVI_ERR_READ)
										  }
    else
      this->input->seek(this->input, n, SEEK_CUR);
  }
  
  if(!hdrl_data) ERR_EXIT(AVI_ERR_NO_HDRL) ;
  if(!AVI->movi_start) ERR_EXIT(AVI_ERR_NO_MOVI) ;

  /* Interpret the header list */
  
  for(i=0;i<hdrl_len;)
    {
      /* List tags are completly ignored */
      
      if(strncasecmp(hdrl_data+i,"LIST",4)==0) { i+= 12; continue; }
      
      n = str2ulong(hdrl_data+i+4);
      n = PAD_EVEN(n);
      
      /* Interpret the tag and its args */
      
      if(strncasecmp(hdrl_data+i,"strh",4)==0)
	{
	  i += 8;
	  if(strncasecmp(hdrl_data+i,"vids",4) == 0 && !vids_strh_seen)
	    {
	      memcpy(AVI->compressor,hdrl_data+i+4,4);
	      AVI->compressor[4] = 0;
	      AVI->dwScale = str2ulong(hdrl_data+i+20);
	      AVI->dwRate  = str2ulong(hdrl_data+i+24);

	      if(AVI->dwScale!=0)
		AVI->fps = (double)AVI->dwRate/(double)AVI->dwScale;
	      this->video_step = (long) (90000.0 / AVI->fps);
	      
	      AVI->video_frames    = str2ulong(hdrl_data+i+32);
	      AVI->video_strn = num_stream;
	      vids_strh_seen = 1;
	      lasttag = 1; /* vids */
	    }
	  else if (strncasecmp (hdrl_data+i,"auds",4) ==0 && ! auds_strh_seen)
	    {
	      AVI->audio_bytes = str2ulong(hdrl_data+i+32)*avi_sampsize(AVI);
	      AVI->audio_strn = num_stream;
	      auds_strh_seen = 1;
	      lasttag = 2; /* auds */
	    }
	  else
	    lasttag = 0;
	  num_stream++;
	}
      else if(strncasecmp(hdrl_data+i,"strf",4)==0)
	{
	  i += 8;
	  if(lasttag == 1) {
	    /* printf ("size : %d\n",sizeof(AVI->bih)); */
	    memcpy (&AVI->bih, hdrl_data+i, sizeof(AVI->bih));
	    /* stream_read(demuxer->stream,(char*) &avi_header.bih,MIN(size2,sizeof(avi_header.bih))); */
	    AVI->width  = str2ulong(hdrl_data+i+4);
	    AVI->height = str2ulong(hdrl_data+i+8);
	    
	    /*
	      printf ("size : %d x %d (%d x %d)\n", AVI->width, AVI->height, AVI->bih.biWidth, AVI->bih.biHeight);
	      printf("  biCompression %d='%.4s'\n", AVI->bih.biCompression, 
	      &AVI->bih.biCompression);
	    */
	    vids_strf_seen = 1;
	  }
	  else if(lasttag == 2)
	    {
	      memcpy (&AVI->wavex, hdrl_data+i, n);

	      AVI->a_fmt   = str2ushort(hdrl_data+i  );
	      AVI->a_chans = str2ushort(hdrl_data+i+2);
	      AVI->a_rate  = str2ulong (hdrl_data+i+4);
	      AVI->a_bits  = str2ushort(hdrl_data+i+14);
	      this->avg_bytes_per_sec = str2ulong (hdrl_data+i+8);
	      auds_strf_seen = 1;
	    }
	  lasttag = 0;
	}
      else
	{
	  i += 8;
	  lasttag = 0;
	}
      
      i += n;
    }
  
  free(hdrl_data);
  
  if(!vids_strh_seen || !vids_strf_seen || AVI->video_frames==0) ERR_EXIT(AVI_ERR_NO_VIDS)

								   AVI->video_tag[0] = AVI->video_strn/10 + '0';
  AVI->video_tag[1] = AVI->video_strn%10 + '0';
  AVI->video_tag[2] = 'd';
  AVI->video_tag[3] = 'b';

  /* Audio tag is set to "99wb" if no audio present */
  if(!AVI->a_chans) AVI->audio_strn = 99;

  AVI->audio_tag[0] = AVI->audio_strn/10 + '0';
  AVI->audio_tag[1] = AVI->audio_strn%10 + '0';
  AVI->audio_tag[2] = 'w';
  AVI->audio_tag[3] = 'b';

  this->input->seek(this->input, AVI->movi_start, SEEK_SET);

  /* if the file has an idx1, check if this is relative
     to the start of the file or to the start of the movi list */

  idx_type = 0;

  if(AVI->idx)
    {
      long pos, len;

      /* Search the first videoframe in the idx1 and look where
         it is in the file */

      for(i=0;i<AVI->n_idx;i++)
	if( strncasecmp(AVI->idx[i],AVI->video_tag,3)==0 ) break;
      if(i>=AVI->n_idx) ERR_EXIT(AVI_ERR_NO_VIDS)

			  pos = str2ulong(AVI->idx[i]+ 8);
      len = str2ulong(AVI->idx[i]+12);

      this->input->seek(this->input, pos, SEEK_SET);
      if(this->input->read(this->input, data, 8)!=8) ERR_EXIT(AVI_ERR_READ) ;
      if( strncasecmp(data,AVI->idx[i],4)==0 && str2ulong(data+4)==len )
	{
	  idx_type = 1; /* Index from start of file */
	}
      else
	{
	  this->input->seek(this->input, pos+AVI->movi_start-4, SEEK_SET);
	  if(this->input->read(this->input, data, 8)!=8) ERR_EXIT(AVI_ERR_READ) ;
	  if( strncasecmp(data,AVI->idx[i],4)==0 && str2ulong(data+4)==len )
	    {
	      idx_type = 2; /* Index from start of movi list */
	    }
	}
      /* idx_type remains 0 if neither of the two tests above succeeds */
    }
  
  if(idx_type == 0)
    {
      /* we must search through the file to get the index */

      this->input->seek(this->input, AVI->movi_start, SEEK_SET);

      AVI->n_idx = 0;

      while(1)
	{
	  if( this->input->read(this->input, data,8) != 8 ) break;
	  n = str2ulong(data+4);

	  /* The movi list may contain sub-lists, ignore them */

	  if(strncasecmp(data,"LIST",4)==0)
	    {
	      this->input->seek(this->input, 4,SEEK_CUR);
	      continue;
	    }

	  /* Check if we got a tag ##db, ##dc or ##wb */

	  if( ( (data[2]=='d' || data[2]=='D') &&
		(data[3]=='b' || data[3]=='B' || data[3]=='c' || data[3]=='C') )
	      || ( (data[2]=='w' || data[2]=='W') &&
		   (data[3]=='b' || data[3]=='B') ) )
	    {
	      avi_add_index_entry(AVI,data,0,this->input->seek(this->input, 0, SEEK_CUR)-8,n);
	    }

	  this->input->seek(this->input, PAD_EVEN(n), SEEK_CUR);
	}
      idx_type = 1;
    }

  /* Now generate the video index and audio index arrays */

  nvi = 0;
  nai = 0;

  for(i=0;i<AVI->n_idx;i++)
    {
      if(strncasecmp(AVI->idx[i],AVI->video_tag,3) == 0) nvi++;
      if(strncasecmp(AVI->idx[i],AVI->audio_tag,4) == 0) nai++;
    }

  AVI->video_frames = nvi;
  AVI->audio_chunks = nai;

  if(AVI->video_frames==0) ERR_EXIT(AVI_ERR_NO_VIDS) ;

  AVI->video_index = (video_index_entry_t *) xmalloc(nvi*sizeof(video_index_entry_t));
  if(AVI->video_index==0) ERR_EXIT(AVI_ERR_NO_MEM) ;

  if(AVI->audio_chunks) {
    AVI->audio_index = (audio_index_entry_t *) xmalloc(nai*sizeof(audio_index_entry_t));
    if(AVI->audio_index==0) ERR_EXIT(AVI_ERR_NO_MEM) ;
  }
  
  nvi = 0;
  nai = 0;
  tot = 0;
  ioff = idx_type == 1 ? 8 : AVI->movi_start+4;

  for(i=0;i<AVI->n_idx;i++)
    {
      if(strncasecmp(AVI->idx[i],AVI->video_tag,3) == 0)
	{
	  AVI->video_index[nvi].pos = str2ulong(AVI->idx[i]+ 8)+ioff;
	  AVI->video_index[nvi].len = str2ulong(AVI->idx[i]+12);
	  nvi++;
	}
      if(strncasecmp(AVI->idx[i],AVI->audio_tag,4) == 0)
	{
	  AVI->audio_index[nai].pos = str2ulong(AVI->idx[i]+ 8)+ioff;
	  AVI->audio_index[nai].len = str2ulong(AVI->idx[i]+12);
	  AVI->audio_index[nai].tot = tot;
	  tot += AVI->audio_index[nai].len;
	  nai++;
	}
    }

  AVI->audio_bytes = tot;

  /* Reposition the file */

  this->input->seek(this->input, AVI->movi_start, SEEK_SET);
  AVI->video_posf = 0;
  AVI->video_posb = 0;

  return AVI;
}

static long AVI_frame_size(avi_t *AVI, long frame)
{
  if(!AVI->video_index)         { AVI->AVI_errno = AVI_ERR_NO_IDX;   return -1; }

  if(frame < 0 || frame >= AVI->video_frames) return 0;
  return(AVI->video_index[frame].len);
}

static void AVI_seek_start(avi_t *AVI)
{
  AVI->video_posf = 0;
  AVI->video_posb = 0;
}

static int AVI_set_video_position(avi_t *AVI, long frame)
{
  if(!AVI->video_index)         { AVI->AVI_errno = AVI_ERR_NO_IDX;   return -1; }

  if (frame < 0 ) frame = 0;
  AVI->video_posf = frame;
  AVI->video_posb = 0;
  return 0;
}
      
static int AVI_set_audio_position(avi_t *AVI, long byte)
{
  long n0, n1, n;

  if(!AVI->audio_index)         { AVI->AVI_errno = AVI_ERR_NO_IDX;   return -1; }

  if(byte < 0) byte = 0;

  /* Binary search in the audio chunks */

  n0 = 0;
  n1 = AVI->audio_chunks;

  while(n0<n1-1)
    {
      n = (n0+n1)/2;
      if(AVI->audio_index[n].tot>byte)
	n1 = n;
      else
	n0 = n;
    }

  AVI->audio_posc = n0;
  AVI->audio_posb = byte - AVI->audio_index[n0].tot;

  return 0;
}

static long AVI_read_audio(demux_avi_t *this, avi_t *AVI, char *audbuf, 
			   long bytes, int *bFrameDone)
{
  long nr, pos, left, todo;

  if(!AVI->audio_index)         { AVI->AVI_errno = AVI_ERR_NO_IDX;   return -1; }

  nr = 0; /* total number of bytes read */

  /* printf ("avi audio package len: %d\n", AVI->audio_index[AVI->audio_posc].len); */


  while(bytes>0)
    {
      left = AVI->audio_index[AVI->audio_posc].len - AVI->audio_posb;
      if(left==0)
	{
	  AVI->audio_posc++;
	  AVI->audio_posb = 0;
	  if (nr>0) {
	    *bFrameDone = 1;
	    return nr;
	  }
	  left = AVI->audio_index[AVI->audio_posc].len - AVI->audio_posb;
	}
      if(bytes<left)
	todo = bytes;
      else
	todo = left;
      pos = AVI->audio_index[AVI->audio_posc].pos + AVI->audio_posb;
      /* printf ("demux_avi: read audio from %d\n", pos); */
      if (this->input->seek (this->input, pos, SEEK_SET)<0)
	return -1;
      if (this->input->read(this->input, audbuf+nr,todo) != todo)
	{
	  AVI->AVI_errno = AVI_ERR_READ;
	  *bFrameDone = 0;
	  return -1;
	}
      bytes -= todo;
      nr    += todo;
      AVI->audio_posb += todo;
    }

  left = AVI->audio_index[AVI->audio_posc].len - AVI->audio_posb;
  *bFrameDone = (left==0);

  return nr;
}

static long AVI_read_video(demux_avi_t *this, avi_t *AVI, char *vidbuf, 
			   long bytes, int *bFrameDone)
{
  long nr, pos, left, todo;

  if(!AVI->video_index)         { AVI->AVI_errno = AVI_ERR_NO_IDX;   return -1; }

  nr = 0; /* total number of bytes read */

  while(bytes>0)
    {
      left = AVI->video_index[AVI->video_posf].len - AVI->video_posb;
      if(left==0)
	{
	  AVI->video_posf++;
	  AVI->video_posb = 0;
	  if (nr>0) {
	    *bFrameDone = 1;
	    return nr;
	  }
	  left = AVI->video_index[AVI->video_posf].len - AVI->video_posb;
	}
      if(bytes<left)
	todo = bytes;
      else
	todo = left;
      pos = AVI->video_index[AVI->video_posf].pos + AVI->video_posb;
      /* printf ("demux_avi: read video from %d\n", pos); */
      if (this->input->seek (this->input, pos, SEEK_SET)<0) 
	return -1;
      if (this->input->read(this->input, vidbuf+nr,todo) != todo)
	{
	  AVI->AVI_errno = AVI_ERR_READ;
	  *bFrameDone = 0;
	  return -1;
	}
      bytes -= todo;
      nr    += todo;
      AVI->video_posb += todo;
    }

  left = AVI->video_index[AVI->video_posf].len - AVI->video_posb;
  *bFrameDone = (left==0);
	 
  return nr;
}

static int demux_avi_next (demux_avi_t *this) {

  buf_element_t *buf;

  if (this->avi->video_frames <= this->avi->video_posf)
    return 0;

  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

  buf->content = buf->mem;
  buf->DTS  = 0 ; /* FIXME */

  if (this->avi->audio_index[this->avi->audio_posc].pos<
      this->avi->video_index[this->avi->video_posf].pos) {

    /* read audio */
    xprintf (VERBOSE|DEMUX|VAVI, "demux_avi: audio \n");
    /*    pBuf->nPTS  = (uint32_t) (90000.0 * (this->avi->audio_index[this->avi->audio_posc].tot + this->avi->audio_posb) / this->nAvgBytesPerSec)  ;  */

    buf->size      = AVI_read_audio (this, this->avi, buf->mem, 2048, &buf->frame_end);
    buf->PTS       = 0;
    buf->input_pos = this->input->seek (this->input, 0, SEEK_CUR);

    switch (this->avi->a_fmt) {
    case 0x01:
      buf->type     = BUF_AUDIO_LPCM;
      break;
    case 0x2000:
      buf->type     = BUF_AUDIO_AC3;
      break;
    case 0x50:
    case 0x55:
      buf->type     = BUF_AUDIO_MPEG;
      break;
    case 0x161:
      buf->type     = BUF_AUDIO_AVI;
      break;
    default:
      printf ("demux_avi: unknown audio type 0x%lx =>exit\n", this->avi->a_fmt);
      this->status  = DEMUX_FINISHED;
      buf->type     = BUF_AUDIO_MPEG;
      break;
    }

    this->audio_fifo->put (this->audio_fifo, buf);

  } else {
    /* read video */
    xprintf (VERBOSE|DEMUX|VAVI, "demux_avi: video \n");

    buf->PTS = 0;
    /* buf->nPTS  = this->avi->video_posf * this->video_step ;   */
    buf->size = AVI_read_video (this, this->avi, buf->mem, 2048, &buf->frame_end);
    buf->type = BUF_VIDEO_AVI ; 

    this->video_fifo->put (this->video_fifo, buf);
  }

  xprintf (VERBOSE|DEMUX|VAVI, "size : %d\n",buf->size);

  return (buf->size>0);
}

static void *demux_avi_loop (void *this_gen) {

  buf_element_t *buf;
  demux_avi_t *this = (demux_avi_t *) this_gen;

  do {
    if (!demux_avi_next(this))
      this->status = DEMUX_FINISHED;

  } while (this->status == DEMUX_OK) ;

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type    = BUF_CONTROL_END;
  this->video_fifo->put (this->video_fifo, buf);
  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type    = BUF_CONTROL_END;
  this->audio_fifo->put (this->audio_fifo, buf);

  xprintf (VERBOSE|DEMUX, "demux_avi: demux loop finished.\n");

  return NULL;
}

static void demux_avi_stop (demux_plugin_t *this_gen) {
  void        *p;
  demux_avi_t *this = (demux_avi_t *) this_gen;

  this->status = DEMUX_FINISHED;
  
  pthread_join (this->thread, &p);

  AVI_close (this->avi);
  this->avi = NULL;
}

static void demux_avi_close (demux_plugin_t *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;
  free(this);
}

static int demux_avi_get_status (demux_plugin_t *this_gen) {
  demux_avi_t *this = (demux_avi_t *) this_gen;
  return this->status;
}

static void demux_avi_start (demux_plugin_t *this_gen,
			     fifo_buffer_t *bufVideo, 
			     fifo_buffer_t *bufAudio,
			     fifo_buffer_t *bufSPU,
			     off_t pos) 
{
  buf_element_t *buf;
  demux_avi_t *this = (demux_avi_t *) this_gen;

  this->audio_fifo   = bufVideo;
  this->video_fifo   = bufAudio;

  this->status       = DEMUX_OK;

  this->avi = AVI_init(this);

  if (!this->avi) {
    printf ("demux_avi: init failed, avi_errno=%d .\n", this->avi->AVI_errno);
    this->status = DEMUX_FINISHED;
    return;
  }

  printf ("demux_avi: video format = %s, audio format = 0x%lx\n",
	  this->avi->compressor, this->avi->a_fmt);

  AVI_seek_start (this->avi);

  /*
   * seek
   */

  /* seek audio */
  while (this->avi->audio_index[this->avi->audio_posc].pos < pos) {
    this->avi->audio_posc++;
    if (this->avi->audio_posc>this->avi->audio_chunks) {
      this->status = DEMUX_FINISHED;
      return;
    }
  }

  /* seek video */

  /*
  while (this->avi->video_index[this->avi->video_posf].pos < pos) {
    this->avi->video_posf++;
    if (this->avi->video_posf>this->avi->video_frames) {
      this->mnStatus = DEMUX_FINISHED;
      return;
    }
  }
  */
  
  
  this->avi->video_posf = (long) (((double) this->avi->audio_index[this->avi->audio_posc].tot / (double) this->avi->audio_bytes) * (double) this->avi->video_frames);


  /* 
   * send start buffers
   */

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type    = BUF_CONTROL_START;
  this->video_fifo->put (this->video_fifo, buf);

  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type    = BUF_CONTROL_START;
  this->audio_fifo->put (this->audio_fifo, buf);

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->content = buf->mem;
  this->avi->bih.biSize = this->video_step; /* HACK */
  memcpy (buf->content, &this->avi->bih, sizeof (this->avi->bih));
  buf->size = sizeof (this->avi->bih);
  buf->type = BUF_VIDEO_AVI; 
  this->video_fifo->put (this->video_fifo, buf);

  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->content = buf->mem;
  memcpy (buf->content, &this->avi->wavex, 
	  sizeof (this->avi->wavex));
  buf->size = sizeof (this->avi->wavex);
  buf->type = BUF_AUDIO_AVI; 
  this->audio_fifo->put (this->audio_fifo, buf);

  pthread_create (&this->thread, NULL, demux_avi_loop, this) ;
}

static int demux_avi_open(demux_plugin_t *this_gen, 
			  input_plugin_t *input, int stage) {

  demux_avi_t *this = (demux_avi_t *) this_gen;

  switch(stage) {

  case STAGE_BY_CONTENT: {
    uint8_t buf[4096];

    if (input->get_blocksize(input))
      return DEMUX_CANNOT_HANDLE;

    if (!(input->get_capabilities(input) & INPUT_CAP_SEEKABLE))
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 0, SEEK_SET);
    
    if(input->read(input, buf, 4)) {
      
      if((buf[0] == 0x52) 
	 && (buf[1] == 0x49) 
	 && (buf[2] == 0x46) 
	 && (buf[3] == 0x46)) {
	this->input = input;
	this->avi = AVI_init (this);
	if (this->avi)
	  return DEMUX_CAN_HANDLE;
	else {
	  printf ("demux_avi: AVI_init failed.\n");
	  return DEMUX_CANNOT_HANDLE;
	}
      }
      
    }
    return DEMUX_CANNOT_HANDLE;
  }
  break;
  
  case STAGE_BY_EXTENSION: {
    char *ending, *mrl;

    mrl = input->get_mrl (input);
    
    ending = strrchr(mrl, '.');
    xprintf(VERBOSE|DEMUX, "demux_avi_can_handle: ending %s of %s\n", 
	    ending, mrl);
    
    if(ending) {
      if(!strcasecmp(ending, ".avi")) {
	this->input = input;
	this->avi = AVI_init (this);
	if (this->avi)
	  return DEMUX_CAN_HANDLE;
	else {
	  printf ("demux_avi: AVI_init failed.\n");
	  return DEMUX_CANNOT_HANDLE;
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

demux_plugin_t *init_demuxer_plugin(int iface, config_values_t *config) {

  demux_avi_t *this = xmalloc (sizeof (demux_avi_t));

  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  switch (iface) {

  case 1:

    this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
    this->demux_plugin.open              = demux_avi_open;
    this->demux_plugin.start             = demux_avi_start;
    this->demux_plugin.stop              = demux_avi_stop;
    this->demux_plugin.close             = demux_avi_close;
    this->demux_plugin.get_status        = demux_avi_get_status;
    this->demux_plugin.get_identifier    = demux_avi_get_id;

    return (demux_plugin_t *) this;
    break;

  default:
    fprintf(stderr,
	    "Demuxer plugin doesn't support plugin API version %d.\n"
	    "PLUGIN DISABLED.\n"
	    "This means there's a version mismatch between xine and this "
	    "demuxer plugin.\nInstalling current input plugins should help.\n",
	    iface);
    return NULL;
  }
}
