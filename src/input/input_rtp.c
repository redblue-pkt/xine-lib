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
#include <malloc.h>
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

#define RTP_BLOCKSIZE 2048

typedef struct _input_buffer {
  struct _input_buffer *next;
  unsigned char        *buf;
} input_buffer_t;

#define N_BUFFERS 128
#define IBUFFER_SIZE 2048

typedef struct {
  input_plugin_t    input_plugin;
  
  char             *mrl;
  config_values_t  *config;

  int               fh;
  
  input_buffer_t   *free_buffers;
  input_buffer_t  **fifo_head;
  input_buffer_t    fifo_tail;
  
  pthread_mutex_t   buffer_mutex;
  pthread_cond_t    buffer_notempty;

  int               last_input_error;
  int               input_eof;

  pthread_t         reader_thread;

  int               curpos;

} rtp_input_plugin_t;

/* ***************************************************************** */
/*                        Private functions                          */
/* ***************************************************************** */

/*
 *
 */
static int host_connect_attempt(struct in_addr ia, int port) {
  int    s=socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  struct sockaddr_in sin;
  
  if(s==-1) {
    perror("socket");
    return -1;
  }
  
  sin.sin_family = AF_INET;	
  sin.sin_addr   = ia;
  sin.sin_port   = htons(port);
  
  /* datagram socket */
  if (bind(s, (struct sockaddr *)&sin, sizeof(sin))) {
    perror("bind failed");
    exit(1);
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
      perror("setsockopt IP_ADD_MEMBERSHIP failed (multicast kernel?)");
      exit(1);
    }
  }
  
  return s;
}

/*
 *
 */
static int host_connect(const char *host, int port) {
  struct hostent *h;
  int i;
  int s;
  
  h=gethostbyname(host);
  if(h==NULL)
    {
      fprintf(stderr,"unable to resolve '%s'.\n", host);
      return -1;
    }
  
  
  for(i=0; h->h_addr_list[i]; i++)
    {
      struct in_addr ia;
      memcpy(&ia, h->h_addr_list[i],4);
      s=host_connect_attempt(ia, port);
      if(s != -1)
	return s;
    }
  fprintf(stderr, "unable to connect to '%s'.\n", host);
  return -1;
}

/*
 *
 */
static void * input_plugin_read_loop(void *arg) {
  rtp_input_plugin_t **this   = (rtp_input_plugin_t **) arg;
  input_buffer_t      *buf;
  int		       r;
  unsigned short       seq    = 0;
  static int	       warned = 0;
  /*    char whirly[] = "/-\\|"; */
  /*    int  gig = 0; */
  
  while (1) {
    pthread_mutex_lock (&(*this)->buffer_mutex);
    /* we expect to be able to get a free buffer - possibly we
       could be a bit more reasonable but this will do for now. */
    if (!(*this)->free_buffers) {
      (*this)->input_eof = 1;
      if (!warned) {
	printf("OUCH - ran out of buffers\n");
	warned = 1;
      }
      pthread_cond_signal(&(*this)->buffer_notempty);
      continue;
    }
    warned = 0;
    buf = (*this)->free_buffers;
    (*this)->free_buffers = (*this)->free_buffers->next;
    pthread_mutex_unlock (&(*this)->buffer_mutex);
    
    /* printf("%c\r", whirly[(gig++ % 4)]); */
    /* fflush(stdout); */
    r = read((*this)->fh, buf->buf, IBUFFER_SIZE);
    if (r < 0) {
      /* descriptor may be closed by main thread */
      if (r != EBADF)
	(*this)->last_input_error = r;
      (*this)->curpos = 0;
      return 0;
    }
    if (r == 0) {
      (*this)->input_eof = 1;
      (*this)->curpos = 0;
      return 0;
    }
    (*this)->curpos += r;
    
    /* For now - check whether we're dropping input */
    if (++seq != *(unsigned short *)buf->buf) {
      printf("OUCH - dropped input packet %d %d\n", seq, *(unsigned short *)buf->buf);
      seq = *(unsigned short *)buf->buf;
    }
    buf->buf[1] = buf->buf[0] = 0;
    pthread_mutex_lock (&(*this)->buffer_mutex);
    buf->next = *(*this)->fifo_head;
    *(*this)->fifo_head = buf;
    (*this)->fifo_head = &buf->next;
    pthread_cond_signal(&(*this)->buffer_notempty);
    pthread_mutex_unlock (&(*this)->buffer_mutex);
  }
}
/* ***************************************************************** */
/*                         END OF PRIVATES                           */
/* ***************************************************************** */

