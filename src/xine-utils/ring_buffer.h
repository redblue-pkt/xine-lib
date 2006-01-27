/* 
 * Copyright (C) 2000-2006 the xine project
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
 * $Id: ring_buffer.h,v 1.1 2006/01/27 07:55:18 tmattern Exp $
 *
 * Fifo + Ring Buffer
 */
typedef struct xine_ring_buffer_s xine_ring_buffer_t;

/* Creates a new ring buffer */
xine_ring_buffer_t *xine_ring_buffer_new(size_t size);

/* Deletes a ring buffer */
void xine_ring_buffer_delete(xine_ring_buffer_t *ring_buffer);

/* Returns a new chunk of the specified size */
/* Might block if the ring buffer is full */
void *xine_ring_buffer_alloc(xine_ring_buffer_t *ring_buffer, size_t size);

/* Put a chunk into the ring */
void xine_ring_buffer_put(xine_ring_buffer_t *ring_buffer, void *chunk);

/* Get a chunk of a specified size from the ring buffer */
/* Might block if the ring buffer is empty */
void *xine_ring_buffer_get(xine_ring_buffer_t *ring_buffer, size_t size);

/* Release the chunk, makes memory available for the alloc function */
void xine_ring_buffer_release(xine_ring_buffer_t *ring_buffer, void *chunk);

