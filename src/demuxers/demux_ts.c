
/*
 * Copyright (C) 2000-2020 the xine project
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
#include <errno.h>

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>  /* htonl */
#endif
#ifdef _WIN32
#include <winsock.h>    /* htonl */
#endif

#define LOG_MODULE "demux_ts"
#define LOG_VERBOSE
/*
#define LOG
*/
#define LOG_DYNAMIC_PMT
#define DUMP_VIDEO_HEADS

#ifdef DUMP_VIDEO_HEADS
#  include <stdio.h>
#endif

#include "group_video.h"

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>

#include "bswap.h"

/*
  #define TS_LOG
  #define TS_PMT_LOG
  #define TS_PAT_LOG

  #define TS_READ_STATS // activates read statistics generation
  #define TS_HEADER_LOG // prints out the Transport packet header.
*/

/* Packet readers: 1 (original), 2 (fast experimental) */
#define TS_PACKET_READER 2

/* transport stream packet layer */
#define TSP_sync_byte          0xff000000
#define TSP_transport_error    0x00800000
#define TSP_payload_unit_start 0x00400000
#define TSP_transport_priority 0x00200000
#define TSP_pid                0x001fff00
#define TSP_scrambling_control 0x000000c0
#define TSP_adaptation_field_1 0x00000020
#define TSP_adaptation_field_0 0x00000010
#define TSP_continuity_counter 0x0000000f

/*
 *  The maximum number of PIDs we are prepared to handle in a single program
 *  is the number that fits in a single-packet PMT.
 */
#define PKT_SIZE 188
#define BODY_SIZE (188 - 4)
/* more PIDS are needed due "auto-detection". 40 spare media entries  */
#define MAX_PIDS ((BODY_SIZE - 1 - 13) / 4) + 40

#define MAX_PMTS 126
#define PAT_BUF_SIZE (4 * MAX_PMTS + 20)

#define MAX_PROGRAMS MAX_PMTS

#define SYNC_BYTE   0x47

#if TS_PACKET_READER == 1
#  define MIN_SYNCS 3
#  define NPKT_PER_READ 96  // 96*188 = 94*192
#  define BUF_SIZE (NPKT_PER_READ * (PKT_SIZE + 4))
#endif

#if TS_PACKET_READER == 2
   /* smallest common multiple of 188 and 192 is 9024 */
#  define BUF_SIZE (2 * 9024)
   /* Kaffeine feeds us through a named pipe. When recording to disk at the same time,
    * a large read buffer provokes a lot of waiting and extra video frame drops. */
#  define SMALL_BUF_SIZE (1 * 9024)
#endif

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

/* descriptors in PMT stream info */
#define DESCRIPTOR_REG_FORMAT  0x05
#define DESCRIPTOR_LANG        0x0a
#define DESCRIPTOR_TELETEXT    0x56
#define DESCRIPTOR_DVBSUB      0x59
#define DESCRIPTOR_AC3         0x6a
#define DESCRIPTOR_EAC3        0x7a
#define DESCRIPTOR_DTS         0x7b
#define DESCRIPTOR_AAC         0x7c

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
      STREAM_VIDEO_HEVC = 0x24,

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

      HDMV_AUDIO_A1_EAC3_SEC  = 0xa1, /* Dolby Digital plus, secondary audio */
      HDMV_AUDIO_A2_DTSHD_SEC = 0xa2, /* DTS HD, secondary audio */

      /* pseudo tags */
      STREAM_AUDIO_EAC3    = (DESCRIPTOR_EAC3 << 8),
      STREAM_AUDIO_DTS     = (DESCRIPTOR_DTS  << 8),

    } streamType;

#define WRAP_THRESHOLD       360000

#define PTS_AUDIO 0
#define PTS_VIDEO 1

/* bitrate estimation */
#define TBRE_MIN_TIME (  2 * 90000)
#define TBRE_TIME     (480 * 90000)

#define TBRE_MODE_PROBE     0
#define TBRE_MODE_AUDIO_PTS 1
#define TBRE_MODE_AUDIO_PCR 2
#define TBRE_MODE_PCR       3
#define TBRE_MODE_DONE      4


#undef  MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#undef  MAX
#define MAX(a,b) ((a)>(b)?(a):(b))

/* seek helpers: try to figure out the type of each video frame from its
 * first ts packet (~150 bytes). the basic idea is:
 * mpeg-ts shall support broadcasting. unlike streaming, client has no
 * return channel to request setup info by. instead, pat and pmt repeat
 * every half second, and video decoder configuration aka sequence head
 * repeats before each keyframe.
 * if easy to do, read frame type directly.
 * also, consider some special cases of related info.
 * if this seems to work during normal business, use it to find next
 * keyframe after seek as well. */

typedef enum {
  FRAMETYPE_UNKNOWN = 0,
  FRAMETYPE_I,
  FRAMETYPE_P,
  FRAMETYPE_B
} frametype_t;

static frametype_t frametype_h264 (const uint8_t *f, uint32_t len) {
  static const uint8_t t[16] = {
    FRAMETYPE_UNKNOWN, FRAMETYPE_I,
    FRAMETYPE_UNKNOWN, FRAMETYPE_P,
    FRAMETYPE_UNKNOWN, FRAMETYPE_B,
    FRAMETYPE_UNKNOWN, FRAMETYPE_UNKNOWN,
    FRAMETYPE_UNKNOWN, FRAMETYPE_UNKNOWN,
    FRAMETYPE_UNKNOWN, FRAMETYPE_UNKNOWN,
    FRAMETYPE_UNKNOWN, FRAMETYPE_UNKNOWN,
    FRAMETYPE_UNKNOWN, FRAMETYPE_UNKNOWN
  };
  const uint8_t *e = f + len - 5;
  while (f <= e) {
    uint32_t v = _X_BE_32 (f);
    f += 1;
    if ((v >> 8) != 1)
      continue;
    v &= 0x1f; /* nal unit type */
    if (v == 7) /* sequence parameter set */
      return FRAMETYPE_I;
    if ((v == 1) || (v == 5)) /* (non) IDR slice, no use to scan further */
      break;
    f += 4 - 1;
    if (v != 9) /* access unit delimiter */
      continue;
    v = t[f[0] >> 4];
    if (v != FRAMETYPE_UNKNOWN)
      return v;
    f += 1;
  }
  return FRAMETYPE_UNKNOWN;
}

static frametype_t frametype_h265 (const uint8_t *f, uint32_t len) {
  static const uint8_t t[8] = {
    FRAMETYPE_UNKNOWN, FRAMETYPE_I, FRAMETYPE_P, FRAMETYPE_P, FRAMETYPE_B,
    FRAMETYPE_UNKNOWN, FRAMETYPE_UNKNOWN, FRAMETYPE_UNKNOWN
  };
  const uint8_t *e = f + len - 5;
  while (f <= e) {
    uint32_t v = _X_BE_32 (f);
    f += 1;
    if ((v >> 8) != 1)
      continue;
    v = (v & 0x7e) >> 1; /* nal unit type */
    if (v == 32) /* video parameter set */
      return FRAMETYPE_I;
    if (v == 33) /* sequence parameter set */
      return FRAMETYPE_I;
    if ((v >= 16) && (v <= 23)) /* IRAP slice */
      return FRAMETYPE_I;
    f += 4 - 1;
    if (v == 39) /* slice extra info prefix */
      continue;
    if (v != 35) /* access unit delimiter */
      continue;
    /* misusing nal unit temporal id as a frame type indicator,
     * as we only have data from first ts packet.
     * seems to be better than nothing with some dvb streams. */
    v = t[f[0] & 7];
    if (v != FRAMETYPE_UNKNOWN)
      return v;
    f += 1;
  }
  return FRAMETYPE_UNKNOWN;
}

static frametype_t frametype_mpeg (const uint8_t *f, uint32_t len) {
  static const uint8_t t[8] = {
    FRAMETYPE_UNKNOWN, FRAMETYPE_I, FRAMETYPE_P, FRAMETYPE_B,
    FRAMETYPE_UNKNOWN, FRAMETYPE_UNKNOWN, FRAMETYPE_UNKNOWN, FRAMETYPE_UNKNOWN
  };
  const uint8_t *e = f + len - 4 - 2;
  while (f <= e) {
    uint32_t v = _X_BE_32 (f);
    f += 1;
    if ((v >> 8) != 1)
      continue;
    v &= 0xff; /* nal unit type */
    if (v == 0xb3) /* sequence head */
      return FRAMETYPE_I;
    f += 4 - 1;
    if (v != 0) /* picture start */
      continue;
    return t[(f[1] & 0x38) >> 3];
  }
  return FRAMETYPE_UNKNOWN;
}

static frametype_t frametype_vc1 (const uint8_t *f, uint32_t len) {
  const uint8_t *e = f + len - 4 - 1;
  while (f <= e) {
    uint32_t v = _X_BE_32 (f);
    f += 1;
    if ((v >> 8) != 1)
      continue;
    v &= 0xff; /* nal unit type */
    if (v == 15) /* sequence head */
      return FRAMETYPE_I;
    if (v == 13) /* frame start */
      break;
    f += 4 - 1;
  }
  return FRAMETYPE_UNKNOWN;
}

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
  uint32_t         type;
  int64_t          pts;
  fifo_buffer_t   *fifo;
  buf_element_t   *buf;
  uint32_t         audio_type;     /* defaults from pmt */
  uint32_t         video_type;
  uint32_t         spu_type;
  uint32_t         hdmv_type;
  uint32_t         sure_type;
  unsigned int     counter;
  uint16_t         descriptor_tag; /* +0x100 for PES stream IDs (no available TS descriptor tag?) */
  uint8_t          keep;           /* used by demux_ts_dynamic_pmt_*() */
#define PES_FLUSHED 1
#define PES_RESUME  2
  uint8_t          resume;
  int              corrupted_pes;
  int              pes_bytes_left; /* butes left if PES packet size is known */

  int              input_normpos;
  int              input_time;
} demux_ts_media;

/* DVBSUB */
#define MAX_SPU_LANGS 32

typedef struct {
  spu_dvb_descriptor_t desc;
  unsigned int pid;
  unsigned int media_index;
} demux_ts_spu_lang;

/* Audio Channels */
#define MAX_AUDIO_TRACKS 32

typedef struct {
    unsigned int pid;
    unsigned int media_index;
    char lang[4];
} demux_ts_audio_track;

typedef struct {
  uint32_t program;
  uint32_t pid;
  uint32_t length;
  uint32_t crc;
  unsigned write_pos;
  uint8_t  buf[4098];
} demux_ts_pmt;

