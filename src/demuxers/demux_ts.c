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
 * $Id: demux_ts.c,v 1.64 2002/11/14 12:45:33 miguelfreitas Exp $
 *
 * Demultiplexer for MPEG2 Transport Streams.
 *
 * For the purposes of playing video, we make some assumptions about the
 * kinds of TS we have to process. The most important simplification is to
 * assume that the TS contains a single program (SPTS) because this then
 * allows significant simplifications to be made in processing PATs.
 *
 * The next simplification is to assume that the program has a reasonable
 * number of video, audio and other streams. This allows PMT processing to
 * be simplified.
 *
 * MODIFICATION HISTORY
 *
 * Date        Author
 * ----        ------
 *
 * 07-Nov-2992 Howdy Pierce
 *                  - various bugfixes
 *
 * 30-May-2002 Mauro Borghi
 *                  - dynamic allocation leaks fixes
 *
 * 27-May-2002 Giovanni Baronetti and Mauro Borghi <mauro.borghi@tilab.com>
 *                  - fill buffers before putting them in fifos 
 *                  - force PMT reparsing when PMT PID changes
 *                  - accept non seekable input plugins -- FIX?
 *                  - accept dvb as input plugin
 *                  - optimised read operations
 *                  - modified resync code
 *
 * 16-May-2002 Thibaut Mattern <tmattern@noos.fr>
 *                  - fix demux loop
 *
 * 07-Jan-2002 Andr Draszik <andid@gmx.net>
 *                  - added support for single-section PMTs
 *                    spanning multiple TS packets
 *
 * 10-Sep-2001 James Courtier-Dutton <jcdutton>
 *                  - re-wrote sync code so that it now does not loose any data
 *
 * 27-Aug-2001 Hubert Matthews  Reviewed by: n/a
 *                  - added in synchronisation code
 *
 *  1-Aug-2001 James Courtier-Dutton <jcdutton>  Reviewed by: n/a
 *                  - TS Streams with zero PES lenght should now work
 *
 * 30-Jul-2001 shaheedhaque     Reviewed by: n/a
 *                  - PATs and PMTs seem to work
 *
 * 29-Jul-2001 shaheedhaque     Reviewed by: n/a
 *                  - Compiles!
 *
 *
 * TODO: do without memcpys, preview buffers
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

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"

#define VALID_MRLS   "fifo,stdin,dvb,tcp"

/*
  #define TS_LOG
  #define TS_PMT_LOG
  #define TS_READ_STATS // activates read statistics generation
*/

/*
 *  The maximum number of PIDs we are prepared to handle in a single program
 *  is the number that fits in a single-packet PMT.
 */
#define PKT_SIZE 188
#define BODY_SIZE (188 - 4)
#define MAX_PIDS ((BODY_SIZE - 1 - 13) / 4)
#define MAX_PMTS ((BODY_SIZE - 1 - 13) / 4)
#define SYNC_BYTE   0x47

#define MIN_SYNCS 3
#define NPKT_PER_READ 100

#define BUF_SIZE (NPKT_PER_READ * PKT_SIZE)

#define MAX_PES_BUF_SIZE 2048

#define NULL_PID 0x1fff
#define INVALID_PID ((unsigned int)(-1))
#define INVALID_PROGRAM ((unsigned int)(-1))
#define INVALID_CC ((unsigned int)(-1))

#define	MIN(a,b)   (((a)<(b))?(a):(b))
#define	MAX(a,b)   (((a)>(b))?(a):(b))

/*
**
** DATA STRUCTURES
**
*/

/*
 * Describe a single elementary stream.
 */
typedef struct {
  unsigned int     pid;
  fifo_buffer_t   *fifo;
  uint8_t         *content;
  uint32_t         size;
  uint32_t         type;
  int64_t          pts;
  buf_element_t   *buf;
  unsigned int     counter;
  uint8_t          descriptor_tag;
  int64_t          packet_count;
  int              corrupted_pes;
  uint32_t         buffered_bytes;

} demux_ts_media;

typedef struct {
  /*
   * The first field must be the "base class" for the plugin!
   */
  demux_plugin_t   demux_plugin;

  xine_stream_t   *stream;

  config_values_t *config;

  fifo_buffer_t   *audio_fifo;
  fifo_buffer_t   *video_fifo;

  input_plugin_t  *input;

  int              status;

  int              blockSize;
  int              rate;
  demux_ts_media   media[MAX_PIDS];
  uint32_t         program_number[MAX_PMTS];
  uint32_t         pmt_pid[MAX_PMTS];
  uint8_t         *pmt[MAX_PMTS];
  uint8_t         *pmt_write_ptr[MAX_PMTS];
  uint32_t         crc32_table[256];
  /*
   * Stuff to do with the transport header. As well as the video
   * and audio PIDs, we keep the index of the corresponding entry
   * inthe media[] array.
   */
  unsigned int     programNumber;
  unsigned int     pcrPid;
  int64_t          PCR;
  int64_t          last_PCR;
  unsigned int     pid;
  unsigned int     pid_count;
  unsigned int     videoPid;
  unsigned int     audioPid;
  unsigned int     videoMedia;
  unsigned int     audioMedia;

  int              send_end_buffers;
  int              ignore_scr_discont;
  int              send_newpts;
  int              buf_flag_seek;

  unsigned int     scrambled_pids[MAX_PIDS];
  unsigned int     scrambled_npids;

#ifdef TS_READ_STATS
  uint32_t         rstat[NPKT_PER_READ + 1];
#endif

} demux_ts_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_ts_class_t;


static void demux_ts_build_crc32_table(demux_ts_t*this) {
  uint32_t  i, j, k;

  for( i = 0 ; i < 256 ; i++ ) {
    k = 0;
    for (j = (i << 24) | 0x800000 ; j != 0x80000000 ; j <<= 1) {
      k = (k << 1) ^ (((k ^ j) & 0x80000000) ? 0x04c11db7 : 0);
    }
    this->crc32_table[i] = k;
  }
}

static uint32_t demux_ts_compute_crc32(demux_ts_t*this, uint8_t *data, 
				       uint32_t length, uint32_t crc32) {
  uint32_t i;

  for(i = 0; i < length; i++) {
    crc32 = (crc32 << 8) ^ this->crc32_table[(crc32 >> 24) ^ data[i]];
  }
  return crc32;
}

