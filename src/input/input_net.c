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
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/time.h>

#include "xine_internal.h"
#include "monitor.h"
#include "input_plugin.h"

#if !defined(NDELAY) && defined(O_NDELAY)
#define	FNDELAY	O_NDELAY
#endif


static uint32_t xine_debug;

#define NET_BS_LEN 2324

typedef struct {
  input_plugin_t   input_plugin;
  
  int              fh;
  char            *mrl;
  config_values_t *config;

  off_t            curpos;

} net_input_plugin_t;

/* **************************************************************** */
/*                       Private functions                          */
/* **************************************************************** */


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
/* **************************************************************** */
/*                          END OF PRIVATES                         */
/* **************************************************************** */

/*
 *
 */
static int net_plugin_open (input_plugin_t *this_gen, char *mrl) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;
  char *filename;
  char *pptr;
  int port = 7658;

  this->mrl = mrl;

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

  this->fh = host_connect(filename, port);
  this->curpos = 0;

  if (this->fh == -1) {
    return 0;
  }

  return 1;
}

/*
 *
 */
static off_t net_plugin_read (input_plugin_t *this_gen, 
				      char *buf, off_t nlen) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;
  off_t n;

  n = read (this->fh, buf, nlen);

  if (n > 0)
    this->curpos += n;

  return n;
}

/*
 *
 */
static off_t net_plugin_get_length (input_plugin_t *this_gen) {

  return 0;
}

/*
 *
 */
static uint32_t net_plugin_get_capabilities (input_plugin_t *this_gen) {

  return INPUT_CAP_NOCAP;
}

/*
 *
 */
static uint32_t net_plugin_get_blocksize (input_plugin_t *this_gen) {

  return NET_BS_LEN;
;
}

/*
 *
 */
static off_t net_plugin_get_current_pos (input_plugin_t *this_gen){
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  return this->curpos;
}

/*
 *
 */
static int net_plugin_eject_media (input_plugin_t *this_gen) {
  return 1;
}

/*
 *
 */
static void net_plugin_close (input_plugin_t *this_gen) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  close(this->fh);
  this->fh = -1;
}

/*
 *
 */
static void net_plugin_stop (input_plugin_t *this_gen) {

  net_plugin_close(this_gen);
}

/*
 *
 */
static char *net_plugin_get_description (input_plugin_t *this_gen) {
  return "net input plugin as shipped with xine";
}

/*
 *
 */
static char *net_plugin_get_identifier (input_plugin_t *this_gen) {
  return "TCP";
}

/*
 *
 */
static char* net_plugin_get_mrl (input_plugin_t *this_gen) {
  net_input_plugin_t *this = (net_input_plugin_t *) this_gen;

  return this->mrl;
}

/*
 *
 */
static int net_plugin_get_optional_data (input_plugin_t *this_gen, 
					 void *data, int data_type) {

  return INPUT_OPTIONAL_UNSUPPORTED;
}

/*
 *
 */
input_plugin_t *init_input_plugin (int iface, config_values_t *config) {

  net_input_plugin_t *this;

  xine_debug = config->lookup_int (config, "xine_debug", 0);

  if (iface != 3) {
    printf("net input plugin doesn't support plugin API version %d.\n"
	   "PLUGIN DISABLED.\n"
	   "This means there's a version mismatch between xine and this input"
	   "plugin.\nInstalling current input plugins should help.\n",
	   iface);
    return NULL;
  }

  this = (net_input_plugin_t *) xmalloc(sizeof(net_input_plugin_t));
  
  this->input_plugin.interface_version = INPUT_PLUGIN_IFACE_VERSION;
  this->input_plugin.get_capabilities  = net_plugin_get_capabilities;
  this->input_plugin.open              = net_plugin_open;
  this->input_plugin.read              = net_plugin_read;
  this->input_plugin.read_block        = NULL;
  this->input_plugin.seek              = NULL;
  this->input_plugin.get_current_pos   = net_plugin_get_current_pos;
  this->input_plugin.get_length        = net_plugin_get_length;
  this->input_plugin.get_blocksize     = net_plugin_get_blocksize;
  this->input_plugin.get_dir           = NULL;
  this->input_plugin.eject_media       = net_plugin_eject_media;
  this->input_plugin.get_mrl           = net_plugin_get_mrl;
  this->input_plugin.close             = net_plugin_close;
  this->input_plugin.stop              = net_plugin_stop;
  this->input_plugin.get_description   = net_plugin_get_description;
  this->input_plugin.get_identifier    = net_plugin_get_identifier;
  this->input_plugin.get_autoplay_list = NULL;
  this->input_plugin.get_optional_data = net_plugin_get_optional_data;
  this->input_plugin.handle_input_event= NULL;
  this->input_plugin.is_branch_possible= NULL;

  this->fh      = -1;
  this->mrl     = NULL;
  this->config  = config;
  this->curpos  = 0;
  
  return (input_plugin_t *) this;
}
