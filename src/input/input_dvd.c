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
 * $Id: input_dvd.c,v 1.2 2001/04/19 09:46:57 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <dlfcn.h>
#include <stdio.h>
#include <fcntl.h>
#if defined(__NetBSD__) || defined(__OpenBSD__) || defined(__FreeBSD__)
# include <sys/cdio.h>
#elif defined(__linux__)
# include <linux/cdrom.h>
#else
# error "Need the DVD ioctls"
#endif
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "xine_internal.h"
#include "monitor.h"
#include "input_plugin.h"
#include "dvd_udf.h"

static uint32_t xine_debug;

#define DVD     "/dev/dvd"
#define RDVD    "/dev/rdvd"

/*
 * global Variables:
 */

static int                dvd_fd, raw_fd;
static off_t              file_size, file_size_left;
static int                file_lbstart, file_lbcur;
static int                gVTSMinor, gVTSMajor;

/*
 * udf dir function 
 */

#define MAX_DIR_ENTRIES 250

static char              *filelist[MAX_DIR_ENTRIES];
static char              *filelist2[MAX_DIR_ENTRIES];

static int openDrive () {
  
  dvd_fd = open(DVD, O_RDONLY | O_NONBLOCK);

  if (dvd_fd < 0) {
    printf ("input_dvd: unable to open dvd drive (%s): %s\n", DVD,
	    strerror(errno));
    return -1;
  }

  raw_fd = open(RDVD, O_RDONLY | O_NONBLOCK);
  if (raw_fd < 0) {
    raw_fd = dvd_fd;
  }
  return raw_fd;
}

static void closeDrive () {

  if (dvd_fd<0)
    return;

  close (dvd_fd);
  if (raw_fd!=dvd_fd)
    close (raw_fd);

  dvd_fd = -1;
}

/*
 * try to open dvd and prepare to read >filename<
 *
 * returns lbnum on success, 0 otherwise
 */

static int openDVDFile (char *filename, off_t *size) {

  char               str[256];
  int                lbnum;

  xprintf (VERBOSE|INPUT, "input_dvd : openDVDFile >%s<\n",filename);

  if (openDrive() < 0) {
    printf ("input_dvd: cannot open dvd drive >%s<\n", DVD);
    return 0;
  }

  snprintf (str, sizeof(str), "/VIDEO_TS/%s", filename);

  xprintf (VERBOSE|INPUT, "UDFFindFile %s\n",str);

  if (!(lbnum=UDFFindFile(dvd_fd, str, size))) {
    printf ("input_dvd: cannot open file >%s<\n", filename);

    closeDrive ();

    return 0;
  }

  lseek (raw_fd, lbnum * (off_t) DVD_VIDEO_LB_LEN, SEEK_SET) ;

  return lbnum;
}


static void input_plugin_init (void) {
  int i;
  
  /*
   * allocate space for directory listing
   */

  for (i=0; i<MAX_DIR_ENTRIES; i++) {
    filelist[i] = (char *) malloc (256);
    filelist2[i] = (char *) malloc (256);
  }
}

static int input_plugin_open (const char *mrl) {

  char *filename;

  xprintf (VERBOSE|INPUT, "input dvd : input_plugin_open >%s<\n", mrl);

  /*
   * do we handle this kind of MRL ?
   */

  if (strncasecmp (mrl, "dvd://",6))
    return 0;

  filename = (char *) &mrl[6];

  xprintf (VERBOSE|INPUT, "input dvd : input_plugin_open media type correct. file name is %s\n",
	  filename);

  sscanf (filename, "VTS_%d_%d.VOB", &gVTSMajor, &gVTSMinor);

  file_lbstart = openDVDFile (filename, &file_size) ;
  file_lbcur = file_lbstart;

  if (!file_lbstart) {
    fprintf (stderr, "unable to find >%s< on dvd.\n",filename);
    return 0;
  }

  file_size_left = file_size;
 
  return 1 ;
}

static uint32_t input_plugin_read (char *buf, uint32_t nlen) {

  if (nlen != DVD_VIDEO_LB_LEN) {
    /* 
     * Hide the error reporting now, demuxer try to read 6 bytes
     * at STAGE_BY_CONTENT probe stage
     */
    fprintf (stderr, "ERROR in input_dvd plugin read: %d bytes "
      	     "is not a sector!\n", nlen);
    return 0;
  }

  if (file_size_left < nlen)
    return 0;

  if (read (raw_fd, buf, DVD_VIDEO_LB_LEN)) {

    file_lbcur++;
    file_size_left -= DVD_VIDEO_LB_LEN;

    return DVD_VIDEO_LB_LEN;
  } else
    fprintf (stderr, "read error in input_dvd plugin\n");

  return 0;
}

static off_t input_plugin_seek (off_t offset, int origin) {

  offset /= DVD_VIDEO_LB_LEN;

  switch (origin) {
  case SEEK_END:
    offset = (file_size / DVD_VIDEO_LB_LEN) - offset;

  case SEEK_SET:
    file_lbcur = file_lbstart + offset;
    file_size_left = file_size - (offset * DVD_VIDEO_LB_LEN);
    break;
  case SEEK_CUR:
    if (offset) {
      file_lbcur += offset;
      file_size_left = file_size - ((file_lbcur - file_lbstart) * DVD_VIDEO_LB_LEN);
    } else {
      return (file_lbcur - file_lbstart) * (off_t) DVD_VIDEO_LB_LEN;
    }

    break;
  default:
    fprintf (stderr, "error in input dvd plugin seek:%d is an unknown origin\n"
	     ,origin);
  }
  
  return lseek (raw_fd, file_lbcur * (off_t) DVD_VIDEO_LB_LEN, SEEK_SET) - file_lbstart * (off_t) DVD_VIDEO_LB_LEN;
}

