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
 *
 * John McCutchan
 * FLAC demuxer (http://flac.sf.net)
 *
 * TODO: Skip id3v2 tags.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <stdlib.h>

#include <FLAC/seekable_stream_decoder.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "../demuxers/demux.h"

/*
#define LOG 1
*/

/* FLAC Demuxer plugin */
typedef struct demux_flac_s {
  demux_plugin_t        demux_plugin;

  xine_stream_t        *stream;
  
  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;

  input_plugin_t       *input;

  int status;

  int seek_flag;

  off_t data_start;
  off_t data_size;

  /* FLAC Stuff */
  FLAC__SeekableStreamDecoder *flac_decoder;

  uint64_t total_samples;
  uint64_t bits_per_sample;
  uint64_t channels;
  uint64_t sample_rate;
  uint64_t length_in_msec;
} demux_flac_t ;


/* FLAC Demuxer class */
typedef struct demux_flac_class_s {
  demux_class_t     demux_class;

  xine_t           *xine;
  config_values_t  *config;

} demux_flac_class_t;

/* FLAC Callbacks */
static FLAC__SeekableStreamDecoderReadStatus
flac_read_callback (const FLAC__SeekableStreamDecoder *decoder,
                    FLAC__byte buffer[],
                    unsigned *bytes,
                    void *client_data)
{
    demux_flac_t *this    = (demux_flac_t *)client_data;
    input_plugin_t *input = this->input; 
    off_t offset = *bytes;

#ifdef LOG
    printf("FLAC_DMXR: flac_read_callback\n");
#endif
    
    /* This should only be called when flac is reading the metadata
     * of the flac stream.
     */
    
    offset = input->read (input, buffer, offset);

#ifdef LOG
    printf("FLAC_DMXR: Read %lld / %u bytes into buffer\n", offset, *bytes);
#endif

    *bytes = offset;
    /* This is the way to detect EOF with xine input plugins */
    if ( (offset != *bytes) && (*bytes != 0) )
    {
#ifdef LOG
        printf("FLAC_DMXR: Marking EOF\n");
#endif
        this->status = DEMUX_FINISHED;
        return FLAC__SEEKABLE_STREAM_DECODER_READ_STATUS_ERROR;
    }
    else
    {
#ifdef LOG
        printf("FLAC_DMXR: Read was perfect\n");
#endif
        return FLAC__SEEKABLE_STREAM_DECODER_READ_STATUS_OK;
    }
}

static FLAC__SeekableStreamDecoderSeekStatus
flac_seek_callback (const FLAC__SeekableStreamDecoder *decoder,
                    FLAC__uint64 absolute_byte_offset,
                    void *client_data)
{
    input_plugin_t *input = ((demux_flac_t *)client_data)->input;
    off_t offset;

#ifdef LOG
    printf("FLAC_DMXR: flac_seek_callback\n");
#endif

    offset = input->seek (input, absolute_byte_offset, SEEK_SET);

    if (offset == -1)
        return FLAC__SEEKABLE_STREAM_DECODER_SEEK_STATUS_ERROR;
    else
        return FLAC__SEEKABLE_STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__SeekableStreamDecoderTellStatus
flac_tell_callback (const FLAC__SeekableStreamDecoder *decoder,
                    FLAC__uint64 *absolute_byte_offset,
                    void *client_data)
{
    input_plugin_t *input = ((demux_flac_t *)client_data)->input;
    off_t offset;

#ifdef LOG
    printf("FLAC_DMXR: flac_tell_callback\n");
#endif
    offset = input->get_current_pos (input);

    *absolute_byte_offset = offset;

    return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__SeekableStreamDecoderLengthStatus
flac_length_callback (const FLAC__SeekableStreamDecoder *decoder,
                     FLAC__uint64 *stream_length,
                     void *client_data)
{
    input_plugin_t *input = ((demux_flac_t *)client_data)->input;
    off_t offset;

#ifdef LOG
    printf("FLAC_DMXR: flac_length_callback\n");
#endif
    offset = input->get_length (input);

    /* FIXME, can flac handle -1 as offset ? */
    return FLAC__SEEKABLE_STREAM_DECODER_LENGTH_STATUS_OK;
}

static FLAC__bool 
flac_eof_callback (const FLAC__SeekableStreamDecoder *decoder,
                    void *client_data)
{
    demux_flac_t *this = (demux_flac_t *)client_data;
#ifdef LOG
    printf("FLAC_DMXR: flac_eof_callback\n");
#endif

    if (this->status == DEMUX_FINISHED)
    {
#ifdef LOG
        printf("FLAC_DMXR: flac_eof_callback: True!\n");
#endif
        return true;
    }
    else
    {
#ifdef LOG
        printf("FLAC_DMXR: flac_eof_callback: False!\n");
#endif
        return false;
    }
}

static FLAC__StreamDecoderWriteStatus 
flac_write_callback (const FLAC__SeekableStreamDecoder *decoder, 
                     const FLAC__Frame *frame,
                     const FLAC__int32 * const buffer[],
                     void *client_data) 
{
    /* This should never be called, all we use flac for in this demuxer
     * is seeking. We do the decoding in the decoder
     */
    
#ifdef LOG
    printf("FLAC_DMXR: Error: Write callback was called!\n");
#endif

    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
}

static void 
flac_metadata_callback (const FLAC__SeekableStreamDecoder *decoder, 
                        const FLAC__StreamMetadata *metadata, 
                        void *client_data)
{
    demux_flac_t *this = (demux_flac_t *)client_data;
  
#ifdef LOG
    printf("FLAC_DMXR: IN: Metadata callback\n");
#endif
    /* This should be called when we first look at a flac stream,
     * We get information about the stream here.
     */
     if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
#ifdef LOG
        printf("FLAC_DMXR: Got METADATA!\n");
#endif
        this->total_samples   = metadata->data.stream_info.total_samples;
        this->bits_per_sample = metadata->data.stream_info.bits_per_sample;
        this->channels        = metadata->data.stream_info.channels;
        this->sample_rate     = metadata->data.stream_info.sample_rate;
        this->length_in_msec  = (this->total_samples * 10 /
                                (this->sample_rate / 100))/1000;
     }
     return;
}

static void 
flac_error_callback (const FLAC__SeekableStreamDecoder *decoder, 
                     FLAC__StreamDecoderErrorStatus status,
                     void *client_data)
{
    /* This will be called if there is an error when flac is seeking
     * in the stream.
     */

#ifdef LOG
    printf("FLAC_DMXR: flac_error_callback\n");
    if (status == FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC)
        printf("FLAC_DMXR: Decoder lost synchronization.\n");
    else if (status == FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER)
        printf("FLAC_DMXR: Decoder encounted a corrupted frame header.\n");
    else if (status == FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH)
        printf("FLAC_DMXR: Frame's data did not match the CRC in the footer.\n");
    else
        printf("FLAC_DMXR: unknown error.\n");
#endif

    return;
}

/* FLAC Demuxer plugin */
static int 
demux_flac_send_chunk (demux_plugin_t *this_gen) {
    demux_flac_t *this = (demux_flac_t *) this_gen;
    buf_element_t *buf = NULL;
    off_t current_file_pos;
    int64_t current_pts;
    unsigned int remaining_sample_bytes = 0;

    remaining_sample_bytes = 2048;

    current_file_pos = this->input->get_current_pos (this->input) 
                        - this->data_start;

    current_pts = current_file_pos;
    current_pts *= 90000;
    if (this->sample_rate != 0)
    {
        current_pts /= this->sample_rate;
    }

    if (this->seek_flag) {
        xine_demux_control_newpts (this->stream, current_pts, 0);
        this->seek_flag = 0;
    }

    while (remaining_sample_bytes)
    {
        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type = BUF_AUDIO_FLAC;
        buf->extra_info->input_pos    = current_file_pos;
        buf->extra_info->input_length = this->data_size;
        buf->extra_info->input_time   = current_pts / 90;
        //buf->pts = current_pts;

        if (remaining_sample_bytes > buf->max_size)
            buf->size = buf->max_size;
        else
            buf->size = remaining_sample_bytes;

        remaining_sample_bytes -= buf->size;

        if (this->input->read (this->input,buf->content,buf->size)!=buf->size) {
#ifdef LOG
            printf("FLAC_DMXR: buf->size != input->read()\n");
#endif
            buf->free_buffer (buf);
            this->status = DEMUX_FINISHED;
            break;
        }

        /*
        if (!remaining_sample_bytes)
        {
            buf->decoder_flags |= BUF_FLAG_FRAME_END;
        }*/

        this->audio_fifo->put (this->audio_fifo, buf);
    }

    return this->status;
}

static void 
demux_flac_send_headers (demux_plugin_t *this_gen) {
    demux_flac_t *this = (demux_flac_t *) this_gen;

    buf_element_t *buf;

#ifdef LOG
    printf("FLAC_DMXR: demux_flac_send_headers\n");
#endif

    this->video_fifo = this->stream->video_fifo;
    this->audio_fifo = this->stream->audio_fifo;

    this->status = DEMUX_OK;

    this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
    this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] = this->channels;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] = this->sample_rate;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] = this->bits_per_sample;

    xine_demux_control_start (this->stream);

    if (this->audio_fifo) {
        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type = BUF_AUDIO_FLAC;
        buf->decoder_flags   = BUF_FLAG_HEADER;
        buf->decoder_info[0] = 0;
        buf->decoder_info[1] = this->sample_rate;
        buf->decoder_info[2] = this->bits_per_sample;
        buf->decoder_info[3] = this->channels;
        buf->size = 0;
        this->audio_fifo->put (this->audio_fifo, buf);
    }
}

