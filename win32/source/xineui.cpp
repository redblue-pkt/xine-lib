/* 
 * Copyright (C) 2000-2001 the xine project
 * 
 * This file is part of xine for win32 video player.
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
 * Xine win32 UI
 * by Matthew Grooms <elon@altavista.com>
 */

#include "xineui.h"
#include "common.h"

/*
#define LOG 1
*/
/**/

static char                 **video_driver_ids;
static char                 **audio_driver_ids;


static void config_update(xine_cfg_entry_t *entry, 
			  int type, int min, int max, int value, char *string) {

  switch(type) {

  case XINE_CONFIG_TYPE_UNKNOWN:
    fprintf(stderr, "Config key '%s' isn't registered yet.\n", entry->key);
    return;
    break;

  case XINE_CONFIG_TYPE_RANGE:
    entry->range_min = min;
    entry->range_max = max;
    break;

  case XINE_CONFIG_TYPE_STRING: 
    entry->str_value = string;
    break;
    
  case XINE_CONFIG_TYPE_ENUM:
  case XINE_CONFIG_TYPE_NUM:
  case XINE_CONFIG_TYPE_BOOL:
    entry->num_value = value;
    break;

  default:
    fprintf(stderr, "Unknown config type %d\n", type);
    return;
    break;
  }
  
  xine_config_update_entry(gGui->xine, entry);
}

static void config_update_num(char *key, int value) {
  xine_cfg_entry_t entry;

  if(xine_config_lookup_entry(gGui->xine, key, &entry))
    config_update(&entry, XINE_CONFIG_TYPE_NUM, 0, 0, value, NULL);
  else
    fprintf(stderr, "WOW, key %s isn't registered\n", key);
}

/*
 * Try to load video output plugin, by stored name or probing
 */
static xine_video_port_t *load_video_out_driver(int driver_number, win32_visual_t *vis) {
  xine_video_port_t      *video_port = NULL;
  int                     driver_num;


  /*
   * Setting default (configfile stuff need registering before updating, etc...).
   */
  driver_num = 
    xine_config_register_enum(gGui->xine, "video.driver", 
			      0, video_driver_ids,
			      ("video driver to use"),
			      ("Choose video driver. "
				"NOTE: you may restart xine to use the new driver"),
			      CONFIG_LEVEL_ADV,
			      CONFIG_NO_CB, 
			      CONFIG_NO_DATA);
  
  if (driver_number < 0) {
    /* video output driver auto-probing */
    const char *const *driver_ids;
    int                i;
    
    if((!strcasecmp(video_driver_ids[driver_num], "none")) || 
       (!strcasecmp(video_driver_ids[driver_num], "null"))) {

      /*vis = (win32_visual_t *) xine_xmalloc(sizeof(win32_visual_t));*/
      video_port = xine_open_video_driver(gGui->xine,
					  video_driver_ids[driver_num],
					  XINE_VISUAL_TYPE_NONE,
					  (void *) vis);
      if (video_port)
    	return video_port;
      
    }
    else if(strcasecmp(video_driver_ids[driver_num], "auto")) {
      
      vis = (win32_visual_t *) xine_xmalloc(sizeof(win32_visual_t));
      video_port = xine_open_video_driver(gGui->xine, 
					  video_driver_ids[driver_num],
					  XINE_VISUAL_TYPE_DIRECTX,
					  (void *) vis);
      if (video_port)
	return video_port;
    }
    
    /* note: xine-lib can do auto-probing for us if we want.
     *       but doing it here should do no harm.
     */
    i = 0;
    driver_ids = xine_list_video_output_plugins (gGui->xine);

    while (driver_ids[i]) {
      
      printf (("main: probing <%s> video output plugin\n"), driver_ids[i]);
      
      /*vis = (win32_visual_t *) xine_xmalloc(sizeof(win32_visual_t));*/
      video_port = xine_open_video_driver(gGui->xine, 
					  driver_ids[i],
					  XINE_VISUAL_TYPE_DIRECTX, 
					  (void *) vis);
      if (video_port) {
	return video_port;
      }
     
      i++;
    }
      
    if (!video_port) {
      printf (("main: all available video drivers failed.\n"));
      exit (1);
    }
    
  }
  else {
    
    /* 'none' plugin is a special case, just change the visual type */
    if((!strcasecmp(video_driver_ids[driver_number], "none")) 
       || (!strcasecmp(video_driver_ids[driver_number], "null"))) {

      vis = (win32_visual_t *) xine_xmalloc(sizeof(win32_visual_t));
      video_port = xine_open_video_driver(gGui->xine,
					  video_driver_ids[driver_number],
					  XINE_VISUAL_TYPE_NONE,
					  (void *) &vis);
      
      /* do not save on config, otherwise user would never see images again... */
    }
    else {
      vis = (win32_visual_t *) xine_xmalloc(sizeof(win32_visual_t));
      video_port = xine_open_video_driver(gGui->xine,
					  video_driver_ids[driver_number],
					  XINE_VISUAL_TYPE_DIRECTX, 
					  (void *) &vis);
      
#if (0)
      /* save requested driver (-V) */ 
      if(video_port)
        config_update_num("video.driver", driver_number);
#endif
    }
    
    if(!video_port) {
      printf (("main: video driver <%s> failed\n"), video_driver_ids[driver_number]);
      exit (1);
    }
    
  }

  return video_port;
}

