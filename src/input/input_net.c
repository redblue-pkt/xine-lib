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
 * Read from a tcp network stream over a lan (put a tweaked mp1e encoder the
 * other end and you can watch tv anywhere in the house ..)
 *
 * $Id: input_net.c,v 1.41 2002/12/27 16:47:11 miguelfreitas Exp $
 *
 * how to set up mp1e for use with this plugin:
 * 
 * use mp1 to capture the live stream, e.g.
 * mp1e -b 1200 -R 4,32 -a 0 -B 160 -v >live.mpg 
 *
 * add an extra service "xine" to /etc/services and /etc/inetd.conf, e.g.:
 * /etc/services:
 * xine       1025/tcp
 * /etc/inetd.conf:
 * xine            stream  tcp     nowait  bartscgr        /usr/sbin/tcpd /usr/bin/tail -f /home/bartscgr/Projects/inf.misc/live.mpg
 *
 * now restart inetd and you can use xine to watch the live stream, e.g.:
 * xine tcp://192.168.0.43:1025.mpg
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/time.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"
#include "net_buf_ctrl.h"

#ifdef __GNUC__
#define LOG_MSG(xine, message, args...) {                            \
    xine_log(xine, XINE_LOG_MSG, message, ##args);                   \
    printf(message, ##args);                                         \
  }
#else
#define LOG_MSG(xine, ...) {                                         \
    xine_log(xine, XINE_LOG_MSG, __VA_ARGS__);                       \
    printf(__VA_ARGS__);                                             \
  }
#endif

/*
#define LOG
*/

#define NET_BS_LEN 2324
#define PREVIEW_SIZE            2200
#define BUFSIZE                 1024

typedef struct {
  input_plugin_t   input_plugin;

  xine_stream_t          *stream;
  
  int              fh;
  char            *mrl;

  char             preview[PREVIEW_SIZE];
  off_t            preview_size;
  off_t            preview_pos;

  off_t            curpos;

  nbc_t           *nbc;

  /* scratch buffer for forward seeking */
  char             seek_buf[BUFSIZE];


} net_input_plugin_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
  config_values_t  *config;

} net_input_class_t;

/* **************************************************************** */
/*                       Private functions                          */
/* **************************************************************** */

static int host_connect_attempt(struct in_addr ia, int port, xine_t *xine) {

  int                s;
  struct sockaddr_in sin;

  s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s==-1) {
    LOG_MSG(xine, _("input_net: socket(): %s\n"), strerror(errno));
    return -1;
  }

  sin.sin_family = AF_INET;	
  sin.sin_addr   = ia;
  sin.sin_port   = htons(port);
  
  if (connect(s, (struct sockaddr *)&sin, sizeof(sin))==-1 && errno != EINPROGRESS) {
    LOG_MSG (xine, _("input_net: connect(): %s\n"), strerror(errno));
    close(s);
    return -1;
  }	

  return s;
}

static int host_connect(const char *host, int port, xine_t *xine) {
  struct hostent *h;
  int             i;
  int             s;
	
  h = gethostbyname(host);
  if (h==NULL) {
    LOG_MSG (xine, _("input_net: unable to resolve '%s'.\n"), host);
    return -1;
  }
	
  for (i=0; h->h_addr_list[i]; i++) {
    struct in_addr ia;
    memcpy (&ia, h->h_addr_list[i],4);
    s = host_connect_attempt (ia, port, xine);
    if (s != -1)
      return s;
  }

  LOG_MSG (xine, _("input_net: unable to connect to '%s'.\n"), host);
  return -1;
}

#define LOW_WATER_MARK  50
#define HIGH_WATER_MARK 100

static off_t net_plugin_read (input_plugin_t *this_gen, 
			      char *buf, off_t len) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;
  off_t n, total;

#ifdef LOG
  printf ("input_net: reading %d bytes...\n", len);
#endif

  nbc_check_buffers (this->nbc);

  total=0;
  while (total<len){

    if (this->preview_pos < this->preview_size) {
      n = this->preview_size - this->preview_pos;
      if (n > (len - total))
        n = len - total;
#ifdef LOG
      printf ("input_net: %lld bytes from preview (which has %lld bytes)\n",
	      n, this->preview_size);
#endif

      memcpy (&buf[total], &this->preview[this->preview_pos], n);
      this->preview_pos += n;
    } else
    {
      n = read (this->fh, &buf[total], len-total);
    }

#ifdef LOG
    printf ("input_net: got %lld bytes (%lld/%lld bytes read)\n",
	    n,total,len);
#endif
  
    if (n > 0){
      this->curpos += n;
      total += n;
    }
    else if (n<0 && errno!=EAGAIN) 
      return total;
  }
  return total;
}

