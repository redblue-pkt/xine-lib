/* 
 * Copyright (C) 2000-2001 the xine project
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
 * input plugin for http network streams
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
#include "monitor.h"
#include "input_plugin.h"

#define BUFSIZE 1024

static uint32_t xine_debug;

typedef struct {
  input_plugin_t   input_plugin;
  
  int              fh;
  char            *mrl;
  config_values_t *config;

  off_t            curpos;
  
  char             buf[BUFSIZE];              
  char             mrlbuf[BUFSIZE];              

} http_input_plugin_t;


static int host_connect_attempt(struct in_addr ia, int port) {

  int                s;
  struct sockaddr_in sin;
	
  s=socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (s==-1) {
    printf ("input_http: failed to open socket\n");
    return -1;
  }

  sin.sin_family = AF_INET;	
  sin.sin_addr   = ia;
  sin.sin_port   = htons(port);
	
  if (connect(s, (struct sockaddr *)&sin, sizeof(sin))==-1 && errno != EINPROGRESS) {
    printf ("input_http: cannot connect to host\n");
    close(s);
    return -1;
  }	
	
  return s;
}

static int host_connect(const char *host, int port) {
  struct hostent *h;
  int i;
  int s;
	
  h=gethostbyname(host);
  if (h==NULL) {
    printf ("input_http: unable to resolve >%s<\n", host);
    return -1;
  }
	
  for(i=0; h->h_addr_list[i]; i++) {
    struct in_addr ia;
    memcpy(&ia, h->h_addr_list[i],4);
    s=host_connect_attempt(ia, port);
    if(s != -1)
      return s;
  }

  printf ("http: unable to connect to >%s<\n", host);
  return -1;
}

static int http_plugin_open (input_plugin_t *this_gen, char *mrl) {

  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  char   *filename, *host;
  char   *pstr;
  int     port = 80;
  int     done,len;

  strncpy(this->mrlbuf, mrl, 1024);
  this->mrl = mrl;

  /* parse url */

  if (!strncasecmp (this->mrlbuf, "http:",5))
    host = (char *) &this->mrlbuf[7];
  else
    return 0;

  if (! (filename = strchr (host, '/')) )
    return 0;

  *filename=0;
  filename++;
    
  printf ("input_http: opening >%s< on host >%s<\n", filename, host);
  
  pstr=strrchr(host, ':');
  if (pstr) {
    *pstr++=0;
    sscanf(pstr,"%d", &port);
  }

  this->fh = host_connect(host, port);
  this->curpos = 0;

  if (this->fh == -1) {
    return 0;
  }

  sprintf (this->buf, "GET http://%s:%d/%s  HTTP/1.0\r\n\r\n", host,port,filename);
  if (write (this->fh, this->buf, strlen(this->buf)) != strlen(this->buf)) {
    printf ("input_http: couldn't send request\n");
    return 0 ;
  }

  printf ("input_http: request sent: >%s<\n",
	  this->buf);

  /* read and parse reply */
  done = 0; len = 0;

  while (!done) {

    /*
    printf ("input_http: read...\n");
    */

    switch (read (this->fh, &this->buf[len], 1)) {
    case -1:
      return 0;
    case 0:
      continue;
    }

    if (this->buf[len] == '\n') {

      this->buf[len] = 0;

      printf ("input_http: answer: >%s<\n", this->buf);

      if (len == 1)
	done = 1;
      else
	len = 0;
    } else
      len ++;
  }

  return 1;
}

static off_t http_plugin_read (input_plugin_t *this_gen, 
			       char *buf, off_t nlen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  off_t n, num_bytes;

  num_bytes = 0;

  while (num_bytes < nlen) {

    n = read (this->fh, buf, nlen);
    
    if (n<0) {
      printf ("input_http: read error (%s)\n", strerror (errno));
      return num_bytes;
    } else if (n > 0) {
      num_bytes += n;
      this->curpos += n;
    }
  }
  return num_bytes;
}

/*
 * helper function to release buffer
 * in case demux thread is cancelled
 */