/*
 * Try to load audio output plugin, by stored name or probing
 */
static xine_audio_port_t *load_audio_out_driver(int driver_number) {
  xine_audio_port_t      *audio_port = NULL;
  int                     driver_num;
  
  /*
   * Setting default (configfile stuff need registering before updating, etc...).
   */
  driver_num = 
    xine_config_register_enum(gGui->xine, "video.driver", 
			      0, video_driver_ids,
			      ("video driver to use"),
			      ("Choose video driver. "
				"NOTE: you may restart xine to use the new driver"),
			      CONFIG_LEVEL_ADV,
			      CONFIG_NO_CB, 
			      CONFIG_NO_DATA);
  

  driver_num = 
    xine_config_register_enum(gGui->xine, "audio.driver", 
			      0, audio_driver_ids,
			      ("audio driver to use"),
			      ("Choose audio driver. "
				"NOTE: you may restart xine to use the new driver"),
			      CONFIG_LEVEL_ADV,
			      CONFIG_NO_CB, 
			      CONFIG_NO_DATA);
  
  if (driver_number < 0) {
    const char *const *driver_ids;
    int    i;
    
    if (strcasecmp(audio_driver_ids[driver_num], "auto")) {
      
      /* don't want to load an audio driver ? */
      if (!strncasecmp(audio_driver_ids[driver_num], "NULL", 4)) {
        printf(("main: not using any audio driver (as requested).\n"));
        return NULL;
      }
      
      audio_port = xine_open_audio_driver(gGui->xine, 
					  audio_driver_ids[driver_num],
					  NULL);
      if (audio_port)
	return audio_port;
    }
    
    /* note: xine-lib can do auto-probing for us if we want.
     *       but doing it here should do no harm.
     */
    i = 0;
    driver_ids = xine_list_audio_output_plugins (gGui->xine);

    while (driver_ids[i]) {
      
      printf (("main: probing <%s> audio output plugin\n"), driver_ids[i]);
      
      audio_port = xine_open_audio_driver(gGui->xine, 
					  driver_ids[i],
					  NULL);
      if (audio_port) {
	return audio_port;
      }
     
      i++;
    }
      
    printf(("main: audio driver probing failed => no audio output\n"));
  }
  else {
    
    /* don't want to load an audio driver ? */
    if (!strncasecmp (audio_driver_ids[driver_number], "NULL", 4)) {

      printf(("main: not using any audio driver (as requested).\n"));

      /* calling -A null is useful to developers, but we should not save it at
       * config. if user doesn't have a sound card he may go to setup screen
       * changing audio.driver to NULL in order to make xine start a bit faster.
       */
    
    }
    else {
    
      audio_port = xine_open_audio_driver(gGui->xine, audio_driver_ids[driver_number], NULL);

      if (!audio_port) {
        printf (("main: audio driver <%s> failed\n"), audio_driver_ids[driver_number]);
        exit (1);
      }
    
      /* save requested driver (-A) */ 
      config_update_num("audio.driver", driver_number);
    }
  
  }

  return audio_port;
}


