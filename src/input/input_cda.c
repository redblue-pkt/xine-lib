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
 * $Id: input_cda.c,v 1.2 2001/12/08 03:01:40 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#include <netdb.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <pwd.h>
#include <sys/types.h>

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#ifdef HAVE_LINUX_CDROM_H
# include <linux/cdrom.h>
#endif
#ifdef HAVE_SYS_CDIO_H
# include <sys/cdio.h>
/* TODO: not clean yet */
# if defined (__FreeBSD__)
#  include <sys/cdrio.h>
# endif
#endif
#if ! defined (HAVE_LINUX_CDROM_H) && ! defined (HAVE_SYS_CDIO_H)
#error "you need to add cdrom / CDA support for your platform to input_cda and configure.in"
#endif
#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"

/*
#define DEBUG_DISC
#define DEBUG_POS
*/

#define CDROM          "/dev/cdaudio"

#define CDDB_SERVER    "freedb.freedb.org"
#define CDDB_PORT      8880

/* for type */
#define CDAUDIO   1
#define CDDATA    2

/* for status */
#define CDA_NO       1
#define CDA_PLAY     2
#define CDA_PAUSE    3
#define CDA_STOP     4
#define CDA_EJECT    5
#define CDA_COMPLETE 6

#define CDA_BLOCKSIZE 75

typedef struct {
  int           type;
  int           length;
  int           start;
  int           track;
  char         *title;
} trackinfo_t;

typedef struct {
  int           fd;
  char         *device_name;
  int           cur_track;
  int           cur_pos;
  int           status;
  int           num_tracks;
  int           first_track;
  int           length;
  unsigned long disc_id;
  char         *title;
  char         *category;
  char         *cdiscid;
  char          ui_title[256];
  trackinfo_t  *track;
} cdainfo_t;

typedef struct {

  input_plugin_t         input_plugin;
  
  config_values_t       *config;
  xine_t                *xine;

  uint32_t               speed;

  char                  *mrl;
  
  struct {
    char                *server;
    int                  port;
    int                  fd;
  } cddb;

  cdainfo_t             *cda;

  char                  *filelist[100];
  int                    mrls_allocated_entries;
  mrl_t                **mrls;
  
} cda_input_plugin_t;

/* Func proto */
static void _cda_stop_cd(cdainfo_t *);

/*
 * ******************************** PRIVATES ***************************************
 */

/*
 * Return user name.
 */
static char *_cda_get_username_safe(void) {
  char          *un;
  struct passwd *pw;

  pw = getpwuid(geteuid());

  un = strdup(((pw && pw->pw_name) ? strdup(pw->pw_name) : "unknown"));

  return un;
}

/*
 * Return host name.
 */
static char *_cda_get_hostname_safe(void) {
  char *hn;
  char  buf[2048];

  memset(&buf, 0, sizeof(buf));
  if(gethostname(&buf[0], sizeof(buf)) == 0)
    hn = strdup(buf);
  else
    hn = strdup("unknown");
  
  return hn;
}

/*
 * ************* CDDB *********************
 */
/*
 * Small sighandler ;-)
 */
static void die(int signal) {
  exit(signal);
}

/*
 * Read from socket, fill char *s, return size length.
 */
static int _cda_cddb_socket_read(char *s, int size, int socket) {
  int i = 0, r;
  char c;
  
  alarm(20);
  signal(SIGALRM, die);
  
  while((r=recv(socket, &c, 1, 0)) != 0) {
    if(c == '\r' || c == '\n')
      break;
    if(i > size)
      break;
    s[i] = c;
    i++;
  }
  s[i] = '\n';
  s[i+1] = 0;
  recv(socket, &c, 1, 0);
  
  alarm(0);
  signal(SIGALRM, SIG_DFL);
  
  s[i] = 0;
  return r;
}

/*
 * Send a command to socket
 */
static int _cda_cddb_send_command(cda_input_plugin_t *this, char *cmd) {
  
  if((this == NULL) || (this->cddb.fd < 0) || (cmd == NULL))
    return -1;
  
  return (send(this->cddb.fd, cmd, strlen(cmd), 0));
}

/*
 * Handle return code od a command result.
 */
static int _cda_cddb_handle_code(char *buf) {
  int  rcode, fdig, sdig, tdig;
  int  err = -1;

  if(sscanf(buf, "%d", &rcode) == 1) {

    fdig = rcode / 100;
    sdig = (rcode - (fdig * 100)) / 10;
    tdig = (rcode - (fdig * 100) - (sdig * 10));

    /*
    printf(" %d--\n", fdig);
    printf(" -%d-\n", sdig);
    printf(" --%d\n", tdig);
    */
    switch(fdig) {
    case 1:
      //      printf("Informative message\n");
      err = 0;
      break;
    case 2:
      //      printf("Command OK\n");
      err = 0;
      break;
    case 3:
      //      printf("Command OK so far, continue\n");
      err = 0;
      break;
    case 4:
      //      printf("Command OK, but cannot be performed for some specified reasons\n");
      err = -1;
      break;
    case 5:
      //      printf("Command unimplemented, incorrect, or program error\n");
      err = -1;
      break;
    default:
      //      printf("Unhandled case %d\n", fdig);
      err = -1;
      break;
    }

    switch(sdig) {
    case 0:
      //      printf("Ready for further commands\n");
      err = 0;
      break;
    case 1:
      //      printf("More server-to-client output follows (until terminating marker)\n");
      err = 0;
      break;
    case 2:
      //      printf("More client-to-server input follows (until terminating marker)\n");
      err = 0;
      break;
    case 3:
      //      printf("Connection will close\n");
      err = -1;
      break;
    default:
      //      printf("Unhandled case %d\n", sdig);
      err = -1;
      break;
    }

    if(err >= 0)
      err = rcode;
  }
  
  return err;
}

