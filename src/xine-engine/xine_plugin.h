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
 * $Id: xine_plugin.h,v 1.2 2002/09/04 23:31:13 guenter Exp $
 *
 * generic plugin definitions
 *
 */

#ifndef XINE_PLUGIN_H
#define XINE_PLUGIN_H

#define PLUGIN_NONE	          0
#define PLUGIN_INPUT	      1
#define PLUGIN_DEMUX	      2
#define PLUGIN_AUDIO_DECODER  3
#define PLUGIN_VIDEO_DECODER  4
#define PLUGIN_SPU_DECODER    5
#define PLUGIN_AUDIO_OUT      6
#define PLUGIN_VIDEO_OUT      7


typedef struct {
  uint8_t     type;               /* one of the PLUGIN_* constants above     */
  uint8_t     API;                /* API version supported by this plugin    */
  const char *id;                 /* a name that identifies this plugin      */
  uint32_t    version;            /* version number, increased every release */
  void       *special_info;       /* plugin-type specific, see structs below */
  void       *(*init)(xine_t *, void *); /* used to get/initialize an instance*/
} plugin_info_t;


/* special_info for a video output plugin */
typedef struct {
  int    priority;          /* priority of this plugin for auto-probing  */
  char  *description;       /* human-readable description of this plugin */
  int    visual_type;       /* visual type supported by this plugin      */
} vo_info_t;

/* special info for a audio output plugin */
typedef struct {
  char   *description;
  int     priority;
} ao_info_t ;

/* special_info for a decoder plugin */
typedef struct {
  uint32_t  *supported_types;/* streamtypes this decoder can handle       */
  int        priority;
} decoder_info_t;

#endif