static void event_listener(void *user_data, const xine_event_t *event) {
  struct timeval tv;

	XINE_UI * xine_ui = ( XINE_UI * ) user_data;

  /*
   * Ignoring finished event logo is displayed (or played), that save us
   * from a loop of death
   */
  if(gGui->logo_mode && (event->type == XINE_EVENT_UI_PLAYBACK_FINISHED))
    return;
  
  gettimeofday (&tv, NULL);
  
  if(abs(tv.tv_sec - event->tv.tv_sec) > 3) {
    printf("Event too old, discarding\n");
    return;
  }
  

	switch( event->type )
	{ 
		case XINE_EVENT_UI_CHANNELS_CHANGED:
		{
			xine_ui->spu_channel = xine_get_param(gGui->stream, XINE_PARAM_SPU_CHANNEL);
			xine_ui->audio_channel = xine_get_param(gGui->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL);
		}
		break;

		case XINE_EVENT_UI_PLAYBACK_FINISHED:
			xine_ui->Stop();
			xine_ui->Play( xine_ui->playindex + 1 );
		break;

#if (0)
		case XINE_EVENT_NEED_NEXT_MRL:
		{
			xine_next_mrl_event_t * xine_next_mrl_event = ( xine_next_mrl_event_t * ) xine_event;

			PLAYITEM * playitem = 0;
			if( xine_ui->playindex < ( xine_ui->playcount - 1 ) )
			{
				xine_ui->mrl_short_name = xine_ui->playlist[ xine_ui->playindex + 1 ]->mrl_short_name;
				xine_ui->mrl_long_name = xine_ui->playlist[ xine_ui->playindex + 1 ]->mrl_long_name;
				xine_next_mrl_event->mrl = xine_ui->mrl_long_name;
				xine_ui->playindex++;
			}
			else
				xine_next_mrl_event->mrl = 0;

			xine_next_mrl_event->handled = 1;
		}
		break;

		case XINE_EVENT_BRANCHED:
#ifdef LOG
			printf("xineui.cpp : event received XINE_EVENT_BRANCHED\n");
#endif
//			gui_branched_callback ();
		break;
#endif

        /* e.g. aspect ratio change during dvd playback */
        case XINE_EVENT_FRAME_FORMAT_CHANGE:
#ifdef LOG
			printf("xineui.cpp : event received XINE_EVENT_FRAME_FORMAT_CHANGE\n");
#endif
        break;

        /* report current audio level (l/r) */
        case XINE_EVENT_AUDIO_LEVEL:
            if(event->stream == gGui->stream) {
                xine_audio_level_data_t *aevent = (xine_audio_level_data_t *) event->data;
      
                printf("XINE_EVENT_AUDIO_LEVEL: left 0>%d<255, right 0>%d<255\n", 
	            aevent->left, aevent->right);
			}
        break;

        /* last event sent when stream is disposed */
        case XINE_EVENT_QUIT:
#ifdef LOG
			printf("xineui.cpp : event received XINE_EVENT_QUIT\n");
#endif
        break;
    
		default:
#ifdef LOG
			printf("xineui.cpp : unsupported event received 0x%X\n", event->type);
#endif
		break;

	}      
}

_XINE_UI::_XINE_UI()
{
	memset( this, 0, sizeof( _XINE_UI ) );
}

_XINE_UI::~_XINE_UI()
{
	EndGui();
	EndXine();
}

bool _XINE_UI::InitGui( HINSTANCE hinstance )
{
	if( !hinstance )
		return false;

	hinst = hinstance;

	if( !init_ctrlwnd() )
		return false;

	if( !init_videownd() )
		return false;

	return true;
}

void _XINE_UI::EndGui()
{
	end_ctrlwnd();
	end_videownd();
}

