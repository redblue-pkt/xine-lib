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
#include "xineutils.h"
#include "input_plugin.h"
#include "net_buf_ctrl.h"

extern int errno;

#define PREVIEW_SIZE            2200
#define BUFSIZE 1024

#define DEFAULT_HTTP_PORT 80

typedef struct {
  input_plugin_t   input_plugin;

  xine_t          *xine;
  
  int              fh;
  char            *mrl;
  config_values_t *config;

  nbc_t           *nbc; 

  off_t            curpos;
  off_t            contentlength;
    
  char             buf[BUFSIZE];
  char             mrlbuf[BUFSIZE];
  char             proxybuf[BUFSIZE];

  char             auth[BUFSIZE];
  char             proxyauth[BUFSIZE];
  
  char            *user;
  char            *password;
  char            *host;
  int              port;
  char            *filename;
  
  char            *proxyuser;
  char            *proxypassword;
  char            *proxyhost;
  int              proxyport;

  char             preview[PREVIEW_SIZE];
  off_t            preview_size;
  off_t            preview_pos;

} http_input_plugin_t;


static int http_plugin_host_connect_attempt(struct in_addr ia, int port, xine_t *xine) {

  int                s;
  struct sockaddr_in sin;
	
  s=socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (s==-1) {
    xine_log (xine, XINE_LOG_MSG, _("input_http: failed to open socket\n"));
    return -1;
  }

  sin.sin_family = AF_INET;	
  sin.sin_addr   = ia;
  sin.sin_port   = htons(port);
	
  if (connect(s, (struct sockaddr *)&sin, sizeof(sin))==-1 && errno != EINPROGRESS) {
    xine_log (xine, XINE_LOG_MSG, _("input_http: cannot connect to host\n"));
    close(s);
    return -1;
  }	
	
  return s;
}

static int http_plugin_host_connect(const char *host, int port, xine_t *xine) {
  struct hostent *h;
  int i;
  int s;
	
  h=gethostbyname(host);
  if (h==NULL) {
    xine_log (xine, XINE_LOG_MSG, _("input_http: unable to resolve >%s<\n"), host);
    return -1;
  }
	
  for(i=0; h->h_addr_list[i]; i++) {
    struct in_addr ia;
    memcpy(&ia, h->h_addr_list[i], 4);
    s=http_plugin_host_connect_attempt(ia, port, xine);
    if(s != -1)
      return s;
  }

  xine_log (xine, XINE_LOG_MSG, _("http: unable to connect to >%s<\n"), host);
  return -1;
}

static int http_plugin_parse_url (char *urlbuf, char **user, char **password,
    char** host, int *port, char **filename) {
  char   *start = NULL;
  char   *authcolon = NULL;
  char	 *at = NULL;
  char	 *portcolon = NULL;
  char   *slash = NULL;
  
  if (user != NULL)
    *user = NULL;
  
  if (password != NULL)
    *password = NULL;
  
  if (host != NULL)
    *host = NULL;
  
  if (filename != NULL)
    *filename = NULL;
  
  if (port != NULL)
    *port = 0;
  
  start = strstr(urlbuf, "://");
  if (start != NULL)
    start += 3;
  else
    start = urlbuf;
  
  at = strchr(start, '@');
  slash = strchr(start, '/');
  
  if (at != NULL && slash != NULL && at > slash)
    at = NULL;
  
  if (at != NULL)
  {
    authcolon = strchr(start, ':');
    if(authcolon != NULL && authcolon > at)
      authcolon = NULL;
    
    portcolon = strchr(at, ':');
  } else
    portcolon = strchr(start, ':');
  
  if (portcolon != NULL && slash != NULL && portcolon > slash)
    portcolon = NULL;
  
  if (at != NULL)
  {
    *at = '\0';
    
    if (user != NULL)
      *user = start;
    
    if (authcolon != NULL)
    {
      *authcolon = '\0';
      
      if (password != NULL)
      	*password = authcolon + 1;
    }
    
    if (host != NULL)
      *host = at + 1;
  } else
    if (host != NULL)
      *host = start;
  
  if (slash != 0)
  {
    *slash = '\0';
    
    if (filename != NULL)
      *filename = slash + 1;
  } else
    *filename = urlbuf + strlen(urlbuf);
  
  if (portcolon != NULL)
  {
    *portcolon = '\0';
    
    if (port != NULL)
      *port = atoi(portcolon + 1);
  }
  
  return 0;
}

