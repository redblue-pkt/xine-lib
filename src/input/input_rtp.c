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
 * Xine input plugin for multicast video streams.
 *
 *
 * This is something of an experiment - it doesn't work well yet.  Originally
 * the intent was to read an rtp stream, from, for example, Cisco IP
 * Tv. That's still a long term goal but RTP doesn't fit well in an input
 * plugin because typically video is carried on one multicast group and audio
 * in another - i.e it's already demultiplexed and an input plugin would
 * actually have to reassemble the content. Now that demultiplexers are
 * becomming separate loadable objects the right thing to do is to write an
 * RTP demux plugin and a playlist plugin that handles SDP.
 *
 *
 * In the meantime some experience with multicast video was wanted.  Not
 * having hardware available to construct a stream on the fly a server was
 * written to multicast the contents of an mpeg program stream - it just
 * reads a pack then transmits it at the appropriate time as follows.
 *
 *  fd is open for read on mpeg stream, sock for write on a multicast socket.
 *
 *  while (1) {
 *      \/\* read pack *\/
 *      read(fd, buf, 2048)
 *      \/\* end of stream  *\/
 *      if (buf[3] == 0xb9)
 *          return 0;
 *
 *      \/\* extract the system reference clock, srcb, from the pack *\/
 *
 *      send_at = srcb/90000.0;
 *      while (time_now < send_at) {
 *          wait;
 *      }
 *      r = write(sock, buf, 2048);
 *  }
 *
 * One problem is that a stream from a DVD needs each pack sending
 * at approx 2.5ms intervals which is a shorter interval than the
 * standard linux clock. The RTC can be used for more finely grained
 * timing.
 *
 * If you live in a non multicast friendly environment then the stream
 * can be unicast.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"

#ifdef __GNUC__
#define LOG_MSG_STDERR(xine, message, args...) {                     \
    xine_log(xine, XINE_LOG_MSG, message, ##args);                 \
    fprintf(stderr, message, ##args);                                \
  }
#define LOG_MSG(xine, message, args...) {                            \
    xine_log(xine, XINE_LOG_MSG, message, ##args);                 \
    printf(message, ##args);                                         \
  }
#else
#define LOG_MSG_STDERR(xine, ...) {                                  \
    xine_log(xine, XINE_LOG_MSG, __VA_ARGS__);                     \
    fprintf(stderr, __VA_ARGS__);                                    \
  }
#define LOG_MSG(xine, ...) {                                         \
    xine_log(xine, XINE_LOG_MSG, __VA_ARGS__);                     \
    printf(__VA_ARGS__);                                             \
  }
#endif

#define BUFFER_SIZE (1024*1024)

typedef struct {
  input_plugin_t    input_plugin;

  xine_stream_t    *stream;
  
  char             *mrl;
  config_values_t  *config;

  char             *filename;
  int               port;
  int               is_rtp;
  
  int               fh;
  
  pthread_mutex_t   buffer_mutex;
  pthread_cond_t    buffer_notempty;
  pthread_cond_t    buffer_empty;

  unsigned char    *buffer;
  long              buffer_start;    /* first data byte */
  long              buffer_length; /* no data bytes */

  struct timespec   preview_timeout;
  
  unsigned char     packet_buffer[65536];
  
  int               last_input_error;
  int               input_eof;

  pthread_t         reader_thread;

  int               curpos;
  int               rtp_running;

} rtp_input_plugin_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
  config_values_t  *config;

} rtp_input_class_t;


/* ***************************************************************** */
/*                        Private functions                          */
/* ***************************************************************** */

/*
 *
 */
