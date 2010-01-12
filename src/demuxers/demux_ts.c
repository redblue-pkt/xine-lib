
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
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
 *  8-Apr-2009 Petri Hintukainen <phi@sdf-eu.org>
 *                  - support for 192-byte packets (HDMV/BluRay)
 *                  - support for audio inside PES PID 0xfd (HDMV/BluRay)
 *                  - demux HDMV/BluRay bitmap subtitles
 *
 * 28-Nov-2004 Mike Lampard <mlampard>
 *                  - Added support for PMT sections larger than 1 ts packet
 *
 * 28-Aug-2004 James Courtier-Dutton <jcdutton>
 *                  - Improve PAT and PMT handling. Added some FIXME comments.
 *
 *  9-Aug-2003 James Courtier-Dutton <jcdutton>
 *                  - Improve readability of code. Added some FIXME comments.
 *
 * 25-Nov-2002 Peter Liljenberg
 *                  - Added DVBSUB support
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
 *                  - TS Streams with zero PES length should now work
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


/** HOW TO IMPLEMENT A DVBSUB DECODER.
 *
 * The DVBSUB protocol is specified in ETSI EN 300 743.  It can be
 * downloaded for free (registration required, though) from
 * www.etsi.org.
 *
 * The spu_decoder should handle the packet type BUF_SPU_DVB.
 *
 * BUF_SPU_DVBSUB packets without the flag BUF_FLAG_SPECIAL contain
 * the payload of the PES packets carrying DVBSUB data.  Since the
 * payload can be broken up over several buf_element_t and the DVBSUB
 * is PES oriented, the decoder_info[2] field (low 16 bits) is used to convey the
 * packet boundaries to the decoder:
 *
 * + For the first buffer of a packet, buf->content points to the
 *   first byte of the PES payload.  decoder_info[2] is set to the length of the
 *   payload.  The decoder can use this value to determine when a
 *   complete PES packet has been collected.
 *
 * + For the following buffers of the PES packet, decoder_info[2] is 0.
 *
 * The decoder can either use this information to reconstruct the PES
 * payload, or ignore it and implement a parser that handles the
 * irregularites at the start and end of PES packets.
 *
 * In any case buf->pts is always set to the PTS of the PES packet.
 *
 *
 * BUF_SPU_DVB with BUF_FLAG_SPECIAL set contains no payload, and is
 * used to pass control information to the decoder.
 *
 * If decoder_info[1] == BUF_SPECIAL_SPU_DVB_DESCRIPTOR then
 * decoder_info_ptr[2] either points to a spu_dvb_descriptor_t or is NULL.
 *
 * If it is 0, the user has disabled the subtitling, or has selected a
 * channel that is not present in the stream.  The decoder should
 * remove any visible subtitling.
 *
 * If it is a pointer, the decoder should reset itself and start
 * extracting the subtitle service identified by comp_page_id and
 * aux_page_id in the spu_dvb_descriptor_t, (the composition and
 * auxilliary page ids, respectively).
 **/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <crc.h>
#else
#  include <libavutil/crc.h>
#endif

#define LOG_MODULE "demux_ts"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>

/*
  #define TS_LOG
  #define TS_PMT_LOG
  #define TS_PAT_LOG

  #define TS_READ_STATS // activates read statistics generation
  #define TS_HEADER_LOG // prints out the Transport packet header.
*/

/*
 *  The maximum number of PIDs we are prepared to handle in a single program
 *  is the number that fits in a single-packet PMT.
 */
#define PKT_SIZE 188
#define BODY_SIZE (188 - 4)
/* more PIDS are needed due "auto-detection". 40 spare media entries  */
#define MAX_PIDS ((BODY_SIZE - 1 - 13) / 4) + 40
#define MAX_PMTS ((BODY_SIZE - 1 - 13) / 4) + 10
#define SYNC_BYTE   0x47

#define MIN_SYNCS 3
#define NPKT_PER_READ 96  // 96*188 = 94*192

#define BUF_SIZE (NPKT_PER_READ * (PKT_SIZE + 4))

#define MAX_PES_BUF_SIZE 2048

#define CORRUPT_PES_THRESHOLD 10

#define NULL_PID 0x1fff
#define INVALID_PID ((unsigned int)(-1))
#define INVALID_PROGRAM ((unsigned int)(-1))
#define INVALID_CC ((unsigned int)(-1))

#define PROG_STREAM_MAP  0xBC
#define PRIVATE_STREAM1  0xBD
#define PADDING_STREAM   0xBE
#define PRIVATE_STREAM2  0xBF
#define AUDIO_STREAM_S   0xC0
#define AUDIO_STREAM_E   0xDF
#define VIDEO_STREAM_S   0xE0
#define VIDEO_STREAM_E   0xEF
#define ECM_STREAM       0xF0
#define EMM_STREAM       0xF1
#define DSM_CC_STREAM    0xF2
#define ISO13522_STREAM  0xF3
#define PROG_STREAM_DIR  0xFF

  typedef enum
    {
      ISO_11172_VIDEO = 0x01,           /* ISO/IEC 11172 Video */
      ISO_13818_VIDEO = 0x02,           /* ISO/IEC 13818-2 Video */
      ISO_11172_AUDIO = 0x03,           /* ISO/IEC 11172 Audio */
      ISO_13818_AUDIO = 0x04,           /* ISO/IEC 13818-3 Audi */
      ISO_13818_PRIVATE = 0x05,         /* ISO/IEC 13818-1 private sections */
      ISO_13818_PES_PRIVATE = 0x06,     /* ISO/IEC 13818-1 PES packets containing private data */
      ISO_13522_MHEG = 0x07,            /* ISO/IEC 13512 MHEG */
      ISO_13818_DSMCC = 0x08,           /* ISO/IEC 13818-1 Annex A  DSM CC */
      ISO_13818_TYPE_A = 0x0a,          /* ISO/IEC 13818-6 Multiprotocol encapsulation */
      ISO_13818_TYPE_B = 0x0b,          /* ISO/IEC 13818-6 DSM-CC U-N Messages */
      ISO_13818_TYPE_C = 0x0c,          /* ISO/IEC 13818-6 Stream Descriptors */
      ISO_13818_TYPE_D = 0x0d,          /* ISO/IEC 13818-6 Sections (any type, including private data) */
      ISO_13818_AUX = 0x0e,             /* ISO/IEC 13818-1 auxiliary */
      ISO_13818_PART7_AUDIO = 0x0f,     /* ISO/IEC 13818-7 Audio with ADTS transport sytax */
      ISO_14496_PART2_VIDEO = 0x10,     /* ISO/IEC 14496-2 Visual (MPEG-4) */
      ISO_14496_PART3_AUDIO = 0x11,     /* ISO/IEC 14496-3 Audio with LATM transport syntax */
      ISO_14496_PART10_VIDEO = 0x1b,    /* ISO/IEC 14496-10 Video (MPEG-4 part 10/AVC, aka H.264) */
      STREAM_VIDEO_MPEG      = 0x80,
      STREAM_AUDIO_AC3       = 0x81,

      STREAM_VIDEO_VC1       = 0xea,    /* VC-1 Video */

      HDMV_AUDIO_80_PCM       = 0x80, /* BluRay PCM */
      HDMV_AUDIO_82_DTS       = 0x82, /* DTS */
      HDMV_AUDIO_83_TRUEHD    = 0x83, /* Dolby TrueHD, primary audio */
      HDMV_AUDIO_84_EAC3      = 0x84, /* Dolby Digital plus, primary audio */
      HDMV_AUDIO_85_DTS_HRA   = 0x85, /* DTS-HRA */
      HDMV_AUDIO_86_DTS_HD_MA = 0x86, /* DTS-HD Master audio */

      HDMV_SPU_BITMAP      = 0x90,
      HDMV_SPU_INTERACTIVE = 0x91,
      HDMV_SPU_TEXT        = 0x92,

    } streamType;

