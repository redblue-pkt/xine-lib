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
 * WIN32 PORT,
 * by Matthew Grooms <elon@altavista.com>
 *
 * timer.c - Missing unix timer functions
 *
 */

#include <sys/timeb.h>
#include <signal.h>
#include "timer.h"


#ifndef SIGALRM
#define SIGALRM 14
#endif


int gettimeofday(struct timeval *tv, struct timezone *tz) {
  struct timeb timebuffer;

  ftime(&timebuffer);
  tv->tv_sec = timebuffer.time; 
  tv->tv_usec = timebuffer.millitm * 1000; 

  return 0;
}


/*
	These functions are designed to mimick
	a subset of itimer for use with the 
	alarm signal on win32. This is just
	enough for xine to work.
*/

static HANDLE sigalarm = 0;

int setitimer( int which, struct itimerval * value, struct itimerval *ovalue )
{
	long int miliseconds;
		
	if( !sigalarm )
		sigalarm = CreateEvent( 0, FALSE, TRUE, "SIGALARM" );

    miliseconds = value->it_value.tv_usec / 1000;

	timeSetEvent( miliseconds, 0, ( LPTIMECALLBACK ) sigalarm, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_PULSE );

	return 0;
}

/*
	Wait for sigalarm to wake the thread
*/

int pause( void )
{
	WaitForSingleObject( sigalarm, INFINITE );

	return 0;
}

int nanosleep( const struct timespec * rqtp, struct timespec * rmtp )
{
	Sleep( rqtp->tv_nsec / 1000000 );

	return 0;
}

unsigned int sleep( unsigned int seconds )
{
	Sleep( seconds * 1000 );
	return 0;
}

static UINT alarmid = 0;
void CALLBACK AlarmCallback(UINT wTimerID, 
							UINT msg, 
							DWORD dwUser, 
							DWORD dw1, 
							DWORD dw2)
{
	if (alarmid) {
		alarmid = 0;
		raise(SIGALRM);
	}
} 

int alarm( int sec )
{
	long int miliseconds;

	if (sec) {
	  if (!alarmid) {
	    miliseconds = sec * 1000;
		timeSetEvent( miliseconds, 0, ( LPTIMECALLBACK ) AlarmCallback, 0, TIME_ONESHOT | TIME_CALLBACK_FUNCTION );
	  }
	}
	else {
	  if (alarmid) {
	    timeKillEvent(alarmid);
		alarmid = 0;
	  }
	}

	return 0;
}