static int host_connect_attempt(struct in_addr ia, int port, xine_t *xine) {
  int s=socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  struct sockaddr_in sin;
  int optval;
  
  if(s == -1) {
    LOG_MSG_STDERR(xine, _("socket(): %s.\n"), strerror(errno));
    return -1;
  }
  
  sin.sin_family = AF_INET;	
  sin.sin_addr   = ia;
  sin.sin_port   = htons(port);
  
  /* Try to increase receive buffer to 1MB to avoid dropping packets */
  optval = 1024 * 1024;
  if ((setsockopt(s, SOL_SOCKET, SO_RCVBUF,
		  &optval, sizeof(optval))) < 0) {
    LOG_MSG_STDERR(xine, _("setsockopt(SO_RCVBUF): %s.\n"), strerror(errno));
    return -1;
  }
  
  /* datagram socket */
  if (bind(s, (struct sockaddr *)&sin, sizeof(sin))) {
    LOG_MSG_STDERR(xine, _("bind(): %s.\n"), strerror(errno));
    return -1;
  }
  
  /* multicast ? */
  if ((ntohl(sin.sin_addr.s_addr) >> 28) == 0xe) {
#ifdef HAVE_IP_MREQN
    struct ip_mreqn mreqn;
    
    mreqn.imr_multiaddr.s_addr = sin.sin_addr.s_addr;
    mreqn.imr_address.s_addr = INADDR_ANY;
    mreqn.imr_ifindex = 0;
#else
    struct ip_mreq mreqn;
    
    mreqn.imr_multiaddr.s_addr = sin.sin_addr.s_addr;
    mreqn.imr_interface.s_addr = INADDR_ANY;
#endif
    if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,&mreqn,sizeof(mreqn))) {
      LOG_MSG_STDERR(xine, _("setsockopt(IP_ADD_MEMBERSHIP) failed (multicast kernel?): %s.\n"),
		     strerror(errno));
      return -1;
    }
  }
  
  return s;
}

/*
 *
 */
static int host_connect(const char *host, int port, xine_t *xine) {
  struct hostent *h;
  int i;
  int s;
  
  h=gethostbyname(host);
  if(h==NULL)
    {
      LOG_MSG_STDERR(xine, _("unable to resolve '%s'.\n"), host);
      return -1;
    }
  
  
  for(i=0; h->h_addr_list[i]; i++)
    {
      struct in_addr ia;
      memcpy(&ia, h->h_addr_list[i],4);
      s = host_connect_attempt(ia, port, xine);
      if(s != -1)
	return s;
    }
  LOG_MSG_STDERR(xine, _("unable to bind to '%s'.\n"), host);
  return -1;
}

/*
 *
 */
static void * input_plugin_read_loop(void *arg) {
  rtp_input_plugin_t *this   = (rtp_input_plugin_t *) arg;
  unsigned char *data;
  long length;
  
  while (1)
    {
      /* System calls are not a thread cancellation point in Linux
       * pthreads.  However, the RT signal sent to cancel the thread
       * will cause recv() to return with EINTR, and we can manually
       * check cancellation.
       **/
      pthread_testcancel();
      length = recv(this->fh, this->packet_buffer,
		    sizeof(this->packet_buffer), 0);
      pthread_testcancel();

      if (length < 0)
	{
	  if (errno != EINTR)
	    {
	      LOG_MSG_STDERR(this->stream->xine,
			     _("recv(): %s.\n"), strerror(errno));
	      return NULL;
	    }
	}
      else
	{
	  data = this->packet_buffer;
	  
	  if (this->is_rtp)
	    {
	      int pad, ext;
	      int csrc;
	      
	      /* Do minimal RTP parsing to extract payload.  See
	       * http://www.faqs.org/rfcs/rfc1889.html for header format.
	       *
	       * WARNING: wholly untested code.  I don't have any RTP sender.
	       **/

	      if (length < 12)
		continue;
	      
	      pad = data[0] & 0x20;
	      ext = data[0] & 0x10;
	      csrc = data[0] & 0x0f;

	      data += 12 + csrc * 4;
	      length -= 12 + csrc * 4;

	      if (ext)
		{
		  long hlen;
		  
		  if (length < 4)
		    continue;

		  hlen = (data[2] << 8) | data[3];
		  data += hlen;
		  length -= hlen;
		}

	      if (pad)
		{
		  if (length < 1)
		    continue;

		  /* FIXME: is the pad length byte included in the
		   * length value or not?  We assume it is not.
		   */
		  length -= data[length - 1] + 1;
		}
	    }

	  /* insert data into cyclic buffer */
	  pthread_mutex_lock(&this->buffer_mutex);
	  
	  while (length > 0)
	    {
	      long n, pos;
	      
	      while (this->buffer_length == BUFFER_SIZE)
		{
		  pthread_cond_wait(&this->buffer_empty,
				    &this->buffer_mutex);
		}
	      
	      pos = (this->buffer_start + this->buffer_length) % BUFFER_SIZE;
	      n = length;

	      /* Don't write past end of buffer */
	      if (pos + n > BUFFER_SIZE)
		{
		  n = BUFFER_SIZE - pos;
		}

	      /* And don't write into start of data either */
	      if (pos < this->buffer_start && pos + n > this->buffer_start)
		{
		  n = this->buffer_start - pos;
		}

	      memcpy(this->buffer + pos, data, n);

	      data += n;
	      length -= n;

	      this->buffer_length += n;
	      pthread_cond_signal(&this->buffer_notempty);
	    }

	  pthread_mutex_unlock(&this->buffer_mutex);
	}
    }
}
/* ***************************************************************** */
/*                         END OF PRIVATES                           */
/* ***************************************************************** */