/*
 * Try to talk with CDDB server (to retrieve disc/tracks titles).
 */
static void _cda_cddb_retrieve(cda_input_plugin_t *this) {
  char  buffer[2048];
  char *username, *hostname;
  int   err, i;

  if((this == NULL) || (this->cddb.fd < 0))
    return;

  username = _cda_get_username_safe();
  hostname = _cda_get_hostname_safe();
  
  memset(&buffer, 0, sizeof(buffer));

  /* Get welcome message */
  if(_cda_cddb_socket_read(&buffer[0], 2047, this->cddb.fd)) {
    if((err = _cda_cddb_handle_code(buffer)) >= 0) {
      /* send hello */
      memset(&buffer, 0, sizeof(buffer));
      sprintf(buffer, "cddb hello %s %s xine %s\n", username, hostname, VERSION);
      if((err = _cda_cddb_send_command(this, buffer)) > 0) {
	/* Get answer from hello */
	memset(&buffer, 0, sizeof(buffer));
	if(_cda_cddb_socket_read(&buffer[0], 2047, this->cddb.fd)) {
	  /* Parse returned code */
	  if((err = _cda_cddb_handle_code(buffer)) >= 0) {
	    /* We are logged, query disc */
	    memset(&buffer, 0, sizeof(buffer));
	    sprintf(buffer, "cddb query %08lx %d ", this->cda->disc_id, this->cda->num_tracks);
	    for(i = 0; i < this->cda->num_tracks; i++) {
	      sprintf(buffer, "%s%d ", buffer, this->cda->track[i].start);
	    }
	    sprintf(buffer, "%s%d\n", buffer, this->cda->track[i].length);
	    if((err = _cda_cddb_send_command(this, buffer)) > 0) {
	      memset(&buffer, 0, sizeof(buffer));
	      if(_cda_cddb_socket_read(&buffer[0], 2047, this->cddb.fd)) {
		/* Parse returned code */
		if((err = _cda_cddb_handle_code(buffer)) == 200) {
		  /* Disc entry exist */
		  char *m = NULL, *p = buffer;
		  int   f = 0;

		  while((f <= 2) && ((m = xine_strsep(&p, " ")) != NULL)) {
		    if(f == 1)
		      this->cda->category = strdup(m);
		    else if(f == 2)
		      this->cda->cdiscid = strdup(m);
		    f++;
		  }
		}
		
		/* Now, grab track titles */
		memset(&buffer, 0, sizeof(buffer));
		sprintf(buffer, "cddb read %s %s\n", this->cda->category, this->cda->cdiscid);
		if((err = _cda_cddb_send_command(this, buffer)) > 0) {
		  /* Get answer from read */
		  memset(&buffer, 0, sizeof(buffer));
		  if(_cda_cddb_socket_read(&buffer[0], 2047, this->cddb.fd)) {
		    /* Great, now we will have track titles */
		    if((err = _cda_cddb_handle_code(buffer)) == 210) {
		      char           buf[2048];
		      unsigned char *pt;
		      int            tnum;
		      
		      while(strcmp(buffer, ".")) {
			memset(&buffer, 0, sizeof(buffer));
			_cda_cddb_socket_read(&buffer[0], 2047, this->cddb.fd);
			if(sscanf(buffer, "DTITLE=%s", &buf[0]) == 1) {
			  pt = strrchr(buffer, '=');
			  if(pt) pt++;
			  this->cda->title = strdup(pt);
			}
			else if(sscanf(buffer, "TTITLE%d=%s", &tnum, &buf[0]) == 2) {
			  pt = strrchr(buffer, '=');
			  if(pt) pt++;
			  this->cda->track[tnum].title = strdup(pt);
			}
		      }
		    }
		  }
		}
	      }
	    }
	  }	  
	}
      }
    }
  }
  
  free(username);
  free(hostname);
}

/*
 * Open a socket.
 */
