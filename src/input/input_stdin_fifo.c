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
 * $Id: input_stdin_fifo.c,v 1.29 2002/09/04 23:31:08 guenter Exp $
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

#if defined(__linux__)
#include <linux/config.h> /* Check for DEVFS */
#endif

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"
#include "net_buf_ctrl.h"

/*
#define LOG
*/

#define PREVIEW_SIZE            2200

#define DEFAULT_LOW_WATER_MARK     1
#define DEFAULT_HIGH_WATER_MARK    5

extern int errno;

typedef struct {
  input_plugin_t   input_plugin;

  xine_t          *xine;
  
  int              fh;
  char            *mrl;
  config_values_t *config;
  off_t            curpos;

  char             preview[PREVIEW_SIZE];
  off_t            preview_size;
  off_t            preview_pos;

  nbc_t           *nbc; 

  char             scratch[1025];

} stdin_input_plugin_t;

static off_t stdin_plugin_read (input_plugin_t *this_gen, 
				char *buf, off_t todo) ;

static int stdin_plugin_open(input_plugin_t *this_gen, char *mrl) {
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;
  char *filename;
  char *pfn;

#ifdef LOG
  printf ("input_stdin_fifo: trying to open '%s'...\n",
	  mrl);
#endif

  this->mrl = mrl;

  if (!strncasecmp(mrl, "stdin://", 8) 
      || !strncmp(mrl, "-", 1)) {
#if defined(CONFIG_DEVFS_FS)
    filename = "/dev/vc/stdin";
#else
    filename = "/dev/stdin";
#endif

  } else if(!strncasecmp(mrl, "fifo://", 7)) {

    if ((pfn = strrchr((mrl + 7), ':')) != NULL) {

      filename = ++pfn;

    } else {

      if (!(strncasecmp(mrl + 7, "mpeg1", 5))
	  || (!(strncasecmp(mrl + 7, "mpeg2", 5)))) {
	filename = (char *) &mrl[12];

      } else {
	filename = (char *) &mrl[7];
      }
    }
  } else {
    return 0;
  }
  
  printf("input_stdin_fifo: filename '%s'\n", filename);
  this->fh = open (filename, O_RDONLY);
  
  if(this->fh == -1) {
    printf ("stdin: failed to open '%s'\n",
	    filename);

    return 0;
  }

  /*
   * buffering control
   */

  this->nbc    = nbc_init (this->xine);

  /*
   * fill preview buffer
   */

  this->preview_size = stdin_plugin_read (&this->input_plugin, this->preview,
					  PREVIEW_SIZE);
  this->preview_pos  = 0;

  return 1;
}

static off_t stdin_plugin_read (input_plugin_t *this_gen, 
				char *buf, off_t todo) {

  stdin_input_plugin_t  *this = (stdin_input_plugin_t *) this_gen;
  off_t                  num_bytes, total_bytes;

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

#ifdef LOG
      printf ("stdin: %lld bytes from preview (which has %lld bytes)\n",
	      num_bytes, this->preview_size);
#endif

      memcpy (&buf[total_bytes], &this->preview[this->preview_pos], num_bytes);

      this->preview_pos += num_bytes;

    } else {
      num_bytes = read (this->fh, &buf[total_bytes], todo - total_bytes);
#ifdef LOG
      printf ("stdin: %lld bytes from file\n",
	      num_bytes);
#endif
    }


    if(num_bytes < 0) {

      this->curpos += total_bytes;
      return num_bytes;

    } else if (!num_bytes) {

      this->curpos += total_bytes;
      return total_bytes;
    }
    total_bytes += num_bytes;
  }

  this->curpos += total_bytes;

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

