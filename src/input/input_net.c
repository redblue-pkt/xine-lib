/*
 * Copyright (C) 2000-2018 the xine project
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Read from a tcp network stream over a lan (put a tweaked mp1e encoder the
 * other end and you can watch tv anywhere in the house ..)
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
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef ENABLE_IPV6
#include <sys/types.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef WIN32
#include <winsock.h>
#endif

#include <errno.h>
#include <sys/time.h>

#define LOG_MODULE "input_net"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>
#include <xine/io_helper.h>
#include "net_buf_ctrl.h"
#include "group_network.h"
#include "input_helper.h"

#define NET_BS_LEN 2324

typedef struct {
  input_plugin_t   input_plugin;

  xine_stream_t   *stream;

  int              fh;
  char            *mrl;
  char            *host_port;

  char             preview[MAX_PREVIEW_SIZE];
  off_t            preview_size;

  off_t            curpos;

  nbc_t           *nbc;

} net_input_plugin_t;

/* **************************************************************** */
/*                       Private functions                          */
/* **************************************************************** */

#define LOW_WATER_MARK  50
#define HIGH_WATER_MARK 100

static off_t net_plugin_read (input_plugin_t *this_gen,
			      void *buf_gen, off_t len) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;
  char *buf = (char *)buf_gen;
  off_t n, total;

  lprintf("reading %" PRIdMAX " bytes...\n", (intmax_t)len);

  if (len < 0)
    return -1;

  total=0;
  if (this->curpos < this->preview_size) {
    n = this->preview_size - this->curpos;
    if (n > (len - total))
      n = len - total;

    lprintf("%" PRIdMAX " bytes from preview (which has %" PRIdMAX " bytes)\n", (intmax_t)n, (intmax_t)this->preview_size);

    memcpy (&buf[total], &this->preview[this->curpos], n);
    this->curpos += n;
    total += n;
  }

  if( (len-total) > 0 ) {
    n = _x_read_abort (this->stream, this->fh, &buf[total], len-total);

    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "input_net: got %" PRIdMAX " bytes (%" PRIdMAX "/%" PRIdMAX " bytes read)\n", (intmax_t)n, (intmax_t)total, (intmax_t)len);

    if (n < 0) {
      _x_message(this->stream, XINE_MSG_READ_ERROR, this->host_port, NULL);
      return 0;
    }

    this->curpos += n;
    total += n;
  }
  return total;
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

  return _x_input_seek_preview(this_gen, offset, origin,
                               &this->curpos, -1, this->preview_size);
}


static const char* net_plugin_get_mrl (input_plugin_t *this_gen) {
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

  if (this->fh != -1) {
    _x_io_tcp_close(this->stream, this->fh);
    this->fh = -1;
  }

  free (this->mrl);
  free (this->host_port);

  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }

  free (this_gen);
}

static int net_plugin_open (input_plugin_t *this_gen ) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;
  const char *filename;
  char *pptr;
  int port = 7658;
  int toread = MAX_PREVIEW_SIZE;
  int trycount = 0;

  filename = this->host_port;
  pptr=strrchr(filename, ':');
  if(pptr) {
    *pptr++ = 0;
    sscanf(pptr,"%d", &port);
  }

  this->fh     = _x_io_tcp_connect(this->stream, filename, port);
  this->curpos = 0;

  if (this->fh == -1) {
    return 0;
  }

  /*
   * fill preview buffer
   */
  while ((toread > 0) && (trycount < 10)) {
#ifndef WIN32
    this->preview_size += read (this->fh, this->preview + this->preview_size, toread);
#else
    this->preview_size += recv (this->fh, this->preview + this->preview_size, toread, 0);
#endif
    trycount++;
    toread = MAX_PREVIEW_SIZE - this->preview_size;
  }

  this->curpos       = 0;

  return 1;
}

static input_plugin_t *net_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream, const char *mrl) {
  /* net_input_plugin_t *this = (net_input_plugin_t *) this_gen; */
  net_input_plugin_t *this;
  nbc_t *nbc = NULL;
  const char *filename;

  if (!strncasecmp (mrl, "tcp://", 6)) {
    filename = &mrl[6];

    if((!filename) || (strlen(filename) == 0)) {
      return NULL;
    }

    nbc = nbc_init (stream);

  } else if (!strncasecmp (mrl, "slave://", 8)) {

    filename = &mrl[8];

    if((!filename) || (strlen(filename) == 0)) {
      return NULL;
    }

    /* the only difference for slave:// is that network buffering control
     * is not used. otherwise, dvd still menus are not displayed (it freezes
     * with "buffering..." all the time)
     */

    nbc = NULL;

  } else {
    return NULL;
  }

  this = calloc(1, sizeof(net_input_plugin_t));
  this->mrl           = strdup(mrl);
  this->host_port     = strdup(filename);
  this->stream        = stream;
  this->fh            = -1;
  this->curpos        = 0;
  this->nbc           = nbc;
  this->preview_size  = 0;

  this->input_plugin.open              = net_plugin_open;
  this->input_plugin.get_capabilities  = net_plugin_get_capabilities;
  this->input_plugin.read              = net_plugin_read;
  this->input_plugin.read_block        = _x_input_default_read_block;
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

void *input_net_init_class (xine_t *xine, const void *data) {

  input_class_t *this;

  this = calloc(1, sizeof(*this));
  if (!this)
    return NULL;

  this->get_instance      = net_class_get_instance;
  this->description       = N_("net input plugin as shipped with xine");
  this->identifier        = "TCP";
  this->get_dir           = NULL;
  this->get_autoplay_list = NULL;
  this->dispose           = default_input_class_dispose;
  this->eject_media       = NULL;

  return this;
}

