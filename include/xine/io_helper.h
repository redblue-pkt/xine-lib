/*
 * Copyright (C) 2000-2021 the xine project,
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

#ifndef IO_HELPER_H
#define IO_HELPER_H

#include "xine_internal.h"


/* select states */
#define XIO_READ_READY    1
#define XIO_WRITE_READY   2

/* xine select return codes */
#define XIO_READY         0
#define XIO_ERROR         1
#define XIO_ABORTED       2
#define XIO_TIMEOUT       3


/*
 * Waits for a file descriptor/socket to change status.
 *
 * network input plugins should use this function in order to
 * not freeze the engine.
 *
 * params :
 *   stream        needed for aborting and reporting errors but may be NULL
 *   fd            file/socket descriptor or -1 (just wait)
 *   state         XIO_READ_READY, XIO_WRITE_READY or 0 (just wait)
 *   timeout_msec  timeout in milliseconds
 *
 * Engine seek/stop can abort this function if stream != NULL.
 *
 * return value :
 *   XIO_READY     the file descriptor is ready for cmd
 *   XIO_ERROR     an i/o error occured
 *   XIO_ABORTED   command aborted by an other thread
 *   XIO_TIMEOUT   the file descriptor is not ready after timeout_msec milliseconds
 */
int _x_io_select (xine_stream_t *stream, int fd, int state, int timeout_msec) XINE_PROTECTED XINE_USED;


/*
 * open a tcp connection
 *
 * params :
 *   stream        needed for reporting errors but may be NULL
 *   host          address of target
 *   port          port on target
 *
 * returns a socket descriptor or -1 if an error occured
 */
int _x_io_tcp_connect(xine_stream_t *stream, const char *host, int port) XINE_PROTECTED XINE_USED;

/* connect and handshake. */
typedef enum {
  /* return success. */
  XIO_HANDSHAKE_OK = 1,
  /* reopen same target, and try a different handshake (eg with/without tls). */
  XIO_HANDSHAKE_TRY_SAME = 2,
  /* try next target, if any. */
  XIO_HANDSHAKE_TRY_NEXT = 3,
  /* return failure (eg when handshake has hit a -1 EINTR). */
  XIO_HANDSHAKE_INTR = 4
} xio_handshake_status_t;
/* use _x_io_* () below. */
typedef xio_handshake_status_t (xio_handshake_cb_t)(void *userdata, int fd);
/* like _x_io_tcp_connect (). */
int _x_io_tcp_handshake_connect (xine_stream_t *stream, const char *host, int port,
  xio_handshake_cb_t *handshake_cb, void *userdata) XINE_PROTECTED XINE_USED;

/*
 * wait for finish connection
 *
 * params :
 *   stream        needed for aborting and reporting errors but may be NULL
 *   fd            socket descriptor
 *   timeout_msec  timeout in milliseconds
 *
 * return value:
 *   XIO_READY     host respond, the socket is ready for cmd
 *   XIO_ERROR     an i/o error occured
 *   XIO_ABORTED   command aborted by an other thread
 *   XIO_TIMEOUT   the file descriptor is not ready after timeout
 */
int _x_io_tcp_connect_finish(xine_stream_t *stream, int fd, int timeout_msec) XINE_PROTECTED XINE_USED;

/* The next 5 read/write functions will try to transfer todo/min bytes unless
 * - the end of stream is reached (0), or
 * - no data is available for user network timeout seconds (-1 ETIMEDOUT), or
 * - an io error hits (-1 Exxx), or
 * - xine engine wants to seek/stop (-1 EINTR).
 * _x_io_tcp_part_read (stream, s, buf, 0, len) may also yield (-1 EAGAIN), and will never wait.
 * network input plugins should use these functions in order not to freeze the engine.
 * "off_t" is just there for historical reasons, "(s)size_t" should be enough.
 */
off_t _x_io_tcp_read (xine_stream_t *stream, int s, void *buf, off_t todo) XINE_PROTECTED XINE_USED;
/* like _x_io_tcp_read () but:
 * 1. wait until we have at least min bytes, then
 * 2. return up to max bytes that are already there. */
ssize_t _x_io_tcp_part_read (xine_stream_t *stream, int s, void *buf, size_t min, size_t max) XINE_PROTECTED XINE_USED;
off_t _x_io_tcp_write (xine_stream_t *stream, int s, const void *buf, off_t todo) XINE_PROTECTED XINE_USED;
off_t _x_io_file_read (xine_stream_t *stream, int fd, void *buf, off_t todo) XINE_PROTECTED XINE_USED;
off_t _x_io_file_write (xine_stream_t *stream, int fd, const void *buf, off_t todo) XINE_PROTECTED XINE_USED;

/* XXX this is slow.
 * read a string from socket, return string length (same as strlen)
 * the string is always '\0' terminated but given buffer size is never exceeded
 * that is, _x_io_tcp_read_line(,,,X) <= (X-1) ; X > 0
 */
int _x_io_tcp_read_line(xine_stream_t *stream, int sock, char *str, int size) XINE_PROTECTED XINE_USED;

/*
 * Close socket
 */
int _x_io_tcp_close(xine_stream_t *stream, int fd) XINE_PROTECTED;

#endif