static void check_newpts( demux_ts_t*this, int64_t pts ) {

  printf ("demux_ts: check_newpts %lld, send_newpts %d, buf_flag_seek %d\n",
	  pts, this->send_newpts, this->buf_flag_seek);

  if (this->send_newpts && pts) {

    if (this->buf_flag_seek) {
      xine_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      xine_demux_control_newpts(this->stream, pts, 0);
    }
    this->send_newpts = 0;
    this->ignore_scr_discont = 1;
  }
}


/*
 * demux_ts_parse_pat
 *
 * Parse a program association table (PAT).
 * The PAT is expected to be exactly one section long,
 * and that section is expected to be contained in a single TS packet.
 *
 * The PAT is assumed to contain a single program definition, though
 * we can cope with the stupidity of SPTSs which contain NITs.
 */
static void demux_ts_parse_pat (demux_ts_t*this, unsigned char *original_pkt,
                                unsigned char *pkt, unsigned int pusi) {
  uint32_t       table_id;
  uint32_t       section_syntax_indicator;
  uint32_t       section_length;
  uint32_t       transport_stream_id;
  uint32_t       version_number;
  uint32_t       current_next_indicator;
  uint32_t       section_number;
  uint32_t       last_section_number;
  uint32_t       crc32;
  uint32_t       calc_crc32;

  unsigned char *program;
  unsigned int   program_number;
  unsigned int   pmt_pid;
  unsigned int   program_count;

  /*
   * A PAT in a single section should start with a payload unit start
   * indicator set.
   */
  if (!pusi) {
    printf ("demux_ts: demux error! PAT without payload unit "
	    "start indicator\n");
    return;
  }

  /*
   * sections start with a pointer. Skip it!
   */
  pkt += pkt[4];
  if (pkt - original_pkt > PKT_SIZE) {
    printf ("demux_ts: demux error! PAT with invalid pointer\n");
    return;
  }
  table_id = (unsigned int)pkt[5] ;
  section_syntax_indicator = (((unsigned int)pkt[6] >> 7) & 1) ;
  section_length = (((unsigned int)pkt[6] & 0x03) << 8) | pkt[7];
  transport_stream_id = ((uint32_t)pkt[8] << 8) | pkt[9];
  version_number = ((uint32_t)pkt[10] >> 1) & 0x1f;
  current_next_indicator = ((uint32_t)pkt[10] & 0x01);
  section_number = (uint32_t)pkt[11];
  last_section_number = (uint32_t)pkt[12];
  crc32 = (uint32_t)pkt[4+section_length] << 24;
  crc32 |= (uint32_t)pkt[5+section_length] << 16;
  crc32 |= (uint32_t)pkt[6+section_length] << 8;
  crc32 |= (uint32_t)pkt[7+section_length] ;

#ifdef TS_LOG
  printf ("demux_ts: PAT table_id: %.2x\n", table_id);
  printf ("              section_syntax: %d\n", section_syntax_indicator);
  printf ("              section_length: %d (%#.3x)\n",
          section_length, section_length);
  printf ("              transport_stream_id: %#.4x\n", transport_stream_id);
  printf ("              version_number: %d\n", version_number);
  printf ("              c/n indicator: %d\n", current_next_indicator);
  printf ("              section_number: %d\n", section_number);
  printf ("              last_section_number: %d\n", last_section_number);
#endif

  if ((section_syntax_indicator != 1) || !(current_next_indicator)) {
    return;
  }

  if (pkt - original_pkt > BODY_SIZE - 1 - 3 - section_length) {
    printf ("demux_ts: FIXME: (unsupported )PAT spans multiple TS packets\n");
    return;
  }

  if ((section_number != 0) || (last_section_number != 0)) {
    printf ("demux_ts: FIXME: (unsupported) PAT consists of multiple (%d) sections\n",
	    last_section_number);
    return;
  }

  /* Check CRC. */
  calc_crc32 = demux_ts_compute_crc32 (this, pkt+5, section_length+3-4,
                                       0xffffffff);
  if (crc32 != calc_crc32) {
    printf ("demux_ts: demux error! PAT with invalid CRC32: packet_crc32: %.8x calc_crc32: %.8x\n",
	    crc32,calc_crc32);
    return;
  }

  /*
   * Process all programs in the program loop.
   */
  program_count = 0;
  for (program = pkt + 13;
       program < pkt + 13 + section_length - 9;
       program += 4) {
    program_number = ((unsigned int)program[0] << 8) | program[1];
    pmt_pid = (((unsigned int)program[2] & 0x1f) << 8) | program[3];

    /*
     * completely skip NIT pids.
     */
    if (program_number == 0x0000)
      continue;

    /*
     * If we have yet to learn our program number, then learn it,
     * use this loop to eventually add support for dynamically changing
     * PATs.
     */
    program_count = 0;

    while ((this->program_number[program_count] != INVALID_PROGRAM) &&
           (this->program_number[program_count] != program_number) ) {
      program_count++;
    }
    this->program_number[program_count] = program_number;

    /* force PMT reparsing when pmt_pid changes */
    if (this->pmt_pid[program_count] != pmt_pid) {
      this->pmt_pid[program_count] = pmt_pid;
      this->audioPid = INVALID_PID;
      this->videoPid = INVALID_PID;
    }

    this->pmt_pid[program_count] = pmt_pid;
    if (this->pmt[program_count] != NULL) {
      free(this->pmt[program_count]);
      this->pmt[program_count] = NULL;
      this->pmt_write_ptr[program_count] = NULL;
    }
#ifdef TS_LOG
    if (this->program_number[program_count] != INVALID_PROGRAM)
      printf ("demux_ts: PAT acquired count=%d programNumber=0x%04x "
              "pmtPid=0x%04x\n",
              program_count,
              this->program_number[program_count],
              this->pmt_pid[program_count]);
#endif
  }
}