typedef struct {
  /*
   * The first field must be the "base class" for the plugin!
   */
  demux_plugin_t   demux_plugin;

  xine_stream_t   *stream;

  fifo_buffer_t   *audio_fifo;
  fifo_buffer_t   *video_fifo;

  input_plugin_t  *input;
  unsigned int     read_retries;

  int              status;

  int              hdmv;       /* -1 = unknown, 0 = mpeg-ts, 1 = hdmv/m2ts */

  unsigned int     rate;
  unsigned int     media_num;
  demux_ts_media   media[MAX_PIDS];

  /* PAT */
  unsigned int     pat_length;
  uint32_t         pat_crc;
  unsigned int     pat_write_pos;
  uint32_t         transport_stream_id;
  /* seek helpers */
  int64_t          last_pat_time;
  int64_t          last_keyframe_time;
  uint32_t         pat_interval;
  uint32_t         keyframe_interval;
  frametype_t     (*get_frametype)(const uint8_t *f, uint32_t len);
  /* programs */
  demux_ts_pmt    *pmts[MAX_PMTS];
  uint32_t         programs[MAX_PMTS + 1];
  /*
   * Stuff to do with the transport header. As well as the video
   * and audio PIDs, we keep the index of the corresponding entry
   * inthe media[] array.
   */
  unsigned int     pcr_pid;
  unsigned int     videoPid;
  unsigned int     videoMedia;

  demux_ts_audio_track audio_tracks[MAX_AUDIO_TRACKS];
  unsigned int      audio_tracks_count;

  int64_t          last_pts[2], apts, bpts;
  int32_t          bounce_left;
  int              send_newpts;
  int              buf_flag_seek;

  unsigned int     scrambled_pids[MAX_PIDS];
  unsigned int     scrambled_npids;

#ifdef TS_READ_STATS
  uint32_t         rstat[NPKT_PER_READ + 1];
#endif

  /* DVBSUB */
  unsigned int      spu_pid;
  unsigned int      spu_media_index;
  demux_ts_spu_lang spu_langs[MAX_SPU_LANGS];
  unsigned int      spu_langs_count;
  int               current_spu_channel;

  /* dvb */
  xine_event_queue_t *event_queue;

#if TS_PACKET_READER == 1
  int     pkt_size;   /* TS packet size */
  int     pkt_offset; /* TS packet offset */
  /* For syncronisation */
  int32_t packet_number;
  /* NEW: var to keep track of number of last read packets */
  int32_t npkt_read;
#endif

  off_t   frame_pos; /* current ts packet position in input stream (bytes from beginning) */

  /* bitrate estimation */
  off_t        tbre_bytes, tbre_lastpos;
  int64_t      tbre_time, tbre_lasttime;
  unsigned int tbre_mode, tbre_pid;

#ifdef DUMP_VIDEO_HEADS
  FILE *vhdfile;
#endif

  /* statistics */
  int     enlarge_total, enlarge_ok;

  uint8_t pat[PAT_BUF_SIZE];

  /* 0x00 | media_index    (video/audio/subtitle)
   * 0x80 | pmt_index      (pmt)
   * 0xff                  (special/unused) */
  uint8_t pid_index[0x2000];

#if TS_PACKET_READER == 2
  int     buf_pos;
  int     buf_size;
  int     buf_max;
#endif
  uint8_t buf[BUF_SIZE]; /* == PKT_SIZE * NPKT_PER_READ */

} demux_ts_t;

static void demux_ts_hexdump (demux_ts_t *this, const char *intro, const uint8_t *p, uint32_t len) {
  static const uint8_t tab_hex[16] = "0123456789abcdef";
  uint8_t sb[512 * 3], *q = sb;
  sb[0] = 0;
  if (len > 512)
    len = 512;
  if (len) {
    do {
      *q++ = tab_hex[p[0] >> 4];
      *q++ = tab_hex[p[0] & 15];
      *q++ = ' ';
      p++;
    } while (--len);
    q[-1] = 0;
  }
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "%s %s\n", intro, sb);
}


static void reset_track_map(fifo_buffer_t *fifo)
{
  buf_element_t *buf = fifo->buffer_pool_alloc (fifo);

  buf->type            = BUF_CONTROL_RESET_TRACK_MAP;
  buf->decoder_info[1] = -1;

  fifo->put (fifo, buf);
}

/* TJ. dynamic PMT support. The idea is:
   First, reuse unchanged pids and add new ones.
   Then, comb out those who are no longer referenced.
   For example, the Kaffeine dvb frontend preserves original pids but only
   sends the currently user selected ones, plus matching generated pat/pmt */

static int demux_ts_dynamic_pmt_find (demux_ts_t *this,
  int pid, int type, unsigned int descriptor_tag) {
  unsigned int i;
  demux_ts_media *m;
  /* do we already have this one? */
  pid &= 0x1fff;
  i = this->pid_index[pid];
  if (!(i & 0x80)) {
    m = &this->media[i];
    if (((int)m->pid == pid)
      && ((m->type & BUF_MAJOR_MASK) == (unsigned int)type)
      && (m->descriptor_tag == descriptor_tag)) {
      /* mark this media decriptor for reuse */
      m->keep = 1;
      return i;
    }
  }
  i = this->media_num;
  if (i < MAX_PIDS) {
    /* prepare new media descriptor */
    this->pid_index[pid] = i;
    m = &this->media[i];
    m->pid            = pid;
    m->descriptor_tag = descriptor_tag;
    m->type           = type;
    m->audio_type     = BUF_AUDIO_MPEG;
    m->video_type     = BUF_VIDEO_MPEG;
    m->spu_type       = 0;
    m->hdmv_type      = 0;
    m->sure_type      = 0;
    m->counter        = INVALID_CC;
    m->corrupted_pes  = 1;
    m->pts            = 0;
    m->keep           = 1;
    m->resume         = 0;
    if (type == BUF_AUDIO_BASE) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: new audio pid %d\n", pid);
      /* allocate new audio track as well */
      if (this->audio_tracks_count >= MAX_AUDIO_TRACKS) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                 "demux_ts: too many audio PIDs, ignoring pid %d\n", pid);
        return -1;
      }
      m->type |= this->audio_tracks_count;
      this->audio_tracks[this->audio_tracks_count].pid = pid;
      this->audio_tracks[this->audio_tracks_count].media_index = i;
      this->audio_tracks_count++;
      m->fifo = this->stream->audio_fifo;
      switch (descriptor_tag) {
        case ISO_13818_PART7_AUDIO:   m->audio_type = BUF_AUDIO_AAC;      break;
        case ISO_14496_PART3_AUDIO:   m->audio_type = BUF_AUDIO_AAC_LATM; break;
        case HDMV_AUDIO_84_EAC3:
        case STREAM_AUDIO_EAC3:       m->hdmv_type  = BUF_AUDIO_EAC3;     break;
        case STREAM_AUDIO_AC3:        m->hdmv_type  = BUF_AUDIO_A52;      break; /* ac3 - raw */
        case STREAM_AUDIO_DTS:
        case HDMV_AUDIO_82_DTS:
        case HDMV_AUDIO_86_DTS_HD_MA: m->hdmv_type = BUF_AUDIO_DTS;       break;
        default: ;
      }
    } else if (type == BUF_VIDEO_BASE) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: new video pid %d\n", pid);
      this->get_frametype = frametype_mpeg;
      m->fifo = this->stream->video_fifo;
      switch (descriptor_tag) {
        case ISO_14496_PART2_VIDEO:  m->video_type = BUF_VIDEO_MPEG4;
                                     this->get_frametype = NULL;           break;
        case ISO_14496_PART10_VIDEO: m->video_type = BUF_VIDEO_H264;
                                     this->get_frametype = frametype_h264; break;
        case STREAM_VIDEO_VC1:       m->sure_type  = BUF_VIDEO_VC1;
                                     this->get_frametype = frametype_vc1;  break;
        case STREAM_VIDEO_HEVC:      m->sure_type  = BUF_VIDEO_HEVC;
                                     this->get_frametype = frametype_h265; break;
        default: ;
      }
    } else {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: new subtitle pid %d\n", pid);
      m->fifo = this->stream->video_fifo;
    }

    if (m->buf) {
      m->buf->free_buffer(m->buf);
      m->buf = NULL;
    }

    this->media_num++;
    return i;
  }
  /* table full */
#ifdef LOG_DYNAMIC_PMT
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: media descriptor table full.\n");
#endif
  return -1;
}

static void demux_ts_dynamic_pmt_clean (demux_ts_t *this) {
  unsigned int i, count = 0, tracks = 0, spus = 0;
  /* densify media table */
  for (i = 0; i < this->media_num; i++) {
    demux_ts_media *m = &this->media[i];
    unsigned int type = m->type & BUF_MAJOR_MASK;
    unsigned int chan = m->type & 0xff;
    if (m->keep) {
      m->keep = 0;
      if (type == BUF_VIDEO_BASE) {
        /* adjust single video link */
        this->videoMedia = count;
      } else if (type == BUF_AUDIO_BASE) {
        /* densify audio track table */
        this->audio_tracks[chan].media_index = count;
        if (chan > tracks) {
          m->type = (m->type & ~0xff) | tracks;
          this->audio_tracks[tracks] = this->audio_tracks[chan];
        }
        tracks++;
      } else if (type == BUF_SPU_BASE) {
        /* spu language table has already been rebuilt from scratch.
           Adjust backlinks only */
        while ((spus < this->spu_langs_count) && (this->spu_langs[spus].pid == m->pid)) {
          this->spu_langs[spus].media_index = count;
          spus++;
        }
        if (i == this->spu_media_index)
          this->spu_media_index = count;
      }
      if (i > count) {
        this->pid_index[m->pid & 0x1fff] = count;
        this->media[count] = *m;
        m->buf = NULL;
        m->pid = INVALID_PID;
      }
      count++;
    } else {
      /* drop this no longer needed media descriptor */
#ifdef LOG_DYNAMIC_PMT
      const char *name = "";
      if (type == BUF_VIDEO_BASE) name = "video";
      else if (type == BUF_AUDIO_BASE) name = "audio";
      else if (type == BUF_SPU_BASE) name = "subtitle";
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: dropped %s pid %d\n", name, m->pid);
#endif
      this->pid_index[m->pid & 0x1fff] = 0xff;
      m->pid = INVALID_PID;
      if (m->buf) {
        m->buf->free_buffer (m->buf);
        m->buf = NULL;
      }
    }
  }
  if ((tracks < this->audio_tracks_count) && this->audio_fifo) {
    /* at least 1 audio track removed, tell audio decoder loop */
    reset_track_map(this->audio_fifo);
#ifdef LOG_DYNAMIC_PMT
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: new audio track map\n");
#endif
  }
#ifdef LOG_DYNAMIC_PMT
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "demux_ts: using %u pids, %u audio %u subtitle channels\n", count, tracks, spus);
#endif
  /* adjust table sizes */
  this->media_num = count;
  this->audio_tracks_count = tracks;
  /* should really have no effect */
  this->spu_langs_count = spus;
}

static void demux_ts_dynamic_pmt_clear (demux_ts_t *this) {
  unsigned int i;
#ifdef LOG_DYNAMIC_PMT
  if (this->media_num)
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "demux_ts: deleting %u media descriptors.\n", this->media_num);
#endif
  for (i = 0; i < this->media_num; i++) {
    this->pid_index[this->media[i].pid & 0x1fff] = 0xff;
    if (this->media[i].buf) {
      this->media[i].buf->free_buffer (this->media[i].buf);
      this->media[i].buf = NULL;
    }
  }
  this->media_num = 0;

  this->videoPid = INVALID_PID;
  this->audio_tracks_count = 0;
  this->spu_pid = INVALID_PID;
  this->spu_langs_count = 0;

  this->pcr_pid = INVALID_PID;

  for (i = 0; this->programs[i] != INVALID_PROGRAM; i++)
    if (this->pmts[i])
      this->pmts[i]->length = 0;
}


static void demux_ts_tbre_reset (demux_ts_t *this) {
  if (this->tbre_time <= TBRE_TIME) {
    this->tbre_pid  = INVALID_PID;
    this->tbre_mode = TBRE_MODE_PROBE;
  }
}

static void demux_ts_tbre_update (demux_ts_t *this, unsigned int mode, int64_t now) {
  /* select best available timesource on the fly */
  if ((mode < this->tbre_mode) || (now <= 0))
    return;

  if (mode == this->tbre_mode) {
    /* skip discontinuities */
    int64_t diff = now - this->tbre_lasttime;
    if ((diff < 0 ? -diff : diff) < 220000) {
      /* add this step */
      this->tbre_bytes += this->frame_pos - this->tbre_lastpos;
      this->tbre_time += diff;
      /* update bitrate */
      if (this->tbre_time > TBRE_MIN_TIME)
        this->rate = this->tbre_bytes * 90000 / this->tbre_time;
      /* stop analyzing */
      if (this->tbre_time > TBRE_TIME)
        this->tbre_mode = TBRE_MODE_DONE;
    }
  } else {
    /* upgrade timesource */
    this->tbre_mode = mode;
  }

  /* remember where and when */
  this->tbre_lastpos  = this->frame_pos;
  this->tbre_lasttime = now;
}

/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define ts_abs(x) (((x) < 0) ? -(x) : (x))

/* bounce detection like in metronom. looks like double work but saves
 * up to 80 discontinuity messages. */
