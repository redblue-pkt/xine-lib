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
 * Quicktime File Demuxer by Mike Melanson (melanson@pcisys.net)
 *  based on a Quicktime parsing experiment entitled 'lazyqt'
 *
 * Ideally, more documentation is forthcoming, but in the meantime:
 * functional flow:
 *  create_qt_info
 *  open_qt_file
 *   parse_moov_atom
 *    parse_mvhd_atom
 *    parse_trak_atom
 *    build_frame_table
 *  free_qt_info
 *
 * $Id: demux_qt.c,v 1.118 2002/11/20 05:09:55 tmmm Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <zlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"
#include "buffer.h"
#include "bswap.h"

#include "qtpalette.h"

typedef unsigned int qt_atom;

#define QT_ATOM( ch0, ch1, ch2, ch3 ) \
        ( (unsigned char)(ch3) | \
        ( (unsigned char)(ch2) << 8 ) | \
        ( (unsigned char)(ch1) << 16 ) | \
        ( (unsigned char)(ch0) << 24 ) )

/* top level atoms */
#define FREE_ATOM QT_ATOM('f', 'r', 'e', 'e')
#define JUNK_ATOM QT_ATOM('j', 'u', 'n', 'k')
#define MDAT_ATOM QT_ATOM('m', 'd', 'a', 't')
#define MOOV_ATOM QT_ATOM('m', 'o', 'o', 'v')
#define PNOT_ATOM QT_ATOM('p', 'n', 'o', 't')
#define SKIP_ATOM QT_ATOM('s', 'k', 'i', 'p')
#define WIDE_ATOM QT_ATOM('w', 'i', 'd', 'e')
#define PICT_ATOM QT_ATOM('P', 'I', 'C', 'T')

#define CMOV_ATOM QT_ATOM('c', 'm', 'o', 'v')

#define MVHD_ATOM QT_ATOM('m', 'v', 'h', 'd')

#define VMHD_ATOM QT_ATOM('v', 'm', 'h', 'd')
#define SMHD_ATOM QT_ATOM('s', 'm', 'h', 'd')

#define TRAK_ATOM QT_ATOM('t', 'r', 'a', 'k')
#define TKHD_ATOM QT_ATOM('t', 'k', 'h', 'd')
#define MDHD_ATOM QT_ATOM('m', 'd', 'h', 'd')
#define ELST_ATOM QT_ATOM('e', 'l', 's', 't')

/* atoms in a sample table */
#define STSD_ATOM QT_ATOM('s', 't', 's', 'd')
#define STSZ_ATOM QT_ATOM('s', 't', 's', 'z')
#define STSC_ATOM QT_ATOM('s', 't', 's', 'c')
#define STCO_ATOM QT_ATOM('s', 't', 'c', 'o')
#define STTS_ATOM QT_ATOM('s', 't', 't', 's')
#define STSS_ATOM QT_ATOM('s', 't', 's', 's')
#define CO64_ATOM QT_ATOM('c', 'o', '6', '4')

#define ESDS_ATOM QT_ATOM('e', 's', 'd', 's')

#define IMA4_FOURCC QT_ATOM('i', 'm', 'a', '4')
#define MP4A_FOURCC QT_ATOM('m', 'p', '4', 'a')

#define UDTA_ATOM QT_ATOM('u', 'd', 't', 'a')
#define CPY_ATOM QT_ATOM(0xA9, 'c', 'p', 'y')
#define DES_ATOM QT_ATOM(0xA9, 'd', 'e', 's')
#define CMT_ATOM QT_ATOM(0xA9, 'c', 'm', 't')

/* placeholder for cutting and pasting */
#define _ATOM QT_ATOM('', '', '', '')

#define ATOM_PREAMBLE_SIZE 8
#define PALETTE_COUNT 256

/* these are things that can go wrong */
typedef enum {
  QT_OK,
  QT_FILE_READ_ERROR,
  QT_NO_MEMORY,
  QT_NOT_A_VALID_FILE,
  QT_NO_MOOV_ATOM,
  QT_NO_ZLIB,
  QT_ZLIB_ERROR,
  QT_HEADER_TROUBLE
} qt_error;

/* there are other types but these are the ones we usually care about */
typedef enum {

  MEDIA_AUDIO,
  MEDIA_VIDEO,
  MEDIA_OTHER

} media_type;

typedef struct {
  media_type type;
  int64_t offset;
  unsigned int size;
  int64_t pts;
  int keyframe;
  unsigned int official_byte_count;
} qt_frame;

typedef struct {
  unsigned int track_duration;
  unsigned int media_time;
} edit_list_table_t;

typedef struct {
  unsigned int first_chunk;
  unsigned int samples_per_chunk;
} sample_to_chunk_table_t;

typedef struct {
  unsigned int count;
  unsigned int duration;
} time_to_sample_table_t;

typedef struct {

  /* trak description */
  media_type type;
  union {

    struct {
      unsigned int codec_format;
      unsigned int width;
      unsigned int height;
      int palette_count;
      palette_entry_t palette[PALETTE_COUNT];
      int depth;
    } video;

    struct {
      unsigned int codec_format;
      unsigned int sample_rate;
      unsigned int channels;
      unsigned int bits;
      unsigned int vbr;
    } audio;

  } media_description;

  /* edit list table */
  unsigned int edit_list_count;
  edit_list_table_t *edit_list_table;

  /* chunk offsets */
  unsigned int chunk_offset_count;
  int64_t *chunk_offset_table;

  /* sample sizes */
  unsigned int sample_size;
  unsigned int sample_size_count;
  unsigned int *sample_size_table;

  /* sync samples, a.k.a., keyframes */
  unsigned int sync_sample_count;
  unsigned int *sync_sample_table;

  /* sample to chunk table */
  unsigned int sample_to_chunk_count;
  sample_to_chunk_table_t *sample_to_chunk_table;

  /* time to sample table */
  unsigned int time_to_sample_count;
  time_to_sample_table_t *time_to_sample_table;

  /* temporary frame table corresponding to this sample table */
  qt_frame *frames;
  unsigned int frame_count;

  /* trak timescale */
  unsigned int timescale;

  /* flags that indicate how a trak is supposed to be used */
  unsigned int flags;
  
  /* decoder data pass information to the AAC decoder */
  void *decoder_config;
  int decoder_config_len;

  /* special audio parameters */
  unsigned int samples_per_packet;
  unsigned int bytes_per_packet;
  unsigned int bytes_per_frame;
  unsigned int bytes_per_sample;
  unsigned int samples_per_frame;

} qt_sample_table;

typedef struct {
  FILE *qt_file;
  int compressed_header;  /* 1 if there was a compressed moov; just FYI */

  unsigned int creation_time;  /* in ms since Jan-01-1904 */
  unsigned int modification_time;
  unsigned int timescale;  /* base clock frequency is Hz */
  unsigned int duration;
        
  int64_t moov_first_offset;
  int64_t moov_last_offset;

  qt_atom audio_codec;
  unsigned int audio_type;
  unsigned int audio_sample_rate;
  unsigned int audio_channels;
  unsigned int audio_bits;
  unsigned int audio_vbr;    /* flag to indicate if audio is VBR */
  void *audio_decoder_config;
  int audio_decoder_config_len;
  unsigned int *audio_sample_size_table;

  qt_atom video_codec;
  unsigned int video_type;
  unsigned int video_width;
  unsigned int video_height;
  unsigned int video_depth;
  void *video_decoder_config;
  int video_decoder_config_len;

  qt_frame *frames;
  unsigned int frame_count;

  int                    palette_count;
  palette_entry_t        palette[PALETTE_COUNT];

  char                  *copyright;
  char                  *description;
  char                  *comment;

  qt_error last_error;
} qt_info;

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  int                  status;

  /* when this flag is set, demuxer only dispatches audio samples until it
   * encounters a video keyframe, then it starts sending every frame again */
  int                  waiting_for_keyframe;

  qt_info             *qt;
  xine_bmiheader       bih;
  unsigned int         current_frame;
  unsigned int         last_frame;

  off_t                data_start;
  off_t                data_size;

  char                 last_mrl[1024];
} demux_qt_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_qt_class_t;

/**********************************************************************
 * lazyqt special debugging functions
 **********************************************************************/

/* define DEBUG_ATOM_LOAD as 1 to get a verbose parsing of the relevant 
 * atoms */
#define DEBUG_ATOM_LOAD 0

/* define DEBUG_EDIT_LIST as 1 to get a detailed look at how the demuxer is
 * handling edit lists */
#define DEBUG_EDIT_LIST 0

/* define DEBUG_FRAME_TABLE as 1 to dump the complete frame table that the
 * demuxer plans to use during file playback */
#define DEBUG_FRAME_TABLE 0

/* define DEBUG_VIDEO_DEMUX as 1 to see details about the video chunks the
 * demuxer is sending off to the video decoder */
#define DEBUG_VIDEO_DEMUX 0

/* define DEBUG_AUDIO_DEMUX as 1 to see details about the audio chunks the
 * demuxer is sending off to the audio decoder */
#define DEBUG_AUDIO_DEMUX 0

