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
 * $Id: demux_ts.c,v 1.15 2001/09/17 14:32:05 jcdutton Exp $
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
 * 10-Sep-2001 James Courtier-Dutton <jcdutton>
 *                              Re-wrote sync code so that it now does not loose any data.
 * 27-Aug-2001 Hubert Matthews  Reviewed by: n/a
 *	                        Added in synchronisation code.
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
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#include "xine_internal.h"
#include "monitor.h"
#include "demux.h"

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

#define NULL_PID 8191
#define INVALID_PID ((unsigned int)(-1))
#define INVALID_PROGRAM ((unsigned int)(-1))
#define INVALID_CC ((unsigned int)(-1))

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
  
  fifo_buffer_t   *fifoAudio;
  fifo_buffer_t   *fifoVideo;
  
  input_plugin_t  *input;
  
  pthread_t        thread;
  
  int              status;
  
  int              blockSize;
  int              rate;
  demux_ts_media   media[MAX_PIDS];
  uint32_t	   program_number[MAX_PMTS];
  uint32_t	   pmt_pid[MAX_PMTS];
  /*
   * Stuff to do with the transport header. As well as the video
   * and audio PIDs, we keep the index of the corresponding entry
   * inthe media[] array.
   */
  unsigned int     programNumber;
  unsigned int     pmtPid;
  unsigned int     pcrPid;
  unsigned int     pid;
  unsigned int     videoPid;
  unsigned int     audioPid;
  unsigned int     videoMedia;
  unsigned int     audioMedia;
} demux_ts;

static uint32_t xine_debug;


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
				unsigned char *pkt, unsigned int pus) {

  uint32_t	 table_id;
  uint32_t	 section_syntax_indicator;
  uint32_t	 section_length;
  uint32_t	 transport_stream_id;
  uint32_t	 version_number;
  uint32_t	 current_next_indicator;
  uint32_t	 section_number;
  uint32_t	 last_section_number;

  unsigned char *program;
  unsigned int   program_number;
  unsigned int   pmt_pid;
  unsigned int   program_count;

  /*
   * A PAT in a single section should start with a payload unit start
   * indicator set.
   */
  if (!pus) {
    printf ("demux_ts: demux error! PAT without payload unit start\n");
    return;
  }
  
  /*
   * PAT packets with a pus start with a pointer. Skip it!
   */
  pkt += pkt[4];
  if (pkt - original_pkt > PKT_SIZE) {
    printf ("demux_ts: demux error! PAT with invalid pointer\n");
    return;
  }
  if (!(pkt[10] & 0x01)) {
    /*
     * Not current!
     */
    return;
  }
  table_id = (unsigned int)pkt[5] ;
  section_syntax_indicator = (((unsigned int)pkt[6] >> 8) & 1) ;
  section_length = (((unsigned int)pkt[6] & 0x3) << 8) | pkt[7];
  xprintf (VERBOSE|DEMUX,"PAT table_id=%d section_syntax=%d section len=%d\n",
           table_id,
           section_syntax_indicator,
           section_length);
  if (pkt - original_pkt > BODY_SIZE - 1 - 3 - (int)section_length) {
    printf ("demux_ts: demux error! PAT with invalid section length\n");
    return;
  }
  if ((pkt[11]) || (pkt[12])) {
    printf ("demux_ts: demux error! PAT with invalid section %02x of %02x\n", pkt[11], pkt[12]);
    return;
  }
  
  /*
   * TBD: at this point, we should check the CRC. Its not that expensive, and
   * the consequences of getting it wrong are dire!
   */
  
  /*
   * Process all programs in the program loop.
   */
  program_count = 0;
  for (program = pkt + 13; program < pkt + 13 + section_length - 9; program += 4) {
    program_number = ((unsigned int)program[0] << 8) | program[1];
    pmt_pid = (((unsigned int)program[2] & 0x1f) << 8) | program[3];
    
    /*
     * Skip NITs completely.
     */
    if (!program_number)
      continue;
    program_count = 0;
    while ((this->program_number[program_count] != INVALID_PROGRAM) && 
           (this->program_number[program_count] != program_number) ) {
    program_count++;
    } 
    this->program_number[program_count] = program_number; 
    this->pmt_pid[program_count] = pmt_pid; 
    /*
     * If we have yet to learn our program number, then learn it.
     */
    program_count = 0;
    while ((this->program_number[program_count] != INVALID_PROGRAM) ) {
      xprintf(VERBOSE|DEMUX, "acquiring count=%d programNumber=%u pmtPid=%04x\n",
	 program_count,
         this->program_number[program_count],
         this->pmt_pid[program_count]);
      program_count++;
    }
  }
}

