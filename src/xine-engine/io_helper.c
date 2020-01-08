/*
 * Copyright (C) 2000-2020 the xine project,
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
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <errno.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <string.h>

#include <xine/io_helper.h>
#include "xine_private.h"

/* private constants */
#define XIO_POLLING_INTERVAL  50000  /* usec */

/* #define ENABLE_IPV6 */

#ifndef ENABLE_IPV6
static void reportIP (xine_stream_t *stream, const char *text, const uint8_t *p, int port) {
  if (stream && (stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)) {
    char b[128], *q = b;
    xine_uint32_2str (&q, p[0]);
    *q++ = '.';
    xine_uint32_2str (&q, p[1]);
    *q++ = '.';
    xine_uint32_2str (&q, p[2]);
    *q++ = '.';
    xine_uint32_2str (&q, p[3]);
    *q++ = ':';
    xine_uint32_2str (&q, port);
    *q = 0;
    xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "io_helper: %s %s.\n", text, b);
  }
}
#else
static void reportIP (xine_stream_t *stream, const char *text, struct addrinfo *addr) {
  if (stream && (stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)) {
    char b[128], *q = b;
    if (addr->ai_family == AF_INET) {
      struct sockaddr_in *sa4 = (struct sockaddr_in *)addr->ai_addr;
      uint8_t *p = (uint8_t *) &sa4->sin_addr;
      xine_uint32_2str (&q, p[0]);
      *q++ = '.';
      xine_uint32_2str (&q, p[1]);
      *q++ = '.';
      xine_uint32_2str (&q, p[2]);
      *q++ = '.';
      xine_uint32_2str (&q, p[3]);
      *q++ = ':';
      xine_uint32_2str (&q, ntohs (sa4->sin_port));
    } else if (addr->ai_family == AF_INET6) {
      static const uint8_t tab_hex[16] = "0123456789abcdef";
      struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)addr->ai_addr;
      uint8_t *p = (uint8_t *) &sa6->sin6_addr;
      int i;
      *q++ = '[';
      for (i = 0; i < 16; i += 2) {
        uint32_t v = ((uint32_t)p[i] << 8) | p[i + 1];
        if (v) {
          if (v & 0xf000) *q++ = tab_hex[v >> 12];
          if (v & 0xff00) *q++ = tab_hex[(v >> 8) & 15];
          if (v & 0xfff0) *q++ = tab_hex[(v >> 4) & 15];
          *q++ = tab_hex[v & 15];
          *q++ = ':';
        } else {
          while (q[-2] != ':') *q++ = ':';
        }
      }
      if (q[-2] != ':') q--;
      *q++ = ']';
      *q++ = ':';
      xine_uint32_2str (&q, ntohs (sa6->sin6_port));
    }
    *q = 0;
    xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "io_helper: %s %s.\n", text, b);
  }
}
#endif

/* win32 specific error messages */
#ifdef WIN32
static const char *sock_strerror (int num) {
  switch (num) {
    case WSAEACCES: return ("Access denied");
    case WSAENETDOWN: return ("Network down");
    case WSAENETUNREACH: return ("Network unreachable");
    case WSAENETRESET: return ("Network reset");
    case WSAECONNABORTED: return ("Connection aborted");
    case WSAECONNRESET: return ("Connection reset by peer");
    case WSAECONNREFUSED: return ("Connection refused");
    case WSAENAMETOOLONG: return ("Name too long");
    case WSAEHOSTDOWN: return ("Host down");
    case WSAEHOSTUNREACH: return ("Host unreachable");
    case WSAHOST_NOT_FOUND: return ("Host not found");
    case WSATRY_AGAIN: return ("Try again later");
    case WSANO_RECOVERY: return ("Name resolution unavailable");
    case WSANO_DATA: return ("No suitable database entry");
    case WSAETIMEDOUT: return ("Connection timeout");
    default: return (strerror (num));
  }
}
#  define sock_errno WSAGetLastError ()
#  define IF_EAGAIN (WSAGetLastError() == WSAEWOULDBLOCK)
#  define SOCK_EAGAIN WSAEWOULDBLOCK
#  define SOCK_EINPROGRESS WSAEWOULDBLOCK
#  define SOCK_ENOENT WSAHOST_NOT_FOUND
#  define SOCK_EACCES WSAEACCES
#  define SOCK_ECONNREFUSED WSAECONNREFUSED
#else
#  define sock_strerror strerror
#  define sock_errno errno
#  define IF_EAGAIN (errno == EAGAIN)
#  define SOCK_EAGAIN EAGAIN
#  define SOCK_EINPROGRESS EINPROGRESS
#  define SOCK_ENOENT ENOENT
#  define SOCK_EACCES EACCES
#  define SOCK_ECONNREFUSED ECONNREFUSED
#endif

