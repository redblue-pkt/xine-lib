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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * Rip Input Plugin for catching streams
 *
 * It saves raw data into file as go from input plugins.
 * 
 * Usage:
 *
 * - activation:
 *     xine stream_mrl#rip:file.raw
 * 
 * - it's possible speeder saving streams in the xine without playing:
 *     xine stream_mrl#rip:file.raw;noaudio;novideo
 *
 * $Id: input_rip.c,v 1.1 2003/08/21 00:37:29 miguelfreitas Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>

/* logging */
/*
#define LOG 1
*/
#define LOG_MODULE "input_rip"
#define CLR_FAIL "\e[1;31m"
#define CLR_RST "\e[0;39m"

#include "xine_internal.h"

#define SCRATCH_SIZE 1024

typedef struct {
  input_plugin_t    input_plugin;      /* inherited structure */

  input_plugin_t   *main_input_plugin; /* original input plugin */

  xine_stream_t    *stream;
  FILE             *file;              /* destination file */

  char             *preview;           /* preview data */
  off_t             preview_size;      /* size of read preview data */
  off_t             curpos;            /* current position */
} rip_input_plugin_t;


/* 
 * read data from input plugin and write it into file
 */
static off_t rip_plugin_read(input_plugin_t *this_gen, char *buf, off_t len) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  off_t retlen, nreal, npreview;

  lprintf("reading %lld bytes\n", len);

  if (this->curpos < this->preview_size && this->preview) {
  /* get some data from preview */
    npreview = this->preview_size - this->curpos;
    if (npreview > len) npreview = len;

    lprintf(" => get %lld bytes from preview (%lld bytes)\n", npreview, this->preview_size);

    memcpy(buf, &this->preview[this->curpos], npreview);
  } else {
  /* no data from preview */
    npreview = 0;
  }

  /* really to read/catch */
  nreal = len - npreview;
  if (nreal) {
    lprintf(" => read %lld bytes from input plugin\n", nreal);
    
    /* read from main input plugin */
    retlen = this->main_input_plugin->read(this->main_input_plugin, &buf[npreview], nreal);
    lprintf("%s => returned %lld\e" CLR_RST "\n", retlen == nreal ? "" : CLR_FAIL, retlen);

    if (retlen < 0) {
        xine_log(this->stream->xine, XINE_LOG_MSG, 
          _("input_rip: reading by input plugin failed\n"));
	return -1;
    }
    
    /* write to file */
    if (retlen && this->file) {
      if (fwrite(buf + npreview, retlen, 1, this->file) != 1) {
        xine_log(this->stream->xine, XINE_LOG_MSG, 
          _("input_rip: error writing to file %lld bytes: %s\n"), 
          retlen, strerror(errno));
        return 0;
      }
    }
  } else {
    retlen = 0;
  }

  this->curpos += (npreview + retlen);

  return npreview + retlen;
}

/*
 * open catching file and slave input plugin
 */
static int rip_plugin_open(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  xine_log(this->stream->xine, XINE_LOG_MSG,
      _("input_rip: open() function should never be called\n"));
  return 0;
}

/*
 * delete seeking from capabilities,
 * set preview cap (implemented by original plugin or rip plugin)
 *
 * preview capability can be deleted after opening with some special inputs
 */
static uint32_t rip_plugin_get_capabilities(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  uint32_t caps;

  caps = this->main_input_plugin->get_capabilities(this->main_input_plugin);
  return (caps & (~INPUT_CAP_SEEKABLE)) | (this->preview ? INPUT_CAP_PREVIEW : 0);
}

/* 
 * read a block of data from input plugin and write it into file
 *
 * This rip plugin returns block unchanged from main input plugin. But special
 * case is reading over preview - it returns own allocated block.
 */
