/*
 * Copyright (C) 2000-2002 the xine project
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
 * This file contains plugin entries for several visualization post plugins.
 *
 * $Id: visualizations.c,v 1.2 2003/01/14 03:41:00 tmmm Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"
#include "post.h"


void *oscope_init_plugin(xine_t *xine, void *data);
void *fftscope_init_plugin(xine_t *xine, void *data);

/*
 * exported plugin catalog entries
 */

/* plugin catalog information */
post_info_t oscope_special_info = { XINE_POST_TYPE_AUDIO_VISUALIZATION };
post_info_t fftscope_special_info = { XINE_POST_TYPE_AUDIO_VISUALIZATION };

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_POST, 2, "oscope", XINE_VERSION_CODE, &oscope_special_info, &oscope_init_plugin },
  { PLUGIN_POST, 2, "fftscope", XINE_VERSION_CODE, &fftscope_special_info, &fftscope_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