#define WRAP_THRESHOLD       270000

#define PTS_AUDIO 0
#define PTS_VIDEO 1

#undef  MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#undef  MAX
#define MAX(a,b) ((a)>(b)?(a):(b))

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
  uint16_t         descriptor_tag; /* +0x100 for PES stream IDs (no available TS descriptor tag?) */
  int64_t          packet_count;
  int              corrupted_pes;
  uint32_t         buffered_bytes;
  int              autodetected;

} demux_ts_media;

/* DVBSUB */
#define MAX_SPU_LANGS 32

typedef struct {
  spu_dvb_descriptor_t desc;
  int pid;
  int media_index;
} demux_ts_spu_lang;

/* Audio Channels */
#define MAX_AUDIO_TRACKS 32

typedef struct {
    int pid;
    int media_index;
    char lang[4];
} demux_ts_audio_track;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;

  const AVCRC      *av_crc;
} demux_ts_class_t;

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

  demux_ts_class_t *class;

  int              status;

  int              hdmv;       /* -1 = unknown, 0 = mpeg-ts, 1 = hdmv/m2ts */
  int              pkt_size;   /* TS packet size */
  int              pkt_offset; /* TS packet offset */

  int              blockSize;
  int              rate;
  int              media_num;
  demux_ts_media   media[MAX_PIDS];
  uint32_t         program_number[MAX_PMTS];
  uint32_t         pmt_pid[MAX_PMTS];
  uint8_t         *pmt[MAX_PMTS];
  uint8_t         *pmt_write_ptr[MAX_PMTS];
  uint32_t         last_pmt_crc;
  /*
   * Stuff to do with the transport header. As well as the video
   * and audio PIDs, we keep the index of the corresponding entry
   * inthe media[] array.
   */
  unsigned int     programNumber;
  unsigned int     pcrPid;
  unsigned int     pid;
  unsigned int     pid_count;
  unsigned int     videoPid;
  unsigned int     videoMedia;

  demux_ts_audio_track audio_tracks[MAX_AUDIO_TRACKS];
  int              audio_tracks_count;

  int              send_end_buffers;
  int64_t          last_pts[2];
  int              send_newpts;
  int              buf_flag_seek;

  unsigned int     scrambled_pids[MAX_PIDS];
  unsigned int     scrambled_npids;

#ifdef TS_READ_STATS
  uint32_t         rstat[NPKT_PER_READ + 1];
#endif

  /* DVBSUB */
  unsigned int      spu_pid;
  unsigned int      spu_media;
  demux_ts_spu_lang spu_langs[MAX_SPU_LANGS];
  int               spu_langs_count;
  int               current_spu_channel;

  /* dvb */
  xine_event_queue_t *event_queue;
  /* For syncronisation */
  int32_t packet_number;
  /* NEW: var to keep track of number of last read packets */
  int32_t npkt_read;

  uint8_t buf[BUF_SIZE]; /* == PKT_SIZE * NPKT_PER_READ */

  int numPreview;

} demux_ts_t;

/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

static void check_newpts( demux_ts_t *this, int64_t pts, int video )
{
  int64_t diff;

#ifdef TS_LOG
  printf ("demux_ts: check_newpts %lld, send_newpts %d, buf_flag_seek %d\n",
	  pts, this->send_newpts, this->buf_flag_seek);
#endif

  diff = pts - this->last_pts[video];

  if( pts &&
      (this->send_newpts || (this->last_pts[video] && abs(diff)>WRAP_THRESHOLD) ) ) {

    if (this->buf_flag_seek) {
      _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      _x_demux_control_newpts(this->stream, pts, 0);
    }
    this->send_newpts = 0;
    this->last_pts[1-video] = 0;
  }

  if( pts )
  {
    /* don't detect a discontinuity only for video respectively audio. It's also a discontinuity
       indication when audio and video pts differ to much e. g. when a pts wrap happens.
       The original code worked well when the wrap happend like this:

       V7 A7 V8 V9 A9 Dv V0 V1 da A1 V2 V3 A3 V4

       Legend:
       Vn = video packet with timestamp n
       An = audio packet with timestamp n
       Dv = discontinuity detected on following video packet
       Da = discontinuity detected on following audio packet
       dv = discontinuity detected on following video packet but ignored
       da = discontinuity detected on following audio packet but ignored

       But with a certain delay between audio and video packets (e. g. the way DVB-S broadcasts
       the packets) the code didn't work:

       V7 V8 A7 V9 Dv V0 _A9_ V1 V2 Da _A1_ V3 V4 A3

       Packet A9 caused audio to jump forward and A1 caused it to jump backward with inserting
       a delay of almoust 26.5 hours!

       The new code gives the following sequences for the above examples:

       V7 A7 V8 V9 A9 Dv V0 V1 A1 V2 V3 A3 V4

       V7 V8 A7 V9 Dv V0 Da A9 Dv V1 V2 A1 V3 V4 A3

       After proving this code it should be cleaned up to use just a single variable "last_pts". */

/*
    this->last_pts[video] = pts;
*/
    this->last_pts[video] = this->last_pts[1-video] = pts;
  }
}

/* Send a BUF_SPU_DVB to let xine know of that channel. */
static void demux_send_special_spu_buf( demux_ts_t *this, uint32_t spu_type, int spu_channel )
{
  buf_element_t *buf;

  buf = this->video_fifo->buffer_pool_alloc( this->video_fifo );
  buf->type = spu_type|spu_channel;
  buf->content = buf->mem;
  buf->size = 0;
  this->video_fifo->put( this->video_fifo, buf );
}

/*
 * demux_ts_update_spu_channel
 *
 * Send a BUF_SPU_DVB with BUF_SPECIAL_SPU_DVB_DESCRIPTOR to tell
 * the decoder to reset itself on the new channel.
 */
static void demux_ts_update_spu_channel(demux_ts_t *this)
{
  buf_element_t *buf;

  this->current_spu_channel = this->stream->spu_channel;

  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
  buf->type = BUF_SPU_DVB;
  buf->content = buf->mem;
  buf->decoder_flags = BUF_FLAG_SPECIAL;
  buf->decoder_info[1] = BUF_SPECIAL_SPU_DVB_DESCRIPTOR;
  buf->size = 0;

  if (this->current_spu_channel >= 0
      && this->current_spu_channel < this->spu_langs_count)
    {
      demux_ts_spu_lang *lang = &this->spu_langs[this->current_spu_channel];

      buf->decoder_info[2] = sizeof(lang->desc);
      buf->decoder_info_ptr[2] = &(lang->desc);
      buf->type |= this->current_spu_channel;

      this->spu_pid = lang->pid;
      this->spu_media = lang->media_index;

#ifdef TS_LOG
      printf("demux_ts: DVBSUB: selecting lang: %s  page %ld %ld\n",
	     lang->desc.lang, lang->desc.comp_page_id, lang->desc.aux_page_id);
#endif
    }
  else
    {
      buf->decoder_info_ptr[2] = NULL;

      this->spu_pid = INVALID_PID;

#ifdef TS_LOG
      printf("demux_ts: DVBSUB: deselecting lang\n");
#endif
    }

 if ((this->media[this->spu_media].type & BUF_MAJOR_MASK) == BUF_SPU_HDMV) {
   buf->type = BUF_SPU_HDMV;
 }

  this->video_fifo->put(this->video_fifo, buf);
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
  int32_t        section_length;
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
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: demux error! PAT without payload unit start indicator\n");
    return;
  }

  /*
   * sections start with a pointer. Skip it!
   */
  pkt += pkt[4];
  if (pkt - original_pkt > PKT_SIZE) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: demux error! PAT with invalid pointer\n");
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

