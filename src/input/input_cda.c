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
 * $Id: input_cda.c,v 1.14 2002/01/02 18:16:07 jkeil Exp $
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

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#ifdef HAVE_LINUX_CDROM_H
# define NON_BLOCKING
# include <linux/cdrom.h>
#endif
#ifdef HAVE_SYS_CDIO_H
# include <sys/cdio.h>
/* TODO: not clean yet */
# if defined (__FreeBSD__)
#define CDIOREADSUBCHANNEL CDIOCREADSUBCHANNEL
#  include <sys/cdrio.h>
# endif
#endif

/* For Digital UNIX */
#ifdef HAVE_IO_CAM_CDROM_H
#include <io/cam/cdrom.h>
#endif

#if !defined (HAVE_LINUX_CDROM_H) && !defined (HAVE_SYS_CDIO_H) && !defined(HAVE_IO_CAM_CDROM_H)
#error "you need to add cdrom / CDA support for your platform to input_cda and configure.in"
#endif

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"

extern int errno;

/*
#define DEBUG_DISC
#define DEBUG_POS
#define TRACE_FUNCS
*/

#ifdef TRACE_FUNCS
#define _ENTER_FUNC() printf("%s() ENTERING.\n", __XINE_FUNCTION__)
#define _LEAVE_FUNC() printf("%s() LEAVING.\n", __XINE_FUNCTION__)
#else
#define _ENTER_FUNC()
#define _LEAVE_FUNC()
#endif

#ifdef __GNUC__
#define LOG_MSG_STDERR(xine, message, args...) {                     \
    xine_log(xine, XINE_LOG_INPUT, message, ##args);                 \
    fprintf(stderr, message, ##args);                                \
  }
#define LOG_MSG(xine, message, args...) {                            \
    xine_log(xine, XINE_LOG_INPUT, message, ##args);                 \
    printf(message, ##args);                                         \
  }
#else
#define LOG_MSG_STDERR(xine, ...) {                                  \
    xine_log(xine, XINE_LOG_INPUT, __VA_ARGS__);                     \
    fprintf(stderr, __VA_ARGS__);                                    \
  }
#define LOG_MSG(xine, ...) {                                         \
    xine_log(xine, XINE_LOG_INPUT, __VA_ARGS__);                     \
    printf(__VA_ARGS__);                                             \
  }
#endif

#if defined(__sun)
#define CDROM	       "/vol/dev/aliases/cdrom0"
#else
#define CDROM          "/dev/cdaudio"
#endif

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
  xine_t       *xine;
  int           fd;
  char         *device_name;
  int           cur_track;
  int           cur_pos;
  int           status;
  int           num_tracks;
  int           length;
  unsigned long disc_id;
  int           have_cddb_info;
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
    char                *cache_dir;
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
 * Callbacks for configuratoin changes.
 */
static void device_change_cb(void *data, cfg_entry_t *cfg) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) data;
  
  if(this->cda->device_name)
    free(this->cda->device_name);

  this->cda->device_name = strdup(cfg->str_value);
}
static void server_change_cb(void *data, cfg_entry_t *cfg) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) data;
  
  this->cddb.server = cfg->str_value;
}
static void port_change_cb(void *data, cfg_entry_t *cfg) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) data;
  
  this->cddb.port = cfg->num_value;
}
static void cachedir_change_cb(void *data, cfg_entry_t *cfg) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) data;
  
  this->cddb.cache_dir = cfg->str_value;
}

/*
 *
 */
static void _cda_mkdir_safe(char *path) {
  struct stat  pstat;
  
  if(path == NULL)
    return;

  if((lstat(path, &pstat)) < 0) {
    /* file or directory no exist, create it */
    if(mkdir(path, 0755) < 0) {
      fprintf(stderr, "input_cda: mkdir(%s) failed: %s\n", path, strerror(errno));
      return;
    }
  }
  else {
    /* Check of found file is a directory file */
    if(!S_ISDIR(pstat.st_mode)) {
      fprintf(stderr, "input_cda: %s is not a directory.\n", path);
    }
  }
}