static void newpts_test (demux_ts_t *this, int64_t pts, int video) {
#ifdef TS_LOG
  printf ("demux_ts: newpts_test %lld, send_newpts %d, buf_flag_seek %d\n",
    pts, this->send_newpts, this->buf_flag_seek);
#endif
/*if (pts)*/
  {
    int64_t diff;
    do {
      this->last_pts[video] = pts;
      if (!this->apts) {
        diff = 0;
        this->apts = pts;
        break;
      }
      diff = pts - this->apts;
      if (ts_abs (diff) <= WRAP_THRESHOLD) {
        this->apts = pts;
        break;
      }
      if (this->bpts) {
        diff = pts - this->bpts;
        if (ts_abs (diff) <= WRAP_THRESHOLD) {
          this->bpts = pts;
          break;
        }
      }
      this->bpts = this->apts;
      this->apts = pts;
      this->bounce_left = WRAP_THRESHOLD;
      _x_demux_control_newpts (this->stream, pts, this->buf_flag_seek ? BUF_FLAG_SEEK : 0);
      this->send_newpts = 0;
      this->buf_flag_seek = 0;
      return;
    } while (0);
    if (this->bounce_left) {
      this->bounce_left -= diff;
      if (this->bounce_left <= 0) {
        this->bpts = 0;
        this->bounce_left = 0;
      }
    }
    if (this->send_newpts || this->buf_flag_seek) {
      _x_demux_control_newpts (this->stream, pts, this->buf_flag_seek ? BUF_FLAG_SEEK : 0);
      this->send_newpts = 0;
      this->buf_flag_seek = 0;
    }
  }
}

#if 0
static void check_newpts( demux_ts_t *this, int64_t pts, int video )
{

#ifdef TS_LOG
  printf ("demux_ts: check_newpts %lld, send_newpts %d, buf_flag_seek %d\n",
          pts, this->send_newpts, this->buf_flag_seek);
#endif

  if (pts) {
    int64_t diff = pts - this->last_pts[video];
    if (this->send_newpts || (this->last_pts[video] && (abs (diff) > WRAP_THRESHOLD))) {
      if (this->buf_flag_seek) {
        _x_demux_control_newpts (this->stream, pts, BUF_FLAG_SEEK);
        this->buf_flag_seek = 0;
      } else {
        _x_demux_control_newpts (this->stream, pts, 0);
      }
      this->send_newpts = 0;
      this->last_pts[1 - video] = 0;
    }
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
#endif

/* Send a BUF_SPU_DVB to let xine know of that channel. */
static void demux_send_special_spu_buf( demux_ts_t *this, uint32_t spu_type, int spu_channel )
{
  buf_element_t *buf;

  buf = this->video_fifo->buffer_pool_alloc( this->video_fifo );
  buf->type = spu_type|spu_channel;
  this->video_fifo->put( this->video_fifo, buf );
}

static void demux_ts_send_buffer (demux_ts_t *this, demux_ts_media *m, int flags) {
  if (m->buf) {
    /* test/tell discontinuity right before sending it.
     * dont bother this on every ts packet. :-) */
    if (m->pts) {
      uint32_t t = m->type & BUF_MAJOR_MASK;
      if ((t == BUF_VIDEO_BASE) || (t == BUF_AUDIO_BASE))
        newpts_test (this, m->pts, (t == BUF_VIDEO_BASE) ? PTS_VIDEO : PTS_AUDIO);
    }
    m->buf->content = m->buf->mem;
    m->buf->type = m->type;
    m->buf->decoder_flags |= flags;
    m->buf->pts = m->pts;
    m->buf->decoder_info[0] = 1;
    m->buf->extra_info->input_normpos = m->input_normpos;
    m->buf->extra_info->input_time = m->input_time;

    m->fifo->put(m->fifo, m->buf);
    m->buf = NULL;

#ifdef TS_LOG
    printf ("demux_ts: produced buffer, pts=%lld\n", m->pts);
#endif
  }
}

static void demux_ts_flush_media (demux_ts_t *this, demux_ts_media *m) {
  m->resume |= PES_FLUSHED;
  demux_ts_send_buffer (this, m, BUF_FLAG_FRAME_END);
}

/*
 * demux_ts_update_spu_channel
 *
 * Normally, we would handle spu like audio - send all we got, with
 * channel numbers, and let decoder loop do the rest.
 * However:
 * 1. We like to reduce video fifo load, and more importantly,
 * 2. There may be joint packets with multiple spus of same type,
 *    to be selected by decoder.
 * NOTE: there are 2 reasons for calling this:
 * a) pmt change, called while both old and new media descriptors are valid.
 * b) user channel change.
 *
 * Send a BUF_SPU_DVB with BUF_SPECIAL_SPU_DVB_DESCRIPTOR to tell
 * the decoder to reset itself on the new channel.
 */
static void demux_ts_update_spu_channel(demux_ts_t *this)
{
  unsigned int old_mi = this->spu_media_index;

  this->current_spu_channel = this->stream->spu_channel;

  if ((this->current_spu_channel >= 0) && ((unsigned int)this->current_spu_channel < this->spu_langs_count)) {
    demux_ts_spu_lang *lang = &this->spu_langs[this->current_spu_channel];

    this->spu_pid = lang->pid;
    this->spu_media_index = lang->media_index;

    /* same media -> skip flushing old buf. */
    if (old_mi == lang->media_index)
      old_mi = 0xffffffff;

    this->media[lang->media_index].type =
      this->media[lang->media_index].spu_type | this->current_spu_channel;

#ifdef TS_LOG
    printf ("demux_ts: DVBSUB: selecting lang: %s  page %ld %ld\n",
      lang->desc.lang, lang->desc.comp_page_id, lang->desc.aux_page_id);
#endif
  } else {
    this->spu_pid = INVALID_PID;
    this->spu_media_index = 0xffffffff;

#ifdef TS_LOG
    printf ("demux_ts: DVBSUB: deselecting lang\n");
#endif
  }

  if (old_mi < this->media_num) {
    demux_ts_flush_media (this, this->media + old_mi);
    this->media[old_mi].corrupted_pes = 1;

    if ((this->media[old_mi].type & (BUF_MAJOR_MASK | BUF_DECODER_MASK)) == BUF_SPU_DVB) {
      buf_element_t *buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

      buf->decoder_flags = BUF_FLAG_SPECIAL;
      buf->decoder_info[1] = BUF_SPECIAL_SPU_DVB_DESCRIPTOR;
      buf->decoder_info[2] = 0;
      buf->decoder_info_ptr[2] = NULL;
      buf->type = this->media[old_mi].type;
      this->video_fifo->put (this->video_fifo, buf);
    }
  }
    
  if (this->spu_media_index < this->media_num) {
    if ((this->media[this->spu_media_index].type & (BUF_MAJOR_MASK | BUF_DECODER_MASK)) == BUF_SPU_DVB) {
      buf_element_t *buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

      buf->decoder_flags = BUF_FLAG_SPECIAL;
      buf->decoder_info[1] = BUF_SPECIAL_SPU_DVB_DESCRIPTOR;
      buf->decoder_info[2] = sizeof (this->spu_langs[0].desc);
      buf->decoder_info_ptr[2] = buf->content;
      memcpy (buf->content, &this->spu_langs[this->current_spu_channel].desc, sizeof (this->spu_langs[0].desc));
      buf->type = this->media[this->spu_media_index].type;
      this->video_fifo->put (this->video_fifo, buf);
    }
  }
}

static void post_sequence_end(fifo_buffer_t *fifo, uint32_t video_type) {

  if (video_type == BUF_VIDEO_H264 ||
      video_type == BUF_VIDEO_MPEG ||
      video_type == BUF_VIDEO_VC1) {

    buf_element_t *buf = fifo->buffer_pool_try_alloc(fifo);
    if (buf) {
      buf->type = video_type;
      buf->size = 4;
      buf->decoder_flags = BUF_FLAG_FRAME_END;
      buf->content[0] = 0x00;
      buf->content[1] = 0x00;
      buf->content[2] = 0x01;
      buf->content[3] = (video_type == BUF_VIDEO_MPEG) ? 0xb7 : 0x0a;
      fifo->put(fifo, buf);
    }
  }
}


static void demux_ts_flush(demux_ts_t *this)
{
  unsigned int i;
  for (i = 0; i < this->media_num; ++i) {
    demux_ts_flush_media (this, &this->media[i]);
    this->media[i].corrupted_pes = 1;
  }

  /* append sequence end code to video stream */
  if (this->videoPid != INVALID_PID)
    post_sequence_end(this->stream->video_fifo, this->media[this->videoMedia].type);
}

/*
 * demux_ts_parse_pat
 *
 * Parse a program association table (PAT).
 * The PAT is expected to be exactly one section long.
 *
 * We can cope with the stupidity of SPTSs which contain NITs.
 */
static void demux_ts_parse_pat (demux_ts_t*this, const uint8_t *pkt, unsigned int pusi, unsigned int len) {
#ifdef TS_PAT_LOG
  uint32_t       table_id;
  uint32_t       version_number;
#endif
  uint32_t       section_syntax_indicator;
  uint32_t       section_length;
  uint32_t       transport_stream_id;
  uint32_t       current_next_indicator;
  uint32_t       section_number;
  uint32_t       last_section_number;
  uint32_t       crc32;
  uint32_t       calc_crc32;

  const uint8_t *program;
  unsigned int   program_count;
  unsigned int   pid_count;

  /* reassemble the section */
  if (pusi) {
    unsigned int pointer = (unsigned int)pkt[0] + 1;
    this->pat_write_pos = 0;
    /* offset the section by n + 1 bytes. this is sometimes used to let it end
       at an exact TS packet boundary */
    if (len <= pointer) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "demux_ts: demux error! PAT with invalid pointer\n");
      return;
    }
    len -= pointer;
    pkt += pointer;
  } else {
    if (!this->pat_write_pos)
      return;
  }
  if (len > PAT_BUF_SIZE - this->pat_write_pos)
    len = PAT_BUF_SIZE - this->pat_write_pos;
  xine_small_memcpy (this->pat + this->pat_write_pos, pkt, len);
  this->pat_write_pos +=len;

  /* lets see if we got the section length already */
  pkt = this->pat;
  if (this->pat_write_pos < 3)
    return;
  section_length = ((((uint32_t)pkt[1] << 8) | pkt[2]) & 0x03ff) + 3;
  /* this should be at least the head plus crc */
  if (section_length < 8 + 4) {
    this->pat_write_pos = 0;
    return;
  }
  /* and it should fit into buf */
  if (section_length > PAT_BUF_SIZE) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "demux_ts: PAT too large (%u bytes)\n", section_length);
    this->pat_write_pos = 0;
    return;
  }

  /* lets see if we got the section complete */
  if (this->pat_write_pos < section_length)
    return;

  /* figure out PAT repeat interval. */
  do {
    int64_t diff, now = this->last_pts[0];
    if (!now) {
      now = this->last_pts[1];
      if (!now)
        break;
    }
    if (!this->last_pat_time) {
      this->last_pat_time = now;
      break;
    }
    diff = now - this->last_pat_time;
    this->last_pat_time = now;
    if (diff < 0)
      break;
    this->pat_interval = diff <= (int64_t)0xffffffff ? diff : 0xffffffff;
  } while (0);

  /* same crc means either same table, or wrong crc - both are reason for skipping. */
  crc32 = _X_BE_32 (pkt + section_length - 4);
  if ((this->pat_length == section_length) &&
      (this->pat_crc    == crc32))
    return;

  /* OK now parse it */
  this->pat_write_pos = 0;
#ifdef TS_PAT_LOG
  table_id                  = (unsigned int)pkt[0];
#endif
  section_syntax_indicator  = (pkt[1] >> 7) & 0x01;
  transport_stream_id       = ((uint32_t)pkt[3] << 8) | pkt[4];
#ifdef TS_PAT_LOG
  version_number            = ((uint32_t)pkt[5] >> 1) & 0x1f;
#endif
  current_next_indicator    =  pkt[5] & 0x01;
  section_number            =  pkt[6];
  last_section_number       =  pkt[7];

#ifdef TS_PAT_LOG
  printf ("demux_ts: PAT table_id: %.2x\n", table_id);
  printf ("              section_syntax: %d\n", section_syntax_indicator);
  printf ("              section_length: %u (%#.3x)\n", section_length, section_length);
  printf ("              transport_stream_id: %#.4x\n", transport_stream_id);
  printf ("              version_number: %d\n", version_number);
  printf ("              c/n indicator: %d\n", current_next_indicator);
  printf ("              section_number: %d\n", section_number);
  printf ("              last_section_number: %d\n", last_section_number);