bool _XINE_UI::InitXine()
{
  int                     i;
  int                     audio_channel = -1;
  int                     spu_channel = -1;
  char                   *audio_driver_id = NULL;
  char                   *video_driver_id = NULL;
  int                     driver_num;
  int                     session = -1;
  char                   *session_mrl = NULL;
  int major, minor, sub;

  /* Check xine library version */
  if( !xine_check_version( 0, 9, 4 ) )
  {
	xine_get_version(&major, &minor, &sub);
	error( "require xine library version 0.9.4, found %d.%d.%d.\n", 
	major, minor, sub );
	return false;
  }
  
  gGui = (gGui_t *) xine_xmalloc(sizeof(gGui_t));
  gui = gGui;

  gGui->stream                 = NULL;
  gGui->debug_level            = 0;
  gGui->autoscan_plugin        = NULL;
  gGui->network                = 0;
  gGui->use_root_window        = 0;

  /*gGui->vo_port*/

#ifdef HAVE_XF86VIDMODE
  gGui->XF86VidMode_fullscreen = 0;
#endif

#if (0)
	/* generate and init a config "object"	 */
	char * cfgfile = "config";
	gGui->configfile = ( char * ) xine_xmalloc( ( strlen( ( xine_get_homedir( ) ) ) + strlen( cfgfile ) ) +2 );
	sprintf( configfile, "%s/%s", ( xine_get_homedir() ), cfgfile );

	/*config = config_file_init( configfile );*/

#else
  /*
   * Initialize config
   */
  {
    char *cfgdir = ".xine";
	char *cfgfile = "config";
    
    if (!(gGui->configfile = getenv ("XINERC"))) {
      gGui->configfile = (char *) xine_xmalloc(strlen(xine_get_homedir())
					       + strlen(cfgdir) 
					       + strlen(cfgfile)
					       + 3);
      sprintf (gGui->configfile, "%s/%s", xine_get_homedir(), cfgdir);
      mkdir (gGui->configfile, 0755);
      sprintf (gGui->configfile + strlen(gGui->configfile), "/%s", cfgfile);
    }

#if (0)
    /* Popup setup window if there is no config file */
    if(stat(gGui->configfile, &st) < 0)
      gGui->actions_on_start[aos++] = ACTID_SETUP;
#endif

  }
#endif


    gGui->xine = xine_new();
    xine_config_load(gGui->xine, gGui->configfile);

#if (0)
  /*
   * init gui
   */
  gui_init(_argc - optind, &_argv[optind], &window_attribute);
#endif

  pthread_mutex_init(&gGui->download_mutex, NULL);

#if (0)  
  /* Automatically start playback if new_mode is enabled and playlist is filled */
  if(gGui->smart_mode && 
    (gGui->playlist.num || actions_on_start(gGui->actions_on_start, ACTID_PLAYLIST)) &&
    (!(actions_on_start(gGui->actions_on_start, ACTID_PLAY))))
     gGui->actions_on_start[aos++] = ACTID_PLAY;
#endif
  
  /*
   * xine init
   */
  xine_init(gGui->xine);


  /*
   * load and init output drivers
   */
  /* Video out plugin */
  driver_num = -1;
  {
    const char *const *vids = xine_list_video_output_plugins(gGui->xine);
    int                i = 0;
    
    while(vids[i++]);
    
    video_driver_ids = (char **) xine_xmalloc(sizeof(char *) * (i + 1));
    i = 0;
    video_driver_ids[i] = strdup("auto");
    while(vids[i]) {
      video_driver_ids[i + 1] = strdup(vids[i]);
      i++;
    }
    
    video_driver_ids[i + 1] = NULL;
    
    if(video_driver_id) {
      for(i = 0; video_driver_ids[i] != NULL; i++) {
	if(!strcasecmp(video_driver_id, video_driver_ids[i])) {
	  driver_num = i;
	  break;
	}
      }
    }
    gGui->vo_port = load_video_out_driver(driver_num, &win32_visual);
  }  
  
  {
    xine_cfg_entry_t  cfg_vo_entry;
    
    if(xine_config_lookup_entry(gGui->xine, "video.driver", &cfg_vo_entry)) {

      if(!strcasecmp(video_driver_ids[cfg_vo_entry.num_value], "dxr3")) {
     	xine_cfg_entry_t  cfg_entry;	
      }
    }
  }
  SAFE_FREE(video_driver_id);

  /* Audio out plugin */
  driver_num = -1;
  {
    const char *const *aids = xine_list_audio_output_plugins(gGui->xine);
    int                i = 0;
    
    while(aids[i++]);
    
    audio_driver_ids = (char **) xine_xmalloc(sizeof(char *) * (i + 2));
    i = 0;
    audio_driver_ids[i] = strdup("auto");
    audio_driver_ids[i + 1] = strdup("null");
    while(aids[i]) {
      audio_driver_ids[i + 2] = strdup(aids[i]);
      i++;
    }
    
    audio_driver_ids[i + 2] = NULL;
    
    if(audio_driver_id) {
      for(i = 0; audio_driver_ids[i] != NULL; i++) {
	if(!strcasecmp(audio_driver_id, audio_driver_ids[i])) {
	  driver_num = i;
	  break;
	}
      }
    }
    gGui->ao_port = load_audio_out_driver(driver_num);
  }
  SAFE_FREE(audio_driver_id);

  /* post_init(); */

  gGui->stream = xine_stream_new(gGui->xine, gGui->ao_port, gGui->vo_port);
  gGui->spu_stream = xine_stream_new(gGui->xine, NULL, gGui->vo_port);

#if (0)
  osd_init();

  /*
   * Setup logo.
   */
  gGui->logo_mode = 0;
  gGui->logo_has_changed = 0;
  gGui->logo_mrl = xine_config_register_string (gGui->xine, "gui.logo_mrl", XINE_LOGO_MRL,
	   					_("Logo mrl"),
	 					CONFIG_NO_HELP, 
						CONFIG_LEVEL_EXP,
						main_change_logo_cb, 
						CONFIG_NO_DATA);
#endif

  gGui->event_queue = xine_event_new_queue(gGui->stream);
  xine_event_create_listener_thread(gGui->event_queue, event_listener, this);

  xine_tvmode_init(gGui->xine);


#if 1
  xine_set_param(gGui->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL, audio_channel);
  xine_set_param(gGui->stream, XINE_PARAM_SPU_CHANNEL, spu_channel);
#else
  xine_set_param(gGui->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL, 0);
  xine_set_param(gGui->stream, XINE_PARAM_SPU_CHANNEL, 0);
#endif


#if 0
  /* Visual animation stream init */
  gGui->visual_anim.stream = xine_stream_new(gGui->xine, NULL, gGui->vo_port);
  gGui->visual_anim.event_queue = xine_event_new_queue(gGui->visual_anim.stream);
  gGui->visual_anim.current = 0;
  xine_event_create_listener_thread(gGui->visual_anim.event_queue, event_listener, this);
  xine_set_param(gGui->visual_anim.stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL, -2);
  xine_set_param(gGui->visual_anim.stream, XINE_PARAM_SPU_CHANNEL, -2);
#endif

#if (0)
  /* Playlist scanning feature stream */
  gGui->playlist.scan_stream = xine_stream_new(gGui->xine, gGui->ao_port, gGui->vo_port);
  xine_set_param(gGui->playlist.scan_stream, XINE_PARAM_SPU_CHANNEL, -2);
#endif  

  return true;
}

