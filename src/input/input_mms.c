/*
 * Copyright (C) 2002 the xine project
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
 * $Id: input_mms.c,v 1.20 2002/10/20 01:15:53 guenter Exp $
 *
 * mms input plugin based on work from major mms
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "bswap.h"

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"

#include "mms.h"
#include "net_buf_ctrl.h"


extern int errno;

#if !defined(NDELAY) && defined(O_NDELAY)
#define FNDELAY O_NDELAY
#endif

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
} mms_input_class_t;

typedef struct {
  input_plugin_t   input_plugin;

  mms_t           *mms;

  char            *mrl;

  off_t            curpos;

  nbc_t           *nbc; 

  char             scratch[1025];

} mms_input_plugin_t;


static off_t mms_plugin_read (input_plugin_t *this_gen, 
                              char *buf, off_t len) {
  mms_input_plugin_t *this = (mms_input_plugin_t *) this_gen;
  off_t               n;

#ifdef LOG
  printf ("mms_plugin_read: %lld bytes ...\n",
          len);
#endif

  nbc_check_buffers (this->nbc);

  n = mms_read (this->mms, buf, len);
  this->curpos += n;

  return n;
}

static buf_element_t *mms_plugin_read_block (input_plugin_t *this_gen, 
                                             fifo_buffer_t *fifo, off_t todo) {
  /*mms_input_plugin_t   *this = (mms_input_plugin_t *) this_gen; */
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);
  int                   total_bytes;

#ifdef LOG
  printf ("mms_plugin_read_block: %lld bytes...\n",
          todo);
#endif

  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;
  
  total_bytes = mms_plugin_read (this_gen, buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->size = total_bytes;

  return buf;
}

static off_t mms_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  mms_input_plugin_t   *this = (mms_input_plugin_t *) this_gen; 
  off_t                 dest = this->curpos;

#ifdef LOG
  printf ("mms_plugin_seek: %lld offset, %d origin...\n",
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
    printf ("input_mms: SEEK_END not implemented!\n");
    return this->curpos;
  default:
    printf ("input_mms: unknown origin in seek!\n");
    return this->curpos;
  }

  if (this->curpos > dest) {
    printf ("input_mms: cannot seek back!\n");
    return this->curpos;
  }

  while (this->curpos<dest) {
    int n, diff;

    diff = dest - this->curpos;

    if (diff>1024)
      diff = 1024;

    n = mms_read (this->mms, this->scratch, diff);
    this->curpos += n;

    if (n < diff)
      return this->curpos;

  }

  return this->curpos;
}

static off_t mms_plugin_get_length (input_plugin_t *this_gen) {
  mms_input_plugin_t   *this = (mms_input_plugin_t *) this_gen; 
  off_t                 length;

  if (!this->mms)
    return 0;

  length = mms_get_length (this->mms);

#ifdef LOG
  printf ("input_mms: length is %lld\n", length);
#endif

  return length;

}

static uint32_t mms_plugin_get_capabilities (input_plugin_t *this_gen) {
  return INPUT_CAP_NOCAP;
}

static uint32_t mms_plugin_get_blocksize (input_plugin_t *this_gen) {
  return 0;
}

static off_t mms_plugin_get_current_pos (input_plugin_t *this_gen){
  mms_input_plugin_t *this = (mms_input_plugin_t *) this_gen;

  /*
  printf ("current pos is %lld\n", this->curpos);
  */

  return this->curpos;
}

static void mms_plugin_dispose (input_plugin_t *this_gen) {
  mms_input_plugin_t *this = (mms_input_plugin_t *) this_gen;

  if (this->mms) {
    mms_close (this->mms);
    this->mms = NULL;
  }

  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }
  
  if(this->mrl)
    free(this->mrl);
  
  free (this);
}

static char* mms_plugin_get_mrl (input_plugin_t *this_gen) {
  mms_input_plugin_t *this = (mms_input_plugin_t *) this_gen;

  return this->mrl;
}

static int mms_plugin_get_optional_data (input_plugin_t *this_gen, 
                                         void *data, int data_type) {
  mms_input_plugin_t *this = (mms_input_plugin_t *) this_gen;

  switch (data_type) {

  case INPUT_OPTIONAL_DATA_PREVIEW:
    return mms_peek_header (this->mms, data);
    break;
    
  default:
    return INPUT_OPTIONAL_UNSUPPORTED;
    break;

  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static input_plugin_t *open_plugin (input_class_t *cls_gen, xine_stream_t *stream, 
				    const char *data) {

  /* mms_input_class_t  *cls = (mms_input_class_t *) cls_gen; */
  mms_input_plugin_t *this;
  mms_t              *mms;
  char               *mrl = strdup(data);

#ifdef LOG
  printf ("input_mms: trying to open '%s'\n", mrl);
#endif

  mms = mms_connect (mrl);

  if (!mms) {
    free (mrl);
    return NULL;
  }

  this = (mms_input_plugin_t *) malloc (sizeof (mms_input_plugin_t));

  this->mms    = mms;
  this->mrl    = mrl; 
  this->curpos = 0;
  this->nbc    = nbc_init (stream);

  this->input_plugin.get_capabilities  = mms_plugin_get_capabilities;
  this->input_plugin.read              = mms_plugin_read;
  this->input_plugin.read_block        = mms_plugin_read_block;
  this->input_plugin.seek              = mms_plugin_seek;
  this->input_plugin.get_current_pos   = mms_plugin_get_current_pos;
  this->input_plugin.get_length        = mms_plugin_get_length;
  this->input_plugin.get_blocksize     = mms_plugin_get_blocksize;
  this->input_plugin.get_mrl           = mms_plugin_get_mrl;
  this->input_plugin.dispose           = mms_plugin_dispose;
  this->input_plugin.get_optional_data = mms_plugin_get_optional_data;
  this->input_plugin.input_class       = cls_gen;
  
  return &this->input_plugin;
}

/*
 * mms input plugin class stuff
 */

static char *mms_class_get_description (input_class_t *this_gen) {
  return _("mms streaming input plugin");
}

static char *mms_class_get_identifier (input_class_t *this_gen) {
  return "mms";
}

static void mms_class_dispose (input_class_t *this_gen) {
  mms_input_class_t  *this = (mms_input_class_t *) this_gen;

  free (this);
}

static int mms_class_eject_media (input_class_t *this_gen) {
  return 1;
}

static void *init_class (xine_t *xine, void *data) {

  mms_input_class_t  *this;

  this = (mms_input_class_t *) xine_xmalloc (sizeof (mms_input_class_t));

  this->xine   = xine;

  this->input_class.open_plugin        = open_plugin;
  this->input_class.get_identifier     = mms_class_get_identifier;
  this->input_class.get_description    = mms_class_get_description;
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = mms_class_dispose;
  this->input_class.eject_media        = mms_class_eject_media;

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_INPUT, 9, "mms", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