#ifdef TS_PAT_LOG
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
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: FIXME: (unsupported )PAT spans multiple TS packets\n");
    return;
  }

  if ((section_number != 0) || (last_section_number != 0)) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: FIXME: (unsupported) PAT consists of multiple (%d) sections\n", last_section_number);
    return;
  }

  /* Check CRC. */
  calc_crc32 = htonl(av_crc(this->class->av_crc, 0xffffffff, pkt+5, section_length+3-4));
  if (crc32 != calc_crc32) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: demux error! PAT with invalid CRC32: packet_crc32: %.8x calc_crc32: %.8x\n",
	     crc32,calc_crc32);
    return;
  }
#ifdef TS_PAT_LOG
  else {
    printf ("demux_ts: PAT CRC32 ok.\n");
  }
#endif

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
           (this->program_number[program_count] != program_number)  &&
           (program_count+1 < MAX_PMTS ) ) {
      program_count++;
    }
    this->program_number[program_count] = program_number;

    /* force PMT reparsing when pmt_pid changes */
    if (this->pmt_pid[program_count] != pmt_pid) {
      this->pmt_pid[program_count] = pmt_pid;
      this->audio_tracks_count = 0;
      this->last_pmt_crc = 0;
      this->videoPid = INVALID_PID;
      this->spu_pid = INVALID_PID;

      if (this->pmt[program_count] != NULL) {
	free(this->pmt[program_count]);
	this->pmt[program_count] = NULL;
	this->pmt_write_ptr[program_count] = NULL;
      }
    }
#ifdef TS_PAT_LOG
    if (this->program_number[program_count] != INVALID_PROGRAM)
      printf ("demux_ts: PAT acquired count=%d programNumber=0x%04x "
              "pmtPid=0x%04x\n",
              program_count,
              this->program_number[program_count],
              this->pmt_pid[program_count]);
#endif
  }
}

static int demux_ts_parse_pes_header (xine_t *xine, demux_ts_media *m,
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
    xprintf (xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: error %02x %02x %02x (should be 0x000001) \n", p[0], p[1], p[2]);
    return 0 ;
  }

  packet_len -= 6;
  /* packet_len = p[4] << 8 | p[5]; */
  stream_id  = p[3];

  if (packet_len==0)
  {
    xprintf (xine, XINE_VERBOSITY_DEBUG,
             "demux_ts: error pes length 0\n");
    return 0;
  }

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
    xprintf (xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: illegal value for PES_header_data_length (0x%x)\n", header_len);
    return 0;
  }

  p += header_len + 9;
  packet_len -= header_len + 3;

  if (m->descriptor_tag == STREAM_VIDEO_VC1) {
    m->content   = p;
    m->size      = packet_len;
    m->type      = BUF_VIDEO_VC1;
    return 1;
  }

  if (m->descriptor_tag == HDMV_SPU_BITMAP) {
    long payload_len = ((buf[4] << 8) | buf[5]) - header_len - 3;

    m->content = p;
    m->size = packet_len;
    m->type |= BUF_SPU_HDMV;
    m->buf->decoder_info[2] = payload_len;
    return 1;

  } else

  if (stream_id == 0xbd || stream_id == 0xfd /* HDMV */) {

    int spu_id;

    lprintf ("audio buf = %02X %02X %02X %02X %02X %02X %02X %02X\n",
	     p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

    /*
     * we check the descriptor tag first because some stations
     * do not include any of the ac3 header info in their audio tracks
     * these "raw" streams may begin with a byte that looks like a stream type.
     * For audio streams, m->type already contains the stream no.
     */
    if((m->descriptor_tag == STREAM_AUDIO_AC3) ||    /* ac3 - raw */
       (p[0] == 0x0B && p[1] == 0x77)) { /* ac3 - syncword */
      m->content   = p;
      m->size = packet_len;
      m->type |= BUF_AUDIO_A52;
      return 1;

    } else if (m->descriptor_tag == HDMV_AUDIO_83_TRUEHD) {
      /* TODO: separate AC3 and TrueHD streams ... */
      m->content = p;
      m->size = packet_len;
      m->type |= BUF_AUDIO_A52;
      return 1;

    } else if (m->descriptor_tag == HDMV_AUDIO_82_DTS ||
               m->descriptor_tag == HDMV_AUDIO_86_DTS_HD_MA ) {
      m->content = p;
      m->size = packet_len;
      m->type |= BUF_AUDIO_DTS;
      return 1;

    } else if (m->descriptor_tag == HDMV_AUDIO_80_PCM) {

      m->content = p + 4;
      m->size    = packet_len - 4;
      m->type   |= BUF_AUDIO_LPCM_BE;

      m->buf->decoder_flags  |= BUF_FLAG_SPECIAL;
      m->buf->decoder_info[1] = BUF_SPECIAL_LPCM_CONFIG;
      m->buf->decoder_info[2] = (p[3]<<24) | (p[2]<<16) | (p[1]<<8) | p[0];

      return 1;

    } else if (m->descriptor_tag == ISO_13818_PES_PRIVATE
	     && p[0] == 0x20 && p[1] == 0x00) {
      /* DVBSUB */
      long payload_len = ((buf[4] << 8) | buf[5]) - header_len - 3;

      m->content = p;
      m->size = packet_len;
      m->type |= BUF_SPU_DVB;
      m->buf->decoder_info[2] = payload_len;
      return 1;
    } else if ((p[0] & 0xE0) == 0x20) {
      spu_id = (p[0] & 0x1f);

      m->content   = p+1;
      m->size      = packet_len-1;
      m->type      = BUF_SPU_DVD + spu_id;
      return 1;
    } else if ((p[0] & 0xF0) == 0x80) {

      m->content   = p+4;
      m->size      = packet_len - 4;
      m->type      |= BUF_AUDIO_A52;
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
      m->type      |= BUF_AUDIO_LPCM_BE;
      return 1;
    }

  } else if ((stream_id >= 0xbc) && ((stream_id & 0xf0) == 0xe0)) {

    m->content   = p;
    m->size      = packet_len;
    switch (m->descriptor_tag) {
    case ISO_11172_VIDEO:
    case ISO_13818_VIDEO:
    case STREAM_VIDEO_MPEG:
      lprintf ("demux_ts: found MPEG video type.\n");
      m->type      = BUF_VIDEO_MPEG;
      break;
    case ISO_14496_PART2_VIDEO:
      lprintf ("demux_ts: found MPEG4 video type.\n");
      m->type      = BUF_VIDEO_MPEG4;
      break;
    case ISO_14496_PART10_VIDEO:
      lprintf ("demux_ts: found H264 video type.\n");
      m->type      = BUF_VIDEO_H264;
      break;
    default:
      lprintf ("demux_ts: unknown video type: %d, defaulting to MPEG.\n", m->descriptor_tag);
      m->type      = BUF_VIDEO_MPEG;
      break;
    }
    return 1;

  } else if ((stream_id & 0xe0) == 0xc0) {

    m->content   = p;
    m->size      = packet_len;
    switch (m->descriptor_tag) {
    case  ISO_11172_AUDIO:
    case  ISO_13818_AUDIO:
      lprintf ("demux_ts: found MPEG audio track.\n");
      m->type      |= BUF_AUDIO_MPEG;
      break;
    case  ISO_13818_PART7_AUDIO:
    case  ISO_14496_PART3_AUDIO:
      lprintf ("demux_ts: found AAC audio track.\n");
      m->type      |= BUF_AUDIO_AAC;
      break;
    default:
      lprintf ("demux_ts: unknown audio type: %d, defaulting to MPEG.\n", m->descriptor_tag);
      m->type      |= BUF_AUDIO_MPEG;
      break;
    }
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
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "demux_ts: PID 0x%.4x: unexpected cc %d (expected %d)\n", m->pid, cc, m->counter);
    }
  }
  m->counter = cc;
  m->counter++;

  if (pus) { /* new PES packet */

    if (m->buffered_bytes) {
      m->buf->content = m->buf->mem;
      m->buf->size = m->buffered_bytes;
      m->buf->type = m->type;
      if( (m->buf->type & 0xffff0000) == BUF_SPU_DVD ) {
        m->buf->decoder_flags |= BUF_FLAG_SPECIAL;
        m->buf->decoder_info[1] = BUF_SPECIAL_SPU_DVD_SUBTYPE;
        m->buf->decoder_info[2] = SPU_DVD_SUBTYPE_PACKAGE;
      }
      else {
        if (this->numPreview<5)
	  ++this->numPreview;
	if ( this->numPreview==1 )
	  m->buf->decoder_flags=BUF_FLAG_HEADER | BUF_FLAG_FRAME_END;
	else if ( this->numPreview<5 )
	  m->buf->decoder_flags=BUF_FLAG_PREVIEW;
	else
	  m->buf->decoder_flags |= BUF_FLAG_FRAME_END;
      }
      m->buf->pts = m->pts;
      m->buf->decoder_info[0] = 1;

      if( this->input->get_length (this->input) )
        m->buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                         65535 / this->input->get_length (this->input) );
      if (this->rate)
        m->buf->extra_info->input_time = (int)((int64_t)this->input->get_current_pos (this->input)
                                         * 1000 / (this->rate * 50));
      m->fifo->put(m->fifo, m->buf);
      m->buffered_bytes = 0;
      m->buf = NULL; /* forget about buf -- not our responsibility anymore */
#ifdef TS_LOG
      printf ("demux_ts: produced buffer, pts=%lld\n", m->pts);
#endif
    }
    /* allocate the buffer here, as pes_header needs a valid buf for dvbsubs */
     m->buf = m->fifo->buffer_pool_alloc(m->fifo);

    if (!demux_ts_parse_pes_header(this->stream->xine, m, ts, len, this->stream)) {
      m->buf->free_buffer(m->buf);
      m->buf = NULL;

      if (m->corrupted_pes > CORRUPT_PES_THRESHOLD && m->autodetected) {
        if (this->videoPid == m->pid) {
          this->videoPid = INVALID_PID;
          this->last_pmt_crc = 0;
        }
      } else {
        m->corrupted_pes++;
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "demux_ts: PID 0x%.4x: corrupted pes encountered\n", m->pid);
      }
    } else {

      m->corrupted_pes = 0;
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
      if( this->input->get_length (this->input) )
        m->buf->extra_info->input_normpos = (int)( (double) this->input->get_current_pos (this->input) *
                                         65535 / this->input->get_length (this->input) );
      if (this->rate)
        m->buf->extra_info->input_time = (int)((int64_t)this->input->get_current_pos (this->input)
                                         * 1000 / (this->rate * 50));

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
			     uint16_t descriptor) {

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


/* Find the first ISO 639 language descriptor (tag 10) and
 * store the 3-char code in dest, nullterminated.  If no
 * code is found, zero out dest.
 **/
static void demux_ts_get_lang_desc(demux_ts_t *this, char *dest,
				   const unsigned char *data, int length)
{
  const unsigned char *d = data;

  while (d < (data + length))

    {
      if (d[0] == 10 && d[1] >= 4)

	{
      memcpy(dest, d + 2, 3);
	  dest[3] = 0;
	  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: found ISO 639 lang: %s\n", dest);
	  return;
	}
      d += 2 + d[1];
    }
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: found no ISO 639 lang\n");
  memset(dest, 0, 4);
}

/* Find the registration code (tag=5) and return it as a uint32_t
 * This should return "AC-3" or 0x41432d33 for AC3/A52 audio tracks.
 */
static void demux_ts_get_reg_desc(demux_ts_t *this, uint32_t *dest,
				   const unsigned char *data, int length)
{
  const unsigned char *d = data;

  while (d < (data + length))

    {
      if (d[0] == 5 && d[1] >= 4)

	{
          *dest = (d[2] << 24) | (d[3] << 16) | (d[4] << 8) | d[5];
	  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		  "demux_ts: found registration format identifier: 0x%.4x\n", *dest);
	  return;
	}
      d += 2 + d[1];
    }
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: found no format id\n");
  *dest = 0;
}