/*
 *
 */
static void _cda_mkdir_recursive_safe(char *path) {
  char *p, *pp;
  char buf[XINE_PATH_MAX + XINE_NAME_MAX + 1];
  char buf2[XINE_PATH_MAX + XINE_NAME_MAX + 1];

  if(path == NULL)
    return;

  memset(&buf, 0, sizeof(buf));
  memset(&buf2, 0, sizeof(buf2));

  sprintf(buf, "%s", path);
  pp = buf;
  while((p = xine_strsep(&pp, "/")) != NULL) {
    if(p && strlen(p)) {
      sprintf(buf2, "%s/%s", buf2, p);
      _cda_mkdir_safe(buf2);
    }
  }
}

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
 *
 */
static char *_cda_cddb_get_default_location(void) {
  static char buf[XINE_PATH_MAX + XINE_NAME_MAX + 1];
  
  memset(&buf, 0, sizeof(buf));
  sprintf(buf, "%s/.xine/cddbcache", (xine_get_homedir()));
  
  return buf;
}


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
 * Try to load cached cddb infos
 */
static int _cda_load_cached_cddb_infos(cda_input_plugin_t *this) {
  char  cdir[XINE_PATH_MAX + XINE_NAME_MAX + 1];
  DIR  *dir;

  if(this == NULL)
    return 0;
  
  memset(&cdir, 0, sizeof(cdir));
  sprintf(cdir, "%s", this->cddb.cache_dir);
  
  if((dir = opendir(cdir)) != NULL) {
    struct dirent *pdir;
    
    while((pdir = readdir(dir)) != NULL) {
      char discid[9];
      
      memset(&discid, 0, sizeof(discid));
      sprintf(discid, "%08lx", this->cda->disc_id);
     
      if(!strcasecmp(pdir->d_name, discid)) {
	FILE *fd;
	
	sprintf(cdir, "%s/%s", cdir, discid);
	if((fd = fopen(cdir, "r")) == NULL) {
	  LOG_MSG_STDERR(this->xine, _("input_cda: fopen(%s) failed: %s\n"), cdir, strerror(errno));
	  closedir(dir);
	  return 0;
	}
	else {
	  char buffer[256], *ln, *pt;
	  char buf[256];
	  int  tnum;
	  
	  while((ln = fgets(buffer, 255, fd)) != NULL) {

	    buffer[strlen(buffer) - 1] = '\0';
	    
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
	  fclose(fd);
	}
	
	closedir(dir);
	return 1;
      }
    }
    closedir(dir);
  }
  
  return 0;
}

/*
 * Save cddb grabbed infos.
 */
static void _cda_save_cached_cddb_infos(cda_input_plugin_t *this, char *filecontent) {
  char   cfile[XINE_PATH_MAX + XINE_NAME_MAX + 1];
  FILE  *fd;
  
  if((this == NULL) || (filecontent == NULL))
    return;
  
  memset(&cfile, 0, sizeof(cfile));

  /* Ensure "~/.xine/cddbcache" exist */
  sprintf(cfile, "%s", this->cddb.cache_dir);
  
  _cda_mkdir_recursive_safe(cfile);
  
  sprintf(cfile, "%s/%08lx", this->cddb.cache_dir, this->cda->disc_id);
  
  if((fd = fopen(cfile, "w")) == NULL) {
    LOG_MSG(this->xine, _("input_cda: fopen(%s) failed: %s\n"), cfile, strerror(errno));
    return;
  }
  else {
    fprintf(fd, filecontent);
    fclose(fd);
  }
  
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
 * Try to talk with CDDB server (to retrieve disc/tracks titles).
 */
static void _cda_cddb_retrieve(cda_input_plugin_t *this) {
  char  buffer[2048];
  char *username, *hostname;
  int   err, i;

  if(this == NULL)
    return;
  
  if(_cda_load_cached_cddb_infos(this)) {
#ifdef DEBUG
    int j;
    
    printf("We already have infos\n");
    printf("Title: '%s'\n", this->cda->title);
    for(j=0;j<this->cda->num_tracks;j++)
      printf("Track %2d: '%s'\n", j, this->cda->track[j].title);
#endif
    this->cda->have_cddb_info = 1;
    return;
  }
  else {
    
    this->cddb.fd = _cda_cddb_socket_open(this);
    if(this->cddb.fd >= 0) {
      LOG_MSG(this->xine, _("input_cda: server '%s:%d' successfuly connected.\n"), 
	     this->cddb.server, this->cddb.port);
      
    }
    else {
      LOG_MSG(this->xine, _("input_cda: opening server '%s:%d' failed: %s\n"), 
	     this->cddb.server, this->cddb.port, strerror(errno));
      this->cda->have_cddb_info = 0;
      return;
    }
    
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
			char           buffercache[32768];
			
			this->cda->have_cddb_info = 1;
			memset(&buffercache, 0, sizeof(buffercache));
						
			while(strcmp(buffer, ".")) {
			  memset(&buffer, 0, sizeof(buffer));
			  _cda_cddb_socket_read(&buffer[0], 2047, this->cddb.fd);

			  sprintf(buffercache, "%s%s\n", buffercache, buffer);
			  
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
			/* Save grabbed info */
			_cda_save_cached_cddb_infos(this, buffercache);
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
    _cda_cddb_socket_close(this);
    free(username);
    free(hostname);
  }
 
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

  _cda_cddb_retrieve(this);

}

/*
 * **************** CDDB END *********************
 */

/*
 * Return 1 if CD has been changed, 0 of not, -1 on error.
 */
static int _cda_is_cd_changed(cdainfo_t *cda) {
#ifdef	DROM_MEDIA_CHANGED
  int err, cd_changed=0;

  if(cda == NULL || cda->fd < 0)
    return -1;
  
  if((err = ioctl(cda->fd, CDROM_MEDIA_CHANGED, cd_changed)) < 0) {
    LOG_MSG_STDERR(this->xine, _("input_cda: ioctl(CDROM_MEDIA_CHANGED) failed: %s.\n"), strerror(errno));
    return -1;
  }
  
  switch(err) {
  case 1:
    return 1;
    break;
    
  default:
    return 0;
    break;
  }

  return -1;
#else
  /*
   * At least on solaris, CDROM_MEDIA_CHANGED does not exist. Just return an
   * error for now 
   */
  return -1;
#endif
}

/*
 * Get CDA status (pos, cur track, status)
 */
static int _cda_get_status_cd(cdainfo_t *cda) {
#ifdef CDIOREADSUBCHANNEL
  struct ioc_read_subchannel cdsc;
  struct cd_sub_channel_info data;
#endif
#ifdef CDROMSUBCHNL
  struct cdrom_subchnl	     sc;
#endif
#ifdef CDROM_READ_SUBCHANNEL
  struct cd_sub_channel      sch;
#endif
  int                        cur_pos_abs = 0;
  int                        cur_frame = 0;
  int                        cur_track;
  int                        cur_pos_rel = 0;
  
  if(cda == NULL || cda->fd < 0)
    return 0;
  

#ifdef CDIOREADSUBCHANNEL
  memset(&cdsc, 0, sizeof(struct ioc_read_subchannel));
  cdsc.data           = &data;
  cdsc.data_len       = sizeof(data);
  cdsc.data_format    = CD_CURRENT_POSITION;
  cdsc.address_format = CD_MSF_FORMAT;
   
  if(ioctl(cda->fd, CDIOCREADSUBCHANNEL, (char *)&cdsc) < 0) {
#endif
#ifdef CDROM_READ_SUBCHANNEL
  sch.sch_data_format    = CDROM_CURRENT_POSITION;
  sch.sch_address_format = CDROM_MSF_FORMAT;
   
  if(ioctl(cda->fd, CDROM_READ_SUBCHANNEL, &sch) < 0) {
#endif
#ifdef CDROMSUBCHNL
  sc.cdsc_format = CDROM_MSF;
 
  if(ioctl(cda->fd, CDROMSUBCHNL, &sc) < 0) {
#endif
    LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDROMSUBCHNL) failed: %s.\n"), strerror(errno));
    return 0;
  }

#ifdef CDIOREADSUBCHANNEL
  switch(data.header.audio_status) {
  case CD_AS_PLAY_IN_PROGRESS:
    cda->status = CDA_PLAY;
    cur_pos_abs = data.what.position.absaddr.msf.minute * 60 + data.what.position.absaddr.msf.second;
    cur_frame   = cur_pos_abs * 75 + data.what.position.absaddr.msf.frame;
    break;

  case CD_AS_PLAY_PAUSED:
    cda->status = CDA_PAUSE;
    break;

  case CD_AS_PLAY_COMPLETED:
    cda->status = CDA_COMPLETE;
    break;

  case CD_AS_NO_STATUS:
    cda->status = CDA_STOP;
    break;
  }
#endif

#ifdef CDROMSUBCHNL
  switch(sc.cdsc_audiostatus) {
  case CDROM_AUDIO_PLAY:
    cda->status = CDA_PLAY;
    cur_pos_abs = sc.cdsc_absaddr.msf.minute * 60 + sc.cdsc_absaddr.msf.second;
    cur_frame   = cur_pos_abs * 75 + sc.cdsc_absaddr.msf.frame;
    break;
    
  case CDROM_AUDIO_PAUSED:
    cda->status = CDA_PAUSE;
    break;
    
  case CDROM_AUDIO_COMPLETED:
    cda->status = CDA_COMPLETE;
    break;
    
  case CDROM_AUDIO_NO_STATUS:
    cda->status = CDA_STOP;
    break;
  }
#endif

  cur_track = 0;
  
  while(cur_track < cda->num_tracks &&
	cur_frame >= cda->track[cur_track].start)
    cur_track++;

  cur_pos_rel = (cur_frame - cda->track[cur_track - 1].start) / 75;

  if(cur_track == cda->cur_track)
    cda->cur_pos = cur_pos_rel;
  else {
    if(cda->status == CDA_PLAY) {
      _cda_stop_cd(cda);
      cda->status = CDA_STOP;
      cda->cur_pos = 0;
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
#ifdef CDIOCPLAYMSF
  struct ioc_play_msf cdmsf;
#endif
#ifdef CDROMPLAYMSF
  struct cdrom_msf    msf;
#endif
  
  if(cda == NULL || cda->fd < 0)
    return 0;
  
  end--;

  if(start >= end)
    start = end - 1;
  
#ifdef CDIOCPLAYMSF
  cdmsf.start_m    = start / (60 * 75);
  cdmsf.start_s    = (start % (60 * 75)) / 75;
  cdmsf.start_f    = start % 75;
  cdmsf.end_m      = end / (60 * 75);
  cdmsf.end_s      = (end % (60 * 75)) / 75;
  cdmsf.end_f      = end % 75;
#endif
#ifdef CDROMPLAYMSF
  msf.cdmsf_min0   = start / (60 * 75);
  msf.cdmsf_sec0   = (start % (60 * 75)) / 75;
  msf.cdmsf_frame0 = start % 75;
  msf.cdmsf_min1   = end / (60 * 75);
  msf.cdmsf_sec1   = (end % (60 * 75)) / 75;
  msf.cdmsf_frame1 = end % 75;
#endif
  
#ifdef CDIOCSTART
  if(ioctl(cda->fd, CDIOCSTART) < 0) {
    LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDIOCSTART) failed: %s.\n"), strerror(errno));
    return 0;
  }
#endif
#ifdef CDROMSTART
  if (ioctl(cda->fd, CDROMSTART)) {
    LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDROMSTART) failed: %s.\n"), strerror(errno));
    return 0;
  }
#endif
#ifdef CDIOCPLAYMSF
  if(ioctl(cda->fd, CDIOCPLAYMSF, (char *)&cdmsf) < 0) {
    LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDIOCPLAYMSF) failed: %s.\n"), strerror(errno));
    return 0;
  }
#endif  
#ifdef CDROMPLAYMSF
  if(ioctl(cda->fd, CDROMPLAYMSF, &msf)) {
    LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDROMPLAYMSF) failed: %s.\n"), strerror(errno));
    return 0;
  }
#endif

  return 1;
}

/*
 * Open audio cd device FD.
 */
static int _cda_open_cd(cdainfo_t *cda) {

  if(cda == NULL)
    return 0;
#ifdef NON_BLOCKING
  if((cda->fd = open(cda->device_name, O_RDONLY | O_NONBLOCK)) < 0) {
#else
  if((cda->fd = open(cda->device_name, O_RDONLY)) < 0) {
#endif  
    if(errno == EACCES) {
      LOG_MSG_STDERR(cda->xine, _("input_cda: No rights to open %s.\n"), cda->device_name);
    }
    else if(errno != ENXIO) {
      LOG_MSG_STDERR(cda->xine, _("input_cda: open(%s) failed: %s.\n"), cda->device_name, strerror(errno));
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
#ifdef CDIOCSTOP
    ioctl(cda->cd, CDIOCSTOP);
#endif
#ifdef CDROMSTOP
    ioctl(cda->fd, CDROMSTOP);
#endif
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
#ifdef CDIOCPAUSE
    ioctl(cda->fd, CDIOCPAUSE);
#endif
#ifdef CDROMPAUSE
    ioctl(cda->fd, CDROMPAUSE);
#endif
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
#ifdef CDIOCRESUME
    ioctl(cda->fd, CDIOCRESUME);
#endif
#ifdef CDROMRESUME
    ioctl(cda->fd, CDROMRESUME);
#endif
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
	LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDROMCLOSETRAY) failed: %s\n"), strerror(errno));  
      }
      break;
    case CDS_DISC_OK:
      if((err = ioctl(cda->fd, CDROMEJECT)) != 0) {
	LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDROMEJECT) failed: %s\n"), strerror(errno));  
      }
      break;
    }
  }
  else {
    LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDROM_DRIVE_STATUS) failed: %s\n"), strerror(errno));
    _cda_close_cd(cda);
    return 0;
  }