#endif

  if ((section_syntax_indicator != 1) || !current_next_indicator)
    return;

  if ((section_number != 0) || (last_section_number != 0)) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "demux_ts: FIXME (unsupported) PAT consists of multiple (%d) sections\n", last_section_number);
    return;
  }

  /* Check CRC. */
  calc_crc32 = htonl (xine_crc32_ieee (0xffffffff, pkt, section_length - 4));
  if (crc32 != calc_crc32) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "demux_ts: demux error! PAT with invalid CRC32: packet_crc32: %.8x calc_crc32: %.8x\n",
             crc32,calc_crc32);
    return;
  }
#ifdef TS_PAT_LOG
  printf ("demux_ts: PAT CRC32 ok.\n");
#endif

  if (this->transport_stream_id != transport_stream_id) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "demux_ts: PAT transport stream id %u.\n", transport_stream_id);
    this->transport_stream_id = transport_stream_id;
  }
  this->pat_length = section_length;
  this->pat_crc    = crc32;

  /* Unregister previous pmts. */
  for (program_count = 0; program_count < 0x2000; program_count++) {
    if (this->pid_index[program_count] & 0x80)
      this->pid_index[program_count] = 0xff;
  }
  for (program_count = 0; this->programs[program_count] != INVALID_PROGRAM; program_count++) {
    if (this->pmts[program_count]) {
      free (this->pmts[program_count]);
      this->pmts[program_count] = NULL;
    }
  }
  /*
   * Process all programs in the program loop.
   */
  program_count = 0;
  pid_count = 0;
  for (program = pkt + 8;
       (program < pkt + section_length - 4) && (program_count < MAX_PMTS);
       program += 4) {
    uint32_t v = _X_BE_32 (program);
    uint32_t program_number = v >> 16;
    uint32_t pmt_pid = v & 0x1fff;

    /*
     * completely skip NIT pids.
     */
    if (program_number == 0x0000)
      continue;

    /* register this pmt */
    this->programs[program_count] = program_number;
    if (this->pid_index[pmt_pid] == 0xff) {
      this->pid_index[pmt_pid] = 0x80 | program_count;
      pid_count++;
    }
#if 0
    /* force PMT reparsing when pmt_pid changes */
    if (this->pmts[program_count] && (this->pmts[program_count]->pid != pmt_pid)) {
      demux_ts_dynamic_pmt_clear (this);
    }
#endif
#ifdef TS_PAT_LOG
    if (this->program_number[program_count] != INVALID_PROGRAM)
      printf ("demux_ts: PAT acquired count=%d programNumber=0x%04x pmtPid=0x%04x\n",
        program_count, program_number, pmt_pid);
#endif

    ++program_count;
  }
  /* Add "end of table" marker. */
  this->programs[program_count] = INVALID_PROGRAM;
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "demux_ts: found %u programs, %u pmt pids.\n", program_count, pid_count);
}

static int demux_ts_parse_pes_header (demux_ts_t *this, demux_ts_media *m,
  const uint8_t *buf, unsigned int packet_len) {

  const uint8_t *p;
  uint32_t       header_len;
  int64_t        pts;
  uint32_t       stream_id;

  if (this->stream->xine->verbosity == 4)
    demux_ts_hexdump (this, "demux_ts: PES header", buf, buf[8] + 9);

  if (packet_len < 9) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "demux_ts: too short PES packet header (%d bytes)\n", packet_len);
    return 0;
  }

  p = buf;

  /* we should have a PES packet here */

  stream_id = _X_BE_32 (p);
  if ((stream_id >> 8) != 1) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "demux_ts: pes header error 0x%06x (should be 0x000001) \n", stream_id >> 8);
    return 0 ;
  }

  stream_id &= 0xff;

  if (stream_id == PRIVATE_STREAM2 && this->hdmv) {
    header_len = 6;
    pts = 0;
  } else if (stream_id == PADDING_STREAM) {
    return 0;
  } else {
    header_len = p[8] + 9;

    /* sometimes corruption on header_len causes segfault in memcpy below */
    if (header_len > packet_len) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
               "demux_ts: illegal value for PES_header_data_length (0x%x) pid %d type 0x%08x\n", p[8], m->pid, m->type);
      return 0;
    }

    if (p[7] & 0x80) { /* pts avail */
      uint32_t v;

      if (header_len < 14) {
        return 0;
      }

      pts = (uint64_t)(p[9] & 0x0e) << 29;
      v = _X_BE_32 (p + 10);
      v = ((v >> 1) & 0x7fff) | ((v >> 2) & 0x3fff8000);
      pts |= v;

    } else
      pts = 0;

    /* code works but not used in xine
       if ((p[7] & 0x40) && (header_len >= 19)) {

       DTS  = (p[14] & 0x0E) << 29 ;
       DTS |=  p[15]         << 22 ;
       DTS |= (p[16] & 0xFE) << 14 ;
       DTS |=  p[17]         <<  7 ;
       DTS |= (p[18] & 0xFE) >>  1 ;

       } else
       DTS = 0;
    */
  }

#ifdef TS_LOG
  printf ("demux_ts: packet stream id: %.2x len: %u (%x)\n",
          stream_id, packet_len, packet_len);
#endif

#ifdef DUMP_VIDEO_HEADS
  if ((m->pid == this->videoPid) && this->vhdfile) {
    static const uint8_t tab_hex[16] = "0123456789abcdef";
    const uint8_t *a = p + header_len;
    uint8_t sb[1024], *q = sb;
    int len = packet_len - header_len;
    memcpy (q, "> ", 2); q += 2;
    if (len) {
      do {
        *q++ = tab_hex[a[0] >> 4];
        *q++ = tab_hex[a[0] & 15];
        *q++ = ' ';
        a++;
      } while (--len);
      q--;
    }
    *q++ = '\n';
    fwrite (sb, 1, q - sb, this->vhdfile);
  }
#endif

  if ((m->pid == this->videoPid) && this->get_frametype) {
    frametype_t t = this->get_frametype (p + header_len, packet_len - header_len);
    if (t == FRAMETYPE_I) {
      if (!this->last_keyframe_time) {
        this->last_keyframe_time = pts;
      } else if (pts) {
        int64_t diff = pts - this->last_keyframe_time;
        this->keyframe_interval = ((diff < 0) || (diff > (int64_t)0xffffffff)) ? 0xffffffff : diff;
        this->last_keyframe_time = pts;
      }
    }
  }

  /* TJ. p[4,5] has the payload size in bytes. This is limited to roughly 64k.
   * For video frames larger than that, it usually is just 0, and payload extends
   * to the beginning of the next one.
   * However, I found an hls live stream that always sets size like this:
   * (1) [4,5] == 0xffd2, [6] == 0x84; (2) [4,5] == 0xffd2, [6] == 0x80; (3) [4,5] == 0x1288, [6] == 0x80;
   * I dont know whether [6] & 0x04 is reliable elsewhere. So lets try this HACK:
   * If [4,5] is > 0xff00, wait for a possible resume,
   * Accept a resume if it has same pts.
   */
  m->pes_bytes_left = (int)(p[4] << 8 | p[5]) - header_len + 6;
  lprintf("PES packet payload left: %d bytes\n", m->pes_bytes_left);
  if (!(m->resume & PES_RESUME)) {
    if (!(m->resume & PES_FLUSHED))
      demux_ts_flush_media (this, m);
    if (m->pes_bytes_left > 0xff00)
      m->resume |= PES_RESUME;
  } else {
    if ((pts != m->pts) && pts) {
      m->resume &= ~PES_RESUME;
      if (!(m->resume & PES_FLUSHED))
        demux_ts_flush_media (this, m);
    } else if (m->pes_bytes_left <= 0xff00) {
      m->resume &= ~PES_RESUME;
    }
  }
  /* now that finished previous buf is sent, set new pts. */
  m->pts = pts;
  /* allocate the buffer here, as pes_header needs a valid buf for dvbsubs */
  if (!m->buf)
    m->buf = m->fifo->buffer_pool_alloc (m->fifo);

  p += header_len;
  packet_len -= header_len;

  if (m->sure_type) {
    m->type = m->sure_type;
    return header_len;
  }

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
    if (m->hdmv_type) {
      m->type = (m->type & 0xff) | m->hdmv_type;
      return header_len;
    }

    if (m->descriptor_tag == HDMV_AUDIO_83_TRUEHD) {
      /* TODO: separate AC3 and TrueHD streams ... */
      m->type = (m->type & 0xff) | BUF_AUDIO_A52;
      return header_len;

    } else if (packet_len < 2) {
      return 0;

    } else if (m->descriptor_tag == HDMV_AUDIO_80_PCM) {

      if (packet_len < 4) {
        return 0;
      }

      m->type = (m->type & 0xff) | BUF_AUDIO_LPCM_BE;

      m->buf->decoder_flags  |= BUF_FLAG_SPECIAL;
      m->buf->decoder_info[1] = BUF_SPECIAL_LPCM_CONFIG;
      m->buf->decoder_info[2] = (p[3]<<24) | (p[2]<<16) | (p[1]<<8) | p[0];

      m->pes_bytes_left -= 4;
      return header_len + 4;

    } else if (m->descriptor_tag == ISO_13818_PES_PRIVATE
             && p[0] == 0x20 && p[1] == 0x00) {
      /* DVBSUB */
      m->type = (m->type & 0xff) | BUF_SPU_DVB;
      m->buf->decoder_info[2] = m->pes_bytes_left;
      return header_len;

    } else if (p[0] == 0x0B && p[1] == 0x77) { /* ac3 - syncword */
      m->type = (m->type & 0xff) | BUF_AUDIO_A52;
      return header_len;

    } else if ((p[0] & 0xE0) == 0x20) {
      spu_id = (p[0] & 0x1f);

      m->type      = BUF_SPU_DVD + spu_id;
      m->pes_bytes_left -= 1;
      return header_len + 1;

    } else if ((p[0] & 0xF0) == 0x80) {

      if (packet_len < 4) {
        return 0;
      }

      m->type = (m->type & 0xff) | BUF_AUDIO_A52;
      m->pes_bytes_left -= 4;
      return header_len + 4;

#if 0
    /* commented out: does not set PCM type. Decoder can't handle raw PCM stream without configuration. */
    } else if ((p[0]&0xf0) == 0xa0) {

      unsigned int pcm_offset;

      for (pcm_offset=0; ++pcm_offset < packet_len-1 ; ){
        if (p[pcm_offset] == 0x01 && p[pcm_offset+1] == 0x80 ) { /* START */
          pcm_offset += 2;
          break;
        }
      }

      if (packet_len < pcm_offset) {
        return 0;
      }

      m->type      |= BUF_AUDIO_LPCM_BE;
      m->pes_bytes_left -= pcm_offset;
      return header_len + pcm_offset;
#endif
    }

  } else if ((stream_id & 0xf0) == 0xe0) {

    m->type = m->video_type;
    return header_len;

  } else if ((stream_id & 0xe0) == 0xc0) {

    m->type = (m->type & 0xff) | m->audio_type;
    return header_len;

  } else {
#ifdef TS_LOG
    printf ("demux_ts: unknown packet, id: %x\n", stream_id);
#endif
  }

  return 0 ;
}

static void update_extra_info(demux_ts_t *this, demux_ts_media *m)
{
  off_t length = this->input->get_length (this->input);

  /* cache frame position */

  if (length > 0) {
    m->input_normpos = (double)this->frame_pos * 65535.0 / length;
  }
  if (this->rate) {
    m->input_time = this->frame_pos * 1000 / this->rate;
  }
}

/*
 *  buffer arriving pes data
 */