static inline int ts_payloadsize(unsigned char * tsp)
{
  if (!(tsp[3] & 0x10))
     return 0;
  if (tsp[3] & 0x20) {
     if (tsp[4] > 183)
       return 0;
     else
       return 183 - tsp[4];
  }
  return 184;
}


/*
 * NAME demux_ts_parse_pmt
 *
 * Parse a PMT. The PMT is expected to be exactly one section long,
 * and that section is expected to be contained in a single TS packet.
 *
 * In other words, the PMT is assumed to describe a reasonable number of
 * video, audio and other streams (with descriptors).
 * FIXME: Implement support for multi section PMT.
 */
static void demux_ts_parse_pmt (demux_ts_t     *this,
                                unsigned char *originalPkt,
                                unsigned char *pkt,
                                unsigned int   pusi,
                                uint32_t       program_count) {

  uint32_t       table_id;
  uint32_t       section_syntax_indicator;
  uint32_t       section_length = 0; /* to calm down gcc */
  uint32_t       program_number;
  uint32_t       version_number;
  uint32_t       current_next_indicator;
  uint32_t       section_number;
  uint32_t       last_section_number;
  uint32_t       program_info_length;
  uint32_t       crc32;
  uint32_t       calc_crc32;
  uint32_t       coded_length;
  unsigned int	 pid;
  unsigned char *stream;
  unsigned int	 i;
  int		 count;
  uint8_t	*ptr = NULL;
  unsigned char  len;
  unsigned int   offset=0;

  /*
   * A new section should start with the payload unit start
   * indicator set. We allocate some mem (max. allowed for a PM section)
   * to copy the complete section into one chunk.
   */
  if (pusi) {
    pkt+=pkt[4]; /* pointer to start of section */
    offset=1;

    if (this->pmt[program_count] != NULL) free(this->pmt[program_count]);
    this->pmt[program_count] = (uint8_t *) calloc(4096, sizeof(unsigned char));
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
printf("Program Number is %i, looking for %i\n",program_number,this->program_number[program_count]);
      printf ("ts_demux: waiting for next PMT on this PID...\n");
#endif
      return;
    }

    if ((section_number != 0) || (last_section_number != 0)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	       "demux_ts: FIXME (unsupported) PMT consists of multiple (%d) sections\n", last_section_number);
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

  if (!pusi){
    section_length = (this->pmt[program_count][1] << 8
		      | this->pmt[program_count][2]) & 0x03ff;
  }

  count=ts_payloadsize(originalPkt);

  ptr = originalPkt+offset+(PKT_SIZE-count);
  len = count-offset;
  memcpy (this->pmt_write_ptr[program_count], ptr, len);
  this->pmt_write_ptr[program_count] +=len;

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

  if (!section_length) {
    free (this->pmt[program_count]);
    this->pmt[program_count] = NULL;
#ifdef TS_PMT_LOG
    printf ("ts_demux: eek, zero-length section?\n");
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
  calc_crc32 = htonl(av_crc(this->class->av_crc, 0xffffffff,
			    this->pmt[program_count], section_length+3-4));

  if (crc32 != calc_crc32) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: demux error! PMT with invalid CRC32: packet_crc32: %#.8x calc_crc32: %#.8x\n",
	     crc32,calc_crc32);
    return;
  }
  else {
#ifdef TS_PMT_LOG
    printf ("demux_ts: PMT CRC32 ok.\n");
#endif
    if ( crc32==this->last_pmt_crc ) {
#ifdef TS_PMT_LOG
      printf("demux_ts: PMT with CRC32=%d already parsed. Skipping.\n", crc32);
#endif
      return;
    }
    else {
#ifdef TS_PMT_LOG
      printf("demux_ts: new PMT, parsing...\n");
#endif
      this->last_pmt_crc = crc32;
    }
  }

  /*
   * Forget the current video, audio and subtitle PIDs; if the PMT has not
   * changed, we'll pick them up again when we parse this PMT, while if the
   * PMT has changed (e.g. an IPTV streamer that's just changed its source),
   * we'll get new PIDs that we should follow.
   */
  this->audio_tracks_count = 0;
  this->videoPid = INVALID_PID;
  this->spu_pid = INVALID_PID;

  /*
   * ES definitions start here...we are going to learn upto one video
   * PID and one audio PID.
   */
  program_info_length = ((this->pmt[program_count][10] << 8)
                       | this->pmt[program_count][11]) & 0x0fff;