#elif defined (__FreeBSD__)
  if(ioctl(cda->fd, CDIOCALLOW) == -1) {
    LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDROMALLOW) failed: %s\n"), strerror(errno));
  } 
  else {
    if(ioctl(cda->fd, CDIOCEJECT) == -1) {
      LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDROMEJECT) failed: %s\n"), strerror(errno));
    }
  }
#elif defined (__sun)
  if((err = ioctl(cda->fd, CDROMEJECT)) != 0) {
    LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDROMEJECT) failed: %s\n"), strerror(errno));  
  }
#endif

  _cda_close_cd(cda);

  return 1;
}

/*
 * Read cd table of content.
 */
static int _cda_read_toc_cd(cdainfo_t *cda) {
#ifdef CDIOREADTOCENTRYS
  struct cd_toc_entry        toc_buffer[MAX_TRACKS];
  struct ioc_read_toc_entry  cdte;
#endif
#ifdef CDROMREADTOCHDR
  struct cdrom_tochdr	     hdr;
#endif
#ifdef CDROMREADTOCENTRY
  struct cdrom_tocentry	     entry;
#endif
#ifdef CDIOREADTOCHEADER
  struct ioc_toc_header      cdth;
#endif
  int			     i, pos;
  

#ifdef CDIOREADTOCHEADER
  if(ioctl(cda->fd, CDIOREADTOCHEADER, (char *)&cdth) < 0) {
    LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDIOREADTOCHEADER) failed: %s.\n"), strerror(errno));
    return 0;
  }

  cda->num_tracks  = cdth.ending_track;
