/* 
 * Copyright (C) 2001-2002 the xine project
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
 * $Id: demux_qt.c,v 1.25 2002/04/09 03:38:00 miguelfreitas Exp $
 *
 * demultiplexer for mpeg-4 system (aka quicktime) streams, based on:
 *
 * openquicktime.c
 *
 * This file is part of OpenQuicktime, a free QuickTime library.
 *
 * Based on QT4Linux by Adam Williams.
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
#include <ctype.h>
#include <zlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"
#include "buffer.h"

#define	WINE_TYPEDEFS_ONLY
#include "libw32dll/wine/avifmt.h"
#include "libw32dll/wine/windef.h"
#include "libw32dll/wine/vfw.h"
#include "libw32dll/wine/mmreg.h"

/*
#define LOG
*/

/*
#define DBG_QT
*/

#define VALID_ENDS   "mov,mp4"

/* OpenQuicktime Codec Parameter Types */
#define QUICKTIME_UNKNOWN_PARAMETER         -1
#define QUICKTIME_STRING_PARAMETER           0
#define QUICKTIME_BOOLEAN_PARAMETER          1
#define QUICKTIME_INTEGER_PARAMETER          4
#define QUICKTIME_UNSIGNED_INTEGER_PARAMETER 5
#define QUICKTIME_DOUBLE_PARAMETER           8

#define HEADER_LENGTH 8
#define MAXTRACKS 1024

typedef int64_t longest;
typedef uint64_t ulongest;

#ifdef DBG_QT
int debug_fh;
#endif

/*
typedef __s64 longest;
typedef __u64 ulongest;
*/

typedef struct {
  longest start;      /* byte start in file */
  longest end;        /* byte endpoint in file */
  longest size;       /* byte size for writing */
  int use_64;         /* Use 64 bit header */
  unsigned char type[4];
} quicktime_atom_t;

typedef struct {
  float values[9];
} quicktime_matrix_t;


typedef struct {
  int version;
  long flags;
  unsigned long creation_time;
  unsigned long modification_time;
  int track_id;
  long reserved1;
  long duration;
  char reserved2[8];
  int layer;
  int alternate_group;
  float volume;
  long reserved3;
  quicktime_matrix_t matrix;
  float track_width;
  float track_height;
} quicktime_tkhd_t;


typedef struct {
  long seed;
  long flags;
  long size;
  short int *alpha;
  short int *red;
  short int *green;
  short int *blue;
} quicktime_ctab_t;



/* ===================== sample table ======================== */

/* sample description */

typedef struct {
  int motion_jpeg_quantization_table;
} quicktime_mjqt_t;


typedef struct {
  int motion_jpeg_huffman_table;
} quicktime_mjht_t;


typedef struct {
  char format[4];
  char reserved[6];
  int data_reference;
  
  /* common to audio and video */
  int version;
  int revision;
  char vendor[4];
  
  /* video description */
  long temporal_quality;
  long spatial_quality;
  int width;
  int height;
  float dpi_horizontal;
  float dpi_vertical;
  longest data_size;
  int frames_per_sample;
  char compressor_name[32];
  int depth;
  int ctab_id;
  quicktime_ctab_t ctab;
  float gamma;
  int fields;    /* 0, 1, or 2 */
  int field_dominance;   /* 0 - unknown     1 - top first     2 - bottom first */
  quicktime_mjqt_t mjqt;
  quicktime_mjht_t mjht;
  
  /* audio description */
  int channels;
  int sample_size;
  int compression_id;
  int packet_size;
  float sample_rate;
  
  /* audio description V1 */
  unsigned int samplesPerPacket;
  unsigned int bytesPerPacket;
  unsigned int bytesPerFrames;
  unsigned int bytesPerSample;
  char* private_data;
  unsigned int private_data_size;
} quicktime_stsd_table_t;


typedef struct {
  int version;
  long flags;
  long total_entries;
  quicktime_stsd_table_t *table;
} quicktime_stsd_t;


/* time to sample */
typedef struct {
  long sample_count;
  long sample_duration;
  int64_t pts;
} quicktime_stts_table_t;

typedef struct {
  int version;
  long flags;
  long total_entries;
  quicktime_stts_table_t *table;
} quicktime_stts_t;


/* sync sample */
typedef struct {
  long sample;
} quicktime_stss_table_t;

typedef struct {
  int version;
  long flags;
  long total_entries;
  long entries_allocated;
  quicktime_stss_table_t *table;
} quicktime_stss_t;


/* sample to chunk */
typedef struct {
  long chunk;
  long samples;
  long id;
} quicktime_stsc_table_t;

typedef struct {
  int version;
  long flags;
  long total_entries;
  
  long entries_allocated;
  quicktime_stsc_table_t *table;
} quicktime_stsc_t;


/* sample size */
typedef struct {
  longest size;
} quicktime_stsz_table_t;

typedef struct {
  int version;
  long flags;
  longest sample_size;
  long total_entries;
  
  long entries_allocated;    /* used by the library for allocating a table */
  quicktime_stsz_table_t *table;
} quicktime_stsz_t;


/* chunk offset */
typedef struct {
  longest offset;
} quicktime_stco_table_t;

typedef struct {
  int version;
  long flags;
  long total_entries;
  
  long entries_allocated;    /* used by the library for allocating a table */
  quicktime_stco_table_t *table;
} quicktime_stco_t;


/* sample table */
typedef struct {
  int version;
  long flags;
  quicktime_stsd_t stsd;
  quicktime_stts_t stts;
  quicktime_stss_t stss;
  quicktime_stsc_t stsc;
  quicktime_stsz_t stsz;
  quicktime_stco_t stco;
} quicktime_stbl_t;

/* data reference */

typedef struct {
  longest size;
  char type[4];
  int version;
  long flags;
  char *data_reference;
} quicktime_dref_table_t;

typedef struct {
  int version;
  long flags;
  long total_entries;
  quicktime_dref_table_t *table;
} quicktime_dref_t;

/* data information */

typedef struct {
  quicktime_dref_t dref;
} quicktime_dinf_t;

/* video media header */

typedef struct {
  int version;
  long flags;
  int graphics_mode;
  int opcolor[3];
} quicktime_vmhd_t;


/* sound media header */

typedef struct {
  int version;
  long flags;
  int balance;
  int reserved;
} quicktime_smhd_t;

/* handler reference */

typedef struct {
  int version;
  long flags;
  char component_type[4];
  char component_subtype[4];
  long component_manufacturer;
  long component_flags;
  long component_flag_mask;
  char component_name[256];
} quicktime_hdlr_t;

/* media information */

typedef struct {
  int is_video;
  int is_audio;
  quicktime_vmhd_t vmhd;
  quicktime_smhd_t smhd;
  quicktime_stbl_t stbl;
  quicktime_hdlr_t hdlr;
  quicktime_dinf_t dinf;
} quicktime_minf_t;


/* media header */

typedef struct {
  int version;
  long flags;
  unsigned long creation_time;
  unsigned long modification_time;
  long time_scale;
  long duration;
  int language;
  int quality;
} quicktime_mdhd_t;


/* media */

typedef struct {
  quicktime_mdhd_t mdhd;
  quicktime_minf_t minf;
  quicktime_hdlr_t hdlr;
} quicktime_mdia_t;

/* edit list */
typedef struct
{
  long duration;
  long time;
  float rate;
} quicktime_elst_table_t;

typedef struct
{
  int version;
  long flags;
  long total_entries;

  quicktime_elst_table_t *table;
} quicktime_elst_t;

typedef struct
{
  quicktime_elst_t elst;
} quicktime_edts_t;


typedef struct
{
  quicktime_tkhd_t tkhd;
  quicktime_mdia_t mdia;
  quicktime_edts_t edts;
} quicktime_trak_t;


typedef struct
{
  int version;
  long flags;
  unsigned long creation_time;
  unsigned long modification_time;
  long time_scale;
  long duration;
  float preferred_rate;
  float preferred_volume;
  char reserved[10];
  quicktime_matrix_t matrix;
  long preview_time;
  long preview_duration;
  long poster_time;
  long selection_time;
  long selection_duration;
  long current_time;
  long next_track_id;
} quicktime_mvhd_t;

typedef struct
{
  char *copyright;
  int copyright_len;
  char *name;
  int name_len;
  char *info;
  int info_len;
} quicktime_udta_t;


typedef struct {
  int total_tracks;
  
  quicktime_mvhd_t mvhd;
  quicktime_trak_t *trak[MAXTRACKS];
  quicktime_udta_t udta;
  quicktime_ctab_t ctab;
} quicktime_moov_t;

typedef struct {
  quicktime_atom_t atom;
} quicktime_mdat_t;


/* table of pointers to every track */
typedef struct {
  quicktime_trak_t *track; /* real quicktime track corresponding to this table */
  int channels;            /* number of audio channels in the track */
  long current_position;   /* current sample in output file */
  long current_chunk;      /* current chunk in output file */
  
  void *codec;
} quicktime_audio_map_t;

typedef struct {
  quicktime_trak_t *track;
  long current_position;   /* current frame in output file */
  long current_chunk;      /* current chunk in output file */
  
  /* Array of pointers to frames of raw data when caching frames. */
  /*	unsigned char **frame_cache; */
  /*	long frames_cached; */
  
  void *codec;
} quicktime_video_map_t;

/* file descriptor passed to all routines */

typedef struct quicktime_struc {
  
  int(*quicktime_read_data)(struct quicktime_struc *file, char *data, longest size);
  /* int(*quicktime_write_data)(struct quicktime_struc *file, char *data, int size); */
  int(*quicktime_fseek)(struct quicktime_struc *file, longest offset);
  

  longest total_length;
  quicktime_mdat_t mdat;
  quicktime_moov_t moov;

  /* for begining and ending frame writes where the user wants to write the  */
  /* file descriptor directly */
  longest offset;
  /* I/O */
  /* Current position of virtual file descriptor */
  longest file_position;      
  /* Work around a bug in glibc where ftello returns only 32 bits by maintaining
     our own position */
  longest ftell_position;
  
  
  /* Read ahead buffer */
  longest preload_size;      /* Enables preload when nonzero. */
  char *preload_buffer;
  longest preload_start;     /* Start of preload_buffer in file */
  longest preload_end;       /* End of preload buffer in file */
  longest preload_ptr;       /* Offset of preload_start in preload_buffer */
  
  /* mapping of audio channels to movie tracks */
  /* one audio map entry exists for each channel */
  int total_atracks;
  quicktime_audio_map_t *atracks;
  
  /* mapping of video tracks to movie tracks */
  int total_vtracks;
  quicktime_video_map_t *vtracks;
  
  /* Parameters to handle compressed atoms */
  longest decompressed_buffer_size;
  char *decompressed_buffer;
  longest decompressed_position;

  input_plugin_t *input;
  
} quicktime_t;

typedef struct {
  int64_t           pts;
  off_t             offset;
  int               first_sample;
  int               last_sample;
  int32_t           type;
  quicktime_trak_t *track;
} qt_idx_t;

#define MAX_QT_INDEX 200000

typedef struct demux_qt_s {
  demux_plugin_t        demux_plugin;

  xine_t               *xine;

  config_values_t      *config;

  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;

  input_plugin_t       *input;

  pthread_t             thread;
  pthread_mutex_t       mutex;

  int                   status;
  int                   send_end_buffers;

  quicktime_t          *qt;
  qt_idx_t              index[MAX_QT_INDEX];
  int                   num_index_entries;

  int			has_audio;	 /* 1 if this qt stream has audio */

  int                   video_step; /* in PTS */
  double                audio_factor;
  
  uint32_t              video_type;      /* BUF_VIDEO_xxx type */
  uint32_t              audio_type;      /* BUF_AUDIO_xxx type */

  WAVEFORMATEX          wavex;
  BITMAPINFOHEADER      bih;
  
  uint8_t               scratch[64*1024];

  int                   send_newpts;
} demux_qt_t ;


static void check_newpts( demux_qt_t *this, int64_t pts )
{
  if( this->send_newpts && pts ) {
    
    buf_element_t *buf;
  
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type = BUF_CONTROL_NEWPTS;
    buf->disc_off = pts;
    this->video_fifo->put (this->video_fifo, buf);

    if (this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_CONTROL_NEWPTS;
      buf->disc_off = pts;
      this->audio_fifo->put (this->audio_fifo, buf);
    }
    this->send_newpts = 0;
  }
}


/*
 * openquicktime stuff
 */

/* util.c */

/* Disk I/O */

static longest quicktime_ftell(quicktime_t *file) {
  return file->ftell_position;
}

static int quicktime_fseek(quicktime_t *file, longest offset) {
  file->ftell_position = offset;
  if(offset > file->total_length || offset < 0) 
    return 1;
  if (file->input->seek(file->input, file->ftell_position, SEEK_SET)) {
    /*		perror("quicktime_read_data FSEEK"); */
    return 1;
  }
  return 0;
}

/* Read entire buffer from the preload buffer */
static int quicktime_read_preload(quicktime_t *file, char *data, longest size) {
  longest selection_start = file->file_position;
  longest selection_end = file->file_position + size;
  longest fragment_start, fragment_len;

  fragment_start = file->preload_ptr + (selection_start - file->preload_start);
  while(fragment_start < 0) 
    fragment_start += file->preload_size;
  while(fragment_start >= file->preload_size) 
    fragment_start -= file->preload_size;

  // gcc 2.96 fails here
  while (selection_start < selection_end) {
    fragment_len = selection_end - selection_start;
    if (fragment_start + fragment_len > file->preload_size)
      fragment_len = file->preload_size - fragment_start;

    memcpy (data, file->preload_buffer + fragment_start, fragment_len);
    fragment_start += fragment_len;
    data = data + fragment_len;

    if (fragment_start >= file->preload_size) 
      fragment_start = (longest)0;
    selection_start += fragment_len;
  }
  return 0;
}

static int quicktime_read_data(quicktime_t *file, char *data, longest size) {

  int result = 1;

  if (file->decompressed_buffer) {

    if (file->decompressed_position < file->decompressed_buffer_size) {
      memcpy(data, 
	     file->decompressed_buffer+file->decompressed_position,
	     size);
      file->decompressed_position+=size;
      return result;

    } else {

      //printf("Deleting Decompressed buffer\n");
      file->decompressed_position = 0;
      file->decompressed_buffer_size=0;
      free(file->decompressed_buffer);
      file->decompressed_buffer = NULL;
    }
  }


  if (!file->preload_size) {
    //printf("quicktime_read_data 0x%llx\n", file->file_position);
    file->quicktime_fseek(file, file->file_position);
    /* result = fread(data, size, 1, (FILE*)file->stream); */
    result = file->input->read(file->input, data, size);
    file->ftell_position += size;

  } else {

    longest selection_start = file->file_position;
    longest selection_end = file->file_position + size;
    longest fragment_start, fragment_len;

    if(selection_end - selection_start > file->preload_size) {
      /* Size is larger than preload size.  Should never happen. */
      //printf("read data 1\n");
      file->quicktime_fseek(file, file->file_position);
      /* result = fread(data, size, 1, (FILE*)file->stream); */
      result = file->input->read(file->input, data, size);
      file->ftell_position += size;

    } else if (selection_start >= file->preload_start 
	       && selection_start < file->preload_end 
	       && selection_end <= file->preload_end 
	       && selection_end > file->preload_start) {
      /* Entire range is in buffer */
      //printf("read data 2\n");
      quicktime_read_preload(file, data, size);

    } else if (selection_end > file->preload_end 
	       && selection_end - file->preload_size < file->preload_end) {
      /* Range is after buffer */
      /* Move the preload start to within one preload length of the selection_end */
      //printf("read data 3\n");
      while (selection_end - file->preload_start > file->preload_size) {
	fragment_len = selection_end - file->preload_start - file->preload_size;
	if (file->preload_ptr + fragment_len > file->preload_size) 
	  fragment_len = file->preload_size - file->preload_ptr;
	file->preload_start += fragment_len;
	file->preload_ptr += fragment_len;
	if (file->preload_ptr >= file->preload_size) 
	  file->preload_ptr = 0;
      }

      /* Append sequential data after the preload end to the new end */
      fragment_start = file->preload_ptr + file->preload_end - file->preload_start;
      while (fragment_start >= file->preload_size) 
	fragment_start -= file->preload_size;

      while (file->preload_end < selection_end) {
	fragment_len = selection_end - file->preload_end;
	if (fragment_start + fragment_len > file->preload_size) 
	  fragment_len = file->preload_size - fragment_start;
	file->quicktime_fseek(file, file->preload_end);
	/* result = fread(&(file->preload_buffer[fragment_start]), fragment_len, 1, (FILE*)file->stream); */
	result = file->input->read (file->input,
				    &(file->preload_buffer[fragment_start]), 
				    fragment_len);
	file->ftell_position += fragment_len;
	file->preload_end += fragment_len;
	fragment_start += fragment_len;
	if (fragment_start >= file->preload_size) 
	  fragment_start = 0;
      }

      quicktime_read_preload(file, data, size);
    } else {
      //printf("quicktime_read_data 4 selection_start %lld selection_end %lld preload_start %lld\n", selection_start, selection_end, file->preload_start);
      /* Range is before buffer or over a preload_size away from the end of the buffer. */
      /* Replace entire preload buffer with range. */
      file->quicktime_fseek(file, file->file_position);
      /* result = fread(file->preload_buffer, size, 1, (FILE*)file->stream); */
      result = file->input->read(file->input, file->preload_buffer, size);
      file->ftell_position += size;
      file->preload_start = file->file_position;
      file->preload_end = file->file_position + size;
      file->preload_ptr = 0;
      //printf("quicktime_read_data 5\n");
      quicktime_read_preload(file, data, size);
      //printf("quicktime_read_data 6\n");
    }
  }

  //printf("quicktime_read_data 1 %lld %lld\n", file->file_position, size);
  file->file_position += size;
  return result;
}