static void pool_release_buffer (void *arg) {
  buf_element_t *buf = (buf_element_t *) arg;
  if( buf != NULL )
    buf->free_buffer(buf);
}

static buf_element_t *http_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {

  off_t                 num_bytes, total_bytes;
  http_input_plugin_t  *this = (http_input_plugin_t *) this_gen;
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
  pthread_cleanup_push( pool_release_buffer, buf );

  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;
  total_bytes = 0;

  while (total_bytes < todo) {
    pthread_testcancel();
    num_bytes = read (this->fh, buf->mem + total_bytes, todo-total_bytes);
    if (num_bytes < 0) {
      printf ("input_http: read error (%s)\n", strerror (errno));
      buf->free_buffer (buf);
      buf = NULL;
      break;
    }
    total_bytes += num_bytes;
  }

  if (buf != NULL)
    buf->size = total_bytes;

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL);
  pthread_cleanup_pop(0);

  return buf;
}

static off_t http_plugin_get_length (input_plugin_t *this_gen) {

  return 0;
}

static uint32_t http_plugin_get_capabilities (input_plugin_t *this_gen) {

  return INPUT_CAP_NOCAP;
}

static uint32_t http_plugin_get_blocksize (input_plugin_t *this_gen) {

  return 0;
}

static off_t http_plugin_get_current_pos (input_plugin_t *this_gen){
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  return this->curpos;
}

static int http_plugin_eject_media (input_plugin_t *this_gen) {
  return 1;
}

static void http_plugin_close (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  close(this->fh);
  this->fh = -1;
}

static void http_plugin_stop (input_plugin_t *this_gen) {

  http_plugin_close(this_gen);
}

static char *http_plugin_get_description (input_plugin_t *this_gen) {
  return "http network stream input plugin";
}

static char *http_plugin_get_identifier (input_plugin_t *this_gen) {
  return "HTTP";
}

static char* http_plugin_get_mrl (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  return this->mrl;
}

static int http_plugin_get_optional_data (input_plugin_t *this_gen, 
					  void *data, int data_type) {

  return INPUT_OPTIONAL_UNSUPPORTED;
}

input_plugin_t *init_input_plugin (int iface, xine_t *xine) {

  http_input_plugin_t *this;
  config_values_t    *config;

  if (iface != 5) {
    printf("http input plugin doesn't support plugin API version %d.\n"
	   "PLUGIN DISABLED.\n"
	   "This means there's a version mismatch between xine and this input"
	   "plugin.\nInstalling current input plugins should help.\n",
	   iface);
    return NULL;
  }

  this       = (http_input_plugin_t *) xmalloc(sizeof(http_input_plugin_t));
  config     = xine->config;
  xine_debug = config->lookup_int (config, "xine_debug", 0);

  this->input_plugin.interface_version = INPUT_PLUGIN_IFACE_VERSION;
  this->input_plugin.get_capabilities  = http_plugin_get_capabilities;
  this->input_plugin.open              = http_plugin_open;
  this->input_plugin.read              = http_plugin_read;
  this->input_plugin.read_block        = http_plugin_read_block;
  this->input_plugin.seek              = NULL;
  this->input_plugin.get_current_pos   = http_plugin_get_current_pos;
  this->input_plugin.get_length        = http_plugin_get_length;
  this->input_plugin.get_blocksize     = http_plugin_get_blocksize;
  this->input_plugin.get_dir           = NULL;
  this->input_plugin.eject_media       = http_plugin_eject_media;
  this->input_plugin.get_mrl           = http_plugin_get_mrl;
  this->input_plugin.close             = http_plugin_close;
  this->input_plugin.stop              = http_plugin_stop;
  this->input_plugin.get_description   = http_plugin_get_description;
  this->input_plugin.get_identifier    = http_plugin_get_identifier;
  this->input_plugin.get_autoplay_list = NULL;
  this->input_plugin.get_optional_data = http_plugin_get_optional_data;
  this->input_plugin.is_branch_possible= NULL;

  this->fh      = -1;
  this->config  = config;
  this->curpos  = 0;
  
  return (input_plugin_t *) this;
}