/*
 *
 */
static int rtp_plugin_open (input_plugin_t *this_gen, char *mrl ) {
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;
  char               *filename;
  char               *pptr;
  int                 port = 7658;
  pthread_attr_t      thread_attrs;
  int                 err;

  this->mrl = mrl;

  if (!strncmp (mrl, "rtp:",4)) {
       filename = &mrl[4];
  } else if (!strncmp (mrl, "udp:",4)) {
       filename = &mrl[4];
  } else
       return 0;
    
  if(strncmp(filename, "//", 2)==0)
  	filename+=2;
  
  printf ("Opening >%s<\n", filename);
  
  pptr=strrchr(filename, ':');
  if(pptr)
  {
       *pptr++=0;
       sscanf(pptr,"%d", &port);
  }

  if (this->fh != -1)
      close(this->fh);
  this->fh = host_connect(filename, port);

  if (this->fh == -1) {
       return 0;
  }

  this->last_input_error = 0;
  this->input_eof = 0;
  this->fifo_tail.next = &this->fifo_tail;
  this->fifo_head = &this->fifo_tail.next;
  this->curpos = 0;

  pthread_cond_init(&this->buffer_notempty, NULL);
  pthread_attr_init(&thread_attrs);
  pthread_attr_setdetachstate(&thread_attrs, PTHREAD_CREATE_DETACHED);
  if ((err = pthread_create(&this->reader_thread, &thread_attrs, 
		            input_plugin_read_loop, (void *)&this)) != 0) {
    fprintf (stderr, "input_rtp: can't create new thread (%s)\n",
	     strerror(err));
    exit (1);
  }
  pthread_attr_destroy(&thread_attrs);

  return 1;
}

/*
 *
 */
static off_t rtp_plugin_read (input_plugin_t *this_gen, 
				char *buf, off_t nlen) {
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;
  input_buffer_t *ibuf;
  
  pthread_mutex_lock (&this->buffer_mutex);
  while (this->fifo_tail.next == &this->fifo_tail) {
	if (this->input_eof) {
	  pthread_mutex_unlock (&this->buffer_mutex);
	  return 0;
	}
	if (this->last_input_error) {
	  pthread_mutex_unlock (&this->buffer_mutex);
	  return this->last_input_error;
	}
	pthread_cond_wait(&this->buffer_notempty, &this->buffer_mutex);
  }
  ibuf = this->fifo_tail.next;
  this->fifo_tail.next = this->fifo_tail.next->next;
  
  /* Is FIFO now empty */
  if (this->fifo_tail.next == &this->fifo_tail)
    this->fifo_head = &this->fifo_tail.next;
  
  pthread_mutex_unlock (&this->buffer_mutex);
  
  memcpy(buf, ibuf->buf, nlen < IBUFFER_SIZE ? nlen : IBUFFER_SIZE);
  
  pthread_mutex_lock (&this->buffer_mutex);
  ibuf->next = this->free_buffers;
  this->free_buffers = ibuf;
  pthread_mutex_unlock (&this->buffer_mutex);
  
  return nlen < IBUFFER_SIZE ? nlen : IBUFFER_SIZE;
}

/*
 *
 */
static off_t rtp_plugin_get_length (input_plugin_t *this_gen) {

  return 0;
}

/*
 *
 */
