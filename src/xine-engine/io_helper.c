/*
 * Copyright (C) 2000-2003 the xine project,
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
 * abortable i/o helper functions
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include "io_helper.h"

/* private constants */
#define XIO_FILE_READ             0
#define XIO_FILE_WRITE            1
#define XIO_TCP_READ              2
#define XIO_TCP_WRITE             3
#define XIO_POLLING_INTERVAL  50000  /* usec */


int xio_tcp_connect(xine_stream_t *stream, const char *host, int port) {

  struct hostent *h;
  int             i, s;
  
  h = gethostbyname(host);
  if (h == NULL) {
    xine_message(stream, XINE_MSG_UNKNOWN_HOST, "unable to resolve", host, NULL);
    return -1;
  }

  s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);  
  if (s == -1) {
    xine_message(stream, XINE_MSG_CONNECTION_REFUSED, "failed to create socket", strerror(errno), NULL);
    return -1;
  }
  
  if (fcntl (s, F_SETFL, fcntl (s, F_GETFL) | O_NONBLOCK) == -1) {
    xine_message(stream, XINE_MSG_CONNECTION_REFUSED, "can't put socket in non-blocking mode", strerror(errno), NULL);
    return -1;
  }

  for (i = 0; h->h_addr_list[i]; i++) {
    struct in_addr ia;
    struct sockaddr_in sin;
 
    memcpy (&ia, h->h_addr_list[i], 4);
    sin.sin_family = AF_INET;
    sin.sin_addr   = ia;
    sin.sin_port   = htons(port);
    
#ifndef WIN32
    if (connect(s, (struct sockaddr *)&sin, sizeof(sin))==-1 && errno != EINPROGRESS) {
#else
    if (connect(s, (struct sockaddr *)&sin, sizeof(sin))==-1 && WSAGetLastError() != WSAEINPROGRESS) {
#endif /* WIN32 */
      xine_message(stream, XINE_MSG_CONNECTION_REFUSED, strerror(errno), NULL);
      close(s);
      continue;
    }	
    return s;
  }
  return -1;
}


int xio_select (xine_stream_t *stream, int fd, int state, int timeout_msec) {

  fd_set fdset;
  fd_set *rset, *wset;
  struct timeval select_timeout;
  int timeout_usec, total_time_usec;
  int ret;
  
  timeout_usec = 1000 * timeout_msec;
  total_time_usec = 0;

  while (total_time_usec < timeout_usec) {

    FD_ZERO (&fdset);
    FD_SET  (fd, &fdset);

    select_timeout.tv_sec  = 0;
    select_timeout.tv_usec = XIO_POLLING_INTERVAL;

    rset = (state & XIO_READ_READY) ? &fdset : NULL;
    wset = (state & XIO_WRITE_READY) ? &fdset : NULL;
    ret = select (fd + 1, rset, wset, NULL, &select_timeout);
    if (ret == -1) {
      /* select error */
      return XIO_ERROR;
    } else if (ret == 1) {
      /* fd is ready */
      return XIO_READY;
    }

    /* select timeout
     *   aborts current read if action pending. otherwise xine
     *   cannot be stopped when no more data is available.
     */
    if (stream->demux_action_pending)
      return XIO_ABORTED;

    total_time_usec += XIO_POLLING_INTERVAL;
  }
  return XIO_TIMEOUT;
}


off_t xio_rw_abort(xine_stream_t *stream, int fd, int cmd, char *buf, off_t todo) {

  off_t ret = -1;
  off_t total = 0;
  int sret;
  int state = 0;

  if ((cmd == XIO_TCP_READ) || (cmd == XIO_FILE_READ)) {
    state = XIO_READ_READY;
  } else {
    state = XIO_WRITE_READY;
  }
  
  while (total < todo) {

    do {
      sret = xio_select(stream, fd, state, 500); /* 500 ms */
    } while (sret == XIO_TIMEOUT);
    
    if (sret != XIO_READY)
      return -1;
    
    switch (cmd) {
      case XIO_FILE_READ:
        ret = read(fd, &buf[total], todo - total);
        break;
      case XIO_FILE_WRITE:
        ret = write(fd, &buf[total], todo - total);
        break;
      case XIO_TCP_READ:
        ret = recv(fd, &buf[total], todo - total, 0);
        break;
      case XIO_TCP_WRITE:
        ret = send(fd, &buf[total], todo - total, 0);
        break;
      default:
        assert(1);
    }
    /* check EOF */
    if (!ret)
      break;

    /* check errors */
    if (ret < 0) {

      /* non-blocking mode */
#ifndef WIN32
      if (errno == EAGAIN)
        continue;
      perror("io_helper: I/O error");
#else
      if (WSAGetLastError() == WSAEWOULDBLOCK)
        continue;
      printf("io_helper: WSAGetLastError() = %d\n", WSAGetLastError());
#endif
      
      return ret;
    }
    total += ret;
  }
  return total;
}

off_t xio_tcp_read (xine_stream_t *stream, int s, char *buf, off_t todo) {
  return xio_rw_abort (stream, s, XIO_TCP_READ, buf, todo);
}

off_t xio_tcp_write (xine_stream_t *stream, int s, char *buf, off_t todo) {
  return xio_rw_abort (stream, s, XIO_TCP_WRITE, buf, todo);
}

off_t xio_file_read (xine_stream_t *stream, int s, char *buf, off_t todo) {
  return xio_rw_abort (stream, s, XIO_FILE_READ, buf, todo);
}

off_t xio_file_write (xine_stream_t *stream, int s, char *buf, off_t todo) {
  return xio_rw_abort (stream, s, XIO_FILE_WRITE, buf, todo);
}