static void demux_ts_buffer_pes (demux_ts_t*this, const uint8_t *ts,
  unsigned int mediaIndex, unsigned int tsp_head, unsigned int len) {

  demux_ts_media *m = &this->media[mediaIndex];

  if (!m->fifo) {
#ifdef TS_LOG
    printf ("fifo unavailable (%d)\n", mediaIndex);
#endif
    return; /* To avoid segfault if video out or audio out plugin not loaded */
  }

  /* By checking the CC here, we avoid the need to check for the no-payload
     case (i.e. adaptation field only) when it does not get bumped. */
  {
    uint32_t cc = tsp_head & TSP_continuity_counter;
    if (m->counter != INVALID_CC) {
      if ((m->counter & 0x0f) != cc) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
          "demux_ts: PID %u: unexpected cc %u (expected %u)\n", m->pid, cc, m->counter);
      }
    }
    m->counter = cc;
    m->counter++;
  }

  if (tsp_head & TSP_payload_unit_start) { /* new PES packet */
    int pes_header_len;

    pes_header_len = demux_ts_parse_pes_header (this, m, ts, len);

    if (pes_header_len <= 0) {
      if (m->buf)
        m->buf->free_buffer(m->buf);
      m->buf = NULL;
      m->corrupted_pes = 1;
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
              "demux_ts: PID %u: corrupted pes encountered\n", m->pid);
    } else {
      m->corrupted_pes = 0;
      if (m->pes_bytes_left > 0)
        m->pes_bytes_left += m->buf->size;
      /* skip PES header */
      ts  += pes_header_len;
      len -= pes_header_len;
      update_extra_info (this, m);
      /* rate estimation */
      if ((this->tbre_pid == INVALID_PID) && (this->audio_fifo == m->fifo))
        this->tbre_pid = m->pid;
      if (m->pid == this->tbre_pid)
        demux_ts_tbre_update (this, TBRE_MODE_AUDIO_PTS, m->pts);
    }
  }

  if (!m->corrupted_pes) {
    int room = m->buf->max_size - m->buf->size;

    /* append data */
    m->resume &= ~PES_FLUSHED;
    if ((int)len > room) {
      buf_element_t *new_buf;
      new_buf = m->fifo->buffer_pool_realloc (m->buf, m->buf->size + len);
      this->enlarge_total++;
      if (!new_buf) {
        this->enlarge_ok++;
        xine_small_memcpy (m->buf->mem + m->buf->size, ts, len);
        m->buf->size += len;
      } else {
        if (room > 0)
          xine_small_memcpy (m->buf->mem + m->buf->size, ts, room);
        m->pes_bytes_left -= m->buf->max_size;
        m->buf->size = m->buf->max_size;
        demux_ts_send_buffer (this, m, 0);
        m->buf = new_buf;
        /* m->buf->decoder_flags = BUF_FLAG_MERGE; */
        xine_small_memcpy (m->buf->mem, ts + room, len - room);
        m->buf->size = len - room;
      }
    } else {
      xine_small_memcpy (m->buf->mem + m->buf->size, ts, len);
      m->buf->size += len;
    }

    /* flush now? */
    do {
      if ((m->pes_bytes_left > 0) && (m->buf->size >= m->pes_bytes_left)) {
        /* PES payload complete */
        m->pes_bytes_left -= m->buf->size;
        /* skip rest data - there shouldn't be any */
        m->corrupted_pes = 1;
        if (m->resume & PES_RESUME)
          return;
        break;
      }
      /* If video data ends to sequence end code, flush buffer. */
      /* (there won't be any more data -> no pusi -> last buffer is never flushed) */
      if (m->pid == this->videoPid && m->buf->size > 4 && m->buf->mem[m->buf->size-4] == 0) {
        if (m->type == BUF_VIDEO_MPEG) {
          if (!memcmp(&m->buf->mem[m->buf->size-4], "\x00\x00\x01\xb7", 4)) {
            xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
              "demux_ts: PID %u: flushing after MPEG end of sequence code\n", m->pid);
            break;
          }
        } else if (m->type == BUF_VIDEO_H264) {
          if ((!memcmp(&m->buf->mem[m->buf->size-4], "\x00\x00\x01\x0a", 4)) ||
            ((m->buf->size > 5) && !memcmp(&m->buf->mem[m->buf->size-5], "\x00\x00\x01\x0a", 4))) {
            xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
              "demux_ts: PID %u: flushing after H.264 end of sequence code\n", m->pid);
            break;
          }
        } else if (m->type == BUF_VIDEO_VC1) {
          if (!memcmp(&m->buf->mem[m->buf->size-4], "\x00\x00\x01\x0a", 4)) {
            xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
              "demux_ts: PID %u: flushing after VC-1 end of sequence code\n", m->pid);
            break;
          }
        }
      }
      return;
    } while (0);
    demux_ts_flush_media (this, m);
    m->buf = m->fifo->buffer_pool_alloc (m->fifo);
  }
}

/* Find the first ISO 639 language descriptor (tag 10) and
 * store the 3-char code in dest, nullterminated.  If no
 * code is found, zero out dest.
 **/
static void demux_ts_get_lang_desc(demux_ts_t *this, char *dest,
  const uint8_t *data, int length) {
  const uint8_t *d = data;

  while (d < (data + length))

    {
      if (d[0] == DESCRIPTOR_LANG && d[1] >= 4)

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
static uint32_t demux_ts_get_reg_desc (demux_ts_t *this, const uint8_t *data, int length) {
  const uint8_t *d = data, *e = data + length - 5;
  while (d < e) {
    if ((d[0] == DESCRIPTOR_REG_FORMAT) && (d[1] >= 4)) {
      char b[20];
      uint32_t r = _X_ME_32 (d + 2);
      _x_tag32_me2str (b, r);
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "demux_ts: found registration format identifier [%s].\n", b);
      return r;
    }
    d += 2 + d[1];
  }
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: found no format id.\n");
  return 0;
}

/*
 * NAME demux_ts_parse_pmt
 *
 * Parse a PMT. The PMT is expected to be exactly one section long.
 *
 * In other words, the PMT is assumed to describe a reasonable number of
 * video, audio and other streams (with descriptors).
 * FIXME: Implement support for multi section PMT.
 */
static void demux_ts_parse_pmt (demux_ts_t *this, const uint8_t *pkt,
  unsigned int pusi, unsigned int plen, uint32_t program_count, uint32_t pid) {

#ifdef TS_PMT_LOG
  uint32_t       table_id;
  uint32_t       version_number;
#endif
  uint32_t       section_syntax_indicator;
  uint32_t       section_length;
  uint32_t       program_number;
  uint32_t       current_next_indicator;
  uint32_t       section_number;
  uint32_t       last_section_number;
  uint32_t       program_info_length;
  uint32_t       crc32;
  uint32_t       calc_crc32;
  uint32_t       coded_length;
  const uint8_t *stream;
  unsigned int   i;
  int            mi;
  demux_ts_pmt  *pmt;

  pmt = this->pmts[program_count];
  if (!pmt) {
    /* allocate space for largest possible section */
    pmt = malloc (sizeof (demux_ts_pmt));
    if (!pmt)
      return;
    this->pmts[program_count] = pmt;
    pmt->program   = this->programs[program_count];
    pmt->pid       = INVALID_PID;
    pmt->length    = 0;
    pmt->crc       = 0;
    pmt->write_pos = 0;
  }
  /* reassemble the section */
  if (pusi) {
    unsigned int pointer = (unsigned int)pkt[0] + 1;
    pmt->write_pos = 0;
    /* offset the section by n + 1 bytes. this is sometimes used to let it end
       at an exact TS packet boundary */
    if (plen <= pointer) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "demux_ts: demux error! PMT with invalid pointer\n");
      return;
    }
    plen -= pointer;
    pkt += pointer;
  } else {
    if (!pmt->write_pos)
      return;
  }
  if (plen > sizeof (pmt->buf) - pmt->write_pos)
    plen = sizeof (pmt->buf) - pmt->write_pos;
  xine_small_memcpy (pmt->buf + pmt->write_pos, pkt, plen);
  pmt->write_pos += plen;

  /* lets see if we got the section length already */
  pkt = pmt->buf;
  if (pmt->write_pos < 3)
    return;
  section_length = ((((uint32_t)pkt[1] << 8) | pkt[2]) & 0xfff) + 3;
  /* this should be at least the head plus crc */
  if (section_length < 8 + 4) {
    pmt->write_pos = 0;
    return;
  }

  /* lets see if we got the section complete */
  if (pmt->write_pos < section_length)
    return;

  /* same crc means either same table, or wrong crc - both are reason for skipping. */
  crc32 = _X_BE_32 (pkt + section_length - 4);
  if ((pmt->length == section_length) &&
      (pmt->crc    == crc32) &&
      (pmt->pid    == pid))
    return;

  /* Check CRC. */
  calc_crc32 = htonl (xine_crc32_ieee (0xffffffff, pkt, section_length - 4));
  if (crc32 != calc_crc32) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "demux_ts: error! PMT pid %u with invalid CRC32 0x%08x (should be 0x%08x).\n",
      pid, crc32, calc_crc32);
    return;
  }
#ifdef TS_PMT_LOG
  printf ("demux_ts: PMT CRC32 ok.\n");
#endif

  /* OK now parse it */
  pmt->write_pos = 0;
#ifdef TS_PMT_LOG
  table_id                  = (unsigned int)pkt[0];
#endif
  section_syntax_indicator  = (pkt[1] >> 7) & 0x01;
  program_number            = ((uint32_t)pkt[3] << 8) | pkt[4];
#ifdef TS_PMT_LOG
  version_number            = ((uint32_t)pkt[5] >> 1) & 0x1f;
#endif
  current_next_indicator    =  pkt[5] & 0x01;
  section_number            =  pkt[6];
  last_section_number       =  pkt[7];

#ifdef TS_PMT_LOG
  printf ("demux_ts: PMT table_id: %.2x\n", table_id);
  printf ("              section_syntax: %d\n", section_syntax_indicator);
  printf ("              section_length: %d (%#.3x)\n", section_length, section_length);
  printf ("              program_number: %#.4x\n", program_number);
  printf ("              version_number: %d\n", version_number);
  printf ("              c/n indicator: %d\n", current_next_indicator);
  printf ("              section_number: %d\n", section_number);
  printf ("              last_section_number: %d\n", last_section_number);
#endif

  if ((section_syntax_indicator != 1) || !current_next_indicator)
    return;

  if ((section_number != 0) || (last_section_number != 0)) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "demux_ts: FIXME (unsupported) PMT consists of multiple (%d) sections\n", last_section_number);
    return;
  }

  if (program_number != pmt->program) {
    /* several programs can share the same PMT pid */
#ifdef TS_PMT_LOG
    printf ("Program Number is %i, looking for %i\n", program_number, pmt->program);
    printf ("demux_ts: waiting for next PMT on this PID...\n");
#endif
    return;
  }

  pmt->length = section_length;
  pmt->crc    = crc32;
  pmt->pid    = pid;

  /* dont "parse" the CRC */
  section_length -= 4;

  /*
   * ES definitions start here...we are going to learn upto one video
   * PID and one audio PID.
   */
  program_info_length = ((pkt[10] << 8) | pkt[11]) & 0x0fff;