void _XINE_UI::EndXine()
{
	if( gui && gui->xine )
		xine_exit( gui->xine );
}

void _XINE_UI::error( LPSTR szfmt, ... )
{
    char tempbuff[ 256 ];
    *tempbuff = 0;
    wvsprintf(	&tempbuff[ strlen( tempbuff ) ], szfmt, ( char * )( &szfmt + 1 ) );
    MessageBox( 0, tempbuff, "Error", MB_ICONERROR | MB_OK | MB_APPLMODAL | MB_SYSTEMMODAL );
}

void _XINE_UI::warning( LPSTR szfmt, ... )
{
    char tempbuff[ 256 ];
    *tempbuff = 0;
    wvsprintf(	&tempbuff[ strlen( tempbuff ) ], szfmt, ( char * )( &szfmt + 1 ) );
    MessageBox( 0, tempbuff, "Warning", MB_ICONWARNING | MB_OK | MB_APPLMODAL | MB_SYSTEMMODAL );
}

PLAYITEM * _XINE_UI::PlaylistAdd( char * short_name, char * long_name, int type )
{
	if( playcount >= MAX_PLAYITEMS )
		return false;

	PLAYITEM * playitem = new PLAYITEM;

	playitem->mrl_short_name = strdup( short_name );
	playitem->mrl_long_name = strdup( long_name );
	playitem->mrl_type = type;

	playlist[ playcount ] = playitem;
	playcount++;

	return playitem;
}