/* Program info descriptor is currently just ignored.
 * printf ("demux_ts: program_info_desc: ");
 * for (i = 0; i < program_info_length; i++)
 *       printf ("%.2x ", this->pmt[program_count][12+i]);
 * printf ("\n");
 */
  stream = &this->pmt[program_count][12] + program_info_length;
  coded_length = 13 + program_info_length;
  if (coded_length > section_length) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux error! PMT with inconsistent progInfo length\n");
    return;
  }
  section_length -= coded_length;

  /*
   * Extract the elementary streams.
   */
  this->spu_langs_count = 0;
  while (section_length > 0) {
    unsigned int stream_info_length;

    pid = ((stream[1] << 8) | stream[2]) & 0x1fff;
    stream_info_length = ((stream[3] << 8) | stream[4]) & 0x0fff;
    coded_length = 5 + stream_info_length;
    if (coded_length > section_length) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	       "demux error! PMT with inconsistent streamInfo length\n");
      return;
    }

    /*
     * Squirrel away the first audio and the first video stream. TBD: there
     * should really be a way to select the stream of interest.
     */
    switch (stream[0]) {
    case ISO_11172_VIDEO:
    case ISO_13818_VIDEO:
    case ISO_14496_PART2_VIDEO:
    case ISO_14496_PART10_VIDEO:
    case STREAM_VIDEO_VC1:
      if (this->videoPid == INVALID_PID) {
#ifdef TS_PMT_LOG
        printf ("demux_ts: PMT video pid 0x%.4x type %2.2x\n", pid, stream[0]);
#endif
        demux_ts_pes_new(this, this->media_num, pid, this->video_fifo,stream[0]);
	this->videoMedia = this->media_num;
	this->videoPid = pid;
      }

      break;
    case ISO_11172_AUDIO:
    case ISO_13818_AUDIO:
    case ISO_13818_PART7_AUDIO:
    case ISO_14496_PART3_AUDIO:
      if (this->audio_tracks_count < MAX_AUDIO_TRACKS) {
        int i, found = 0;
        for(i = 0; i < this->audio_tracks_count; i++) {
            if(this->audio_tracks[i].pid == pid) {
                found = 1;
                break;
            }
        }
        if(!found) {
#ifdef TS_PMT_LOG
            printf ("demux_ts: PMT audio pid 0x%.4x type %2.2x\n", pid, stream[0]);
#endif
            demux_ts_pes_new(this, this->media_num, pid, this->audio_fifo,stream[0]);
            this->audio_tracks[this->audio_tracks_count].pid = pid;
            this->audio_tracks[this->audio_tracks_count].media_index = this->media_num;
            this->media[this->media_num].type = this->audio_tracks_count;
            demux_ts_get_lang_desc(this, this->audio_tracks[this->audio_tracks_count].lang,
			       stream + 5, stream_info_length);
            this->audio_tracks_count++;
        }
      }
      break;
    case ISO_13818_PRIVATE:
#ifdef TS_PMT_LOG
      printf ("demux_ts: PMT streamtype 13818_PRIVATE, pid: 0x%.4x type %2.2x\n", pid, stream[0]);

      for (i = 5; i < coded_length; i++)
        printf ("%.2x ", stream[i]);
      printf ("\n");
#endif
      break;
    case  ISO_13818_TYPE_C: /* data carousel */
#ifdef TS_PMT_LOG
      printf ("demux_ts: PMT streamtype 13818_TYPE_C, pid: 0x%.4x type %2.2x\n", pid, stream[0]);
#endif
      break;
    case ISO_13818_PES_PRIVATE:
      for (i = 5; i < coded_length; i += stream[i+1] + 2) {
          if ((stream[i] == 0x6a) && (this->audio_tracks_count < MAX_AUDIO_TRACKS)) {
          int i, found = 0;
          for(i = 0; i < this->audio_tracks_count; i++) {
            if(this->audio_tracks[i].pid == pid) {
              found = 1;
              break;
             }
	  }
          if(!found) {
#ifdef TS_PMT_LOG
            printf ("demux_ts: PMT AC3 audio pid 0x%.4x type %2.2x\n", pid, stream[0]);
#endif
          demux_ts_pes_new(this, this->media_num, pid,
                           this->audio_fifo, STREAM_AUDIO_AC3);

          this->audio_tracks[this->audio_tracks_count].pid = pid;
          this->audio_tracks[this->audio_tracks_count].media_index = this->media_num;
          this->media[this->media_num].type = this->audio_tracks_count;
          demux_ts_get_lang_desc(this, this->audio_tracks[this->audio_tracks_count].lang,
                                 stream + 5, stream_info_length);
          this->audio_tracks_count++;
          break;
          }
        }
        /* Teletext */
        else if (stream[i] == 0x56)
          {
#ifdef TS_PMT_LOG
            printf ("demux_ts: PMT Teletext, pid: 0x%.4x type %2.2x\n", pid, stream[0]);

            for (i = 5; i < coded_length; i++)
              printf ("%.2x ", stream[i]);
            printf ("\n");
#endif
          break;
          }

	/* DVBSUB */
	else if (stream[i] == 0x59)
	  {
	    int pos;
            for (pos = i + 2;
		 pos + 8 <= i + 2 + stream[i + 1]
		   && this->spu_langs_count < MAX_SPU_LANGS;
		 pos += 8)
	      {
		int no = this->spu_langs_count;
		demux_ts_spu_lang *lang = &this->spu_langs[no];

		this->spu_langs_count++;

		memcpy(lang->desc.lang, &stream[pos], 3);
		lang->desc.lang[3] = 0;
		lang->desc.comp_page_id =
		  (stream[pos + 4] << 8) | stream[pos + 5];
		lang->desc.aux_page_id =
		  (stream[pos + 6] << 8) | stream[pos + 7];
		lang->pid = pid;
		lang->media_index = this->media_num;
		this->media[this->media_num].type = no;
		demux_ts_pes_new(this, this->media_num, pid, this->video_fifo, stream[0]);
		demux_send_special_spu_buf( this, BUF_SPU_DVB, no );
#ifdef TS_LOG
		printf("demux_ts: DVBSUB: pid 0x%.4x: %s  page %ld %ld type %2.2x\n",
		       pid, lang->desc.lang,
		       lang->desc.comp_page_id,
		       lang->desc.aux_page_id,
                       stream[0]);
#endif
	      }
	  }
      }
      break;

    case HDMV_SPU_INTERACTIVE:
    case HDMV_SPU_TEXT:
      if (this->hdmv > 0) {
        printf("demux_ts: Skipping unsupported HDMV subtitle stream_type: 0x%.2x pid: 0x%.4x\n",
               stream[0], pid);
        break;
      }
      /* fall thru */

    case HDMV_SPU_BITMAP:
      if (this->hdmv > 0) {
	if (pid >= 0x1200 && pid < 0x1300) {
	  /* HDMV Presentation Graphics / SPU */

	  if (this->spu_langs_count >= MAX_SPU_LANGS) {
	    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
		     "demux_ts: too many SPU tracks! ignoring pid 0x%.4x\n",
		     pid);
	    break;
	  }

	  demux_ts_spu_lang *lang = &this->spu_langs[this->spu_langs_count];

	  memset(lang->desc.lang, 0, sizeof(lang->desc.lang));
	  /*memcpy(lang->desc.lang, &stream[pos], 3);*/
	  /*lang->desc.lang[3] = 0;*/
	  lang->pid = pid;
	  lang->media_index = this->media_num;
	  this->media[this->media_num].type = this->spu_langs_count;
	  demux_ts_pes_new(this, this->media_num, pid, this->video_fifo, stream[0]);
	  demux_send_special_spu_buf( this, BUF_SPU_HDMV, this->spu_langs_count );
	  this->spu_langs_count++;
#ifdef TS_PMT_LOG
	  printf("demux_ts: HDMV subtitle stream_type: 0x%.2x pid: 0x%.4x\n",
		 stream[0], pid);
#endif
	  break;
	}
      }
      /* fall thru */
    default:

/* This following section handles all the cases where the audio track info is stored in PMT user info with stream id >= 0x80
 * We first check that the stream id >= 0x80, because all values below that are invalid if not handled above,
 * then we check the registration format identifier to see if it holds "AC-3" (0x41432d33) and
 * if is does, we tag this as an audio stream.
 * FIXME: This will need expanding if we ever see a DTS or other media format here.
 */
        if ((this->audio_tracks_count < MAX_AUDIO_TRACKS) && (stream[0] >= 0x80) ) {
        int i, found = 0;
        for(i = 0; i < this->audio_tracks_count; i++) {
            if(this->audio_tracks[i].pid == pid) {
                found = 1;
                break;
            }
        }
        if(!found) {
            uint32_t format_identifier=0;
            demux_ts_get_reg_desc(this, &format_identifier,
                    stream + 5, stream_info_length);
            /* If no format identifier, assume A52 */
            if (( format_identifier == 0x41432d33) ||
                ( format_identifier == 0) ||
                ((format_identifier == 0x48444d56 || this->hdmv>0) && stream[0] == HDMV_AUDIO_80_PCM) /* BluRay PCM */) {

                demux_ts_pes_new(this, this->media_num, pid, this->audio_fifo, stream[0]);
                this->audio_tracks[this->audio_tracks_count].pid = pid;
                this->audio_tracks[this->audio_tracks_count].media_index = this->media_num;
                this->media[this->media_num].type = this->audio_tracks_count;
                demux_ts_get_lang_desc(this, this->audio_tracks[this->audio_tracks_count].lang,
                                       stream + 5, stream_info_length);
                this->audio_tracks_count++;
                break;
            }
        }
      } else {
#ifdef TS_PMT_LOG
        printf ("demux_ts: PMT unknown stream_type: 0x%.2x pid: 0x%.4x\n",
                stream[0], pid);

        for (i = 5; i < coded_length; i++)
          printf ("%.2x ", stream[i]);
        printf ("\n");
#endif
      }
      break;
    }
    this->media_num++;
    stream += coded_length;
    section_length -= coded_length;
  }

  /*
   * Get the current PCR PID.
   */
  pid = ((this->pmt[program_count][8] << 8)
         | this->pmt[program_count][9]) & 0x1fff;
  if (this->pcrPid != pid) {
#ifdef TS_PMT_LOG
    if (this->pcrPid == INVALID_PID) {
      printf ("demux_ts: PMT pcr pid 0x%.4x\n", pid);
    } else {
      printf ("demux_ts: PMT pcr pid changed 0x%.4x\n", pid);
    }
#endif
    this->pcrPid = pid;
  }

  if ( this->stream->spu_channel>=0 && this->spu_langs_count>0 )
    demux_ts_update_spu_channel( this );

  /* Inform UI of channels changes */
  xine_event_t ui_event;
  ui_event.type = XINE_EVENT_UI_CHANNELS_CHANGED;
  ui_event.data_length = 0;
  xine_event_send( this->stream, &ui_event );
}