#endif
#ifdef CDROMREADTOCHDR
  if(ioctl(cda->fd, CDROMREADTOCHDR, &hdr) < 0) {
    LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDROMREADTOCHDR) failed: %s.\n"), strerror(errno));
    return 0;
  }

  cda->num_tracks  = hdr.cdth_trk1;
#endif
  
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
    
    cda->have_cddb_info = 0;
    
    cda->track = (trackinfo_t *) realloc(cda->track, (cda->num_tracks + 1) * sizeof(trackinfo_t));
  }
  else
    cda->track = (trackinfo_t *) malloc((cda->num_tracks + 1) * sizeof(trackinfo_t));
  
#ifdef CDIOREADTOCENTRYS
  cdte.address_format = CD_MSF_FORMAT;
  cdte.starting_track = 0;
  cdte.data           = toc_buffer;
  cdte.data_len       = sizeof(toc_buffer);
  
  if(ioctl(cda->fd, CDIOREADTOCENTRYS, (char *)&cdte) < 0) {
    LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDIOREADTOCENTRYS) failed: %s.\n"), strerror(errno));
    return 0;
  }
  
  for(i = 0; i  <= cda->num_tracks; i++) {
    cda->track[i].track  = i + 1;
    cda->track[i].type   = (cdte.data[i].control & CDROM_DATA_TRACK) ? CDDATA : CDAUDIO;
    cda->track[i].length = cdte.data[i].addr.msf.minute * 60 + cdte.data[i].addr.msf.second;
    cda->track[i].start  = cda->track[i].length * 75 + cdte.data[i].addr.msf.frame;
    cda->track[i].title  = NULL;
    
  }
