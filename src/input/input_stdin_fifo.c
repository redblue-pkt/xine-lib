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
 * $Id: input_stdin_fifo.c,v 1.48 2003/04/13 17:31:40 miguelfreitas Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"
#include "net_buf_ctrl.h"

/*
#define LOG
*/

#define BUFSIZE                 1024

typedef struct {
  input_plugin_t   input_plugin;

  xine_stream_t   *stream;

  int              fh;
  char            *mrl;
  off_t            curpos;

  char             preview[MAX_PREVIEW_SIZE];
  off_t            preview_size;

  nbc_t           *nbc;

  char             seek_buf[BUFSIZE];

} stdin_input_plugin_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
} stdin_input_class_t;



static off_t stdin_plugin_read (input_plugin_t *this_gen, 
				char *buf, off_t len) {

  stdin_input_plugin_t  *this = (stdin_input_plugin_t *) this_gen;
  off_t n, total;

#ifdef LOG
  printf ("stdin: reading %lld bytes...\n", len);
#endif

  total=0;
  if (this->curpos < this->preview_size) {
    n = this->preview_size - this->curpos;
    if (n > (len - total))
      n = len - total;
#ifdef LOG
    printf ("stdin: %lld bytes from preview (which has %lld bytes)\n",
            n, this->preview_size);
#endif

    memcpy (&buf[total], &this->preview[this->curpos], n);
    this->curpos += n;
    total += n;
  }

  if( (len-total) > 0 ) {
    n = xine_read_abort (this->stream, this->fh, &buf[total], len-total);

#ifdef LOG
    printf ("stdin: got %lld bytes (%lld/%lld bytes read)\n",
	    n,total,len);
#endif
  
    if (n < 0) {
      xine_message(this->stream, XINE_MSG_READ_ERROR, NULL);
      return 0;
    }

    this->curpos += n;
    total += n;
  }
  return total;
}

static buf_element_t *stdin_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, 
					       off_t todo) {

  off_t                 total_bytes;
  /* stdin_input_plugin_t  *this = (stdin_input_plugin_t *) this_gen; */
  buf_element_t         *buf = fifo->buffer_pool_alloc (fifo);

  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  total_bytes = stdin_plugin_read (this_gen, buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->size = total_bytes;

  return buf;
}

/* forward reference */
static off_t stdin_plugin_get_current_pos (input_plugin_t *this_gen);

static off_t stdin_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {

  stdin_input_plugin_t  *this = (stdin_input_plugin_t *) this_gen;

#ifdef LOG
  printf ("stdin: seek %lld offset, %d origin...\n",
	  offset, origin);
#endif

  if ((origin == SEEK_CUR) && (offset >= 0)) {

    for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
      if( !this_gen->read (this_gen, this->seek_buf, BUFSIZE) )
        return this->curpos;
    }

    this_gen->read (this_gen, this->seek_buf, offset);
  }

  if (origin == SEEK_SET) {

    if (offset < this->curpos) {

      if( this->curpos <= this->preview_size ) 
        this->curpos = offset;
      else
        printf ("stdin: cannot seek back! (%lld > %lld)\n", this->curpos, offset);

    } else {
      offset -= this->curpos;

      for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
        if( !this_gen->read (this_gen, this->seek_buf, BUFSIZE) )
          return this->curpos;
      }

      this_gen->read (this_gen, this->seek_buf, offset);
    }
  }

  return this->curpos;
}

static off_t stdin_plugin_get_length(input_plugin_t *this_gen) {

  return 0;
}

static uint32_t stdin_plugin_get_capabilities(input_plugin_t *this_gen) {
  
  return INPUT_CAP_PREVIEW;
}

static uint32_t stdin_plugin_get_blocksize(input_plugin_t *this_gen) {

  return 0;
}

static off_t stdin_plugin_get_current_pos (input_plugin_t *this_gen){
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;

  return this->curpos;
}

static char* stdin_plugin_get_mrl (input_plugin_t *this_gen) {
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;

  return this->mrl;
}

static void stdin_plugin_dispose (input_plugin_t *this_gen ) {
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;

  if (this->nbc) 
    nbc_close (this->nbc);

  if ((this->fh != STDIN_FILENO) && (this->fh != -1))
    close(this->fh);
  
  free (this->mrl);
  free (this);
}