static int _cda_cddb_socket_open(cda_input_plugin_t *this) {
  int                 sockfd;
  struct hostent     *he;
  struct sockaddr_in  their_addr;

  if(this == NULL)
    return -1;
  
  this->cddb.server = 
    this->config->register_string(this->config, "input.cda_cddb_server", CDDB_SERVER,
				  "cddbp server name", NULL, NULL, NULL);
  
  this->cddb.port = 
    this->config->register_num(this->config, "input.cda_cddb_port", CDDB_PORT,
			       "cddbp server port", NULL, NULL, NULL);
  

  alarm(15);
  signal(SIGALRM, die);
  if((he=gethostbyname(this->cddb.server)) == NULL) {
    alarm(0);
    signal(SIGALRM, SIG_DFL);
    return -1;
  }
  
  if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    alarm(0);
    signal(SIGALRM, SIG_DFL);
    return -1;
  }
  
  their_addr.sin_family = AF_INET;
  their_addr.sin_port = htons(this->cddb.port);
  their_addr.sin_addr = *((struct in_addr *)he->h_addr);
  memset(&(their_addr.sin_zero), 0, 8);
  
  if(connect(sockfd, (struct sockaddr *)&their_addr, sizeof(struct sockaddr)) == -1) {
    alarm(0);
    signal(SIGALRM, SIG_DFL);
    return -1;
  }
  alarm(0);
  signal(SIGALRM, SIG_DFL);

  return sockfd;
}

/*
 * Close the socket
 */
static void _cda_cddb_socket_close(cda_input_plugin_t *this) {
  
  if((this == NULL) || (this->cddb.fd < 0))
    return;

  close(this->cddb.fd);
  this->cddb.fd = -1;
}

/*
 * 
 */
static unsigned int _cda_cddb_sum(int n) {
  unsigned int ret = 0;
  
  while(n > 0) {
    ret += (n % 10);
    n /= 10;
  }
  return ret;
}

/*
 * Compute cddb disc compliant id
 */
static unsigned long _cda_calc_cddb_id(cdainfo_t *cda) {
  int i, tsum = 0;
  
  if(cda == NULL || (cda->num_tracks <= 0))
    return 0;
  
  for(i = 0; i < cda->num_tracks; i++)
    tsum += _cda_cddb_sum((cda->track[i].start / 75));
  
  return ((tsum % 0xff) << 24 
	  | (cda->track[cda->num_tracks].length - (cda->track[0].start / 75)) << 8 
	  | cda->num_tracks);
}

/*
 * return cbbd disc id.
 */
static unsigned long _cda_get_cddb_id(cdainfo_t *cda) {

  if(cda == NULL || (cda->num_tracks <= 0))
    return 0;

  return _cda_calc_cddb_id(cda);
}

/*
 * grab (try) titles from cddb server.
 */
static void _cda_cbbd_grab_infos(cda_input_plugin_t *this) {

  if(this == NULL)
    return;

  this->cddb.fd = _cda_cddb_socket_open(this);
  if(this->cddb.fd >= 0) {
    printf("input_cda: server '%s:%d' successfuly connected.\n", 
	   this->cddb.server, this->cddb.port);

    _cda_cddb_retrieve(this);
  }
  else
    printf("input_cda: opening server '%s:%d' failed: %s\n", 
	   this->cddb.server, this->cddb.port, strerror(errno));
  
  _cda_cddb_socket_close(this);
}

/*
 * **************** CDDB END *********************
 */

/*
 * Get CDA status (pos, cur track, status)
 * This function was grabbed and adapted from workbone (thanks to this team).
 */
