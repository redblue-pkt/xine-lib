#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <conio.h>
#include "utils.h"
#include "xineint.h"


void event_listener( void * user_data, xine_event_t * xine_event )
{
	printf ("main: event listener, got event type %d\n", xine_event->type);
  
	switch( xine_event->type )
	{ 
		case XINE_EVENT_UI_CHANNELS_CHANGED:
			printf( "xine-event : XINE_EVENT_UI_CHANNELS_CHANGED\n" );
		break;

		case XINE_EVENT_UI_SET_TITLE:
			printf( "xine-event : XINE_EVENT_UI_SET_TITLE\n" );
		break;

		case XINE_EVENT_UI_PLAYBACK_FINISHED:
			printf( "xine-event : XINE_EVENT_PLAYBACK_FINISHED\n" );
		break;

#if 0
		case XINE_EVENT_NEED_NEXT_MRL:
			printf( "xine-event : XINE_EVENT_NEED_NEXT_MRL\n" );
		break;

		case XINE_EVENT_BRANCHED:
			printf( "xine-event : XINE_EVENT_BRANCHED\n" );
		break;
#endif
	}      
}

int main( int argc, char *argv[ ], char *envp[ ] )
{
	win32_visual_t win32_visual;

	// print welcome

	printf( "xine win32 console app v 0.1\n" );

	// init xine libs

	config_values_t config;
	memset( &win32_visual, 0, sizeof( win32_visual ) );
	xine_t * xine = xine_startup( &config, &win32_visual );

	if( !argv[1] )
		printf( "xineconsole error : no media input file specified\n" );
	else
		xine_play( xine, argv[1], 0, 0 );

	xine_register_event_listener( xine, event_listener, &win32_visual );

	xine_set_audio_property( xine, AO_PROP_MUTE_VOL, 1 );

	getch();

	return 0;
}