/*
 *
 */
static off_t rtp_plugin_read (input_plugin_t *this_gen, 
			      char *buf, off_t length) {
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;
  struct timeval tv;
  struct timespec timeout;
  off_t copied = 0;

  gettimeofday(&tv, NULL);
  timeout.tv_nsec = tv.tv_usec * 1000;
  timeout.tv_sec = tv.tv_sec + 1;
    
  pthread_mutex_lock(&this->buffer_mutex);

  while (length > 0)
    {
      long n;
	      
      while (this->buffer_length == 0)
	{
	  if (pthread_cond_timedwait(&this->buffer_notempty,
				     &this->buffer_mutex,
				     &timeout) != 0)
	    {
	      pthread_mutex_unlock(&this->buffer_mutex);
	      return copied;
	    }
	}

      n = length;

      /* Don't read past end of buffer */
      if (this->buffer_start + n > BUFFER_SIZE)
	{
	  n = BUFFER_SIZE - this->buffer_start;
	}

      /* Don't copy past end of data either */
      if (n > this->buffer_length)
	{
	  n = this->buffer_length;
	}

      memcpy(buf, this->buffer + this->buffer_start, n);

      buf += n;
      copied += n;
      length -= n;

      this->buffer_start = (this->buffer_start + n) % BUFFER_SIZE;
      this->buffer_length -= n;
      pthread_cond_signal(&this->buffer_empty);
    }

  pthread_mutex_unlock(&this->buffer_mutex);

  this->curpos += copied;

  return copied;
}

/*
 *
 */
static off_t rtp_plugin_seek (input_plugin_t *this_gen,
			      off_t offset, int origin) {
  
  return -1;
}

/*
 *
 */
static off_t rtp_plugin_get_length (input_plugin_t *this_gen) {

  return -1;
}

/*
 *
 */
static off_t rtp_plugin_get_current_pos (input_plugin_t *this_gen){
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;
  
  return this->curpos;
}

/*
 *
 */
static uint32_t rtp_plugin_get_capabilities (input_plugin_t *this_gen) {

  return INPUT_CAP_PREVIEW;
}

/*
 *
 */
static uint32_t rtp_plugin_get_blocksize (input_plugin_t *this_gen) {

  return 0;
}

/*
 *
 */
static char* rtp_plugin_get_mrl (input_plugin_t *this_gen) {
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;

  return this->mrl;
}

/*
 *
 */
static int rtp_plugin_get_optional_data (input_plugin_t *this_gen, 
					 void *data, int data_type) {
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;

  if (data_type == INPUT_OPTIONAL_DATA_PREVIEW)
    {
      unsigned char *buf = (unsigned char *) data;
      long pos, len, copied;
      
      pthread_mutex_lock(&this->buffer_mutex);
      while (this->buffer_length < MAX_PREVIEW_SIZE)
	{
	  LOG_MSG(this->stream->xine, _("RTP: waiting for preview data\n"));
      
	  if (pthread_cond_timedwait(&this->buffer_notempty,
				     &this->buffer_mutex,
				     &this->preview_timeout) == ETIMEDOUT)
	    {
	      LOG_MSG(this->stream->xine, _("RTP: waiting for preview data: timeout\n"));
	      break;
	    }
	}

      pos = this->buffer_start;
      len = this->buffer_length < MAX_PREVIEW_SIZE
	? this->buffer_length : MAX_PREVIEW_SIZE;
      copied = len;
      
      /* we really shouldn't wrap, but maybe we will. */
      if (pos + len > BUFFER_SIZE)
	{
	  long n = BUFFER_SIZE - pos;
	  
	  memcpy(buf, this->buffer + pos, n);

	  buf += n;
	  len -= n;
	  pos = 0;
	}
      
      memcpy(buf, this->buffer + pos, len);

      pthread_mutex_unlock(&this->buffer_mutex);
      
      return copied;
    }
  else
    {
      return INPUT_OPTIONAL_UNSUPPORTED;
    }
}

static void rtp_plugin_dispose (input_plugin_t *this_gen ) {
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;
  
  
  if (this->rtp_running) {
    LOG_MSG(this->stream->xine, _("RTP: stopping reading thread...\n"));
    pthread_cancel(this->reader_thread);
    pthread_join(this->reader_thread, NULL);
    LOG_MSG(this->stream->xine, _("RTP: reading thread terminated\n"));
  }
  
  
  if (this->fh != -1)
    close(this->fh);

  free(this->buffer);
  free(this->mrl);
  free(this);
}