static int _cda_get_status_cd(cdainfo_t *cda) {
  struct cdrom_subchnl	sc;
  int                   cur_pos_abs = 0;
  int                   cur_frame = 0;
  int                   cur_track;
  int                   cur_ntracks;
  int                   cur_cdlen;
  int                   cur_index;
  int                   cur_pos_rel = 0;
  int                   cur_tracklen;
  
  if(cda == NULL || cda->fd < 0)
    return 0;
  
  cur_track      = cda->cur_track;
  cur_ntracks    = cda->num_tracks;
  cur_cdlen      = cda->length;

  sc.cdsc_format = CDROM_MSF;
  
  if(ioctl(cda->fd, CDROMSUBCHNL, &sc)) {
    fprintf(stderr, "input_cda: ioctl(CDROMSUBCHNL) failed: %s.\n", strerror(errno));
    return 0;
  }
  
  switch (sc.cdsc_audiostatus) {
  case CDROM_AUDIO_PLAY:
    cda->status = CDA_PLAY;
    
  __get_pos:
    
    cur_pos_abs = sc.cdsc_absaddr.msf.minute * 60 + sc.cdsc_absaddr.msf.second;
    cur_frame   = cur_pos_abs * 75 + sc.cdsc_absaddr.msf.frame;
    
    if(cur_track < 1 
       || cur_frame < cda->track[cur_track-1].start 
       || cur_frame >= (cur_track >= cur_ntracks ? 
			(cur_cdlen + 1) * 75 :
			cda->track[cur_track].start)) {
      cur_track = 0;
      
      while (cur_track < cur_ntracks && cur_frame >= cda->track[cur_track].start)
	cur_track++;
    }

    if(cur_track >= 1 && sc.cdsc_trk > cda->track[cur_track-1].track)
      cur_track++;
    
    cur_index = sc.cdsc_ind;
    
  __get_posrel:
    
    if(cur_track >= 1 && cur_track <= cur_ntracks) {
      cur_pos_rel = (cur_frame - cda->track[cur_track-1].start) / 75;
      if(cur_pos_rel < 0)
	cur_pos_rel = -cur_pos_rel;
    }
    
    if(cur_pos_abs < 0)
      cur_pos_abs = cur_frame = 0;
    
    if(cur_track < 1)
      cur_tracklen = cda->length;
    else
      cur_tracklen = cda->track[cur_track-1].length;
    break;
    
  case CDROM_AUDIO_PAUSED:
    cda->status = CDA_PAUSE;
    goto __get_pos;
    break;
    
  case CDROM_AUDIO_COMPLETED:
    cda->status = CDA_COMPLETE;
    break;
    
  case CDROM_AUDIO_NO_STATUS:
    cda->status = CDA_STOP;
    goto __get_posrel;
  }

  if(cur_track == cda->cur_track)
    cda->cur_pos = cur_pos_rel;
  else {
    if(cda->status == CDA_PLAY) {
      _cda_stop_cd(cda);
      cda->status = CDA_STOP;
      cda->cur_pos = cda->track[cda->cur_track - 1].length;
    }
  }

#ifdef DEBUG_DISC
  printf("Current Track        = %d\n", cda->cur_track);
  printf("Current pos in track = %d (%02d:%02d:%02d)\n", cda->cur_pos,
	 (cda->cur_pos / (60 * 60)),
	 ((cda->cur_pos / 60) % 60),
	 (cda->cur_pos  %60));
  printf("Current status: ");
  switch(cda->status) {
  case CDA_NO:
    printf("NO CD\n");
    break;
  case CDA_PLAY:
    printf("PLAY\n");
    break;
  case CDA_PAUSE:
    printf("PAUSE\n");
    break;
  case CDA_STOP:
    printf("STOP\n");
    break;
  case CDA_EJECT:
    printf("EJECT\n");
    break;
  case CDA_COMPLETE:
    printf("COMPLETE\n");
    break;
  }
#endif
  return 1;
}

/*
 * Play a time chunk (in secs);
 */
static int _cda_play_chunk_cd(cdainfo_t *cda, int start, int end) {
  struct cdrom_msf   msf;
  
  if(cda == NULL || cda->fd < 0)
    return 0;
  
  end--;

  if(start >= end)
    start = end - 1;
  
  msf.cdmsf_min0   = start / (60*75);
  msf.cdmsf_sec0   = (start % (60*75)) / 75;
  msf.cdmsf_frame0 = start % 75;
  msf.cdmsf_min1   = end / (60*75);
  msf.cdmsf_sec1   = (end % (60*75)) / 75;
  msf.cdmsf_frame1 = end % 75;
  
  if (ioctl(cda->fd, CDROMSTART)) {
    fprintf(stderr, "input_cda: ioctl(CDROMSTART) failed: %s.\n", strerror(errno));
    return 0;
  }
  
  if(ioctl(cda->fd, CDROMPLAYMSF, &msf)) {
    fprintf(stderr, "input_cda: ioctl(CDROMPLAYMSF) failed: %s.\n", strerror(errno));
    return 0;
  }

  return 1;
}

/*
 * Open audio cd device FD.
 */
static int _cda_open_cd(cdainfo_t *cda) {

  if(cda == NULL)
    return 0;
  
  if((cda->fd = open(cda->device_name, 0)) < 0) {
    
    if(errno == EACCES) {
      fprintf(stderr, "input_cda: No rights to open %s.\n", cda->device_name);
    }
    else if(errno != ENXIO) {
      fprintf(stderr, "input_cda: open() failed: %s.\n", strerror(errno));
    }
    
    return 0;
  }

  return 1;
}

/*
 * Close opened audio cd fd.
 */
static void _cda_close_cd(cdainfo_t *cda) {

  if(cda == NULL)
    return;

  if(cda->fd >= 0) {
    close(cda->fd);
    cda->fd = -1;
  }
}

/*
 * Stop audio cd.
 */
static void _cda_stop_cd(cdainfo_t *cda) {

  if (cda->fd < 0)
    return;
  
  if(cda->status != CDA_STOP) {
    ioctl(cda->fd, CDROMSTOP);
    _cda_get_status_cd(cda);
  }
}

/*
 * Pause audio cd.
 */
static void _cda_pause_cd(cdainfo_t *cda) {
  
  if (cda->fd < 0)
    return;
  
  if(cda->status == CDA_PLAY) {
    ioctl(cda->fd, CDROMPAUSE);
    _cda_get_status_cd(cda);
  }
}

/*
 * Resume audio cd.
 */
static void _cda_resume_cd(cdainfo_t *cda) {
  
  if (cda->fd < 0)
    return;
  
  if(cda->status == CDA_PAUSE) {
    ioctl(cda->fd, CDROMRESUME);
    _cda_get_status_cd(cda);
  }
}

/*
 * Eject audio cd.
 */