#endif
#ifdef CDROMREADTOCENTRY
  for(i = 0; i <= cda->num_tracks; i++) {
    if(i == cda->num_tracks)
      entry.cdte_track = CDROM_LEADOUT;
    else
      entry.cdte_track = i + 1;
    
    entry.cdte_format = CDROM_MSF;
    
    if(ioctl(cda->fd, CDROMREADTOCENTRY, &entry))	{
      LOG_MSG_STDERR(cda->xine, _("input_cda: ioctl(CDROMREADTOCENTRY) failed: %s.\n"), strerror(errno));
      return 0;
    }
    cda->track[i].track  = i + 1;
    cda->track[i].type   = (entry.cdte_ctrl & CDROM_DATA_TRACK) ? CDDATA : CDAUDIO;
    cda->track[i].length = entry.cdte_addr.msf.minute * 60 + entry.cdte_addr.msf.second;
    cda->track[i].start  = cda->track[i].length * 75 + entry.cdte_addr.msf.frame;
    cda->track[i].title  = NULL;
  }
#endif
  
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
  printf("Disc have %d track(s), length %d (%02d:%02d:%02d)\n", 
	 cda->num_tracks, 
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
  
  if(_cda_play_chunk_cd(cda, cda->track[start].start + (pos * 75), end)) {
#ifdef __sun
    /*
     * On solaris x86 with a PIONEER DVD-ROM DVD-105 the CDDA play 
     * operation does not start if we immediately get a status from
     * the drive.  A 0.1 second sleep avoids the problem.
     */
    xine_usec_sleep(100000);
#endif
    _cda_get_status_cd(cda);
  }
  
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
  
  _ENTER_FUNC();

  this->mrl = mrl;

  if(strncasecmp (mrl, "cda://", 6))
    return 0;
  
  if(!_cda_open_cd(this->cda)) {
    _cda_free_cda(this->cda);
    return 0;
  }

  if((_cda_is_cd_changed(this->cda) == 1) && (this->cda->num_tracks)) {
    if(!_cda_read_toc_cd(this->cda)) {
      _cda_free_cda(this->cda);
      return 0;
    }
    _cda_cbbd_grab_infos(this);
  }

  filename = (char *) &mrl[6];
  
  if(sscanf(filename, "%d", &this->cda->cur_track) != 1) {
    LOG_MSG_STDERR(this->xine, _("input_cda: malformed MRL. Use cda://<track #>\n"));
    _cda_free_cda(this->cda);
    return 0;
  }
  
  if((!this->cda->cur_track) || (this->cda->cur_track > this->cda->num_tracks)) {
    LOG_MSG_STDERR(this->xine, _("input_cda: invalid track %d (valid range: 1 .. %d)\n"),
	    this->cda->cur_track, this->cda->num_tracks - 1);
    _cda_free_cda(this->cda);
    return 0;
  }
  
  _LEAVE_FUNC();
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

  _ENTER_FUNC();

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

  _LEAVE_FUNC();

  return buf;
}

