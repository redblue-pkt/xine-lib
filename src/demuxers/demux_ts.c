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
 * $Id: demux_ts.c,v 1.38 2002/03/11 12:31:24 guenter Exp $
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
 * 07-Jan-2002 Andr Draszik <andid@gmx.net>
 *                              - added support for single-section PMTs
 *                                spanning multiple TS packets.
 * 10-Sep-2001 James Courtier-Dutton <jcdutton>
 *                              Re-wrote sync code so that it now does not loose any data.
 * 27-Aug-2001 Hubert Matthews  Reviewed by: n/a
 *                              Added in synchronisation code.
 *
 *  1-Aug-2001 James Courtier-Dutton <jcdutton>
 *                              Reviewed by: n/a
 *                              TS Streams with zero PES lenght should now work.
 *
 * 30-Jul-2001 shaheedhaque     Reviewed by: n/a
 *                              PATs and PMTs seem to work.
 *
 * 29-Jul-2001 shaheedhaque     Reviewed by: n/a
 *                              Compiles!

 *
 * TODO: do without memcpys, seeking (if possible), preview buffers
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"

#define VALID_MRLS   "fifo,stdin"
#define VALID_ENDS   "m2t,ts,trp"

#ifdef __GNUC__
#define LOG_MSG_STDERR(xine, message, args...) {                     \
    xine_log(xine, XINE_LOG_FORMAT, message, ##args);                 \
    fprintf(stderr, message, ##args);                                \
  }
#define LOG_MSG(xine, message, args...) {                            \
    xine_log(xine, XINE_LOG_FORMAT, message, ##args);                 \
    printf(message, ##args);                                         \
  }
#else
#define LOG_MSG_STDERR(xine, ...) {                                  \
    xine_log(xine, XINE_LOG_FORMAT, __VA_ARGS__);                     \
    fprintf(stderr, __VA_ARGS__);                                    \
  }
#define LOG_MSG(xine, ...) {                                         \
    xine_log(xine, XINE_LOG_FORMAT, __VA_ARGS__);                     \
    printf(__VA_ARGS__);                                             \
  }
#endif

/*
#define TS_LOG
#define TS_PMT_LOG
*/

/*
 * The maximum number of PIDs we are prepared to handle in a single program is the
 * number that fits in a single-packet PMT.
 */
#define PKT_SIZE 188
#define BODY_SIZE (188 - 4)
#define MAX_PIDS ((BODY_SIZE - 1 - 13) / 4)
#define MAX_PMTS ((BODY_SIZE - 1 - 13) / 4)
#define SYNC_BYTE   0x47
#define MIN_SYNCS   5
#define BUF_SIZE    ((MIN_SYNCS+1) * PKT_SIZE)

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
  uint32_t         PTS;
  buf_element_t   *buf;
  int              pes_buf_next;
  int              pes_len;
  int              pes_len_zero;
  unsigned int     counter;
  int              broken_pes;

} demux_ts_media;

typedef struct {
  /*
   * The first field must be the "base class" for the plugin!
   */
  demux_plugin_t   plugin;

  xine_t          *xine;
  
  config_values_t *config;

  fifo_buffer_t   *fifoAudio;
  fifo_buffer_t   *fifoVideo;

  input_plugin_t  *input;

  pthread_t        thread;

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
  uint32_t         PCR;
  unsigned int     pid;
  unsigned int     videoPid;
  unsigned int     audioPid;
  unsigned int     videoMedia;
  unsigned int     audioMedia;
} demux_ts;

static void demux_ts_build_crc32_table(demux_ts *this) {
  uint32_t  i, j, k;

  for( i = 0 ; i < 256 ; i++ )
  {
    k = 0;
    for (j = (i << 24) | 0x800000 ; j != 0x80000000 ; j <<= 1) {
      k = (k << 1) ^ (((k ^ j) & 0x80000000) ? 0x04c11db7 : 0);
    }
    this->crc32_table[i] = k;
  }
}

