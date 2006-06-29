/* 
  $Id: xine-extra.c,v 1.2 2006/06/29 12:28:06 dgp85 Exp $
 
  Copyright (C) 2002 Rocky Bernstein <rocky@panix.com>
  
  Program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 
  These are routines that probably should be in xine, but for whatever
  reason aren't - yet. 
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_VCDNAV
#include <cdio/types.h>
#else
#include "cdio/types.h"
#endif

#include "xine-extra.h"

static xine_t *my_xine = NULL;

/* This is modified from xine_log() and should really reside inside
   xine-lib/src/xine-engine/xine.c. It logs a message to "buf" and also
   prints it to stream.
*/

static void __attribute__(__format__(__printf__(4, 0)))
xine_vflog(xine_t *this, FILE *stream, int buf, const char *format, 
           va_list args)
{
  this->log_buffers[buf]->scratch_printf(this->log_buffers[buf], format, args);
  vfprintf(stream, format, args);
}

/*!
  This routine is like xine_log, except it takes a va_list instead of
  a variable number of arguments. It might be useful as a function
  pointer where one wants a specific prototype.

  In short this writes a message to buffer 'buf' and to stdout.
*/

void 
xine_vlog_msg(xine_t *this, int buf, const char *format, va_list args)
{
  xine_vflog(this, stdout, buf, format, args);
}

/*! This routine is like xine_log, except it takes a va_list instead
  of a variable number of arguments and writes to stderr rather than
  stdout. It might be useful as a function pointer where one wants a
  specific prototype.

  In short this writes a message to buffer 'buf' and to stderr.
*/
void 
xine_vlog_err(xine_t *this, int buf, const char *format, va_list args)
{
  xine_vflog(this, stderr, buf, format, args);
}

/*! Call this before calling any of the xine_log_msg or xine_log_err
  routines. It sets up the xine buffer that will be used in error
  logging.  

  \return true if everything went okay; false is returned if
  logging was already initialized, in which case nothing is done.

 */
bool
xine_log_init(xine_t *this)
{
  if (NULL == this) return false;
  my_xine = this;
  return true;
}

/*! This routine is like xine_log without any xine-specific paramenters.
  Before calling this routine you should have set up a xine log buffer via
  xine_log_init(). 

  In short this writes a message to buffer 'buf' and to stdout.

  \return true if everything went okay; false is there was 
  an error, such as logging wasn't initialized. On error, nothing is 
  logged.
*/
bool
xine_log_msg(const char *format, ...)
{
  va_list args;

  if (NULL == my_xine) return false;
  va_start(args, format);
  xine_vlog_msg(my_xine, XINE_LOG_MSG, format, args);
  va_end(args);
  return true;
}

/*! This routine is like xine_log without any xine-specific paramenters.
  Before calling this routine you should have set up a xine log buffer via
  xine_log_init(). 

  In short this writes a message to buffer 'buf' and to stdout.

  \return true if everything went okay; false is there was 
  an error, such as logging wasn't initialized. On error, nothing is 
  logged.
*/
bool
xine_log_err(const char *format, ...)
{
  va_list args;

  va_start(args, format);
  if (NULL == my_xine) return false;
  xine_vlog_err(my_xine, XINE_LOG_MSG, format, args);
  va_end(args);
  return true;
}

void
xine_free_mrls(int *num_mrls, xine_mrl_t **mrls) 
{
  (*num_mrls)--;
  for ( ; *num_mrls >= 0; (*num_mrls)-- ) {
    MRL_ZERO(mrls[*num_mrls]);
    free(mrls[*num_mrls]);
  }
  mrls=NULL;
  *num_mrls=0;
}
