/*
 * Copyright (C) 2000-2003 the xine project
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
 * $Id: common.h,v 1.1 2003/04/20 16:42:09 guenter Exp $
 *
 */

#ifndef __COMMON_H__
#define __COMMON_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine.h>
#include <xineutils.h>

#if (0)
#include "Imlib-light/Imlib.h"

#include "xitk.h"

#include "kbindings.h"
#include "videowin.h"
#include "mediamark.h"
#include "actions.h"
#include "config_wrapper.h"
#include "control.h"
#include "errors.h"
#include "event.h"
#include "event_sender.h"
#include "i18n.h"
#include "lang.h"
#include "lirc.h"
#include "mrl_browser.h"
#include "network.h"
#include "panel.h"
#include "playlist.h"
#include "session.h"
#include "setup.h"
#include "skins.h"
#include "snapshot.h"
#include "stream_infos.h"
#include "viewlog.h"
#include "download.h"
#include "osd.h"
#include "file_browser.h"
#include "post.h"

#include "utils.h"
#endif

#ifdef HAVE_ORBIT 
#include "../corba/xine-server.h"
#endif

#ifdef HAVE_LIRC
#include <lirc/lirc_client.h>
#endif

/*
 * config related constants
 */
#define CONFIG_LEVEL_BEG         0 /* => beginner */
#define CONFIG_LEVEL_ADV        10 /* advanced user */
#define CONFIG_LEVEL_EXP        20 /* expert */
#define CONFIG_LEVEL_MAS        30 /* motku */
#define CONFIG_LEVEL_DEB        40 /* debugger (only available in debug mode) */

#define CONFIG_NO_DESC          NULL
#define CONFIG_NO_HELP          NULL
#define CONFIG_NO_CB            NULL
#define CONFIG_NO_DATA          NULL

/*
 * flags for autoplay options
 */
#define PLAY_ON_START           0x00000001
#define PLAYED_ON_START         0x00000002
#define QUIT_ON_STOP            0x00000004
#define FULL_ON_START           0x00000008
#define HIDEGUI_ON_START        0x00000010
#define PLAY_FROM_DVD           0x00000020
#define PLAY_FROM_VCD           0x00000040

/* Sound mixer capabilities */
#define MIXER_CAP_NOTHING       0x00000000
#define MIXER_CAP_VOL           0x00000001
#define MIXER_CAP_MUTE          0x00000002

/* Playlist loop modes */
#define PLAYLIST_LOOP_NO_LOOP   0 /* no loop (default) */
#define PLAYLIST_LOOP_LOOP      1 /* loop the whole playlist */
#define PLAYLIST_LOOP_REPEAT    2 /* loop the current mrl */
#define PLAYLIST_LOOP_SHUFFLE   3 /* random selection in playlist */
#define PLAYLIST_LOOP_SHUF_PLUS 4 /* random selection in playlist, never ending */
#define PLAYLIST_LOOP_MODES_NUM 5

#define SAFE_FREE(x)            do {           \
                                  if((x)) {    \
                                    free((x)); \
                                    x = NULL;  \
                                  }            \
                                } while(0)

/* Our default location for skin downloads */
#define SKIN_SERVER_URL         "http://xine.sourceforge.net/skins/skins.slx"

typedef struct {
  xine_video_port_t        *vo_port;
  int                       post_video_num;
  xine_post_t              *post_video;

  struct {
    int                     hue;
    int                     brightness;
    int                     saturation;
    int                     contrast;
  } video_settings;

  xine_audio_port_t        *ao_port;

  xine_stream_t            *stream;
  xine_stream_t            *spu_stream;

  xine_t                   *xine;

  xine_event_queue_t       *event_queue;

  int                       smart_mode;

  /* Visual stuff (like animation in video window while audio only playback) */
  struct {
    xine_stream_t          *stream;
    xine_event_queue_t     *event_queue;
    int                     running;
    int                     current;
    int                     enabled; /* 0, 1:vpost, 2:vanim */
    
    char                  **mrls;
    int                     num_mrls;
    
    int                     post_plugin_num;
    xine_post_t            *post_output;
    int                     post_changed;
    
  } visual_anim;
  
  struct {
    int                     enabled;
    int                     timeout;

    xine_osd_t             *sinfo;
    int                     sinfo_visible;

    xine_osd_t             *bar[2];
    int                     bar_visible;

    xine_osd_t             *status;
    int                     status_visible;

    xine_osd_t             *info;
    int                     info_visible;

  } osd;

  /* xine lib/gui configuration filename */
  char                     *configfile;
  int                       experience_level;

  const char               *logo_mrl;
  int                       logo_mode;
  int                       logo_has_changed;

  /* stuff like FULL_ON_START, QUIT_ON_STOP */
  /*action_id_t               actions_on_start[16];*/
  char                     *autoscan_plugin;


  uint32_t                  debug_level;

  int                       is_display_mrl;

  int                       mrl_overrided;

  int                       running;
  int                       ignore_next;

#ifdef HAVE_LIRC
  int                       lirc_enable;
#endif

#ifdef HAVE_XF86VIDMODE
  int                       XF86VidMode_fullscreen;
#endif

  struct {
    int                     caps; /* MIXER_CAP_x */
    int                     volume_level;
    int                     mute;
  } mixer;

  int                       layer_above;
  int                       always_layer_above;

  int                       network;
  
  int                       use_root_window;

  const char               *snapshot_location;
  
  int                       ssaver_timeout;

  int                       skip_by_chapter;

  int                       auto_vo_visibility;
  int                       auto_panel_visibility;

  int                        eventer_sticky;
  int                        stream_info_auto_update;

  int                        play_anyway;

  pthread_mutex_t            download_mutex;

} gGui_t;

#endif