static int _cda_eject_cd(cdainfo_t *cda) {
  int err, status;

  if(cda->fd < 0)
    _cda_open_cd(cda);
  
#if defined (__linux__)
  if((status = ioctl(cda->fd, CDROM_DRIVE_STATUS, CDSL_CURRENT)) > 0) {
    switch(status) {
    case CDS_TRAY_OPEN:
      if((err = ioctl(cda->fd, CDROMCLOSETRAY)) != 0) {
	fprintf(stderr, "input_cda: ioctl(CDROMCLOSETRAY) failed: %s\n", strerror(errno));  
      }
      break;
    case CDS_DISC_OK:
      if((err = ioctl(cda->fd, CDROMEJECT)) != 0) {
	fprintf(stderr, "input_cda: ioctl(CDROMEJECT) failed: %s\n", strerror(errno));  
      }
      break;
    }
  }
  else {
    fprintf(stderr, "input_cda: ioctl(CDROM_DRIVE_STATUS) failed: %s\n", strerror(errno));
    _cda_close_cd(cda);
    return 0;
  }
#elif defined (__FreeBSD__)
  if(ioctl(cda->fd, CDIOCALLOW) == -1) {
    fprintf(stderr, "input_cda: ioctl(CDROMALLOW) failed: %s\n", strerror(errno));
  } 
  else {
    if(ioctl(cda->fd, CDIOCEJECT) == -1) {
      fprintf(stderr, "input_cda: ioctl(CDROMEJECT) failed: %s\n", strerror(errno));
    }
  }
#elif defined (__sun)
  if((err = ioctl(cda->fd, CDROMEJECT)) != 0) {
    fprintf(stderr, "input_cda: ioctl(CDROMEJECT) failed: %s\n", strerror(errno));  
  }
#endif

  _cda_close_cd(cda);

  return 1;
}

/*
 * Read cd table of content.
 */
static int _cda_read_toc_cd(cdainfo_t *cda) {
  struct cdrom_tochdr	hdr;
  struct cdrom_tocentry	entry;
  int			i, pos;
  
  if(ioctl(cda->fd, CDROMREADTOCHDR, &hdr)) {
    fprintf(stderr, "input_cda: ioctl(CDROMREADTOCHDR) failed: %s.\n", strerror(errno));
    return 0;
  }

  cda->first_track = hdr.cdth_trk0;
  cda->num_tracks  = hdr.cdth_trk1;

  if(cda->track) {
    /* Freeing old track/disc titles */
    for(i = 0; i < cda->num_tracks; i++) {
      if(cda->track[i].title)
	free(cda->track[i].title);
    }

    if(cda->title)
      free(cda->title);

    if(cda->category)
      free(cda->category);

    if(cda->cdiscid)
      free(cda->cdiscid);

    cda->track = (trackinfo_t *) realloc(cda->track, (cda->num_tracks + 1) * sizeof(trackinfo_t));
  }
  else
    cda->track = (trackinfo_t *) malloc((cda->num_tracks + 1) * sizeof(trackinfo_t));

  for(i = 0; i <= cda->num_tracks; i++) {
    if(i == cda->num_tracks)
      entry.cdte_track = CDROM_LEADOUT;
    else
      entry.cdte_track = i + 1;
    
    entry.cdte_format = CDROM_MSF;
    
    if(ioctl(cda->fd, CDROMREADTOCENTRY, &entry))	{
      fprintf(stderr, "input_cda: ioctl(CDROMREADTOCENTRY) failed: %s.\n", strerror(errno));
      return 0;
    }
    cda->track[i].track  = i + 1;
    cda->track[i].type   = (entry.cdte_ctrl & CDROM_DATA_TRACK) ? CDDATA : CDAUDIO;
    cda->track[i].length = entry.cdte_addr.msf.minute * 60 + entry.cdte_addr.msf.second;
    cda->track[i].start  = cda->track[i].length * 75 + entry.cdte_addr.msf.frame;
    cda->track[i].title  = NULL;
  }
  
  /* compute real track length */
  pos = cda->track[0].length;  
  for(i = 0; i < cda->num_tracks; i++) {
    cda->track[i].length = cda->track[i+1].length - pos;
    pos = cda->track[i+1].length;
    if(cda->track[i].type == CDDATA)
      cda->track[i].length = (cda->track[i + 1].start - cda->track[i].start) * 2;
  }
  
  cda->length   = cda->track[cda->num_tracks].length;
  cda->disc_id  = _cda_get_cddb_id(cda);
  cda->title    = NULL;
  cda->cdiscid  = NULL;
  cda->category = NULL;
  
#ifdef DEBUG_DISC
  printf("Disc have %d track(s), first track is %d, length %d (%02d:%02d:%02d)\n", 
	 cda->num_tracks, cda->first_track, 
	 cda->length, (cda->length / (60 * 60)), ((cda->length / 60) % 60), (cda->length %60));

  { /* CDDB infos */
    int t;
    printf("CDDB disc ID is %08lx\n", cda->disc_id);
    printf("%d, ", cda->num_tracks);
    for(t = 0; t < cda->num_tracks; t++) {
      printf("%d, ", cda->track[t].start);
    }
    printf("%d\n", cda->track[t].length);
  }

  for(i = 0; i < cda->num_tracks; i++) {
    printf("Track %2d, %s type, length %3d seconds(%02d:%02d:%02d), start at %3d secs\n", 
	   i, 
	   ((cda->track[i].type == CDDATA)?"DATA":"AUDIO"), 
	   cda->track[i].length, 
	   (cda->track[i].length / (60 * 60)),
	   ((cda->track[i].length / 60) % 60),
	   (cda->track[i].length %60),	   
	   cda->track[i].start);
  }
  printf("LEADOUT (%2d), length %3d seconds(%02d:%02d:%02d), start at %3d secs\n", 
	 i, 
	 cda->track[i].length, 
	 (cda->track[i].length / (60 * 60)),
	 ((cda->track[i].length / 60) % 60),
	 (cda->track[i].length %60),	   
	 cda->track[i].start);
#endif

  return 1;
}

