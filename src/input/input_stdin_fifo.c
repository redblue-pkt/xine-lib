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
 * $Id: input_stdin_fifo.c,v 1.46 2003/03/20 23:26:11 miguelfreitas Exp $
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

typedef struct {
  input_plugin_t   input_plugin;

  xine_stream_t   *stream;
  
  int              fh;
  char            *mrl;
  off_t            curpos;

  char             preview[MAX_PREVIEW_SIZE];
  off_t            preview_size;
  off_t            preview_pos;

  nbc_t           *nbc; 

  char             scratch[1025];

} stdin_input_plugin_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
} stdin_input_class_t;



static off_t stdin_plugin_read (input_plugin_t *this_gen, 
				char *buf, off_t todo) {

  stdin_input_plugin_t  *this = (stdin_input_plugin_t *) this_gen;
  off_t                  num_bytes, total_bytes, preview_bytes_sent = 0;

  nbc_check_buffers (this->nbc);

  total_bytes = 0;

#ifdef LOG
  printf ("stdin: read %lld bytes...\n", todo);
#endif

  while (total_bytes < todo) {

    if (this->preview_pos < this->preview_size) {

      num_bytes = this->preview_size - this->preview_pos;
      if (num_bytes > (todo - total_bytes)) 
        num_bytes = todo - total_bytes;
      preview_bytes_sent = num_bytes;

#ifdef LOG
      printf ("stdin: %lld bytes from preview (which has %lld bytes)\n",
        num_bytes, this->preview_size);
#endif

      memcpy (&buf[total_bytes], &this->preview[this->preview_pos], num_bytes);

      this->preview_pos += num_bytes;

    } else {
      fd_set rset;
      struct timeval timeout;

      while (1) {
        FD_ZERO (&rset);
        FD_SET  (this->fh, &rset);

        timeout.tv_sec  = 0;
        timeout.tv_usec = 10000;
      
        if (select (this->fh+1, &rset, NULL, NULL, &timeout) <= 0) {
          nbc_check_buffers (this->nbc);
#ifdef LOG
          printf ("stdin: timeout\n");
#endif
          /* aborts current read if action pending. otherwise xine
           * cannot be stopped when no more data is available.
           */
          if( this->stream->demux_action_pending )
            return 0;
            
        } else {
          break;
        }
      } 
      num_bytes = read (this->fh, &buf[total_bytes], todo - total_bytes);
#ifdef LOG
      printf ("stdin: %lld bytes from file\n", num_bytes);
#endif
      if (num_bytes == 0)
        return 0;
    }


    if(num_bytes < 0) {

      this->curpos += total_bytes - preview_bytes_sent;
      return num_bytes;

    } else if (!num_bytes) {

      this->curpos += total_bytes - preview_bytes_sent;
      return total_bytes;
    }
    total_bytes += num_bytes;
  }

  this->curpos += total_bytes - preview_bytes_sent;

  return total_bytes;
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
  off_t dest;
  off_t actual_curpos = stdin_plugin_get_current_pos(this_gen);

#ifdef LOG
  printf ("stdin: seek %lld offset, %d origin...\n",
	  offset, origin);
#endif

  switch (origin) {
  case SEEK_SET:
    dest = offset;
    break;
  case SEEK_CUR:
    dest = actual_curpos + offset;
    break;
  case SEEK_END:
    printf ("stdin: SEEK_END not implemented!\n");
    return this->curpos;
  default:
    printf ("stdin: unknown origin in seek!\n");
    return actual_curpos;
  }

  if (actual_curpos > dest) {
    printf ("stdin: cannot seek back! (%lld > %lld)\n", actual_curpos, dest);
    return actual_curpos;
  }

  while (actual_curpos < dest) {

    off_t n, diff;

    diff = dest - actual_curpos;

    if (diff>1024)
      diff = 1024;

    n = stdin_plugin_read (this_gen, this->scratch, diff);

    actual_curpos += n;
    if (n<diff)
      break;
  }

  if (actual_curpos < this->preview_size)
    this->preview_pos = actual_curpos;
  else {
    this->preview_pos = this->preview_size;
    this->curpos = actual_curpos;
  }

  return actual_curpos;
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

  if (this->preview_pos < this->preview_size)
    return this->preview_pos;
  else
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

  if (this->fh != STDIN_FILENO)
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


static input_plugin_t *open_plugin (input_class_t *cls_gen, xine_stream_t *stream, 
				    const char *data) {

  /* stdin_input_class_t  *cls = (stdin_input_class_t *) cls_gen; */
  stdin_input_plugin_t *this;
  char                 *mrl = strdup(data);
  int                   fh;


#ifdef LOG
  printf ("input_stdin_fifo: trying to open '%s'...\n",
	  mrl);
#endif

  if (!strncasecmp(mrl, "stdin:/", 7) 
      || !strncmp(mrl, "-", 1)) {

    fh = STDIN_FILENO;

  } else if (!strncasecmp (mrl, "fifo:", 5)) {
    char *filename;

    filename = (char *) &mrl[5];
    
    fh = open (filename, O_RDONLY);

    printf("input_stdin_fifo: filename '%s'\n", filename);
  
    if (fh == -1) {
      printf ("stdin: failed to open '%s'\n",
	      filename);
      free (mrl);
      return NULL;
    }
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

  this->curpos          = 0;
  this->mrl             = mrl;
  this->fh              = fh;

  /*
   * buffering control
   */

  this->nbc    = nbc_init (this->stream);

  /*
   * fill preview buffer
   */

  this->preview_size = stdin_plugin_read (&this->input_plugin, this->preview,
					  MAX_PREVIEW_SIZE);
  this->preview_pos  = 0;

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

  this = (stdin_input_class_t *) xine_xmalloc (sizeof (stdin_input_class_t));

  this->xine   = xine;

  this->input_class.open_plugin        = open_plugin;
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
  { PLUGIN_INPUT, 11, "stdin", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