/* Program info descriptor is currently just ignored.
 * printf ("demux_ts: program_info_desc: ");
 * for (i = 0; i < program_info_length; i++)
 *       printf ("%.2x ", this->pmt[program_count][12+i]);
 * printf ("\n");
 */
  stream = pkt + 12 + program_info_length;
  coded_length = 12 + program_info_length;
  if (coded_length > section_length) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "demux error! PMT with inconsistent progInfo length\n");
    return;
  }
  section_length -= coded_length;

  /*
   * Forget the current video, audio and subtitle PIDs; if the PMT has not
   * changed, we'll pick them up again when we parse this PMT, while if the
   * PMT has changed (e.g. an IPTV streamer that's just changed its source),
   * we'll get new PIDs that we should follow.
   */
  this->videoPid = INVALID_PID;
  this->spu_pid = INVALID_PID;

  this->spu_langs_count = 0;
  reset_track_map(this->video_fifo);

  /*
   * Extract the elementary streams.
   */
  while (section_length > 0) {
    uint32_t v = _X_BE_32 (stream + 1);
    uint32_t pid = (v >> 16) & 0x1fff;
    uint32_t stream_info_length = v & 0xfff;
    uint32_t coded_length = 5 + stream_info_length;

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
    case STREAM_VIDEO_HEVC:
    case STREAM_VIDEO_VC1:
      if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
        demux_ts_hexdump (this, "demux_ts: pmt video descriptor", stream, coded_length);
      if (this->videoPid == INVALID_PID) {
#ifdef TS_PMT_LOG
        printf ("demux_ts: PMT video pid 0x%.4x type %2.2x\n", pid, stream[0]);
#endif

        mi = demux_ts_dynamic_pmt_find (this, pid, BUF_VIDEO_BASE, stream[0]);
        if (mi >= 0) {
          this->videoMedia = mi;
          this->videoPid = pid;
        }
      }

      break;
    case ISO_11172_AUDIO:
    case ISO_13818_AUDIO:
    case ISO_13818_PART7_AUDIO:
    case ISO_14496_PART3_AUDIO:
      if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
        demux_ts_hexdump (this, "demux_ts: pmt audio descriptor", stream, coded_length);
      if (this->audio_tracks_count < MAX_AUDIO_TRACKS) {

        mi = demux_ts_dynamic_pmt_find (this, pid, BUF_AUDIO_BASE, stream[0]);
        if (mi >= 0) {
#ifdef TS_PMT_LOG
          printf ("demux_ts: PMT audio pid 0x%.4x type %2.2x\n", pid, stream[0]);
#endif
          demux_ts_get_lang_desc (this,
            this->audio_tracks[this->media[mi].type & 0xff].lang,
            stream + 5, stream_info_length);
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
      {
        uint32_t format_identifier = demux_ts_get_reg_desc (this, stream + 5, stream_info_length);
        if (format_identifier == ME_FOURCC ('H','E','V','C')) {
          mi = demux_ts_dynamic_pmt_find (this, pid, BUF_VIDEO_BASE, STREAM_VIDEO_HEVC);
          if (mi >= 0) {
            this->videoMedia = mi;
            this->videoPid = pid;
          }
          break;
        }
      }

      /* FIXME: We may have multiple streams in this pid.
       * For now, we only handle 1 per base type (a/v/s). */
      for (i = 5; i < coded_length; i += stream[i+1] + 2) {

        if ((stream[i] == DESCRIPTOR_AC3) || (stream[i] == DESCRIPTOR_EAC3) || (stream[i] == DESCRIPTOR_DTS)) {
          mi = demux_ts_dynamic_pmt_find (this, pid, BUF_AUDIO_BASE,
            stream[i] == DESCRIPTOR_AC3 ? STREAM_AUDIO_AC3 :
            stream[i] == DESCRIPTOR_DTS ? STREAM_AUDIO_DTS :
            STREAM_AUDIO_EAC3);
          if (mi >= 0) {
#ifdef TS_PMT_LOG
            printf ("demux_ts: PMT AC3 audio pid 0x%.4x type %2.2x\n", pid, stream[0]);
#endif
            demux_ts_get_lang_desc (this,
              this->audio_tracks[this->media[mi].type & 0xff].lang,
              stream + 5, stream_info_length);
            break;
          }
        }

        /* Teletext */
        else if (stream[i] == DESCRIPTOR_TELETEXT)
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
        else if (stream[i] == DESCRIPTOR_DVBSUB)
          {
            int pos;

            mi = demux_ts_dynamic_pmt_find (this, pid, BUF_SPU_BASE, stream[0]);
            if (mi < 0) break;

            /* set this early for demux_ts_update_spu_channel () below. */
            this->media[mi].spu_type = BUF_SPU_DVB;

            for (pos = i + 2;
                 pos + 8 <= (int)i + 2 + stream[i + 1]
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
                lang->media_index = mi;
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
      if (this->hdmv > 0)
        /* ignore BluRay menu streams */
        break;
      /* fall thru */

    case HDMV_SPU_TEXT:
    case HDMV_SPU_BITMAP:
      if (this->hdmv > 0) {
        if ((pid >= 0x1200 && pid < 0x1300) || pid == 0x1800) {
          /* HDMV Presentation Graphics / SPU */

          if (this->spu_langs_count >= MAX_SPU_LANGS) {
            xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
              "demux_ts: too many SPU tracks! ignoring pid %u\n", pid);
            break;
          }

          mi = demux_ts_dynamic_pmt_find (this, pid, BUF_SPU_BASE, stream[0]);
          if (mi < 0) break;

          demux_ts_spu_lang *lang = &this->spu_langs[this->spu_langs_count];

          strcpy(lang->desc.lang, "und");
          lang->pid = pid;
          lang->media_index = mi;

          if (stream[0] == HDMV_SPU_TEXT) {
            /* This is the only stream in the mux */
            this->media[mi].type = BUF_SPU_HDMV_TEXT;
            this->spu_pid = pid;
          } else {
            this->media[mi].type = BUF_SPU_HDMV;
          }
          demux_send_special_spu_buf( this, this->media[mi].type, this->spu_langs_count );
          this->media[mi].spu_type = this->media[mi].type;
          this->media[mi].type |= this->spu_langs_count;
          this->media[mi].sure_type = this->media[mi].type;
          this->spu_langs_count++;
#ifdef TS_PMT_LOG
          printf("demux_ts: HDMV subtitle stream_type: 0x%.2x pid: 0x%.4x\n",
                 stream[0], pid);
#endif
          break;
        }
      }
      /* fall thru */
    case HDMV_AUDIO_A1_EAC3_SEC:
    case HDMV_AUDIO_A2_DTSHD_SEC:
      if (this->hdmv > 0) {
        if (pid >= 0x1a00 && pid < 0x1a20) {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                  "demux_ts: Skipping unsupported HDMV secondary audio stream_type: 0x%.2x pid: 0x%.4x\n",
                  stream[0], pid);
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

        uint32_t format_identifier = demux_ts_get_reg_desc (this, stream + 5, stream_info_length);
        /* If no format identifier, assume A52 */
        if ((format_identifier == ME_FOURCC ('A','C','-','3'))
          || (format_identifier == 0)
          || (((format_identifier == ME_FOURCC ('H','D','M','V')) || (this->hdmv > 0))
            && (stream[0] == HDMV_AUDIO_80_PCM)) /* BluRay PCM */) {

          mi = demux_ts_dynamic_pmt_find (this, pid, BUF_AUDIO_BASE, stream[0]);
          if (mi >= 0) {
            demux_ts_get_lang_desc (this,
              this->audio_tracks[this->media[mi].type & 0xff].lang,
              stream + 5, stream_info_length);
#ifdef TS_PMT_LOG
            printf ("demux_ts: PMT audio pid 0x%.4x type %2.2x\n", pid, stream[0]);
#endif
            break;
          }
        }
      }

      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "demux_ts: PMT unknown stream_type: 0x%.2x pid: %u\n", stream[0], pid);

#ifdef TS_PMT_LOG
      printf ("demux_ts: PMT unknown stream_type: 0x%.2x pid: 0x%.4x\n",
              stream[0], pid);

      for (i = 5; i < coded_length; i++)
        printf ("%.2x ", stream[i]);
      printf ("\n");
#endif
      break;
    }
    stream += coded_length;
    section_length -= coded_length;
  }

  /*
   * Get the current PCR PID.
   */
  {
    uint32_t pid = _X_BE_32 (pmt->buf + 6) & 0x1fff;
    if (this->pcr_pid != pid) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: PCR pid %u.\n", pid);
      this->pcr_pid = pid;
    }
  }

  if (this->stream->spu_channel >= 0)
    demux_ts_update_spu_channel (this);

  demux_ts_dynamic_pmt_clean (this);

  demux_ts_tbre_reset (this);

  /* Inform UI of channels changes */
  xine_event_t ui_event;
  ui_event.type = XINE_EVENT_UI_CHANNELS_CHANGED;
  ui_event.data_length = 0;
  xine_event_send( this->stream, &ui_event );
}

#if TS_PACKET_READER == 1
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

  this->frame_pos += this->pkt_size;

  if ( (this->packet_number) >= this->npkt_read) {

    /* NEW: handle read returning less packets than NPKT_PER_READ... */
    do {
      this->frame_pos = this->input->get_current_pos (this->input);

      read_length = this->input->read(this->input, this->buf,
                                      this->pkt_size * NPKT_PER_READ);

      if (read_length < 0) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                 "demux_ts: read returned %d\n", read_length);
        if (this->read_retries > 2)
          this->status = DEMUX_FINISHED;
        this->read_retries++;
        return NULL;
      }
      this->read_retries = 0;

      if (read_length % this->pkt_size) {
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
        demux_ts_flush(this);
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
#endif


static int64_t demux_ts_adaptation_field_parse (const uint8_t *data, uint32_t adaptation_field_length) {

#ifdef TS_LOG
  uint32_t    discontinuity_indicator=0;
  uint32_t    random_access_indicator=0;
  uint32_t    elementary_stream_priority_indicator=0;
#endif
  uint32_t    PCR_flag=0;
  int64_t     PCR=-1;
#ifdef TS_LOG
  uint32_t    EPCR=0;
  uint32_t    OPCR_flag=0;
  uint32_t    OPCR=0;
  uint32_t    EOPCR=0;
  uint32_t    slicing_point_flag=0;
  uint32_t    transport_private_data_flag=0;
  uint32_t    adaptation_field_extension_flag=0;
#endif
  uint32_t    offset = 1;

#ifdef TS_LOG
  discontinuity_indicator = ((data[0] >> 7) & 0x01);
  random_access_indicator = ((data[0] >> 6) & 0x01);
  elementary_stream_priority_indicator = ((data[0] >> 5) & 0x01);
#endif
  PCR_flag = ((data[0] >> 4) & 0x01);
#ifdef TS_LOG
  OPCR_flag = ((data[0] >> 3) & 0x01);
  slicing_point_flag = ((data[0] >> 2) & 0x01);
  transport_private_data_flag = ((data[0] >> 1) & 0x01);
  adaptation_field_extension_flag = (data[0] & 0x01);
#endif

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
    if (adaptation_field_length < offset + 6)
      return -1;

    PCR   = _X_BE_32 (data + offset);
    PCR <<= 1;
    PCR  |= data[offset + 4] >> 7;

#ifdef TS_LOG
    EPCR = ((data[offset+4] & 0x1) << 8) | data[offset+5];
    printf ("demux_ts: PCR: %lld, EPCR: %u\n",
            PCR, EPCR);
#endif
    offset+=6;
  }

#ifdef TS_LOG
  if(OPCR_flag) {
    if (adaptation_field_length < offset + 6)
      return PCR;

    OPCR   = _X_BE_32 (data + offset);
    OPCR <<= 1;
    OPCR  |= data[offset + 4] >> 7;
    EOPCR = ((data[offset+4] & 0x1) << 8) | data[offset+5];

    printf ("demux_ts: OPCR: %u, EOPCR: %u\n",
            OPCR,EOPCR);

    offset+=6;
  }

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
#endif /* TS_LOG */

  return PCR;
}

#if TS_PACKET_READER == 2
static int sync_ts (const uint8_t *buf, int len) {
  int offs = len - 377;
  while (offs > 0) {
    if ((buf[0] == SYNC_BYTE) && (buf[188] == SYNC_BYTE) && (buf[376] == SYNC_BYTE))
      return len - 377 - offs;
    buf++;
    offs--;
  }
  return -1;
}

static int sync_hdmv (const uint8_t *buf, int len) {
  int offs = len - 385;
  while (offs > 0) {
    if ((buf[0] == SYNC_BYTE) && (buf[192] == SYNC_BYTE) && (buf[384] == SYNC_BYTE))
      return len - 385 - offs;
    buf++;
    offs--;
  }
  return -1;
}