static int rtp_plugin_open (input_plugin_t *this_gen ) {
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;
  int                 err;

  LOG_MSG(this->stream->xine, _("Opening >%s<\n"), this->filename);
  
  this->fh = host_connect(this->filename, this->port, this->stream->xine);

  if (this->fh == -1) {
       return 0;
  }

  this->last_input_error = 0;
  this->input_eof = 0;
  this->curpos = 0;
  this->rtp_running = 1;

  if ((err = pthread_create(&this->reader_thread, NULL, 
		            input_plugin_read_loop, (void *)this)) != 0) {
    LOG_MSG_STDERR(this->stream->xine,
		   _("input_rtp: can't create new thread (%s)\n"),
		   strerror(err));
    abort();
  }

  this->preview_timeout.tv_sec = time(NULL) + 5;
  this->preview_timeout.tv_nsec = 0;
    
  return 1;
}

static input_plugin_t *rtp_class_get_instance (input_class_t *cls_gen,
					xine_stream_t *stream,
					const char *data) {
  rtp_input_plugin_t *this;
  char               *filename = NULL;
  char               *pptr;
  char               *mrl;
  int                 is_rtp = 0;
  int                 port = 7658;

  mrl = strdup(data);
  if (!strncasecmp (mrl, "rtp://", 6)) {
    filename = &mrl[6];
    is_rtp = 1;
  }
  else if (!strncasecmp (mrl, "udp://", 6)) {
    filename = &mrl[6];
    is_rtp = 0;
  }
  
  if (filename == NULL || strlen(filename) == 0) {
    free(mrl);
    return NULL;
  }
  
  pptr=strrchr(filename, ':');
  if (pptr) {
    *pptr++ = 0;
    sscanf(pptr,"%d", &port);
  }
  
  this = (rtp_input_plugin_t *) malloc(sizeof(rtp_input_plugin_t));
  this->stream      = stream;
  this->mrl         = mrl;
  this->filename    = filename;
  this->port        = port;
  this->is_rtp      = is_rtp;
  this->fh          = -1;
  this->rtp_running = 0;

  pthread_mutex_init(&this->buffer_mutex, NULL);
  pthread_cond_init(&this->buffer_notempty, NULL);
  pthread_cond_init(&this->buffer_empty, NULL);

  this->buffer = malloc(BUFFER_SIZE);
  this->buffer_start = 0;
  this->buffer_length = 0;
  
  this->preview_timeout.tv_sec = time(NULL) + 5;
  this->preview_timeout.tv_nsec = 0;
    
  this->input_plugin.open              = rtp_plugin_open;
  this->input_plugin.get_capabilities  = rtp_plugin_get_capabilities;
  this->input_plugin.read              = rtp_plugin_read;
  this->input_plugin.read_block        = NULL;
  this->input_plugin.seek              = rtp_plugin_seek;
  this->input_plugin.get_current_pos   = rtp_plugin_get_current_pos;
  this->input_plugin.get_length        = rtp_plugin_get_length;
  this->input_plugin.get_blocksize     = rtp_plugin_get_blocksize;
  this->input_plugin.get_mrl           = rtp_plugin_get_mrl;
  this->input_plugin.get_optional_data = rtp_plugin_get_optional_data;
  this->input_plugin.dispose           = rtp_plugin_dispose;
  this->input_plugin.input_class       = cls_gen;
  
  return &this->input_plugin;
}


/*
 *  net plugin class
 */
 
static char *rtp_class_get_description (input_class_t *this_gen) {
	return _("RTP and UDP input plugin as shipped with xine");
}

static char *rtp_class_get_identifier (input_class_t *this_gen) {
  return "RTP/UDP";
}

static void rtp_class_dispose (input_class_t *this_gen) {
  rtp_input_class_t  *this = (rtp_input_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {

  rtp_input_class_t  *this;

  this         = (rtp_input_class_t *) xine_xmalloc(sizeof(rtp_input_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->input_class.get_instance      = rtp_class_get_instance;
  this->input_class.get_description   = rtp_class_get_description;
  this->input_class.get_identifier    = rtp_class_get_identifier;
  this->input_class.get_dir           = NULL;
  this->input_class.get_autoplay_list = NULL;
  this->input_class.dispose           = rtp_class_dispose;
  this->input_class.eject_media       = NULL;

  return this;
}


/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_INPUT, 13, "rtp", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