static void 
demux_flac_dispose (demux_plugin_t *this_gen) {
    demux_flac_t *this = (demux_flac_t *) this_gen;

#ifdef LOG
    printf("FLAC_DMXR: demux_flac_dispose\n");
#endif
    if (this->flac_decoder)
        FLAC__seekable_stream_decoder_delete (this->flac_decoder);

    free(this);
    return;
}

static int 
demux_flac_get_status (demux_plugin_t *this_gen) {
    demux_flac_t *this = (demux_flac_t *) this_gen;

#ifdef LOG
    printf("FLAC_DMXR: demux_flac_get_status\n");
#endif

    return this->status;
}


static int 
demux_flac_seek (demux_plugin_t *this_gen, off_t start_pos, int start_time) {
    demux_flac_t *this = (demux_flac_t *) this_gen;

#ifdef LOG
    printf("FLAC_DMXR: demux_flac_seek\n");
#endif
    if (start_pos || !start_time) {
        
        this->input->seek (this->input, start_pos, SEEK_SET);
#ifdef LOG
        printf ("Seek to position: %lld\n", start_pos);
#endif
    
    } else {
      
        double distance = (double)start_time*1000.0;
        uint64_t target_sample = (uint64_t)(distance * this->total_samples);
        FLAC__bool s = false;

        if (this->length_in_msec != 0)
        {
            distance /= (double)this->length_in_msec;
        }


        s = FLAC__seekable_stream_decoder_seek_absolute (this->flac_decoder,
                                                         target_sample);

        if (s) {
#ifdef LOG
            printf ("Seek to: %d successfull!\n", start_time);
#endif
        } else
            this->status = DEMUX_FINISHED;
    }

    xine_demux_flush_engine (this->stream);

    return this->status;
}