static int http_plugin_basicauth (const char *user, const char *password,
    char* dest, int len) {
  static char *enctable="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
  char        *tmp;
  char        *sptr;
  char        *dptr;
  int          totlen;
  int          enclen;
  int          count;
  
  totlen = strlen (user) + 1;
  if(password != NULL)
    totlen += strlen (password);
  
  enclen = ((totlen + 2) / 3 ) * 4 + 1;
  
  if (len < enclen)
    return -1;
  
  tmp = malloc (sizeof(char) * (totlen + 1));
  strcpy (tmp, user);
  strcat (tmp, ":");
  if (password != NULL)
    strcat (tmp, password);  
  
  count = strlen(tmp);
  sptr = tmp;
  dptr = dest;
  while (count >= 3) {
    dptr[0] = enctable[(sptr[0] & 0xFC) >> 2];
    dptr[1] = enctable[((sptr[0] & 0x3) << 4) | ((sptr[1] & 0xF0) >> 4)];
    dptr[2] = enctable[((sptr[1] & 0x0F) << 2) | ((sptr[2] & 0xC0) >> 6)];
    dptr[3] = enctable[sptr[2] & 0x3F];
    count -= 3;
    sptr += 3;
    dptr += 4;
  }
  
  if (count > 0) {
    dptr[0] = enctable[(sptr[0] & 0xFC) >> 2];
    dptr[1] = enctable[(sptr[0] & 0x3) << 4];
    dptr[2] = '=';
    
    if (count > 1) {
      dptr[1] = enctable[((sptr[0] & 0x3) << 4) | ((sptr[1] & 0xF0) >> 4)];
      dptr[2] = enctable[(sptr[1] & 0x0F) << 2];
    }
    
    dptr[3] = '=';
    dptr += 4;
  }
  
  dptr[0] = '\0';
  
  free(tmp);
  return 0;
}

static off_t http_plugin_read (input_plugin_t *this_gen, 
			       char *buf, off_t nlen) ;