static const uint8_t *sync_next (demux_ts_t *this) {
  int reread = 3;
  int rescan = 8;
  while (1) {
    const uint8_t *p = this->buf + this->buf_pos;
    int left = this->buf_size - this->buf_pos;
    if (this->hdmv > 0) {
      /* next packet already there? */
      if ((left >= 192) && (p[0] == SYNC_BYTE)) {
        this->buf_pos += 192;
        this->frame_pos += 192;
        return p;
      }
      if (left <= 0) {
        this->buf_pos = 0;
        this->buf_size = 0;
      } else {
        int n;
        if (p[0] != SYNC_BYTE) {
          /* skip junk already there */
          n = sync_hdmv (p, left);
          if (n >= 0) {
            this->frame_pos += 192;
            this->buf_pos += n + 192;
            p += n;
            return p;
          }
          /* wrong mode ?? */
          if (sync_ts (p, left) >= 0) {
            this->hdmv = 0;
            continue;
          }
          if (left > 192)
            left = 192;
          this->buf_pos = this->buf_size - left;
          /* return without error, allow engine to stop */
          if (--rescan <= 0)
            return NULL;
        }
        this->buf_size = left;
        /* align */
        n = this->buf_pos;
        if (n > 0) {
          this->buf_pos = 0;
          if (left <= n)
            memcpy (this->buf, this->buf + n, left);
          else
            memmove (this->buf, this->buf + n, left);
        }
      }
    } else { /* plain ts */
      /* next packet already there? */
      if ((left >= 188) && (p[0] == SYNC_BYTE)) {
        this->buf_pos += 188;
        this->frame_pos += 188;
        return p;
      }
      if (left <= 0) {
        this->buf_pos = 0;
        this->buf_size = 0;
      } else {
        int n;
        if (p[0] != SYNC_BYTE) {
          /* skip junk already there */
          n = sync_ts (p, left);
          if (n >= 0) {
            this->frame_pos += 188;
            this->buf_pos += n + 188;
            p += n;
            return p;
          }
          /* wrong mode ?? */
          if (sync_hdmv (p, left) >= 0) {
            this->hdmv = 1;
            continue;
          }
          if (left > 188)
            left = 188;
          this->buf_pos = this->buf_size - left;
          /* return without error, allow engine to stop */
          if (--rescan <= 0)
            return NULL;
        }
        this->buf_size = left;
        /* align */
        n = this->buf_pos;
        if (n > 0) {
          this->buf_pos = 0;
          if (left <= n)
            memcpy (this->buf, this->buf + n, left);
          else
            memmove (this->buf, this->buf + n, left);
        }
      }
    }
    /* refill */
    this->frame_pos = this->input->get_current_pos (this->input);
    {
      errno = 0;
      int n = this->input->read (this->input, this->buf + this->buf_size, this->buf_max - this->buf_size);
      if (n <= 0) {
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
          return NULL;
        }
        if (_x_action_pending(this->stream)) {
          return NULL;
        }
        if (--reread <= 0) {
          demux_ts_flush (this);
          this->status = DEMUX_FINISHED;
          return NULL;
        }
      }
      this->buf_size += n;
    }
  }
}
#endif

/* transport stream packet layer */
static void demux_ts_parse_packet (demux_ts_t*this) {

  const uint8_t *originalPkt;
  uint32_t       tsp_head;
  uint32_t       pid;
  unsigned int   data_offset;
  unsigned int   data_len;
  uint32_t       index;

  /* get next synchronised packet, or NULL */
#if TS_PACKET_READER == 2
  originalPkt = sync_next (this);
#elif TS_PACKET_READER == 1
  originalPkt = demux_synchronise(this);
#endif
  if (originalPkt == NULL)
    return;

  tsp_head = _X_BE_32 (originalPkt);
  pid      = (tsp_head & TSP_pid) >> 8;

#ifdef TS_HEADER_LOG
  printf ("demux_ts:ts_header:sync_byte=0x%.2x\n",
    tsp_head >> 24);
  printf ("demux_ts:ts_header:transport_error_indicator=%d\n",
    !!(tsp_head & TSP_transport_error));
  printf ("demux_ts:ts_header:payload_unit_start_indicator=%d\n",
    !!(tsp_head & TSP_payload_unit_start));
  printf ("demux_ts:ts_header:transport_priority=%d\n",
    !!(tsp_head & TSP_transport_priority));
  printf ("demux_ts:ts_header:pid=0x%.4x\n", pid);
  printf ("demux_ts:ts_header:transport_scrambling_control=0x%.1x\n",
    (tsp_head & TSP_scrambling_control) >> 6);
  printf ("demux_ts:ts_header:adaptation_field_control=0x%.1x\n",
    (tsp_head & (TSP_adaptation_field_1 | TSP_adaptation_field_0)) >> 4);
  printf ("demux_ts:ts_header:continuity_counter=0x%.1x\n",
    tsp_head & TSP_continuity_counter);
#endif
  /*
   * Discard packets that are obviously bad.
   */
  if ((tsp_head >> 24) != SYNC_BYTE) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "demux_ts: error! invalid ts sync byte %.2x\n", tsp_head >> 24);
    return;
  }
  if (tsp_head & TSP_transport_error) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: error! transport error\n");
    return;
  }

  if (tsp_head & TSP_scrambling_control) {
    unsigned int u;
    for (u = 0; u < this->scrambled_npids; u++) {
      if (this->scrambled_pids[u] == pid)
        return;
    }
    if (this->scrambled_npids < MAX_PIDS) {
      this->scrambled_pids[this->scrambled_npids] = pid;
      this->scrambled_npids++;
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: PID %u is scrambled!\n", pid);
    }
    return;
  }

  data_offset = 4;

  if (tsp_head & TSP_adaptation_field_1) {
    uint32_t adaptation_field_length = originalPkt[4];
    if (adaptation_field_length > PKT_SIZE - 5) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_ts: invalid adaptation field length\n");
      return;
    }

    if (adaptation_field_length > 0) {
      int64_t pcr = demux_ts_adaptation_field_parse (originalPkt+5, adaptation_field_length);
      if (pid == this->pcr_pid)
        demux_ts_tbre_update (this, TBRE_MODE_PCR, pcr);
      else if (pid == this->tbre_pid)
        demux_ts_tbre_update (this, TBRE_MODE_AUDIO_PCR, pcr);
    }
    /*
     * Skip adaptation header.
     */
    data_offset += adaptation_field_length + 1;
    if (data_offset >= PKT_SIZE) {
      /* no payload or invalid header */
      return;
    }
  }

  if (!(tsp_head & TSP_adaptation_field_0)) {
    return;
  }

  data_len = PKT_SIZE - data_offset;
  index = this->pid_index[pid];

  /* Do the demuxing in descending order of packet frequency! */
  if (!(index & 0x80)) {
    do {
      if (pid == this->videoPid) {
#ifdef TS_LOG
        printf ("demux_ts: Video pid: 0x%.4x\n", pid);
#endif
        break;
      }
      if ((this->media[index].type & BUF_MAJOR_MASK) == BUF_AUDIO_BASE) {
#ifdef TS_LOG
        printf ("demux_ts: Audio pid: 0x%.4x\n", pid);
#endif
        break;
      }
      if (pid == this->spu_pid) {
        /* DVBSUB */
#ifdef TS_LOG
        printf ("demux_ts: SPU pid: 0x%.4x\n", pid);
#endif
        break;
      }
      return;
    } while (0);
    demux_ts_buffer_pes (this, originalPkt + data_offset, index, tsp_head, data_len);
    return;
  }

  if (index != 0xff) {
    /* PMT */
    index &= 0x7f;
#ifdef TS_LOG
    printf ("demux_ts: PMT prog: 0x%.4x pid: 0x%.4x\n", this->program_number[index], this->pmt_pid[index]);
#endif
    demux_ts_parse_pmt (this, originalPkt + data_offset, tsp_head & TSP_payload_unit_start, data_len, index, pid);
    return;
  }

  if (pid == NULL_PID) {
#ifdef TS_LOG
    printf ("demux_ts: Null Packet\n");
#endif
    return;
  }

  /* PAT */
  if (pid == 0) {
    demux_ts_parse_pat (this, originalPkt + data_offset, tsp_head & TSP_payload_unit_start, data_len);
    return;
  }

  if (pid == 0x1ffb) {
    /* printf ("demux_ts: PSIP table. Program Guide etc....not supported yet. PID = 0x1ffb\n"); */
    return;
  }
}

/*
 * check for pids change events
 */

static void demux_ts_event_handler (demux_ts_t *this) {
  xine_event_t *event = NULL;

  while ((event = xine_event_next (this->event_queue, event))) {

    switch (event->type) {

    case XINE_EVENT_END_OF_CLIP:
      /* flush all streams */
      demux_ts_flush(this);
      /* fall thru */

    case XINE_EVENT_PIDS_CHANGE:

      demux_ts_dynamic_pmt_clear(this);
      this->send_newpts = 1;
      _x_demux_control_start (this->stream);
      break;

    }
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

  for (i = 0; this->programs[i] != INVALID_PROGRAM; i++) {
    if (this->pmts[i] != NULL) {
      free (this->pmts[i]);
      this->pmts[i] = NULL;
    }
  }
  for (i=0; i < MAX_PIDS; i++) {
    if (this->media[i].buf != NULL) {
      this->media[i].buf->free_buffer(this->media[i].buf);
      this->media[i].buf = NULL;
    }
  }

  xine_event_dispose_queue (this->event_queue);

#ifdef DUMP_VIDEO_HEADS
  if (this->vhdfile)
    fclose (this->vhdfile);
#endif

  if (this->enlarge_total)
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "demux_ts: %d of %d buffer enlarges worked.\n",
      this->enlarge_ok, this->enlarge_total);

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
  this->pcr_pid = INVALID_PID;
  this->audio_tracks_count = 0;
  this->media_num= 0;

  _x_demux_control_start (this->stream);

  this->input->seek (this->input, 0, SEEK_SET);

  this->send_newpts = 1;

  this->status = DEMUX_OK ;

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
  uint32_t caps;
  int i;

  if (playing) {
    /* Keyframe search below may mean waiting on input for several seconds (hls).
     * Output layers are in flush mode already, so there is no need to let fifos
     * run dry naturally. Flush them first here. */
    this->buf_flag_seek = 1;
    _x_demux_flush_engine (this->stream);
    /* Append sequence end code to video stream. */
    /* Keep ffmpeg h.264 video decoder from piling up too many DR1 frames, */
    /* and thus freezing video out. */
    if (this->videoPid != INVALID_PID && this->stream->video_fifo)
      post_sequence_end (this->stream->video_fifo, this->media[this->videoMedia].type);
  }

  if (this->stream->master != this->stream) {
    if (this->media_num == 1 && this->spu_langs_count == 1 &&
        this->media[this->spu_langs[0].media_index].type == BUF_SPU_HDMV_TEXT) {
      /* this stream is used as subtitle slave stream. Need to seek to 0. */
      start_pos = 0;
      start_time = 0;
    }
  }

  caps = this->input->get_capabilities (this->input);
  if (caps & (INPUT_CAP_SEEKABLE | INPUT_CAP_SLOW_SEEKABLE | INPUT_CAP_TIME_SEEKABLE)) {
    if ((caps & INPUT_CAP_TIME_SEEKABLE) && this->input->seek_time) {
      if (start_pos > 0) {
        int32_t duration = 0;
        if ((this->input->get_optional_data (this->input, &duration, INPUT_OPTIONAL_DATA_DURATION) == INPUT_OPTIONAL_SUCCESS)
          && (duration > 0)) {
          start_time = (double)start_pos * duration / 65535;
        }
      }
      this->input->seek_time (this->input, start_time, SEEK_SET);
    } else {
      start_pos = (off_t)((double)start_pos / 65535 * this->input->get_length (this->input));
      if ((!start_pos) && (start_time)) {
        if (this->input->seek_time) {
          this->input->seek_time (this->input, start_time, SEEK_SET);
        } else {
          start_pos = (int64_t)start_time * this->rate / 1000;
          this->input->seek (this->input, start_pos, SEEK_SET);
        }
      } else {
        this->input->seek (this->input, start_pos, SEEK_SET);
      }
    }
#if TS_PACKET_READER == 2
    this->buf_pos  = 0;
    this->buf_size = 0;
#endif
    /* Ideally, we seek to video keyframes.
     * Unfortunately, they are marked in a codec specific way,
     * and may even hide behind escape codes.
     * Limit scan to ~10 seconds / 8Mbyte. */
    if ((this->videoPid != INVALID_PID) && this->get_frametype && (this->keyframe_interval < 1000000)) {
      uint32_t n;
      uint32_t want_phead = (SYNC_BYTE << 24) | TSP_payload_unit_start | (this->videoPid << 8) | TSP_adaptation_field_0;
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "demux_ts: seek: keyframes repeat every %u pts, try finding next one.\n", this->keyframe_interval);
      n = (8 << 20) / 188;
      while (n--) {
        const uint8_t *p;
        uint32_t phead, len = 188;
#if TS_PACKET_READER == 2
        p = sync_next (this);
#elif TS_PACKET_READER == 1
        p = demux_synchronise(this);
#endif
        if (!p)
          break;
        /* ts packet head */
        phead = _X_BE_32 (p);
        if ((phead & (TSP_sync_byte | TSP_transport_error | TSP_payload_unit_start
                     | TSP_pid | TSP_scrambling_control | TSP_adaptation_field_0)) != want_phead)
          continue;
        p += 4;
        len -= 4;
        /* optional adaptation field */
        if (phead & TSP_adaptation_field_1) {
          uint32_t al = 1 + p[0];
          if (len < al)
            continue;
          p += al;
          len -= al;
        }
        /* pes head */
        if ((len < 9) || (_X_BE_32 (p) >> 8) != 1)
          continue;
        {
          uint32_t el = 9 + p[8];
          if (len < el)
            continue;
          p += el;
          len -= el;
        }
        /* frame type */
        if (this->get_frametype (p, len) != FRAMETYPE_I)
          continue;
#if TS_PACKET_READER == 2
        this->buf_pos -= this->hdmv > 0 ? 192 : 188;
#endif
        /* it works -- dont disable ourselves ;-) */
        this->last_keyframe_time = 0;
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
          "demux_ts: seek: found keyframe after %u packets.\n", (8 << 20) / 188 - n);
        break;
      }
    }
    /* If no keyframe info, lets use a sufficiently frequent PAT as a seek target instead. */
    else if ((this->videoPid != INVALID_PID) && (this->pat_interval < 900000)) {
      uint32_t n;
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "demux_ts: seek: PAT repeats every %u pts, try finding next one.\n", this->pat_interval);
      n = (8 << 20) / 188;
      while (n--) {
        const uint8_t *originalPkt;
#if TS_PACKET_READER == 2
        originalPkt = sync_next (this);
#elif TS_PACKET_READER == 1
        originalPkt = demux_synchronise(this);
#endif
        if (!originalPkt)
          break;
        if ((_X_BE_32 (originalPkt)
            & (TSP_sync_byte | TSP_transport_error | TSP_payload_unit_start
              | TSP_pid | TSP_scrambling_control | TSP_adaptation_field_0))
            == ((SYNC_BYTE << 24) | TSP_payload_unit_start | TSP_adaptation_field_0)) {
#if TS_PACKET_READER == 2
          this->buf_pos -= this->hdmv > 0 ? 192 : 188;
#endif
          this->last_pat_time = 0;
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
            "demux_ts: seek: found PAT after %u packets.\n", (8 << 20) / 188 - n);
          break;
        }
      }
    }
    /* seek is called from outside the demux thread.
     * DEMUX_FINISHED here may be a leftover from previous run, or the result of a seek
     * behind last keyframe. but that would trigger an annoying error message, and keep
     * user stuck until an explicit new seek, stop or open.
     * instead, defer a possible error to send_chunk () what will trigger a regular
     * stream end/switch. */
    this->status = DEMUX_OK;
  }

  this->send_newpts = 1;

  for (i=0; i<MAX_PIDS; i++) {
    demux_ts_media *m = &this->media[i];

    if (m->buf != NULL)
      m->buf->free_buffer(m->buf);
    m->buf            = NULL;
    m->counter        = INVALID_CC;
    m->corrupted_pes  = 1;
    m->pts            = 0;
    m->resume         = 0;
  }

  if( !playing ) {

    this->status        = DEMUX_OK;
    this->buf_flag_seek = 0;

  }

  demux_ts_tbre_reset (this);

  return this->status;
}