/* Define DEBUG_DUMP_MOOV as 1 to dump the raw moov atom to disk. This is
 * particularly useful in debugging a file with a compressed moov (cmov)
 * atom. The atom will be dumped to the filename specified as 
 * RAW_MOOV_FILENAME. */
#define DEBUG_DUMP_MOOV 0
#define RAW_MOOV_FILENAME "moovatom.raw"

#if DEBUG_ATOM_LOAD
#define debug_atom_load printf
#else
static inline void debug_atom_load(const char *format, ...) { }
#endif

#if DEBUG_EDIT_LIST
#define debug_edit_list printf
#else
static inline void debug_edit_list(const char *format, ...) { }
#endif

#if DEBUG_FRAME_TABLE
#define debug_frame_table printf
#else
static inline void debug_frame_table(const char *format, ...) { }
#endif

#if DEBUG_VIDEO_DEMUX
#define debug_video_demux printf
#else
static inline void debug_video_demux(const char *format, ...) { }
#endif

#if DEBUG_AUDIO_DEMUX
#define debug_audio_demux printf
#else
static inline void debug_audio_demux(const char *format, ...) { }
#endif

void dump_moov_atom(unsigned char *moov_atom, int moov_atom_size) {
#if DEBUG_DUMP_MOOV

  FILE *f;

  f = fopen(RAW_MOOV_FILENAME, "w");
  if (!f) {
    perror(RAW_MOOV_FILENAME);
    return;
  }

  if (fwrite(moov_atom, moov_atom_size, 1, f) != 1)
    printf ("  qt debug: could not write moov atom to disk\n");

  fclose(f);

#endif
}

/**********************************************************************
 * lazyqt functions
 **********************************************************************/

/*
 * This function traverses a file and looks for a moov atom. Returns the
 * file offset of the beginning of the moov atom (that means the offset
 * of the 4-byte length preceding the characters 'moov'). Returns -1
 * if no moov atom was found.
 *
 * Note: Do not count on the input stream being positioned anywhere in
 * particular when this function is finished.
 */
