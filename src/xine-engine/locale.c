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
 * $Id: locale.c,v 1.2 2002/01/20 23:21:52 f1rmb Exp $
 *
 * intl init.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"

#define XINE_LOCALE_LOG

char *xine_set_locale(void) {
  char *cur_locale = NULL;
  
  if(setlocale (LC_ALL,"") == NULL) {
#ifdef XINE_LOCALE_LOG
    printf("xine-lib: locale not supported by C library\n");
#endif
    //    xine_log(this, XINE_LOG_INTERNAL, "xine-lib: locale not supported by C library");
    return NULL;
  }
  
  cur_locale = setlocale(LC_ALL, NULL);
  
  return cur_locale;
}