static int demux_ts_get_stream_length (demux_plugin_t *this_gen) {

  demux_ts_t*this = (demux_ts_t*)this_gen;
  unsigned int rate = this->rate;

  if (rate)
    return (int)((int64_t) this->input->get_length (this->input)
                 * 1000 / rate);

  return 0;
}


static uint32_t demux_ts_get_capabilities(demux_plugin_t *this_gen)
{
  (void)this_gen;
  return DEMUX_CAP_AUDIOLANG | DEMUX_CAP_SPULANG;
}

static int demux_ts_get_optional_data(demux_plugin_t *this_gen,
                                      void *data, int data_type)
{
  demux_ts_t *this = (demux_ts_t *) this_gen;

  /* be a bit paranoid */
  if (this == NULL || this->stream == NULL)
    return DEMUX_OPTIONAL_UNSUPPORTED;

  switch (data_type) {

    case DEMUX_OPTIONAL_DATA_AUDIOLANG:
      {
        char *str = data;
        int channel = *((int *)data);
        if ((channel >= 0) && ((unsigned int)channel < this->audio_tracks_count)) {
          if (this->audio_tracks[channel].lang[0]) {
            strcpy (str, this->audio_tracks[channel].lang);
          } else {
            /* input plugin may know the language */
            if (this->input->get_capabilities(this->input) & INPUT_CAP_AUDIOLANG)
              return DEMUX_OPTIONAL_UNSUPPORTED;
            sprintf (str, "%3i", channel);
          }
          return DEMUX_OPTIONAL_SUCCESS;
        } else {
          strcpy (str, "none");
        }
        return DEMUX_OPTIONAL_UNSUPPORTED;
      }

    case DEMUX_OPTIONAL_DATA_SPULANG:
        {
          char *str = data;
          int channel = *((int *)data);
          if ((channel >= 0) && ((unsigned int)channel < this->spu_langs_count)) {
            if (this->spu_langs[channel].desc.lang[0]) {
              strcpy (str, this->spu_langs[channel].desc.lang);
          } else {
            /* input plugin may know the language */
            if (this->input->get_capabilities(this->input) & INPUT_CAP_SPULANG)
              return DEMUX_OPTIONAL_UNSUPPORTED;
            sprintf (str, "%3i", channel);
          }
          return DEMUX_OPTIONAL_SUCCESS;
        } else {
          strcpy (str, "none");
        }
        return DEMUX_OPTIONAL_UNSUPPORTED;
      }

    default:
      return DEMUX_OPTIONAL_UNSUPPORTED;
  }
}

static int detect_ts (const uint32_t *buf, size_t len) {
  uint32_t stats_ts[188 / 4], stats_hdmv[192 / 4];
  /* Fold 188 or 192 counter slots over the buffer.
   * Count bytes that "fail" to be 0x47.
   * Consider a slot failed when >= 20% (1/5) of its bytes fail.
   * NOTE: this works with buffer size <= 188 * 127, or 23876.
   * we just need 2048. */
  {
    uint32_t i, v;
    v = 128 - len / (5 * 188);
    v += v << 8;
    v += v << 16;
    for (i = 0; i < 188 / 4; i++)
      stats_ts[i] = v;
    v = 128 - len / (5 * 192);
    v += v << 8;
    v += v << 16;
    for (i = 0; i < 192 / 4; i++)
      stats_hdmv[i] = v;
  }
  {
    const uint32_t *b = buf, *e = buf + len / 4;
    int i, j;
    i = 188 / 4 - 1;
    j = 192 / 4 - 1;
    while (b < e) {
      /* misuse plain int as a vector register.
       * endian does not matter here. */
      uint32_t a = *b++;
      a ^= 0x47474747;
      a |= a >> 4;
      a |= a >> 2;
      a |= a >> 1;
      a &= 0x01010101;
      stats_ts[i] += a;
      stats_hdmv[j] += a;
      if (--i < 0)
        i = 188 / 4 - 1;
      if (--j < 0)
        j = 192 / 4 - 1;
    }
  }
  {
    uint32_t s, i;
    /* Now count the _passed_ slots. */
    s = 0;
    for (i = 0; i < 188 / 4; i++)
      s += (stats_ts[i] >> 7) & 0x01010101;
    s += s >> 16;
    s += s >> 8;
    s &= 0x000000ff;
    s = 188 - s;
    /* 0x47 may appear again in packet head. */
    if ((s > 0) && (s < 5))
      return 0;
    s = 0;
    for (i = 0; i < 192 / 4; i++)
      s += (stats_hdmv[i] >> 7) & 0x01010101;
    s += s >> 16;
    s += s >> 8;
    s &= 0x000000ff;
    s = 192 - s;
    /* 0x47 may appear again in packet head, and in timestamp field.
     * FIXME: main read resync code is not really prepared for the latter. */
    if ((s > 0) && (s < 7))
      return 1;
  }
  return -1;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen,
                                    xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_ts_t *this;
  int         i;
  int         hdmv = -1;
  int         size;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    uint32_t buf[2048 / 4];

    size = _x_demux_read_header (input, (uint8_t *)buf, sizeof (buf));
    if (size < PKT_SIZE)
      return NULL;

    hdmv = detect_ts (buf, size);
    if (hdmv < 0)
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

  this = calloc (1, sizeof (*this));
  if (!this)
    return NULL;

#ifndef HAVE_ZERO_SAFE_MEM
  this->scrambled_npids    = 0;
  this->audio_tracks_count = 0;
  this->spu_langs_count    = 0;
  this->pat_length         = 0;
  this->pat_crc            = 0;
  this->last_pat_time      = 0;
  this->last_keyframe_time = 0;
  this->get_frametype      = NULL;
  this->bounce_left        = 0;
  this->apts               = 0;
  this->bpts               = 0;
  this->last_pts[0]        = 0;
  this->last_pts[1]        = 0;
#  if TS_PACKET_READER == 2
  this->buf_pos            = 0;
  this->buf_size           = 0;
#  endif
  this->enlarge_total      = 0;
  this->enlarge_ok         = 0;
#endif

#  if TS_PACKET_READER == 2
  this->buf_max   = (input->get_capabilities (input) & INPUT_CAP_SEEKABLE) ? BUF_SIZE : SMALL_BUF_SIZE;
#  endif

  this->stream    = stream;
  this->input     = input;

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

  this->transport_stream_id = -1;

  for (i = 0; i < MAX_PIDS; i++) {
    this->media[i].pid = INVALID_PID;
#ifndef HAVE_ZERO_SAFE_MEM
    this->media[i].buf = NULL;
#endif
  }

#ifndef HAVE_ZERO_SAFE_MEM
  for (i = 0; i < MAX_PMTS; i++) {
    this->pmts[i] = NULL;
  }
#endif

  this->programs[0] = INVALID_PROGRAM;

  memset (this->pid_index, 0xff, sizeof (this->pid_index));

  this->videoPid = INVALID_PID;
  this->pcr_pid = INVALID_PID;

  this->rate = 1000000; /* byte/sec */
  this->tbre_pid = INVALID_PID;

  this->pat_interval      = 0xffffffff;
  this->keyframe_interval = 0xffffffff;

  this->status = DEMUX_FINISHED;

  /* DVBSUB */
  this->spu_pid = INVALID_PID;
  this->spu_media_index = 0xffffffff;
  this->current_spu_channel = -1;

  /* dvb */
  this->event_queue = xine_event_new_queue (this->stream);
  {
    static const int want_types[] = {
      XINE_EVENT_END_OF_CLIP,
      XINE_EVENT_PIDS_CHANGE,
      XINE_EVENT_QUIT
    };
    xine_event_select (this->event_queue, want_types);
  }

  /* HDMV */
  this->hdmv       = hdmv;
#if TS_PACKET_READER == 1
  this->pkt_offset = (hdmv > 0) ? 4 : 0;
  this->pkt_size   = PKT_SIZE + this->pkt_offset;
#endif

#ifdef DUMP_VIDEO_HEADS
  this->vhdfile = fopen ("video_heads.log", "rb+");
#endif

  return &this->demux_plugin;
}

/*
 * ts demuxer class
 */
void *demux_ts_init_class (xine_t *xine, const void *data) {
  (void)xine;
  (void)data;

  static const demux_class_t demux_ts_class = {
    .open_plugin     = open_plugin,
    .description     = N_("MPEG Transport Stream demuxer"),
    .identifier      = "MPEG_TS",
    .mimetypes       = "video/mp2t: m2t: MPEG2 transport stream;",

    /* accept dvb streams; also handle the special dvbs,dvbt and dvbc
     * mrl formats: the content is exactly the same but the input plugin
     * uses a different tuning algorithm [Pragma]
     */
    .extensions      = "ts m2t trp m2ts mts dvb:// dvbs:// dvbc:// dvbt://",
    .dispose         = NULL,
  };

  return (void *)&demux_ts_class;
}
