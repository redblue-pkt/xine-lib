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
 * $Id: demux_ts.c,v 1.3 2001/08/01 19:09:42 jcdutton Exp $
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
 *  1-Aug-2001 James Courtier-Dutton <jcdutton>
 *                              Reviewed by: n/a
 *                              TS Streams with zero PES lenght should now work.
 *
 * 30-Jul-2001 shaheedhaque     Reviewed by: n/a
 *                              PATs and PMTs seem to work.
 *
 * 29-Jul-2001 shaheedhaque     Reviewed by: n/a
 *                              Compiles!
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
    unsigned int pid;
    fifo_buffer_t *fifo;
    int type;
    buf_element_t *buf;
    int pes_buf_next;
    int pes_len;
    int pes_len_zero;
    unsigned int counter;
} demux_ts_media;

typedef struct {
    /*
     * The first field must be the "base class" for the plugin!
     */
    demux_plugin_t plugin;

    fifo_buffer_t *fifoAudio;
    fifo_buffer_t *fifoVideo;
    fifo_buffer_t *fifoSPU;

    input_plugin_t *input;

    pthread_t thread;

    int mnAudioChannel;
    int mnSPUChannel;
    int status;

    int blockSize;
    demux_ts_media media[MAX_PIDS];
    /*
     * Stuff to do with the transport header. As well as the video
     * and audio PIDs, we keep the index of the corresponding entry
     * inthe media[] array.
     */
    unsigned int programNumber;
    unsigned int pmtPid;
    unsigned int pcrPid;
    unsigned int videoPid;
    unsigned int audioPid;
    unsigned int videoMedia;
    unsigned int audioMedia;
} demux_ts;

/*
**
** PRIVATE FUNCTIONS
**
*/
static void *demux_ts_loop(
    void *gen_this);
static void demux_ts_pat_parse(
    demux_ts *this,
    unsigned char *originalPkt,
    unsigned char *pkt,
    unsigned int pus);
static void demux_ts_pes_buffer(
    demux_ts *this,
    unsigned char *ts,
    unsigned int mediaIndex,
    unsigned int pus,
    unsigned int cc,
    unsigned int len);
static void demux_ts_pes_new(
    demux_ts *this,
    unsigned int mediaIndex,
    unsigned int pid,
    fifo_buffer_t *fifo);
static void demux_ts_pmt_parse(
    demux_ts *this,
    unsigned char *originalPkt,
    unsigned char *pkt,
    unsigned int pus);
static void demux_ts_parse_ts(
    demux_ts *this);
static void demux_ts_queue_pes(
    demux_ts *this,
    buf_element_t *buf);
static void demux_ts_close(
    demux_plugin_t *gen_this);
static char *demux_ts_get_id(
    void);
static int demux_ts_get_status(
    demux_plugin_t *this_gen);
static int demux_ts_open(
    demux_plugin_t *this_gen,
    input_plugin_t *input,
    int stage);
static void demux_ts_start(
    demux_plugin_t *this_gen,
    fifo_buffer_t *fifoVideo,
    fifo_buffer_t *fifoAudio,
    off_t pos,
    gui_get_next_mrl_cb_t next_mrl_cb,
    gui_branched_cb_t branched_cb);
static void demux_ts_stop(
    demux_plugin_t *this_gen);

/*
**
** DATA
**
*/
static uint32_t xine_debug;

/*
 * Sit in a loop eating data.
 */