static longest quicktime_position(quicktime_t *file) { 

  if (file->decompressed_buffer) {
    return file->decompressed_position;
  }

  return file->file_position; 
}

static longest quicktime_byte_position(quicktime_t *file)
{
  return quicktime_position(file);
}


static float quicktime_read_fixed32(quicktime_t *file)
{
  unsigned long a, b, c, d;
  unsigned char data[4];

  file->quicktime_read_data(file, (char*)data, 4);
  a = data[0];
  b = data[1];
  c = data[2];
  d = data[3];
	
  a = (a << 8) + b;
  b = (c << 8) + d;

  if(b)
    return (float)a + (float)b / 65536;
  else
    return a;
}

static float quicktime_read_fixed16(quicktime_t *file)
{
  unsigned char data[2];
	
  file->quicktime_read_data(file, (char*)data, 2);
  //printf("quicktime_read_fixed16 %02x%02x\n", data[0], data[1]);
  if(data[1])
    return (float)data[0] + (float)data[1] / 256;
  else
    return (float)data[0];
}

static unsigned long quicktime_read_uint32(quicktime_t *file)
{
  unsigned long result;
  unsigned long a, b, c, d;
  char data[4];

  file->quicktime_read_data(file, (char*)data, 4);
  a = (unsigned char)data[0];
  b = (unsigned char)data[1];
  c = (unsigned char)data[2];
  d = (unsigned char)data[3];

  result = (a << 24) | (b << 16) | (c << 8) | d;
  return result;
}

static long quicktime_read_int32(quicktime_t *file)
{
  unsigned long result;
  unsigned long a, b, c, d;
  char data[4];

  file->quicktime_read_data(file, (char*)data, 4);
  a = (unsigned char)data[0];
  b = (unsigned char)data[1];
  c = (unsigned char)data[2];
  d = (unsigned char)data[3];

  result = (a << 24) | (b << 16) | (c << 8) | d;
  return (long)result;
}

static longest quicktime_read_int64(quicktime_t *file)
{
  ulongest result, a, b, c, d, e, f, g, h;
  char data[8];

  file->quicktime_read_data(file, (char*)data, 8);
  a = (unsigned char)data[0];
  b = (unsigned char)data[1];
  c = (unsigned char)data[2];
  d = (unsigned char)data[3];
  e = (unsigned char)data[4];
  f = (unsigned char)data[5];
  g = (unsigned char)data[6];
  h = (unsigned char)data[7];

  result = (a << 56) | 
    (b << 48) | 
    (c << 40) | 
    (d << 32) | 
    (e << 24) | 
    (f << 16) | 
    (g << 8) | 
    h;
  return (longest)result;
}


static long quicktime_read_int24(quicktime_t *file)
{
  unsigned long result;
  unsigned long a, b, c;
  char data[4];
	
  file->quicktime_read_data(file, (char*)data, 3);
  a = (unsigned char)data[0];
  b = (unsigned char)data[1];
  c = (unsigned char)data[2];

  result = (a << 16) | (b << 8) | c;
  return (long)result;
}

static int quicktime_read_int16(quicktime_t *file)
{
  unsigned long result;
  unsigned long a, b;
  char data[2];
	
  file->quicktime_read_data(file, (char*)data, 2);
  a = (unsigned char)data[0];
  b = (unsigned char)data[1];

  result = (a << 8) | b;
  return (int)result;
}

static int quicktime_read_char(quicktime_t *file)
{
  char output;
  file->quicktime_read_data(file, &output, 1);
  return output;
}

static void quicktime_read_char32(quicktime_t *file, char *string)
{
  file->quicktime_read_data(file, string, 4);
}

static int quicktime_set_position(quicktime_t *file, longest position) {
  //if(file->wr) printf("quicktime_set_position 0x%llx\n", position);
  if (file->decompressed_buffer)
    file->decompressed_position = position;
  else
    file->file_position = position;

  return 0;
}

static void quicktime_copy_char32(char *output, char *input) {

  *output++ = *input++;
  *output++ = *input++;
  *output++ = *input++;
  *output = *input;
}


static unsigned long quicktime_current_time(void) {

  time_t t;
  time (&t);
  return (t+(66*31536000)+1468800);
}

static int quicktime_match_32(char *input, char *output) {

  if(input[0] == output[0] &&
     input[1] == output[1] &&
     input[2] == output[2] &&
     input[3] == output[3])
    return 1;
  else 
    return 0;
}

static void quicktime_read_pascal(quicktime_t *file, char *data) {

  char len = quicktime_read_char(file);
  file->quicktime_read_data(file, data, len);
  data[(int) len] = 0;
}

/* matrix.c */

static void quicktime_matrix_init(quicktime_matrix_t *matrix) {

  int i;
  for (i = 0; i < 9; i++) 
    matrix->values[i] = 0;

  matrix->values[0] = matrix->values[4] = 1;
  matrix->values[8] = 16384;
}

static void quicktime_matrix_delete(quicktime_matrix_t *matrix) {
}

static void quicktime_read_matrix(quicktime_t *file, 
				  quicktime_matrix_t *matrix) {
  int i = 0;
  for(i = 0; i < 9; i++) {
    matrix->values[i] = quicktime_read_fixed32(file);
  }
}

static int quicktime_get_timescale(float frame_rate) {
  int timescale = 600;
  /* Encode the 29.97, 23.976, 59.94 framerates */
  if(frame_rate - (int)frame_rate != 0) 
    timescale = (int)(frame_rate * 1001 + 0.5);
  else
    if((600 / frame_rate) - (int)(600 / frame_rate) != 0) 
      timescale = (int)(frame_rate * 100 + 0.5);
  /* printf("quicktime_get_timescale %f %d\n", 600.0 / (double)frame_rate, (int)(60
     0.0 / frame_rate)); */
  return timescale;
}

/* mvhd.c */

static int quicktime_mvhd_init(quicktime_mvhd_t *mvhd) {

  int i;

  mvhd->version = 0;
  mvhd->flags = 0;
  mvhd->creation_time = quicktime_current_time();
  mvhd->modification_time = quicktime_current_time();
  mvhd->time_scale = 600;
  mvhd->duration = 0;
  mvhd->preferred_rate = 1.0;
  mvhd->preferred_volume = 0.996094;
  for(i = 0; i < 10; i++) 
    mvhd->reserved[i] = 0;
  quicktime_matrix_init(&(mvhd->matrix));
  mvhd->preview_time = 0;
  mvhd->preview_duration = 0;
  mvhd->poster_time = 0;
  mvhd->selection_time = 0;
  mvhd->selection_duration = 0;
  mvhd->current_time = 0;
  mvhd->next_track_id = 1;
  return 0;
}

static int quicktime_mvhd_delete(quicktime_mvhd_t *mvhd) {
  return 0;
}

static void quicktime_read_mvhd(quicktime_t *file, quicktime_mvhd_t *mvhd) {
  mvhd->version = quicktime_read_char(file);
  mvhd->flags = quicktime_read_int24(file);
  mvhd->creation_time = quicktime_read_int32(file);
  mvhd->modification_time = quicktime_read_int32(file);
  mvhd->time_scale = quicktime_read_int32(file);
  mvhd->duration = quicktime_read_int32(file);
  mvhd->preferred_rate = quicktime_read_fixed32(file);
  mvhd->preferred_volume = quicktime_read_fixed16(file);
  file->quicktime_read_data(file, mvhd->reserved, 10);
  quicktime_read_matrix(file, &(mvhd->matrix));
  mvhd->preview_time = quicktime_read_int32(file);
  mvhd->preview_duration = quicktime_read_int32(file);
  mvhd->poster_time = quicktime_read_int32(file);
  mvhd->selection_time = quicktime_read_int32(file);
  mvhd->selection_duration = quicktime_read_int32(file);
  mvhd->current_time = quicktime_read_int32(file);
  mvhd->next_track_id = quicktime_read_int32(file);
}

static void quicktime_mhvd_init_video(quicktime_t *file, quicktime_mvhd_t *mvhd, float frame_rate)
{
  mvhd->time_scale = quicktime_get_timescale(frame_rate);
}

static int quicktime_atom_reset(quicktime_atom_t *atom)
{
  atom->end = 0;
  atom->type[0] = atom->type[1] = atom->type[2] = atom->type[3] = 0;
  return 0;
}
static int quicktime_atom_is(quicktime_atom_t *atom, char *_type)
{
  unsigned char *type = _type;

  if(atom->type[0] == type[0] &&
     atom->type[1] == type[1] &&
     atom->type[2] == type[2] &&
     atom->type[3] == type[3])
    return 1;
  else
    return 0;
}

static unsigned long quicktime_atom_read_size(char *data)
{
  unsigned long result;
  unsigned long a, b, c, d;
  
  a = (unsigned char)data[0];
  b = (unsigned char)data[1];
  c = (unsigned char)data[2];
  d = (unsigned char)data[3];
  
  result = (a << 24) | (b << 16) | (c << 8) | d;
  
  /*
    extended header is size 1
    if(result < HEADER_LENGTH) result = HEADER_LENGTH;
  */
  return result;
}

static longest quicktime_atom_read_size64(char *data)
{
  ulongest result, a, b, c, d, e, f, g, h;
  
  a = (unsigned char)data[0];
  b = (unsigned char)data[1];
  c = (unsigned char)data[2];
  d = (unsigned char)data[3];
  e = (unsigned char)data[4];
  f = (unsigned char)data[5];
  g = (unsigned char)data[6];
  h = (unsigned char)data[7];
  
  result = (a << 56) | 
    (b << 48) | 
    (c << 40) | 
    (d << 32) | 
    (e << 24) | 
    (f << 16) | 
    (g << 8) | 
    h;
  
  if(result < HEADER_LENGTH) result = HEADER_LENGTH;
  return (longest)result;
}

static int quicktime_atom_read_type(char *data, char *type)
{
  type[0] = data[4];
  type[1] = data[5];
  type[2] = data[6];
  type[3] = data[7];
  
  //printf("%c%c%c%c ", type[0], type[1], type[2], type[3]); 
  /* need this for quicktime_check_sig */
  if(isalpha(type[0] & 0xff) && isalpha(type[1] & 0xff) && isalpha(type[2] & 0xff) && isalpha(type[3] & 0xff))
    return 0;
  else
    return 1;
}

static int quicktime_atom_skip(quicktime_t *file, quicktime_atom_t *atom) {

  if (atom->start == atom->end) 
    atom->end++;

  return quicktime_set_position(file, atom->end);
}


static int quicktime_atom_read_header(quicktime_t *file, quicktime_atom_t *atom)
{
  char header[10];
  int result;
  
  quicktime_atom_reset(atom);
  
  atom->start = quicktime_position(file);

  if (!file->quicktime_read_data(file, header, HEADER_LENGTH)) 
    return 1;

  result = quicktime_atom_read_type(header, atom->type);
  atom->size = quicktime_atom_read_size(header);
  atom->end = atom->start + atom->size;

  //  printf("quicktime_atom_read_header 1 %c%c%c%c start 0x%llx size %lld end 0x%llx ftell 0x%llx 0x%llx\n", 
	     // 	atom->type[0], atom->type[1], atom->type[2], atom->type[3],
	     //  	atom->start, atom->size, atom->end,
	     //  	file->file_position,
	     //  	(longest)FTELL(file->stream));
	

  /* Skip placeholder atom */
  if(quicktime_match_32(atom->type, "wide")) {
    atom->start = quicktime_position(file);
    quicktime_atom_reset(atom);
    if(!file->quicktime_read_data(file, header, HEADER_LENGTH)) 
      return 1;
    result = quicktime_atom_read_type(header, atom->type);
    atom->size -= 8;
    if(atom->size <= 0) {
      /* Wrapper ended.  Get new atom size */
      atom->size = quicktime_atom_read_size(header);
    }
    atom->end = atom->start + atom->size;
  } else {
    /* Get extended size */
    if(atom->size == 1) {
      if(!file->quicktime_read_data(file, header, HEADER_LENGTH)) 
	return 1;

      atom->size = quicktime_atom_read_size64(header);
      atom->end = atom->start + atom->size;
      /*
       * printf("quicktime_atom_read_header 2 %c%c%c%c start 0x%llx size %lld end 0x%llx ftell 0x%llx\n", 
       * 	atom->type[0], atom->type[1], atom->type[2], atom->type[3],
       * 	atom->start, atom->size, atom->end,
       * 	file->file_position);
       */
    }
  }
  
  return result;
}

/* udta.c */

#define DEFAULT_INFO "Made with Quicktime for Linux"

static int quicktime_udta_init(quicktime_udta_t *udta)
{
  udta->copyright = 0;
  udta->copyright_len = 0;
  udta->name = 0;
  udta->name_len = 0;
  
  udta->info = malloc(strlen(DEFAULT_INFO) + 1);
  udta->info_len = strlen(DEFAULT_INFO);
  sprintf(udta->info, DEFAULT_INFO);
  return 0;
}

static int quicktime_udta_delete(quicktime_udta_t *udta)
{  
  if(udta->copyright_len && udta->copyright)
    {
      free(udta->copyright);
    }
  if(udta->name_len && udta->info)
    {
      free(udta->name);
    }
  if(udta->info_len && udta->info)
    {
      free(udta->info);
    }
  
  quicktime_udta_init(udta);
  
  return 0;
}

static int quicktime_read_udta_string(quicktime_t *file, char **string, int *size)
{
  int result;

  if(*size) free(*string);
  *size = quicktime_read_int16(file);  /* Size of string */
  quicktime_read_int16(file);  /* Discard language code */
  *string = malloc(*size + 1);
  result = file->quicktime_read_data(file, *string, *size);
  (*string)[*size] = 0;
  return !result;
}

static int quicktime_read_udta(quicktime_t *file, quicktime_udta_t *udta, quicktime_atom_t *udta_atom)
{
  quicktime_atom_t leaf_atom;
  int result = 0;
  
  do
    {
      quicktime_atom_read_header(file, &leaf_atom);
      
      if(quicktime_atom_is(&leaf_atom, "©cpy"))
	{
	  result += quicktime_read_udta_string(file, &(udta->copyright), &(udta->copyright_len));
	}
      else
	if(quicktime_atom_is(&leaf_atom, "©nam"))
	  {
	    result += quicktime_read_udta_string(file, &(udta->name), &(udta->name_len));
	  }
	else
	  if(quicktime_atom_is(&leaf_atom, "©inf"))
	    {
	      result += quicktime_read_udta_string(file, &(udta->info), &(udta->info_len));
	    }
	  else
	    quicktime_atom_skip(file, &leaf_atom);
    }while(quicktime_position(file) < udta_atom->end);
  
  return result;
}

#if 0
static int quicktime_set_udta_string(char **string, int *size, char *new_string)
{
  if(*size) free(*string);
  *size = strlen(new_string + 1);
  *string = malloc(*size + 1);
  strcpy(*string, new_string);
  return 0;
}
#endif

static int quicktime_ctab_init(quicktime_ctab_t *ctab)
{
  ctab->seed = 0;
  ctab->flags = 0;
  ctab->size = 0;
  ctab->alpha = 0;
  ctab->red = 0;
  ctab->green = 0;
  ctab->blue = 0;
  return 0;
}

static int quicktime_ctab_delete(quicktime_ctab_t *ctab)
{
  if(ctab->alpha) free(ctab->alpha);
  if(ctab->red) free(ctab->red);
  if(ctab->green) free(ctab->green);
  if(ctab->blue) free(ctab->blue);
  return 0;
}

static int quicktime_read_ctab(quicktime_t *file, quicktime_ctab_t *ctab)
{
  long i;
  
  ctab->seed = quicktime_read_int32(file);
  ctab->flags = quicktime_read_int16(file);
  ctab->size = quicktime_read_int16(file) + 1;
  ctab->alpha = malloc(sizeof(int16_t) * ctab->size);
  ctab->red = malloc(sizeof(int16_t) * ctab->size);
  ctab->green = malloc(sizeof(int16_t) * ctab->size);
  ctab->blue = malloc(sizeof(int16_t) * ctab->size);
  
  for(i = 0; i < ctab->size; i++)
    {
      ctab->alpha[i] = quicktime_read_int16(file);
      ctab->red[i] = quicktime_read_int16(file);
      ctab->green[i] = quicktime_read_int16(file);
      ctab->blue[i] = quicktime_read_int16(file);
    }
  
  return 0;
}

/* tkhd.c */