bool _XINE_UI::PlaylistDel( int index )
{
	if( index >= playcount )
		return false;

	PLAYITEM * playitem = playlist[ index ];

	free( playitem->mrl_short_name );
	free( playitem->mrl_long_name );

	delete playitem;

	memcpy( &playlist[ index ], &playlist[ index + 1 ], ( playcount - index ) * sizeof( PLAYITEM * ) );
	playcount--;

	if( ( index < playindex ) && ( playcount > 0 ) )
		playindex--;

	if( index == playindex )
	{
		if( playindex >= playcount )
			playindex--;

		mrl_short_name	= 0;
		mrl_long_name	= 0;
		Stop();
	}

	return true;
}

bool _XINE_UI::Play( int newindex )
{
	int pos_stream, pos_time;
	int length_time;

	// if we are paused, just continue playing

	if( mode == XINE_STATUS_PLAY )
	{
		SetSpeed( XINE_SPEED_NORMAL );
		return true;
	}

	// make sure the playindex is valid

	if( ( newindex >= 0 ) && ( newindex < playcount ) )
		playindex = newindex;
	else
		return false;

	// is this different mrl then we are already playing

	if( newindex == playindex )
	{
		// its the same, play from current time

		HWND htimebar = GetDlgItem( hctrlwnd, ID_TIMEBAR );
		mrl_time_current = SendMessage( htimebar, TBM_GETPOS, (WPARAM) 0, (LPARAM) 0 );
	}
	else
	{
		// its different, rewind and play from 0

		mrl_time_current = 0;
	}

	// store our new mrl info

	mrl_short_name	= playlist[ playindex ]->mrl_short_name;
	mrl_long_name	= playlist[ playindex ]->mrl_long_name;
	mrl_type		= playlist[ playindex ]->mrl_type;

	// play our mrl
    if(!xine_open(gGui->stream, (const char *)mrl_long_name)) {
      return 0;
	}

    if(xine_play(gGui->stream, 0, mrl_time_current))
	{
		mrl_time_length = 0;
		if (xine_get_pos_length (gGui->stream, &pos_stream, &pos_time, &length_time))
		{
			mrl_time_length = length_time/1000;
		}

		/*mrl_time_length = xine_get_stream_length( gGui->stream )/1000;*/

		HWND htimebar = GetDlgItem( hctrlwnd, ID_TIMEBAR );
		SendMessage( htimebar, TBM_SETRANGE, (WPARAM) TRUE, (LPARAM) MAKELONG( 0, mrl_time_length ) );
		mode = XINE_STATUS_PLAY;

		// start our update loop

		UpdateLoop();
	}

	return true;
}

bool _XINE_UI::Stop()
{
	mode = XINE_STATUS_STOP;
	mrl_time_current = 0;
	UpdateCtrl();
	UpdatePanel();
	xine_stop( gGui->stream );

	return true;
}

bool _XINE_UI::SetSpeed( int speed )
{
	/*xine_set_speed( gGui->stream, speed );*/
	xine_set_param(gGui->stream, XINE_PARAM_SPEED, speed);
	return true;
}

int _XINE_UI::GetSpeed()
{
	/*return xine_get_speed( gGui->stream );*/
	return 	xine_get_param(gGui->stream, XINE_PARAM_SPEED);

}

bool _XINE_UI::SetTime( int time )
{
	if( mode == XINE_STATUS_PLAY )
	{
      if(!xine_open(gGui->stream, (const char *)mrl_long_name)) {
        return false;
	  }

      xine_play(gGui->stream, 0, time);


	  mrl_time_current = time;
	}

	return true;
}

bool _XINE_UI::SelectSpuChannel( int channel )
{
	xine_set_param(gGui->stream, XINE_PARAM_SPU_CHANNEL, channel);
	return true;
}

bool _XINE_UI::SelectAudioChannel( int channel )
{
    xine_set_param(gGui->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL, channel);
	return true;
}

bool _XINE_UI::SetVolume( int volume )
{
    xine_set_param(gGui->stream, XINE_PARAM_AUDIO_VOLUME, volume);
	return true;
}

bool _XINE_UI::SetMute( bool mute )
{
    xine_set_param(gGui->stream, XINE_PARAM_AUDIO_MUTE, mute);
	return true;
}

bool _XINE_UI::DriverMessage( int type, void * param )
{
	gGui->vo_port->driver->gui_data_exchange( gGui->vo_port->driver, type, param );
	return true;
}