/*
 *
 */
static void _cda_play_track_to_track_from_pos(cdainfo_t *cda, 
					      int start_track, int pos, int end_track) {
  int start;
  int end;
  
  if(cda == NULL || cda->fd < 0)
    return;
  
  _cda_get_status_cd(cda);

  start = start_track - 1;
  end   = end_track - 1;
  
  if(start >= cda->num_tracks)
    end = cda->length * 75;
  else
    end = cda->track[(end_track - 1)].start - 1;
  
  if(_cda_play_chunk_cd(cda, cda->track[start].start + (pos * 75), end))
    _cda_get_status_cd(cda);
  
}

/*
 * Some frontends functions to _cda_play_track_to_track_from_pos()
 */
static void _cda_play_track_to_track(cdainfo_t *cda, int start_track, int end_track) {
  _cda_play_track_to_track_from_pos(cda, start_track, 0, end_track);
}
static void _cda_play_track_from_pos(cdainfo_t *cda, int track, int pos) {
  _cda_play_track_to_track_from_pos(cda, track, pos, track + 1);
}
static void _cda_play_track(cdainfo_t *cda, int track) {
  _cda_play_track_to_track(cda, track, track + 1);
}

/*
 *
 */
static void _cda_free_cda(cdainfo_t *cda) {

  if(cda == NULL)
    return;

  _cda_close_cd(cda);

  if(cda->device_name)
    free(cda->device_name);
  if(cda->track)
    free(cda->track);
  free(cda);
}

/*
 * Change title in UI.
 */
static void _cda_update_ui_title(cda_input_plugin_t *this) {
  xine_ui_event_t  uievent;
  
  if((this == NULL) 
     || (this->cda->title == NULL) || (this->cda->track[this->cda->cur_track - 1].title == NULL))
    return;

  memset(&this->cda->ui_title, 0, sizeof(this->cda->ui_title));
  snprintf(this->cda->ui_title, 255, "%s -*- %d: %s", this->cda->title, 
	   this->cda->cur_track, this->cda->track[this->cda->cur_track - 1].title);

  uievent.event.type = XINE_EVENT_UI_SET_TITLE;
  uievent.data       = this->cda->ui_title;
  
  xine_send_event(this->xine, &uievent.event);
}

/*
 * *************************** END OF PRIVATES ************************************
 */

/*
 *
 */
static int cda_plugin_open (input_plugin_t *this_gen, char *mrl) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;
  char               *filename;
  
  this->mrl = mrl;

  if(strncasecmp (mrl, "cda://", 6))
    return 0;
  
  if(!_cda_open_cd(this->cda)) {
    _cda_free_cda(this->cda);
    return 0;
  }

  if(!_cda_read_toc_cd(this->cda)) {
    _cda_free_cda(this->cda);
    return 0;
  }

  _cda_cbbd_grab_infos(this);
  
  filename = (char *) &mrl[6];
  
  if(sscanf(filename, "%d", &this->cda->cur_track) != 1) {
    fprintf(stderr, "input_cda: malformed MRL. Use cda://<track #>\n");
    _cda_free_cda(this->cda);
    return 0;
  }
  
  if((!this->cda->cur_track) || (this->cda->cur_track > this->cda->num_tracks)) {
    fprintf(stderr, "input_cda: invalid track %d (valid range: 1 .. %d)\n",
	    this->cda->cur_track, this->cda->num_tracks - 1);
    _cda_free_cda(this->cda);
    return 0;
  }
  
  return 1;
}

/*
 *
 */
static buf_element_t *cda_plugin_read_block (input_plugin_t *this_gen,
					     fifo_buffer_t *fifo, off_t nlen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;
  buf_element_t      *buf;
  unsigned char       buffer[nlen];

  /* Check if speed has changed */
  if(this->xine->speed != this->speed) {
    int old_status = this->cda->status;
    this->speed = this->xine->speed;
    if((this->speed == SPEED_PAUSE) && this->cda->status == CDA_PLAY) {
      _cda_pause_cd(this->cda);
    }
    else {
      if(old_status == CDA_PAUSE) {
	_cda_resume_cd(this->cda);
      }
    }
  }
  
  memset(&buffer, 'X', sizeof(buffer));
  
  buf          = fifo->buffer_pool_alloc(fifo);
  buf->content = buf->mem;
  buf->type    = BUF_DEMUX_BLOCK;
  memcpy(buf->mem, buffer, nlen);

  return buf;
}