static buf_element_t *rip_plugin_read_block(input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  buf_element_t *buf;
  off_t npreview, nreal, retval;

  lprintf("read %lld bytes (block)\n", todo);

  if (!todo) return NULL;
  
  /* number of bytes from preview */
  if (this->preview && this->curpos < this->preview_size) {
    npreview = this->preview_size - this->curpos;
    if (npreview > todo) npreview = todo;
  } else {
    npreview = 0;
  }
  /* really read & catched bytes */
  nreal = todo - npreview;

  if (npreview) {
    /* new block created by rip input plugin  */
    buf = fifo->buffer_pool_alloc(fifo);
    buf->content = buf->mem;
    buf->type = BUF_DEMUX_BLOCK;
    buf->size = npreview;
    memcpy(buf->content, &this->preview[this->curpos], npreview);

    lprintf(" => read %lld bytes by rip plugin (block)\n", nreal + npreview);
    retval = rip_plugin_read(this_gen, &buf->content[npreview], nreal);
    if (retval != nreal) {
        buf->free_buffer(buf);
        return NULL;
    }

    buf->size += retval;
  } else {
    /* all data go as block from input plugin */
    lprintf(" => reading %lld bytes from input plugin (block)\n", nreal);
    
    buf = this->main_input_plugin->read_block(this->main_input_plugin, fifo, nreal);
    if (!buf) {
      lprintf(CLR_FAIL " => reading failed (block)" CLR_RST "\n");
      return NULL;
    }
  }

  this->curpos += buf->size;

  /* write the block */    
  if (buf && this->file && buf->type == BUF_DEMUX_BLOCK) {
    if (nreal > 0 && fwrite(&buf->content[npreview], nreal, 1, this->file) != 1) {
      xine_log(this->stream->xine, XINE_LOG_MSG, 
        _("input_rip: error writing to file: %s\n"), strerror(errno));
      buf->free_buffer(buf);
      return NULL;
    }
  }

  return buf;
}

/*
 * enabled only forward seeking
 */
static off_t rip_plugin_seek(input_plugin_t *this_gen, off_t offset, int origin) {
  char buffer[SCRATCH_SIZE];
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
  uint32_t blocksize;
  off_t toread, writed;

  lprintf("seek, offset %lld, origin %d (curpos %lld)\n", offset, origin, this->curpos);
  
  switch (origin) {
    case SEEK_SET: toread = offset - this->curpos; break;
    case SEEK_CUR: toread = offset; break;
    default: toread = 0;
  }

  if (toread < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, 
      "cannot seek back (%lld bytes)\n", -toread);
    return -1;
  }

  if( this_gen->get_capabilities(this_gen) & INPUT_CAP_BLOCK )
    blocksize = this_gen->get_blocksize(this_gen);
  else
    blocksize = 1;

  toread -= (toread % blocksize);

  /* read/catch by sizeof(buffer) bytes  */
  while (toread > 0) {
    if( blocksize > 1 ) {
      buf_element_t *buf;

      buf = rip_plugin_read_block(this_gen, this->stream->video_fifo, blocksize);
      if (buf) {
        writed = buf->size;
        buf->free_buffer(buf);
      } else {
        toread = 0;
	writed = 0;
      }
    } else {
      writed = rip_plugin_read(this_gen, buffer, (toread > sizeof buffer) ? (sizeof buffer) : toread);
    }

    if (writed <= 0) {
      xine_log(this->stream->xine, XINE_LOG_MSG, 
               _("input_rip: seeking failed\n"));
      break;
    }
    toread -= writed;
  }

  lprintf(" => newpos %lld\n", this->curpos);
  
  return this->curpos;
}

/*
 * return current position,
 * check values for debug build
 */
static off_t rip_plugin_get_current_pos(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;
#ifdef DEBUG
  off_t pos;

  pos = this->main_input_plugin->get_current_pos(this->main_input_plugin);
  if (pos != this->curpos) {
    lprintf("position: computed = %lld, input plugin = %lld\n", this->curpos, pos);
  }
#endif
  
  return this->curpos;
}

static off_t rip_plugin_get_length (input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_length(this->main_input_plugin);
}

static uint32_t rip_plugin_get_blocksize(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_blocksize(this->main_input_plugin);
}

static char* rip_plugin_get_mrl (input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  return this->main_input_plugin->get_mrl(this->main_input_plugin);
}