static void find_moov_atom(input_plugin_t *input, off_t *moov_offset,
  int64_t *moov_size) {

  off_t atom_size;
  qt_atom atom;
  unsigned char atom_preamble[ATOM_PREAMBLE_SIZE];

  /* init the passed variables */
  *moov_offset = *moov_size = -1;

  /* take it from the top */
  if (input->seek(input, 0, SEEK_SET) != 0)
    return;

  /* traverse through the input */
  while (*moov_offset == -1) {
    if (input->read(input, atom_preamble, ATOM_PREAMBLE_SIZE) !=
      ATOM_PREAMBLE_SIZE)
      break;

    atom_size = BE_32(&atom_preamble[0]);
    atom = BE_32(&atom_preamble[4]);

    /* if the moov atom is found, log the position and break from the loop */
    if (atom == MOOV_ATOM) {
      *moov_offset = input->get_current_pos(input) - ATOM_PREAMBLE_SIZE;
      *moov_size = atom_size;
      break;
    }

    /* if this atom is not the moov atom, make sure that it is at least one
     * of the other top-level QT atom */
    if ((atom != FREE_ATOM) &&
        (atom != JUNK_ATOM) &&
        (atom != MDAT_ATOM) &&
        (atom != PNOT_ATOM) &&
        (atom != SKIP_ATOM) &&
        (atom != WIDE_ATOM) &&
        (atom != PICT_ATOM))
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

/* create a qt_info structure or return NULL if no memory */
qt_info *create_qt_info(void) {
  qt_info *info;

  info = (qt_info *)malloc(sizeof(qt_info));

  if (!info)
    return NULL;

  info->frames = NULL;
  info->qt_file = NULL;
  info->compressed_header = 0;

  info->creation_time = 0;
  info->modification_time = 0;
  info->timescale = 0;
  info->duration = 0;

  info->audio_codec = 0;
  info->audio_type = 0;
  info->audio_sample_rate = 0;
  info->audio_channels = 0;
  info->audio_bits = 0;
  info->audio_vbr = 0;
  info->audio_decoder_config = NULL;
  info->audio_decoder_config_len = 0;

  info->video_codec = 0;
  info->video_type = 0;
  info->video_width = 0;
  info->video_height = 0;
  info->video_depth = 0;
  info->video_decoder_config = NULL;
  info->video_decoder_config_len = 0;

  info->frames = NULL;
  info->frame_count = 0;

  info->palette_count = 0;

  info->copyright = NULL;
  info->description = NULL;
  info->comment = NULL;

  info->last_error = QT_OK;

  return info;
}

/* release a qt_info structure and associated data */
void free_qt_info(qt_info *info) {

  if(info) {
    if(info->frames)
      free(info->frames);
    if(info->audio_decoder_config)
      free(info->audio_decoder_config);
    if(info->video_decoder_config)
      free(info->video_decoder_config);
    free(info->copyright);
    free(info->description);
    free(info->comment);
    free(info);
    info = NULL;
  }
}

/* returns 1 if the file is determined to be a QT file, 0 otherwise */
static int is_qt_file(input_plugin_t *qt_file) {

  off_t moov_atom_offset = -1;
  int64_t moov_atom_size = -1;
  int i;
  unsigned char atom_preamble[ATOM_PREAMBLE_SIZE];

  find_moov_atom(qt_file, &moov_atom_offset, &moov_atom_size);
  if (moov_atom_offset == -1) {
    return 0;
  } else {
    /* check that the next atom in the chunk contains alphanumeric
     * characters in the atom type field; if not, disqualify the file 
     * as a QT file */
    qt_file->seek(qt_file, moov_atom_offset + ATOM_PREAMBLE_SIZE, SEEK_SET);
    if (qt_file->read(qt_file, atom_preamble, ATOM_PREAMBLE_SIZE) !=
      ATOM_PREAMBLE_SIZE)
      return 0;

    for (i = 4; i < 8; i++)
      if (!isalnum(atom_preamble[i]))
        return 0;
    return 1;
  }
}

/* fetch interesting information from the movie header atom */
static void parse_mvhd_atom(qt_info *info, unsigned char *mvhd_atom) {

  info->creation_time = BE_32(&mvhd_atom[0x0C]);
  info->modification_time = BE_32(&mvhd_atom[0x10]);
  info->timescale = BE_32(&mvhd_atom[0x14]);
  info->duration = BE_32(&mvhd_atom[0x18]);

}

/* helper function from mplayer's parse_mp4.c */
static int mp4_read_descr_len(unsigned char *s, uint32_t *length) {
  uint8_t b;
  uint8_t numBytes = 0;
  
  *length = 0;

  do {
    b = *s++;
    numBytes++;
    *length = (*length << 7) | (b & 0x7F);
  } while ((b & 0x80) && numBytes < 4);

  return numBytes;
}


/*
 * This function traverses through a trak atom searching for the sample
 * table atoms, which it loads into an internal sample table structure.
 */
static qt_error parse_trak_atom(qt_sample_table *sample_table,
  unsigned char *trak_atom) {

  int i, j;
  unsigned int trak_atom_size = BE_32(&trak_atom[0]);
  qt_atom current_atom;
  qt_error last_error = QT_OK;

  /* for palette traversal */
  int color_depth;
  int color_flag;
  int color_start;
  int color_count;
  int color_end;
  int color_index;
  int color_dec;
  int color_greyscale;
  unsigned char *color_table;

  /* initialize sample table structure */
  sample_table->edit_list_count = 0;
  sample_table->edit_list_table = NULL;
  sample_table->chunk_offset_count = 0;
  sample_table->chunk_offset_table = NULL;
  sample_table->sample_size = 0;
  sample_table->sample_size_count = 0;
  sample_table->sample_size_table = NULL;
  sample_table->sync_sample_table = 0;
  sample_table->sync_sample_table = NULL;
  sample_table->sample_to_chunk_count = 0;
  sample_table->sample_to_chunk_table = NULL;
  sample_table->time_to_sample_count = 0;
  sample_table->time_to_sample_table = NULL;
  sample_table->frames = NULL;
  sample_table->decoder_config = NULL;
  sample_table->decoder_config_len = 0;

  /* special audio parameters */
  sample_table->samples_per_packet = 0;
  sample_table->bytes_per_packet = 0;
  sample_table->bytes_per_frame = 0;
  sample_table->bytes_per_sample = 0;
  sample_table->samples_per_frame = 0;

  /* default type */
  sample_table->type = MEDIA_OTHER;

  /* video size and depth not yet known */
  sample_table->media_description.video.width = 0;
  sample_table->media_description.video.height = 0;
  sample_table->media_description.video.depth = 0;

  /* assume no palette at first */
  sample_table->media_description.video.palette_count = 0;

  /* search for media type atoms */
  for (i = ATOM_PREAMBLE_SIZE; i < trak_atom_size - 4; i++) {
    current_atom = BE_32(&trak_atom[i]);

    if (current_atom == VMHD_ATOM) {
      sample_table->type = MEDIA_VIDEO;
      break;
    } else if (current_atom == SMHD_ATOM) {
      sample_table->type = MEDIA_AUDIO;
      break;
    }
  }
  
  debug_atom_load("  qt: parsing %s trak atom\n",
    (sample_table->type == MEDIA_VIDEO) ? "video" :
      (sample_table->type == MEDIA_AUDIO) ? "audio" : "other");

  /* search for the useful atoms */
  for (i = ATOM_PREAMBLE_SIZE; i < trak_atom_size - 4; i++) {
    current_atom = BE_32(&trak_atom[i]);

    if (current_atom == TKHD_ATOM) {
      sample_table->flags = BE_16(&trak_atom[i + 6]);

      if (sample_table->type == MEDIA_VIDEO) {
        /* fetch display parameters */
        if( !sample_table->media_description.video.width ||
            !sample_table->media_description.video.height ) {

          sample_table->media_description.video.width =
            BE_16(&trak_atom[i + 0x50]);
          sample_table->media_description.video.height =
            BE_16(&trak_atom[i + 0x54]); 
        }
      }
    } else if (current_atom == ELST_ATOM) {

      /* there should only be one edit list table */
      if (sample_table->edit_list_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->edit_list_count = BE_32(&trak_atom[i + 8]);

      debug_atom_load("    qt elst atom (edit list atom): %d entries\n",
        sample_table->edit_list_count);

      sample_table->edit_list_table = (edit_list_table_t *)malloc(
        sample_table->edit_list_count * sizeof(edit_list_table_t));
      if (!sample_table->edit_list_table) {
        last_error = QT_NO_MEMORY;
        goto free_sample_table;
      }

      /* load the edit list table */
      for (j = 0; j < sample_table->edit_list_count; j++) {
        sample_table->edit_list_table[j].track_duration =
          BE_32(&trak_atom[i + 12 + j * 12 + 0]);
        sample_table->edit_list_table[j].media_time =
          BE_32(&trak_atom[i + 12 + j * 12 + 4]);
        debug_atom_load("      %d: track duration = %d, media time = %d\n",
          j,
          sample_table->edit_list_table[j].track_duration,
          sample_table->edit_list_table[j].media_time);
      }

    } else if (current_atom == MDHD_ATOM)
      sample_table->timescale = BE_32(&trak_atom[i + 0x10]);
    else if (current_atom == STSD_ATOM) {

      if (sample_table->type == MEDIA_VIDEO) {

        /* fetch video parameters */
        if( BE_16(&trak_atom[i + 0x2C]) && BE_16(&trak_atom[i + 0x2E]) ) {
          sample_table->media_description.video.width =
            BE_16(&trak_atom[i + 0x2C]);
          sample_table->media_description.video.height =
            BE_16(&trak_atom[i + 0x2E]);
        }
        sample_table->media_description.video.codec_format =
          ME_32(&trak_atom[i + 0x10]);

        /* figure out the palette situation */
        color_depth = trak_atom[i + 0x5F];
        sample_table->media_description.video.depth = color_depth;
        color_greyscale = color_depth & 0x20;
        color_depth &= 0x1F;

        /* if the depth is 2, 4, or 8 bpp, file is palettized */
        if ((color_depth == 2) || (color_depth == 4) || (color_depth == 8)) {

          color_flag = BE_16(&trak_atom[i + 0x60]);

          if (color_greyscale) {

            sample_table->media_description.video.palette_count =
              1 << color_depth;

            /* compute the greyscale palette */
            color_index = 255;
            color_dec = 256 / 
              (sample_table->media_description.video.palette_count - 1);
            for (j = 0; 
                 j < sample_table->media_description.video.palette_count;
                 j++) {

              sample_table->media_description.video.palette[j].r = color_index;
              sample_table->media_description.video.palette[j].g = color_index;
              sample_table->media_description.video.palette[j].b = color_index;
              color_index -= color_dec;
              if (color_index < 0)
                color_index = 0;
            }

          } else if (color_flag & 0x08) {

            /* if flag bit 3 is set, load the default palette */
            sample_table->media_description.video.palette_count =
              1 << color_depth;

            if (color_depth == 2)
              color_table = qt_default_palette_4;
            else if (color_depth == 4)
              color_table = qt_default_palette_16;
            else
              color_table = qt_default_palette_256;

            for (j = 0; 
              j < sample_table->media_description.video.palette_count;
              j++) {

              sample_table->media_description.video.palette[j].r =
                color_table[j * 4 + 0];
              sample_table->media_description.video.palette[j].g =
                color_table[j * 4 + 1];
              sample_table->media_description.video.palette[j].b =
                color_table[j * 4 + 2];

            }

          } else {

            /* load the palette from the file */
            color_start = BE_32(&trak_atom[i + 0x62]);
            color_count = BE_16(&trak_atom[i + 0x66]);
            color_end = BE_16(&trak_atom[i + 0x68]);
            sample_table->media_description.video.palette_count =
              color_end + 1;

            for (j = color_start; j <= color_end; j++) {

              color_index = BE_16(&trak_atom[i + 0x6A + j * 8]);
              if (color_count & 0x8000)
                color_index = j;
              if (color_index < 
                sample_table->media_description.video.palette_count) {
                sample_table->media_description.video.palette[color_index].r =
                  trak_atom[i + 0x6A + j * 8 + 2];
                sample_table->media_description.video.palette[color_index].g =
                  trak_atom[i + 0x6A + j * 8 + 4];
                sample_table->media_description.video.palette[color_index].b =
                  trak_atom[i + 0x6A + j * 8 + 6];
              }
            }
          }
        } else
          sample_table->media_description.video.palette_count = 0;

        debug_atom_load("    video description\n");
        debug_atom_load("      %dx%d, video fourcc = '%c%c%c%c' (%02X%02X%02X%02X)\n",
          sample_table->media_description.video.width,
          sample_table->media_description.video.height,
          trak_atom[i + 0x10],
          trak_atom[i + 0x11],
          trak_atom[i + 0x12],
          trak_atom[i + 0x13],
          trak_atom[i + 0x10],
          trak_atom[i + 0x11],
          trak_atom[i + 0x12],
          trak_atom[i + 0x13]);
        debug_atom_load("      %d RGB colors\n",
          sample_table->media_description.video.palette_count);
        for (j = 0; j < sample_table->media_description.video.palette_count;
             j++)
          debug_atom_load("        %d: %3d %3d %3d\n",
            j,
            sample_table->media_description.video.palette[j].r,
            sample_table->media_description.video.palette[j].g,
            sample_table->media_description.video.palette[j].b);

      } else if (sample_table->type == MEDIA_AUDIO) {

        /* fetch audio parameters */
        sample_table->media_description.audio.codec_format =
	  ME_32(&trak_atom[i + 0x10]);
        sample_table->media_description.audio.sample_rate =
          BE_16(&trak_atom[i + 0x2C]);
        sample_table->media_description.audio.channels = trak_atom[i + 0x25];
        sample_table->media_description.audio.bits = trak_atom[i + 0x27];

        /* assume uncompressed audio parameters */
        sample_table->bytes_per_sample =
          sample_table->media_description.audio.bits / 8;
        sample_table->samples_per_frame =
          sample_table->media_description.audio.channels;
        sample_table->bytes_per_frame = 
          sample_table->bytes_per_sample * sample_table->samples_per_frame;
        sample_table->samples_per_packet = sample_table->samples_per_frame;
        sample_table->bytes_per_packet = sample_table->bytes_per_sample;

        /* special case time: some ima4-encoded files don't have the
         * extra header; compensate */
        if (BE_32(&trak_atom[i + 0x10]) == IMA4_FOURCC) {
          sample_table->samples_per_packet = 64;
          sample_table->bytes_per_packet = 34;
          sample_table->bytes_per_frame = 34 * 
            sample_table->media_description.audio.channels;
          sample_table->bytes_per_sample = 2;
          sample_table->samples_per_frame = 64 *
            sample_table->media_description.audio.channels;
        }

        /* it's time to dig a little deeper to determine the real audio
         * properties; if a the stsd compressor atom has 0x24 bytes, it
         * appears to be a handler for uncompressed data; if there are an
         * extra 0x10 bytes, there are some more useful decoding params */
        if (BE_32(&trak_atom[i + 0x0C]) > 0x24) {

          if (BE_32(&trak_atom[i + 0x30]))
            sample_table->samples_per_packet = BE_32(&trak_atom[i + 0x30]);
          if (BE_32(&trak_atom[i + 0x34]))
            sample_table->bytes_per_packet = BE_32(&trak_atom[i + 0x34]);
          if (BE_32(&trak_atom[i + 0x38]))
            sample_table->bytes_per_frame = BE_32(&trak_atom[i + 0x38]);
          if (BE_32(&trak_atom[i + 0x3C]))
            sample_table->bytes_per_sample = BE_32(&trak_atom[i + 0x3C]);
          sample_table->samples_per_frame =
            (sample_table->bytes_per_frame / sample_table->bytes_per_packet) *
            sample_table->samples_per_packet;

        }

        /* see if the trak deserves a promotion to VBR */
        if (BE_16(&trak_atom[i + 0x28]) == 0xFFFE)
          sample_table->media_description.audio.vbr = 1;
        else
          sample_table->media_description.audio.vbr = 0;

        /* if this is MP4 audio, mark it as VBR */
        if (BE_32(&trak_atom[i + 0x10]) == MP4A_FOURCC)
          sample_table->media_description.audio.vbr = 1;

        debug_atom_load("    audio description\n");
        debug_atom_load("      %d Hz, %d bits, %d channels, %saudio fourcc = '%c%c%c%c' (%02X%02X%02X%02X)\n",
          sample_table->media_description.audio.sample_rate,
          sample_table->media_description.audio.bits,
          sample_table->media_description.audio.channels,
          (sample_table->media_description.audio.vbr) ? "vbr, " : "",
          trak_atom[i + 0x10],
          trak_atom[i + 0x11],
          trak_atom[i + 0x12],
          trak_atom[i + 0x13],
          trak_atom[i + 0x10],
          trak_atom[i + 0x11],
          trak_atom[i + 0x12],
          trak_atom[i + 0x13]);
        if (BE_32(&trak_atom[i + 0x0C]) > 0x24) {
          debug_atom_load("      %d samples/packet, %d bytes/packet, %d bytes/frame\n",
            sample_table->samples_per_packet,
            sample_table->bytes_per_packet,
            sample_table->bytes_per_frame);
          debug_atom_load("      %d bytes/sample (%d samples/frame)\n",
            sample_table->bytes_per_sample,
            sample_table->samples_per_frame);
        }
      }

    } else if (current_atom == ESDS_ATOM) {

      uint32_t len;
      
      debug_atom_load("    qt/mpeg-4 esds atom\n");

      if ((sample_table->type == MEDIA_VIDEO) || 
          (sample_table->type == MEDIA_AUDIO)) {
        
        j = i + 8;
        if( trak_atom[j++] == 0x03 ) {
          j += mp4_read_descr_len( &trak_atom[j], &len );
          j++;
        }
        j += 2;
        if( trak_atom[j++] == 0x04 ) {
          j += mp4_read_descr_len( &trak_atom[j], &len );
          j += 13;
          if( trak_atom[j++] == 0x05 ) {
            j += mp4_read_descr_len( &trak_atom[j], &len );
            debug_atom_load("      decoder config is %d (0x%X) bytes long\n",
              len, len);
            sample_table->decoder_config = malloc(len);
            sample_table->decoder_config_len = len;
            memcpy(sample_table->decoder_config,&trak_atom[j],len);
          }
        }
      }

    } else if (current_atom == STSZ_ATOM) {

      /* there should only be one of these atoms */
      if (sample_table->sample_size_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->sample_size = BE_32(&trak_atom[i + 8]);
      sample_table->sample_size_count = BE_32(&trak_atom[i + 12]);

      debug_atom_load("    qt stsz atom (sample size atom): sample size = %d, %d entries\n",
        sample_table->sample_size, sample_table->sample_size_count);

      /* allocate space and load table only if sample size is 0 */
      if (sample_table->sample_size == 0) {
        sample_table->sample_size_table = (unsigned int *)malloc(
          sample_table->sample_size_count * sizeof(unsigned int));
        if (!sample_table->sample_size_table) {
          last_error = QT_NO_MEMORY;
          goto free_sample_table;
        }
        /* load the sample size table */
        for (j = 0; j < sample_table->sample_size_count; j++) {
          sample_table->sample_size_table[j] =
            BE_32(&trak_atom[i + 16 + j * 4]);
          debug_atom_load("      sample size %d: %d\n",
            j, sample_table->sample_size_table[j]);
        }
      } else
        /* set the pointer to non-NULL to indicate that the atom type has
         * already been seen for this trak atom */
        sample_table->sample_size_table = (void *)-1;

    } else if (current_atom == STSS_ATOM) {

      /* there should only be one of these atoms */
      if (sample_table->sync_sample_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->sync_sample_count = BE_32(&trak_atom[i + 8]);

      debug_atom_load("    qt stss atom (sample sync atom): %d sync samples\n",
        sample_table->sync_sample_count);

      sample_table->sync_sample_table = (unsigned int *)malloc(
        sample_table->sync_sample_count * sizeof(unsigned int));
      if (!sample_table->sync_sample_table) {
        last_error = QT_NO_MEMORY;
        goto free_sample_table;
      }

      /* load the sync sample table */
      for (j = 0; j < sample_table->sync_sample_count; j++) {
        sample_table->sync_sample_table[j] =
          BE_32(&trak_atom[i + 12 + j * 4]);
        debug_atom_load("      sync sample %d: sample %d (%d) is a keyframe\n",
          j, sample_table->sync_sample_table[j],
          sample_table->sync_sample_table[j] - 1);
      }

    } else if (current_atom == STCO_ATOM) {

      /* there should only be one of either stco or co64 */
      if (sample_table->chunk_offset_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->chunk_offset_count = BE_32(&trak_atom[i + 8]);

      debug_atom_load("    qt stco atom (32-bit chunk offset atom): %d chunk offsets\n",
        sample_table->chunk_offset_count);

      sample_table->chunk_offset_table = (int64_t *)malloc(
        sample_table->chunk_offset_count * sizeof(int64_t));
      if (!sample_table->chunk_offset_table) {
        last_error = QT_NO_MEMORY;
        goto free_sample_table;
      }

      /* load the chunk offset table */
      for (j = 0; j < sample_table->chunk_offset_count; j++) {
        sample_table->chunk_offset_table[j] =
          BE_32(&trak_atom[i + 12 + j * 4]);
        debug_atom_load("      chunk %d @ 0x%llX\n",
          j, sample_table->chunk_offset_table[j]);
      }

    } else if (current_atom == CO64_ATOM) {

      /* there should only be one of either stco or co64 */
      if (sample_table->chunk_offset_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->chunk_offset_count = BE_32(&trak_atom[i + 8]);

      debug_atom_load("    qt co64 atom (64-bit chunk offset atom): %d chunk offsets\n",
        sample_table->chunk_offset_count);

      sample_table->chunk_offset_table = (int64_t *)malloc(
        sample_table->chunk_offset_count * sizeof(int64_t));
      if (!sample_table->chunk_offset_table) {
        last_error = QT_NO_MEMORY;
        goto free_sample_table;
      }

      /* load the 64-bit chunk offset table */
      for (j = 0; j < sample_table->chunk_offset_count; j++) {
        sample_table->chunk_offset_table[j] =
          BE_32(&trak_atom[i + 12 + j * 8 + 0]);
        sample_table->chunk_offset_table[j] <<= 32;
        sample_table->chunk_offset_table[j] |=
          BE_32(&trak_atom[i + 12 + j * 8 + 4]);
        debug_atom_load("      chunk %d @ 0x%llX\n",
          j, sample_table->chunk_offset_table[j]);
      }

    } else if (current_atom == STSC_ATOM) {

      /* there should only be one of these atoms */
      if (sample_table->sample_to_chunk_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->sample_to_chunk_count = BE_32(&trak_atom[i + 8]);

      debug_atom_load("    qt stsc atom (sample-to-chunk atom): %d entries\n",
        sample_table->sample_to_chunk_count);

      sample_table->sample_to_chunk_table = (sample_to_chunk_table_t *)malloc(
        sample_table->sample_to_chunk_count * sizeof(sample_to_chunk_table_t));
      if (!sample_table->sample_to_chunk_table) {
        last_error = QT_NO_MEMORY;
        goto free_sample_table;
      }

      /* load the sample to chunk table */
      for (j = 0; j < sample_table->sample_to_chunk_count; j++) {
        sample_table->sample_to_chunk_table[j].first_chunk =
          BE_32(&trak_atom[i + 12 + j * 12 + 0]);
        sample_table->sample_to_chunk_table[j].samples_per_chunk =
          BE_32(&trak_atom[i + 12 + j * 12 + 4]);
        debug_atom_load("      %d: %d samples/chunk starting at chunk %d (%d)\n",
          j, sample_table->sample_to_chunk_table[j].samples_per_chunk,
          sample_table->sample_to_chunk_table[j].first_chunk,
          sample_table->sample_to_chunk_table[j].first_chunk - 1);
      }

    } else if (current_atom == STTS_ATOM) {

      /* there should only be one of these atoms */
      if (sample_table->time_to_sample_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->time_to_sample_count = BE_32(&trak_atom[i + 8]);

      debug_atom_load("    qt stts atom (time-to-sample atom): %d entries\n",
        sample_table->time_to_sample_count);

      sample_table->time_to_sample_table = (time_to_sample_table_t *)malloc(
        sample_table->time_to_sample_count * sizeof(time_to_sample_table_t));
      if (!sample_table->time_to_sample_table) {
        last_error = QT_NO_MEMORY;
        goto free_sample_table;
      }

      /* load the time to sample table */
      for (j = 0; j < sample_table->time_to_sample_count; j++) {
        sample_table->time_to_sample_table[j].count =
          BE_32(&trak_atom[i + 12 + j * 8 + 0]);
        sample_table->time_to_sample_table[j].duration =
          BE_32(&trak_atom[i + 12 + j * 8 + 4]);
        debug_atom_load("      %d: count = %d, duration = %d\n",
          j, sample_table->time_to_sample_table[j].count,
          sample_table->time_to_sample_table[j].duration);
      }
    }
  }

  return QT_OK;

  /* jump here to make sure everything is free'd and avoid leaking memory */
free_sample_table:
  free(sample_table->edit_list_table);
  free(sample_table->chunk_offset_table);
  /* this pointer might have been set to -1 as a special case */
  if (sample_table->sample_size_table != (void *)-1)
    free(sample_table->sample_size_table);
  free(sample_table->sync_sample_table);
  free(sample_table->sample_to_chunk_table);
  free(sample_table->time_to_sample_table);
  free(sample_table->decoder_config);

  return last_error;
}

static qt_error build_frame_table(qt_sample_table *sample_table,
  unsigned int global_timescale) {

  int i, j;
  unsigned int frame_counter;
  unsigned int chunk_start, chunk_end;
  unsigned int samples_per_chunk;
  uint64_t current_offset;
  int64_t current_pts;
  unsigned int pts_index;
  unsigned int pts_index_countdown;
  unsigned int audio_frame_counter = 0;
  unsigned int edit_list_media_time;
  int64_t edit_list_duration;
  int64_t frame_duration = 0;
  unsigned int edit_list_index;
  unsigned int edit_list_pts_counter;

  /* AUDIO and OTHER frame types follow the same rules; VIDEO and vbr audio
   * frame types follow a different set */
  if ((sample_table->type == MEDIA_VIDEO) || 
      (sample_table->media_description.audio.vbr)) {

    /* in this case, the total number of frames is equal to the number of
     * entries in the sample size table */
    sample_table->frame_count = sample_table->sample_size_count;
    sample_table->frames = (qt_frame *)malloc(
      sample_table->frame_count * sizeof(qt_frame));
    if (!sample_table->frames)
      return QT_NO_MEMORY;

    /* initialize more accounting variables */
    frame_counter = 0;
    current_pts = 0;
    pts_index = 0;
    pts_index_countdown =
      sample_table->time_to_sample_table[pts_index].count;

    /* iterate through each start chunk in the stsc table */
    for (i = 0; i < sample_table->sample_to_chunk_count; i++) {
      /* iterate from the first chunk of the current table entry to
       * the first chunk of the next table entry */
      chunk_start = sample_table->sample_to_chunk_table[i].first_chunk;
      if (i < sample_table->sample_to_chunk_count - 1)
        chunk_end =
          sample_table->sample_to_chunk_table[i + 1].first_chunk;
      else
        /* if the first chunk is in the last table entry, iterate to the
           final chunk number (the number of offsets in stco table) */
        chunk_end = sample_table->chunk_offset_count + 1;

      /* iterate through each sample in a chunk */
      for (j = chunk_start - 1; j < chunk_end - 1; j++) {

        samples_per_chunk =
          sample_table->sample_to_chunk_table[i].samples_per_chunk;
        current_offset = sample_table->chunk_offset_table[j];
        while (samples_per_chunk > 0) {
          sample_table->frames[frame_counter].type = sample_table->type;

          /* figure out the offset and size */
          sample_table->frames[frame_counter].offset = current_offset;
          if (sample_table->sample_size) {
            sample_table->frames[frame_counter].size =
              sample_table->sample_size;
            current_offset += sample_table->sample_size;
          } else {
            sample_table->frames[frame_counter].size =
              sample_table->sample_size_table[frame_counter];
            current_offset +=
              sample_table->sample_size_table[frame_counter];
          }

          /* if there is no stss (sample sync) table, make all of the frames
           * keyframes; otherwise, clear the keyframe bits for now */
          if (sample_table->sync_sample_table)
            sample_table->frames[frame_counter].keyframe = 0;
          else
            sample_table->frames[frame_counter].keyframe = 1;

          /* figure out the pts situation */
          sample_table->frames[frame_counter].pts = current_pts;
          current_pts +=
            sample_table->time_to_sample_table[pts_index].duration;
          pts_index_countdown--;
          /* time to refresh countdown? */
          if (!pts_index_countdown) {
            pts_index++;
            pts_index_countdown =
              sample_table->time_to_sample_table[pts_index].count;
          }

          samples_per_chunk--;
          frame_counter++;
        }
      }
    }

    /* fill in the keyframe information */
    if (sample_table->sync_sample_table) {
      for (i = 0; i < sample_table->sync_sample_count; i++)
        sample_table->frames[sample_table->sync_sample_table[i] - 1].keyframe = 1;
    }

    /* initialize edit list considerations */
    edit_list_index = 0;
    if (sample_table->edit_list_table) {
      edit_list_media_time = 
        sample_table->edit_list_table[edit_list_index].media_time;
      edit_list_duration = 
        sample_table->edit_list_table[edit_list_index].track_duration;

      /* duration is in global timescale units; convert to trak timescale */
      edit_list_duration *= sample_table->timescale;
      edit_list_duration /= global_timescale;

      edit_list_index++;
      /* if this is the last edit list entry, don't let the duration
       * expire (so set it to an absurdly large value) */
      if (edit_list_index == sample_table->edit_list_count)
        edit_list_duration = 0xFFFFFFFFFFFF;
      debug_edit_list("  qt: edit list table exists, initial = %d, %lld\n", edit_list_media_time, edit_list_duration);
    } else {
      edit_list_media_time = 0;
      edit_list_duration = 0xFFFFFFFFFFFF;
      debug_edit_list("  qt: no edit list table, initial = %d, %lld\n", edit_list_media_time, edit_list_duration);
    }

    /* fix up pts information w.r.t. the edit list table */
    edit_list_pts_counter = 0;
    for (i = 0; i < sample_table->frame_count; i++) {

      debug_edit_list("    %d: (before) pts = %lld...", i, sample_table->frames[i].pts);

      if (sample_table->frames[i].pts < edit_list_media_time) 
        sample_table->frames[i].pts = edit_list_pts_counter;
      else {
        if (i < sample_table->frame_count - 1)
          frame_duration = 
            (sample_table->frames[i + 1].pts - sample_table->frames[i].pts);

            debug_edit_list("duration = %lld...", frame_duration);
        sample_table->frames[i].pts = edit_list_pts_counter;
        edit_list_pts_counter += frame_duration;
        edit_list_duration -= frame_duration;
      }

      debug_edit_list("(fixup) pts = %lld...", sample_table->frames[i].pts);

      /* reload media time and duration */
      if (edit_list_duration <= 0) {
        if ((sample_table->edit_list_table) &&
            (edit_list_index < sample_table->edit_list_count)) {
          debug_edit_list("\n  edit list index = %d, ", edit_list_index);
          edit_list_media_time = 
            sample_table->edit_list_table[edit_list_index].media_time;
          edit_list_duration = 
            sample_table->edit_list_table[edit_list_index].track_duration;

          /* duration is in global timescale units; convert to trak timescale */
          edit_list_duration *= sample_table->timescale;
          edit_list_duration /= global_timescale;

          edit_list_index++;
          /* if this is the last edit list entry, don't let the duration
           * expire (so set it to an absurdly large value) */
          if (edit_list_index == sample_table->edit_list_count)
            edit_list_duration = 0xFFFFFFFFFFFF;
          debug_edit_list("entry: %d, %lld\n      ", edit_list_media_time, edit_list_duration);
        } else {
          edit_list_media_time = 0;
          edit_list_duration = 0xFFFFFFFFFFFF;
          debug_edit_list("no edit list table (or expired): %d, %lld\n", edit_list_media_time, edit_list_duration);
        }
      }

      debug_edit_list("(after) pts = %lld...\n", sample_table->frames[i].pts);
    }

    /* compute final pts values */
    for (i = 0; i < sample_table->frame_count; i++) {
      sample_table->frames[i].pts *= 90000;
      sample_table->frames[i].pts /= sample_table->timescale;
      debug_edit_list("  final pts for sample %d = %lld\n", i, sample_table->frames[i].pts);
    }

  } else {

    /* in this case, the total number of frames is equal to the number of
     * chunks */
    sample_table->frame_count = sample_table->chunk_offset_count;
    sample_table->frames = (qt_frame *)malloc(
      sample_table->frame_count * sizeof(qt_frame));
    if (!sample_table->frames)
      return QT_NO_MEMORY;

    if (sample_table->type == MEDIA_AUDIO) {
      /* iterate through each start chunk in the stsc table */
      for (i = 0; i < sample_table->sample_to_chunk_count; i++) {
        /* iterate from the first chunk of the current table entry to
         * the first chunk of the next table entry */
        chunk_start = sample_table->sample_to_chunk_table[i].first_chunk;
        if (i < sample_table->sample_to_chunk_count - 1)
          chunk_end =
            sample_table->sample_to_chunk_table[i + 1].first_chunk;
        else
          /* if the first chunk is in the last table entry, iterate to the
             final chunk number (the number of offsets in stco table) */
          chunk_end = sample_table->chunk_offset_count + 1;

        /* iterate through each sample in a chunk and fill in size and
         * pts information */
        for (j = chunk_start - 1; j < chunk_end - 1; j++) {

          /* figure out the pts for this chunk */
          sample_table->frames[j].pts = audio_frame_counter;
          sample_table->frames[j].pts *= 90000;
          sample_table->frames[j].pts /= sample_table->timescale;

          /* fetch the alleged chunk size according to the QT header */
          sample_table->frames[j].size =
            sample_table->sample_to_chunk_table[i].samples_per_chunk;

          /* the chunk size is actually the audio frame count */
          audio_frame_counter += sample_table->frames[j].size;

          /* compute the actual chunk size */
          sample_table->frames[j].size =
            (sample_table->frames[j].size * 
             sample_table->media_description.audio.channels) /
             sample_table->samples_per_frame *
             sample_table->bytes_per_frame;
        }
      }
    }

    /* fill in the rest of the information for the audio samples */
    for (i = 0; i < sample_table->frame_count; i++) {
      sample_table->frames[i].type = sample_table->type;
      sample_table->frames[i].offset = sample_table->chunk_offset_table[i];
      sample_table->frames[i].keyframe = 0;
      if (sample_table->type != MEDIA_AUDIO)
        sample_table->frames[i].pts = 0;
    }
  }

  return QT_OK;
}


/*
 * This function takes a pointer to a qt_info structure and a pointer to
 * a buffer containing an uncompressed moov atom. When the function
 * finishes successfully, qt_info will have a list of qt_frame objects,
 * ordered by offset.
 */
static void parse_moov_atom(qt_info *info, unsigned char *moov_atom) {
  int i, j;
  unsigned int moov_atom_size = BE_32(&moov_atom[0]);
  unsigned int sample_table_count = 0;
  qt_sample_table *sample_tables = NULL;
  qt_atom current_atom;
  unsigned int *sample_table_indices;
  unsigned int min_offset_table;
  int64_t min_offset;
  int string_size;

  /* make sure this is actually a moov atom */
  if (BE_32(&moov_atom[4]) != MOOV_ATOM) {
    info->last_error = QT_NO_MOOV_ATOM;
    return;
  }

  /* prowl through the moov atom looking for very specific targets */
  for (i = ATOM_PREAMBLE_SIZE; i < moov_atom_size - 4; i++) {
    current_atom = BE_32(&moov_atom[i]);

    if (current_atom == MVHD_ATOM) {
      parse_mvhd_atom(info, &moov_atom[i - 4]);
      if (info->last_error != QT_OK)
        return;
      i += BE_32(&moov_atom[i - 4]) - 4;
    } else if (current_atom == TRAK_ATOM) {

      /* make a new sample temporary sample table */
      sample_table_count++;
      sample_tables = (qt_sample_table *)realloc(sample_tables,
        sample_table_count * sizeof(qt_sample_table));

      parse_trak_atom(&sample_tables[sample_table_count - 1],
        &moov_atom[i - 4]);
      if (info->last_error != QT_OK)
        return;
      i += BE_32(&moov_atom[i - 4]) - 4;

    } else if (current_atom == CPY_ATOM) {

      string_size = BE_16(&moov_atom[i + 4]) + 1;
      info->copyright = xine_xmalloc(string_size);
      strncpy(info->copyright, &moov_atom[i + 8], string_size - 1);
      info->copyright[string_size - 1] = 0;

    } else if (current_atom == DES_ATOM) {

      string_size = BE_16(&moov_atom[i + 4]) + 1;
      info->description = xine_xmalloc(string_size);
      strncpy(info->description, &moov_atom[i + 8], string_size - 1);
      info->description[string_size - 1] = 0;

    } else if (current_atom == CMT_ATOM) {

      string_size = BE_16(&moov_atom[i + 4]) + 1;
      info->comment = xine_xmalloc(string_size);
      strncpy(info->comment, &moov_atom[i + 8], string_size - 1);
      info->comment[string_size - 1] = 0;
    }
  }
  debug_atom_load("  qt: finished parsing moov atom\n");

  /* build frame tables corresponding to each sample table */
  info->frame_count = 0;
  debug_frame_table("  qt: preparing to build %d frame tables\n",
    sample_table_count);
  for (i = 0; i < sample_table_count; i++) {

    debug_frame_table("    qt: building frame table #%d\n", i);
    build_frame_table(&sample_tables[i], info->timescale);
    info->frame_count += sample_tables[i].frame_count;

    /* while traversing tables, look for A/V information */
    if (sample_tables[i].type == MEDIA_VIDEO) {

      info->video_width = sample_tables[i].media_description.video.width;
      info->video_height = sample_tables[i].media_description.video.height;
      info->video_depth = sample_tables[i].media_description.video.depth;
      info->video_codec =
        sample_tables[i].media_description.video.codec_format;
      
      /* fixme: there may be multiple trak with decoder_config. 
         i don't know if we should choose one or concatenate everything? */
      if( sample_tables[i].frame_count > 1 && sample_tables[i].decoder_config ) {
        info->video_decoder_config = sample_tables[i].decoder_config;
        info->video_decoder_config_len = sample_tables[i].decoder_config_len;
      }

      /* pass the palette info to the master qt_info structure */
      if (sample_tables[i].media_description.video.palette_count) {
        info->palette_count =
          sample_tables[i].media_description.video.palette_count;
        memcpy(info->palette,
          sample_tables[i].media_description.video.palette,
          PALETTE_COUNT * sizeof(palette_entry_t));
      }

    } else if (sample_tables[i].type == MEDIA_AUDIO) {

      info->audio_sample_rate =
        sample_tables[i].media_description.audio.sample_rate;
      info->audio_channels =
        sample_tables[i].media_description.audio.channels;
      info->audio_bits =
        sample_tables[i].media_description.audio.bits;
      info->audio_codec =
        sample_tables[i].media_description.audio.codec_format;
      info->audio_vbr = 
        sample_tables[i].media_description.audio.vbr;

      info->audio_decoder_config = sample_tables[i].decoder_config;
      info->audio_decoder_config_len = sample_tables[i].decoder_config_len;

      info->audio_sample_size_table = sample_tables[i].sample_size_table;
    }
  }
  debug_frame_table("  qt: finished building frame tables, merging into one...\n");

  /* allocate the master frame index */
  info->frames = (qt_frame *)malloc(info->frame_count * sizeof(qt_frame));
  if (!info->frames) {
    info->last_error = QT_NO_MEMORY;
    return;
  }

  /* allocate and zero out space for table indices */
  sample_table_indices = (unsigned int *)malloc(
    sample_table_count * sizeof(unsigned int));

  if (!sample_table_indices) {
    info->last_error = QT_NO_MEMORY;
    return;
  }
  for (i = 0; i < sample_table_count; i++) {
    if (sample_tables[i].frame_count == 0)
      sample_table_indices[i] = 0x7FFFFFF;
    else
      sample_table_indices[i] = 0;
  }

  /* merge the tables, order by frame offset */
  for (i = 0; i < info->frame_count; i++) {
    
    /* get the table number of the smallest frame offset */
    min_offset_table = -1;
    min_offset = 0; /* value not used (avoid compiler warnings) */
    for (j = 0; j < sample_table_count; j++) {
      if ((sample_table_indices[j] < info->frame_count) &&
          (min_offset_table == -1 ||
            (sample_tables[j].frames[sample_table_indices[j]].offset <
             min_offset) ) ) {
        min_offset_table = j;
        min_offset = sample_tables[j].frames[sample_table_indices[j]].offset;
      }
    }

    info->frames[i] =
      sample_tables[min_offset_table].frames[sample_table_indices[min_offset_table]];
    sample_table_indices[min_offset_table]++;
    if (sample_table_indices[min_offset_table] >=
      sample_tables[min_offset_table].frame_count)
        sample_table_indices[min_offset_table] = info->frame_count;
  }

  debug_frame_table("  qt: final frame table contains %d frames\n",
    info->frame_count);
  for (i = 0; i < info->frame_count; i++)
    debug_frame_table("    %d: %s frame @ offset 0x%llX, 0x%X bytes, pts = %lld%s\n",
    i,
    (info->frames[i].type == MEDIA_VIDEO) ? "video" :
      (info->frames[i].type == MEDIA_AUDIO) ? "audio" : "other",
    info->frames[i].offset,
    info->frames[i].size,
    info->frames[i].pts,
    (info->frames[i].keyframe) ? ", keyframe" : "");

  /* free the temporary tables on the way out */
  for (i = 0; i < sample_table_count; i++) {
    free(sample_tables[i].edit_list_table);
    free(sample_tables[i].chunk_offset_table);
    /* this pointer might have been set to -1 as a special case */
    if (sample_tables[i].sample_size_table != (void *)-1)
      free(sample_tables[i].sample_size_table);
    free(sample_tables[i].time_to_sample_table);
    free(sample_tables[i].sample_to_chunk_table);
    free(sample_tables[i].sync_sample_table);
    free(sample_tables[i].frames);
  }
  free(sample_tables);
  free(sample_table_indices);
}

static qt_error open_qt_file(qt_info *info, input_plugin_t *input) {

  unsigned char *moov_atom = NULL;
  off_t moov_atom_offset = -1;
  int64_t moov_atom_size = -1;

  /* zlib stuff */
  z_stream z_state;
  int z_ret_code;
  unsigned char *unzip_buffer;

  find_moov_atom(input, &moov_atom_offset, &moov_atom_size);
  if (moov_atom_offset == -1) {
    info->last_error = QT_NO_MOOV_ATOM;
    return info->last_error;
  }
  info->moov_first_offset = moov_atom_offset;
  info->moov_last_offset = info->moov_first_offset + moov_atom_size;

  moov_atom = (unsigned char *)malloc(moov_atom_size);
  if (moov_atom == NULL) {
    info->last_error = QT_NO_MEMORY;
    return info->last_error;
  }
  /* seek to the start of moov atom */
  if (input->seek(input, info->moov_first_offset, SEEK_SET) !=
    info->moov_first_offset) {
    free(moov_atom);
    info->last_error = QT_FILE_READ_ERROR;
    return info->last_error;
  }
  if (input->read(input, moov_atom, moov_atom_size) !=
    moov_atom_size) {
    free(moov_atom);
    info->last_error = QT_FILE_READ_ERROR;
    return info->last_error;
  }

  /* check if moov is compressed */
  if (BE_32(&moov_atom[12]) == CMOV_ATOM) {

    info->compressed_header = 1;

    z_state.next_in = &moov_atom[0x28];
    z_state.avail_in = moov_atom_size - 0x28;
    z_state.avail_out = BE_32(&moov_atom[0x24]);
    unzip_buffer = (unsigned char *)malloc(BE_32(&moov_atom[0x24]));
    if (!unzip_buffer) {
      free(moov_atom);
      info->last_error = QT_NO_MEMORY;
      return info->last_error;
    }

    z_state.next_out = unzip_buffer;
    z_state.zalloc = (alloc_func)0;
    z_state.zfree = (free_func)0;
    z_state.opaque = (voidpf)0;

    z_ret_code = inflateInit (&z_state);
    if (Z_OK != z_ret_code) {
      free(unzip_buffer);
      free(moov_atom);
      info->last_error = QT_ZLIB_ERROR;
      return info->last_error;
    }

    z_ret_code = inflate(&z_state, Z_NO_FLUSH);
    if ((z_ret_code != Z_OK) && (z_ret_code != Z_STREAM_END)) {
      free(unzip_buffer);
      free(moov_atom);
      info->last_error = QT_ZLIB_ERROR;
      return info->last_error;
    }

    z_ret_code = inflateEnd(&z_state);
    if (Z_OK != z_ret_code) {
      free(unzip_buffer);
      free(moov_atom);
      info->last_error = QT_ZLIB_ERROR;
      return info->last_error;
    }

    /* replace the compressed moov atom with the decompressed atom */
    free (moov_atom);
    moov_atom = unzip_buffer;
    moov_atom_size = BE_32(&moov_atom[0]);
  }

  if (!moov_atom) {
    info->last_error = QT_NO_MOOV_ATOM;
    return info->last_error;
  }

  /* write moov atom to disk if debugging option is turned on */
  dump_moov_atom(moov_atom, moov_atom_size);

  /* take apart the moov atom */
  parse_moov_atom(info, moov_atom);

  free(moov_atom);

  return QT_OK;
}

/**********************************************************************
 * xine demuxer functions
 **********************************************************************/

static int demux_qt_send_chunk(demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int i, j;
  unsigned int remaining_sample_bytes;
  int edit_list_compensation = 0;
  int frame_duration;
  int first_buf;

  i = this->current_frame;

  /* if there is an incongruency between last and current sample, it
   * must be time to send a new pts */
  if (this->last_frame + 1 != this->current_frame) {
    /* send new pts */
    xine_demux_control_newpts(this->stream, this->qt->frames[i].pts, 
      this->qt->frames[i].pts ? BUF_FLAG_SEEK : 0);
  }

  this->last_frame = this->current_frame;
  this->current_frame++;

  /* check if all the samples have been sent */
  if (i >= this->qt->frame_count) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  /* check if we're only sending audio samples until the next keyframe */
  if ((this->waiting_for_keyframe) &&
      (this->qt->frames[i].type == MEDIA_VIDEO)) {
    if (this->qt->frames[i].keyframe) {
      this->waiting_for_keyframe = 0;
    } else {
      /* move on to the next sample */
      return this->status;
    }
  }

  if (this->qt->frames[i].type == MEDIA_VIDEO) {
    remaining_sample_bytes = this->qt->frames[i].size;
    this->input->seek(this->input, this->qt->frames[i].offset,
      SEEK_SET);

    /* frame duration is the pts diff between this video frame and
     * the next video frame (so search for the next video frame) */
    frame_duration = 0;
    j = i;
    while (++j < this->qt->frame_count) {
      if (this->qt->frames[j].type == MEDIA_VIDEO) {
        frame_duration = 
          this->qt->frames[j].pts - this->qt->frames[i].pts;
        break;
      }
    }

    /* Due to the edit lists, some successive frames have the same pts
     * which would ordinarily cause frame_duration to be 0 which can
     * cause DIV-by-0 errors in the engine. Perform this little trick
     * to compensate. */
    if (!frame_duration) {
      frame_duration = 1;
      edit_list_compensation++;
    } else {
      frame_duration -= edit_list_compensation;
      edit_list_compensation = 0;
    }

    this->stream->stream_info[XINE_STREAM_INFO_FRAME_DURATION] =
      frame_duration;

    debug_video_demux("  qt: sending off frame %d (video) %d bytes, %lld pts, duration %d\n",
      i, 
      this->qt->frames[i].size,
      this->qt->frames[i].pts,
      frame_duration);

    while (remaining_sample_bytes) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = this->qt->video_type;
      buf->input_pos = this->qt->frames[i].offset - this->data_start;
      buf->input_length = this->data_size;
      buf->input_time = this->qt->frames[i].pts / 90000;
      buf->pts = this->qt->frames[i].pts;

      buf->decoder_flags |= BUF_FLAG_FRAMERATE;
      buf->decoder_info[0] = frame_duration;

      if (remaining_sample_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_sample_bytes;
      remaining_sample_bytes -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      if (this->qt->frames[i].keyframe)
        buf->decoder_flags |= BUF_FLAG_KEYFRAME;
      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      this->video_fifo->put(this->video_fifo, buf);
    }

  } else if ((this->qt->frames[i].type == MEDIA_AUDIO) &&
      this->audio_fifo && this->qt->audio_type) {
    /* load an audio sample and packetize it */
    remaining_sample_bytes = this->qt->frames[i].size;
    this->input->seek(this->input, this->qt->frames[i].offset,
      SEEK_SET);

    debug_audio_demux("  qt: sending off frame %d (audio) %d bytes, %lld pts\n",
      i, 
      this->qt->frames[i].size,
      this->qt->frames[i].pts);

    first_buf = 1;
    while (remaining_sample_bytes) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = this->qt->audio_type;
      buf->input_pos = this->qt->frames[i].offset - this->data_start;
      buf->input_length = this->data_size;
      /* The audio chunk is often broken up into multiple 8K buffers when
       * it is sent to the audio decoder. Only attach the proper timestamp
       * to the first buffer. This is for the linear PCM decoder which
       * turns around and sends out audio buffers as soon as they are
       * received. If 2 or more consecutive audio buffers are dispatched to
       * the audio out unit, the engine will compensate with pops. */
      if ((buf->type == BUF_AUDIO_LPCM_BE) || 
          (buf->type == BUF_AUDIO_LPCM_LE)) { 
        if (first_buf) {
          buf->input_time = this->qt->frames[i].pts / 90000;
          buf->pts = this->qt->frames[i].pts;
          first_buf = 0;
        } else {
          buf->input_time = 0;
          buf->pts = 0;
        }
      } else {
        buf->input_time = this->qt->frames[i].pts / 90000;
        buf->pts = this->qt->frames[i].pts;
      }

      if (remaining_sample_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_sample_bytes;
      remaining_sample_bytes -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      if (!remaining_sample_bytes) {
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

#if 0
        if( this->qt->audio_sample_size_table ) {
          buf->decoder_flags |= BUF_FLAG_SPECIAL;
          buf->decoder_info[1] = BUF_SPECIAL_SAMPLE_SIZE_TABLE;
          buf->decoder_info[3] = (uint32_t)
            &this->qt->audio_sample_size_table[this->qt->frames[i].official_byte_count];
        }
#endif
      }

      this->audio_fifo->put(this->audio_fifo, buf);
    }
  }
  return this->status;
}

static void demux_qt_send_headers(demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  this->data_start = this->qt->frames[0].offset;
  this->data_size =
    this->qt->frames[this->qt->frame_count - 1].offset +
    this->qt->frames[this->qt->frame_count - 1].size -
    this->data_start;

  this->bih.biWidth = this->qt->video_width;
  this->bih.biHeight = this->qt->video_height;
  this->bih.biBitCount = this->qt->video_depth;

  this->bih.biCompression = this->qt->video_codec;
  this->qt->video_type = fourcc_to_buf_video(this->bih.biCompression);

  /* hack: workaround a fourcc clash! 'mpg4' is used by MS and Sorenson
   * mpeg4 codecs (they are not compatible).
   */
  if( this->qt->video_type == BUF_VIDEO_MSMPEG4_V1 )
    this->qt->video_type = BUF_VIDEO_MPEG4;
#if 0
  if( !this->qt->video_type && this->qt->video_codec )
    xine_report_codec( this->stream, XINE_CODEC_VIDEO, this->bih.biCompression, 0, 0);
#endif

  this->qt->audio_type = formattag_to_buf_audio(this->qt->audio_codec);

#if 0
  if( !this->qt->audio_type && this->qt->audio_codec )
    xine_report_codec( this->stream, XINE_CODEC_AUDIO, this->qt->audio_codec, 0, 0);
#endif

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] =
    (this->qt->video_type) ? 1 : 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] =
    (this->qt->audio_type) ? 1 : 0;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]  = this->bih.biWidth;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->bih.biHeight;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->qt->audio_channels;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    this->qt->audio_sample_rate;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
    this->qt->audio_bits;

  if (this->qt->copyright)
    this->stream->meta_info[XINE_META_INFO_ARTIST] =
      strdup(this->qt->copyright);
  if (this->qt->description)
    this->stream->meta_info[XINE_META_INFO_TITLE] =
      strdup(this->qt->description);
  if (this->qt->comment)
    this->stream->meta_info[XINE_META_INFO_COMMENT] =
      strdup(this->qt->comment);

  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_FOURCC] = 
    this->qt->video_codec;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_FOURCC] = 
    this->qt->audio_codec;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER;
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = 3000;  /* initial video_step */
  memcpy(buf->content, &this->bih, sizeof(this->bih));
  buf->size = sizeof(this->bih);
  buf->type = this->qt->video_type;
  this->video_fifo->put (this->video_fifo, buf);
      
  /* send header info to decoder. some mpeg4 streams need this */
  if( this->qt->video_decoder_config ) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type = this->qt->video_type;
    buf->size = this->qt->video_decoder_config_len;
    buf->content = this->qt->video_decoder_config;      
    this->video_fifo->put (this->video_fifo, buf);
  }

  /* send off the palette, if there is one */
  if (this->qt->palette_count) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_SPECIAL;
    buf->decoder_info[1] = BUF_SPECIAL_PALETTE;
    buf->decoder_info[2] = this->qt->palette_count;
    buf->decoder_info[3] = (unsigned int)&this->qt->palette;
    buf->size = 0;
    buf->type = this->qt->video_type;
    this->video_fifo->put (this->video_fifo, buf);
  }

  if (this->audio_fifo && this->qt->audio_type) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->qt->audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->qt->audio_sample_rate;
    buf->decoder_info[2] = this->qt->audio_bits;
    buf->decoder_info[3] = this->qt->audio_channels;
    this->audio_fifo->put (this->audio_fifo, buf);
    
    if( this->qt->audio_decoder_config ) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = this->qt->audio_type;
      buf->size = 0;
      buf->decoder_flags = BUF_FLAG_SPECIAL;
      buf->decoder_info[1] = BUF_SPECIAL_DECODER_CONFIG;
      buf->decoder_info[2] = this->qt->audio_decoder_config_len;
      buf->decoder_info[3] = (uint32_t)this->qt->audio_decoder_config;
      this->audio_fifo->put (this->audio_fifo, buf);
    }
  }

  xine_demux_control_headers_done (this->stream);
}

