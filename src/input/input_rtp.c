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
 *      /* read pack */
 *      read(fd, buf, 2048)
 *      /* end of stream */
 *      if (buf[3] == 0xb9)
 *          return 0;
 *
 *      /* extract the system reference clock, srcb, from the pack */
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

#include "input_plugin.h"

static int last_input_error;
static int input_eof;
static uint32_t xine_debug;

typedef struct _input_buffer {
    struct _input_buffer *next;
    unsigned char        *buf;
} input_buffer;
    
#define N_BUFFERS 128
#define IBUFFER_SIZE 2048

static int input_file_handle = -1;

input_buffer   *free_buffers;
input_buffer   **fifo_head;
input_buffer   fifo_tail;

pthread_mutex_t  buffer_mutex;
pthread_cond_t   buffer_notempty;

static pthread_t reader_thread;

static void * input_plugin_read_loop(void *);

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
	 struct ip_mreqn mreqn;
	 
	 mreqn.imr_multiaddr.s_addr = sin.sin_addr.s_addr;
	 mreqn.imr_address.s_addr = INADDR_ANY;
	 mreqn.imr_ifindex = 0;
	 if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,&mreqn,sizeof(mreqn))) {
	      perror("setsockopt IP_ADD_MEMBERSHIP failed (multicast kernel?)");
	      exit(1);
	 }
    }

    return s;
}

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

static void input_plugin_init (void) {
    int bufn;

    for (bufn = 0; bufn < N_BUFFERS; bufn++) {
	input_buffer *buf = malloc(sizeof(input_buffer));
	if (!buf) {
	    fprintf(stderr, "unable to allocate input buffer.\n");
	    exit(1);
	}
	buf->buf = malloc(IBUFFER_SIZE);
	if (!buf->buf) {
	    fprintf(stderr, "unable to allocate input buffer.\n");
	    exit(1);
	}
	buf->next = free_buffers;
	free_buffers = buf;
    }
}

static int input_plugin_open (char *mrl) {
  char *filename;
  char *pptr;
  int port = 7658;
  pthread_attr_t  thread_attrs;

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

  if (input_file_handle != -1)
      close(input_file_handle);
  input_file_handle = host_connect(filename, port);

  if (input_file_handle == -1) {
       return 0;
  }

  last_input_error = 0;
  input_eof = 0;
  fifo_tail.next = &fifo_tail;
  fifo_head = &fifo_tail.next;

  pthread_cond_init(&buffer_notempty, NULL);
  pthread_attr_init(&thread_attrs);
  pthread_attr_setdetachstate(&thread_attrs, PTHREAD_CREATE_DETACHED);
  pthread_create(&reader_thread, &thread_attrs, input_plugin_read_loop, (void *)input_file_handle);
  pthread_attr_destroy(&thread_attrs);

  return 1;
}

static uint32_t input_plugin_read (char *buf, uint32_t nlen) {
    input_buffer *ibuf;

    pthread_mutex_lock (&buffer_mutex);
    while (fifo_tail.next == &fifo_tail) {
	if (input_eof) {
	    pthread_mutex_unlock (&buffer_mutex);
	    return 0;
	}
	if (last_input_error) {
	    pthread_mutex_unlock (&buffer_mutex);
	    return last_input_error;
	}
	pthread_cond_wait(&buffer_notempty, &buffer_mutex);
    }
    ibuf = fifo_tail.next;
    fifo_tail.next = fifo_tail.next->next;

    /* Is FIFO now empty */
    if (fifo_tail.next == &fifo_tail)
	fifo_head = &fifo_tail.next;

    pthread_mutex_unlock (&buffer_mutex);
    
    memcpy(buf, ibuf->buf, nlen < IBUFFER_SIZE ? nlen : IBUFFER_SIZE);

    pthread_mutex_lock (&buffer_mutex);
    ibuf->next = free_buffers;
    free_buffers = ibuf;
    pthread_mutex_unlock (&buffer_mutex);

    return nlen < IBUFFER_SIZE ? nlen : IBUFFER_SIZE;
}

static void * input_plugin_read_loop(void *arg) {
    int		     inf      = (int) arg;
    input_buffer     *buf;
    int		     r;
    unsigned short   seq      = 0;
    static int	     warned   = 0;
	
    char whirly[] = "/-\\|";
    int  gig = 0;

    while (1) {
	pthread_mutex_lock (&buffer_mutex);
	/* we expect to be able to get a free buffer - possibly we
	   could be a bit more reasonable but this will do for now. */
	if (!free_buffers) {
	    input_eof = 1;
	    if (!warned) {
		printf("OUCH - ran out of buffers\n");
		warned = 1;
	    }
	    pthread_cond_signal(&buffer_notempty);
	    continue;
	}
	warned = 0;
	buf = free_buffers;
	free_buffers = free_buffers->next;
	pthread_mutex_unlock (&buffer_mutex);

	/* printf("%c\r", whirly[(gig++ % 4)]); */
	/* fflush(stdout); */
	r = read(inf, buf->buf, IBUFFER_SIZE);
	if (r < 0) {
	    /* descriptor may be closed by main thread */
	    if (r != EBADF)
		last_input_error = r;
	    return 0;
	}
	if (r == 0) {
	    input_eof = 1;
	    return 0;
	}
	
	/* For now - check whether we're dropping input */
	if (++seq != *(unsigned short *)buf->buf) {
	    printf("OUCH - dropped input packet %d %d\n", seq, *(unsigned short *)buf->buf);
	    seq = *(unsigned short *)buf->buf;
	}
	buf->buf[1] = buf->buf[0] = 0;
	pthread_mutex_lock (&buffer_mutex);
	buf->next = *fifo_head;
	*fifo_head = buf;
	fifo_head = &buf->next;
	pthread_cond_signal(&buffer_notempty);
	pthread_mutex_unlock (&buffer_mutex);
    }
}

static off_t input_plugin_seek (off_t offset, int origin) {

  return -1;
}

static uint32_t input_plugin_get_length (void) {
  return 0;
}

static uint32_t input_plugin_get_capabilities (void) {
  return 0;
}

static uint32_t input_plugin_get_blocksize (void) {
  return 2048;
}

static void input_plugin_close (void) {
    close(input_file_handle);
    input_file_handle = -1;
}

static int input_plugin_eject (void) {
  return 1;
}

static char *input_plugin_get_identifier (void) {
  return "RTP";
}

static int input_plugin_is_branch_possible (char *next_mrl) {
  return 0;
}

static input_plugin_t plugin_op = {
  NULL,
  NULL,
  input_plugin_init,
  input_plugin_open,
  input_plugin_read,
  input_plugin_seek,
  input_plugin_get_length,
  input_plugin_get_capabilities,
  NULL,
  input_plugin_get_blocksize,
  input_plugin_eject,
  input_plugin_close,
  input_plugin_get_identifier,
  NULL,
  input_plugin_is_branch_possible,
  NULL
};

input_plugin_t *input_plugin_getinfo(uint32_t dbglvl) {

  xine_debug = dbglvl;

  return &plugin_op;
}