int _x_io_tcp_connect (xine_stream_t *stream, const char *host, int port) {
  return _x_io_tcp_handshake_connect (stream, host, port, NULL, NULL);
}

int _x_io_tcp_handshake_connect (xine_stream_t *stream, const char *host, int port,
  xio_handshake_cb_t *handshake_cb, void *userdata) {
#ifndef ENABLE_IPV6
  struct hostent *h;
  int i;
#else
  struct addrinfo hints, *res = NULL, *tmpaddr;
  int ip_version;
#endif
  xine_private_t *xine = stream ? (xine_private_t *)stream->xine : NULL;
  int same_retries;

  /* resolve host ip(s) */
  xprintf (&xine->x, XINE_VERBOSITY_DEBUG, "io_helper: resolving %s:%d...\n", host, port);
#ifndef ENABLE_IPV6
  h = gethostbyname (host);
  if (h == NULL) {
    int e = sock_errno;
    xprintf (&xine->x, XINE_VERBOSITY_DEBUG, "io_helper: gethostbyname: %s (%d).\n", sock_strerror (e), e);
    _x_message (stream, XINE_MSG_UNKNOWN_HOST, "unable to resolve", host, sock_strerror (e), NULL);
    return -1;
  }
  /* report found ip's */
  for (i = 0; h->h_addr_list[i]; i++)
    reportIP (stream, "found IP", h->h_addr_list[i], port);
#else
  memset (&hints, 0, sizeof (hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = PF_UNSPEC;
  {
    char strport[32], *q = strport;
    int r;
    xine_uint32_2str (&q, port);
    r = getaddrinfo (host, strport, &hints, &res);
    if (r != 0) {
      xprintf (&xine->x, XINE_VERBOSITY_DEBUG, "io_helper: getaddrinfo: %s (%d).\n", gai_strerror (r), r);
      _x_message (stream, XINE_MSG_UNKNOWN_HOST, "unable to resolve", host, gai_strerror (r), NULL);
      return -1;
    }
    /* report ip's */
    for (tmpaddr = res; tmpaddr; tmpaddr = tmpaddr->ai_next)
      reportIP (stream, "found IP", tmpaddr);
  }
  if (xine) {
    static const uint8_t modes[4] = {0, 4, (6 << 4) | 4, (4 << 4) | 6};
    ip_version = modes[xine->ip_pref & 3];
  } else {
    ip_version = 4;
  }
#endif

  /* try to connect ip's */
  same_retries = 5;
#ifndef ENABLE_IPV6
  i = 0;
#else
  tmpaddr = res;
#endif
  while (1) {
    xio_handshake_status_t status = XIO_HANDSHAKE_OK;
    int s;
#ifndef ENABLE_IPV6
    if (!h->h_addr_list[i])
      break;
#else
    if (!tmpaddr) {
      ip_version >>= 4;
      if (ip_version) {
        tmpaddr = res;
        continue;
      }
      break;
    }
    if (ip_version) {
      if (((ip_version & 15) == 4) == (tmpaddr->ai_family != AF_INET)) {
        tmpaddr = tmpaddr->ai_next;
        continue;
      }
    }
#endif
    do {
      int r;
      /* make socket */
#ifndef ENABLE_IPV6
      s = xine_socket_cloexec (PF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
      s = xine_socket_cloexec (tmpaddr->ai_family, SOCK_STREAM, IPPROTO_TCP);
#endif
      if (s == -1) {
        int e = sock_errno;
        xprintf (&xine->x, XINE_VERBOSITY_DEBUG, "io_helper: socket: %s (%d).\n", sock_strerror (e), e);
        _x_message (stream, XINE_MSG_CONNECTION_REFUSED, "failed to create socket", sock_strerror (e), NULL);
        /* XXX: does it make sense to retry this? */
        status = XIO_HANDSHAKE_INTR;
        break;
      }
      /* try to turn off blocking, but dont require that.
       * main io will work the same with and without, only connect () and close () may hang. */
#ifndef WIN32
      if (fcntl (s, F_SETFL, fcntl (s, F_GETFL) | O_NONBLOCK) == -1)
#else
      {
        unsigned long non_block = 1;
        r = ioctlsocket (s, FIONBIO, &non_block);
      }
      if (r == SOCKET_ERROR)
#endif
      {
        int e = sock_errno;
        xprintf (&xine->x, XINE_VERBOSITY_DEBUG, "io_helper: connect: %s (%d).\n", sock_strerror (e), e);
        _x_message (stream, XINE_MSG_CONNECTION_REFUSED, "can't put socket in non-blocking mode", sock_strerror (e), NULL);
      }
      /* connect now */
#ifndef ENABLE_IPV6
      {
        struct in_addr ia;
        union {
          struct sockaddr_in sin;
          struct sockaddr addr;
        } a;
        reportIP (stream, "connecting", h->h_addr_list[i], port);
        memcpy (&ia, h->h_addr_list[i], 4);
        a.sin.sin_family = AF_INET;
        a.sin.sin_addr = ia;
        a.sin.sin_port = htons (port);
        r = connect (s, &a.addr, sizeof (a.sin));
      }
#else
      reportIP (stream, "connecting", tmpaddr);
      r = connect (s, tmpaddr->ai_addr, tmpaddr->ai_addrlen);
#endif
      if (r == -1) {
        int e = sock_errno;
        if (e != SOCK_EINPROGRESS) {
          xprintf (&xine->x, XINE_VERBOSITY_DEBUG, "io_helper: socket: %s (%d).\n", sock_strerror (e), e);
          _x_message (stream, XINE_MSG_CONNECTION_REFUSED, host, sock_strerror (e), NULL);
          status = XIO_HANDSHAKE_TRY_NEXT;
          break;
        }
      }
#if 1
      /* yes _x_io_tcp_connect_finish () does this below.
       * however, if there are more IPs we can try, test this one now
       * before we discard any alternatives.
       * handshake_cb will also need this. */
#  ifndef ENABLE_IPV6
      if (stream && (handshake_cb || h->h_addr_list[i + 1]))
#  else
      if (stream && (handshake_cb || tmpaddr->ai_next))
#  endif
      {
        r = _x_io_select (stream, s, XIO_WRITE_READY, xine ? xine->network_timeout * 1000 : 30000);
        if (r == XIO_ABORTED) {
          status = XIO_HANDSHAKE_INTR;
          break;
        }
        if (r != XIO_READY) {
          status = XIO_HANDSHAKE_TRY_NEXT;
          break;
        }
        {
          int e;
          socklen_t len = sizeof (e);
          if ((getsockopt (s, SOL_SOCKET, SO_ERROR, &e, &len)) == -1)
            e = sock_errno;
          if (e) {
            xprintf (&xine->x, XINE_VERBOSITY_DEBUG, "io_helper: getsockopt: %s (%d).\n", sock_strerror (e), e);
            status = XIO_HANDSHAKE_TRY_NEXT;
            break;
          }
        }
      }
#endif
    } while (0);
    if ((status == XIO_HANDSHAKE_OK) && handshake_cb)
      status = handshake_cb (userdata, s);
    if (status == XIO_HANDSHAKE_OK) {
      /* done */
#ifdef ENABLE_IPV6
      freeaddrinfo (res);
#endif
      return s;
    }
    if (s >= 0)
      _x_io_tcp_close (NULL, s);
    if (status == XIO_HANDSHAKE_INTR)
      break;
    if (status == XIO_HANDSHAKE_TRY_SAME) {
      if (--same_retries <= 0) {
        xprintf (&xine->x, XINE_VERBOSITY_DEBUG,
          "_x_io_tcp_handshake_connect: too many XIO_HANDSHAKE_TRY_SAME, skipping.\n");
        status = XIO_HANDSHAKE_TRY_NEXT;
      }
    }
    if (status == XIO_HANDSHAKE_TRY_NEXT) {
      same_retries = 5;
#ifndef ENABLE_IPV6
      i++;
#else
      tmpaddr = tmpaddr->ai_next;
#endif
    } else if (status != XIO_HANDSHAKE_TRY_SAME) {
      xprintf (&xine->x, XINE_VERBOSITY_DEBUG,
        "_x_io_tcp_handshake_connect: unknown handshake status %d, leaving.\n", (int)status);
      break;
    }
  }
#ifdef ENABLE_IPV6
  freeaddrinfo (res);
#endif
  return -1;
}

int _x_io_select (xine_stream_t *stream, int fd, int state, int timeout_msec) {

  int timeout_usec, total_time_usec;
  int ret;
#ifdef WIN32
  HANDLE h;
  DWORD dwret;
  char msg[256];
#endif

#ifdef WIN32
  /* handle console file descriptiors differently on Windows */
  switch (fd) {
    case STDIN_FILENO: h = GetStdHandle(STD_INPUT_HANDLE); break;
    case STDOUT_FILENO: h = GetStdHandle(STD_OUTPUT_HANDLE); break;
    case STDERR_FILENO: h = GetStdHandle(STD_ERROR_HANDLE); break;
    default: h = INVALID_HANDLE_VALUE;
  }
#endif
  timeout_usec = 1000 * timeout_msec;
  total_time_usec = 0;

#ifdef WIN32
  if (h != INVALID_HANDLE_VALUE) {
    while (total_time_usec < timeout_usec) {
      dwret = WaitForSingleObject(h, timeout_msec);

      switch (dwret) {
        case WAIT_OBJECT_0: return XIO_READY;
        case WAIT_TIMEOUT:
          /* select timeout
           *   aborts current read if action pending. otherwise xine
           *   cannot be stopped when no more data is available.
           */
          if (_x_action_pending (stream))
            return XIO_ABORTED;
          break;
        case WAIT_ABANDONED:
          if (stream)
            xine_log(stream->xine, XINE_LOG_MSG,
                     _("io_helper: waiting abandoned\n"));
          return XIO_ERROR;
        case WAIT_FAILED:
        default:
          dwret = GetLastError();
          FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, (LPSTR)&msg, sizeof(msg), NULL);
          if (stream)
            xine_log(stream->xine, XINE_LOG_MSG,
                     _("io_helper: waiting failed: %s\n"), msg);
          return XIO_ERROR;
      }
    }
    if (_x_action_pending (stream)) {
      errno = EINTR;
      return XIO_ABORTED;
    }
    total_time_usec += XIO_POLLING_INTERVAL;
    return XIO_TIMEOUT;
  }
#endif

  if (timeout_msec == 0) {
    struct timeval select_timeout = {0, 0};
    fd_set fdset;
    fd_set *rset, *wset;

    if (_x_action_pending (stream)) {
      errno = EINTR;
      return XIO_ABORTED;
    }
    FD_ZERO (&fdset);
    FD_SET  (fd, &fdset);
    rset = (state & XIO_READ_READY) ? &fdset : NULL;
    wset = (state & XIO_WRITE_READY) ? &fdset : NULL;
    ret = select (fd + 1, rset, wset, NULL, &select_timeout);
    if (ret == -1 && errno != EINTR) {
      /* select error */
      return XIO_ERROR;
    } else if (ret == 1) {
      /* fd is ready */
      return XIO_READY;
    }
    return XIO_TIMEOUT;
  }

  while (total_time_usec < timeout_usec) {
    struct timeval select_timeout = {0, XIO_POLLING_INTERVAL};
    fd_set fdset;
    fd_set *rset, *wset;

    FD_ZERO (&fdset);
    FD_SET  (fd, &fdset);
    rset = (state & XIO_READ_READY) ? &fdset : NULL;
    wset = (state & XIO_WRITE_READY) ? &fdset : NULL;
    ret = select (fd + 1, rset, wset, NULL, &select_timeout);

    if (ret == -1 && errno != EINTR) {
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
    if (_x_action_pending (stream)) {
      errno = EINTR;
      return XIO_ABORTED;
    }

    total_time_usec += XIO_POLLING_INTERVAL;
  }
  return XIO_TIMEOUT;
}


/*
 * wait for finish connection
 */
int _x_io_tcp_connect_finish (xine_stream_t *stream, int fd, int timeout_msec) {
  int r;
  r = _x_io_select (stream, fd, XIO_WRITE_READY, timeout_msec);
  if (r == XIO_READY) {
    /* find out, if connection is successfull */
    int e;
    socklen_t len = sizeof (e);
    if ((getsockopt (fd, SOL_SOCKET, SO_ERROR, &e, &len)) == -1) {
      e = sock_errno;
      if (stream)
        xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "io_helper: getsockopt: %s (%d).\n", sock_strerror (e), e);
      _x_message (stream, XINE_MSG_CONNECTION_REFUSED, _("failed to get status of socket"), sock_strerror (e), NULL);
      return XIO_ERROR;
    }
    if (e) {
      if (stream)
        xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "io_helper: getsockopt: %s (%d).\n", sock_strerror (e), e);
      _x_message (stream, XINE_MSG_CONNECTION_REFUSED, sock_strerror (e), NULL);
      return XIO_ERROR;
    }
  }
  return r;
}


static off_t xio_err (xine_stream_t *stream, int ret) {
  /* non-blocking mode */
  int e = sock_errno;
  if (stream)
    xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "io_helper: getsockopt: %s (%d).\n", sock_strerror (e), e);
  if (e == SOCK_EACCES) {
    _x_message (stream, XINE_MSG_PERMISSION_ERROR, NULL, NULL);
    if (stream)
      xine_log (stream->xine, XINE_LOG_MSG, _("io_helper: Permission denied\n"));
  } else if (e == SOCK_ENOENT) {
    _x_message (stream, XINE_MSG_FILE_NOT_FOUND, NULL, NULL);
    if (stream)
      xine_log (stream->xine, XINE_LOG_MSG, _("io_helper: File not found\n"));
  } else if (e == SOCK_ECONNREFUSED) {
    _x_message (stream, XINE_MSG_CONNECTION_REFUSED, NULL, NULL);
    if (stream)
      xine_log (stream->xine, XINE_LOG_MSG, _("io_helper: Connection Refused\n"));
  }
  return ret;
}