static int demux_qt_seek (demux_plugin_t *this_gen,
                          off_t start_pos, int start_time) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

  int best_index;
  int left, middle, right;
  int found;
  int64_t keyframe_pts;

  this->waiting_for_keyframe = 0;

  /* perform a binary search on the sample table, testing the offset
   * boundaries first */
  if (start_pos <= this->qt->frames[0].offset)
    best_index = 0;
  else if (start_pos >= this->qt->frames[this->qt->frame_count - 1].offset) {
    this->status = DEMUX_FINISHED;
    return this->status;
  } else {
    left = 0;
    right = this->qt->frame_count - 1;
    found = 0;

    while (!found) {
      middle = (left + right) / 2;
      if ((start_pos >= this->qt->frames[middle].offset) &&
          (start_pos <= this->qt->frames[middle].offset +
           this->qt->frames[middle].size)) {
        found = 1;
      } else if (start_pos < this->qt->frames[middle].offset) {
        right = middle;
      } else {
        left = middle;
      }
    }

    best_index = middle;
  }

  /* search back in the table for the nearest keyframe */
  while (best_index) {
    if (this->qt->frames[best_index].keyframe) {
      break;
    }
    best_index--;
  }

  /* not done yet; now that the nearest keyframe has been found, seek
   * back to the first audio frame that has a pts less than or equal to
   * that of the keyframe */
  this->waiting_for_keyframe = 1;
  keyframe_pts = this->qt->frames[best_index].pts;
  while (best_index) {
    if ((this->qt->frames[best_index].type == MEDIA_AUDIO) &&
        (this->qt->frames[best_index].pts < keyframe_pts)) {
      break;
    }
    best_index--;
  }

  this->current_frame = best_index;
  this->status = DEMUX_OK;

  if( !this->stream->demux_thread_running ) {
    this->last_frame = 0;
  } else {
    xine_demux_flush_engine(this->stream);
  }
  
  return this->status;
}