static int rip_plugin_get_optional_data (input_plugin_t *this_gen, 
					  void *data, int data_type) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  lprintf("get optional data\n");
  if (this->preview && data_type == INPUT_OPTIONAL_DATA_PREVIEW) {
    memcpy(data, this->preview, this->preview_size);
    return this->preview_size;
  } else
    return this->main_input_plugin->get_optional_data(
    	this->main_input_plugin, data, data_type);
}

/* 
 * dispose main input plugin and self 
 */
static void rip_plugin_dispose(input_plugin_t *this_gen) {
  rip_input_plugin_t *this = (rip_input_plugin_t *)this_gen;

  lprintf("rip_plugin_dispose\n");

  this->main_input_plugin->dispose(this->main_input_plugin);
  if (this->file) fclose(this->file);
  if (this->preview) free(this->preview);
  free(this);
}

/* 
 * create self instance, 
 * target file for writing stream is specified in 'data'
 */
input_plugin_t *rip_plugin_get_instance (xine_stream_t *stream, const char *filename) {
  rip_input_plugin_t *this;
  input_plugin_t *main_plugin = stream->input_plugin;

  lprintf("rip_plugin_get_instance(catch file = %s)\n", filename ? filename : "(null)");

  /* check given input plugin */
  if (!stream->input_plugin) {
    xine_log(stream->xine, XINE_LOG_MSG, 
      _("input_rip: input plugin not defined!\n"));
    return NULL;
  }

  if ( main_plugin->get_capabilities(main_plugin) & INPUT_CAP_RIP_FORBIDDEN ) {
    xine_log(stream->xine, XINE_LOG_MSG, 
      _("input_rip: ripping/caching is not permitted!\n"));
    return NULL;
  }

  if (!filename || !filename[0]) {
    xine_log(stream->xine, XINE_LOG_MSG, 
      _("input_rip: file name not given!\n"));
    return NULL;
  }

  this = (rip_input_plugin_t *)xine_xmalloc(sizeof(rip_input_plugin_t));
  this->main_input_plugin = main_plugin;
  this->stream            = stream;
  this->curpos = 0;

  if ((this->file = fopen(filename, "wb")) == NULL) {
    xine_log(this->stream->xine, XINE_LOG_MSG, 
      _("input_rip: error opening file %s: %s\n"), 
      filename, strerror(errno));
    free(this);
    return NULL;
  }

  /* fill preview memory */
  if ( main_plugin->get_capabilities(main_plugin) & INPUT_CAP_BLOCK ) {
    buf_element_t *buf;
    uint32_t blocksize;

    blocksize = main_plugin->get_blocksize(main_plugin);
    buf = main_plugin->read_block(main_plugin, stream->video_fifo, blocksize);

    this->preview_size = buf->size;
    this->preview = malloc(this->preview_size);
    memcpy(this->preview, buf->content, this->preview_size);
    
    buf->free_buffer(buf);
  } else {
    this->preview = malloc(MAX_PREVIEW_SIZE);
    this->preview_size = main_plugin->read(main_plugin, this->preview, MAX_PREVIEW_SIZE);
  }

  if (this->file && this->preview && this->preview_size) {
    if (fwrite(this->preview, this->preview_size, 1, this->file) != 1) {
      xine_log(this->stream->xine, XINE_LOG_MSG, 
        _("input_rip: error writing to file %lld bytes: %s\n"), 
        this->preview_size, strerror(errno));
      fclose(this->file);
      free(this);
      return NULL;
    }
  }


  this->input_plugin.open                = rip_plugin_open;
  this->input_plugin.get_capabilities    = rip_plugin_get_capabilities;
  this->input_plugin.read                = rip_plugin_read;
  this->input_plugin.read_block          = rip_plugin_read_block;
  this->input_plugin.seek                = rip_plugin_seek;
  this->input_plugin.get_current_pos     = rip_plugin_get_current_pos;
  this->input_plugin.get_length          = rip_plugin_get_length;
  this->input_plugin.get_blocksize       = rip_plugin_get_blocksize;
  this->input_plugin.get_mrl             = rip_plugin_get_mrl;
  this->input_plugin.get_optional_data   = rip_plugin_get_optional_data;
  this->input_plugin.dispose             = rip_plugin_dispose;
  this->input_plugin.input_class         = main_plugin->input_class;

  return &this->input_plugin;
}