static off_t stdin_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {

  stdin_input_plugin_t  *this = (stdin_input_plugin_t *) this_gen;
  off_t dest = this->curpos;

#ifdef LOG
  printf ("stdin: seek %lld offset, %d origin...\n",
	  offset, origin);
#endif

  switch (origin) {
  case SEEK_SET:
    dest = offset;
    break;
  case SEEK_CUR:
    dest = this->curpos + offset;
    break;
  case SEEK_END:
    printf ("stdin: SEEK_END not implemented!\n");
    return this->curpos;
  default:
    printf ("stdin: unknown origin in seek!\n");
    return this->curpos;
  }

  if (this->curpos > dest) {
    printf ("stdin: cannot seek back!\n");
    return this->curpos;
  }

  while (this->curpos<dest) {

    off_t n, diff;

    diff = dest - this->curpos;

    if (diff>1024)
      diff = 1024;

    n = stdin_plugin_read (this_gen, this->scratch, diff);

    this->curpos += n;
    if (n<diff)
      return this->curpos;
  }

  return this->curpos;
}

static off_t stdin_plugin_get_length(input_plugin_t *this_gen) {

  return 0;
}

static uint32_t stdin_plugin_get_capabilities(input_plugin_t *this_gen) {
  
  return INPUT_CAP_NOCAP;
}

static uint32_t stdin_plugin_get_blocksize(input_plugin_t *this_gen) {

  return 0;
}

static off_t stdin_plugin_get_current_pos (input_plugin_t *this_gen){
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;

  return this->curpos;
}

static int stdin_plugin_eject_media(input_plugin_t *this_gen) {
  return 1;
}

static char* stdin_plugin_get_mrl (input_plugin_t *this_gen) {
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;

  return this->mrl;
}

static void stdin_plugin_close(input_plugin_t *this_gen) {
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;

  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }

  close(this->fh);
  this->fh = -1;
}

static void stdin_plugin_stop(input_plugin_t *this_gen) {
  stdin_plugin_close(this_gen);
}

static char *stdin_plugin_get_description (input_plugin_t *this_gen) {
  return _("stdin/fifo input plugin as shipped with xine");
}

static char *stdin_plugin_get_identifier(input_plugin_t *this_gen) {
  return "stdin_fifo";
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

static void stdin_plugin_dispose (input_plugin_t *this_gen ) {
  free (this_gen);
}


void *init_input_plugin (xine_t *xine, void *data) {

  stdin_input_plugin_t *this;
  config_values_t      *config;

  this       = (stdin_input_plugin_t *) xine_xmalloc(sizeof(stdin_input_plugin_t));
  config     = xine->config;
  this->xine = xine;

  this->input_plugin.interface_version = INPUT_PLUGIN_IFACE_VERSION;
  this->input_plugin.get_capabilities  = stdin_plugin_get_capabilities;
  this->input_plugin.open              = stdin_plugin_open;
  this->input_plugin.read              = stdin_plugin_read;
  this->input_plugin.read_block        = stdin_plugin_read_block;
  this->input_plugin.seek              = stdin_plugin_seek;
  this->input_plugin.get_current_pos   = stdin_plugin_get_current_pos;
  this->input_plugin.get_length        = stdin_plugin_get_length;
  this->input_plugin.get_blocksize     = stdin_plugin_get_blocksize;
  this->input_plugin.get_dir           = NULL;
  this->input_plugin.eject_media       = stdin_plugin_eject_media;
  this->input_plugin.get_mrl           = stdin_plugin_get_mrl;
  this->input_plugin.close             = stdin_plugin_close;
  this->input_plugin.stop              = stdin_plugin_stop;
  this->input_plugin.get_description   = stdin_plugin_get_description;
  this->input_plugin.get_identifier    = stdin_plugin_get_identifier;
  this->input_plugin.get_autoplay_list = NULL;
  this->input_plugin.get_optional_data = stdin_plugin_get_optional_data;
  this->input_plugin.dispose           = stdin_plugin_dispose;
  this->input_plugin.is_branch_possible= NULL;

  this->fh              = -1;
  this->mrl             = NULL;
  this->config          = config;
  this->curpos          = 0;
  this->nbc             = NULL;

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_INPUT, 8, "stdin", XINE_VERSION_CODE, NULL, init_input_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
