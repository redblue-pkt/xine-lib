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
 *   fd            file/socket descriptor
 *   state         XIO_READ_READY, XIO_WRITE_READY
 *  *abort         an other thread can abort this function by setting *abort
 *   timeout_sec   timeout in seconds
 *
 * return value :
 *   XIO_READY     the file descriptor is ready for cmd
 *   XIO_ERROR     an i/o error occured
 *   XIO_ABORTED   command aborted by an other thread
 *   XIO_TIMEOUT   the file descriptor is not ready after timeout_msec milliseconds
 */
int xio_select (xine_stream_t *stream, int fd, int state, int timeout_msec);


/*
 * open a tcp connection
 *
 * returns a socket descriptor or -1 if an error occured
 */
int xio_tcp_connect(xine_stream_t *stream, const char *host, int port);

/*
 * read from tcp socket checking demux_action_pending
 *
 * network input plugins should use this function in order to
 * not freeze the engine.
 *
 * aborts with zero if no data is available and *abort is set
 */
off_t xio_tcp_read (xine_stream_t *stream, int s, char *buf, off_t todo);


/*
 * write to a tcp socket checking demux_action_pending
 *
 * network input plugins should use this function in order to
 * not freeze the engine.
 *
 * aborts with zero if no data is available and *abort is set
 */
off_t xio_tcp_write (xine_stream_t *stream, int s, char *buf, off_t todo);

/*
 * read from a file descriptor checking demux_action_pending
 *
 * the fifo input plugin should use this function in order to
 * not freeze the engine.
 *
 * aborts with zero if no data is available and *abort is set
 */
off_t xio_file_read (xine_stream_t *stream, int fd, char *buf, off_t todo);


/*
 * write to a tcp socket checking demux_action_pending
 *
 * the fifo input plugin should use this function in order to
 * not freeze the engine.
 *
 * aborts with zero if *abort is set
 */
off_t xio_file_write (xine_stream_t *stream, int fd, char *buf, off_t todo);

#endif