static off_t rtp_plugin_get_current_pos (input_plugin_t *this_gen){
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;
  
  return (this->curpos * IBUFFER_SIZE);
}

/*
 *
 */
static uint32_t rtp_plugin_get_capabilities (input_plugin_t *this_gen) {

  return INPUT_CAP_NOCAP;
}

/*
 *
 */
static uint32_t rtp_plugin_get_blocksize (input_plugin_t *this_gen) {

  return RTP_BLOCKSIZE;
}

/*
 *
 */
static void rtp_plugin_close (input_plugin_t *this_gen) {
  rtp_input_plugin_t *this = (rtp_input_plugin_t *) this_gen;

  close(this->fh);
  this->fh = -1;
}

/*
 *
 */
static void rtp_plugin_stop (input_plugin_t *this_gen) {
  rtp_plugin_stop(this_gen);
}

/*
 *
 */
static int rtp_plugin_eject_media (input_plugin_t *this_gen) {

  return 1;
}

/*
 *
 */
static char *rtp_plugin_get_description (input_plugin_t *this_gen) {

  return "rtp input plugin as shipped with xine";
}

/*
 *
 */
static char *rtp_plugin_get_identifier (input_plugin_t *this_gen) {

  return "RTP";
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

  return INPUT_OPTIONAL_UNSUPPORTED;
}

/*
 *
 */
input_plugin_t *init_input_plugin (int iface, xine_t *xine) {

  rtp_input_plugin_t *this;
  config_values_t    *config;
  int                 bufn;

  if (iface != 5) {
    printf("rtp input plugin doesn't support plugin API version %d.\n"
	   "PLUGIN DISABLED.\n"
	   "This means there's a version mismatch between xine and this input"
	   "plugin.\nInstalling current input plugins should help.\n",
	   iface);
    return NULL;
  }

    
  this       = (rtp_input_plugin_t *) xine_xmalloc(sizeof(rtp_input_plugin_t));
  config     = xine->config;
  
  for (bufn = 0; bufn < N_BUFFERS; bufn++) {
    input_buffer_t *buf = xine_xmalloc(sizeof(input_buffer_t));
    if (!buf) {
      fprintf(stderr, "unable to allocate input buffer.\n");
      exit(1);
    }
    buf->buf = xine_xmalloc(IBUFFER_SIZE);
    if (!buf->buf) {
      fprintf(stderr, "unable to allocate input buffer.\n");
      exit(1);
    }
    buf->next = this->free_buffers;
    this->free_buffers = buf;
  }
  
  this->input_plugin.interface_version = INPUT_PLUGIN_IFACE_VERSION;
  this->input_plugin.get_capabilities  = rtp_plugin_get_capabilities;
  this->input_plugin.open              = rtp_plugin_open;
  this->input_plugin.read              = rtp_plugin_read;
  this->input_plugin.read_block        = NULL;
  this->input_plugin.seek              = NULL;
  this->input_plugin.get_current_pos   = rtp_plugin_get_current_pos;
  this->input_plugin.get_length        = rtp_plugin_get_length;
  this->input_plugin.get_blocksize     = rtp_plugin_get_blocksize;
  this->input_plugin.eject_media       = rtp_plugin_eject_media;
  this->input_plugin.close             = rtp_plugin_close;
  this->input_plugin.stop              = rtp_plugin_stop;
  this->input_plugin.get_identifier    = rtp_plugin_get_identifier;
  this->input_plugin.get_description   = rtp_plugin_get_description;
  this->input_plugin.get_dir           = NULL;
  this->input_plugin.get_mrl           = rtp_plugin_get_mrl;
  this->input_plugin.get_autoplay_list = NULL;
  this->input_plugin.get_optional_data = rtp_plugin_get_optional_data;
  this->input_plugin.is_branch_possible= NULL;
  
  this->fh      = -1;
  this->mrl     = NULL;
  this->config  = config;
    
  return (input_plugin_t *) this;
}