static int quicktime_tkhd_init(quicktime_tkhd_t *tkhd)
{
  int i;
  tkhd->version = 0;
  tkhd->flags = 15;
  tkhd->creation_time = quicktime_current_time();
  tkhd->modification_time = quicktime_current_time();
  tkhd->track_id=0;
  tkhd->reserved1 = 0;
  tkhd->duration = 0;      /* need to set this when closing */
  for(i = 0; i < 8; i++) tkhd->reserved2[i] = 0;
  tkhd->layer = 0;
  tkhd->alternate_group = 0;
  tkhd->volume = 0.996094;
  tkhd->reserved3 = 0;
  quicktime_matrix_init(&(tkhd->matrix));
  tkhd->track_width = 0;
  tkhd->track_height = 0;
  return 0;
}

static int quicktime_tkhd_delete(quicktime_tkhd_t *tkhd)
{
  return 0;
}

static void quicktime_read_tkhd(quicktime_t *file, quicktime_tkhd_t *tkhd)
{
  //printf("quicktime_read_tkhd 1 0x%llx\n", quicktime_position(file));
  tkhd->version = quicktime_read_char(file);
  tkhd->flags = quicktime_read_int24(file);
  tkhd->creation_time = quicktime_read_int32(file);
  tkhd->modification_time = quicktime_read_int32(file);
  tkhd->track_id = quicktime_read_int32(file);
  tkhd->reserved1 = quicktime_read_int32(file);
  tkhd->duration = quicktime_read_int32(file);

  file->quicktime_read_data(file, tkhd->reserved2, 8);

  tkhd->layer = quicktime_read_int16(file);
  tkhd->alternate_group = quicktime_read_int16(file);
  //printf("quicktime_read_tkhd 1 0x%llx\n", quicktime_position(file));
  tkhd->volume = quicktime_read_fixed16(file);
  //printf("quicktime_read_tkhd 2\n");
  tkhd->reserved3 = quicktime_read_int16(file);
  quicktime_read_matrix(file, &(tkhd->matrix));
  tkhd->track_width = quicktime_read_fixed32(file);
  tkhd->track_height = quicktime_read_fixed32(file);
}


static void quicktime_tkhd_init_video(quicktime_t *file, 
			       quicktime_tkhd_t *tkhd, 
			       int frame_w, 
			       int frame_h)
{
  tkhd->track_width = frame_w;
  tkhd->track_height = frame_h;
  tkhd->volume = 0;
}

/* elst.c */


static void quicktime_elst_table_init(quicktime_elst_table_t *table)
{
  table->duration = 0;
  table->time = 0;
  table->rate = 1;
}

static void quicktime_elst_table_delete(quicktime_elst_table_t *table)
{
}

static void quicktime_read_elst_table(quicktime_t *file, quicktime_elst_table_t *table)
{
  table->duration = quicktime_read_int32(file);
  table->time = quicktime_read_int32(file);
  table->rate = quicktime_read_fixed32(file);
}


static void quicktime_elst_init(quicktime_elst_t *elst)
{
  elst->version = 0;
  elst->flags = 0;
  elst->total_entries = 0;
  elst->table = 0;
}

static void quicktime_elst_init_all(quicktime_elst_t *elst)
{
  if(!elst->total_entries)
    {
      elst->total_entries = 1;
      elst->table = (quicktime_elst_table_t*)malloc(sizeof(quicktime_elst_table_t) * elst->total_entries);
      quicktime_elst_table_init(&(elst->table[0]));
    }
}

static void quicktime_elst_delete(quicktime_elst_t *elst)
{
  int i;
  if(elst->total_entries)
    {
      for(i = 0; i < elst->total_entries; i++)
	quicktime_elst_table_delete(&(elst->table[i]));
      free(elst->table);
    }
  elst->total_entries = 0;
}


static void quicktime_read_elst(quicktime_t *file, quicktime_elst_t *elst)
{
  long i;
  /* quicktime_atom_t leaf_atom; */
  
  elst->version = quicktime_read_char(file);
  elst->flags = quicktime_read_int24(file);
  elst->total_entries = quicktime_read_int32(file);
  elst->table = (quicktime_elst_table_t*)calloc(1, sizeof(quicktime_elst_table_t) * elst->total_entries);
  for(i = 0; i < elst->total_entries; i++)
    {
      quicktime_elst_table_init(&(elst->table[i]));
      quicktime_read_elst_table(file, &(elst->table[i]));
    }
}

/* edts.c */

static void quicktime_edts_init(quicktime_edts_t *edts)
{
  quicktime_elst_init(&(edts->elst));
}

static void quicktime_edts_delete(quicktime_edts_t *edts)
{
  quicktime_elst_delete(&(edts->elst));
}

static void quicktime_edts_init_table(quicktime_edts_t *edts)
{
  quicktime_elst_init_all(&(edts->elst));
}

static void quicktime_read_edts(quicktime_t *file, quicktime_edts_t *edts, quicktime_atom_t *edts_atom)
{
  quicktime_atom_t leaf_atom;
  
  do
    {
      quicktime_atom_read_header(file, &leaf_atom);
      //printf("quicktime_read_edts %llx %llx\n", quicktime_position(file), leaf_atom.end);
      if(quicktime_atom_is(&leaf_atom, "elst"))
	{ quicktime_read_elst(file, &(edts->elst)); }
      else
	quicktime_atom_skip(file, &leaf_atom);
    }while(quicktime_position(file) < edts_atom->end);
}

/* mdhd.c */

void quicktime_mdhd_init(quicktime_mdhd_t *mdhd)
{
  mdhd->version = 0;
  mdhd->flags = 0;
  mdhd->creation_time = quicktime_current_time();
  mdhd->modification_time = quicktime_current_time();
  mdhd->time_scale = 0;
  mdhd->duration = 0;
  mdhd->language = 0;
  mdhd->quality = 100;
}

static void quicktime_mdhd_init_video(quicktime_t *file, 
			       quicktime_mdhd_t *mdhd, 
			       int frame_w,
			       int frame_h, 
			       float frame_rate)
{
  mdhd->time_scale = quicktime_get_timescale(frame_rate);
  //printf("quicktime_mdhd_init_video %ld %f\n", mdhd->time_scale, (double)frame_rate);
  mdhd->duration = 0;      /* set this when closing */
}

static void quicktime_mdhd_init_audio(quicktime_t *file, 
			       quicktime_mdhd_t *mdhd, 
			       int channels, 
			       int sample_rate, 
			       int bits, 
			       char *compressor)
{
  mdhd->time_scale = sample_rate;
  mdhd->duration = 0;      /* set this when closing */
}

static void quicktime_mdhd_delete(quicktime_mdhd_t *mdhd)
{
}

static void quicktime_read_mdhd(quicktime_t *file, quicktime_mdhd_t *mdhd)
{
  mdhd->version = quicktime_read_char(file);
  mdhd->flags = quicktime_read_int24(file);
  mdhd->creation_time = quicktime_read_int32(file);
  mdhd->modification_time = quicktime_read_int32(file);
  mdhd->time_scale = quicktime_read_int32(file);
  mdhd->duration = quicktime_read_int32(file);
  mdhd->language = quicktime_read_int16(file);
  mdhd->quality = quicktime_read_int16(file);
}

/* hdlr.c */

static void quicktime_hdlr_init(quicktime_hdlr_t *hdlr)
{
  hdlr->version = 0;
  hdlr->flags = 0;
  hdlr->component_type[0] = 'm';
  hdlr->component_type[1] = 'h';
  hdlr->component_type[2] = 'l';
  hdlr->component_type[3] = 'r';
  hdlr->component_subtype[0] = 'v';
  hdlr->component_subtype[1] = 'i';
  hdlr->component_subtype[2] = 'd';
  hdlr->component_subtype[3] = 'e';
  hdlr->component_manufacturer = 0;
  hdlr->component_flags = 0;
  hdlr->component_flag_mask = 0;
  strcpy(hdlr->component_name, "Linux Media Handler");
}

static void quicktime_hdlr_init_video(quicktime_hdlr_t *hdlr)
{
  hdlr->component_subtype[0] = 'v';
  hdlr->component_subtype[1] = 'i';
  hdlr->component_subtype[2] = 'd';
  hdlr->component_subtype[3] = 'e';
  strcpy(hdlr->component_name, "Linux Video Media Handler");
}

static void quicktime_hdlr_init_audio(quicktime_hdlr_t *hdlr)
{
  hdlr->component_subtype[0] = 's';
  hdlr->component_subtype[1] = 'o';
  hdlr->component_subtype[2] = 'u';
  hdlr->component_subtype[3] = 'n';
  strcpy(hdlr->component_name, "Linux Sound Media Handler");
}

static void quicktime_hdlr_init_data(quicktime_hdlr_t *hdlr)
{
  hdlr->component_type[0] = 'd';
  hdlr->component_type[1] = 'h';
  hdlr->component_type[2] = 'l';
  hdlr->component_type[3] = 'r';
  hdlr->component_subtype[0] = 'a';
  hdlr->component_subtype[1] = 'l';
  hdlr->component_subtype[2] = 'i';
  hdlr->component_subtype[3] = 's';
  strcpy(hdlr->component_name, "Linux Alias Data Handler");
}

static void quicktime_hdlr_delete(quicktime_hdlr_t *hdlr)
{
}

static void quicktime_read_hdlr(quicktime_t *file, quicktime_hdlr_t *hdlr)
{
  hdlr->version = quicktime_read_char(file);
  hdlr->flags = quicktime_read_int24(file);
  quicktime_read_char32(file, hdlr->component_type);
  quicktime_read_char32(file, hdlr->component_subtype);
  hdlr->component_manufacturer = quicktime_read_int32(file);
  hdlr->component_flags = quicktime_read_int32(file);
  hdlr->component_flag_mask = quicktime_read_int32(file);
  quicktime_read_pascal(file, hdlr->component_name);
}

/* vmhd.c */

static void quicktime_vmhd_init(quicktime_vmhd_t *vmhd)
{
  vmhd->version = 0;
  vmhd->flags = 1;
  vmhd->graphics_mode = 64;
  vmhd->opcolor[0] = 32768;
  vmhd->opcolor[1] = 32768;
	vmhd->opcolor[2] = 32768;
}

static void quicktime_vmhd_init_video(quicktime_t *file, 
			       quicktime_vmhd_t *vmhd, 
			       int frame_w,
			       int frame_h, 
			       float frame_rate)
{
}

static void quicktime_vmhd_delete(quicktime_vmhd_t *vmhd)
{
}

static void quicktime_read_vmhd(quicktime_t *file, quicktime_vmhd_t *vmhd)
{
  int i;
  vmhd->version = quicktime_read_char(file);
  vmhd->flags = quicktime_read_int24(file);
  vmhd->graphics_mode = quicktime_read_int16(file);
  for(i = 0; i < 3; i++)
    vmhd->opcolor[i] = quicktime_read_int16(file);
}

/* smhd.c */

static void quicktime_smhd_init(quicktime_smhd_t *smhd)
{
  smhd->version = 0;
  smhd->flags = 0;
  smhd->balance = 0;
  smhd->reserved = 0;
}

static void quicktime_smhd_delete(quicktime_smhd_t *smhd)
{
}

static void quicktime_read_smhd(quicktime_t *file, quicktime_smhd_t *smhd)
{
  smhd->version = quicktime_read_char(file);
  smhd->flags = quicktime_read_int24(file);
  smhd->balance = quicktime_read_int16(file);
  smhd->reserved = quicktime_read_int16(file);
}

/* dref.c */

void quicktime_dref_table_init(quicktime_dref_table_t *table)
{
  table->size = 0;
  table->type[0] = 'a';
  table->type[1] = 'l';
  table->type[2] = 'i';
  table->type[3] = 's';
  table->version = 0;
  table->flags = 0x0001;
  table->data_reference = malloc(256);
  table->data_reference[0] = 0;
}

void quicktime_dref_table_delete(quicktime_dref_table_t *table)
{
  if(table->data_reference) free(table->data_reference);
  table->data_reference = 0;
}

void quicktime_read_dref_table(quicktime_t *file, quicktime_dref_table_t *table)
{
  table->size = quicktime_read_int32(file);
  quicktime_read_char32(file, table->type);
  table->version = quicktime_read_char(file);
  table->flags = quicktime_read_int24(file);
  if(table->data_reference) free(table->data_reference);
  
  table->data_reference = malloc(table->size);
  if(table->size > 12)
    file->quicktime_read_data(file, table->data_reference, table->size - 12);
  table->data_reference[table->size - 12] = 0;
}


void quicktime_dref_init(quicktime_dref_t *dref)
{
  dref->version = 0;
  dref->flags = 0;
  dref->total_entries = 0;
  dref->table = 0;
}

void quicktime_dref_init_all(quicktime_dref_t *dref)
{
  if(!dref->total_entries)
    {
      dref->total_entries = 1;
      dref->table = (quicktime_dref_table_t *)malloc(sizeof(quicktime_dref_table_t) * dref->total_entries);
      quicktime_dref_table_init(&(dref->table[0]));
    }
}

void quicktime_dref_delete(quicktime_dref_t *dref)
{
	if(dref->table)
	{
		int i;
		for(i = 0; i < dref->total_entries; i++)
			quicktime_dref_table_delete(&(dref->table[i]));
		free(dref->table);
	}
	dref->total_entries = 0;
}

void quicktime_read_dref(quicktime_t *file, quicktime_dref_t *dref)
{
  long i;
  
  dref->version = quicktime_read_char(file);
  dref->flags = quicktime_read_int24(file);
  dref->total_entries = quicktime_read_int32(file);
  dref->table = (quicktime_dref_table_t*)malloc(sizeof(quicktime_dref_table_t) * dref->total_entries);
  for(i = 0; i < dref->total_entries; i++)
    {
      quicktime_dref_table_init(&(dref->table[i]));
      quicktime_read_dref_table(file, &(dref->table[i]));
    }
}

/* dinf.c */

static void quicktime_dinf_init(quicktime_dinf_t *dinf)
{
  quicktime_dref_init(&(dinf->dref));
}

static void quicktime_dinf_delete(quicktime_dinf_t *dinf)
{
  quicktime_dref_delete(&(dinf->dref));
}

static void quicktime_dinf_init_all(quicktime_dinf_t *dinf)
{
  quicktime_dref_init_all(&(dinf->dref));
}

static void quicktime_read_dinf(quicktime_t *file, quicktime_dinf_t *dinf, quicktime_atom_t *dinf_atom)
{
  quicktime_atom_t leaf_atom;
  
  do
    {
      quicktime_atom_read_header(file, &leaf_atom);
      if(quicktime_atom_is(&leaf_atom, "dref"))
	{ quicktime_read_dref(file, &(dinf->dref)); }
      else
	quicktime_atom_skip(file, &leaf_atom);
    }while(quicktime_position(file) < dinf_atom->end);
}

/* stsdtable.c */

static void quicktime_mjqt_init(quicktime_mjqt_t *mjqt)
{
}

static void quicktime_mjqt_delete(quicktime_mjqt_t *mjqt)
{
}

static void quicktime_mjht_init(quicktime_mjht_t *mjht)
{
}

static void quicktime_mjht_delete(quicktime_mjht_t *mjht)
{
}

static void quicktime_read_stsd_audio(quicktime_t *file, quicktime_stsd_table_t *table, quicktime_atom_t *parent_atom)
{
  quicktime_atom_t atom;

  table->version = quicktime_read_int16(file);
  table->revision = quicktime_read_int16(file);
  file->quicktime_read_data(file, table->vendor, 4);
  table->channels = quicktime_read_int16(file);
  table->sample_size = quicktime_read_int16(file);
  table->compression_id = quicktime_read_int16(file);
  table->packet_size = quicktime_read_int16(file);
  table->sample_rate = quicktime_read_fixed32(file);
  if(table->compression_id == 65534)
    { /* Version 1 */
      table->samplesPerPacket = quicktime_read_fixed32(file);
      table->bytesPerPacket = quicktime_read_fixed32(file);
      table->bytesPerFrames = quicktime_read_fixed32(file);
      table->bytesPerSample = quicktime_read_fixed32(file);
      /* Reading the next atom ... could be a wav header */
      quicktime_atom_read_header(file, &atom);
      table->private_data = malloc(atom.size);
      /* printf("%d%d%d%d", atom.type[0], atom.type[1], atom.type[2], atom.type[3]); */
      file->quicktime_read_data(file, table->private_data, atom.size); 
      table->private_data_size = atom.size;
    }

  /*
    quicktime_stsd_audio_dump(table);
    printf("%lld %lld %lld", file->offset, file->file_position, file->ftell_position);
  */
}