static int stdin_plugin_get_optional_data (input_plugin_t *this_gen, 
					   void *data, int data_type) {
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;

  switch (data_type) {
  case INPUT_OPTIONAL_DATA_PREVIEW:

    memcpy (data, this->preview, this->preview_size);
    return this->preview_size;

    break;
  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static int stdin_plugin_open (input_plugin_t *this_gen ) {
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;

#ifdef LOG
  printf ("input_stdin_fifo: trying to open '%s'...\n",
	  this->mrl);
#endif

  if (this->fh == -1) {
    char *filename;

    filename = (char *) &this->mrl[5];
    this->fh = open (filename, O_RDONLY);

#ifdef LOG
    printf("input_stdin_fifo: filename '%s'\n", filename);
#endif

    if (this->fh == -1) {
      printf ("stdin: failed to open '%s'\n",
	      filename);
      return 0;
    }
  }


  /*
   * mrl accepted and opened successfully at this point
   *
   * => create plugin instance
   */

  /*
   * fill preview buffer
   */

  this->preview_size = stdin_plugin_read (&this->input_plugin, this->preview,
					  MAX_PREVIEW_SIZE);
  this->curpos          = 0;

  return 1;
}


static input_plugin_t *stdin_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream,
				    const char *data) {

  /* stdin_input_class_t  *cls = (stdin_input_class_t *) cls_gen; */
  stdin_input_plugin_t *this;
  char                 *mrl = strdup(data);
  int                   fh;


  if (!strncasecmp(mrl, "stdin:/", 7)
      || !strncmp(mrl, "-", 1)) {

    fh = STDIN_FILENO;

  } else if (!strncasecmp (mrl, "fifo:", 5)) {
    char *filename;

    filename = (char *) &mrl[5];
    fh = -1;
#ifdef LOG
    printf("input_stdin_fifo: filename '%s'\n", filename);
#endif
  } else {
    free (mrl);
    return NULL;
  }


  /*
   * mrl accepted and opened successfully at this point
   *
   * => create plugin instance
   */

  this       = (stdin_input_plugin_t *) xine_xmalloc(sizeof(stdin_input_plugin_t));

  this->stream = stream;
  this->curpos = 0;
  this->mrl    = mrl;
  this->fh     = fh;

  this->input_plugin.open              = stdin_plugin_open;
  this->input_plugin.get_capabilities  = stdin_plugin_get_capabilities;
  this->input_plugin.read              = stdin_plugin_read;
  this->input_plugin.read_block        = stdin_plugin_read_block;
  this->input_plugin.seek              = stdin_plugin_seek;
  this->input_plugin.get_current_pos   = stdin_plugin_get_current_pos;
  this->input_plugin.get_length        = stdin_plugin_get_length;
  this->input_plugin.get_blocksize     = stdin_plugin_get_blocksize;
  this->input_plugin.get_mrl           = stdin_plugin_get_mrl;
  this->input_plugin.dispose           = stdin_plugin_dispose;
  this->input_plugin.get_optional_data = stdin_plugin_get_optional_data;
  this->input_plugin.input_class       = cls_gen;

  /*
   * buffering control
   */
  this->nbc    = nbc_init (this->stream);

  return &this->input_plugin;
}

/*
 * stdin input plugin class stuff
 */

static char *stdin_class_get_description (input_class_t *this_gen) {
  return _("stdin streaming input plugin");
}

static char *stdin_class_get_identifier (input_class_t *this_gen) {
  return "stdin_fifo";
}

static void stdin_class_dispose (input_class_t *this_gen) {
  stdin_input_class_t  *this = (stdin_input_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {

  stdin_input_class_t  *this;

  printf ("input_stdin_fifo: init_class\n");
  this = (stdin_input_class_t *) xine_xmalloc (sizeof (stdin_input_class_t));

  this->xine   = xine;

  this->input_class.get_instance       = stdin_class_get_instance;
  this->input_class.get_identifier     = stdin_class_get_identifier;
  this->input_class.get_description    = stdin_class_get_description;
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = stdin_class_dispose;
  this->input_class.eject_media        = NULL;

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT, 12, "stdin", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