static off_t input_plugin_get_length (void) {
  return file_size;
}

static uint32_t input_plugin_get_capabilities (void) {
  return INPUT_CAP_SEEKABLE | INPUT_CAP_BLOCK | INPUT_CAP_AUTOPLAY;
}

static uint32_t input_plugin_get_blocksize (void) {
  return DVD_VIDEO_LB_LEN;
}

static int input_plugin_eject (void) {
  int ret, status;
  int fd;

  if((fd = open(DVD, O_RDONLY|O_NONBLOCK)) > -1) {

#if defined (__linux__)
    if((status = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT)) > 0) {
      switch(status) {
      case CDS_TRAY_OPEN:
	if((ret = ioctl(fd, CDROMCLOSETRAY)) != 0) {
	  xprintf(VERBOSE|INPUT, "CDROMCLOSETRAY failed: %s\n", strerror(errno));  
	}
	break;
      case CDS_DISC_OK:
	if((ret = ioctl(fd, CDROMEJECT)) != 0) {
	  xprintf(VERBOSE|INPUT, "CDROMEJECT failed: %s\n", strerror(errno));  
	}
	break;
      }
    }
    else {
      xprintf(VERBOSE|INPUT, "CDROM_DRIVE_STATUS failed: %s\n", 
	      strerror(errno));
      close(fd);
      return 0;
    }

#elif defined (__NetBSD__) || defined (__OpenBSD__) || defined (__FreeBSD__)

    if (ioctl(fd, CDIOCALLOW) == -1) {
      perror("ioctl(cdromallow)");
    } else {
      if (ioctl(fd, CDIOCEJECT) == -1) {
        perror("ioctl(cdromeject)");
      }
    }

#endif

    close(fd);
  }
  return 1;
}

static void input_plugin_close (void) {
  closeDrive ();
}

static char *input_plugin_get_identifier (void) {
  return "DVD";
}

static char** input_plugin_get_dir (char *filename, int *nEntries) {

  int i, fd;

  if (filename) {
    *nEntries = 0;
    return NULL;
  }

  if((fd = open(DVD, O_RDONLY|O_NONBLOCK)) > -1) {

    int    nFiles, nFiles2;

    UDFListDir (fd, "/VIDEO_TS", MAX_DIR_ENTRIES, filelist, &nFiles);

    nFiles2 = 0;
    for (i=0; i<nFiles; i++) {
      int nLen;

      nLen = strlen (filelist[i]);

      if (nLen<4) 
	continue;
      
      if (!strcasecmp (&filelist[i][nLen-4], ".VOB")) {

	sprintf (filelist2[nFiles2], "dvd://%s",filelist[i]); 

	nFiles2++;
      }

    }

    *nEntries = nFiles2;

    close (fd);

  } else {
    *nEntries = 0;
    return NULL;
  }

  return filelist2;
}

static char **input_plugin_get_autoplay_list (int *nFiles) {

  int i, fd;

  if((fd = open(DVD, O_RDONLY|O_NONBLOCK)) > -1) {
    int    nFiles3, nFiles2;

    UDFListDir (fd, "/VIDEO_TS", MAX_DIR_ENTRIES, filelist, &nFiles3);

    nFiles2 = 0;
    for (i=0; i<nFiles3; i++) {
      int nLen;

      nLen = strlen (filelist[i]);

      if (nLen<4) 
	continue;
      
      if (!strcasecmp (&filelist[i][nLen-4], ".VOB")) {

	sprintf (filelist2[nFiles2], "dvd://%s",filelist[i]); 

	nFiles2++;
      }

    }

    *nFiles = nFiles2;

    close (fd);

  } else {
    *nFiles = 0;
    return NULL;
  }

  return filelist2;
}

static int input_plugin_is_branch_possible (const char *next_mrl) {
  
  char *filename;
  int   vts_minor, vts_major;

  printf ("input_dvd: is_branch_possible to %s ?\n", next_mrl);

  /*
   * do we handle this kind of MRL ?
   */

  if (strncmp (next_mrl, "dvd://",6))
    return 0;

  filename = (char *) &next_mrl[6];

  if (sscanf (filename, "VTS_%d_%d.VOB", &vts_major, &vts_minor) == 2) {
    if ((vts_major==gVTSMajor) && (vts_minor==(gVTSMinor+1))) {
      printf ("input_dvd: branching is possible\n");
      return 1;
    }
  }

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
  input_plugin_get_dir,
  input_plugin_get_blocksize,
  input_plugin_eject,
  input_plugin_close,
  input_plugin_get_identifier,
  input_plugin_get_autoplay_list,
  input_plugin_is_branch_possible,
  NULL
};

input_plugin_t *input_plugin_getinfo(uint32_t dbglvl) {

  xine_debug = dbglvl;

  return &plugin_op;
}