static void quicktime_read_stsd_video(quicktime_t *file, quicktime_stsd_table_t *table, quicktime_atom_t *parent_atom)
{
  quicktime_atom_t leaf_atom;
  int len;
	
  table->version = quicktime_read_int16(file);
  table->revision = quicktime_read_int16(file);
  file->quicktime_read_data(file, table->vendor, 4);
  table->temporal_quality = quicktime_read_int32(file);
  table->spatial_quality = quicktime_read_int32(file);
  table->width = quicktime_read_int16(file);
  table->height = quicktime_read_int16(file);
  table->dpi_horizontal = quicktime_read_fixed32(file);
  table->dpi_vertical = quicktime_read_fixed32(file);
  table->data_size = quicktime_read_int32(file);
  table->frames_per_sample = quicktime_read_int16(file);
  len = quicktime_read_char(file);
  file->quicktime_read_data(file, table->compressor_name, 31);
  table->depth = quicktime_read_int16(file);
  table->ctab_id = quicktime_read_int16(file);
	
  while(quicktime_position(file) < parent_atom->end)
    {
      quicktime_atom_read_header(file, &leaf_atom);
      /*
      printf("quicktime_read_stsd_video 1 0x%llx 0x%llx 0x%llx\n", leaf_atom.start, leaf_atom.end, quicktime_position(file));
      */
		
      if(quicktime_atom_is(&leaf_atom, "ctab"))
	{
	  quicktime_read_ctab(file, &(table->ctab));
	}
      else
	if(quicktime_atom_is(&leaf_atom, "gama"))
	  {
	    table->gamma = quicktime_read_fixed32(file);
	  }
	else
	  if(quicktime_atom_is(&leaf_atom, "fiel"))
	    {
	      table->fields = quicktime_read_char(file);
	      table->field_dominance = quicktime_read_char(file);
	    }
	  else
	    /* 		if(quicktime_atom_is(&leaf_atom, "mjqt")) */
	    /* 		{ */
	    /* 			quicktime_read_mjqt(file, &(table->mjqt)); */
	    /* 		} */
	    /* 		else */
	    /* 		if(quicktime_atom_is(&leaf_atom, "mjht")) */
	    /* 		{ */
	    /* 			quicktime_read_mjht(file, &(table->mjht)); */
	    /* 		} */
	    /* 		else */
	    quicktime_atom_skip(file, &leaf_atom);
    }
  //printf("quicktime_read_stsd_video 2\n");
}

static void quicktime_read_stsd_table(quicktime_t *file, quicktime_minf_t *minf, quicktime_stsd_table_t *table)
{
  quicktime_atom_t leaf_atom;

  quicktime_atom_read_header(file, &leaf_atom);
	
  table->format[0] = leaf_atom.type[0];
  table->format[1] = leaf_atom.type[1];
  table->format[2] = leaf_atom.type[2];
  table->format[3] = leaf_atom.type[3];
  file->quicktime_read_data(file, table->reserved, 6);
  table->data_reference = quicktime_read_int16(file);

  if(minf->is_audio) quicktime_read_stsd_audio(file, table, &leaf_atom);
  if(minf->is_video) quicktime_read_stsd_video(file, table, &leaf_atom);
}

static void quicktime_stsd_table_init(quicktime_stsd_table_t *table)
{
  int i;
  table->format[0] = 'y';
  table->format[1] = 'u';
  table->format[2] = 'v';
  table->format[3] = '2';
  for(i = 0; i < 6; i++) table->reserved[i] = 0;
  table->data_reference = 1;

  table->version = 0;
  table->revision = 0;
  table->vendor[0] = 'l';
  table->vendor[1] = 'n';
  table->vendor[2] = 'u';
  table->vendor[3] = 'x';

  table->temporal_quality = 100;
  table->spatial_quality = 258;
  table->width = 0;
  table->height = 0;
  table->dpi_horizontal = 72;
  table->dpi_vertical = 72;
  table->data_size = 0;
  table->frames_per_sample = 1;
  for(i = 0; i < 32; i++) table->compressor_name[i] = 0;
  sprintf(table->compressor_name, "Quicktime for Linux");
  table->depth = 24;
  table->ctab_id = 65535;
  quicktime_ctab_init(&(table->ctab));
  table->gamma = 0;
  table->fields = 0;
  table->field_dominance = 1;
  quicktime_mjqt_init(&(table->mjqt));
  quicktime_mjht_init(&(table->mjht));
	
  table->channels = 0;
  table->sample_size = 0;
  table->compression_id = 0;
  table->packet_size = 0;
  table->sample_rate = 0;

  /* For the Version 1 */
  table->private_data = NULL;
  table->private_data_size = 0;
}

static void quicktime_stsd_table_delete(quicktime_stsd_table_t *table)
{
  quicktime_ctab_delete(&(table->ctab));
  quicktime_mjqt_delete(&(table->mjqt));
  quicktime_mjht_delete(&(table->mjht));
}

/* stsd.c */


static void quicktime_stsd_init(quicktime_stsd_t *stsd)
{
  stsd->version = 0;
  stsd->flags = 0;
  stsd->total_entries = 0;
}

static void quicktime_stsd_init_table(quicktime_stsd_t *stsd)
{
  if(!stsd->total_entries)
    {
      stsd->total_entries = 1;
      stsd->table = (quicktime_stsd_table_t*)calloc(1, sizeof(quicktime_stsd_table_t) * stsd->total_entries);
      quicktime_stsd_table_init(&(stsd->table[0]));
    }
}

static void quicktime_stsd_init_video(quicktime_t *file, 
			       quicktime_stsd_t *stsd, 
			       int frame_w,
			       int frame_h, 
			       float frame_rate,
			       char *compression)
{
  quicktime_stsd_table_t *table;
  quicktime_stsd_init_table(stsd);
  //printf("quicktime_stsd_init_video 1\n");
  table = &(stsd->table[0]);
  //printf("quicktime_stsd_init_video 1\n");
  
  quicktime_copy_char32(table->format, compression);
  //printf("quicktime_stsd_init_video 1\n");
  table->width = frame_w;
  //printf("quicktime_stsd_init_video 1\n");
  table->height = frame_h;
  //printf("quicktime_stsd_init_video 1\n");
  table->frames_per_sample = 1;
  //printf("quicktime_stsd_init_video 1\n");
  table->depth = 24;
  //printf("quicktime_stsd_init_video 1\n");
  table->ctab_id = 65535;
  //printf("quicktime_stsd_init_video 2\n");
}

static void quicktime_stsd_init_audio(quicktime_t *file, 
			       quicktime_stsd_t *stsd, 
			       int channels,
			       int sample_rate, 
			       int bits, 
			       char *compressor)
{
  quicktime_stsd_table_t *table;
  quicktime_stsd_init_table(stsd);
  table = &(stsd->table[0]);
	
  quicktime_copy_char32(table->format, compressor);
  table->channels = channels;
  table->sample_size = bits;
  table->sample_rate = sample_rate;
}

static void quicktime_stsd_delete(quicktime_stsd_t *stsd)
{
  int i;
  if(stsd->total_entries)
    {
      for(i = 0; i < stsd->total_entries; i++)
	quicktime_stsd_table_delete(&(stsd->table[i]));
      free(stsd->table);
    }

  stsd->total_entries = 0;
}

static void quicktime_read_stsd(quicktime_t *file, quicktime_minf_t *minf, quicktime_stsd_t *stsd)
{
  long i;
  /* quicktime_atom_t leaf_atom; */

  stsd->version = quicktime_read_char(file);
  stsd->flags = quicktime_read_int24(file);
  stsd->total_entries = quicktime_read_int32(file);
  stsd->table = (quicktime_stsd_table_t*)malloc(sizeof(quicktime_stsd_table_t) * stsd->total_entries);
  for(i = 0; i < stsd->total_entries; i++)
    {
      quicktime_stsd_table_init(&(stsd->table[i]));
      quicktime_read_stsd_table(file, minf, &(stsd->table[i]));
    }
}

/* stts.c */

static void quicktime_stts_init(quicktime_stts_t *stts) {
  stts->version = 0;
  stts->flags = 0;
  stts->total_entries = 0;
}

static void quicktime_stts_init_table(quicktime_stts_t *stts) {
  if(!stts->total_entries) {
    stts->total_entries = 1;
    stts->table = (quicktime_stts_table_t*)malloc(sizeof(quicktime_stts_table_t) * stts->total_entries);
  }
}

static void quicktime_stts_init_video(quicktime_t *file, quicktime_stts_t *stts, int time_scale, float frame_rate) {
  quicktime_stts_table_t *table;
  quicktime_stts_init_table(stts);
  table = &(stts->table[0]);

  table->sample_count = 0;      /* need to set this when closing */
  table->sample_duration = time_scale / frame_rate;
  //printf("quicktime_stts_init_video %ld %f\n", time_scale, (double)frame_rate);
}

static void quicktime_stts_init_audio(quicktime_t *file, quicktime_stts_t *stts, int sample_rate) {
  quicktime_stts_table_t *table;
  quicktime_stts_init_table(stts);
  table = &(stts->table[0]);

  table->sample_count = 0;     /* need to set this when closing */
  table->sample_duration = 1;
}

static void quicktime_stts_delete(quicktime_stts_t *stts) {
  if(stts->total_entries) free(stts->table);
  stts->total_entries = 0;
}

static void quicktime_read_stts(quicktime_t *file, 
				quicktime_stts_t *stts,
				long time_scale) {
  int i;
 
  stts->version = quicktime_read_char(file);
  stts->flags = quicktime_read_int24(file);
  stts->total_entries = quicktime_read_int32(file);

  stts->table = (quicktime_stts_table_t*)malloc(sizeof(quicktime_stts_table_t) * stts->total_entries);
  printf ("demux_qt: reading stts... (time scale is %ld units/sec)\n",
	  time_scale);
  for(i = 0; i < stts->total_entries; i++) {
    stts->table[i].sample_count = quicktime_read_int32(file);
    stts->table[i].sample_duration = quicktime_read_int32(file);
  }
}

/* stsc.c */

static void quicktime_stsc_init(quicktime_stsc_t *stsc) {
  stsc->version = 0;
  stsc->flags = 0;
  stsc->total_entries = 0;
  stsc->entries_allocated = 0;
}

static void quicktime_stsc_init_table(quicktime_t *file, quicktime_stsc_t *stsc) {
  if(!stsc->entries_allocated) {
    stsc->total_entries = 0;
    stsc->entries_allocated = 2000;
    stsc->table = (quicktime_stsc_table_t*)calloc(1, sizeof(quicktime_stsc_table_t) * stsc->entries_allocated);
  }
}

static void quicktime_stsc_init_video(quicktime_t *file, quicktime_stsc_t *stsc) {
  quicktime_stsc_table_t *table;
  quicktime_stsc_init_table(file, stsc);
  table = &(stsc->table[0]);
  table->chunk = 1;
  table->samples = 1;
  table->id = 1;
}

static void quicktime_stsc_init_audio(quicktime_t *file, quicktime_stsc_t *stsc, int sample_rate) {
  quicktime_stsc_table_t *table;
  quicktime_stsc_init_table(file, stsc);
  table = &(stsc->table[0]);
  table->chunk = 1;
  table->samples = 0;         /* set this after completion or after every audio chunk is written */
  table->id = 1;
}

static void quicktime_stsc_delete(quicktime_stsc_t *stsc) {
  if(stsc->total_entries) 
    free(stsc->table);
  stsc->total_entries = 0;
}

static void quicktime_read_stsc(quicktime_t *file, quicktime_stsc_t *stsc)
{
  long i;
  stsc->version = quicktime_read_char(file);
  stsc->flags = quicktime_read_int24(file);
  stsc->total_entries = quicktime_read_int32(file);
	
  stsc->entries_allocated = stsc->total_entries;
  stsc->table = (quicktime_stsc_table_t*)malloc(sizeof(quicktime_stsc_table_t) * stsc->total_entries);
  for(i = 0; i < stsc->total_entries; i++) {
    stsc->table[i].chunk = quicktime_read_int32(file);
    stsc->table[i].samples = quicktime_read_int32(file);
    stsc->table[i].id = quicktime_read_int32(file);
  }
}


static int quicktime_update_stsc(quicktime_stsc_t *stsc, long chunk, long samples) {
  /* long i; */

  if (chunk > stsc->entries_allocated) {
    stsc->entries_allocated = chunk * 2;
    stsc->table =(quicktime_stsc_table_t*)realloc(stsc->table, sizeof(quicktime_stsc_table_t) * stsc->entries_allocated);
  }

  stsc->table[chunk - 1].samples = samples;
  stsc->table[chunk - 1].chunk = chunk;
  stsc->table[chunk - 1].id = 1;
  if (chunk > stsc->total_entries) 
    stsc->total_entries = chunk;
  return 0;
}

/* Optimizing while writing doesn't allow seeks during recording so */
/* entries are created for every chunk and only optimized during */
/* writeout.  Unfortunately there's no way to keep audio synchronized */
/* after overwriting  a recording as the fractional audio chunk in the */
/* middle always overwrites the previous location of a larger chunk.  On */
/* writing, the table must be optimized.  RealProducer requires an  */
/* optimized table. */

/* stsz.c */

static void quicktime_stsz_init(quicktime_stsz_t *stsz) {
  stsz->version = 0;
  stsz->flags = 0;
  stsz->sample_size = 0;
  stsz->total_entries = 0;
  stsz->entries_allocated = 0;
}

static void quicktime_stsz_init_video(quicktime_t *file, quicktime_stsz_t *stsz) {
  stsz->sample_size = 0;
  if(!stsz->entries_allocated) {
    stsz->entries_allocated = 2000;
    stsz->total_entries = 0;
    stsz->table = (quicktime_stsz_table_t*)malloc(sizeof(quicktime_stsz_table_t) * stsz->entries_allocated);
  }
}

static void quicktime_stsz_init_audio(quicktime_t *file, quicktime_stsz_t *stsz, int channels, int bits) {
  /*stsz->sample_size = channels * bits / 8; */
  stsz->sample_size = 1;   /* ? */
  stsz->total_entries = 0;   /* set this when closing */
  stsz->entries_allocated = 0;
}

static void quicktime_stsz_delete(quicktime_stsz_t *stsz) {
  if(!stsz->sample_size && stsz->total_entries) 
    free(stsz->table);
  stsz->total_entries = 0;
  stsz->entries_allocated = 0;
}

static void quicktime_read_stsz(quicktime_t *file, quicktime_stsz_t *stsz) {
  long i;
  stsz->version = quicktime_read_char(file);
  stsz->flags = quicktime_read_int24(file);
  stsz->sample_size = quicktime_read_int32(file);
  stsz->total_entries = quicktime_read_int32(file);
  stsz->entries_allocated = stsz->total_entries;
  if(!stsz->sample_size) {
    stsz->table = (quicktime_stsz_table_t*)malloc(sizeof(quicktime_stsz_table_t) * stsz->entries_allocated);
    for(i = 0; i < stsz->total_entries; i++) {
      stsz->table[i].size = quicktime_read_int32(file);
    }
  }
}

static void quicktime_update_stsz(quicktime_stsz_t *stsz, long sample, long sample_size)
{
  /* long i; */

  if(!stsz->sample_size)
    {
      if(sample >= stsz->entries_allocated)
	{
	  stsz->entries_allocated = sample * 2;
	  stsz->table = (quicktime_stsz_table_t*)realloc(stsz->table, sizeof(quicktime_stsz_table_t) * stsz->entries_allocated);
	}

      //		printf("sample %ld sample_size %ld\n", sample, sample_size);
      stsz->table[sample].size = sample_size;
      if(sample >= stsz->total_entries) stsz->total_entries = sample + 1;
    }
}

/* stco.c */

static void quicktime_stco_init(quicktime_stco_t *stco)
{
  stco->version = 0;
  stco->flags = 0;
  stco->total_entries = 0;
  stco->entries_allocated = 0;
}

static void quicktime_stco_delete(quicktime_stco_t *stco)
{
  if(stco->total_entries) free(stco->table);
  stco->total_entries = 0;
  stco->entries_allocated = 0;
}

static void quicktime_stco_init_common(quicktime_t *file, quicktime_stco_t *stco)
{
  if(!stco->entries_allocated)
    {
      stco->entries_allocated = 2000;
      stco->total_entries = 0;
      stco->table = (quicktime_stco_table_t*)malloc(sizeof(quicktime_stco_table_t) * stco->entries_allocated);
      /*printf("quicktime_stco_init_common %x\n", stco->table); */
    }
}

static void quicktime_read_stco(quicktime_t *file, quicktime_stco_t *stco)
{
  long i;
  stco->version = quicktime_read_char(file);
  stco->flags = quicktime_read_int24(file);
  stco->total_entries = quicktime_read_int32(file);
  stco->entries_allocated = stco->total_entries;
  stco->table = (quicktime_stco_table_t*)calloc(1, sizeof(quicktime_stco_table_t) * stco->entries_allocated);
  for(i = 0; i < stco->total_entries; i++)
    {
      stco->table[i].offset = quicktime_read_uint32(file);
    }
}

static void quicktime_read_stco64(quicktime_t *file, quicktime_stco_t *stco)
{
  long i;
  stco->version = quicktime_read_char(file);
  stco->flags = quicktime_read_int24(file);
  stco->total_entries = quicktime_read_int32(file);
  stco->entries_allocated = stco->total_entries;
  stco->table = (quicktime_stco_table_t*)calloc(1, sizeof(quicktime_stco_table_t) * stco->entries_allocated);
  for(i = 0; i < stco->total_entries; i++)
    {
      stco->table[i].offset = quicktime_read_int64(file);
    }
}