/*
 *
 */
static off_t cda_plugin_read (input_plugin_t *this_gen, char *buf, off_t nlen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;
  char               *buffer[nlen];

  _ENTER_FUNC();

  _cda_get_status_cd(this->cda);
  
  /* Dummy */
  memset(&buffer, 'X', sizeof(buf));
  memcpy(buf, buffer, nlen);

  _LEAVE_FUNC();

  return nlen;
}

/*
 *
 */
static off_t cda_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  cda_input_plugin_t  *this = (cda_input_plugin_t *) this_gen;
  
  _ENTER_FUNC();

  switch (origin) {
  case SEEK_SET:
    if(((int) (offset/CDA_BLOCKSIZE)) != this->cda->cur_pos)
      _cda_play_track_from_pos(this->cda, this->cda->cur_track, (int) (offset/CDA_BLOCKSIZE));
    _cda_update_ui_title(this);
    break;
    
  default:
    LOG_MSG_STDERR(this->xine, _("input_cda: error seek to origin %d not implemented!\n"),
	     origin);
    return 0;
  }

  _LEAVE_FUNC();

  return offset;
}

/*
 * Return current length;
 */
static off_t cda_plugin_get_length (input_plugin_t *this_gen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;

  _ENTER_FUNC();
  _LEAVE_FUNC();

  return (this->cda->track[this->cda->cur_track-1].length * CDA_BLOCKSIZE);
}