static int sync_correct(demux_ts_t*this, uint8_t *buf, int32_t npkt_read) {

  int p = 0;
  int n = 0;
  int i = 0;
  int sync_ok = 0;
  int read_length;

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: about to resync!\n");

  for (p=0; p < npkt_read; p++) {
    for(n=0; n < this->pkt_size; n++) {
      sync_ok = 1;
      for (i=0; i < MIN(MIN_SYNCS, npkt_read - p); i++) {
	if (buf[this->pkt_offset + n + ((i+p) * this->pkt_size)] != SYNC_BYTE) {
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
    memmove(&buf[0], &buf[n + p * this->pkt_size],
	    ((this->pkt_size * (npkt_read - p)) - n));
    read_length = this->input->read(this->input,
				    &buf[(this->pkt_size * (npkt_read - p)) - n],
				    n + p * this->pkt_size);
    /* FIXME: when read_length is not as required... we now stop demuxing */
    if (read_length != (n + p * this->pkt_size)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	       "demux_ts_tsync_correct: sync found, but read failed\n");
      return 0;
    }
  } else {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts_tsync_correct: sync not found! Stop demuxing\n");
    return 0;
  }
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: resync successful!\n");
  return 1;
}

static int sync_detect(demux_ts_t*this, uint8_t *buf, int32_t npkt_read) {

  int i, sync_ok;

  sync_ok = 1;

  if (this->hdmv) {
    this->pkt_size   = PKT_SIZE + 4;
    this->pkt_offset = 4;
    for (i=0; i < MIN(MIN_SYNCS, npkt_read - 3); i++) {
      if (buf[this->pkt_offset + i * this->pkt_size] != SYNC_BYTE) {
	sync_ok = 0;
	break;
      }
    }
    if (sync_ok) {
      if (this->hdmv < 0) {
        /* fix npkt_read (packet size is 192, not 188) */
        this->npkt_read = npkt_read * PKT_SIZE / this->pkt_size;
      }
      this->hdmv = 1;
      return sync_ok;
    }
    if (this->hdmv > 0)
      return sync_correct(this, buf, npkt_read);

    /* plain ts */
    this->hdmv       = 0;
    this->pkt_size   = PKT_SIZE;
    this->pkt_offset = 0;
  }

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

  uint8_t *return_pointer = NULL;
  int32_t read_length;
  if ( (this->packet_number) >= this->npkt_read) {

    /* NEW: handle read returning less packets than NPKT_PER_READ... */
    do {
      read_length = this->input->read(this->input, this->buf,
				      this->pkt_size * NPKT_PER_READ);
      if (read_length < 0 || read_length % this->pkt_size) {
	xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
		 "demux_ts: read returned %d bytes (not a multiple of %d!)\n",
		 read_length, this->pkt_size);
	this->status = DEMUX_FINISHED;
	return NULL;
      }
      this->npkt_read = read_length / this->pkt_size;

#ifdef TS_READ_STATS
      this->rstat[this->npkt_read]++;
#endif
      /*
       * what if this->npkt_read < 5 ? --> ok in sync_detect
       *
       * NEW: stop demuxing if read returns 0 a few times... (200)
       */

      if (this->npkt_read == 0) {
	xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: read 0 packets\n");
	this->status = DEMUX_FINISHED;
	return NULL;
      }

    } while (! read_length);

    this->packet_number = 0;

    if (!sync_detect(this, &(this->buf)[0], this->npkt_read)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: sync error.\n");
      this->status = DEMUX_FINISHED;
      return NULL;
    }
  }
  return_pointer = &(this->buf)[this->pkt_offset + this->pkt_size * this->packet_number];
  this->packet_number++;
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

/* check if an apid is in the list of known apids */
static int apid_check(demux_ts_t*this, unsigned int pid) {
  int i;
  for(i=0; i<this->audio_tracks_count; i++) {
    if(this->audio_tracks[i].pid == pid)
      return i;
  }
  return -1;
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


#ifdef TS_HEADER_LOG
  printf("demux_ts:ts_header:sync_byte=0x%.2x\n",sync_byte);
  printf("demux_ts:ts_header:transport_error_indicator=%d\n", transport_error_indicator);
  printf("demux_ts:ts_header:payload_unit_start_indicator=%d\n", payload_unit_start_indicator);
  printf("demux_ts:ts_header:transport_priority=%d\n", transport_priority);
  printf("demux_ts:ts_header:pid=0x%.4x\n", pid);
  printf("demux_ts:ts_header:transport_scrambling_control=0x%.1x\n", transport_scrambling_control);
  printf("demux_ts:ts_header:adaptation_field_control=0x%.1x\n", adaptation_field_control);
  printf("demux_ts:ts_header:continuity_counter=0x%.1x\n", continuity_counter);
#endif
  /*
   * Discard packets that are obviously bad.
   */
  if (sync_byte != SYNC_BYTE) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux error! invalid ts sync byte %.2x\n", sync_byte);
    return;
  }
  if (transport_error_indicator) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux error! transport error\n");
    return;
  }
  if (pid == 0x1ffb) {
      /* printf ("demux_ts: PSIP table. Program Guide etc....not supported yet. PID = 0x1ffb\n"); */
      return;
  }

  if (transport_scrambling_control) {
    if (this->videoPid == pid) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	       "demux_ts: selected videoPid is scrambled; skipping...\n");
    }
    for (i=0; i < this->scrambled_npids; i++) {
      if (this->scrambled_pids[i] == pid) return;
    }
    this->scrambled_pids[this->scrambled_npids] = pid;
    this->scrambled_npids++;

    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: PID 0x%.4x is scrambled!\n", pid);
    return;
  }

  data_offset = 4;

  if( adaptation_field_control & 0x2 ){
    uint32_t adaptation_field_length = originalPkt[4];
    if (adaptation_field_length > 0) {
      demux_ts_adaptation_field_parse (originalPkt+5, adaptation_field_length);
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

  /*
   * audio/video pid auto-detection, if necessary
   */
   program_count=0;
   if(this->media_num<MAX_PMTS)
      while ((this->program_number[program_count] != INVALID_PROGRAM) &&
		 (program_count < MAX_PMTS)) {
        if (pid == this->pmt_pid[program_count]) {

#ifdef TS_LOG
          printf ("demux_ts: PMT prog: 0x%.4x pid: 0x%.4x\n",
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

  if (payload_unit_start_indicator && this->media_num < MAX_PIDS){
    int pes_stream_id;
    if (pid == 0) {
      demux_ts_parse_pat (this, originalPkt, originalPkt+data_offset-4,
			  payload_unit_start_indicator);
      return;
    }
    program_count = 0;
    pes_stream_id = originalPkt[data_offset+3];

#ifdef TS_HEADER_LOG
    printf("demux_ts:ts_pes_header:stream_id=0x%.2x\n",pes_stream_id);
#endif

    if ( (pes_stream_id >= VIDEO_STREAM_S) && (pes_stream_id <= VIDEO_STREAM_E) ) {
      if ( this->videoPid == INVALID_PID) {
        int i, found = 0;
        for(i = 0; i < this->media_num; i++) {
          if (this->media[i].pid == pid) {
            found = 1;
            break;
          }
        }

        if (found && (this->media[i].corrupted_pes == 0)) {
          this->videoPid = pid;
	  this->videoMedia = i;
        } else if (!found) {
	  this->videoPid = pid;
	  this->videoMedia = this->media_num;
	  this->media[this->videoMedia].autodetected = 1;
	  demux_ts_pes_new(this, this->media_num++, pid, this->video_fifo, 0x100 + pes_stream_id);
        }

        if (this->videoPid != INVALID_PID) {
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                   "demux_ts: auto-detected video pid 0x%.4x\n", pid);
        }
      }
    } else if ( (pes_stream_id >= AUDIO_STREAM_S) && (pes_stream_id <= AUDIO_STREAM_E) ) {
       if (this->audio_tracks_count < MAX_AUDIO_TRACKS) {
           int i, found = 0;
           for(i = 0; i < this->audio_tracks_count; i++) {
               if(this->audio_tracks[i].pid == pid) {
                   found = 1;
                   break;
               }
           }
           if(!found) {
#ifdef TS_PMT_LOG
               xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                        "demux_ts: auto-detected audio pid 0x%.4x\n", pid);
#endif
               /* store PID, index and stream no. */
               this->audio_tracks[this->audio_tracks_count].pid = pid;
               this->audio_tracks[this->audio_tracks_count].media_index = this->media_num;
               this->media[this->media_num].type = this->audio_tracks_count;
               demux_ts_pes_new(this, this->media_num++, pid,
                                this->audio_fifo, 0x100 + pes_stream_id);
               this->audio_tracks_count++;
	   }
       }
    }
  }

  if (data_len > PKT_SIZE) {

    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
	     "demux_ts: demux error! invalid payload size %d\n", data_len);

  } else {

    /*
     * Do the demuxing in descending order of packet frequency!
     */
    int index;
    if (pid == this->videoPid) {
#ifdef TS_LOG
      printf ("demux_ts: Video pid: 0x%.4x\n", pid);
#endif
      check_newpts(this, this->media[this->videoMedia].pts, PTS_VIDEO);
      demux_ts_buffer_pes (this, originalPkt+data_offset, this->videoMedia,
			   payload_unit_start_indicator, continuity_counter,
			   data_len);
      return;
    }
    else if ((index = apid_check(this, pid)) > -1) {
#ifdef TS_LOG
      printf ("demux_ts: Audio pid: 0x%.4x\n", pid);
#endif
      check_newpts(this, this->media[this->audio_tracks[index].media_index].pts, PTS_AUDIO);
      demux_ts_buffer_pes (this, originalPkt+data_offset,
               this->audio_tracks[index].media_index,
			   payload_unit_start_indicator, continuity_counter,
			   data_len);
      return;
    }
    else if (pid == NULL_PID) {
#ifdef TS_LOG
      printf ("demux_ts: Null Packet\n");
#endif
      return;
    }
    /* DVBSUB */
    else if (pid == this->spu_pid) {
#ifdef TS_LOG
      printf ("demux_ts: SPU pid: 0x%.4x\n", pid);
#endif
      demux_ts_buffer_pes (this, originalPkt+data_offset, this->spu_media,
			   payload_unit_start_indicator, continuity_counter,
			   data_len);
      return;
    }
  }
}

/*
 * check for pids change events
 */

static void demux_ts_event_handler (demux_ts_t *this) {

  xine_event_t *event;

  while ((event = xine_event_get (this->event_queue))) {


    switch (event->type) {

    case XINE_EVENT_PIDS_CHANGE:

      this->videoPid    = INVALID_PID;
      this->audio_tracks_count = 0;
      this->media_num   = 0;
      this->send_newpts = 1;
      this->spu_pid     = INVALID_PID;
      this->spu_media   = 0;
      this->spu_langs_count= 0;
      this->last_pmt_crc = 0;
      _x_demux_control_start (this->stream);
      break;

    }

    xine_event_free (event);
  }
}

/*
 * send a piece of data down the fifos
 */

static int demux_ts_send_chunk (demux_plugin_t *this_gen) {

  demux_ts_t*this = (demux_ts_t*)this_gen;

  demux_ts_event_handler (this);

  demux_ts_parse_packet(this);

  /* DVBSUB: check if channel has changed.  Dunno if I should, or
   * even could, lock the xine object. */
  if (this->stream->spu_channel != this->current_spu_channel) {
    demux_ts_update_spu_channel(this);
  }

  return this->status;
}

static void demux_ts_dispose (demux_plugin_t *this_gen) {
  int i;
  demux_ts_t*this = (demux_ts_t*)this_gen;

  for (i=0; i < MAX_PMTS; i++) {
    if (this->pmt[i] != NULL) {
      free(this->pmt[i]);
      this->pmt[i] = NULL;
    }
  }
  for (i=0; i < MAX_PIDS; i++) {
    if (this->media[i].buf != NULL) {
      this->media[i].buf->free_buffer(this->media[i].buf);
      this->media[i].buf = NULL;
    }
  }

  xine_event_dispose_queue (this->event_queue);

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
  this->audio_tracks_count = 0;
  this->media_num= 0;
  this->last_pmt_crc = 0;

  _x_demux_control_start (this->stream);

  this->input->seek (this->input, 0, SEEK_SET);

  this->send_newpts = 1;

  this->status = DEMUX_OK ;

  this->send_end_buffers  = 1;
  this->scrambled_npids   = 0;

  /* DVBSUB */
  this->spu_pid = INVALID_PID;
  this->spu_langs_count = 0;
  this->current_spu_channel = -1;

  /* FIXME ? */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
}

static int demux_ts_seek (demux_plugin_t *this_gen,
			  off_t start_pos, int start_time, int playing) {

  demux_ts_t *this = (demux_ts_t *) this_gen;
  int i;
  start_time /= 1000;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->input->get_length (this->input) );

  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {

    if ((!start_pos) && (start_time)) {
      start_pos = start_time;
      start_pos *= this->rate;
      start_pos *= 50;
    }
    this->input->seek (this->input, start_pos, SEEK_SET);

  }

  this->send_newpts = 1;

  for (i=0; i<MAX_PIDS; i++) {
    demux_ts_media *m = &this->media[i];

    if (m->buf != NULL)
      m->buf->free_buffer(m->buf);
    m->buf            = NULL;
    m->counter        = INVALID_CC;
    m->corrupted_pes  = 1;
    m->buffered_bytes = 0;
  }

  if( !playing ) {

    this->status        = DEMUX_OK;
    this->buf_flag_seek = 0;

  } else {

    this->buf_flag_seek = 1;
    _x_demux_flush_engine(this->stream);

  }

  return this->status;
}