static void quicktime_update_stco(quicktime_stco_t *stco, long chunk, longest offset)
{
  /* long i; */

  if(chunk > stco->entries_allocated)
    {
      stco->entries_allocated = chunk * 2;
      stco->table = (quicktime_stco_table_t*)realloc(stco->table, sizeof(quicktime_stco_table_t) * stco->entries_allocated);
    }
	
  stco->table[chunk - 1].offset = offset;
  if(chunk > stco->total_entries) stco->total_entries = chunk;
}

/* stss.c */

static void quicktime_stss_init(quicktime_stss_t *stss)
{
	stss->version = 0;
	stss->flags = 0;
	stss->total_entries = 0;
}

static void quicktime_stss_delete(quicktime_stss_t *stss)
{
	if(stss->total_entries) free(stss->table);
	stss->total_entries = 0;
}

static void quicktime_read_stss(quicktime_t *file, quicktime_stss_t *stss)
{
	long i;
	stss->version = quicktime_read_char(file);
	stss->flags = quicktime_read_int24(file);
	stss->total_entries = quicktime_read_int32(file);
	
	stss->table = (quicktime_stss_table_t*)malloc(sizeof(quicktime_stss_table_t) * stss->total_entries);
	for(i = 0; i < stss->total_entries; i++)
	{
		stss->table[i].sample = quicktime_read_int32(file);
	}
}



/* stbl.c */

static void quicktime_stbl_init(quicktime_stbl_t *stbl)
{
  stbl->version = 0;
  stbl->flags = 0;
  quicktime_stsd_init(&(stbl->stsd));
  quicktime_stts_init(&(stbl->stts));
  quicktime_stss_init(&(stbl->stss));
  quicktime_stsc_init(&(stbl->stsc));
  quicktime_stsz_init(&(stbl->stsz));
  quicktime_stco_init(&(stbl->stco));
}

static void quicktime_stbl_init_video(quicktime_t *file, 
				      quicktime_stbl_t *stbl, 
				      int frame_w,
				      int frame_h, 
				      int time_scale, 
				      float frame_rate,
				      char *compressor)
{
  //printf("quicktime_stbl_init_video 1\n");
  quicktime_stsd_init_video(file, &(stbl->stsd), frame_w, frame_h, frame_rate, compressor);
  //printf("quicktime_stbl_init_video 1 %d %f\n", time_scale, (double)frame_rate);
  quicktime_stts_init_video(file, &(stbl->stts), time_scale, frame_rate);
  //printf("quicktime_stbl_init_video 1\n");
  quicktime_stsc_init_video(file, &(stbl->stsc));
  //printf("quicktime_stbl_init_video 1\n");
  quicktime_stsz_init_video(file, &(stbl->stsz));
  //printf("quicktime_stbl_init_video 1\n");
  quicktime_stco_init_common(file, &(stbl->stco));
  //printf("quicktime_stbl_init_video 2\n");
}


static void quicktime_stbl_init_audio(quicktime_t *file, 
			       quicktime_stbl_t *stbl, 
			       int channels, 
			       int sample_rate, 
			       int bits, 
			       char *compressor)
{
  quicktime_stsd_init_audio(file, &(stbl->stsd), channels, sample_rate, bits, compressor);
  quicktime_stts_init_audio(file, &(stbl->stts), sample_rate);
  quicktime_stsc_init_audio(file, &(stbl->stsc), sample_rate);
  quicktime_stsz_init_audio(file, &(stbl->stsz), channels, bits);
  quicktime_stco_init_common(file, &(stbl->stco));
}

static void quicktime_stbl_delete(quicktime_stbl_t *stbl)
{
  quicktime_stsd_delete(&(stbl->stsd));
  quicktime_stts_delete(&(stbl->stts));
  quicktime_stss_delete(&(stbl->stss));
  quicktime_stsc_delete(&(stbl->stsc));
  quicktime_stsz_delete(&(stbl->stsz));
  quicktime_stco_delete(&(stbl->stco));
}

static int quicktime_read_stbl(quicktime_t *file, quicktime_minf_t *minf, long time_scale,
			       quicktime_stbl_t *stbl, quicktime_atom_t *parent_atom) {
  quicktime_atom_t leaf_atom;
  
  do {
    quicktime_atom_read_header(file, &leaf_atom);
      
    //printf("quicktime_read_stbl 1\n");
    /* mandatory */
    if(quicktime_atom_is(&leaf_atom, "stsd")) { 
      //printf("STSD start %lld end %lld", leaf_atom.start, leaf_atom.end);
      quicktime_read_stsd(file, minf, &(stbl->stsd)); 
      /* Some codecs store extra information at the end of this */
      quicktime_atom_skip(file, &leaf_atom);
    } else if(quicktime_atom_is(&leaf_atom, "stts")) { 
      quicktime_read_stts(file, &(stbl->stts), time_scale); 
    } else if(quicktime_atom_is(&leaf_atom, "stss")) { 
      quicktime_read_stss(file, &(stbl->stss)); 
    } else if(quicktime_atom_is(&leaf_atom, "stsc")) { 
      quicktime_read_stsc(file, &(stbl->stsc)); 
    } else if(quicktime_atom_is(&leaf_atom, "stsz")) { 
      quicktime_read_stsz(file, &(stbl->stsz)); 
    } else if(quicktime_atom_is(&leaf_atom, "co64")) { 
      quicktime_read_stco64(file, &(stbl->stco)); 
    } else if(quicktime_atom_is(&leaf_atom, "stco")) { 
      quicktime_read_stco(file, &(stbl->stco)); 
    } else
      quicktime_atom_skip(file, &leaf_atom);
  } while (quicktime_position(file) < parent_atom->end);
  
  return 0;
}


/* minf.c */

static void quicktime_minf_init(quicktime_minf_t *minf)
{
  minf->is_video = minf->is_audio = 0;
  quicktime_vmhd_init(&(minf->vmhd));
  quicktime_smhd_init(&(minf->smhd));
  quicktime_hdlr_init(&(minf->hdlr));
  quicktime_dinf_init(&(minf->dinf));
  quicktime_stbl_init(&(minf->stbl));
}

static void quicktime_minf_init_video(quicktime_t *file, 
			       quicktime_minf_t *minf, 
			       int frame_w,
			       int frame_h, 
			       int time_scale, 
			       float frame_rate,
			       char *compressor)
{
  minf->is_video = 1;
  //printf("quicktime_minf_init_video 1\n");
  quicktime_vmhd_init_video(file, &(minf->vmhd), frame_w, frame_h, frame_rate);
  //printf("quicktime_minf_init_video 1 %d %f\n", time_scale, (double)frame_rate);
  quicktime_stbl_init_video(file, &(minf->stbl), frame_w, frame_h, time_scale, frame_rate, compressor);
  //printf("quicktime_minf_init_video 2\n");
  quicktime_hdlr_init_data(&(minf->hdlr));
  //printf("quicktime_minf_init_video 1\n");
  quicktime_dinf_init_all(&(minf->dinf));
  //printf("quicktime_minf_init_video 2\n");
}

static void quicktime_minf_init_audio(quicktime_t *file, 
			       quicktime_minf_t *minf, 
			       int channels, 
			       int sample_rate, 
			       int bits, 
			       char *compressor)
{
  minf->is_audio = 1;
  /* smhd doesn't store anything worth initializing */
  quicktime_stbl_init_audio(file, &(minf->stbl), channels, sample_rate, bits, compressor);
  quicktime_hdlr_init_data(&(minf->hdlr));
  quicktime_dinf_init_all(&(minf->dinf));
}

static void quicktime_minf_delete(quicktime_minf_t *minf)
{
  quicktime_vmhd_delete(&(minf->vmhd));
  quicktime_smhd_delete(&(minf->smhd));
  quicktime_dinf_delete(&(minf->dinf));
  quicktime_stbl_delete(&(minf->stbl));
  quicktime_hdlr_delete(&(minf->hdlr));
}

static int quicktime_read_minf(quicktime_t *file, quicktime_minf_t *minf, 
			       long time_scale,
			       quicktime_atom_t *parent_atom) {
  quicktime_atom_t leaf_atom;
  
  do {
    quicktime_atom_read_header(file, &leaf_atom);
    //printf("quicktime_read_minf 1\n");
    
    /* mandatory */
    if (quicktime_atom_is(&leaf_atom, "vmhd")) { 
      minf->is_video = 1; quicktime_read_vmhd(file, &(minf->vmhd)); 
    } else if (quicktime_atom_is(&leaf_atom, "smhd")) { 
      minf->is_audio = 1; quicktime_read_smhd(file, &(minf->smhd)); 
    } else if (quicktime_atom_is(&leaf_atom, "hdlr")) { 
      quicktime_read_hdlr(file, &(minf->hdlr)); 
      /* Main Actor doesn't write component name */
      quicktime_atom_skip(file, &leaf_atom);
    } else if (quicktime_atom_is(&leaf_atom, "dinf")) { quicktime_read_dinf(file, &(minf->dinf), &leaf_atom); 
    } else if (quicktime_atom_is(&leaf_atom, "stbl")){ 
      quicktime_read_stbl(file, minf, time_scale, &(minf->stbl), &leaf_atom); 
    } else
      quicktime_atom_skip(file, &leaf_atom);
  } while (quicktime_position(file) < parent_atom->end);
  
  return 0;
}

/* mdia.c */


static void quicktime_mdia_init(quicktime_mdia_t *mdia)
{
  quicktime_mdhd_init(&(mdia->mdhd));
  quicktime_hdlr_init(&(mdia->hdlr));
  quicktime_minf_init(&(mdia->minf));
}

static void quicktime_mdia_init_video(quicktime_t *file, 
			       quicktime_mdia_t *mdia,
			       int frame_w,
			       int frame_h, 
			       float frame_rate,
			       char *compressor)
{
  //printf("quicktime_mdia_init_video 1\n");
  quicktime_mdhd_init_video(file, &(mdia->mdhd), frame_w, frame_h, frame_rate);
  //printf("quicktime_mdia_init_video 1 %d %f\n", mdia->mdhd.time_scale, (double)frame_rate);
  quicktime_minf_init_video(file, &(mdia->minf), frame_w, frame_h, mdia->mdhd.time_scale, frame_rate, compressor);
  //printf("quicktime_mdia_init_video 1\n");
  quicktime_hdlr_init_video(&(mdia->hdlr));
  //printf("quicktime_mdia_init_video 2\n");
}

static void quicktime_mdia_init_audio(quicktime_t *file, 
			       quicktime_mdia_t *mdia, 
			       int channels,
			       int sample_rate, 
			       int bits, 
			       char *compressor)
{
  quicktime_mdhd_init_audio(file, &(mdia->mdhd), channels, sample_rate, bits, compressor);
  quicktime_minf_init_audio(file, &(mdia->minf), channels, sample_rate, bits, compressor);
  quicktime_hdlr_init_audio(&(mdia->hdlr));
}

static void quicktime_mdia_delete(quicktime_mdia_t *mdia)
{
  quicktime_mdhd_delete(&(mdia->mdhd));
  quicktime_hdlr_delete(&(mdia->hdlr));
  quicktime_minf_delete(&(mdia->minf));
}

static int quicktime_read_mdia(quicktime_t *file, quicktime_mdia_t *mdia, 
			       quicktime_atom_t *trak_atom) {

  quicktime_atom_t leaf_atom;
  
  do {
    quicktime_atom_read_header(file, &leaf_atom);
    //printf("quicktime_read_mdia 0x%llx\n", quicktime_position(file));
    
    /* mandatory */
    if(quicktime_atom_is(&leaf_atom, "mdhd")) { 
      quicktime_read_mdhd(file, &(mdia->mdhd)); 
    } else if(quicktime_atom_is(&leaf_atom, "hdlr")) {
      quicktime_read_hdlr(file, &(mdia->hdlr)); 
      /* Main Actor doesn't write component name */
      quicktime_atom_skip(file, &leaf_atom);
    } else if(quicktime_atom_is(&leaf_atom, "minf")) { 
      quicktime_read_minf(file, &(mdia->minf), mdia->mdhd.time_scale, &leaf_atom); 
    } else
      quicktime_atom_skip(file, &leaf_atom);
  } while (quicktime_position(file) < trak_atom->end);
  
  
  return 0;
}


/* trak.c */


static int quicktime_trak_init(quicktime_trak_t *trak)
{
  quicktime_tkhd_init(&(trak->tkhd));
  quicktime_edts_init(&(trak->edts));
  quicktime_mdia_init(&(trak->mdia));
  return 0;
}

static int quicktime_trak_init_video(quicktime_t *file, 
			      quicktime_trak_t *trak, 
			      int frame_w, 
			      int frame_h, 
			      float frame_rate,
			      char *compressor)
{
  //printf("quicktime_trak_init_video 1\n");
  quicktime_tkhd_init_video(file, 
			    &(trak->tkhd), 
			    frame_w, 
			    frame_h);
  //printf("quicktime_trak_init_video 1\n");
  quicktime_mdia_init_video(file, 
			    &(trak->mdia), 
			    frame_w, 
			    frame_h, 
			    frame_rate, 
			    compressor);
  //printf("quicktime_trak_init_video 2\n");
  quicktime_edts_init_table(&(trak->edts));
  //printf("quicktime_trak_init_video 2\n");
  
  return 0;
}

static int quicktime_trak_init_audio(quicktime_t *file, 
			      quicktime_trak_t *trak, 
			      int channels, 
			      int sample_rate, 
			      int bits, 
			      char *compressor)
{
  quicktime_mdia_init_audio(file, &(trak->mdia), channels, sample_rate, bits, compressor);
  quicktime_edts_init_table(&(trak->edts));
  
  return 0;
}

static int quicktime_trak_delete(quicktime_trak_t *trak)
{
  quicktime_tkhd_delete(&(trak->tkhd));
  return 0;
}


static quicktime_trak_t* quicktime_add_trak(quicktime_moov_t *moov)
{
  if(moov->total_tracks < MAXTRACKS)
    {
      moov->trak[moov->total_tracks] = malloc(sizeof(quicktime_trak_t));
      quicktime_trak_init(moov->trak[moov->total_tracks]);
      moov->total_tracks++;
    }
  return moov->trak[moov->total_tracks - 1];
}

static int quicktime_delete_trak(quicktime_moov_t *moov)
{
  if(moov->total_tracks)
    {
      moov->total_tracks--;
      quicktime_trak_delete(moov->trak[moov->total_tracks]);
      free(moov->trak[moov->total_tracks]);
    }
  return 0;
}


static int quicktime_read_trak(quicktime_t *file, quicktime_trak_t *trak, quicktime_atom_t *trak_atom)
{
  quicktime_atom_t leaf_atom;

  do {			  
    quicktime_atom_read_header(file, &leaf_atom);

    /*
    printf ("demux_qt: found trak atom, type >%c %c %c %c<\n",
	    leaf_atom.type[0],leaf_atom.type[1],leaf_atom.type[2],leaf_atom.type[3]);
    */
    
    /* mandatory */
    
    if(quicktime_atom_is(&leaf_atom, "tkhd")) { 
      quicktime_read_tkhd(file, &(trak->tkhd)); 
    } else if (quicktime_atom_is(&leaf_atom, "mdia")) {  
      quicktime_read_mdia(file, &(trak->mdia), &leaf_atom); 
    } else { /* optional */
      if(quicktime_atom_is(&leaf_atom, "clip")) { 
	quicktime_atom_skip(file, &leaf_atom); 
      } else if(quicktime_atom_is(&leaf_atom, "matt")) { 
	quicktime_atom_skip(file, &leaf_atom); 
      } else if(quicktime_atom_is(&leaf_atom, "edts")) {
	quicktime_read_edts(file, &(trak->edts), &leaf_atom); 
      } else if (quicktime_atom_is(&leaf_atom, "load")) {
	quicktime_atom_skip(file, &leaf_atom); 
      } else if(quicktime_atom_is(&leaf_atom, "tref")) {
	quicktime_atom_skip(file, &leaf_atom); 
      } else if(quicktime_atom_is(&leaf_atom, "imap")) {
	quicktime_atom_skip(file, &leaf_atom); 
      } else if(quicktime_atom_is(&leaf_atom, "udta")){
	quicktime_atom_skip(file, &leaf_atom); 
      } else {
	/* printf ("skipping this atom\n"); */
	quicktime_atom_skip(file, &leaf_atom);
	/* printf("quicktime_read_trak 0x%llx 0x%llx\n", quicktime_position(file), leaf_atom.end); */
      }
    }
  } while(quicktime_position(file) < trak_atom->end);

  return 0;
}