static int demux_ts_parse_pes_header (demux_ts_media *m, uint8_t *buf, int packet_len) {

  unsigned char *p;
  uint32_t       header_len;
  uint32_t       PTS;
  uint32_t       stream_id;

  p = buf; 

  /* we should have a PES packet here */

  if (p[0] || p[1] || (p[2] != 1)) {
    printf ("demux_ts: error %02x %02x %02x (should be 0x000001) \n",p[0],p[1],p[2]);
    return 0 ;
  }

  packet_len -= 6;
  /* packet_len = p[4] << 8 | p[5]; */
  stream_id  = p[3];

  if (packet_len==0)
    return 0;

  xprintf(VERBOSE|DEMUX, "packet stream id = %02x len = %d\n",
	  stream_id, packet_len);


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
    xprintf(VERBOSE | DEMUX, "unknown packet, id = %x\n",stream_id);
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

    printf ("fifo unavailable (%d)\n", mediaIndex);

    return; /* To avoid segfault if video out or audio out plugin not loaded */

  }

  /*
   * By checking the CC here, we avoid the need to check for the no-payload
   * case (i.e. adaptation field only) when it does not get bumped.
   */
  if (m->counter != INVALID_CC) {
    if ((m->counter & 0x0f) != cc) {
      printf("demux_ts: dropped input packet cc = %d expected = %d\n", cc, m->counter);
    }
  }

  m->counter = cc;
  m->counter++;

  if (pus) {
    
    /* new PES packet */
    
    if (ts[0] || ts[1] || ts[2] != 1) {
      fprintf(stderr, "PUS set but no PES header (corrupt stream?)\n");
      return;
    }
    
    if (!demux_ts_parse_pes_header(m, ts, len)) {
      m->broken_pes = 1;
      printf ("demux_ts: broken pes encountered\n");
    } else {
      m->broken_pes = 0;
      buf = m->fifo->buffer_pool_alloc(m->fifo);
      memcpy (buf->mem, ts+len-m->size, m->size); /* FIXME: reconstruct parser to do without memcpys */
      buf->content         = buf->mem;
      buf->size            = m->size;
      buf->type            = m->type;
      buf->PTS             = m->PTS;
      buf->decoder_info[0] = 1;
      m->fifo->put (m->fifo, buf);
    }

  } else if (!m->broken_pes) {
    buf = m->fifo->buffer_pool_alloc(m->fifo);
    memcpy (buf->mem, ts, len); /* FIXME: reconstruct parser to do without memcpys */
    buf->content         = buf->mem;
    buf->size            = len;
    buf->type            = m->type;
    buf->PTS             = 0;
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
 * NAME demux_ts_pmt_parse
 *
 * Parse a PMT. The PMT is expected to be exactly one section long,
 * and that section is expected to be contained in a single TS packet.
 *
 * In other words, the PMT is assumed to describe a reasonable number of
 * video, audio and other streams (with descriptors).
 */
static void demux_ts_parse_pmt(demux_ts *this,
			       unsigned char *originalPkt,
			       unsigned char *pkt,
			       unsigned int pus) {
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
      ISO_13818_AUX = 14
    } streamType;
  uint32_t	 table_id;
  uint32_t	 section_syntax_indicator;
  uint32_t	 section_length;
  uint32_t	 transport_stream_id;
  uint32_t	 version_number;
  uint32_t	 current_next_indicator;
  uint32_t	 section_number;
  uint32_t	 last_section_number;
  //unsigned int length;
  unsigned int programInfoLength;
  unsigned int codedLength;
  unsigned int mediaIndex;
  unsigned int pid;
  unsigned char *stream;
  
  /*
   * A PMT in a single section should start with a payload unit start
   * indicator set.
   */
  if (!pus) {
    fprintf (stderr, "demux error! PMT without payload unit start\n");
    return;
  }
  
  /*
   * PMT packets with a pus start with a pointer. Skip it!
   */
  pkt += pkt[4];
  if (pkt - originalPkt > PKT_SIZE) {
    fprintf (stderr, "demux error! PMT with invalid pointer\n");
    return;
  }
  if (!(pkt[10] & 0x01)) {
    /*
     * Not current!
     */
    return;
  }
  table_id = (unsigned int)pkt[5] ;
  section_syntax_indicator = (((unsigned int)pkt[6] >> 8) & 1) ;
  section_length = (((unsigned int)pkt[6] & 0x3) << 8) | pkt[7];
  printf ("PMT table_id=%d section_syntax=%d section len=%d\n",
           table_id,
           section_syntax_indicator,
           section_length);
  if (pkt - originalPkt > BODY_SIZE - 1 - 3 - (int)section_length) {
    fprintf (stderr, "demux error! PMT with invalid section length\n");
    return;
  }
  if ((pkt[11]) || (pkt[12])) {
    fprintf (stderr, "demux error! PMT with invalid section %02x of %02x\n", pkt[11], pkt[12]);
    return;
  }
  
  /*
   * TBD: at this point, we should check the CRC. Its not that expensive, and
   * the consequences of getting it wrong are dire!
   */
  
  /*
   * ES definitions start here...we are going to learn upto one video
   * PID and one audio PID.
   */
  
  programInfoLength = (((unsigned int)pkt[15] & 0x0f) << 8) | pkt[16];
  stream = &pkt[17] + programInfoLength;
  codedLength = 13 + programInfoLength;
  if (codedLength > section_length) {
    fprintf (stderr, "demux error! PMT with inconsistent progInfo length\n");
    return;
  }
  section_length -= codedLength;
  
  /*
   * Extract the elementary streams.
   */
  mediaIndex = 0;
  while (section_length > 0) {
    unsigned int streamInfoLength;
    
    pid = (((unsigned int)stream[1] & 0x1f) << 8) | stream[2];
    streamInfoLength = (((unsigned int)stream[3] & 0xf) << 8) | stream[4];
    codedLength = 5 + streamInfoLength;
    if (codedLength > section_length) {
      fprintf (stderr, "demux error! PMT with inconsistent streamInfo length\n");
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
	xprintf(VERBOSE|DEMUX, "PMT video pid  %04x\n", pid);
	demux_ts_pes_new(this, mediaIndex, pid, this->fifoVideo);
      }
      this->videoPid = pid;
      this->videoMedia = mediaIndex;
      break;
    case ISO_11172_AUDIO:
    case ISO_13818_AUDIO:
      if (this->audioPid == INVALID_PID) {
	xprintf(VERBOSE|DEMUX, "PMT audio pid  %04x\n", pid);
	demux_ts_pes_new(this, mediaIndex, pid, this->fifoAudio);
      }
      this->audioPid = pid;
      this->audioMedia = mediaIndex;
      break;
    default:
      xprintf(VERBOSE|DEMUX, "PMT stream_type unknown %d pid  %04x\n", stream[0], pid);
      break;
    }
    mediaIndex++;
    stream += codedLength;
    section_length -= codedLength;
  }
  
  /*
   * Get the current PCR PID.
   */
  pid = (((unsigned int)pkt[13] & 0x1f) << 8) |
    pkt[14];
  if (this->pcrPid != pid) {
    if (this->pcrPid == INVALID_PID) {
      xprintf(VERBOSE|DEMUX, "pcr pid %04x\n", pid);
    } else {
      xprintf(VERBOSE|DEMUX, "pcr pid changed %04x\n", pid);
    }
    this->pcrPid = pid;
  }
}