static int demux_ts_get_stream_length (demux_plugin_t *this_gen) {

  demux_ts_t*this = (demux_ts_t*)this_gen;

  if (this->rate)
    return (int)((int64_t) this->input->get_length (this->input)
                 * 1000 / (this->rate * 50));
  else
    return 0;
}


static uint32_t demux_ts_get_capabilities(demux_plugin_t *this_gen)
{
  return DEMUX_CAP_AUDIOLANG | DEMUX_CAP_SPULANG;
}

static int demux_ts_get_optional_data(demux_plugin_t *this_gen,
				      void *data, int data_type)
{
  demux_ts_t *this = (demux_ts_t *) this_gen;
  char *str = data;
  int channel = *((int *)data);

  /* be a bit paranoid */
  if (this == NULL || this->stream == NULL)
    return DEMUX_OPTIONAL_UNSUPPORTED;

  switch (data_type)
    {
    case DEMUX_OPTIONAL_DATA_AUDIOLANG:
      if ((channel >= 0) && (channel < this->audio_tracks_count)) {
        if(this->audio_tracks[channel].lang)
          strcpy(str, this->audio_tracks[channel].lang);
        else
          sprintf(str, "%3i", _x_get_audio_channel(this->stream));
      }
      else {
        snprintf(str, XINE_LANG_MAX, "%3i", _x_get_audio_channel(this->stream));
      }
      return DEMUX_OPTIONAL_SUCCESS;

    case DEMUX_OPTIONAL_DATA_SPULANG:
      if (channel>=0 && channel<this->spu_langs_count) {
        memcpy(str, this->spu_langs[channel].desc.lang, 3);
	str[3] = 0;}
      else
        strcpy(str, "none");
      return DEMUX_OPTIONAL_SUCCESS;

    default:
      return DEMUX_OPTIONAL_UNSUPPORTED;
    }
}

static int detect_ts(uint8_t *buf, size_t len, int ts_size)
{
  int    i, j;
  int    try_again, ts_detected = 0;
  size_t packs = len / ts_size - 2;

  for (i = 0; i < ts_size; i++) {
    try_again = 0;
    if (buf[i] == SYNC_BYTE) {
      for (j = 1; j < packs; j++) {
	if (buf[i + j*ts_size] != SYNC_BYTE) {
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

  return ts_detected;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen,
				    xine_stream_t *stream,
				    input_plugin_t *input) {

  demux_ts_t *this;
  int         i;
  int         hdmv = -1;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    uint8_t buf[2069];
    int     i, j;
    int     try_again, ts_detected;

    if (!_x_demux_read_header(input, buf, sizeof(buf)))
      return NULL;

    if (detect_ts(buf, sizeof(buf), PKT_SIZE))
      hdmv = 0;
    else if (detect_ts(buf, sizeof(buf), PKT_SIZE+4))
      hdmv = 1;
    else
      return NULL;
  }
    break;

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT:
    break;

  default:
    return NULL;
  }

  /*
   * if we reach this point, the input has been accepted.
   */

  this            = calloc(1, sizeof(*this));
  this->stream    = stream;
  this->input     = input;
  this->class     = class_gen;
  this->blockSize = PKT_SIZE;

  this->demux_plugin.send_headers      = demux_ts_send_headers;
  this->demux_plugin.send_chunk        = demux_ts_send_chunk;
  this->demux_plugin.seek              = demux_ts_seek;
  this->demux_plugin.dispose           = demux_ts_dispose;
  this->demux_plugin.get_status        = demux_ts_get_status;
  this->demux_plugin.get_stream_length = demux_ts_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_ts_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ts_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  /*
   * Initialise our specialised data.
   */
  for (i = 0; i < MAX_PIDS; i++) {
    this->media[i].pid = INVALID_PID;
    this->media[i].buf = NULL;
    this->media[i].autodetected = 0;
  }

  for (i = 0; i < MAX_PMTS; i++) {
    this->program_number[i]          = INVALID_PROGRAM;
    this->pmt_pid[i]                 = INVALID_PID;
    this->pmt[i]                     = NULL;
    this->pmt_write_ptr[i]           = NULL;
  }

  this->programNumber = INVALID_PROGRAM;
  this->pcrPid = INVALID_PID;
  this->scrambled_npids = 0;
  this->videoPid = INVALID_PID;
  this->audio_tracks_count = 0;
  this->last_pmt_crc = 0;

  this->rate = 16000; /* FIXME */

  this->status = DEMUX_FINISHED;

  /* DVBSUB */
  this->spu_pid = INVALID_PID;
  this->spu_langs_count = 0;
  this->current_spu_channel = -1;

  /* dvb */
  this->event_queue = xine_event_new_queue (this->stream);

  /* HDMV */
  this->hdmv       = hdmv;
  this->pkt_offset = (hdmv > 0) ? 4 : 0;
  this->pkt_size   = PKT_SIZE + this->pkt_offset;

  this->numPreview=0;

  return &this->demux_plugin;
}

/*
 * ts demuxer class
 */
static void *init_class (xine_t *xine, void *data) {

  demux_ts_class_t     *this;

  this         = calloc(1, sizeof(demux_ts_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.description     = N_("MPEG Transport Stream demuxer");
  this->demux_class.identifier      = "MPEG_TS";
  this->demux_class.mimetypes       = NULL;

  /* accept dvb streams; also handle the special dvbs,dvbt and dvbc
   * mrl formats: the content is exactly the same but the input plugin
   * uses a different tuning algorithm [Pragma]
   */
  this->demux_class.extensions      = "ts m2t trp dvb:// dvbs:// dvbc:// dvbt://";
  this->demux_class.dispose         = default_demux_class_dispose;

  this->av_crc = av_crc_get_table(AV_CRC_32_IEEE);

  return this;
}


/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_ts = {
  10                       /* priority */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "mpeg-ts", XINE_VERSION_CODE, &demux_info_ts, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