static int http_plugin_open (input_plugin_t *this_gen, char *mrl) {

  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  char	 *proxy;
  int     done,len,linenum;

  strncpy (this->mrlbuf, mrl, BUFSIZE);
  this->mrl = mrl;

  if (strncasecmp (this->mrlbuf, "http://", 7))
    return 0;

  this->proxybuf[0] = '\0';
  proxy = getenv("http_proxy");
  
  if (proxy != NULL)
  {
    strncpy(this->proxybuf, proxy, BUFSIZE);
    
    if (http_plugin_parse_url (this->proxybuf, &this->proxyuser,
	&this->proxypassword, &this->proxyhost, &this->proxyport, NULL))
      return 0;
    
    if (this->proxyport == 0)
      this->proxyport = DEFAULT_HTTP_PORT;
    
    if (this->proxyuser != NULL)
      if (http_plugin_basicauth (this->proxyuser, this->proxypassword,
	  this->proxyauth, BUFSIZE))
	return 0;
  }
  
  if(http_plugin_parse_url (this->mrlbuf, &this->user, &this->password,
      &this->host, &this->port, &this->filename))
    return 0;

  if(this->port == 0)
    this->port = DEFAULT_HTTP_PORT;

  if (this->user != NULL)
    if (http_plugin_basicauth (this->user, this->password, this->auth, BUFSIZE))
      return 0;

  {
    char buf[256];

    sprintf(buf, _("input_http: opening >/%s< on host >%s<"), this->filename, this->host);

    if(proxy != NULL)
      sprintf(buf, _("%s via proxy >%s<"), buf, this->proxyhost);
    
    sprintf(buf, "%s\n", buf);

#ifdef LOG
    printf (buf);
#endif
  }
  
  if (proxy != NULL)
    this->fh = http_plugin_host_connect (this->proxyhost, this->proxyport, this->xine);
  else
    this->fh = http_plugin_host_connect (this->host, this->port, this->xine);

  this->curpos = 0;

  if (this->fh == -1) {
    return 0;
  }

  if (proxy != NULL)
    if (this->port != DEFAULT_HTTP_PORT)
      sprintf (this->buf, "GET http://%s:%d/%s HTTP/1.0\015\012",
	  this->host, this->port, this->filename);
    else
      sprintf (this->buf, "GET http://%s/%s HTTP/1.0\015\012",
	  this->host, this->filename);
  else
    sprintf (this->buf, "GET /%s HTTP/1.0\015\012", this->filename);
  
  if (this->port != DEFAULT_HTTP_PORT)
    sprintf (this->buf + strlen(this->buf), "Host: %s:%d\015\012",
	this->host, this->port);
  else
    sprintf (this->buf + strlen(this->buf), "Host: %s\015\012",
	this->host);
  
  if (this->proxyuser != NULL)
    sprintf (this->buf + strlen(this->buf), "Proxy-Authorization: Basic %s\015\012",
	this->proxyauth);
  
  if (this->user != NULL)
    sprintf (this->buf + strlen(this->buf), "Authorization: Basic %s\015\012",
	this->auth);
  
  sprintf (this->buf + strlen(this->buf), "User-Agent: xine/%s\015\012",
      VERSION);
  
  strcat (this->buf, "Accept: */*\015\012");

  strcat (this->buf, "\015\012");

  if (write (this->fh, this->buf, strlen(this->buf)) != strlen(this->buf)) {
    printf ("input_http: couldn't send request\n");
    return 0 ;
  }

#ifdef LOG
  printf ("input_http: request sent: >%s<\n",
	  this->buf);
#endif

  /* read and parse reply */
  done = 0; len = 0; linenum = 0;
  this->contentlength = 0;

  while (!done) {

    /*
    printf ("input_http: read...\n");
    */

    if (read (this->fh, &this->buf[len], 1) <=0) {
      
      switch (errno) {
      case EAGAIN:
	xine_log (this->xine, XINE_LOG_MSG, _("input_http: EAGAIN\n"));
	continue;
      default:
	xine_log (this->xine, XINE_LOG_MSG, _("input_http: read error\n"));
	return 0;
      }
    }

    if (this->buf[len] == '\012') {

      this->buf[len] = '\0';
      len--;
      
      if (len >= 0 && this->buf[len] == '\015') {
	this->buf[len] = '\0';
	len--;
      }

      linenum++;
      
#ifdef LOG
      printf ("input_http: answer: >%s<\n", this->buf);
#endif

      if (linenum == 1)
      {
        int httpver, httpsub, httpcode;
	char httpstatus[BUFSIZE];

	if (sscanf(this->buf, "HTTP/%d.%d %d %[^\015\012]", &httpver, &httpsub,
		   &httpcode, httpstatus) != 4)
	{
      	  xine_log (this->xine, XINE_LOG_MSG, _("input_http: invalid http answer\n"));
	  return 0;
	}
	
	if (httpcode >= 300 && httpcode < 400) {
      	  xine_log (this->xine, XINE_LOG_MSG, _("input_http: 3xx redirection not implemented: >%d %s<\n"),
		  httpcode, httpstatus);
	  return 0;
	}
	if (httpcode < 200 || httpcode >= 300) {
      	  xine_log (this->xine, XINE_LOG_MSG, _("input_http: http status not 2xx: >%d %s<\n"),
		  httpcode, httpstatus);
	  return 0;
	}
      } else {
	if (this->contentlength == 0) {
	  off_t contentlength;
	  
	  if (sscanf(this->buf, "Content-Length: %Ld", &contentlength) == 1) {
      	    xine_log (this->xine, XINE_LOG_MSG, _("input_http: content length = %Ld bytes\n"), contentlength);
	    this->contentlength = contentlength;
	  }
        }
	
	if (!strncasecmp(this->buf, "Location: ", 10))
	{
      	  xine_log (this->xine, XINE_LOG_MSG, _("input_http: Location redirection not implemented\n"));
	  return 0;
	}
      }
      
      if (len == -1)
	done = 1;
      else
	len = 0;
    } else
      len ++;
  }

#ifdef LOG
  printf ("input_http: end of headers\n");
#endif

  this->nbc    = nbc_init (this->xine);

  /*
   * fill preview buffer
   */

  this->preview_size = http_plugin_read (&this->input_plugin, this->preview,
					 PREVIEW_SIZE);
  this->preview_pos  = 0;

  return 1;
}