static int 
demux_flac_get_stream_length (demux_plugin_t *this_gen) {
    demux_flac_t *this = (demux_flac_t *) this_gen; 

#ifdef LOG
    printf("FLAC_DMXR: demux_flac_get_stream_length\n");
#endif

    if (this->flac_decoder)
        return this->length_in_msec;
    else
        return 0;
}

static uint32_t 
demux_flac_get_capabilities (demux_plugin_t *this_gen) {
#ifdef LOG
    printf("FLAC_DMXR: demux_flac_get_capabilities\n");
#endif
    return DEMUX_CAP_NOCAP;
}

static int 
demux_flac_get_optional_data (demux_plugin_t *this_gen, void *data, int dtype) {
#ifdef LOG
    printf("FLAC_DMXR: demux_flac_get_optional_data\n");
#endif
    return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *
open_plugin (demux_class_t *class_gen, 
             xine_stream_t *stream, 
             input_plugin_t *input) {
    demux_flac_t *this;

#ifdef LOG
    printf("FLAC_DMXR: open_plugin\n");
#endif
#if 0
    if ((input->get_capabilities (input) & INPUT_CAP_SEEKABLE) != 0)
    {
        printf("FLAC_DMXR: Input is not seekable, will not handle.\n");
        return NULL;
    }
#endif

    switch (stream->content_detection_method) {
        case METHOD_BY_CONTENT:
        {
            uint8_t buf[4096];

            /* Seek to the beginning */
            input->seek(input, 0, SEEK_SET);


            /* FIXME: Skip id3v2 tag */
            if (input->read (input, buf, 4)) {
                input->seek(input, 0, SEEK_SET);
                /* Look for fLaC tag at the beginning of file */
                if ( (buf[0] != 'f') || (buf[1] != 'L') ||
                     (buf[2] != 'a') || (buf[3] != 'C') )
                    return NULL;
            }
            else {
		printf("demux_flac: failed reading signature.\n");
		/* FIXME: use preview buffer instead */
		return NULL;
            }
        }
        break;
        case METHOD_BY_EXTENSION: {
            char *ending, *mrl;
    
            mrl = input->get_mrl (input);
    
            ending = strrchr (mrl, '.');

            if (!ending || (strlen (ending) < 5))
                return NULL;

            if (strncasecmp (ending, ".flac", 5))
                return NULL;
        }
        break;
        case METHOD_EXPLICIT:
        break;
        default:
            return NULL;
        break;
    }

    /*
    * if we reach this point, the input has been accepted.
    */

    this         = xine_xmalloc (sizeof (demux_flac_t));
    this->stream = stream;
    this->input  = input;

    this->demux_plugin.send_headers      = demux_flac_send_headers;
    this->demux_plugin.send_chunk        = demux_flac_send_chunk;
    this->demux_plugin.seek              = demux_flac_seek;
    this->demux_plugin.dispose           = demux_flac_dispose;
    this->demux_plugin.get_status        = demux_flac_get_status;
    this->demux_plugin.get_stream_length = demux_flac_get_stream_length;
    this->demux_plugin.get_video_frame   = NULL;
    this->demux_plugin.got_video_frame_cb= NULL;
    this->demux_plugin.get_capabilities  = demux_flac_get_capabilities;
    this->demux_plugin.get_optional_data = demux_flac_get_optional_data;
    this->demux_plugin.demux_class       = class_gen;

    this->seek_flag = 0;
  

    /* Get a new FLAC decoder and hook up callbacks */
    this->flac_decoder = FLAC__seekable_stream_decoder_new();
#ifdef LOG
    printf("FLAC_DMXR: this->flac_decoder: %p\n", this->flac_decoder);
#endif
    FLAC__seekable_stream_decoder_set_md5_checking  (this->flac_decoder, false);
    FLAC__seekable_stream_decoder_set_read_callback (this->flac_decoder,
                                                     flac_read_callback);
    FLAC__seekable_stream_decoder_set_seek_callback (this->flac_decoder,
                                                     flac_seek_callback);
    FLAC__seekable_stream_decoder_set_tell_callback (this->flac_decoder,
                                                     flac_tell_callback);
    FLAC__seekable_stream_decoder_set_length_callback (this->flac_decoder,
                                                     flac_length_callback);
    FLAC__seekable_stream_decoder_set_eof_callback  (this->flac_decoder,
                                                     flac_eof_callback);
    FLAC__seekable_stream_decoder_set_metadata_callback (this->flac_decoder,
                                                     flac_metadata_callback);
    FLAC__seekable_stream_decoder_set_write_callback (this->flac_decoder,
                                                     flac_write_callback);
    FLAC__seekable_stream_decoder_set_error_callback (this->flac_decoder,
                                                     flac_error_callback);
    FLAC__seekable_stream_decoder_set_client_data    (this->flac_decoder,
                                                     this);

    FLAC__seekable_stream_decoder_init (this->flac_decoder);

    /* Get some stream info */
    this->data_size  = this->input->get_length (this->input);
    this->data_start = this->input->get_current_pos (this->input);

    /* This will cause FLAC to give us the rest of the information on
     * this flac stream
     */
    this->status = DEMUX_OK;
    FLAC__seekable_stream_decoder_process_until_end_of_metadata (this->flac_decoder);
#ifdef LOG
    printf("FLAC_DMXR: Processed file until end of metadata\n");
#endif

    return &this->demux_plugin;
}


/* FLAC Demuxer class */

static char *
get_description (demux_class_t *this_gen) {
    return "FLAC demux plugin";
}
 
static char *
get_identifier (demux_class_t *this_gen) {
    return "FLAC";
}

static char *
get_extensions (demux_class_t *this_gen) {
    return "flac";
}

static char *
get_mimetypes (demux_class_t *this_gen) {
    return "application/x-flac: flac: FLAC Audio;";
}

static void 
class_dispose (demux_class_t *this_gen) {
    demux_flac_class_t *this = (demux_flac_class_t *) this_gen;

#ifdef LOG
    printf("FLAC_DMXR: class_dispose\n");
#endif
    free (this);
}

void *
demux_flac_init_class (xine_t *xine, void *data) {

    demux_flac_class_t     *this;
  
#ifdef LOG
    printf("FLAC_DMXR: demux_flac_init_class\n");
#endif
    this         = xine_xmalloc (sizeof (demux_flac_class_t));
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
#if 0
plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 20, "flac", XINE_VERSION_CODE, NULL, demux_flac_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
#endif