static uint32_t demux_ts_compute_crc32(demux_ts *this, uint8_t *data, uint32_t length, uint32_t crc32) {
  uint32_t i;

  for(i = 0; i < length; i++) {
    crc32 = (crc32 << 8) ^ this->crc32_table[(crc32 >> 24) ^ data[i]];
  }
  return crc32;
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
static void demux_ts_parse_pat (demux_ts *this, unsigned char *original_pkt,
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
    LOG_MSG (this->xine, _("demux_ts: FIXME: (unsupported )PAT spans "
                           "multiple TS packets\n"));
    return;
  }

  if ((section_number != 0) || (last_section_number != 0)) {
    LOG_MSG (this->xine, _("demux_ts: FIXME: (unsupported) PAT consists of "
                           "multiple (%d) sections\n"),
                         last_section_number);
    return;
  }

  /* Check CRC. */
  calc_crc32 = demux_ts_compute_crc32 (this, pkt+5, section_length+3-4,
                                       0xffffffff);
  if (crc32 != calc_crc32) {
    LOG_MSG (this->xine, _("demux_ts: demux error! PAT with invalid CRC32: "
                           "packet_crc32: %.8x calc_crc32: %.8x\n"),
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
    this->pmt_pid[program_count] = pmt_pid;
    this->pmt[program_count]                     = NULL;
    this->pmt_write_ptr[program_count]           = NULL;
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
                                      xine_t *xine) {

  unsigned char *p;
  uint32_t       header_len;
  uint32_t       PTS;
  uint32_t       stream_id;

  p = buf;

  /* we should have a PES packet here */

  if (p[0] || p[1] || (p[2] != 1)) {
    printf ("demux_ts: error %02x %02x %02x (should be 0x000001) \n",
            p[0], p[1], p[2]);
    LOG_MSG (xine, _("demux_ts: error %02x %02x %02x (should be 0x000001)\n"),
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

  if (p[7] & 0x80) { /* PTS avail */

    PTS  = (p[ 9] & 0x0E) << 29 ;
    PTS |=  p[10]         << 22 ;
    PTS |= (p[11] & 0xFE) << 14 ;
    PTS |=  p[12]         <<  7 ;
    PTS |= (p[13] & 0xFE) >>  1 ;

  } else
    PTS = 0;

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

  m->PTS       = PTS;
//  buf->input_pos = this->input->get_current_pos(this->input);
  /* FIXME: not working correctly */
//  buf->input_time = buf->input_pos / (this->rate * 50);

  header_len = p[8];

  p += header_len + 9;
  packet_len -= header_len + 3;

  if (stream_id == 0xbd) {

    int track, spu_id;

    track = p[0] & 0x0F; /* hack : ac3 track */

    if ((p[0] & 0xE0) == 0x20) {

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
 * buffer arriving pes data
 * Input is 188 bytes of Transport stream
 * Build a PES packet. PES packets can get as big as 65536
 * If PES packet length was empty(zero) work it out based on seeing the next PUS.
 * Once we have a complete PES packet, give PES packet a valid length field.
 * then queue it. The queuing routine might have to cut it up to make bits < 4096. FIXME: implement cut up.
 * Currently if PES packets are >4096, corruption occurs.
 */

static void demux_ts_buffer_pes(demux_ts *this, unsigned char *ts,
                                unsigned int mediaIndex,
                                unsigned int pus,
                                unsigned int cc,
                                unsigned int len) {

  buf_element_t *buf;

  demux_ts_media *m = &this->media[mediaIndex];
  if (!m->fifo) {

    LOG_MSG(this->xine, _("fifo unavailable (%d)\n"), mediaIndex);

    return; /* To avoid segfault if video out or audio out plugin not loaded */

  }

  /*
   * By checking the CC here, we avoid the need to check for the no-payload
   * case (i.e. adaptation field only) when it does not get bumped.
   */
  if (m->counter != INVALID_CC) {
    if ((m->counter & 0x0f) != cc) {
      LOG_MSG (this->xine, _("demux_ts: dropped input packet cc: %d "
                             "expected: %d\n"),
                           cc, m->counter);
    }
  }

  m->counter = cc;
  m->counter++;

  if (pus) {

    /* new PES packet */

    if (ts[0] || ts[1] || ts[2] != 1) {
      LOG_MSG_STDERR (this->xine, _("demux_ts: PUSI set but no PES header "
                                    "(corrupt stream?)\n"));
      return;
    }


    if (!demux_ts_parse_pes_header (m, ts, len, this->xine)) {
      m->broken_pes = 1;
      LOG_MSG (this->xine, _("demux_ts: broken pes encountered\n"));
    } else {
      m->broken_pes = 0;
      buf = m->fifo->buffer_pool_alloc(m->fifo);
      memcpy (buf->mem, ts+len-m->size, m->size); /* FIXME: reconstruct parser to do without memcpys */
      buf->content         = buf->mem;
      buf->size            = m->size;
      buf->type            = m->type;
      buf->pts             = m->PTS;
      /* buf->scr             = this->PCR; */
      buf->decoder_info[0] = 1;
      m->fifo->put (m->fifo, buf);
    }

  } else if (!m->broken_pes) {
    buf = m->fifo->buffer_pool_alloc(m->fifo);
    memcpy (buf->mem, ts, len); /* FIXME: reconstruct parser to do without memcpys */
    buf->content         = buf->mem;
    buf->size            = len;
    buf->type            = m->type;
    buf->pts             = 0;
    /* buf->scr             = 0; */
    buf->input_pos       = this->input->get_current_pos(this->input);
    buf->decoder_info[0] = 1;
    m->fifo->put (m->fifo, buf);
  }
}

/*
 * Create a buffer for a PES stream.
 */
static void demux_ts_pes_new(demux_ts *this,
                             unsigned int mediaIndex,
                             unsigned int pid,
                             fifo_buffer_t *fifo) {

  demux_ts_media *m = &this->media[mediaIndex];

  /* new PID seen - initialise stuff */
  m->pid = pid;
  m->fifo = fifo;
  m->buf = 0;
  m->pes_buf_next = 0;
  m->pes_len = 0;
  m->counter = INVALID_CC;
  m->broken_pes = 1;
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
static void demux_ts_parse_pmt (demux_ts      *this,
                                unsigned char *originalPkt,
                                unsigned char *pkt,
                                unsigned int   pusi,
                                uint32_t       program_count) {
  typedef enum
    {
      ISO_11172_VIDEO = 1, // 1
      ISO_13818_VIDEO = 2, // 2
      ISO_11172_AUDIO = 3, // 3
      ISO_13818_AUDIO = 4, // 4
      ISO_13818_PRIVATE = 5, // 5
      ISO_13818_PES_PRIVATE = 6, // 6
      ISO_13522_MHEG = 7, // 7
      ISO_13818_DSMCC = 8, // 8
      ISO_13818_TYPE_A = 9, // 9
      ISO_13818_TYPE_B = 10, // a
      ISO_13818_TYPE_C = 11, // b
      ISO_13818_TYPE_D = 12, // c
      ISO_13818_TYPE_E = 13, // d
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
    LOG_MSG_STDERR (this->xine, _("demux error! PMT with invalid pointer\n"));
    return;
  }

  /*
   * A new section should start with the payload unit start
   * indicator set. We allocate some mem (max. allowed for a PM section)
   * to copy the complete section into one chunk.
   */
  if (pusi) {
    free (this->pmt[program_count]);
    this->pmt[program_count] =
      (uint8_t *) calloc (1024, sizeof (unsigned char));
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
    LOG_MSG (this->xine, _("demux_ts: demux error! PMT with invalid CRC32: "
                           "packet_crc32: %#.8x calc_crc32: %#.8x\n"),
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
    LOG_MSG_STDERR (this->xine, _("demux error! PMT with inconsistent "
                                  "progInfo length\n"));
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
      LOG_MSG_STDERR (this->xine, _("demux error! PMT with inconsistent "
                                    "streamInfo length\n"));
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
        demux_ts_pes_new(this, mediaIndex, pid, this->fifoVideo);
      }
      this->videoPid = pid;
      this->videoMedia = mediaIndex;
      break;
    case ISO_11172_AUDIO:
    case ISO_13818_AUDIO:
      if (this->audioPid == INVALID_PID) {
#ifdef TS_PMT_LOG
        printf ("demux_ts: PMT audio pid %.4x\n", pid);
#endif
        demux_ts_pes_new(this, mediaIndex, pid, this->fifoAudio);
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
          demux_ts_pes_new(this, mediaIndex, pid, this->fifoAudio);
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
#ifdef TS_PMT_LOG
        printf ("demux_ts: PMT AC3 audio pid %.4x\n", pid);
#endif
        demux_ts_pes_new(this, mediaIndex, pid, this->fifoAudio);
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

void correct_for_sync(demux_ts *this, uint8_t *buf) {
  int32_t n, read_length;
  if((buf[0] == SYNC_BYTE) && (buf[PKT_SIZE] == SYNC_BYTE) &&
     (buf[PKT_SIZE*2] == SYNC_BYTE) && (buf[PKT_SIZE*3] == SYNC_BYTE)) {
        return;
  }
  for(n=1;n<PKT_SIZE;n++) {
    if((buf[n] == SYNC_BYTE) && (buf[n+PKT_SIZE] == SYNC_BYTE) &&
     (buf[n+(PKT_SIZE*2)] == SYNC_BYTE) && (buf[n+(PKT_SIZE*3)] == SYNC_BYTE)) {
      /* Found sync, fill in */
     memmove(&buf[0],&buf[n],((PKT_SIZE*MIN_SYNCS)-n));
     read_length = this->input->read(this->input, &buf[(PKT_SIZE*MIN_SYNCS)-n], n);
     return;
    }
  }
  LOG_MSG(this->xine, _("RE-Sync failed\n")); /* Sync up here */
  return;

}


/* Main synchronisation routine.
 */

static unsigned char * demux_synchronise(demux_ts * this) {
  static int32_t packet_number=MIN_SYNCS;
  static uint8_t buf[BUF_SIZE]; /* This should change to a malloc. */
  uint8_t       *return_pointer = NULL;
  int32_t n, read_length;

  if (packet_number == MIN_SYNCS) {
    for(n=0;n<MIN_SYNCS;n++) {
      read_length = this->input->read(this->input, &buf[n*PKT_SIZE], PKT_SIZE);
      if(read_length != PKT_SIZE) {
        this->status = DEMUX_FINISHED;
        return NULL;
      }
    }
    packet_number=0;
    correct_for_sync(this,&buf[0]);
  }
  return_pointer=&buf[PKT_SIZE*packet_number];
  packet_number++;
  return return_pointer;
}

static uint32_t demux_ts_adaptation_field_parse( uint8_t *data, uint32_t adaptation_field_length) {
  uint32_t    discontinuity_indicator=0;
  uint32_t    random_access_indicator=0;
  uint32_t    elementary_stream_priority_indicator=0;
  uint32_t    PCR_flag=0;
  uint32_t    PCR=0;
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
    PCR = data[offset] << 25;
    PCR |= data[offset+1] << 17;
    PCR |= data[offset+2] << 9;
    PCR |= data[offset+3] << 1;
    PCR |= (data[offset+4] >> 7) & 0x01;
    EPCR = ((data[offset+4] & 0x1) << 8) | data[offset+5];
#ifdef TS_LOG
    printf ("demux_ts: PCR: %u, EPCR: %u\n",
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
static void demux_ts_parse_packet (demux_ts *this) {

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

  /* get next synchronised packet, or NULL */
  originalPkt = demux_synchronise(this);
  if (originalPkt == NULL)
    return;

  sync_byte                      = originalPkt[0];
  transport_error_indicator      = (originalPkt[1]  >> 7) & 0x01;
  payload_unit_start_indicator   = (originalPkt[1] >> 6) & 0x01;
  transport_priority             = (originalPkt[1] >> 5) & 0x01;
  pid                            = ((originalPkt[1] << 8) | originalPkt[2]) & 0x1fff;
  transport_scrambling_control   = (originalPkt[3] >> 6)  & 0x03;
  adaptation_field_control       = (originalPkt[3] >> 4) & 0x03;
  continuity_counter             = originalPkt[3] & 0x0f;

  /*
   * Discard packets that are obviously bad.
   */
  if (sync_byte != 0x47) {
    LOG_MSG_STDERR (this->xine, _("demux error! invalid ts sync byte %.2x\n"),
                                originalPkt[0]);
    return;
  }
  if (transport_error_indicator) {
    LOG_MSG_STDERR(this->xine, _("demux error! transport error\n"));
    return;
  }

  data_offset = 4;
  if (adaptation_field_control & 0x1) {
    /*
     * Has a payload! Calculate & check payload length.
     */
    if (adaptation_field_control & 0x2) {
      uint32_t adaptation_field_length = originalPkt[4];
      if (adaptation_field_length > 0) {
        this->PCR = demux_ts_adaptation_field_parse (originalPkt+5,
                                                     adaptation_field_length);
      }
      /*
       * Skip adaptation header.
       */
      data_offset += adaptation_field_length + 1;
    }

    data_len = PKT_SIZE - data_offset;

    if (data_len > PKT_SIZE) {

      LOG_MSG (this->xine, _("demux_ts: demux error! invalid payload size %d\n"),
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
}

/*
 * Sit in a loop eating data.
 */
static void *demux_ts_loop(void *gen_this) {

  demux_ts *this = (demux_ts *)gen_this;
  buf_element_t *buf;

  do {
    demux_ts_parse_packet(this);
  } while (this->status == DEMUX_OK) ;

#ifdef TS_LOG
  printf ("demux_ts: demux loop finished (status: %d)\n", this->status);
#endif

  this->status = DEMUX_FINISHED;
  buf = this->fifoVideo->buffer_pool_alloc(this->fifoVideo);
  buf->type            = BUF_CONTROL_END;
  buf->decoder_info[0] = 0; /* stream finished */
  this->fifoVideo->put(this->fifoVideo, buf);

  if (this->fifoAudio) {
    buf = this->fifoAudio->buffer_pool_alloc(this->fifoAudio);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_info[0] = 0; /* stream finished */
    this->fifoAudio->put(this->fifoAudio, buf);
  }
  pthread_exit(NULL);
  return NULL;
}

static void demux_ts_close(demux_plugin_t *gen_this) {

  /* nothing */
}

static char *demux_ts_get_id(void) {
  return "MPEG_TS";
}

static char *demux_ts_get_mimetypes(void) {
  return "";
}

static int demux_ts_get_status(demux_plugin_t *this_gen) {

  demux_ts *this = (demux_ts *)this_gen;

  return this->status;
}

static int demux_ts_open(demux_plugin_t *this_gen, input_plugin_t *input,
                         int stage) {

  demux_ts *this = (demux_ts *) this_gen;
  char     *mrl;
  char     *media;
  char     *ending;
  char     *m, *valid_mrls, *valid_ends;

  switch (stage) {
  case STAGE_BY_CONTENT: {
    uint8_t buf[4096];

    if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
      input->seek(input, 0, SEEK_SET);

      if(input->get_blocksize(input))
       return DEMUX_CANNOT_HANDLE;

      if(input->read(input, buf, 6)) {

       if(buf[0] == 0x47)
       {
         this->input = input;
         return DEMUX_CAN_HANDLE;
       }
      }
    }
    return DEMUX_CANNOT_HANDLE;
  }
  break;
  case STAGE_BY_EXTENSION:

    xine_strdupa(valid_mrls, (this->config->register_string(this->config,
                                                            "mrl.mrls_ts", VALID_MRLS,
                                                            "valid mrls for ts demuxer",
                                                            NULL, NULL, NULL)));

    mrl = input->get_mrl(input);
    media = strstr(mrl, "://");

    if (media) {
      LOG_MSG_STDERR (this->xine, _("demux %u ts_open!\n"), __LINE__);
      while((m = xine_strsep(&valid_mrls, ",")) != NULL) {

        while(*m == ' ' || *m == '\t') m++;

        if(!strncmp(mrl, m, strlen(m))) {

          if(!strncmp((media + 3), "ts", 2)) {
            break;
          }
          return DEMUX_CANNOT_HANDLE;

        }
        else if(strncasecmp(mrl, "file", 4)) {
          return DEMUX_CANNOT_HANDLE;
        }
      }
    }

    ending = strrchr(mrl, '.');
    if (ending) {
#ifdef TS_LOG
      LOG_MSG(this->xine, "demux_ts_open: ending %s of %s\n", ending, mrl);
#endif

      xine_strdupa(valid_ends, (this->config->register_string(this->config,
                                                              "mrl.ends_ts", VALID_ENDS,
                                                              "valid mrls ending for ts demuxer",
                                                              NULL, NULL, NULL)));
      while((m = xine_strsep(&valid_ends, ",")) != NULL) {

        while(*m == ' ' || *m == '\t') m++;

        if(!strcasecmp((ending + 1), m)) {
          break;
        }
      }
    }
    return DEMUX_CANNOT_HANDLE;
    break;

  default:
    return DEMUX_CANNOT_HANDLE;
  }

  this->input = input;
  this->blockSize = PKT_SIZE;
  return DEMUX_CAN_HANDLE;
}

static void demux_ts_start(demux_plugin_t *this_gen,
                           fifo_buffer_t *fifoVideo,
                           fifo_buffer_t *fifoAudio,
                           off_t start_pos, int start_time) {

  demux_ts *this = (demux_ts *)this_gen;
  buf_element_t *buf;
  int err;

  this->fifoVideo = fifoVideo;
  this->fifoAudio = fifoAudio;

  /*
   * send start buffers
   */
  buf = this->fifoVideo->buffer_pool_alloc(this->fifoVideo);
  buf->type = BUF_CONTROL_START;
  this->fifoVideo->put(this->fifoVideo, buf);
  if (this->fifoAudio) {
    buf = this->fifoAudio->buffer_pool_alloc(this->fifoAudio);
    buf->type = BUF_CONTROL_START;
    this->fifoAudio->put(this->fifoAudio, buf);
  }

  this->status = DEMUX_OK ;


  if ((this->input->get_capabilities (this->input) & INPUT_CAP_SEEKABLE) != 0 ) {

    if ( (!start_pos) && (start_time))
      start_pos = start_time * this->rate * 50;

    this->input->seek (this->input, start_pos, SEEK_SET);

  }
  demux_ts_build_crc32_table(this);

  /*
   * Now start demuxing.
   */
  if ((err = pthread_create(&this->thread, NULL, demux_ts_loop, this)) != 0) {
    LOG_MSG_STDERR(this->xine, _("demux_ts: can't create new thread (%s)\n"), strerror(err));
    exit (1);
  }
}

static void demux_ts_stop(demux_plugin_t *this_gen)
{
  demux_ts *this = (demux_ts *)this_gen;
  buf_element_t *buf;
  void *p;

  LOG_MSG(this->xine, _("demux_ts: stop...\n"));

  if (this->status != DEMUX_OK) {

    this->fifoVideo->clear(this->fifoVideo);
    if(this->fifoAudio)
      this->fifoAudio->clear(this->fifoAudio);
    return;
  }

  this->status = DEMUX_FINISHED;

  pthread_cancel (this->thread);
  pthread_join (this->thread, &p);

  this->fifoVideo->clear(this->fifoVideo);
  if(this->fifoAudio)
    this->fifoAudio->clear(this->fifoAudio);

  buf = this->fifoVideo->buffer_pool_alloc (this->fifoVideo);
  buf->type            = BUF_CONTROL_END;
  buf->decoder_info[0] = 1; /* forced */
  this->fifoVideo->put (this->fifoVideo, buf);

  if (this->fifoAudio) {
    buf = this->fifoAudio->buffer_pool_alloc (this->fifoAudio);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_info[0] = 1; /* forced */
    this->fifoAudio->put (this->fifoAudio, buf);
  }
}

static int demux_ts_get_stream_length (demux_plugin_t *this_gen) {

  demux_ts *this = (demux_ts *)this_gen;

  return this->input->get_length (this->input) / (this->rate * 50);
}


demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {

  demux_ts        *this;
  int              i;

  if (iface != 6) {
    LOG_MSG (xine,
             _("demux_ts: plugin doesn't support plugin API version %d.\n"
               "          this means there's a version mismatch between xine and this "
               "          demuxer plugin.\nInstalling current demux plugins should help.\n"),
             iface);
    return NULL;
  }
  
  /*
   * Initialise the generic plugin.
   */
  this         = xine_xmalloc(sizeof(*this));
  this->config = xine->config;
  this->xine   = xine;

  (void*) this->config->register_string(this->config, "mrl.mrls_ts", VALID_MRLS,
                                        "valid mrls for ts demuxer",
                                        NULL, NULL, NULL);
  (void*) this->config->register_string(this->config,
                                        "mrl.ends_ts", VALID_ENDS,
                                        "valid mrls ending for ts demuxer",
                                        NULL, NULL, NULL);

  this->plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->plugin.open              = demux_ts_open;
  this->plugin.start             = demux_ts_start;
  this->plugin.stop              = demux_ts_stop;
  this->plugin.close             = demux_ts_close;
  this->plugin.get_status        = demux_ts_get_status;
  this->plugin.get_identifier    = demux_ts_get_id;
  this->plugin.get_stream_length = demux_ts_get_stream_length;
  this->plugin.get_mimetypes     = demux_ts_get_mimetypes;

  /*
   * Initialise our specialised data.
   */
  for (i = 0; i < MAX_PIDS; i++)
    this->media[i].pid = INVALID_PID;

  for (i = 0; i < MAX_PMTS; i++) {
    this->program_number[i]          = INVALID_PROGRAM;
    this->pmt_pid[i]                 = INVALID_PID;
    this->pmt[i]                     = NULL;
    this->pmt_write_ptr[i]           = NULL;
  }

  this->programNumber = INVALID_PROGRAM;
  this->pcrPid = INVALID_PID;
  this->PCR    = 0;
  this->videoPid = INVALID_PID;
  this->audioPid = INVALID_PID;

  this->rate = 16000; /* FIXME */

  return (demux_plugin_t *)this;
}