static void *demux_ts_loop(
    void *gen_this)
{
    demux_ts *this = (demux_ts *)gen_this;
    buf_element_t *buf;

    /*
     * TBD: why do we not appear at the beginning of the file?
     */
this->input->seek(this->input, 0, SEEK_SET);
fprintf (stderr, "demux %u demux_ts_loop seeking back to start of file! \n", __LINE__);

    do {
        demux_ts_parse_ts(this);
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

/*
 * NAME demux_ts_pat_parse
 *
 *  Parse a PAT. The PAT is expected to be exactly one section long,
 *  and that section is expected to be contained in a single TS packet.
 *
 *  The PAT is assumed to contain a single program definition, though
 *  we can cope with the stupidity of SPTSs which contain NITs.
 */
static void demux_ts_pat_parse(
    demux_ts *this,
    unsigned char *originalPkt,
    unsigned char *pkt,
    unsigned int pus)
{
    unsigned int length;
    unsigned char *program;
    unsigned int programNumber;
    unsigned int pmtPid;
    unsigned int programCount;

    /*
     * A PAT in a single section should start with a payload unit start
     * indicator set.
     */
    if (!pus) {
        fprintf (stderr, "demux error! PAT without payload unit start\n");
        return;
    }

    /*
     * PAT packets with a pus start with a pointer. Skip it!
     */
    pkt += pkt[4];
    if (pkt - originalPkt > PKT_SIZE) {
        fprintf (stderr, "demux error! PAT with invalid pointer\n");
        return;
    }
    if (!(pkt[10] & 0x01)) {
        /*
         * Not current!
         */
        return;
    }
    length = (((unsigned int)pkt[6] & 0x3) << 8) | pkt[7];
    if (pkt - originalPkt > BODY_SIZE - 1 - 3 - (int)length) {
        fprintf (stderr, "demux error! PAT with invalid section length\n");
        return;
    }
    if ((pkt[11]) || (pkt[12])) {
        fprintf (stderr, "demux error! PAT with invalid section %02x of %02x\n", pkt[11], pkt[12]);
        return;
    }

    /*
     * TBD: at this point, we should check the CRC. Its not that expensive, and
     * the consequences of getting it wrong are dire!
     */

    /*
     * Process all programs in the program loop.
     */
    programCount = 0;
    for (program = pkt + 13; program < pkt + 13 + length - 9; program += 4) {
        programNumber = ((unsigned int)program[0] << 8) | program[1];

        /*
         * Skip NITs completely.
         */
        if (!programNumber)
            continue;
        programCount++;
        pmtPid = (((unsigned int)program[2] & 0x1f) << 8) | program[3];

        /*
        * If we have yet to learn our program number, then learn it.
        */
        if (this->programNumber == INVALID_PROGRAM) {
            xprintf(VERBOSE|DEMUX, "acquiring programNumber=%u pmtPid=%04x\n", programNumber, pmtPid);
            this->programNumber = programNumber;
            this->pmtPid = pmtPid;
        } else {
            if (this->programNumber != programNumber) {
                fprintf(stderr, "demux error! MPTS: programNumber=%u pmtPid=%04x\n", programNumber, pmtPid);
            } else {
                if (this->pmtPid != pmtPid) {
                    xprintf(VERBOSE|DEMUX, "pmtPid changed %04x\n", pmtPid);
                    this->pmtPid = pmtPid;
                }
            }
        }
    }
}

/*
 * Manage a buffer for a PES stream.
 */
static void demux_ts_pes_buffer(
    demux_ts *this,
    unsigned char *ts,
    unsigned int mediaIndex,
    unsigned int pus,
    unsigned int cc,
    unsigned int len)
{
    demux_ts_media *m = &this->media[mediaIndex];
    /*
     * By checking the CC here, we avoid the need to check for the no-payload
     * case (i.e. adaptation field only) when it does not get bumped.
     */
    if (m->counter != INVALID_CC) {
        if ((m->counter & 0x0f) != cc) {
            fprintf(stderr, "dropped input packet cc = %d expected = %d\n", cc, m->counter);
	}
    }
    m->counter = cc;
    m->counter++;
    if (pus) {
        /* new PES packet */
        if (ts[0] || ts[1] || ts[2] != 1) {
            fprintf(stderr, "PUS set but no PES header (corrupt stream?)\n");
            m->buf->free_buffer(m->buf);
            m->buf = 0;
	    m->pes_len_zero=0;
            return;
        }
        if (m->buf) {
            fprintf(stderr, "PUS set but last PES not complete (corrupt stream?) %d %d %d\n",
                    m->pes_buf_next, m->pes_len, m->pes_len_zero);
            if(m->pes_len_zero) {
/*
	            fprintf(stderr,"Queuing ZERO PES %02X %02X %02X %02X %02X\n",  m->buf->mem[3], m->buf->mem[4], m->buf->mem[5], 
			(m->pes_buf_next-6)  & 0xff,
			(m->pes_buf_next-6)  >> 8 );
 */
		    m->buf->mem[5]=(m->pes_buf_next - 6 ) & 0xff;
        	    m->buf->mem[4]=(m->pes_buf_next - 6 ) >> 8;
		    demux_ts_queue_pes(this, m->buf);
          	    m->buf = 0;
		    m->pes_len_zero=0;
	    } else {
/*
            fprintf(stderr, "PUS2 set but last PES not complete (corrupt stream?) %d %d %d\n",
                    m->pes_buf_next, m->pes_len, m->pes_len_zero);
 */
            m->buf->free_buffer(m->buf);
            m->buf = 0;
            /* return; */
	    }

        }

        m->pes_len = ((ts[4] << 8) | ts[5]) ;
        if (m->pes_len) {
		m->pes_len+=6;
		m->pes_len_zero=1;
	} else {
		m->pes_len_zero=1;
        }
/*
	fprintf(stderr,"starting new pes, len = %d %d %02X\n", m->pes_len, m->pes_len_zero,ts[3]);
 */
        m->buf = m->fifo->buffer_pool_alloc(m->fifo);
        memcpy(m->buf->mem, ts, len);
        m->pes_buf_next = len;
	return;
    } else if (m->buf) {
        memcpy(m->buf->mem+m->pes_buf_next, ts, len);
        m->pes_buf_next += len;
        if( !m->pes_len_zero) {
	    if (m->pes_buf_next == m->pes_len ) {
/*
                fprintf(stderr,"Queuing PES - len = %d\n",  m->pes_len);
	        fprintf(stderr,"Queuing PES %02X\n",  m->buf->mem[3]);
 */
                demux_ts_queue_pes(this, m->buf);
                m->buf = 0;
            } else if (m->pes_buf_next > m->pes_len) {
                fprintf(stderr, "too much data read for PES (corrupt stream?)\n");
                m->buf->free_buffer(m->buf);
                m->buf = 0;
            }
	}
    } else {
        fprintf(stderr, "nowhere to buffer input (corrupt stream?)\n");
    }
}

/*
 * Create a buffer for a PES stream.
 */
static void demux_ts_pes_new(
    demux_ts *this,
    unsigned int mediaIndex,
    unsigned int pid,
    fifo_buffer_t *fifo)
{
    demux_ts_media *m = &this->media[mediaIndex];

    /* new PID seen - initialise stuff */
    m->pid = pid;
    m->fifo = fifo;
    m->buf = 0;
    m->pes_buf_next = 0;
    m->pes_len = 0;
    m->counter = INVALID_CC;
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
static void demux_ts_pmt_parse(
    demux_ts *this,
    unsigned char *originalPkt,
    unsigned char *pkt,
    unsigned int pus)
{
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
    unsigned int length;
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
    length = (((unsigned int)pkt[6] & 0x3) << 8) | pkt[7];
    if (pkt - originalPkt > BODY_SIZE - 1 - 3 - (int)length) {
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
    if (codedLength > length) {
        fprintf (stderr, "demux error! PMT with inconsistent progInfo length\n");
        return;
    }
    length -= codedLength;

    /*
     * Extract the elementary streams.
     */
    mediaIndex = 0;
    while (length > 0) {
        unsigned int streamInfoLength;

        pid = (((unsigned int)stream[1] & 0x1f) << 8) | stream[2];
        streamInfoLength = (((unsigned int)stream[3] & 0xf) << 8) | stream[4];
        codedLength = 5 + streamInfoLength;
        if (codedLength > length) {
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
                xprintf(VERBOSE|DEMUX, "video pid  %04x\n", pid);
                demux_ts_pes_new(this, mediaIndex, pid, this->fifoVideo);
            }
            this->videoPid = pid;
            this->videoMedia = mediaIndex;
	    break;
        case ISO_11172_AUDIO:
	case ISO_13818_AUDIO:
            if (this->audioPid == INVALID_PID) {
                xprintf(VERBOSE|DEMUX, "audio pid  %04x\n", pid);
                demux_ts_pes_new(this, mediaIndex, pid, this->fifoAudio);
            }
            this->audioPid = pid;
            this->audioMedia = mediaIndex;
	    break;
	default:
	    break;
        }
	mediaIndex++;
        stream += codedLength;
        length -= codedLength;
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

static void demux_ts_parse_ts(
    demux_ts *this)
{
    unsigned char originalPkt[PKT_SIZE];
    unsigned int sync_byte;
    unsigned int transport_error_indicator;
    unsigned int payload_unit_start_indicator;
    unsigned int transport_priority;
    unsigned int pid;
    unsigned int transport_scrambling_control;
    unsigned int adaption_field_control;
    unsigned int continuity_counter;
    unsigned int data_offset;
    unsigned int data_len;
	int n;


    /*
     * TBD: implement some sync checking WITH recovery.
     */
    if (this->input->read(this->input, originalPkt, PKT_SIZE) != PKT_SIZE) {
        this->status = DEMUX_FINISHED;
        return;
    }
    sync_byte=originalPkt[0];
    transport_error_indicator = (originalPkt[1]  >> 7) & 0x01;
    payload_unit_start_indicator = (originalPkt[1] >> 6) & 0x01;
    transport_priority = (originalPkt[1] >> 5) & 0x01;
    pid = ((originalPkt[1] << 8) | originalPkt[2]) & 0x1fff;
    transport_scrambling_control = (originalPkt[3] >> 6)  & 0x03;
    adaption_field_control = (originalPkt[3] >> 4) & 0x03;
    continuity_counter  = originalPkt[3] & 0x0f;

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
/*
    for(n=0;n<4;n++) {fprintf(stderr,"%02X ",originalPkt[n]);}
    fprintf(stderr," sync:%02X TE:%02X PUS:%02X TP:%02X PID:%04X TSC:%02X AFC:%02X CC:%02X\n",
	sync_byte,
	transport_error_indicator,
	payload_unit_start_indicator,
	transport_priority,
	pid,
	transport_scrambling_control,
	adaption_field_control, 
	continuity_counter ); 
 */
    
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
            fprintf (stderr, "demux error! invalid payload size %d\n",data_len);
        } else {

            /*
             * Do the demuxing in descending order of packet frequency!
             */
            if (pid == this->videoPid) {
                demux_ts_pes_buffer(this, originalPkt+data_offset, this->videoMedia, payload_unit_start_indicator, continuity_counter, data_len);
            } else if (pid == this->audioPid) {
                demux_ts_pes_buffer(this, originalPkt+data_offset, this->audioMedia, payload_unit_start_indicator, continuity_counter, data_len);
            } else if (pid == this->pmtPid) {
                demux_ts_pmt_parse(this, originalPkt, originalPkt+data_offset-4, payload_unit_start_indicator);
            } else if (pid == 0) {
                demux_ts_pat_parse(this, originalPkt, originalPkt+data_offset-4, payload_unit_start_indicator);
            } else if (pid == 0x1fff) {
		fprintf(stderr,"Null Packet\n");
	    }
        }
    }

    /*
     * Now check for PCRs. First test for an adaptation header, since
     * that is the most likely test to fail.
     */
}

static void demux_ts_queue_pes(
    demux_ts *this,
    buf_element_t *buf)
{
  unsigned char *p;
  int            bMpeg1=0;
  uint32_t       nHeaderLen;
  uint32_t       nPTS;
  uint32_t       nDTS;
  uint32_t       nPacketLen;
  uint32_t       nStreamID;

  p = buf->mem; /* len = this->blockSize; */
  /* FIXME: HACK to get the decoders working */
  buf->decoder_info[0] = 1;
  /* we should have a PES packet here */

  if (p[0] || p[1] || (p[2] != 1)) {
    fprintf (stderr, "demux error! %02x %02x %02x (should be 0x000001) \n",p[0],p[1],p[2]);
    buf->free_buffer(buf);
    return ;
  }

  nPacketLen = p[4] << 8 | p[5];
  nStreamID  = p[3];

  xprintf(VERBOSE|DEMUX, "packet stream id = %02x len = %d\n",nStreamID, nPacketLen);

  if (bMpeg1) {

    if (nStreamID == 0xBF) {
      buf->free_buffer(buf);
      return ;
    }

    p   += 6; /* nPacketLen -= 6; */

    while ((p[0] & 0x80) == 0x80) {
      p++;
      nPacketLen--;
      /* printf ("stuffing\n");*/
    }

    if ((p[0] & 0xc0) == 0x40) {
      /* STD_buffer_scale, STD_buffer_size */
      p += 2;
      nPacketLen -=2;
    }

    nPTS = 0;
    nDTS = 0;
    if ((p[0] & 0xf0) == 0x20) {
      nPTS  = (p[ 0] & 0x0E) << 29 ;
      nPTS |=  p[ 1]         << 22 ;
      nPTS |= (p[ 2] & 0xFE) << 14 ;
      nPTS |=  p[ 3]         <<  7 ;
      nPTS |= (p[ 4] & 0xFE) >>  1 ;
      p   += 5;
      nPacketLen -=5;
    } else if ((p[0] & 0xf0) == 0x30) {
      nPTS  = (p[ 0] & 0x0E) << 29 ;
      nPTS |=  p[ 1]         << 22 ;
      nPTS |= (p[ 2] & 0xFE) << 14 ;
      nPTS |=  p[ 3]         <<  7 ;
      nPTS |= (p[ 4] & 0xFE) >>  1 ;
      nDTS  = (p[ 5] & 0x0E) << 29 ;
      nDTS |=  p[ 6]         << 22 ;
      nDTS |= (p[ 7] & 0xFE) << 14 ;
      nDTS |=  p[ 8]         <<  7 ;
      nDTS |= (p[ 9] & 0xFE) >>  1 ;
      p   += 10;
      nPacketLen -= 10;
    } else {
      p++;
      nPacketLen --;
    }

  } else { /* mpeg 2 */

    if (p[7] & 0x80) { /* PTS avail */

      nPTS  = (p[ 9] & 0x0E) << 29 ;
      nPTS |=  p[10]         << 22 ;
      nPTS |= (p[11] & 0xFE) << 14 ;
      nPTS |=  p[12]         <<  7 ;
      nPTS |= (p[13] & 0xFE) >>  1 ;

    } else
      nPTS = 0;

    if (p[7] & 0x40) { /* PTS avail */

      nDTS  = (p[14] & 0x0E) << 29 ;
      nDTS |=  p[15]         << 22 ;
      nDTS |= (p[16] & 0xFE) << 14 ;
      nDTS |=  p[17]         <<  7 ;
      nDTS |= (p[18] & 0xFE) >>  1 ;

    } else
      nDTS = 0;


    nHeaderLen = p[8];

    p    += nHeaderLen + 9;
    nPacketLen -= nHeaderLen + 3;
  }

  xprintf(VERBOSE|DEMUX, "stream_id=%x len=%d pts=%d dts=%d\n", nStreamID, nPacketLen, nPTS, nDTS);

  if (nStreamID == 0xbd) {

    int nTrack,nSPUID;

    nTrack = p[0] & 0x0F; /* hack : ac3 track */

    if((p[0] & 0xE0) == 0x20) {
      nSPUID = (p[0] & 0x1f);

      xprintf(VERBOSE|DEMUX, "SPU PES packet, id 0x%03x\n",p[0] & 0x1f);

      if((this->mnSPUChannel >= 0)
         && (this->fifoSPU != NULL)
         && (nSPUID == this->mnSPUChannel)) {
        buf->content  = p+1;
        buf->size     = nPacketLen-1;
        buf->type     = BUF_SPU_PACKAGE;
        buf->PTS      = nPTS;
        buf->DTS      = nDTS ;
        buf->input_pos = this->input->seek(this->input, 0, SEEK_CUR);

        this->fifoSPU->put(this->fifoSPU, buf);
        return;
      }
    }

    if (((p[0]&0xF0) == 0x80) && (nTrack == this->mnAudioChannel)) {

      xprintf(VERBOSE|DEMUX|AC3, "ac3 PES packet, track %02x\n",nTrack);
      /* printf ( "ac3 PES packet, track %02x\n",nTrack);  */

      buf->content  = p+4;
      buf->size     = nPacketLen-4;
      buf->type     = BUF_AUDIO_AC3;
      buf->PTS      = nPTS;
      buf->DTS      = nDTS ;
      buf->input_pos = this->input->seek(this->input, 0, SEEK_CUR);

      this->fifoAudio->put(this->fifoAudio, buf);
      return ;
    } else if (((p[0]&0xf0) == 0xa0) || (nTrack == (this->mnAudioChannel-16))) {

      int pcm_offset;

      xprintf(VERBOSE|DEMUX,"LPCMacket, len : %d %02x\n",nPacketLen-4, p[0]);

      /* printf ("PCM!!!!!!!!!!!!!!!!!!!!!!!\n"); */

      for( pcm_offset=0; ++pcm_offset < nPacketLen-1 ; ){
        if ( p[pcm_offset] == 0x01 && p[pcm_offset+1] == 0x80 ) { /* START */
          pcm_offset += 2;
          break;
        }
      }

      buf->content  = p+pcm_offset;
      buf->size     = nPacketLen-pcm_offset;
      buf->type     = BUF_AUDIO_LPCM;
      buf->PTS      = nPTS;
      buf->DTS      = nDTS ;
      buf->input_pos = this->input->seek(this->input, 0, SEEK_CUR);

      this->fifoAudio->put(this->fifoAudio, buf);
      return ;
    }

  } else if ((nStreamID >= 0xbc) && ((nStreamID & 0xf0) == 0xe0)) {

    xprintf(VERBOSE|DEMUX, "video %X\n", nStreamID);

    buf->content = p;
    buf->size    = nPacketLen;
    buf->type    = BUF_VIDEO_MPEG;
    buf->PTS     = nPTS;
    buf->DTS     = nDTS;

    this->fifoVideo->put(this->fifoVideo, buf);
    return ;

  }  else if ((nStreamID & 0xe0) == 0xc0) {
    int nTrack;

    nTrack = nStreamID & 0x1f;

    xprintf(VERBOSE|DEMUX|MPEG, "mpg audio #%d\n", nTrack);
    xprintf(VERBOSE|DEMUX|MPEG, "bMpeg1=%d this->mnAudioChannel %d\n", bMpeg1, this->mnAudioChannel);

//    if ((bMpeg1 && (nTrack == this->mnAudioChannel))
//        || (!bMpeg1 && (nTrack == (this->mnAudioChannel-8)))) {

      buf->content  = p;
      buf->size     = nPacketLen;
      buf->type     = BUF_AUDIO_MPEG;
      buf->PTS      = nPTS;
      buf->DTS      = nDTS;
      buf->input_pos = this->input->seek(this->input, 0, SEEK_CUR);

      this->fifoAudio->put(this->fifoAudio, buf);
      return ;
//    }

  } else {
    xprintf(VERBOSE | DEMUX, "unknown packet, id = %x\n",nStreamID);
  }

  buf->free_buffer(buf);
  return ;
}

static void demux_ts_close(
    demux_plugin_t *gen_this)
{
    /* nothing */
}

static char *demux_ts_get_id(
    void)
{
    return "MPEG_TS";
}

static int demux_ts_get_status(
    demux_plugin_t *this_gen)
{
    demux_ts *this = (demux_ts *)this_gen;
    return this->status;
}

static int demux_ts_open(
    demux_plugin_t *this_gen,
    input_plugin_t *input,
    int stage)
{
    demux_ts *this = (demux_ts *) this_gen;
    char *mrl;
    char *media;
    char *ending;

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

static void demux_ts_start(
    demux_plugin_t *this_gen,
    fifo_buffer_t *fifoVideo,
    fifo_buffer_t *fifoAudio,
    off_t pos,
    gui_get_next_mrl_cb_t next_mrl_cb,
    gui_branched_cb_t branched_cb)
{
    demux_ts *this = (demux_ts *)this_gen;
    buf_element_t *buf;

    this->fifoVideo = fifoVideo;
    this->fifoAudio = fifoAudio;

    /*
     * Send reset buffer.
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
    if (this->input->get_blocksize(this->input))
        this->blockSize = this->input->get_blocksize(this->input);
    pos /= (off_t)this->blockSize;
    pos *= (off_t)this->blockSize;

    /*
     * Now start demuxing.
     */
    pthread_create(&this->thread, NULL, demux_ts_loop, this);
}

static void demux_ts_stop(
    demux_plugin_t *this_gen)
{
  demux_ts *this = (demux_ts *)this_gen;
  void *p;
  buf_element_t *buf;

  printf ("demux_ts: stop...\n");

  if (this->status != DEMUX_OK) {

    this->fifoVideo->clear(this->fifoVideo);
    if(this->fifoAudio)
      this->fifoAudio->clear(this->fifoAudio);
    return;
  }

  this->status = DEMUX_FINISHED;

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

demux_plugin_t *init_demuxer_plugin(
    int iface,
    config_values_t *config)
{
    demux_ts *this;
    int i;

    if (iface != 2) {
        printf(
            "demux_ts: plugin doesn't support plugin API version %d.\n"
            "demux_ts: this means there's a version mismatch between xine and this "
            "demux_ts: demuxer plugin.\nInstalling current input plugins should help.\n",
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

    /*
     * Initialise our specialised data.
     */
    this->mnSPUChannel = -1; /* Turn off SPU by default */
    for (i = 0; i < MAX_PIDS; i++)
        this->media[i].pid = INVALID_PID;
    this->programNumber = INVALID_PROGRAM;
    this->pmtPid = INVALID_PID;
    this->pcrPid = INVALID_PID;
    this->videoPid = INVALID_PID;
    this->audioPid = INVALID_PID;
    return (demux_plugin_t *)this;
}