static off_t http_plugin_read (input_plugin_t *this_gen, 
			       char *buf, off_t nlen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  off_t n, num_bytes;

  nbc_check_buffers (this->nbc);

  num_bytes = 0;

  while (num_bytes < nlen) {

    if (this->preview_pos < this->preview_size) {

      n = this->preview_size - this->preview_pos;
      if (n > (nlen - num_bytes)) 
	n = nlen - num_bytes;

#ifdef LOG
      printf ("stdin: %lld bytes from preview (which has %lld bytes)\n",
	      n, this->preview_size);
#endif

      memcpy (&buf[num_bytes], &this->preview[this->preview_pos], n);

      this->preview_pos += n;

    } else
      n = read (this->fh, &buf[num_bytes], nlen - num_bytes);

    if (n <= 0) {
      
      switch (errno) {
      case EAGAIN:
	xine_log (this->xine, XINE_LOG_MSG, _("input_http: EAGAIN\n"));
	continue;
      default:
	xine_log (this->xine, XINE_LOG_MSG, _("input_http: read error\n"));
	return 0;
      }
    }
    
    num_bytes += n;
    this->curpos += n;
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

  off_t                 total_bytes;
  http_input_plugin_t  *this = (http_input_plugin_t *) this_gen;
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);

  nbc_check_buffers (this->nbc);

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
  pthread_cleanup_push( pool_release_buffer, buf );

  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  total_bytes = http_plugin_read (this_gen, buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    buf = NULL;
  }

  if (buf != NULL)
    buf->size = total_bytes;

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL);
  pthread_cleanup_pop(0);

  return buf;
}

static off_t http_plugin_get_length (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  return this->contentlength;
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

static off_t http_plugin_seek(input_plugin_t *this_gen,
			      off_t offset, int origin) {

  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  /* dummy implementation: don't seek, just return current position */
  return this->curpos;
}

static int http_plugin_eject_media (input_plugin_t *this_gen) {
  return 1;
}

static void http_plugin_close (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  close(this->fh);
  this->fh = -1;

  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }
}

static void http_plugin_stop (input_plugin_t *this_gen) {

  http_plugin_close(this_gen);
}

static char *http_plugin_get_description (input_plugin_t *this_gen) {
  return _("http network stream input plugin");
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

  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  switch (data_type) {
  case INPUT_OPTIONAL_DATA_PREVIEW:

    memcpy (data, this->preview, this->preview_size);
    return this->preview_size;

    break;
  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static void http_plugin_dispose (input_plugin_t *this_gen ) {
  free (this_gen);
}

input_plugin_t *init_input_plugin (int iface, xine_t *xine) {

  http_input_plugin_t *this;
  config_values_t    *config;

  if (iface != 8) {
    xine_log (xine, XINE_LOG_PLUGIN,
	    _("http input plugin doesn't support plugin API version %d.\n"
	      "PLUGIN DISABLED.\n"
	      "This means there's a version mismatch between xine and this input"
	      "plugin.\nInstalling current input plugins should help.\n"),
	    iface);
    return NULL;
  }

  this       = (http_input_plugin_t *) xine_xmalloc(sizeof(http_input_plugin_t));
  config     = xine->config;
  this->xine = xine;

  this->input_plugin.interface_version = INPUT_PLUGIN_IFACE_VERSION;
  this->input_plugin.get_capabilities  = http_plugin_get_capabilities;
  this->input_plugin.open              = http_plugin_open;
  this->input_plugin.read              = http_plugin_read;
  this->input_plugin.read_block        = http_plugin_read_block;
  this->input_plugin.seek              = http_plugin_seek;
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
  this->input_plugin.dispose           = http_plugin_dispose;
  this->input_plugin.is_branch_possible= NULL;

  this->fh      = -1;
  this->config  = config;
  this->curpos  = 0;
  this->nbc     = NULL;
  
  return (input_plugin_t *) this;
}