static void demux_qt_dispose (demux_plugin_t *this_gen) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

  free_qt_info(this->qt);
  free(this);
}

static int demux_qt_get_status (demux_plugin_t *this_gen) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

  return this->status;
}

static int demux_qt_get_stream_length (demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;

  return (this->qt->duration / this->qt->timescale);
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_qt_t     *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf(_("demux_qt.c: input not seekable, can not handle!\n"));
    return NULL;
  }

  if ((input->get_capabilities(input) & INPUT_CAP_BLOCK)) {
    printf(_("demux_qt.c: input is block organized, can not handle!\n"));
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_qt_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_qt_send_headers;
  this->demux_plugin.send_chunk        = demux_qt_send_chunk;
  this->demux_plugin.seek              = demux_qt_seek;
  this->demux_plugin.dispose           = demux_qt_dispose;
  this->demux_plugin.get_status        = demux_qt_get_status;
  this->demux_plugin.get_stream_length = demux_qt_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:

    if (!is_qt_file(this->input)) {
      free (this);
      return NULL;
    }
    if ((this->qt = create_qt_info()) == NULL) {
      free (this);
      return NULL;
    }
    if (open_qt_file(this->qt, this->input) != QT_OK) {
      free (this);
      return NULL;
    }

  break;

  case METHOD_BY_EXTENSION: {
    char *ending, *mrl;

    mrl = input->get_mrl (input);

    ending = strrchr(mrl, '.');

    if (!ending) {
      free (this);
      return NULL;
    }

    if (strncasecmp (ending, ".mov", 4) &&
        strncasecmp (ending, ".qt", 3) &&
        strncasecmp (ending, ".mp4", 4)) {
      free (this);
      return NULL;
    }
  }

  /* we want to fall through here */
  case METHOD_EXPLICIT: {

    if (!is_qt_file(this->input)) {
      free (this);
      return NULL;
    }
    if ((this->qt = create_qt_info()) == NULL) {
      free (this);
      return NULL;
    }
    if (open_qt_file(this->qt, this->input) != QT_OK) {
      free (this);
      return NULL;
    }
  }
  break;

  default:
    free (this);
    return NULL;
  }

  strncpy (this->last_mrl, input->get_mrl (input), 1024);

  /* print vital stats */
  xine_log (this->stream->xine, XINE_LOG_MSG,
    _("demux_qt: Apple Quicktime file, %srunning time: %d min, %d sec\n"),
    (this->qt->compressed_header) ? "compressed header, " : "",
    this->qt->duration / this->qt->timescale / 60,
    this->qt->duration / this->qt->timescale % 60);
  if (this->qt->video_codec)
    xine_log (this->stream->xine, XINE_LOG_MSG,
      _("demux_qt: '%c%c%c%c' video @ %dx%d\n"),
      *((char *)&this->qt->video_codec + 0),
      *((char *)&this->qt->video_codec + 1),
      *((char *)&this->qt->video_codec + 2),
      *((char *)&this->qt->video_codec + 3),
      this->qt->video_width,
      this->qt->video_height);
  if (this->qt->audio_codec)
    xine_log (this->stream->xine, XINE_LOG_MSG,
      _("demux_qt: '%c%c%c%c' audio @ %d Hz, %d bits, %d %s\n"),
      *((char *)&this->qt->audio_codec + 0),
      *((char *)&this->qt->audio_codec + 1),
      *((char *)&this->qt->audio_codec + 2),
      *((char *)&this->qt->audio_codec + 3),
      this->qt->audio_sample_rate,
      this->qt->audio_bits,
      this->qt->audio_channels,
      ngettext("channel", "channels", this->qt->audio_channels));

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "Apple Quicktime (MOV) and MPEG-4 demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "MOV/MPEG-4";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "mov qt mp4";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return "video/quicktime: mov,qt: Quicktime animation;"
         "video/x-quicktime: mov,qt: Quicktime animation;";
}

static void class_dispose (demux_class_t *this_gen) {

  demux_qt_class_t *this = (demux_qt_class_t *) this_gen;

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  demux_qt_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_qt_class_t));
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
  { PLUGIN_DEMUX, 16, "quicktime", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
