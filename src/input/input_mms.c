/* 
 * Copyright (C) 2000-2002 major mms
 * 
 * This file is part of xine-mms
 * 
 * xine-mms is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * xine-mms is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * input plugin for mms network streams
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
#include "strict_scr.h"

/*
#define LOG
*/

extern int errno;

#if !defined(NDELAY) && defined(O_NDELAY)
#define	FNDELAY	O_NDELAY
#endif


typedef struct {
  input_plugin_t   input_plugin;

  xine_t          *xine;
  config_values_t *config;
  
  mms_t           *mms;

  char            *mrl;

  off_t            curpos;

  int              buffering;

  strictscr_t     *scr; 

  char             scratch[1025];

} mms_input_plugin_t;


static int mms_plugin_open (input_plugin_t *this_gen, char *mrl) {

  char* nmrl=NULL;
  mms_input_plugin_t *this = (mms_input_plugin_t *) this_gen;

  
  asx_parse(mrl,&nmrl);
  
  if(!nmrl)
    nmrl=mrl;
  
  printf("mms_plugin_open: using mrl <%s> \n", nmrl);
  if (strncasecmp (nmrl, "mms://",6))
    return 0;

  this->mrl = strdup(nmrl); /* FIXME: small memory leak */

  this->mms = mms_connect (nmrl);

  if (!this->mms)
    return 0;

  this->curpos    = 0;
  this->buffering = 0;

  /* register our scr plugin */

  this->scr->scr.start (&this->scr->scr, this->xine->metronom->get_current_time (this->xine->metronom));
  this->xine->metronom->register_scr (this->xine->metronom, &this->scr->scr); 

  return 1;
}

#define LOW_WATER_MARK  50
#define HIGH_WATER_MARK 100

static off_t mms_plugin_read (input_plugin_t *this_gen, 
			      char *buf, off_t len) {
  mms_input_plugin_t *this = (mms_input_plugin_t *) this_gen;
  off_t               n;
  int                 fifo_fill;

#ifdef LOG
  printf ("mms_plugin_read: %lld bytes ...\n",
	  len);
#endif

  fifo_fill = this->xine->video_fifo->size(this->xine->video_fifo);

  if (fifo_fill<LOW_WATER_MARK) {
    
    this->xine->metronom->set_speed (this->xine->metronom, SPEED_PAUSE);
    this->buffering = 1;
    this->scr->adjustable = 0;
    printf ("input_mms: buffering (%d/%d)...\n", fifo_fill, LOW_WATER_MARK);

  } else if ( (fifo_fill>HIGH_WATER_MARK) && (this->buffering)) {
    this->xine->metronom->set_speed (this->xine->metronom, SPEED_NORMAL);
    this->buffering = 0;
    this->scr->adjustable = 1;
    printf ("input_mms: buffering...done\n");
  }

  n = mms_read (this->mms, buf, len);
  this->curpos += n;

  return n;
}

static buf_element_t *mms_plugin_read_block (input_plugin_t *this_gen, 
					     fifo_buffer_t *fifo, off_t todo) {
  /*mms_input_plugin_t   *this = (mms_input_plugin_t *) this_gen; */
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);
  int total_bytes;

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

  off_t dest = this->curpos;

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
    if (n<diff)
      return this->curpos;
  }

  return this->curpos;
}

static off_t mms_plugin_get_length (input_plugin_t *this_gen) {

  mms_input_plugin_t   *this = (mms_input_plugin_t *) this_gen; 

  off_t length;

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
;
}

static off_t mms_plugin_get_current_pos (input_plugin_t *this_gen){
  mms_input_plugin_t *this = (mms_input_plugin_t *) this_gen;

  /*
  printf ("current pos is %lld\n", this->curpos);
  */

  return this->curpos;
}

static int mms_plugin_eject_media (input_plugin_t *this_gen) {
  return 1;
}


static void mms_plugin_close (input_plugin_t *this_gen) {
  mms_input_plugin_t *this = (mms_input_plugin_t *) this_gen;

  if (this->mms) {
    mms_close (this->mms);
    this->mms = NULL;
  }

  this->xine->metronom->unregister_scr (this->xine->metronom, &this->scr->scr); 
}

static void mms_plugin_stop (input_plugin_t *this_gen) {

  mms_plugin_close(this_gen);
}

static char *mms_plugin_get_description (input_plugin_t *this_gen) {
  return "mms input plugin";
}

static char *mms_plugin_get_identifier (input_plugin_t *this_gen) {
  return "MMS";
}

static char* mms_plugin_get_mrl (input_plugin_t *this_gen) {
  mms_input_plugin_t *this = (mms_input_plugin_t *) this_gen;

  return this->mrl;
}

static int mms_plugin_get_optional_data (input_plugin_t *this_gen, 
					 void *data, int data_type) {

  return INPUT_OPTIONAL_UNSUPPORTED;
}


input_plugin_t *init_input_plugin (int iface, xine_t *xine) {

  mms_input_plugin_t *this;
  config_values_t    *config;
  
  if (iface != 5) {
    printf ("mms input plugin doesn't support plugin API version %d.\n"
	    "PLUGIN DISABLED.\n"
	    "This means there's a version mismatch between xine and this input"
	    "plugin.\nInstalling current input plugins should help.\n",
	    iface);
    return NULL;
  }

  this       = (mms_input_plugin_t *) malloc (sizeof (mms_input_plugin_t));
  config     = xine->config;
  this->xine = xine;

  this->input_plugin.interface_version = INPUT_PLUGIN_IFACE_VERSION;
  this->input_plugin.get_capabilities  = mms_plugin_get_capabilities;
  this->input_plugin.open              = mms_plugin_open;
  this->input_plugin.read              = mms_plugin_read;
  this->input_plugin.read_block        = mms_plugin_read_block;
  this->input_plugin.seek              = mms_plugin_seek;
  this->input_plugin.get_current_pos   = mms_plugin_get_current_pos;
  this->input_plugin.get_length        = mms_plugin_get_length;
  this->input_plugin.get_blocksize     = mms_plugin_get_blocksize;
  this->input_plugin.get_dir           = NULL;
  this->input_plugin.eject_media       = mms_plugin_eject_media;
  this->input_plugin.get_mrl           = mms_plugin_get_mrl;
  this->input_plugin.close             = mms_plugin_close;
  this->input_plugin.stop              = mms_plugin_stop;
  this->input_plugin.get_description   = mms_plugin_get_description;
  this->input_plugin.get_identifier    = mms_plugin_get_identifier;
  this->input_plugin.get_autoplay_list = NULL;
  this->input_plugin.get_optional_data = mms_plugin_get_optional_data;
  this->input_plugin.is_branch_possible= NULL;

  this->mrl       = NULL;
  this->config    = config;
  this->curpos    = 0;
  this->buffering = 0;
  
  this->scr       = strictscr_init (); 
  
  return &this->input_plugin;
}