static buf_element_t *net_plugin_read_block (input_plugin_t *this_gen, 
					     fifo_buffer_t *fifo, off_t todo) {
  net_input_plugin_t   *this = (net_input_plugin_t *) this_gen;
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);
  off_t                 total_bytes;

  nbc_check_buffers (this->nbc);
  
  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;
  
  total_bytes = net_plugin_read (this_gen, buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->size = total_bytes;

  return buf;
}

static off_t net_plugin_get_length (input_plugin_t *this_gen) {

  return 0;
}

static uint32_t net_plugin_get_capabilities (input_plugin_t *this_gen) {

  return INPUT_CAP_PREVIEW;
}

static uint32_t net_plugin_get_blocksize (input_plugin_t *this_gen) {

  return NET_BS_LEN;

}

static off_t net_plugin_get_current_pos (input_plugin_t *this_gen){
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  return this->curpos;
}

static off_t net_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  printf ("input_net: seek %lld bytes, origin %d\n",
	  offset, origin);

  /* only relative forward-seeking is implemented */

  if ((origin == SEEK_CUR) && (offset >= 0)) {

    for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
      this->curpos += net_plugin_read (this_gen, this->seek_buf, BUFSIZE);
    }

    this->curpos += net_plugin_read (this_gen, this->seek_buf, offset);
  }

  return this->curpos;
}


static char* net_plugin_get_mrl (input_plugin_t *this_gen) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  return this->mrl;
}

static int net_plugin_get_optional_data (input_plugin_t *this_gen, 
					 void *data, int data_type) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  switch (data_type) {
  case INPUT_OPTIONAL_DATA_PREVIEW:

    memcpy (data, this->preview, this->preview_size);
    return this->preview_size;

    break;
  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static void net_plugin_dispose (input_plugin_t *this_gen ) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  close(this->fh);
  this->fh = -1;
  
  free (this->mrl);
  
  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }

  free (this_gen);
}


static input_plugin_t *net_plugin_open (input_class_t *cls_gen, xine_stream_t *stream, const char *mrl) {
  /* net_input_plugin_t *this = (net_input_plugin_t *) this_gen; */
  net_input_plugin_t *this = xine_xmalloc(sizeof(net_input_plugin_t));
  char *filename;
  char *pptr;
  int port = 7658;

  this->mrl = strdup(mrl);
  this->stream = stream;

  if (!strncasecmp (mrl, "tcp://", 6)) {
    filename = (char *) &this->mrl[6];
    
    if((!filename) || (strlen(filename) == 0)) {
      free (this->mrl);
      free (this);
      return NULL;
    }
    
  } else {
    free (this->mrl);
    free (this);
    return NULL;
  }
    
  pptr=strrchr(filename, ':');
  if(pptr) {
    *pptr++ = 0;
    sscanf(pptr,"%d", &port);
  }

  this->fh     = host_connect(filename, port, this->stream->xine);
  this->curpos = 0;

  if (this->fh == -1) {
    free (this->mrl);
    free (this);
    return NULL;
  }

  this->nbc = nbc_init (this->stream);

  /*
   * fill preview buffer
   */
  this->preview_pos  = 0;
  this->preview_size  = 0;

  this->preview_size = read (this->fh, this->preview, PREVIEW_SIZE);
  
  this->preview_pos  = 0;
  this->curpos  = 0;

  this->input_plugin.get_capabilities  = net_plugin_get_capabilities;
  this->input_plugin.read              = net_plugin_read;
  this->input_plugin.read_block        = net_plugin_read_block;
  this->input_plugin.seek              = net_plugin_seek;
  this->input_plugin.get_current_pos   = net_plugin_get_current_pos;
  this->input_plugin.get_length        = net_plugin_get_length;
  this->input_plugin.get_blocksize     = net_plugin_get_blocksize;
  this->input_plugin.get_mrl           = net_plugin_get_mrl;
  this->input_plugin.get_optional_data = net_plugin_get_optional_data;
  this->input_plugin.dispose           = net_plugin_dispose;
  this->input_plugin.input_class       = cls_gen;


  return &this->input_plugin;
}


/*
 *  net plugin class
 */
 
static char *net_class_get_description (input_class_t *this_gen) {
	return _("net input plugin as shipped with xine");
}

static char *net_class_get_identifier (input_class_t *this_gen) {
  return "TCP";
}

static void net_class_dispose (input_class_t *this_gen) {
  net_input_class_t  *this = (net_input_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {

  net_input_class_t  *this;

  this         = (net_input_class_t *) xine_xmalloc(sizeof(net_input_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->input_class.open_plugin       = net_plugin_open;
  this->input_class.get_description   = net_class_get_description;
  this->input_class.get_identifier    = net_class_get_identifier;
  this->input_class.get_dir           = NULL;
  this->input_class.get_autoplay_list = NULL;
  this->input_class.dispose           = net_class_dispose;
  this->input_class.eject_media       = NULL;

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_INPUT, 11, "tcp", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