/*
 * Return current pos.
 */
static off_t cda_plugin_get_current_pos (input_plugin_t *this_gen){
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;

  _ENTER_FUNC();

  _cda_get_status_cd(this->cda);

#ifdef DEBUG_POS
  printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b(%02d:%02d:%02d) (%d)%02d",
	 (this->cda->cur_pos / (60 * 60)),
	 ((this->cda->cur_pos / 60) % 60),
	 (this->cda->cur_pos  %60),
	 this->cda->cur_track-1,
	 this->cda->track[this->cda->cur_track-1].length);
#endif
  
  _LEAVE_FUNC();

  return (this->cda->cur_pos * CDA_BLOCKSIZE);
}

/*
 * Get plugin capabilities.
 */
static uint32_t cda_plugin_get_capabilities (input_plugin_t *this_gen) {

  _ENTER_FUNC();
  _LEAVE_FUNC();
  
  return INPUT_CAP_SEEKABLE | INPUT_CAP_AUTOPLAY | INPUT_CAP_GET_DIR;
}

/*
 * Get (pseudo) blocksize.
 */
static uint32_t cda_plugin_get_blocksize (input_plugin_t *this_gen) {

  _ENTER_FUNC();
  _LEAVE_FUNC();

  return CDA_BLOCKSIZE;
}

/*
 * Eject current media.
 */
static int cda_plugin_eject_media (input_plugin_t *this_gen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;

  _ENTER_FUNC();
  _LEAVE_FUNC();

  return (_cda_eject_cd(this->cda));
}

/*
 * Close plugin.
 */
static void cda_plugin_close(input_plugin_t *this_gen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;

  _ENTER_FUNC();

  _cda_stop_cd(this->cda);

  _LEAVE_FUNC();
}

/*
 * Plugin stop.
 */
static void cda_plugin_stop (input_plugin_t *this_gen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;

  _ENTER_FUNC();

  _cda_stop_cd(this->cda);
  _cda_close_cd(this->cda);

  _LEAVE_FUNC();
}

/*
 *
 */
static char *cda_plugin_get_description (input_plugin_t *this_gen) {
  _ENTER_FUNC();
  _LEAVE_FUNC();
  return _("cd audio plugin as shipped with xine");
}

/*
 *
 */
static char *cda_plugin_get_identifier (input_plugin_t *this_gen) {
  _ENTER_FUNC();
  _LEAVE_FUNC();
  return "CDA";
}