/*
 *
 */
static off_t cda_plugin_read (input_plugin_t *this_gen, char *buf, off_t nlen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;
  char               *buffer[nlen];

  _cda_get_status_cd(this->cda);
  
  /* Dummy */
  memset(&buffer, 'X', sizeof(buf));
  memcpy(buf, buffer, nlen);

  return nlen;
}

/*
 *
 */
static off_t cda_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  cda_input_plugin_t  *this = (cda_input_plugin_t *) this_gen;
  
  switch (origin) {
  case SEEK_SET:
    _cda_play_track_from_pos(this->cda, this->cda->cur_track, (int) (offset/CDA_BLOCKSIZE));
    _cda_update_ui_title(this);
    break;
    
  default:
    fprintf (stderr, "input_cda: error seek to origin %d not implemented!\n",
	     origin);
    return 0;
  }

  return offset;
}

/*
 * Return current length;
 */
static off_t cda_plugin_get_length (input_plugin_t *this_gen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;

  return (this->cda->track[this->cda->cur_track-1].length * CDA_BLOCKSIZE);
}

/*
 * Return current pos.
 */
static off_t cda_plugin_get_current_pos (input_plugin_t *this_gen){
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;

  _cda_get_status_cd(this->cda);

#ifdef DEBUG_POS
  printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b(%02d:%02d:%02d) (%d)%02d",
	 (this->cda->cur_pos / (60 * 60)),
	 ((this->cda->cur_pos / 60) % 60),
	 (this->cda->cur_pos  %60),
	 this->cda->cur_track-1,
	 this->cda->track[this->cda->cur_track-1].length);
#endif
  
  return (this->cda->cur_pos * CDA_BLOCKSIZE);
}

/*
 * Get plugin capabilities.
 */
static uint32_t cda_plugin_get_capabilities (input_plugin_t *this_gen) {
  
  return INPUT_CAP_SEEKABLE | INPUT_CAP_AUTOPLAY | INPUT_CAP_GET_DIR;
}

/*
 * Get (pseudo) blocksize.
 */
static uint32_t cda_plugin_get_blocksize (input_plugin_t *this_gen) {

  return CDA_BLOCKSIZE;
}

/*
 * Eject current media.
 */
static int cda_plugin_eject_media (input_plugin_t *this_gen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;

  return (_cda_eject_cd(this->cda));
}

/*
 * Close plugin.
 */
static void cda_plugin_close(input_plugin_t *this_gen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;

  _cda_stop_cd(this->cda);
}

/*
 * Plugin stop.
 */
static void cda_plugin_stop (input_plugin_t *this_gen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;

  _cda_stop_cd(this->cda);
  _cda_close_cd(this->cda);
}

/*
 *
 */
static char *cda_plugin_get_description (input_plugin_t *this_gen) {
  return "cd audio plugin as shipped with xine";
}

/*
 *
 */
static char *cda_plugin_get_identifier (input_plugin_t *this_gen) {
  return "CDA";
}

/*
 * Get dir.
 */
static mrl_t **cda_plugin_get_dir (input_plugin_t *this_gen, 
				   char *filename, int *nEntries) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;
  int                 i;
    
  *nEntries = 0;

  if(filename)
    return NULL;
  
  if(!_cda_open_cd(this->cda)) {
    _cda_free_cda(this->cda);
    return NULL;
  }
  
  if(!_cda_read_toc_cd(this->cda)) {
    _cda_free_cda(this->cda);
    return NULL;
  }
  
  _cda_close_cd(this->cda);
  
  if(!this->cda->num_tracks)
    return NULL;
  
  _cda_cbbd_grab_infos(this);

  *nEntries = this->cda->num_tracks;
  
  for(i=1; i <= this->cda->num_tracks; i++) {
    char mrl[1024];
    
    memset(&mrl, 0, sizeof (mrl));
    sprintf(mrl, "cda://%d",i);
    
    if((i-1) >= this->mrls_allocated_entries) {
      ++this->mrls_allocated_entries;
      /* note: 1 extra pointer for terminating NULL */
      this->mrls = realloc(this->mrls, (this->mrls_allocated_entries+1) * sizeof(mrl_t*));
      this->mrls[(i-1)] = (mrl_t *) xine_xmalloc(sizeof(mrl_t));
    }
    else {
      memset(this->mrls[(i-1)], 0, sizeof(mrl_t));
    }
    
    if(this->mrls[(i-1)]->mrl) {
      this->mrls[(i-1)]->mrl = (char *)
	realloc(this->mrls[(i-1)]->mrl, strlen(mrl) + 1);
    }
    else {
      this->mrls[(i-1)]->mrl = (char *) xine_xmalloc(strlen(mrl) + 1);
    }
    
    this->mrls[i-1]->origin = NULL;
    sprintf(this->mrls[i-1]->mrl, "%s", mrl);
    this->mrls[i-1]->link   = NULL;
    this->mrls[i-1]->type   = (0 | mrl_cda);
    this->mrls[i-1]->size   = this->cda->track[i-1].length;
  }

  /*
   * Freeing exceeded mrls if exists.
   */
  while(this->mrls_allocated_entries > *nEntries) {
    MRL_ZERO(this->mrls[this->mrls_allocated_entries - 1]);
    free(this->mrls[this->mrls_allocated_entries--]);
  }

  this->mrls[*nEntries] = NULL;
  
  return this->mrls;
}