void correct_for_sync(demux_ts *this,uint8_t *buf) {
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
  printf("RE-Sync failed\n"); /* Sync up here */
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
  
/* transport stream packet layer */

static void demux_ts_parse_packet (demux_ts *this) {

  unsigned char *originalPkt;
  unsigned int   sync_byte;
  unsigned int   transport_error_indicator;
  unsigned int   payload_unit_start_indicator;
  unsigned int   transport_priority;
  unsigned int   pid;
  unsigned int   transport_scrambling_control;
  unsigned int   adaption_field_control;
  unsigned int   continuity_counter;
  unsigned int   data_offset;
  unsigned int   data_len;
  uint32_t	 program_count;
 
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
  adaption_field_control         = (originalPkt[3] >> 4) & 0x03;
  continuity_counter             = originalPkt[3] & 0x0f;
  
  /*
   * Discard packets that are obviously bad.
   */
  if (sync_byte != 0x47) {
    fprintf (stderr, "demux error! invalid ts sync byte %02x\n",originalPkt[0]);
    return;
  }
  if (transport_error_indicator) {
    fprintf (stderr, "demux error! transport error\n");
    return;
  }
  
  data_offset=4;
  if (adaption_field_control & 0x1) {
    /*
     * Has a payload! Calculate & check payload length.
     */
    if (adaption_field_control & 0x2) {
      /*
       * Skip adaptation header.
       */
      data_offset+=originalPkt[4]+1;
    }

    data_len = PKT_SIZE - data_offset;

    if (data_len > PKT_SIZE) {

      printf ("demux_ts: demux error! invalid payload size %d\n",data_len);

    } else {
      
      /*
       * Do the demuxing in descending order of packet frequency!
       */
      if (pid == this->videoPid ) {
        xprintf(VERBOSE|DEMUX, "Video pid = %04x\n",pid);
	demux_ts_buffer_pes (this, originalPkt+data_offset, this->videoMedia, 
			     payload_unit_start_indicator, continuity_counter, data_len);
        return;
      } else if (pid == this->audioPid) {
        xprintf(VERBOSE|DEMUX, "Audio pid = %04x\n",pid);
	demux_ts_buffer_pes (this, originalPkt+data_offset, this->audioMedia, 
			     payload_unit_start_indicator, continuity_counter, data_len);
        return;
      } else if (pid == 0) {
	demux_ts_parse_pat (this, originalPkt, originalPkt+data_offset-4, payload_unit_start_indicator);
        return;
      } else if (pid == 0x1fff) {
	xprintf(VERBOSE|DEMUX,"Null Packet\n"); 
        return;
      }
      if ((this->audioPid == INVALID_PID) && (this->videoPid == INVALID_PID)) { 
        program_count = 0;
        while ((this->program_number[program_count] != INVALID_PROGRAM) ) {
          if ( pid == this->pmt_pid[program_count] ) {
	    xprintf(VERBOSE|DEMUX,"PMT prog %04X pid %04X\n",
              this->program_number[program_count],
              this->pmt_pid[program_count]);
	    demux_ts_parse_pmt (this, originalPkt, originalPkt+data_offset-4, payload_unit_start_indicator);
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
  
  xprintf(VERBOSE|DEMUX, "demux loop finished (status: %d)\n", this->status);
  
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
  
  switch (stage) {
  case STAGE_BY_EXTENSION:
    mrl = input->get_mrl(input);
    media = strstr(mrl, "://");
    if (media) {
      fprintf (stderr, "demux %u ts_open! \n", __LINE__);
      if ((!(strncasecmp(mrl, "stdin", 5))) || (!(strncasecmp(mrl, "fifo", 4)))) {
	if(!(strncasecmp(media+3, "ts", 3))) {
	  break;
	}
	return DEMUX_CANNOT_HANDLE;
      }
      else if (strncasecmp(mrl, "file", 4)) {
	return DEMUX_CANNOT_HANDLE;
      }
    }
    ending = strrchr(mrl, '.');
    if (ending) {
      xprintf(VERBOSE|DEMUX, "demux_ts_open: ending %s of %s\n", ending, mrl);
      if ((!strcasecmp(ending, ".m2t")) || (!strcasecmp(ending, ".ts"))) {
	break;
      }
    }
    return DEMUX_CANNOT_HANDLE;
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
			   off_t start_pos, int start_time,
			   gui_get_next_mrl_cb_t next_mrl_cb,
			   gui_branched_cb_t branched_cb) {

  demux_ts *this = (demux_ts *)this_gen;
  buf_element_t *buf;
  
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

  /*
   * Now start demuxing.
   */
  pthread_create(&this->thread, NULL, demux_ts_loop, this);
}

static void demux_ts_stop(demux_plugin_t *this_gen)
{
  demux_ts *this = (demux_ts *)this_gen;
  buf_element_t *buf;
  void *p;

  printf ("demux_ts: stop...\n");

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


demux_plugin_t *init_demuxer_plugin(int iface, config_values_t *config) {

  demux_ts *this;
  int i;
  
  if (iface != 3) {
    printf("demux_ts: plugin doesn't support plugin API version %d.\n"
	   "demux_ts: this means there's a version mismatch between xine and this "
	   "demux_ts: demuxer plugin.\nInstalling current demux plugins should help.\n",
	   iface);
    return NULL;
  }

  /*
   * Initialise the generic plugin.
   */
  this = xmalloc(sizeof(*this));
  xine_debug = config->lookup_int(config, "xine_debug", 0);
  this->plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->plugin.open              = demux_ts_open;
  this->plugin.start             = demux_ts_start;
  this->plugin.stop              = demux_ts_stop;
  this->plugin.close             = demux_ts_close;
  this->plugin.get_status        = demux_ts_get_status;
  this->plugin.get_identifier    = demux_ts_get_id;
  this->plugin.get_stream_length = demux_ts_get_stream_length;
  
  /*
   * Initialise our specialised data.
   */
  for (i = 0; i < MAX_PIDS; i++)
    this->media[i].pid = INVALID_PID;
  for (i = 0; i < MAX_PMTS; i++) {
    this->program_number[i] = INVALID_PROGRAM;
    this->pmt_pid[i]= INVALID_PID;
  }
  this->programNumber = INVALID_PROGRAM;
  this->pmtPid = INVALID_PID;
  this->pcrPid = INVALID_PID;
  this->videoPid = INVALID_PID;
  this->audioPid = INVALID_PID;

  this->rate = 16000; /* FIXME */

  return (demux_plugin_t *)this;
}