static int demux_ts_parse_pes_header (demux_ts_media *m,
				      uint8_t *buf, int packet_len,
                                      xine_stream_t *stream) {

  unsigned char *p;
  uint32_t       header_len;
  int64_t        pts;
  uint32_t       stream_id;
  int            pkt_len;

  p = buf;
  pkt_len = packet_len;

  /* we should have a PES packet here */

  if (p[0] || p[1] || (p[2] != 1)) {
    printf ("demux_ts: error %02x %02x %02x (should be 0x000001) \n",
            p[0], p[1], p[2]);
    return 0 ;
  }

  packet_len -= 6;
  /* packet_len = p[4] << 8 | p[5]; */
  stream_id  = p[3];

  if (packet_len==0)
    return 0;

#ifdef TS_LOG
  printf ("demux_ts: packet stream id: %.2x len: %d (%x)\n",
          stream_id, packet_len, packet_len);
#endif

  if (p[7] & 0x80) { /* pts avail */

    pts  = (int64_t)(p[ 9] & 0x0E) << 29 ;
    pts |=  p[10]         << 22 ;
    pts |= (p[11] & 0xFE) << 14 ;
    pts |=  p[12]         <<  7 ;
    pts |= (p[13] & 0xFE) >>  1 ;

  } else
    pts = 0;

  /* code works but not used in xine
     if (p[7] & 0x40) {

     DTS  = (p[14] & 0x0E) << 29 ;
     DTS |=  p[15]         << 22 ;
     DTS |= (p[16] & 0xFE) << 14 ;
     DTS |=  p[17]         <<  7 ;
     DTS |= (p[18] & 0xFE) >>  1 ;

     } else
     DTS = 0;
  */

  m->pts       = pts;

  header_len = p[8];

  /* sometimes corruption on header_len causes segfault in memcpy below */
  if (header_len + 9 > pkt_len) {
    printf ("demux_ts: illegal value for PES_header_data_length (0x%x)\n",
	    header_len);
    return 0;
  }

  p += header_len + 9;
  packet_len -= header_len + 3;

  if (stream_id == 0xbd) {

    int track, spu_id;
      
#ifdef LOG
    printf ("demux_ts: audio buf = %02X %02X %02X %02X %02X %02X %02X %02X\n",
	    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
#endif
    track = p[0] & 0x0F; /* hack : ac3 track */
    /*
     * we check the descriptor tag first because some stations
     * do not include any of the ac3 header info in their audio tracks
     * these "raw" streams may begin with a byte that looks like a stream type.
     */
    if((m->descriptor_tag == 0x81) ||    /* ac3 - raw */ 
       (p[0] == 0x0B && p[1] == 0x77)) { /* ac3 - syncword */
      m->content   = p;
      m->size = packet_len;
      m->type = BUF_AUDIO_A52;
      return 1;

    } else if ((p[0] & 0xE0) == 0x20) {
      spu_id = (p[0] & 0x1f);

      m->content   = p+1;
      m->size      = packet_len-1;
      m->type      = BUF_SPU_PACKAGE + spu_id;
      return 1;
    } else if ((p[0] & 0xF0) == 0x80) {

      m->content   = p+4;
      m->size      = packet_len - 4;
      m->type      = BUF_AUDIO_A52 + track;
      return 1;

    } else if ((p[0]&0xf0) == 0xa0) {

      int pcm_offset;

      for (pcm_offset=0; ++pcm_offset < packet_len-1 ; ){
        if (p[pcm_offset] == 0x01 && p[pcm_offset+1] == 0x80 ) { /* START */
          pcm_offset += 2;
          break;
        }
      }

      m->content   = p+pcm_offset;
      m->size      = packet_len-pcm_offset;
      m->type      = BUF_AUDIO_LPCM_BE + track;
      return 1;
    }

  } else if ((stream_id >= 0xbc) && ((stream_id & 0xf0) == 0xe0)) {

    m->content   = p;
    m->size      = packet_len;
    m->type      = BUF_VIDEO_MPEG;
    return 1;

  } else if ((stream_id & 0xe0) == 0xc0) {

    int track;

    track = stream_id & 0x1f;

    m->content   = p;
    m->size      = packet_len;
    m->type      = BUF_AUDIO_MPEG + track;
    return 1;

  } else {
#ifdef TS_LOG
    printf ("demux_ts: unknown packet, id: %x\n", stream_id);
#endif
  }

  return 0 ;
}


/*
 *  buffer arriving pes data
 */
static void demux_ts_buffer_pes(demux_ts_t*this, unsigned char *ts,
                                unsigned int mediaIndex,
                                unsigned int pus,
                                unsigned int cc,
                                unsigned int len) {

  demux_ts_media *m = &this->media[mediaIndex];

  if (!m->fifo) {
#ifdef TS_LOG
    printf ("fifo unavailable (%d)\n", mediaIndex);
#endif
    return; /* To avoid segfault if video out or audio out plugin not loaded */
  }

  /* By checking the CC here, we avoid the need to check for the no-payload
     case (i.e. adaptation field only) when it does not get bumped. */
  if (m->counter != INVALID_CC) {
    if ((m->counter & 0x0f) != cc) {
      printf("demux_ts: PID %d: unexpected cc %d (expected %d)\n",
	     m->pid, cc, m->counter);
    }
  }
  m->counter = cc;
  m->counter++;

  if (pus) { /* new PES packet */

    if (m->buffered_bytes) {
      m->buf->content = m->buf->mem;
      m->buf->size = m->buffered_bytes;
      m->buf->type = m->type;
      m->buf->pts = m->pts;
      m->buf->decoder_info[0] = 1;
      m->buf->input_pos = this->input->get_current_pos(this->input);
      m->fifo->put(m->fifo, m->buf);
      m->buffered_bytes = 0;
      m->buf = NULL; /* forget about buf -- not our responsibility anymore */
#ifdef TS_LOG
      printf ("demux_ts: produced buffer, pts=%lld\n", m->pts);
#endif
    }

    if (!demux_ts_parse_pes_header(m, ts, len, this->stream)) {
      m->corrupted_pes = 1;
      printf("demux_ts: PID %d: corrupted pes encountered\n", m->pid);

    } else {

      check_newpts(this, m->pts);
      m->corrupted_pes = 0;
      m->buf = m->fifo->buffer_pool_alloc(m->fifo);
      memcpy(m->buf->mem, ts+len-m->size, m->size);
      m->buffered_bytes = m->size;
    }

  } else if (!m->corrupted_pes) { /* no pus -- PES packet continuation */

    if ((m->buffered_bytes + len) > MAX_PES_BUF_SIZE) {
      m->buf->content = m->buf->mem;
      m->buf->size = m->buffered_bytes;
      m->buf->type = m->type;
      m->buf->pts = m->pts;
      m->buf->decoder_info[0] = 1;
      m->buf->input_pos = this->input->get_current_pos(this->input);
      m->fifo->put(m->fifo, m->buf);
      m->buffered_bytes = 0;
      m->buf = m->fifo->buffer_pool_alloc(m->fifo);

#ifdef TS_LOG
      printf ("demux_ts: produced buffer, pts=%lld\n", m->pts);
#endif

    }
    memcpy(m->buf->mem + m->buffered_bytes, ts, len);
    m->buffered_bytes += len;
  }
}

/*
 * Create a buffer for a PES stream.
 */
static void demux_ts_pes_new(demux_ts_t*this,
                             unsigned int mediaIndex,
                             unsigned int pid,
                             fifo_buffer_t *fifo,
			     uint8_t descriptor) {

  demux_ts_media *m = &this->media[mediaIndex];

  /* new PID seen - initialise stuff */
  m->pid = pid;
  m->fifo = fifo;
  if (m->buf != NULL) m->buf->free_buffer(m->buf);
  m->buf = NULL;
  m->counter = INVALID_CC;
  m->descriptor_tag = descriptor;
  m->corrupted_pes = 1;
  m->buffered_bytes = 0;
}

/*
 * NAME demux_ts_parse_pmt
 *
 * Parse a PMT. The PMT is expected to be exactly one section long,
 * and that section is expected to be contained in a single TS packet.
 *
 * In other words, the PMT is assumed to describe a reasonable number of
 * video, audio and other streams (with descriptors).
 */
static void demux_ts_parse_pmt (demux_ts_t     *this,
                                unsigned char *originalPkt,
                                unsigned char *pkt,
                                unsigned int   pusi,
                                uint32_t       program_count) {
  typedef enum
    {
      ISO_11172_VIDEO = 1, /* 1 */
      ISO_13818_VIDEO = 2, /* 2 */
      ISO_11172_AUDIO = 3, /* 3 */
      ISO_13818_AUDIO = 4, /* 4 */
      ISO_13818_PRIVATE = 5, /* 5 */
      ISO_13818_PES_PRIVATE = 6, /* 6 */
      ISO_13522_MHEG = 7, /* 7 */
      ISO_13818_DSMCC = 8, /* 8 */
      ISO_13818_TYPE_A = 9, /* 9 */
      ISO_13818_TYPE_B = 10, /* a */
      ISO_13818_TYPE_C = 11, /* b */
      ISO_13818_TYPE_D = 12, /* c */
      ISO_13818_TYPE_E = 13, /* d */
      ISO_13818_AUX = 14,
      PRIVATE_A52 = 0x81
    } streamType;

  uint32_t       table_id;
  uint32_t       section_syntax_indicator;
  uint32_t       section_length = 0; /* to calm down gcc */
  uint32_t       program_number;
  uint32_t       version_number;
  uint32_t       current_next_indicator;
  uint32_t       section_number;
  uint32_t       last_section_number;
  uint32_t       crc32;
  uint32_t       calc_crc32;
  unsigned int programInfoLength;
  unsigned int codedLength;
  unsigned int mediaIndex;
  unsigned int pid;
  unsigned char *stream;
  unsigned int i;

  /* sections start with a pointer. Skip it! */
  pkt += pkt[4];
  if (pkt - originalPkt > PKT_SIZE) {
    printf ("demux error! PMT with invalid pointer\n");
    return;
  }

  /*
   * A new section should start with the payload unit start
   * indicator set. We allocate some mem (max. allowed for a PM section)
   * to copy the complete section into one chunk.
   */
  if (pusi) {
    if (this->pmt[program_count] != NULL) free(this->pmt[program_count]);
    this->pmt[program_count] = (uint8_t *) calloc(1024, sizeof(unsigned char));
    this->pmt_write_ptr[program_count] = this->pmt[program_count];

    table_id                  =  pkt[5] ;
    section_syntax_indicator  = (pkt[6] >> 7) & 0x01;
    section_length            = (((uint32_t) pkt[6] << 8) | pkt[7]) & 0x03ff;
    program_number            =  ((uint32_t) pkt[8] << 8) | pkt[9];
    version_number            = (pkt[10] >> 1) & 0x1f;
    current_next_indicator    =  pkt[10] & 0x01;
    section_number            =  pkt[11];
    last_section_number       =  pkt[12];

#ifdef TS_PMT_LOG
    printf ("demux_ts: PMT table_id: %2x\n", table_id);
    printf ("              section_syntax: %d\n", section_syntax_indicator);
    printf ("              section_length: %d (%#.3x)\n",
            section_length, section_length);
    printf ("              program_number: %#.4x\n", program_number);
    printf ("              version_number: %d\n", version_number);
    printf ("              c/n indicator: %d\n", current_next_indicator);
    printf ("              section_number: %d\n", section_number);
    printf ("              last_section_number: %d\n", last_section_number);
#endif

    if ((section_syntax_indicator != 1) || !current_next_indicator) {
#ifdef TS_PMT_LOG
      printf ("ts_demux: section_syntax_indicator != 1 "
              "|| !current_next_indicator\n");
#endif
      return;
    }

    if (program_number != this->program_number[program_count]) {
      /* several programs can share the same PMT pid */
#ifdef TS_PMT_LOG
      printf ("ts_demux: waiting for next PMT on this PID...\n");
#endif
      return;
    }

    if ((section_number != 0) || (last_section_number != 0)) {
      printf ("demux_ts: FIXME (unsupported) PMT consists of multiple (%d)"
              "sections\n", last_section_number);
      return;
    }
  }

  if (!this->pmt[program_count]) {
    /* not the first TS packet of a PMT, or the calloc didn't work */
#ifdef TS_PMT_LOG
    printf ("ts_demux: not the first TS packet of a PMT...\n");
#endif
    return;
  }

  if (!pusi)
    section_length = (this->pmt[program_count][1] << 8
		      | this->pmt[program_count][2]) & 0x03ff;

  codedLength = MIN (BODY_SIZE - (pkt - originalPkt) - 1,
                     (section_length+3) - (this->pmt_write_ptr[program_count]
                                           - this->pmt[program_count]));
  memcpy (this->pmt_write_ptr[program_count], &pkt[5], codedLength);
  this->pmt_write_ptr[program_count] += codedLength;

#ifdef TS_PMT_LOG
  printf ("ts_demux: wr_ptr: %p, will be %p when finished\n",
	  this->pmt_write_ptr[program_count],
	  this->pmt[program_count] + section_length);
#endif
  if (this->pmt_write_ptr[program_count] < this->pmt[program_count]
      + section_length) {
    /* didn't get all TS packets for this section yet */
#ifdef TS_PMT_LOG
    printf ("ts_demux: didn't get all PMT TS packets yet...\n");
#endif
    return;
  }

#ifdef TS_PMT_LOG
  printf ("ts_demux: have all TS packets for the PMT section\n");
#endif

  crc32  = (uint32_t) this->pmt[program_count][section_length+3-4] << 24;
  crc32 |= (uint32_t) this->pmt[program_count][section_length+3-3] << 16;
  crc32 |= (uint32_t) this->pmt[program_count][section_length+3-2] << 8;
  crc32 |= (uint32_t) this->pmt[program_count][section_length+3-1] ;

  /* Check CRC. */
  calc_crc32 = demux_ts_compute_crc32 (this,
                                       this->pmt[program_count],
                                       section_length+3-4, 0xffffffff);
  if (crc32 != calc_crc32) {
    printf ("demux_ts: demux error! PMT with invalid CRC32: "
	    "packet_crc32: %#.8x calc_crc32: %#.8x\n",
	    crc32,calc_crc32); 
    return;
  }

  /*
   * ES definitions start here...we are going to learn upto one video
   * PID and one audio PID.
   */
  programInfoLength = ((this->pmt[program_count][10] << 8)
                       | this->pmt[program_count][11]) & 0x0fff;
  stream = &this->pmt[program_count][12] + programInfoLength;
  codedLength = 13 + programInfoLength;
  if (codedLength > section_length) {
    printf ("demux error! PMT with inconsistent "
	    "progInfo length\n");
    return;
  }
  section_length -= codedLength;

  /*
   * Extract the elementary streams.
   */
  mediaIndex = 0;
  while (section_length > 0) {
    unsigned int streamInfoLength;

    pid = ((stream[1] << 8) | stream[2]) & 0x1fff;
    streamInfoLength = ((stream[3] << 8) | stream[4]) & 0x0fff;
    codedLength = 5 + streamInfoLength;
    if (codedLength > section_length) {
      printf ("demux error! PMT with inconsistent "
	      "streamInfo length\n");
      return;
    }

    /*
     * Squirrel away the first audio and the first video stream. TBD: there
     * should really be a way to select the stream of interest.
     */
    switch (stream[0]) {
    case ISO_11172_VIDEO:
    case ISO_13818_VIDEO:
      if (this->videoPid == INVALID_PID) {
#ifdef TS_PMT_LOG
        printf ("demux_ts: PMT video pid %.4x\n", pid);
#endif
        demux_ts_pes_new(this, mediaIndex, pid, this->video_fifo,stream[0]);
	this->videoMedia = mediaIndex;
	this->videoPid = pid;
      }

      break;
    case ISO_11172_AUDIO:
    case ISO_13818_AUDIO:
      if (this->audioPid == INVALID_PID) {
#ifdef TS_PMT_LOG
        printf ("demux_ts: PMT audio pid %.4x\n", pid);
#endif
        demux_ts_pes_new(this, mediaIndex, pid, this->audio_fifo,stream[0]);
        this->audioPid = pid;
        this->audioMedia = mediaIndex;
      }
      break;
    case ISO_13818_PRIVATE:
#ifdef TS_PMT_LOG
      printf ("demux_ts: PMT streamtype 13818_PRIVATE, pid: %.4x\n", pid);

      for (i = 5; i < codedLength; i++)
        printf ("%.2x ", stream[i]);
      printf ("\n");
#endif
      break;
    case ISO_13818_PES_PRIVATE:
      for (i = 5; i < codedLength; i += stream[i+1] + 2) {
        if ((stream[i] == 0x6a) && (this->audioPid == INVALID_PID)) {
#ifdef TS_PMT_LOG
          printf ("demux_ts: PMT AC3 audio pid %.4x\n", pid);
#endif
          demux_ts_pes_new(this, mediaIndex, pid, this->audio_fifo,stream[0]);
          this->audioPid = pid;
          this->audioMedia = mediaIndex;
          break;
        }
      }
      break;
    case PRIVATE_A52:
#ifdef TS_PMT_LOG
      printf ("demux_ts: PMT streamtype PRIVATE_A52, pid: %.4x\n", pid);

      for (i = 5; i < codedLength; i++)
        printf ("%.2x ", stream[i]);
      printf ("\n");
#endif
      if (this->audioPid == INVALID_PID) {
	demux_ts_pes_new(this, mediaIndex, pid, this->audio_fifo, stream[0]);
        this->audioPid = pid;
        this->audioMedia = mediaIndex;
      }
      break;
    default:
#ifdef TS_PMT_LOG
      printf ("demux_ts: PMT unknown stream_type: %.2x pid: %.4x\n",
              stream[0], pid);

      for (i = 5; i < codedLength; i++)
        printf ("%.2x ", stream[i]);
      printf ("\n");
#endif
      break;
    }
    mediaIndex++;
    stream += codedLength;
    section_length -= codedLength;
  }

  /*
   * Get the current PCR PID.
   */
  pid = ((this->pmt[program_count][8] << 8)
         | this->pmt[program_count][9]) & 0x1fff;
  if (this->pcrPid != pid) {
#ifdef TS_PMT_LOG
    if (this->pcrPid == INVALID_PID) {
      printf ("demux_ts: PMT pcr pid %.4x\n", pid);
    } else {
      printf ("demux_ts: PMT pcr pid changed %.4x\n", pid);
    }
#endif
    this->pcrPid = pid;
  }
}

static int sync_correct(demux_ts_t*this, uint8_t *buf, int32_t npkt_read) {

  int p = 0;
  int n = 0;
  int i = 0;
  int sync_ok = 0;
  int read_length;

  printf ("demux_ts: about to resync!\n");

  for (p=0; p < npkt_read; p++) {
    for(n=0; n < PKT_SIZE; n++) {
      sync_ok = 1;
      for (i=0; i < MIN(MIN_SYNCS, npkt_read - p); i++) {
	if (buf[n + ((i+p) * PKT_SIZE)] != SYNC_BYTE) {
	  sync_ok = 0;
	  break;
	}
      }
      if (sync_ok) break;
    }
    if (sync_ok) break;
  }

  if (sync_ok) {
    /* Found sync, fill in */
    memmove(&buf[0], &buf[n + p * PKT_SIZE],
	    ((PKT_SIZE * (npkt_read - p)) - n));
    read_length = this->input->read(this->input,
				    &buf[(PKT_SIZE * (npkt_read - p)) - n],
				    n + p * PKT_SIZE);
    /* FIXME: when read_length is not as required... we now stop demuxing */
    if (read_length != (n + p * PKT_SIZE)) {
      printf ("demux_ts_tsync_correct: sync found, but read failed\n");
      return 0;
    }
  } else {
    printf ("demux_ts_tsync_correct: sync not found! Stop demuxing\n");
    return 0;
  }
  printf ("demux_ts: resync successful!\n");
  return 1;
}

static int sync_detect(demux_ts_t*this, uint8_t *buf, int32_t npkt_read) {

  int i, sync_ok;

  sync_ok = 1;

  for (i=0; i < MIN(MIN_SYNCS, npkt_read); i++) {
    if (buf[i * PKT_SIZE] != SYNC_BYTE) {
      sync_ok = 0;
      break;
    }
  }
  if (!sync_ok) return sync_correct(this, buf, npkt_read);
  return sync_ok;
}


/*
 *  Main synchronisation routine.
 */
static unsigned char * demux_synchronise(demux_ts_t* this) {

  static int32_t packet_number = 0;
  /* NEW: var to keep track of number of last read packets */
  static int32_t npkt_read = 0;
  static int32_t read_zero = 0;

  static uint8_t buf[BUF_SIZE]; /* This should change to a malloc. */
  uint8_t *return_pointer = NULL;
  int32_t read_length;
  if (packet_number >= npkt_read) {

    /* NEW: handle read returning less packets than NPKT_PER_READ... */
    do {
      read_length = this->input->read(this->input, buf,
				      PKT_SIZE * NPKT_PER_READ);
      if (read_length % PKT_SIZE) {
	printf ("demux_ts: read returned %d bytes (not a multiple of %d!)\n",
		read_length, PKT_SIZE);
	this->status = DEMUX_FINISHED;
	return NULL;
      }
      npkt_read = read_length / PKT_SIZE;

#ifdef TS_READ_STATS
      this->rstat[npkt_read]++;
#endif
      /*
       * what if npkt_read < 5 ? --> ok in sync_detect
       *
       * NEW: stop demuxing if read returns 0 a few times... (200)
       */

      if (npkt_read == 0) {
	/* printf ("demux_ts: read 0 packets! (%d)\n", read_zero); */
	read_zero++;
      } else read_zero = 0;

      if (read_zero > 200) {
	printf ("demux_ts: read 0 packets too many times!\n");
	this->status = DEMUX_FINISHED;
	return NULL;
      }

    } while (! read_length);

    packet_number = 0;

    if (!sync_detect(this, &buf[0], npkt_read)) {
      printf ("demux_ts: sync error.\n");
      this->status = DEMUX_FINISHED;
      return NULL;
    }
  }
  return_pointer = &buf[PKT_SIZE * packet_number];
  packet_number++;
  return return_pointer;
}


static int64_t demux_ts_adaptation_field_parse(uint8_t *data, 
					       uint32_t adaptation_field_length) {

  uint32_t    discontinuity_indicator=0;
  uint32_t    random_access_indicator=0;
  uint32_t    elementary_stream_priority_indicator=0;
  uint32_t    PCR_flag=0;
  int64_t     PCR=0;
  uint32_t    EPCR=0;
  uint32_t    OPCR_flag=0;
  uint32_t    OPCR=0;
  uint32_t    EOPCR=0;
  uint32_t    slicing_point_flag=0;
  uint32_t    transport_private_data_flag=0;
  uint32_t    adaptation_field_extension_flag=0;
  uint32_t    offset = 1;

  discontinuity_indicator = ((data[0] >> 7) & 0x01);
  random_access_indicator = ((data[0] >> 6) & 0x01);
  elementary_stream_priority_indicator = ((data[0] >> 5) & 0x01);
  PCR_flag = ((data[0] >> 4) & 0x01);
  OPCR_flag = ((data[0] >> 3) & 0x01);
  slicing_point_flag = ((data[0] >> 2) & 0x01);
  transport_private_data_flag = ((data[0] >> 1) & 0x01);
  adaptation_field_extension_flag = (data[0] & 0x01);

#ifdef TS_LOG
  printf ("demux_ts: ADAPTATION FIELD length: %d (%x)\n",
          adaptation_field_length, adaptation_field_length);
  if(discontinuity_indicator) {
    printf ("               Discontinuity indicator: %d\n",
            discontinuity_indicator);
  }
  if(random_access_indicator) {
    printf ("               Random_access indicator: %d\n",
            random_access_indicator);
  }
  if(elementary_stream_priority_indicator) {
    printf ("               Elementary_stream_priority_indicator: %d\n",
            elementary_stream_priority_indicator);
  }
#endif
  if(PCR_flag) {
    PCR  = (((int64_t) data[offset]) & 0xFF) << 25;
    PCR += (int64_t) ((data[offset+1] & 0xFF) << 17);
    PCR += (int64_t) ((data[offset+2] & 0xFF) << 9);
    PCR += (int64_t) ((data[offset+3] & 0xFF) << 1);
    PCR += (int64_t) ((data[offset+4] & 0x80) >> 7);

    EPCR = ((data[offset+4] & 0x1) << 8) | data[offset+5];
#ifdef TS_LOG
    printf ("demux_ts: PCR: %lld, EPCR: %u\n",
            PCR, EPCR);
#endif
    offset+=6;
  }
  if(OPCR_flag) {
    OPCR = data[offset] << 25;
    OPCR |= data[offset+1] << 17;
    OPCR |= data[offset+2] << 9;
    OPCR |= data[offset+3] << 1;
    OPCR |= (data[offset+4] >> 7) & 0x01;
    EOPCR = ((data[offset+4] & 0x1) << 8) | data[offset+5];
#ifdef TS_LOG
    printf ("demux_ts: OPCR: %u, EOPCR: %u\n",
            OPCR,EOPCR);
#endif
    offset+=6;
  }
#ifdef TS_LOG
  if(slicing_point_flag) {
    printf ("demux_ts: slicing_point_flag: %d\n",
            slicing_point_flag);
  }
  if(transport_private_data_flag) {
    printf ("demux_ts: transport_private_data_flag: %d\n",
	    transport_private_data_flag);
  }
  if(adaptation_field_extension_flag) {
    printf ("demux_ts: adaptation_field_extension_flag: %d\n",
            adaptation_field_extension_flag);
  }
#endif
  return PCR;
}

/* transport stream packet layer */
static void demux_ts_parse_packet (demux_ts_t*this) {

  unsigned char *originalPkt;
  unsigned int   sync_byte;
  unsigned int   transport_error_indicator;
  unsigned int   payload_unit_start_indicator;
  unsigned int   transport_priority;
  unsigned int   pid;
  unsigned int   transport_scrambling_control;
  unsigned int   adaptation_field_control;
  unsigned int   continuity_counter;
  unsigned int   data_offset;
  unsigned int   data_len;
  uint32_t       program_count;
  int i;

  /* get next synchronised packet, or NULL */
  originalPkt = demux_synchronise(this);
  if (originalPkt == NULL)
    return;

  sync_byte                      = originalPkt[0];
  transport_error_indicator      = (originalPkt[1]  >> 7) & 0x01;
  payload_unit_start_indicator   = (originalPkt[1] >> 6) & 0x01;
  transport_priority             = (originalPkt[1] >> 5) & 0x01;
  pid                            = ((originalPkt[1] << 8) | 
				    originalPkt[2]) & 0x1fff;
  transport_scrambling_control   = (originalPkt[3] >> 6)  & 0x03;
  adaptation_field_control       = (originalPkt[3] >> 4) & 0x03;
  continuity_counter             = originalPkt[3] & 0x0f;

  /*
   * Discard packets that are obviously bad.
   */
  if (sync_byte != 0x47) {
    printf ("demux error! invalid ts sync byte %.2x\n",
	    originalPkt[0]);
    return;
  }
  if (transport_error_indicator) {
    printf ("demux error! transport error\n");
    return;
  }
  if (transport_scrambling_control) {
    if (this->videoPid == pid) {
      printf ("demux_ts: selected videoPid is scrambled; skipping...\n");
    }
    for (i=0; i < this->scrambled_npids; i++) {
      if (this->scrambled_pids[i] == pid) return;
    }
    this->scrambled_pids[this->scrambled_npids] = pid;
    this->scrambled_npids++;

    printf ("demux_ts: PID %d is scrambled!\n", pid);
    return;
  }

  data_offset = 4;

  if( adaptation_field_control & 0x2 ){
    uint32_t adaptation_field_length = originalPkt[4];
    if (adaptation_field_length > 0) {
      this->PCR = demux_ts_adaptation_field_parse (originalPkt+5,
						   adaptation_field_length);

      if (pid == this->pcrPid && this->PCR)  {  
	int64_t scr_diff = this->PCR - this->last_PCR;
	
	/* note: comparing (abs(scr_diff) > 90000) isn't reliable
         * below because abs() give inconsistent results on int64_t
         * types */
	if ( (scr_diff > 90000 || scr_diff < -90000) && !this->send_newpts && 
	     !this->ignore_scr_discont ) {
	  
	  buf_element_t *buf;
	  
	  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
	  buf->type = BUF_CONTROL_DISCONTINUITY;
	  buf->disc_off = scr_diff;
	  this->video_fifo->put (this->video_fifo, buf);
	  
	  if (this->audio_fifo) {
	    buf = this->audio_fifo->buffer_pool_alloc(this->audio_fifo);
	    buf->type = BUF_CONTROL_DISCONTINUITY;
	    buf->disc_off = scr_diff;
	    this->audio_fifo->put (this->audio_fifo, buf);
	  }
	}
	this->last_PCR = this->PCR;
	this->ignore_scr_discont = 0;
      }
    }
    /*
     * Skip adaptation header.
     */
    data_offset += adaptation_field_length + 1;
  }
  
  if (! (adaptation_field_control & 0x1)) {
    return;
  }

  data_len = PKT_SIZE - data_offset;
    
  if (data_len > PKT_SIZE) {

    printf ("demux_ts: demux error! invalid payload size %d\n",
	    data_len);

  } else {

    /*
     * Do the demuxing in descending order of packet frequency!
     */
    if (pid == this->videoPid) {
#ifdef TS_LOG
      printf ("demux_ts: Video pid: %.4x\n", pid);
#endif
      demux_ts_buffer_pes (this, originalPkt+data_offset, this->videoMedia,
			   payload_unit_start_indicator, continuity_counter,
			   data_len);
      return;
    }
    else if (pid == this->audioPid) {
#ifdef TS_LOG
      printf ("demux_ts: Audio pid: %.4x\n", pid);
#endif
      demux_ts_buffer_pes (this, originalPkt+data_offset, this->audioMedia,
			   payload_unit_start_indicator, continuity_counter,
			   data_len);
      return;
    }
    else if (pid == 0) {
      demux_ts_parse_pat (this, originalPkt, originalPkt+data_offset-4,
			  payload_unit_start_indicator);
      return;
    }
    else if (pid == NULL_PID) {
#ifdef TS_LOG
      printf ("demux_ts: Null Packet\n");
#endif
      return;
    }
    if ((this->audioPid == INVALID_PID) && (this->videoPid == INVALID_PID)) {
      program_count = 0;
      while ((this->program_number[program_count] != INVALID_PROGRAM) ) {
	if (pid == this->pmt_pid[program_count]) {
#ifdef TS_LOG
	  printf ("demux_ts: PMT prog: %.4x pid: %.4x\n",
		  this->program_number[program_count],
		  this->pmt_pid[program_count]);
#endif
	  demux_ts_parse_pmt (this, originalPkt, originalPkt+data_offset-4,
			      payload_unit_start_indicator,
			      program_count);
	  return;
	}
	program_count++;
      }
    }
  }
}


/*
 * send a piece of data down the fifos
 */

static int demux_ts_send_chunk (demux_plugin_t *this_gen) {

  demux_ts_t*this = (demux_ts_t*)this_gen;

  demux_ts_parse_packet(this);

  return this->status;
}

static void demux_ts_dispose (demux_plugin_t *this_gen) {
  int i;
  demux_ts_t*this = (demux_ts_t*)this_gen;

  for (i=0; i < MAX_PMTS; i++) {
    if (this->pmt[i] != NULL) free(this->pmt[i]);
  }
  for (i=0; i < MAX_PIDS; i++) {
    if (this->media[i].buf != NULL) 
      this->media[i].buf->free_buffer(this->media[i].buf);
  }
  free(this_gen);
}

static int demux_ts_get_status(demux_plugin_t *this_gen) {

  demux_ts_t*this = (demux_ts_t*)this_gen;

  return this->status;
}

static void demux_ts_send_headers (demux_plugin_t *this_gen) {

  demux_ts_t *this = (demux_ts_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /*
   * send start buffers
   */

  this->videoPid = INVALID_PID;
  this->audioPid = INVALID_PID;

  xine_demux_control_start (this->stream);
  
  this->input->seek (this->input, 0, SEEK_SET);

  this->send_newpts = 1;
  this->ignore_scr_discont = 0;
  
  demux_ts_build_crc32_table (this);
  
  this->status = DEMUX_OK ;

  this->send_end_buffers  = 1;
  this->last_PCR          = 0;
  this->scrambled_npids   = 0;
  
  /* FIXME ? */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;

  xine_demux_control_headers_done (this->stream);
}

static int demux_ts_seek (demux_plugin_t *this_gen,
			  off_t start_pos, int start_time) {

  demux_ts_t *this = (demux_ts_t *) this_gen;
  int i;

  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {

    if ((!start_pos) && (start_time))
      start_pos = start_time * this->rate * 50;

    this->input->seek (this->input, start_pos, SEEK_SET);

  }

  this->send_newpts = 1;
  this->ignore_scr_discont = 0;
  
  for (i=0; i<MAX_PIDS; i++) {
    demux_ts_media *m = &this->media[i];

    if (m->buf != NULL) 
      m->buf->free_buffer(m->buf);
    m->buf            = NULL;
    m->counter        = INVALID_CC;
    m->corrupted_pes  = 1;
    m->buffered_bytes = 0;
  }

  if( !this->stream->demux_thread_running ) {
    
    this->status        = DEMUX_OK;
    this->buf_flag_seek = 0;

  } else {

    this->buf_flag_seek = 1;
    xine_demux_flush_engine(this->stream);

  }
  
  return this->status;
}

static int demux_ts_get_stream_length (demux_plugin_t *this_gen) {

  demux_ts_t*this = (demux_ts_t*)this_gen;

  if (this->rate)
    return this->input->get_length (this->input) / (this->rate * 50);
  else
    return 0;
}

static int demux_ts_get_video_frame (demux_plugin_t *this_gen,
				     int timestamp, 
				     int *width, int *height,
				     int *ratio_code, 
				     int *duration, 
				     int *format,
				     uint8_t *img) {

  /* demux_ts_t *this = (demux_ts_t*)this_gen; */

  return 0;
}


static demux_plugin_t *open_plugin (demux_class_t *class_gen, 
				    xine_stream_t *stream, 
				    input_plugin_t *input) {
  
  demux_ts_t *this;
  int         i;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    uint8_t buf[4096];
    int     got_sample;
    int     i, j;
    int     try_again, ts_detected;

    got_sample = 0;

    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
      input->seek(input, 0, SEEK_SET);

      if (input->read(input, buf, 2069)) 
	got_sample = 1;

    } else if ((input->get_capabilities(input) & INPUT_CAP_PREVIEW) != 0) {

      input->get_optional_data (input, buf, INPUT_OPTIONAL_DATA_PREVIEW);

      got_sample = 1;
    }

    if (!got_sample)
      return NULL;

    ts_detected = 0;

    for (i = 0; i < 188; i++) {
      try_again = 0;
      if (buf[i] == 0x47) {
	for (j = 1; j <= 10; j++) {
	  if (buf[i + j*188] != 0x47) {
	    try_again = 1;
	    break;
	  }
	}
	if (try_again == 0) {
#ifdef TS_LOG
	  printf ("demux_ts: found 0x47 pattern at offset %d\n", i);
#endif
	  ts_detected = 1;
	}
      }
    }

    if (!ts_detected)
      return NULL;
  }
    break;

  case METHOD_BY_EXTENSION: {

    char  *mrl;
    char  *ending;

    mrl = input->get_mrl (input);
    
    /*
     * check extension
     */
    
    ending = strrchr (mrl, '.');

    if (ending) {
      if (!strncasecmp(ending, ".ts", 3) ||
	  !strncasecmp(ending, ".m2t", 4) ||
	  !strncasecmp(ending, ".trp", 4)) {
	break;
      } 
    }

    /*
     * accept dvb streams 
     */

    if (!strncasecmp (mrl, "dvb://", 6))
      break;

    return NULL;
  }

  case METHOD_EXPLICIT:
    break;

  default:
    return NULL;
  }

  /*
   * if we reach this point, the input has been accepted.
   */

  this            = xine_xmalloc(sizeof(*this));
  this->stream    = stream;
  this->input     = input;
  this->blockSize = PKT_SIZE;

  this->demux_plugin.send_headers      = demux_ts_send_headers;
  this->demux_plugin.send_chunk        = demux_ts_send_chunk;
  this->demux_plugin.seek              = demux_ts_seek;
  this->demux_plugin.dispose           = demux_ts_dispose;
  this->demux_plugin.get_status        = demux_ts_get_status;
  this->demux_plugin.get_stream_length = demux_ts_get_stream_length;
  this->demux_plugin.get_video_frame   = demux_ts_get_video_frame;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.demux_class       = class_gen;
  
  /*
   * Initialise our specialised data.
   */
  for (i = 0; i < MAX_PIDS; i++) {
    this->media[i].pid = INVALID_PID;
    this->media[i].buf = NULL;
  }

  for (i = 0; i < MAX_PMTS; i++) {
    this->program_number[i]          = INVALID_PROGRAM;
    this->pmt_pid[i]                 = INVALID_PID;
    this->pmt[i]                     = NULL;
    this->pmt_write_ptr[i]           = NULL;
  }

  this->programNumber = INVALID_PROGRAM;
  this->pcrPid = INVALID_PID;
  this->PCR    = 0;
  this->scrambled_npids = 0;
  this->videoPid = INVALID_PID;
  this->audioPid = INVALID_PID;

  this->rate = 16000; /* FIXME */
  
  this->status = DEMUX_FINISHED;

#ifdef TS_READ_STATS
  for (i=0; i<=NPKT_PER_READ; i++) {
    this->rstat[i] = 0;
  }
#endif

  return &this->demux_plugin;
}

/*
 * ts demuxer class
 */

static char *get_description (demux_class_t *this_gen) {
  return "MPEG Transport Stream demuxer";
}
 
static char *get_identifier (demux_class_t *this_gen) {
  return "MPEG_TS";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "ts m2t trp";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_ts_class_t *this = (demux_ts_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {
  
  demux_ts_class_t     *this;
  
  this         = xine_xmalloc (sizeof (demux_ts_class_t));
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
  { PLUGIN_DEMUX, 16, "mpeg-ts", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