/*
 * Get autoplay.
 */
static char **cda_plugin_get_autoplay_list (input_plugin_t *this_gen, int *nFiles) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;
  int                 i;


  *nFiles = 0;
  
  if(!_cda_open_cd(this->cda)) {
    _cda_free_cda(this->cda);
    return NULL;
  }
  
  if(!_cda_read_toc_cd(this->cda)) {
    _cda_free_cda(this->cda);
    return NULL;
  }
  
  _cda_close_cd(this->cda);
  
  if(!this->cda->num_tracks)
    return NULL;
  
  _cda_cbbd_grab_infos(this);

  *nFiles = this->cda->num_tracks;
  
  for(i = 1; i <= this->cda->num_tracks; i++)
    sprintf (this->filelist[i-1], "cda://%d",i);
  
  this->filelist[i-1] = NULL;
  
  return this->filelist;
}

/*
 * Return current MRL.
 */
static char* cda_plugin_get_mrl (input_plugin_t *this_gen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;
  
  return this->mrl;
}

/*
 * Get optional data.
 */
static int cda_plugin_get_optional_data (input_plugin_t *this_gen, void *data, int data_type) {

  return INPUT_OPTIONAL_UNSUPPORTED;
}

/*
 * Initialize plugin.
 */
input_plugin_t *init_input_plugin (int iface, xine_t *xine) {
  cda_input_plugin_t *this;
  config_values_t    *config;
  int                 i;

  if (iface != 5) {
    printf("cda input plugin doesn't support plugin API version %d.\n"
	   "PLUGIN DISABLED.\n"
	   "This means there's a version mismatch between xine and this input"
	   "plugin.\nInstalling current input plugins should help.\n",
	   iface);
    return NULL;
  }
    
  this       = (cda_input_plugin_t *) xine_xmalloc(sizeof(cda_input_plugin_t));
  config     = xine->config;

  for (i = 0; i < 100; i++) {
    this->filelist[i]       = (char *) xine_xmalloc (256);
  }
  
  this->input_plugin.interface_version  = INPUT_PLUGIN_IFACE_VERSION;
  this->input_plugin.get_capabilities   = cda_plugin_get_capabilities;
  this->input_plugin.open               = cda_plugin_open;
  this->input_plugin.read               = cda_plugin_read;
  this->input_plugin.read_block         = cda_plugin_read_block;
  this->input_plugin.seek               = cda_plugin_seek;
  this->input_plugin.get_current_pos    = cda_plugin_get_current_pos;
  this->input_plugin.get_length         = cda_plugin_get_length;
  this->input_plugin.get_blocksize      = cda_plugin_get_blocksize;
  this->input_plugin.eject_media        = cda_plugin_eject_media;
  this->input_plugin.close              = cda_plugin_close;
  this->input_plugin.stop               = cda_plugin_stop;
  this->input_plugin.get_identifier     = cda_plugin_get_identifier;
  this->input_plugin.get_description    = cda_plugin_get_description;
  this->input_plugin.get_dir            = cda_plugin_get_dir;
  this->input_plugin.get_mrl            = cda_plugin_get_mrl;
  this->input_plugin.get_autoplay_list  = cda_plugin_get_autoplay_list;
  this->input_plugin.get_optional_data  = cda_plugin_get_optional_data;
  this->input_plugin.is_branch_possible = NULL;
  
  this->xine           = xine;
  this->config         = config;

  this->mrl            = NULL;
  
  this->cda            = (cdainfo_t *) xine_xmalloc(sizeof(cdainfo_t));
  this->cda->cur_track = -1;
  this->cda->cur_pos   = -1;
  
  this->cda->device_name = strdup(config->register_string(config, "input.cda_device", CDROM,
							  "path to your local cd audio device file",
							  NULL, NULL, NULL));
  
  this->cddb.server = config->register_string(config, "input.cda_cddb_server", CDDB_SERVER,
					       "cddbp server name", NULL, NULL, NULL);
  
  this->cddb.port = config->register_num(config, "input.cda_cddb_port", CDDB_PORT,
					     "cddbp server port", NULL, NULL, NULL);

  this->cddb.fd = -1;

  this->mrls = (mrl_t **) xine_xmalloc(sizeof(mrl_t*));
  this->mrls_allocated_entries = 0;

  return (input_plugin_t *) this;
}