off_t _x_io_tcp_read (xine_stream_t *stream, int s, void *buf_gen, off_t todo) {
  uint8_t *buf = buf_gen;
  unsigned int timeout;
  size_t want = todo, have = 0;

  _x_assert(buf != NULL);

  if (stream) {
    xine_private_t *xine = (xine_private_t *)stream->xine;
    timeout = xine->network_timeout * 1000;
  } else {
    timeout = 30000; /* 30K msecs = 30 secs */
  }

  while (have < want) {
    ssize_t ret;
    ret = _x_io_select (stream, s, XIO_READ_READY, timeout);
    if (ret != XIO_READY)
      return -1;
    ret = recv (s, buf + have, want - have, 0);
    /* check EOF */
    if (!ret)
      break;
    /* check errors */
    if (ret < 0) {
      if (IF_EAGAIN)
        continue;
      return xio_err (stream, ret);
    }
    have += ret;
  }
  return have;
}

ssize_t _x_io_tcp_part_read (xine_stream_t *stream, int s, void *buf_gen, size_t min, size_t max) {
  uint8_t *buf = buf_gen;
  unsigned int timeout;
  size_t have = 0;

  _x_assert(buf != NULL);

  if (stream) {
    xine_private_t *xine = (xine_private_t *)stream->xine;
    timeout = xine->network_timeout * 1000;
  } else {
    timeout = 30000; /* 30K msecs = 30 secs */
  }

  if (min == 0) {
    ssize_t ret = _x_io_select (stream, s, XIO_READ_READY, 0);
    if (ret != XIO_READY) {
      errno = ret == XIO_TIMEOUT ? EAGAIN : EINTR;
      return -1;
    }
    ret = recv (s, buf, max, 0);
    if (ret < 0) {
      if (!IF_EAGAIN)
        return xio_err (stream, ret);
      errno = EAGAIN;
    }
    return ret;
  }

  while (have < min) {
    ssize_t ret;
    ret = _x_io_select (stream, s, XIO_READ_READY, timeout);
    if (ret != XIO_READY)
      return -1;
    ret = recv (s, buf + have, max - have, 0);
    /* check EOF */
    if (!ret)
      break;
    /* check errors */
    if (ret < 0) {
      if (IF_EAGAIN)
        continue;
      return xio_err (stream, ret);
    }
    have += ret;
  }
  return have;
}