#if 0
static longest quicktime_track_end(quicktime_trak_t *trak)
{
/* get the byte endpoint of the track in the file */
  longest size = 0;
  longest chunk, chunk_offset, chunk_samples, sample;
  quicktime_stsz_t *stsz = &(trak->mdia.minf.stbl.stsz);
  /* quicktime_stsz_table_t *table = stsz->table; */
  quicktime_stsc_t *stsc = &(trak->mdia.minf.stbl.stsc);
  quicktime_stco_t *stco;
  
  /* get the last chunk offset */
  /* the chunk offsets contain the HEADER_LENGTH themselves */
  stco = &(trak->mdia.minf.stbl.stco);
  chunk = stco->total_entries;
  size = chunk_offset = stco->table[chunk - 1].offset;
  
  /* get the number of samples in the last chunk */
  chunk_samples = stsc->table[stsc->total_entries - 1].samples;
  
  /* get the size of last samples */
  if(stsz->sample_size)
    {
      /* assume audio so calculate the sample size */
      size += chunk_samples * stsz->sample_size
	* trak->mdia.minf.stbl.stsd.table[0].channels 
	* trak->mdia.minf.stbl.stsd.table[0].sample_size / 8;
    }
  else
    {
      /* assume video */
      for(sample = stsz->total_entries - chunk_samples; 
	  sample < stsz->total_entries; sample++)
	{
	  size += stsz->table[sample].size;
	}
    }
  
  return size;
}
#endif

static long quicktime_sample_of_chunk(quicktime_trak_t *trak, long chunk)
{
  quicktime_stsc_table_t *table = trak->mdia.minf.stbl.stsc.table;
  long total_entries = trak->mdia.minf.stbl.stsc.total_entries;
  long chunk1entry, chunk2entry;
  long chunk1, chunk2, chunks, total = 0;
  
  for(chunk1entry = total_entries - 1, chunk2entry = total_entries; 
      chunk1entry >= 0; 
      chunk1entry--, chunk2entry--)
    {
      chunk1 = table[chunk1entry].chunk;
      
      if(chunk > chunk1)
	{
	  if(chunk2entry < total_entries)
	    {
	      chunk2 = table[chunk2entry].chunk;
	      
	      if(chunk < chunk2) chunk2 = chunk;
	    }
	  else
	    chunk2 = chunk;
	  
	  chunks = chunk2 - chunk1;
	  
	  total += chunks * table[chunk1entry].samples;
	}
    }
  
  return total;
}

static long quicktime_track_samples(quicktime_t *file, quicktime_trak_t *trak)
{
  quicktime_stsc_table_t *table = trak->mdia.minf.stbl.stsc.table;
  long total_entries = trak->mdia.minf.stbl.stsc.total_entries;
  long chunk = trak->mdia.minf.stbl.stco.total_entries;
  long sample;
  
  if(chunk)
    {
      sample = quicktime_sample_of_chunk(trak, chunk);
      sample += table[total_entries - 1].samples;
    }
  else 
    sample = 0;
  
  return sample;
}

static int quicktime_chunk_of_sample(longest *chunk_sample, 
				     longest *chunk, 
				     quicktime_trak_t *trak, 
				     long sample)
{
  quicktime_stsc_table_t *table = trak->mdia.minf.stbl.stsc.table;
  long total_entries = trak->mdia.minf.stbl.stsc.total_entries;
  long chunk2entry, i, current_chunk, sample_duration;
  long chunk1, chunk2, chunk1samples, range_samples, total = 0;
  quicktime_stts_t *stts = &(trak->mdia.minf.stbl.stts);

  chunk1 = 1;
  chunk1samples = 0;
  chunk2entry = 0;

  if(!total_entries) {
    *chunk_sample = 0;
    *chunk = 0;
    return 0;
  }

  do
    {
      chunk2 = table[chunk2entry].chunk;
      *chunk = chunk2 - chunk1;
      range_samples = *chunk * chunk1samples;

      if(sample < total + range_samples) break;

      /* Yann: I've modified this to handle samples with duration
	 different from 1 ... needed by ".mp3" fourcc */
		
      if(trak->mdia.minf.is_audio)
	{
	  i = stts->total_entries - 1;
		    
	  do
	    {
	      current_chunk = stts->table[i].sample_count;
	      i--;
	    }while(i >= 0 && current_chunk > chunk2entry);	
		    
	  sample_duration = stts->table[i+1].sample_duration;
	}
      else
	sample_duration = 1; // this way nothing is broken ... I hope
		  
      chunk1samples = table[chunk2entry].samples * sample_duration;
      chunk1 = chunk2;

      if(chunk2entry < total_entries)
	{
	  chunk2entry++;
	  total += range_samples;
	}
    }while(chunk2entry < total_entries);

  if(chunk1samples)
    *chunk = (sample - total) / chunk1samples + chunk1;
  else
    *chunk = 1;

  *chunk_sample = total + (*chunk - chunk1) * chunk1samples;
  return 0;
}

static longest quicktime_chunk_to_offset(quicktime_trak_t *trak, long chunk)
{
  quicktime_stco_table_t *table = trak->mdia.minf.stbl.stco.table;

  if(trak->mdia.minf.stbl.stco.total_entries && chunk > trak->mdia.minf.stbl.stco.total_entries)
    return table[trak->mdia.minf.stbl.stco.total_entries - 1].offset;
  else
    if(trak->mdia.minf.stbl.stco.total_entries)
      return table[chunk - 1].offset;
  return HEADER_LENGTH * 2;
}

static long quicktime_offset_to_chunk(longest *chunk_offset, 
			       quicktime_trak_t *trak, 
			       longest offset)
{
  quicktime_stco_table_t *table = trak->mdia.minf.stbl.stco.table;
  int i;

  for(i = trak->mdia.minf.stbl.stco.total_entries - 1; i >= 0; i--)
    {
      if(table[i].offset <= offset)
	{
	  *chunk_offset = table[i].offset;
	  return i + 1;
	}
    }

  /*  Yann: I really wonder why we should return this  */
  /*  *chunk_offset = HEADER_LENGTH * 2;  */
  /*  I return the first chunk offset instead */
  if(trak->mdia.minf.stbl.stco.total_entries)
    *chunk_offset = table[0].offset;
  else
    /* In this case there is no chunk in the file ... retuning -1 */
    *chunk_offset = -1;
	
  return 1;
}

static longest quicktime_samples_to_bytes(quicktime_trak_t *track, long samples)
{
  /* char *compressor = track->mdia.minf.stbl.stsd.table[0].format; */
  int channels = track->mdia.minf.stbl.stsd.table[0].channels;
  
  /* Default use the sample size specification for TWOS and RAW */
  return samples * channels * track->mdia.minf.stbl.stsd.table[0].sample_size / 8;
}



static longest quicktime_sample_range_size(quicktime_trak_t *trak, 
				    long chunk_sample, 
				    long sample)
{
  /* quicktime_stsz_table_t *table = trak->mdia.minf.stbl.stsz.table; */
  quicktime_stts_t *stts = &(trak->mdia.minf.stbl.stts);
  longest i, total;
  
  if(trak->mdia.minf.stbl.stsz.sample_size)
    {
      /* assume audio */
      return quicktime_samples_to_bytes(trak, sample - chunk_sample);
      /* 		return (sample - chunk_sample) * trak->mdia.minf.stbl.stsz.sample_size  */
      /* 			* trak->mdia.minf.stbl.stsd.table[0].channels  */
      /* 			* trak->mdia.minf.stbl.stsd.table[0].sample_size / 8; */
    }
  else
    {
      /* probably video */
      if(trak->mdia.minf.is_video)
	{
	  for(i = chunk_sample, total = 0; i < sample; i++)
	    {
	      total += trak->mdia.minf.stbl.stsz.table[i].size;
	    }
	}
      else // Yann: again, for my .mp3 VBR ...
	{
	  long duration_index = 0;
	  long duration = stts->table[duration_index].sample_duration;
	  long sample_passed = 0;
	  //printf("\t\t  VBR audio duration %d\n", duration);
	  
	  for(i = chunk_sample, total = 0; i < sample; i+=duration)
	    {
	      long chunk_index = i/duration;
	      //printf("\t\t i/duration %li\n", i/duration);
	      total += trak->mdia.minf.stbl.stsz.table[chunk_index].size;

	      if(chunk_index > sample_passed + stts->table[duration_index].sample_count) {
		sample_passed += stts->table[duration_index].sample_count;
		duration_index++;
		duration = stts->table[duration_index].sample_duration;
		
	      }
	    }
	  //printf("\t\t  VBR audio total %d\n", total);
	}
    }
  return total;
}

static longest quicktime_sample_to_offset(quicktime_trak_t *trak, long sample) {
  longest chunk, chunk_sample, chunk_offset1, chunk_offset2;

  quicktime_chunk_of_sample(&chunk_sample, &chunk, trak, sample);
  printf("demux_qt: quicktime_sample_to_offset chunk %lld, chunk_sample %lld, sample %ld\n", 
	 chunk, chunk_sample, sample);

  chunk_offset1 = quicktime_chunk_to_offset(trak, chunk);
  chunk_offset2 = chunk_offset1 + quicktime_sample_range_size(trak, chunk_sample, sample);
  return chunk_offset2;
}

static long quicktime_offset_to_sample(quicktime_trak_t *trak, longest offset)
{
  longest chunk_offset;
  longest chunk = quicktime_offset_to_chunk(&chunk_offset, trak, offset);
  longest chunk_sample = quicktime_sample_of_chunk(trak, chunk);
  longest sample, sample_offset;
  quicktime_stsz_table_t *table = trak->mdia.minf.stbl.stsz.table;
  longest total_samples = trak->mdia.minf.stbl.stsz.total_entries;

  if(trak->mdia.minf.stbl.stsz.sample_size)
    {
      sample = chunk_sample + (offset - chunk_offset) / 
	trak->mdia.minf.stbl.stsz.sample_size;
    }
  else
    for(sample = chunk_sample, sample_offset = chunk_offset; 
	sample_offset < offset && sample < total_samples; )
      {
	sample_offset += table[sample].size;
	if(sample_offset < offset) sample++;
      }
	
  return sample;
}

#if 0
static int quicktime_update_tables(quicktime_t *file, 
			    quicktime_trak_t *trak, 
			    longest offset, 
			    longest chunk, 
			    longest sample, 
			    longest samples, 
			    longest sample_size)
{
  if(offset + sample_size > file->mdat.atom.size) file->mdat.atom.size = offset + sample_size;
  quicktime_update_stco(&(trak->mdia.minf.stbl.stco), chunk, offset);
  if(sample_size) quicktime_update_stsz(&(trak->mdia.minf.stbl.stsz), sample, sample_size);
  quicktime_update_stsc(&(trak->mdia.minf.stbl.stsc), chunk, samples);
  return 0;
}
#endif

static int quicktime_trak_duration(quicktime_trak_t *trak, 
				   long *duration, 
				   long *timescale) {
  quicktime_stts_t *stts = &(trak->mdia.minf.stbl.stts);
  int i;
  *duration = 0;

  for(i = 0; i < stts->total_entries; i++) {
    *duration += stts->table[i].sample_duration * stts->table[i].sample_count;
  }

  *timescale = trak->mdia.mdhd.time_scale;
  return 0;
}

static int quicktime_trak_fix_counts(quicktime_t *file, quicktime_trak_t *trak)
{
  long samples = quicktime_track_samples(file, trak);

  trak->mdia.minf.stbl.stts.table[0].sample_count = samples;

  if(trak->mdia.minf.stbl.stsz.sample_size)
    trak->mdia.minf.stbl.stsz.total_entries = samples;

  return 0;
}

static long quicktime_chunk_samples(quicktime_trak_t *trak, long chunk)
{
  long result, current_chunk;
  quicktime_stsc_t *stsc = &(trak->mdia.minf.stbl.stsc);
  long i = stsc->total_entries - 1;
  quicktime_stts_t *stts = &(trak->mdia.minf.stbl.stts);

  do
    {
      current_chunk = stsc->table[i].chunk;
      result = stsc->table[i].samples;
      i--;
    }while(i >= 0 && current_chunk > chunk);	

  i = stts->total_entries - 1;

  /* Yann: I've modified this to handle samples with a duration 
     different from 1 ... needed for ".mp3" fourcc */

  do
    {
      current_chunk = stts->table[i].sample_count;
      i--;
    }while(i >= 0 && current_chunk > chunk);	

  return result*stts->table[i+1].sample_duration;
}

static int quicktime_trak_shift_offsets(quicktime_trak_t *trak, longest offset)
{
  quicktime_stco_t *stco = &(trak->mdia.minf.stbl.stco);
  int i;

  for(i = 0; i < stco->total_entries; i++)
    {
      stco->table[i].offset += offset;
    }
  return 0;
}


/* moov.c */

static int quicktime_moov_init(quicktime_moov_t *moov) {

  int i;
  
  moov->total_tracks = 0;
  for (i = 0 ; i < MAXTRACKS; i++) 
    moov->trak[i] = 0;
  quicktime_mvhd_init(&(moov->mvhd));
  quicktime_udta_init(&(moov->udta));
  quicktime_ctab_init(&(moov->ctab));
  return 0;
}

static int quicktime_moov_delete(quicktime_moov_t *moov) {

  /* int i; */
  while(moov->total_tracks) 
    quicktime_delete_trak(moov);

  quicktime_mvhd_delete(&(moov->mvhd));
  quicktime_udta_delete(&(moov->udta));
  quicktime_ctab_delete(&(moov->ctab));
  return 0;
}

#define QT_zlib 0x7A6C6962

static int quicktime_read_moov(quicktime_t *file, quicktime_moov_t *moov, 
			       quicktime_atom_t *parent_atom, xine_t *xine) {

  /* mandatory mvhd */
  quicktime_atom_t leaf_atom;
  
  do {
    
    quicktime_atom_read_header(file, &leaf_atom);

    /*
    printf ("demux_qt: found moov atom, type >%c %c %c %c<\n",
	    leaf_atom.type[0],leaf_atom.type[1],leaf_atom.type[2],leaf_atom.type[3]);
	    */

    if(quicktime_atom_is(&leaf_atom, "cmov")) {
      quicktime_atom_t compressed_atom;
      
      unsigned char *cmov_buf = 0;
      unsigned char *moov_buf = 0;
      longest cmov_sz, tlen;
      int moov_sz;
      /* int cmov_ret = 0; */
      /* long cmov_comp = 0; */
      
      quicktime_atom_read_header(file, &compressed_atom);
      
      if(quicktime_atom_is(&compressed_atom, "dcom")) {
	/* quicktime_atom_t compressed_type_atom; */
	int zlibfourcc;
	longest offset;
	
	
	quicktime_read_char32(file, (char *)&zlibfourcc);
	zlibfourcc = quicktime_atom_read_size((char *)&zlibfourcc);
	
	if (zlibfourcc != QT_zlib)
	  printf ("demux_qt: header not compressed with zlib\n");
	
	if(compressed_atom.size - 4 > 0) {
	  offset = file->ftell_position + compressed_atom.size - 4;
	  file->quicktime_fseek(file, offset);
	}
      }
      quicktime_atom_read_header(file, &compressed_atom);
      
      if(quicktime_atom_is(&compressed_atom, "cmvd")) {
	z_stream zstrm;
	int zret;
	
	/* read how large uncompressed moov will be */
	quicktime_read_char32(file, (char *)&moov_sz);
	moov_sz = quicktime_atom_read_size((char *)&moov_sz);
	cmov_sz = compressed_atom.size - 4;
	
	/* Allocate buffer for compressed header */
	cmov_buf = (unsigned char *)malloc( cmov_sz );
	if (cmov_buf == 0) {
	  printf ("demux_qt: QT cmov: malloc err 0");
	  exit(1);
	}
	/* Read in  compressed header */
	
	tlen = file->quicktime_read_data(file, (char*)cmov_buf, cmov_sz);
	
	if (tlen != 1) { 
	  printf ("demux_qt: QT cmov: read err tlen %llu\n", tlen);
	  free(cmov_buf);
	  return 0;
	}
	
	/* Allocate buffer for decompressed header */
	moov_sz += 16; /* slop?? */
	moov_buf = (unsigned char *)malloc( moov_sz );
	if (moov_buf == 0) {
	  printf ("demux_qt: QT cmov: malloc err moov_sz %u\n", moov_sz);
	  exit(1);
	}
	
	zstrm.zalloc          = (alloc_func)0;
	zstrm.zfree           = (free_func)0;
	zstrm.opaque          = (voidpf)0;
	zstrm.next_in         = cmov_buf;
	zstrm.avail_in        = cmov_sz;
	zstrm.next_out        = moov_buf;
	zstrm.avail_out       = moov_sz;
	
	zret = inflateInit(&zstrm);
	if (zret != Z_OK) { 
	  printf ("demux_qt: QT cmov: inflateInit err %d\n", zret);
	  break;
	}
	zret = inflate(&zstrm, Z_NO_FLUSH);
	if ((zret != Z_OK) && (zret != Z_STREAM_END)) {
	  printf ("demux_qt: QT cmov inflate: ERR %d\n", zret);
	  break;
	} else {
	  FILE *DecOut;
	  
	  DecOut = fopen("Out.bin", "w");
	  fwrite(moov_buf, 1, moov_sz, DecOut);
	  fclose(DecOut);
	}
	moov_sz = zstrm.total_out;
	zret = inflateEnd(&zstrm);
	
	file->decompressed_buffer_size = moov_sz;
	file->decompressed_buffer = (char*)moov_buf;
	file->decompressed_position = 8; /*  Passing the first moov */
	
	
	
      } /* end of "cmvd" */
      /*		  
			  if (cmov_buf) free(cmov_buf);
			  if (moov_buf) free(moov_buf);
			  if (cmov_ret == 0) return(cmov_ret); //failed or unsupported */
    } /* end of cmov */
    else  if(quicktime_atom_is(&leaf_atom, "mvhd")) {
      quicktime_read_mvhd(file, &(moov->mvhd));
    } else if(quicktime_atom_is(&leaf_atom, "clip")) {
      quicktime_atom_skip(file, &leaf_atom);
    } else if(quicktime_atom_is(&leaf_atom, "trak")) {
      quicktime_trak_t *trak = quicktime_add_trak(moov);	
      quicktime_read_trak(file, trak, &leaf_atom);
    } else if(quicktime_atom_is(&leaf_atom, "udta")) {
      quicktime_read_udta(file, &(moov->udta), &leaf_atom);
      quicktime_atom_skip(file, &leaf_atom);
    } else if (quicktime_atom_is(&leaf_atom, "ctab")) {
      quicktime_read_ctab(file, &(moov->ctab));
    }	else
      quicktime_atom_skip(file, &leaf_atom);
    
    /*      printf("quicktime_read_moov 0x%llx 0x%llx\n", quicktime_position(file), parent_atom->end); */
  } while ((quicktime_position(file) < parent_atom->end && file->decompressed_buffer==NULL)
	   || (quicktime_position(file) < file->decompressed_buffer_size 
	       && file->decompressed_buffer!=NULL));
    
  return 0;
}


