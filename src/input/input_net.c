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
 * Read from a tcp network stream over a lan (put a tweaked mp1e encoder the
 * other end and you can watch tv anywhere in the house ..)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/time.h>

#include "xine.h"
#include "monitor.h"
#include "input_plugin.h"

static uint32_t xine_debug;

static int input_file_handle;

static int host_connect_attempt(struct in_addr ia, int port) {
	int s=socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	
	struct sockaddr_in sin;
	
	fd_set wfd;
	struct timeval tv;
	
	if(s==-1)
	{
		perror("socket");
		return -1;
	}
	
	if(fcntl(s, F_SETFL, FNDELAY)==-1)
	{
		perror("nonblocking");
		close(s);
		return -1;
	}

	sin.sin_family = AF_INET;	
	sin.sin_addr   = ia;
	sin.sin_port   = htons(port);
	
	if(connect(s, (struct sockaddr *)&sin, sizeof(sin))==-1 && errno != EINPROGRESS)
	{
		perror("connect");
		close(s);
		return -1;
	}	
	
	tv.tv_sec = 60;		/* We use 60 second timeouts for now */
	tv.tv_usec = 0;
	
	FD_ZERO(&wfd);
	FD_SET(s, &wfd);
	
	switch(select(s+1, NULL, &wfd, NULL, &tv))
	{
		case 0:
			/* Time out */
			close(s);
			return -1;
		case -1:
			/* Ermm.. ?? */
			perror("select");
			close(s);
			return -1;
	}
	
	return s;
}

static int host_connect(const char *host, int port) {
	struct hostent *h;
	int i;
	int s;
	
	h=gethostbyname(host);
	if(h==NULL)
	{
		fprintf(stderr,"unable to resolve '%s'.\n", host);
		return -1;
	}
	
	
	for(i=0; h->h_addr_list[i]; i++)
	{
		struct in_addr ia;
		memcpy(&ia, h->h_addr_list[i],4);
		s=host_connect_attempt(ia, port);
		if(s != -1)
			return s;
	}
	fprintf(stderr, "unable to connect to '%s'.\n", host);
	return -1;
}

static void input_plugin_init (void) {
  input_file_handle = -1;
}

static int input_plugin_open (const char *mrl) {

  char *filename;
  char *pptr;
  int port = 7658;

  if (!strncasecmp (mrl, "tcp:",4))
    filename = (char *) &mrl[4];
  else
    return 0;
    
  if(strncmp(filename, "//", 2)==0)
  	filename+=2;

  xprintf (VERBOSE|INPUT, "Opening >%s<\n", filename);
  
  pptr=strrchr(filename, ':');
  if(pptr)
  {
  	*pptr++=0;
  	sscanf(pptr,"%d", &port);
  }

  input_file_handle = host_connect(filename, port);

  if (input_file_handle == -1) {
    return 0;
  }

  return 1;
}

static uint32_t input_plugin_read (char *buf, uint32_t nlen) {
  return read (input_file_handle, buf, nlen);
}

static off_t input_plugin_seek (off_t offset, int origin) {

  return -1;
}

static off_t input_plugin_get_length (void) {
  return 0;
}

static uint32_t input_plugin_get_capabilities (void) {
  return 0;
}

static uint32_t input_plugin_get_blocksize (void) {
  return 2324;
}

static int input_plugin_eject (void) {
  return 1;
}

static void input_plugin_close (void) {
  close(input_file_handle);
  input_file_handle = -1;
}

static char *input_plugin_get_identifier (void) {
  return "TCP";
}

static int input_plugin_is_branch_possible (const char *next_mrl) {
  return 0;
}

static input_plugin_t plugin_op = {
  NULL,
  NULL,
  input_plugin_init,
  input_plugin_open,
  input_plugin_read,
  input_plugin_seek,
  input_plugin_get_length,
  input_plugin_get_capabilities,
  NULL,
  input_plugin_get_blocksize,
  input_plugin_eject,
  input_plugin_close,
  input_plugin_get_identifier,
  NULL,
  input_plugin_is_branch_possible,
  NULL
};

input_plugin_t *input_plugin_getinfo(uint32_t dbglvl) {

  xine_debug = dbglvl;

  return &plugin_op;
}
