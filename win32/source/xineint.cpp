
#include "xineui.h"

static vo_driver_t * load_video_out_driver( char * video_driver_id, config_values_t * config, win32_visual_t * win32_visual )
{
	vo_driver_t * vo_driver = 0;

   /*
	* Setting default (configfile stuff need registering before updating, etc...).
	*/

	char ** driver_ids = xine_list_video_output_plugins( VISUAL_TYPE_WIN32 );
	int		i;
    
	/* video output driver auto-probing */

	i = 0;

	while( driver_ids[i] )
	{
		video_driver_id = driver_ids[i];
      
//		printf (_("main: probing <%s> video output plugin\n"), video_driver_id);

		vo_driver = xine_load_video_output_plugin( config, video_driver_id, VISUAL_TYPE_WIN32, (void *) win32_visual );

		if( vo_driver )
		{
			if(driver_ids)
				free(driver_ids);

			config->update_string( config, "video.driver", video_driver_id );
			return vo_driver;
		}
     
		i++;
	}
      
//	Error( 0, "main: all available video drivers failed.\n");
	return 0;
}


static ao_driver_t * load_audio_out_driver( char * audio_driver_id, config_values_t * config )
{
	ao_driver_t	* ao_driver = 0;
  
   /*
	* Setting default (configfile stuff need registering before updating, etc...).
	*/

	char * default_driver = config->register_string( config, "audio.driver", "auto", "audio driver to use", NULL, NULL, NULL );
  
   /*
	* if no audio driver was specified at the command line, 
	* look up audio driver id in the config file
	*/
	
	if( !audio_driver_id ) 
		audio_driver_id = default_driver;

	/* probe ? */

	if( !strncmp( audio_driver_id, "auto", 4 ) )
	{
		char **driver_ids = xine_list_audio_output_plugins();
		int i = 0;

//		Error( 0,  "main: probing audio drivers...\n" );
    
		while( driver_ids[i] != NULL )
		{
			audio_driver_id = driver_ids[i];
//			Error( 0, "main: trying to autoload '%s' audio driver :", driver_ids[i] );
      		ao_driver = xine_load_audio_output_plugin( config, driver_ids[i] );

			if( ao_driver )
			{
				printf ("main: ...worked, using '%s' audio driver.\n", driver_ids[i] );
				config->update_string( config, "audio.driver", audio_driver_id );

				return ao_driver;
			}

			i++;
		}

//		Error( 0, "main: audio driver probing failed => no audio output\n" );
	    
		config->update_string( config, "audio.driver", "null" );

	}
	else
	{
		/* don't want to load an audio driver ? */
	    if( !strnicmp( audio_driver_id, "NULL", 4 ) )
		{
//			Error( 0,"main: not using any audio driver (as requested).\n");
			config->update_string( config, "audio.driver", "null" );

		}
		else
		{

			ao_driver = xine_load_audio_output_plugin( config, audio_driver_id );

			if( !ao_driver )
			{
//				Error( 0, "main: the specified audio driver '%s' failed\n", audio_driver_id );
				exit(1);
			}

			config->update_string( config, "audio.driver", audio_driver_id );
		}
	}

	return ao_driver;
}

xine_t * xine_startup( config_values_t * config, win32_visual_t * win32_visual )
{
	vo_driver_t *	vo_driver;
	ao_driver_t *	ao_driver;

    int             audio_channel = -1;
    int             spu_channel = -1;

	xine_t		*	xine;
    xine_stream_t  *stream;
    xine_stream_t  *spu_stream;

   /*
	* Check xine library version 
	*/

#if (1)
	if(!xine_check_version(1, 0, 0)) {
		int major, minor, sub;

		xine_get_version (&major, &minor, &sub);
		fprintf(stderr, _("Require xine library version 1.0.0, found %d.%d.%d.\n"),
			major, minor,sub);
		exit(1);
	}
#else
	if( !xine_check_version( 0, 9, 4 ) )
	{
//		Error( 0, "require xine library version 0.9.4, found %d.%d.%d.\n", 
//		xine_get_major_version(), xine_get_minor_version(), xine_get_sub_version() );
		return false;
	}
#endif
	
   /*
	* generate and init a config "object"
	*/	
	char * cfgfile = "config";
	char * configfile = ( char * ) xine_xmalloc( ( strlen( ( xine_get_homedir( ) ) ) + strlen( cfgfile ) ) +2 );
	sprintf( configfile, "%s/%s", ( xine_get_homedir() ), cfgfile );

	/*config = config_file_init( configfile );*/
    xine = xine_new();
    xine_config_load(xine, configfile);

	/*
	* Try to load video output plugin, by stored name or probing
	*/
	
	vo_driver = load_video_out_driver( "vo_directx", config, win32_visual );

   /*
	* Try to load audio output plugin, by stored name or probing
	*/

	ao_driver = load_audio_out_driver( "auto", config );

   /*
	* xine init
	*/
	stream = xine_stream_new(xine, ao_driver, vo_driver);
    spu_stream = xine_stream_new(xine, NULL, vo_driver);

    osd_init();	

    event_queue = xine_event_new_queue(gGui->stream);
    xine_event_create_listener_thread(gGui->event_queue, event_listener, NULL);

    xine_tvmode_init(xine);
  
	/* TC - We need to allow switches on the command line for this stuff! */
    xine_set_param(stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL, audio_channel);
    xine_set_param(stream, XINE_PARAM_SPU_CHANNEL, spu_channel);

#if (0)
    /* Visual animation stream init */
    gGui->visual_anim.stream = xine_stream_new(gGui->xine, NULL, gGui->vo_port);
    gGui->visual_anim.event_queue = xine_event_new_queue(gGui->visual_anim.stream);
    gGui->visual_anim.current = 0;
    xine_event_create_listener_thread(gGui->visual_anim.event_queue, event_listener, NULL);
    xine_set_param(gGui->visual_anim.stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL, -2);
    xine_set_param(gGui->visual_anim.stream, XINE_PARAM_SPU_CHANNEL, -2);

    /* Playlist scanning feature stream */
    gGui->playlist.scan_stream = xine_stream_new(gGui->xine, gGui->ao_port, gGui->vo_port);
    xine_set_param(gGui->playlist.scan_stream, XINE_PARAM_SPU_CHANNEL, -2);
#endif  
	
	return xine;
}