off_t _x_io_tcp_write (xine_stream_t *stream, int s, const void *wbuf_gen, off_t todo) {
  const uint8_t *wbuf = wbuf_gen;
  unsigned int timeout;
  size_t have = 0, want = todo;

  _x_assert (wbuf != NULL);

  if (stream) {
    xine_private_t *xine = (xine_private_t *)stream->xine;
    timeout = xine->network_timeout * 1000;
  } else {
    timeout = 30000; /* 30K msecs = 30 secs */
  }

  while (have < want) {
    ssize_t ret;
    ret = _x_io_select (stream, s, XIO_WRITE_READY, timeout);
    if (ret != XIO_READY)
      return -1;
    ret = send (s, wbuf + have, want - have, 0);
    /* check EOF */
    if (!ret)
      break;
    /* check errors */
    if (ret < 0) {
      if (IF_EAGAIN)
        continue;
      return xio_err (stream, ret);
    }
    have += ret;
  }
  return have;
}

off_t _x_io_file_read (xine_stream_t *stream, int s, void *buf_gen, off_t todo) {
  uint8_t *buf = buf_gen;
  unsigned int timeout;
  size_t have = 0, want = todo;

  _x_assert(buf != NULL);

  if (stream) {
    xine_private_t *xine = (xine_private_t *)stream->xine;
    timeout = xine->network_timeout * 1000;
  } else {
    timeout = 30000; /* 30K msecs = 30 secs */
  }

  while (have < want) {
    ssize_t ret;
    ret = _x_io_select (stream, s, XIO_READ_READY, timeout);
    if (ret != XIO_READY)
      return -1;
    ret = read (s, buf + have, want - have);
    /* check EOF */
    if (!ret)
      break;
    /* check errors */
    if (ret < 0) {
      if (IF_EAGAIN)
        continue;
      return xio_err (stream, ret);
    }
    have += ret;
  }
  return have;
}