/*
 * Get dir.
 */
static mrl_t **cda_plugin_get_dir (input_plugin_t *this_gen, 
				   char *filename, int *nEntries) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;
  int                 i;
    
  _ENTER_FUNC();

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
  
  if((this->cda->have_cddb_info == 0) || (_cda_is_cd_changed(this->cda) == 1)) {
    _cda_cbbd_grab_infos(this);
  }

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
  
  _LEAVE_FUNC();

  return this->mrls;
}

/*
 * Get autoplay.
 */
static char **cda_plugin_get_autoplay_list (input_plugin_t *this_gen, int *nFiles) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;
  int                 i;

  _ENTER_FUNC();

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
  
  if((this->cda->have_cddb_info == 0) || (_cda_is_cd_changed(this->cda) == 1)) {
    _cda_cbbd_grab_infos(this);
  }

  *nFiles = this->cda->num_tracks;
  
  for(i = 1; i <= this->cda->num_tracks; i++) {
    
    if(this->filelist[i - 1] == NULL)
      this->filelist[i - 1] = (char *) realloc(this->filelist[i - 1], sizeof(char *) * 256);
    
    sprintf (this->filelist[i - 1], "cda://%d",i);
  }
  
  this->filelist[i - 1] = (char *) realloc(this->filelist[i - 1], sizeof(char *));
  this->filelist[i - 1] = NULL;
  
  _LEAVE_FUNC();

  return this->filelist;
}

/*
 * Return current MRL.
 */
static char* cda_plugin_get_mrl (input_plugin_t *this_gen) {
  cda_input_plugin_t *this = (cda_input_plugin_t *) this_gen;
  
  _ENTER_FUNC();
  _LEAVE_FUNC();
  return this->mrl;
}

/*
 * Get optional data.
 */
static int cda_plugin_get_optional_data (input_plugin_t *this_gen, void *data, int data_type) {

  _ENTER_FUNC();
  _LEAVE_FUNC();
  return INPUT_OPTIONAL_UNSUPPORTED;
}

/*
 * Initialize plugin.
 */
input_plugin_t *init_input_plugin (int iface, xine_t *xine) {
  cda_input_plugin_t *this;
  config_values_t    *config;
  int                 i;

  _ENTER_FUNC();

  if (iface != 5) {
    LOG_MSG(xine,
	    _("cda input plugin doesn't support plugin API version %d.\n"
	      "PLUGIN DISABLED.\n"
	      "This means there's a version mismatch between xine and this input"
	      "plugin.\nInstalling current input plugins should help.\n"),
	    iface);
    return NULL;
  }
    
  this       = (cda_input_plugin_t *) xine_xmalloc(sizeof(cda_input_plugin_t));
  config     = xine->config;

  for (i = 0; i < 100; i++) {
    this->filelist[i]       = (char *) xine_xmalloc(sizeof(char *) * 256);
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
  this->cda->xine      = xine;
  this->cda->cur_track = -1;
  this->cda->cur_pos   = -1;
  
  this->cda->device_name = strdup(config->register_string(config, "input.cda_device", CDROM,
							  "path to your local cd audio device file",
							  NULL, 
							  device_change_cb, (void *) this));
  
  this->cddb.server = config->register_string(config, "input.cda_cddb_server", CDDB_SERVER,
					      "cddbp server name", NULL,
					      server_change_cb, (void *) this);
  
  this->cddb.port = config->register_num(config, "input.cda_cddb_port", CDDB_PORT,
					 "cddbp server port", NULL,
					 port_change_cb, (void *) this);

  this->cddb.fd = -1;

  this->cddb.cache_dir = config->register_string(config, "input.cda_cddb_cachedir", 
						 (_cda_cddb_get_default_location()),
						 "cddbp cache directory", NULL, 
						 cachedir_change_cb, (void *) this);

  this->mrls = (mrl_t **) xine_xmalloc(sizeof(mrl_t*));
  this->mrls_allocated_entries = 0;

  _LEAVE_FUNC();

  return (input_plugin_t *) this;
}