static int quicktime_shift_offsets(quicktime_moov_t *moov, longest offset)
{
  int i;
  for(i = 0; i < moov->total_tracks; i++)
    {
      quicktime_trak_shift_offsets(moov->trak[i], offset);
    }
  return 0;
}


static int quicktime_update_positions(quicktime_t *file)
{
  /* Get the sample position from the file offset */
  /* for routines that change the positions of all tracks, like */
  /* seek_end and seek_start but not for routines that reposition one track, like */
  /* set_audio_position. */

  longest mdat_offset = quicktime_position(file) - file->mdat.atom.start;
  longest sample, chunk, chunk_offset;
  int i;

  if(file->total_atracks)
    {
      sample = quicktime_offset_to_sample(file->atracks[0].track, mdat_offset);
      chunk = quicktime_offset_to_chunk(&chunk_offset, file->atracks[0].track, mdat_offset);
      for(i = 0; i < file->total_atracks; i++)
	{
	  file->atracks[i].current_position = sample;
	  file->atracks[i].current_chunk = chunk;
	}
    }

  if(file->total_vtracks)
    {
      sample = quicktime_offset_to_sample(file->vtracks[0].track, mdat_offset);
      chunk = quicktime_offset_to_chunk(&chunk_offset, file->vtracks[0].track, mdat_offset);
      for(i = 0; i < file->total_vtracks; i++)
	{
	  file->vtracks[i].current_position = sample;
	  file->vtracks[i].current_chunk = chunk;
	}
    }
  return 0;
}

static int quicktime_seek_start(quicktime_t *file)
{
  quicktime_set_position(file, file->mdat.atom.start + HEADER_LENGTH * 2);
  quicktime_update_positions(file);
  return 0;
}

static long quicktime_audio_length(quicktime_t *file, int track)
{
  quicktime_stts_t *stts = &(file->atracks[track].track->mdia.minf.stbl.stts);
  long i;
	
  if(file->total_atracks > 0) {
    i = stts->total_entries - 1;

    /* Yann: I've modified this to handle samples with a duration 
       different from 1 ... needed for ".mp3" fourcc */
    
    return stts->table[0].sample_duration*quicktime_track_samples(file, file->atracks[track].track);
  }
  
  return 0;
}

static long quicktime_video_length(quicktime_t *file, int track)
{
  /*printf("quicktime_video_length %d %ld\n", quicktime_track_samples(file, file->vtracks[track].track), track); */
  if(file->total_vtracks > 0)
    return quicktime_track_samples(file, file->vtracks[track].track);
  return 0;
}

static long quicktime_audio_position(quicktime_t *file, int track)
{
  return file->atracks[track].current_position;
}

static long quicktime_video_position(quicktime_t *file, int track)
{
  return file->vtracks[track].current_position;
}

static int quicktime_set_audio_position(quicktime_t *file, longest sample, int track)
{
  longest offset, chunk_sample, chunk;
  quicktime_trak_t *trak;

  if(file->total_atracks) {
    trak = file->atracks[track].track;
    file->atracks[track].current_position = sample;
    //		printf("BEFORE  quicktime_chunk_of_sample track %d sample %li\n", track, sample);
    quicktime_chunk_of_sample(&chunk_sample, &chunk, trak, sample);
    file->atracks[track].current_chunk = chunk;
    //		printf("AFTER  quicktime_chunk_of_sample chunk %d chunk_sample %d\n", chunk, chunk_sample);
    offset = quicktime_sample_to_offset(trak, sample);
    //		printf("AFTER  quicktime_sample_to_offset offset %li\n", offset);
    quicktime_set_position(file, offset);
  }

  return 0;
}

static int quicktime_set_video_position(quicktime_t *file, longest frame, 
					int track) {
  longest offset, chunk_sample, chunk;
  quicktime_trak_t *trak;

  if(file->total_vtracks) {
    trak = file->vtracks[track].track;
    file->vtracks[track].current_position = frame;
    quicktime_chunk_of_sample(&chunk_sample, &chunk, trak, frame);
    file->vtracks[track].current_chunk = chunk;
    offset = quicktime_sample_to_offset(trak, frame);
    quicktime_set_position(file, offset);
  }
  return 0;
}

static int quicktime_audio_tracks(quicktime_t *file) {
  int i, result = 0;
  quicktime_minf_t *minf;
  for(i = 0; i < file->moov.total_tracks; i++) {
    minf = &(file->moov.trak[i]->mdia.minf);
    if(minf->is_audio)
      result++;
  }
  return result;
}

static int quicktime_has_audio(quicktime_t *file) {
  if(quicktime_audio_tracks(file)) 
    return 1;
  return 0;
}

static long quicktime_sample_rate(quicktime_t *file, int track) {
  if(file->total_atracks)
    return file->atracks[track].track->mdia.minf.stbl.stsd.table[0].sample_rate;
  return 0;
}

static int quicktime_audio_bits(quicktime_t *file, int track) {
  if(file->total_atracks)
    return file->atracks[track].track->mdia.minf.stbl.stsd.table[0].sample_size;

  return 0;
}

static char* quicktime_audio_compressor(quicktime_t *file, int track) {
  if (track < file->total_atracks)
    return file->atracks[track].track->mdia.minf.stbl.stsd.table[0].format;
  /*
   * XXX: quick hack to avoid crashes when there's no audio.
   * olympus 2040 digital camera records quicktime video without audio
   */
  return "NONE";
}

static int quicktime_track_channels(quicktime_t *file, int track) {
  if(track < file->total_atracks)
    return file->atracks[track].channels;

  return 0;
}

static int quicktime_channel_location(quicktime_t *file, int *quicktime_track, 
				      int *quicktime_channel, int channel) {
  int current_channel = 0, current_track = 0;
  *quicktime_channel = 0;
  *quicktime_track = 0;
  for(current_channel = 0, current_track = 0; current_track < file->total_atracks; ) {
    if(channel >= current_channel) {
      *quicktime_channel = channel - current_channel;
      *quicktime_track = current_track;
    }

    current_channel += file->atracks[current_track].channels;
    current_track++;
  }
  return 0;
}

static int quicktime_video_tracks(quicktime_t *file) {
  int i, result = 0;
  for(i = 0; i < file->moov.total_tracks; i++) {
    if(file->moov.trak[i]->mdia.minf.is_video) 
      result++;
  }
  return result;
}


static int quicktime_has_video(quicktime_t *file) {
  if (quicktime_video_tracks(file)) 
    return 1;
  return 0;
}

static int quicktime_video_width(quicktime_t *file, int track) {
  if(file->total_vtracks)
    return file->vtracks[track].track->tkhd.track_width;
  return 0;
}

static int quicktime_video_height(quicktime_t *file, int track) {
  if(file->total_vtracks)
    return file->vtracks[track].track->tkhd.track_height;
  return 0;
}

static int quicktime_video_depth(quicktime_t *file, int track) {
  if(file->total_vtracks)
    return file->vtracks[track].track->mdia.minf.stbl.stsd.table[0].depth;
  return 0;
}


static float quicktime_frame_rate(quicktime_t *file, int track) {
  if(file->total_vtracks > track)
    return (float)file->vtracks[track].track->mdia.mdhd.time_scale / 
      file->vtracks[track].track->mdia.minf.stbl.stts.table[0].sample_duration;

  return 0;
}

static char* quicktime_video_compressor(quicktime_t *file, int track) {
  return file->vtracks[track].track->mdia.minf.stbl.stsd.table[0].format;
}


	
static int quicktime_init_video_map(quicktime_t *file, quicktime_video_map_t *vtrack, 
				    quicktime_trak_t *trak) {
  vtrack->track = trak;
  vtrack->current_position = 0;
  vtrack->current_chunk = 1;
  return 0;
}

static int quicktime_delete_video_map(quicktime_t *file, quicktime_video_map_t *vtrack) {
  return 0;
}

static int quicktime_init_audio_map(quicktime_t *file, quicktime_audio_map_t *atrack, quicktime_trak_t *trak)
{
  atrack->track = trak;
  atrack->channels = trak->mdia.minf.stbl.stsd.table[0].channels;
  atrack->current_position = 0;
  atrack->current_chunk = 1;
  return 0;
}

static int quicktime_delete_audio_map(quicktime_t *file, quicktime_audio_map_t *atrack) {
  return 0;
}

static void quicktime_mdat_delete(quicktime_mdat_t *mdat) {
}

static void quicktime_read_mdat(quicktime_t *file, quicktime_mdat_t *mdat, 
				quicktime_atom_t *parent_atom, xine_t *xine) {
  mdat->atom.size = parent_atom->size;
  mdat->atom.start = parent_atom->start;
  quicktime_atom_skip(file, parent_atom);
}

static int quicktime_read_info(quicktime_t *file, xine_t *xine) {

  int result = 0, found_moov = 0;
  int i, track;
  longest start_position = quicktime_position(file);
  quicktime_atom_t leaf_atom;
  int found_mdat=0;

  quicktime_set_position(file, 0/*LL*/);

  do {
    result = quicktime_atom_read_header(file, &leaf_atom);

    /*
    printf ("demux_qt: found atom, type >%c %c %c %c<\n",
	    leaf_atom.type[0],leaf_atom.type[1],leaf_atom.type[2],leaf_atom.type[3]);
	    */

    if(!result) {
      if(quicktime_atom_is(&leaf_atom, "mdat")) {
	quicktime_read_mdat(file, &(file->mdat), &leaf_atom, xine);
	found_mdat = 1;
      } else if(quicktime_atom_is(&leaf_atom, "moov")) {
	quicktime_read_moov(file, &(file->moov), &leaf_atom, xine);
	found_moov = 1;
      } else {
	quicktime_atom_skip(file, &leaf_atom);
      }
    }
  } while (!result && (found_mdat + found_moov != 2));

  /* go back to the original position */
  quicktime_set_position(file, start_position);
  
  if (found_moov) {
    /* get tables for all the different tracks */
    file->total_atracks = quicktime_audio_tracks(file);
    file->atracks = (quicktime_audio_map_t*)calloc(1, sizeof(quicktime_audio_map_t) * file->total_atracks);
    
    for(i = 0, track = 0; i < file->total_atracks; i++) {
      while(!file->moov.trak[track]->mdia.minf.is_audio)
	track++;
      quicktime_init_audio_map(file, &(file->atracks[i]), file->moov.trak[track]);
    }
      
    file->total_vtracks = quicktime_video_tracks(file);
    file->vtracks = (quicktime_video_map_t*)calloc(1, sizeof(quicktime_video_map_t) * file->total_vtracks);
    
    for(track = 0, i = 0; i < file->total_vtracks; i++) {
      while(!file->moov.trak[track]->mdia.minf.is_video)
	track++;
	  
      quicktime_init_video_map(file, &(file->vtracks[i]), file->moov.trak[track]);
    }
  }
  
  return !found_moov;
}


/* ============================= Initialization functions */

static int quicktime_init(quicktime_t *file) {

  memset(file, sizeof(quicktime_t), 0);

  file->quicktime_read_data = quicktime_read_data;
  file->quicktime_fseek = quicktime_fseek;

  quicktime_moov_init(&(file->moov));
  return 0;
}

static int quicktime_delete(quicktime_t *file) {
  int i;

  if(file->total_atracks) {
    for(i = 0; i < file->total_atracks; i++)
      quicktime_delete_audio_map(file, &(file->atracks[i]));
    free(file->atracks);
  }

  if(file->total_vtracks) {
    for(i = 0; i < file->total_vtracks; i++)
      quicktime_delete_video_map(file, &(file->vtracks[i]));
    free(file->vtracks);
  }

  file->total_atracks = 0;
  file->total_vtracks = 0;

  if(file->preload_size) {
    free(file->preload_buffer);
    file->preload_size = 0;
  }

  quicktime_moov_delete(&(file->moov));

  quicktime_mdat_delete(&(file->mdat));

  return 0;
}

/* ================================== Entry points ============================= */

static longest get_file_length( quicktime_t *file) {

  return file->input->get_length (file->input);

}


static int quicktime_check_sig(input_plugin_t *input) {

  quicktime_t       *file;
  quicktime_atom_t   leaf_atom;
  int                result1 = 0, result2 = 0;

  file = xine_xmalloc (sizeof (quicktime_t));

  quicktime_init(file);

  file->input = input;

  input->seek (input, 0, SEEK_SET);

  file->total_length = get_file_length(file);

  do {
    result1 = quicktime_atom_read_header(file, &leaf_atom);
    
    /*
    printf ("demux_qt: found atom, type >%c %c %c %c<\n",
	    leaf_atom.type[0],leaf_atom.type[1],leaf_atom.type[2],leaf_atom.type[3]);
    */

    if(!result1) {
      /* just want the "moov" atom */
      if(quicktime_atom_is(&leaf_atom, "moov")) {
	result2 = 1;
      } else
	quicktime_atom_skip(file, &leaf_atom);
    }
  }while(!result1 && !result2 && quicktime_position(file) < file->total_length);

  quicktime_delete(file); 

  free(file); 

  return result2;
}

static void  quicktime_close(quicktime_t *file)
{
  quicktime_delete(file);
  free(file);
}

static quicktime_t* quicktime_open(input_plugin_t *input, xine_t *xine) {
  quicktime_t *new_file = calloc(1, sizeof(quicktime_t));

  quicktime_init(new_file);
  new_file->mdat.atom.start = 0;

  new_file->decompressed_buffer_size = 0;
  new_file->decompressed_buffer = NULL;
  new_file->decompressed_position = 0;

  new_file->input = input;

  new_file->quicktime_read_data = quicktime_read_data;
  new_file->quicktime_fseek = quicktime_fseek;

  input->seek (input, 0, SEEK_SET);

  /* Get length. */
  new_file->total_length = get_file_length(new_file);
  
  if(quicktime_read_info(new_file, xine)) {
    quicktime_close(new_file);
    printf ("demux_qt: quicktime_open: error in header\n");
    new_file = 0;
  }

  return new_file;
}

/*
 * now for the xine-specific demuxer stuff
 */

static off_t demux_qt_get_sample_size (quicktime_trak_t *trak, int sample_num) {

  quicktime_stsz_t *stsz;

  stsz = &trak->mdia.minf.stbl.stsz;

  if (stsz->sample_size)
    return stsz->sample_size;

  return stsz->table[sample_num].size;
}