off_t _x_io_file_write (xine_stream_t *stream, int s, const void *wbuf_gen, off_t todo) {
  const uint8_t *wbuf = wbuf_gen;
  unsigned int timeout;
  size_t have = 0, want = todo;

  _x_assert (wbuf != NULL);

  if (stream) {
    xine_private_t *xine = (xine_private_t *)stream->xine;
    timeout = xine->network_timeout * 1000;
  } else {
    timeout = 30000; /* 30K msecs = 30 secs */
  }

  while (have < want) {
    ssize_t ret;
    ret = _x_io_select (stream, s, XIO_WRITE_READY, timeout);
    if (ret != XIO_READY)
      return -1;
    ret = write (s, wbuf + have, want - have);
    /* check EOF */
    if (!ret)
      break;
    /* check errors */
    if (ret < 0) {
      if (IF_EAGAIN)
        continue;
      return xio_err (stream, ret);
    }
    have += ret;
  }
  return have;
}

/*
 * read a string from socket, return string length (same as strlen)
 * the string is always '\0' terminated but given buffer size is never exceeded
 * that is, _x_io_tcp_read_line(,,,X) <= (X-1) ; X > 0
 */
int _x_io_tcp_read_line(xine_stream_t *stream, int sock, char *str, int size) {
  int i = 0;
  char c;
  off_t r;

  if( size <= 0 )
    return 0;

  while ((r = _x_io_tcp_read (stream, sock, &c, 1)) == 1) {
    if (c == '\r' || c == '\n')
      break;
    if (i+1 == size)
      break;

    str[i] = c;
    i++;
  }

  if (r == 1 && c == '\r')
    r = _x_io_tcp_read (stream, sock, &c, 1);

  str[i] = '\0';

  return (r != -1) ? i : (int)r;
}

int _x_io_tcp_close(xine_stream_t *stream, int fd)
{
  struct linger linger = { 0, 0 };
  int r;

  if (fd == -1) {
    errno = EINVAL;
    return -1;
  }

  /* disable lingering (hard close) */
  r = setsockopt(fd, SOL_SOCKET, SO_LINGER, (void *)&linger, sizeof(linger));
  if (r < 0 && stream) {
    int e = sock_errno;
    xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "io_helper: disable linger: %s (%d).\n", sock_strerror (e), e);
  }

#ifdef WIN32
  r = closesocket(fd);
#else
  r = close(fd);
#endif
  if (r < 0 && stream) {
    int e = sock_errno;
    xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "io_helper: close: %s (%d).\n", sock_strerror (e), e);
  }

  return r;
}