static void *demux_qt_loop (void *this_gen) {

  buf_element_t *buf = NULL;
  demux_qt_t    *this = (demux_qt_t *) this_gen;
  int            idx;
  off_t          offset, size, todo;
  fifo_buffer_t *fifo;
  int64_t        pts;
  uint32_t       flags;

#ifdef LOG
  printf ("demux_qt: demux loop starting...\n"); 
#endif

  idx = 0;

  while(1) {
    int is_audio, sample_num;
    
    pthread_mutex_lock( &this->mutex );
    
    if( this->status != DEMUX_OK)
      break;

    if (idx >= this->num_index_entries)
      break;

    is_audio = (this->index[idx].type & BUF_MAJOR_MASK) == BUF_AUDIO_BASE;

    if (is_audio)
      fifo = this->audio_fifo;
    else
      fifo = this->video_fifo;

    offset = this->index[idx].offset;
    pts    = this->index[idx].pts;

    check_newpts( this, pts );
    
    for (sample_num = this->index[idx].first_sample; sample_num <= this->index[idx].last_sample; sample_num++) {

      todo = demux_qt_get_sample_size (this->index[idx].track, sample_num);

#ifdef LOG
      printf ("demux_qt: [idx:%04d type:%08x len:%08lld ] ---------------------------\n", 
	      idx, this->index[idx].type, todo);
#endif

      flags = BUF_FLAG_FRAME_START;
      while (todo) {

	buf = fifo->buffer_pool_alloc (fifo);

	if (todo>buf->max_size)
	  size = buf->max_size;
	else
	  size = todo;
	todo -= size;

	quicktime_set_position (this->qt, offset);
    
	buf->size          = this->qt->quicktime_read_data (this->qt, buf->mem, size);
#ifdef LOG
	printf ("demux_qt: generated buffer of %d bytes \n", buf->size);
#endif
    
	buf->content       = buf->mem;

#ifdef DBG_QT
	if (is_audio)
	  write (debug_fh, buf->mem, buf->size);
#endif

	buf->type          = this->index[idx].type;
	buf->pts           = pts;
	buf->input_pos     = this->qt->file_position;
	if (todo)
	  buf->decoder_flags = flags;
	else
	  buf->decoder_flags = flags | BUF_FLAG_FRAME_END;


	fifo->put (fifo, buf);

	pts = 0;
	flags = 0;
	offset += size;
      }

    }
    idx++;
    
    pthread_mutex_unlock( &this->mutex );

  }

  printf ("demux_qt: demux loop finished (status: %d)\n",
	  this->status);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_flags   = BUF_FLAG_END_STREAM; /* stream finished */
    this->video_fifo->put (this->video_fifo, buf);
    
    if(this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type            = BUF_CONTROL_END;
      buf->decoder_flags   = BUF_FLAG_END_STREAM; /* stream finished */
      this->audio_fifo->put (this->audio_fifo, buf);
    }

  }
    
  pthread_mutex_unlock( &this->mutex );

  pthread_exit(NULL);

  return NULL;
}

static void demux_qt_close (demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  free (this);

#ifdef DBG_QT
  close (debug_fh);
#endif  

}

static void demux_qt_stop (demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  buf_element_t *buf;
  void *p;

  pthread_mutex_lock( &this->mutex );
  
  if (this->status != DEMUX_OK) {
    printf ("demux_qt: stop...ignored\n");
    pthread_mutex_unlock( &this->mutex );
    return;
  }

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;
  
  pthread_mutex_unlock( &this->mutex );
  pthread_join (this->thread, &p);

  xine_flush_engine(this->xine);

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type            = BUF_CONTROL_END;
  buf->decoder_flags   = BUF_FLAG_END_USER; /* user finished */
  this->video_fifo->put (this->video_fifo, buf);

  if(this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_flags   = BUF_FLAG_END_USER; /* user finished */
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_qt_get_status (demux_plugin_t *this_gen) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

  return this->status;
}

static int demux_qt_detect_compressors (demux_qt_t *this) {

  char *video, *audio;

  video = quicktime_video_compressor (this->qt, 0);

  this->bih.biSize=sizeof(this->bih);
  this->bih.biWidth = quicktime_video_width (this->qt, 0);
  this->bih.biHeight= quicktime_video_height (this->qt, 0);
  this->bih.biPlanes= 0;
  memcpy( &this->bih.biCompression, video, 4);
  this->bih.biBitCount= quicktime_video_depth(this->qt, 0);
  this->bih.biSizeImage=this->bih.biWidth*this->bih.biHeight;
  this->bih.biXPelsPerMeter=1;
  this->bih.biYPelsPerMeter=1;
  this->bih.biClrUsed=0;
  this->bih.biClrImportant=0;

  this->video_step = 90000.0 / quicktime_frame_rate (this->qt, 0);

  this->video_type = fourcc_to_buf_video( video );
  
  /* FIXME: do we really need to set biCompression again here? */
  if (this->video_type == BUF_VIDEO_CINEPAK) {
    this->bih.biCompression=mmioFOURCC('c', 'v', 'i', 'd');
  }
    
  if (!this->video_type) {
    xine_log (this->xine, XINE_LOG_FORMAT,
	      _("demux_qt: unknown video codec '%s'\n"), video);
    return 0;
  }
  
  printf ("demux_qt: video codec is '%s'\n", video);

  this->wavex.nChannels       = quicktime_track_channels (this->qt, 0);
  this->wavex.nSamplesPerSec  = quicktime_sample_rate (this->qt, 0);
  this->wavex.nAvgBytesPerSec = 0; /* FIXME */
  this->wavex.nBlockAlign     = 16;
  this->wavex.wBitsPerSample  = quicktime_audio_bits (this->qt, 0);
  this->wavex.cbSize          = sizeof (this->wavex); /* FIXME */

  this->audio_factor = 90000.0 / ((double) quicktime_sample_rate (this->qt, 0)) ;

  this->has_audio = quicktime_has_audio (this->qt);

  audio = quicktime_audio_compressor (this->qt, 0);

  printf ("demux_qt: audio codec is '%s'\n", audio);

  if (!strncasecmp (audio, "raw ", 4)) {
    this->audio_type = BUF_AUDIO_LPCM_LE;
    this->wavex.wFormatTag = WAVE_FORMAT_ADPCM;
  } else if (!strncasecmp (audio, ".mp3", 4)) {
    this->audio_type = BUF_AUDIO_MPEG;
  } else if (!strncasecmp (audio, "mp4a", 4)) {
    this->audio_type = BUF_AUDIO_AAC;
  } else {
    printf ("demux_qt: unknown audio codec >%s<\n",
	    audio);
    this->audio_type = BUF_CONTROL_NOP;
  }

  return 1;
}

static void demux_qt_add_index_entry (demux_qt_t *this, off_t offset, 
				      int first_sample, int last_sample,
				      int64_t pts, int32_t type,
				      quicktime_trak_t *track) {

  int i,j;

  /*
   * insertion sort 
   */

  for (i=0; i<this->num_index_entries; i++) {
    if (this->index[i].pts >= pts)
      break;
  }

  for (j=this->num_index_entries; j>i; j--) 
    this->index[j] = this->index[j-1];
  
  this->index[i].pts          = pts;
  this->index[i].offset       = offset;
  this->index[i].first_sample = first_sample;
  this->index[i].last_sample  = last_sample;
  this->index[i].type         = type;
  this->index[i].track        = track;

  this->num_index_entries++;
}

static void demux_qt_index_trak (demux_qt_t *this, quicktime_trak_t *trak, uint32_t type) {

  quicktime_stsz_t *stsz;
  quicktime_stsc_t *stsc;
  quicktime_stco_t *stco;
  quicktime_stts_t *stts;
  long              time_scale;
  int               stsc_entry, stsc_first, stsc_cur, stsc_next;
  int               stsc_samples, stsc_last_sample, stsc_first_sample;
  int               stts_entry, stts_first_sample, stts_last_sample;
  int64_t           stts_duration, stts_pts, pts;
  off_t             chunk_offset;

  stts        = &trak->mdia.minf.stbl.stts;
  stsz        = &trak->mdia.minf.stbl.stsz;
  stsc        = &trak->mdia.minf.stbl.stsc;
  stco        = &trak->mdia.minf.stbl.stco;
  time_scale  = trak->mdia.mdhd.time_scale;
  pts         = 0;

  /*
   * generate one entry per chunk
   */
  
  /* chunk-tracking */

  stsc_entry        = 0;
  stsc_first        = stsc->table[stsc_entry].chunk; 
  stsc_cur          = stsc_first;

  if (stsc->total_entries>(stsc_entry+1))
    stsc_next       = stsc->table[stsc_entry+1].chunk;
  else
    stsc_next       = 1000000;
  
  stsc_samples      = stsc->table[stsc_entry].samples;
  stsc_last_sample  = stsc_samples-1;
  stsc_first_sample = 0;
  
  /* time-to-sample tracking */
  
  stts_entry        = 0;
  stts_first_sample = 0;
  stts_last_sample  = stts->table[stts_entry].sample_count-1;
  stts_duration     = stts->table[stts_entry].sample_duration * 90000 / time_scale;
  stts_pts          = 0;
  
  while (stsc_cur < stco->total_entries) {
#ifdef LOG    
    printf ("demux_qt: chunk # is %d...\n", stsc_cur);
#endif
    
    chunk_offset = stco->table[stsc_cur-1].offset;

    /*
     * add index entry
     */

#ifdef LOG
    printf ("demux_qt: index entry, sample_num=%d, offset = %lld, sample %d-%d, pts = %lld\n",
	    stsc_first_sample, chunk_offset, stsc_first_sample, stsc_last_sample, pts);
#endif

    demux_qt_add_index_entry (this, chunk_offset, stsc_first_sample, stsc_last_sample, pts, type,
			      trak);

    /*
     * next chunk
     */

    stsc_cur ++;

    /*
     * offset of chunk / sample
     */
    
    while (stsc_cur >= stsc_next) {
      
      stsc_entry++;
      
      stsc_first = stsc->table[stsc_entry].chunk; 

      if (stsc->total_entries>(stsc_entry+1))
	stsc_next = stsc->table[stsc_entry+1].chunk;
      else
	stsc_next = 1000000;
	
      stsc_samples      = stsc->table[stsc_entry].samples;
    }      

    stsc_first_sample  = stsc_last_sample + 1;
    stsc_last_sample   = stsc_first_sample + stsc_samples - 1;

#ifdef LOG
    printf ("demux_qt: chunk offset is %lld...\n", chunk_offset);
#endif

    /* 
     * find out about pts of sample
     */

    while (stsc_first_sample > stts_last_sample) {

      stts_pts          += stts_duration * stts->table[stts_entry].sample_count;
      stts_entry++;
      stts_first_sample  = stts_last_sample+1;
      stts_last_sample  += stts->table[stts_entry].sample_count;
      stts_duration      = stts->table[stts_entry].sample_duration * 90000 / time_scale;
    }

    pts = stts_pts + (stsc_first_sample - stts_first_sample) * stts_duration;

  }

}

static void demux_qt_create_index (demux_qt_t *this) {

  int               track_num;
  quicktime_trak_t *trak;

  this->num_index_entries = 0;

  /* video */

  printf ("demux_qt: generating index, video entries...\n");

  for (track_num = 0; track_num<this->qt->total_vtracks; track_num++) {

    trak        = this->qt->vtracks[track_num].track;

    demux_qt_index_trak (this, trak, this->video_type | track_num);
  }

  /* audio */

  printf ("demux_qt: generating index, audio entries...\n");

  for (track_num = 0; track_num<this->qt->total_atracks; track_num++) {

    trak = this->qt->atracks[track_num].track;

    demux_qt_index_trak (this, trak, this->audio_type | track_num);
  }
  printf ("demux_qt: index generation done.\n");
}

static void demux_qt_start (demux_plugin_t *this_gen,
			    fifo_buffer_t *video_fifo, 
			    fifo_buffer_t *audio_fifo,
			    off_t start_pos, int start_time) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  buf_element_t *buf;
  int err;

  pthread_mutex_lock( &this->mutex );

  if( this->status != DEMUX_OK ) {
    this->video_fifo  = video_fifo;
    this->audio_fifo  = audio_fifo;

#ifdef DBG_QT
    debug_fh = open ("/tmp/t.mp3", O_CREAT | O_WRONLY | O_TRUNC, 0644);
#endif

    /*
     * init quicktime parser
     */

    this->qt = quicktime_open (this->input, this->xine);

    if (!this->qt) {
      this->status = DEMUX_FINISHED;
      pthread_mutex_unlock( &this->mutex );
      return;
    }
  
    xine_log (this->xine, XINE_LOG_FORMAT,
	    _("demux_qt: video codec %s (%f fps), audio codec %s (%ld Hz, %d bits)\n"),
	    quicktime_video_compressor (this->qt,0),
	    quicktime_frame_rate (this->qt,0),
	    quicktime_audio_compressor (this->qt,0),
	    quicktime_sample_rate (this->qt,0),
	    quicktime_audio_bits (this->qt,0));

    if (!demux_qt_detect_compressors (this)) {
      this->status = DEMUX_FINISHED;
      pthread_mutex_unlock( &this->mutex );
      return;
    }

    /*
     * generate index
     */

    demux_qt_create_index (this);

    /* 
     * send start buffer
     */

    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type    = BUF_CONTROL_START;
    this->video_fifo->put (this->video_fifo, buf);

    if(this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type    = BUF_CONTROL_START;
      this->audio_fifo->put (this->audio_fifo, buf);
    }
  }
  
  /*
   * seek to start pos/time
   */
  if (start_pos) {

    double f = (double) start_pos / (double) this->input->get_length (this->input) ;
    

    quicktime_set_audio_position (this->qt,
				  quicktime_audio_length (this->qt, 0) * f, 0);

    quicktime_set_video_position (this->qt,
				  quicktime_video_length (this->qt, 0) * f, 0);

  }
  this->send_newpts = 1;
  
  if( this->status != DEMUX_OK ) {
    /*
     * send init info to decoders
     */

    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->content = buf->mem;
    buf->decoder_flags   = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0; /* first package, containing bih */
    buf->decoder_info[1] = this->video_step;
    memcpy (buf->content, &this->bih, sizeof (this->bih));
    buf->size = sizeof (this->bih);

    buf->type = this->video_type;

    this->video_fifo->put (this->video_fifo, buf);

    if(this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->content = buf->mem;
      memcpy (buf->content, &this->wavex, 
	      sizeof (this->wavex));
      buf->size = sizeof (this->wavex);
      buf->type = this->audio_type;
      buf->decoder_flags   = BUF_FLAG_HEADER;
      buf->decoder_info[0] = 0; /* first package, containing wavex */
      buf->decoder_info[1] = quicktime_sample_rate (this->qt, 0);
      buf->decoder_info[2] = quicktime_audio_bits (this->qt, 0); 
      buf->decoder_info[3] = quicktime_track_channels (this->qt, 0);
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    /*
     * now start demuxing
     */

    this->status = DEMUX_OK ;
    this->send_end_buffers = 1;

    if ((err = pthread_create (&this->thread, NULL, demux_qt_loop, this)) != 0) {
      printf ("demux_qt: can't create new thread (%s)\n", strerror(err));
      exit (1);
    }
  }
  else {
    xine_flush_engine(this->xine);
  }
  pthread_mutex_unlock( &this->mutex );

}

static void demux_qt_seek (demux_plugin_t *this_gen,
			     off_t start_pos, int start_time) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

	demux_qt_start (this_gen, this->video_fifo, this->audio_fifo,
			 start_pos, start_time);
}


static int demux_qt_open(demux_plugin_t *this_gen,
			 input_plugin_t *input, int stage) {

  demux_qt_t *this = (demux_qt_t *) this_gen;

  switch(stage) {

  case STAGE_BY_CONTENT: {
    
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0) 
      return DEMUX_CANNOT_HANDLE;
    if ((input->get_capabilities(input) & INPUT_CAP_BLOCK) != 0)
      return DEMUX_CANNOT_HANDLE;
      
    if (quicktime_check_sig (input)) {
      this->input = input;
      return DEMUX_CAN_HANDLE;
    }

    return DEMUX_CANNOT_HANDLE;

  }
  break;

  case STAGE_BY_EXTENSION: {
    char *suffix;
    char *MRL;
    char *m, *valid_ends;
    
    MRL = input->get_mrl (input);
    
    suffix = strrchr(MRL, '.');
    
    if(!suffix)
      return DEMUX_CANNOT_HANDLE;
    
    xine_strdupa(valid_ends, (this->config->register_string(this->config,
							    "mrl.ends_qt", VALID_ENDS,
							    "valid mrls ending for qt demuxer",
							    NULL, NULL, NULL)));
    while((m = xine_strsep(&valid_ends, ",")) != NULL) { 
      
      while(*m == ' ' || *m == '\t') m++;
      
      if(!strcasecmp((suffix + 1), m)) {
	this->input = input;
	return DEMUX_CAN_HANDLE;
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

static char *demux_qt_get_id(void) {
  return "QUICKTIME";
}

static char *demux_qt_get_mimetypes(void) {
  return "video/quicktime: mov,qt: Quicktime animation;"
         "video/x-quicktime: mov,qt: Quicktime animation;";
}

static int demux_qt_get_stream_length (demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;

  return this->video_step * quicktime_video_length (this->qt, 0);
}

demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {

  demux_qt_t      *this;

  if (iface != 7) {
    printf ("demux_qt: plugin doesn't support plugin API version %d.\n"
	    "          this means there's a version mismatch between xine and this "
	    "          demuxer plugin.\nInstalling current demux plugins should help.\n",
	    iface);
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_qt_t));
  this->config = xine->config;
  this->xine   = xine;

  (void*) this->config->register_string(this->config,
					"mrl.ends_qt", VALID_ENDS,
					"valid mrls ending for qt demuxer",
					NULL, NULL, NULL);    

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_qt_open;
  this->demux_plugin.start             = demux_qt_start;
  this->demux_plugin.seek              = demux_qt_seek;
  this->demux_plugin.stop              = demux_qt_stop;
  this->demux_plugin.close             = demux_qt_close;
  this->demux_plugin.get_status        = demux_qt_get_status;
  this->demux_plugin.get_identifier    = demux_qt_get_id;
  this->demux_plugin.get_stream_length = demux_qt_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_qt_get_mimetypes;
    
  this->status = DEMUX_FINISHED;
  pthread_mutex_init( &this->mutex, NULL );
  
  return (demux_plugin_t *) this;
}
