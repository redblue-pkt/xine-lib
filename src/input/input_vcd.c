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
 * $Id: input_vcd.c,v 1.1 2001/04/18 22:34:05 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#if defined (__linux__)
#include <linux/cdrom.h>
#elif defined (__FreeBSD__)
#include <sys/cdio.h>
#include <sys/cdrio.h>
#else
#error "you need to add cdrom / VCD support for your platform to input_vcd"
#endif

#include "xine.h"
#include "monitor.h"
#include "input_plugin.h"

static uint32_t xine_debug;

/* for FreeBSD make a link to the right devnode, like /dev/acd0c */
#define CDROM          "/dev/cdrom"
#define VCDSECTORSIZE  2324

typedef struct {
	uint8_t sync		[12];
	uint8_t header		[4];
	uint8_t subheader	[8];
	uint8_t data		[2324];
	uint8_t spare		[4];
} cdsector_t;

typedef struct {
  int                    fd;

#if defined (__linux__)
  struct cdrom_tochdr    tochdr;
  struct cdrom_tocentry  tocent[100];
#elif defined (__FreeBSD__)
  struct ioc_toc_header  tochdr;
  struct cd_toc_entry    *tocent;
  off_t                  cur_sector;
#endif
  int                    total_tracks;
  int                    cur_track;

#if defined (__linux__)
  uint8_t                cur_min, cur_sec, cur_frame;
#endif

  char                  *filelist[100];

} input_vcd_t;

static input_vcd_t gVCD;

static void input_plugin_init (void) {
  int i;

  gVCD.fd = -1;
  for (i=0; i<100; i++)
    gVCD.filelist[i] = (char *) malloc (256);
}


#if defined (__linux__)
static int input_vcd_read_toc (void) {
  int i;

  /* read TOC header */
  if ( ioctl(gVCD.fd, CDROMREADTOCHDR, &gVCD.tochdr) == -1 ) {
    fprintf (stderr, "input_vcd : error in ioctl CDROMREADTOCHDR\n");
    return -1;
  }

  /* read individual tracks */
  for (i=gVCD.tochdr.cdth_trk0; i<=gVCD.tochdr.cdth_trk1; i++) {
    gVCD.tocent[i-1].cdte_track = i;
    gVCD.tocent[i-1].cdte_format = CDROM_MSF;
    if ( ioctl(gVCD.fd, CDROMREADTOCENTRY, &gVCD.tocent[i-1]) == -1 ) {
      fprintf (stderr, "input_vcd: error in ioctl CDROMREADTOCENTRY\n");
      return -1;
    }
  }

  /* read the lead-out track */
  gVCD.tocent[gVCD.tochdr.cdth_trk1].cdte_track = CDROM_LEADOUT;
  gVCD.tocent[gVCD.tochdr.cdth_trk1].cdte_format = CDROM_MSF;

  if (ioctl(gVCD.fd, CDROMREADTOCENTRY, &gVCD.tocent[gVCD.tochdr.cdth_trk1]) == -1 ) {
    fprintf (stderr, "input_vcd: error in ioctl CDROMREADTOCENTRY\n");
    return -1;
  }

  gVCD.total_tracks = gVCD.tochdr.cdth_trk1;

  return 0;
}
#elif defined (__FreeBSD__)
static int input_vcd_read_toc (void) {

  struct ioc_read_toc_entry te;
  int ntracks;

  /* read TOC header */
  if ( ioctl(gVCD.fd, CDIOREADTOCHEADER, &gVCD.tochdr) == -1 ) {
    fprintf (stderr, "input_vcd : error in ioctl CDROMREADTOCHDR\n");
    return -1;
  }

  ntracks = gVCD.tochdr.ending_track 
    - gVCD.tochdr.starting_track + 2;
  gVCD.tocent = (struct cd_toc_entry *)malloc(sizeof(*gVCD.tocent) * ntracks);
  
  te.address_format = CD_LBA_FORMAT;
  te.starting_track = 0;
  te.data_len = ntracks * sizeof(struct cd_toc_entry);
  te.data = gVCD.tocent;
  
  if ( ioctl(gVCD.fd, CDIOREADTOCENTRYS, &te) == -1 ){
    fprintf (stderr, "input_vcd: error in ioctl CDROMREADTOCENTRY\n");
    return -1;
  }

  gVCD.total_tracks = gVCD.tochdr.ending_track
    - gVCD.tochdr.starting_track +1;

  return 0;
}
#endif

static int input_plugin_open (const char *mrl) {

  char *filename;

  if (strncasecmp (mrl, "vcd://",6))
    return 0;
    
  gVCD.fd = open (CDROM, O_RDONLY);

  if (gVCD.fd == -1) {
    return 0;
  }

  if (input_vcd_read_toc ()) {
    close (gVCD.fd);
    gVCD.fd = -1;
    return 0;
  }

  filename = (char *) &mrl[6];

  xprintf (VERBOSE|INPUT, "Opening >%s<\n",filename);

  if (sscanf (filename, "%d", &gVCD.cur_track) != 1) {
    fprintf (stderr, "input_vcd: malformed MRL. Use vcd://<track #>\n");
    close (gVCD.fd);
    gVCD.fd = -1;
    return 0;
  }

  if (gVCD.cur_track>=gVCD.total_tracks) {
    fprintf (stderr, "input_vcd: invalid track %d (valid range: 0 .. %d)\n",
	    gVCD.cur_track, gVCD.total_tracks-1);
    close (gVCD.fd);
    gVCD.fd = -1;
    return 0;
  }

#if defined (__linux__)
  gVCD.cur_min   = gVCD.tocent[gVCD.cur_track].cdte_addr.msf.minute;
  gVCD.cur_sec   = gVCD.tocent[gVCD.cur_track].cdte_addr.msf.second;
  gVCD.cur_frame = gVCD.tocent[gVCD.cur_track].cdte_addr.msf.frame;
#elif defined (__FreeBSD__)
  {
    int bsize = 2352;
    if (ioctl (gVCD.fd, CDRIOCSETBLOCKSIZE, &bsize) == -1) {
      fprintf (stderr, "input_vcd: error in CDRIOCSETBLOCKSIZE %d\n", errno);
      return 0;
    }
  
    gVCD.cur_sector = 
      ntohl(gVCD.tocent
	    [gVCD.cur_track+1 - gVCD.tochdr.starting_track].addr.lba);
    
  }
#endif
  
  return 1;
}



#if defined (__linux__)
static uint32_t input_plugin_read (char *buf, uint32_t nlen) {

  static struct cdrom_msf  msf ;
  static cdsector_t        data;
  struct cdrom_msf0       *end_msf;

  if (nlen != VCDSECTORSIZE)
    return 0;

  do
  {  
    end_msf = &gVCD.tocent[gVCD.cur_track+1].cdte_addr.msf;

    /*
    printf ("cur: %02d:%02d:%02d  end: %02d:%02d:%02d\n",
  	  gVCD.cur_min, gVCD.cur_sec, gVCD.cur_frame,
  	  end_msf->minute, end_msf->second, end_msf->frame);
    */

    if ( (gVCD.cur_min>=end_msf->minute) && (gVCD.cur_sec>=end_msf->second)
         && (gVCD.cur_frame>=end_msf->frame))
      return 0;

    msf.cdmsf_min0	= gVCD.cur_min;
    msf.cdmsf_sec0	= gVCD.cur_sec;
    msf.cdmsf_frame0	= gVCD.cur_frame;

    memcpy (&data, &msf, sizeof (msf));

    if (ioctl (gVCD.fd, CDROMREADRAW, &data) == -1) {
      fprintf (stderr, "input_vcd: error in CDROMREADRAW\n");
      return 0;
    }


    gVCD.cur_frame++;
    if (gVCD.cur_frame>=75) {
      gVCD.cur_frame = 0;
      gVCD.cur_sec++;
      if (gVCD.cur_sec>=60) {
        gVCD.cur_sec = 0;
        gVCD.cur_min++;
      }
    }
    
    /* Header ID check for padding sector. VCD uses this to keep constant
       bitrate so the CD doesn't stop/start */
  }
  while((data.subheader[2]&~0x01)==0x60);
  
  memcpy (buf, data.data, VCDSECTORSIZE); /* FIXME */
  return VCDSECTORSIZE;
}
#elif defined (__FreeBSD__)
static uint32_t input_plugin_read (char *buf, uint32_t nlen) {
  static cdsector_t data;
  int bsize = 2352;

  if (nlen != VCDSECTORSIZE)
    return 0;

  do {
    if (lseek (gVCD.fd, gVCD.cur_sector * bsize, SEEK_SET) == -1) {
      fprintf (stderr, "input_vcd: seek error %d\n", errno);
      return 0;
    }
    if (read (gVCD.fd, &data, bsize) == -1) {
      fprintf (stderr, "input_vcd: read error %d\n", errno);
      return 0;
    }
    gVCD.cur_sector++;
  } while ((data.subheader[2]&~0x01)==0x60);
  memcpy (buf, data.data, VCDSECTORSIZE);
  return VCDSECTORSIZE;
}
#endif


#if defined (__linux__)
static off_t input_plugin_seek (off_t offset, int origin) {

  struct cdrom_msf0       *start_msf;
  uint32_t dist ;
  off_t sector_pos;

  start_msf = &gVCD.tocent[gVCD.cur_track].cdte_addr.msf;

  switch (origin) {
  case SEEK_SET:
    dist = offset / VCDSECTORSIZE;

    gVCD.cur_min = dist / (60*75) + start_msf->minute;
    dist %= 60;
    gVCD.cur_sec = dist / 75 + start_msf->second;
    dist %= 75;
    gVCD.cur_frame = dist + start_msf->frame;

    xprintf (VERBOSE|INPUT, "%d => %02d:%02d:%02d\n",offset,gVCD.cur_min,gVCD.cur_sec,gVCD.cur_frame);

    break;
  case SEEK_CUR:
    if (offset) 
      fprintf (stderr, "input_vcd: SEEK_CUR not implemented for offset != 0\n");

    sector_pos = 75 - start_msf->frame;
    
    if (start_msf->second<60)
      sector_pos += (59 - start_msf->second) * 75;
    
    if ( gVCD.cur_min > start_msf->minute) {
      sector_pos += (gVCD.cur_min - start_msf->minute-1) * 60 * 75;
      
      sector_pos += gVCD.cur_sec * 60;
      
      sector_pos += gVCD.cur_frame ;
    }

    return sector_pos * VCDSECTORSIZE;

    break;
  default:
    fprintf (stderr, "input_vcd: error seek to origin %d not implemented!\n",
	     origin);
    return 0;
  }

  return offset ; /* FIXME */
}
#elif defined (__FreeBSD__)
static off_t input_plugin_seek (off_t offset, int origin) {


  u_long start;
  uint32_t dist ;
  off_t sector_pos;

  start = 
    ntohl(gVCD.tocent
	  [gVCD.cur_track+1 - gVCD.tochdr.starting_track].addr.lba);

  /*  printf("seek: start sector:%lu, origin: %d, offset:%qu\n", 
	 start, origin, offset);
  */

  switch (origin) {
  case SEEK_SET:
    dist = offset / VCDSECTORSIZE;
    gVCD.cur_sector = start + dist;
    break;
  case SEEK_CUR:

    if (offset) 
      fprintf (stderr, "input_vcd: SEEK_CUR not implemented for offset != 0\n");

    sector_pos = gVCD.cur_sector;

    return sector_pos * VCDSECTORSIZE;

    break;
  default:
    fprintf (stderr, "input_vcd: error seek to origin %d not implemented!\n",
	     origin);
    return 0;
  }

  return offset ; /* FIXME */
}
#endif

#if defined (__linux__)
static off_t input_plugin_get_length (void) {
  struct cdrom_msf0       *end_msf, *start_msf;
  off_t len ;

  start_msf = &gVCD.tocent[gVCD.cur_track].cdte_addr.msf;
  end_msf   = &gVCD.tocent[gVCD.cur_track+1].cdte_addr.msf;

  len = 75 - start_msf->frame;

  if (start_msf->second<60)
    len += (59 - start_msf->second) * 75;

  if (end_msf->minute > start_msf->minute) {
    len += (end_msf->minute - start_msf->minute-1) * 60 * 75;

    len += end_msf->second * 60;

    len += end_msf->frame ;
  }
  
  return len * VCDSECTORSIZE;
}
#elif defined (__FreeBSD__)
static off_t input_plugin_get_length (void) {

  off_t len ;


  len = 
    ntohl(gVCD.tocent
	  [gVCD.cur_track+2 
	  - gVCD.tochdr.starting_track].addr.lba)
    - ntohl(gVCD.tocent
	    [gVCD.cur_track+1 
	    - gVCD.tochdr.starting_track].addr.lba);
  
  return len * 2352; /*VCDSECTORSIZE;*/

}
#endif

static uint32_t input_plugin_get_capabilities (void) {
  return INPUT_CAP_SEEKABLE | INPUT_CAP_BLOCK | INPUT_CAP_AUTOPLAY;
}

static uint32_t input_plugin_get_blocksize (void) {
  return VCDSECTORSIZE;
}

#if defined (__linux__)
static int input_plugin_eject (void) {
  int ret, status;

  if((gVCD.fd = open(CDROM, O_RDONLY|O_NONBLOCK)) > -1) {
    if((status = ioctl(gVCD.fd, CDROM_DRIVE_STATUS, CDSL_CURRENT)) > 0) {
      switch(status) {
      case CDS_TRAY_OPEN:
	if((ret = ioctl(gVCD.fd, CDROMCLOSETRAY)) != 0) {
	  xprintf(VERBOSE|INPUT, "CDROMCLOSETRAY failed: %s\n", strerror(errno));  
	}
	break;
      case CDS_DISC_OK:
	if((ret = ioctl(gVCD.fd, CDROMEJECT)) != 0) {
	  xprintf(VERBOSE|INPUT, "CDROMEJECT failed: %s\n", strerror(errno));  
	}
	break;
      }
    }
    else {
      xprintf(VERBOSE|INPUT, "CDROM_DRIVE_STATUS failed: %s\n", 
	      strerror(errno));
      close(gVCD.fd);
      return 0;
    }
  }

  close(gVCD.fd);
  
  return 1;
}
#elif defined (__FreeBSD__)
static int input_plugin_eject (void) {
  int fd;

  if ((fd = open(CDROM, O_RDONLY|O_NONBLOCK)) > -1) {
    if (ioctl(fd, CDIOCALLOW) == -1) {
      perror("ioctl(cdromallow)");
    } else {
      if (ioctl(fd, CDIOCEJECT) == -1) {
	perror("ioctl(cdromeject)");
      }
    }
    close(fd);
  }
  
  return 1;
}
#endif

static void input_plugin_close (void) {
  xprintf (VERBOSE|INPUT, "closing input\n");

  close(gVCD.fd);
  gVCD.fd = -1;
}

static char *input_plugin_get_identifier (void) {
  return "VCD";
}

static char **input_plugin_get_autoplay_list (int *nFiles) {

  int i;

  gVCD.fd = open (CDROM, O_RDONLY);

  if (gVCD.fd == -1) {
    perror ("unable to open /dev/cdrom");
    return NULL;
  }

  if (input_vcd_read_toc ()) {
    close (gVCD.fd);
    gVCD.fd = -1;

    printf ("vcd_read_toc failed\n");

    return NULL;
  }

  close (gVCD.fd);
  gVCD.fd = -1;

  *nFiles = gVCD.total_tracks;
  
  /* printf ("%d tracks\n",gVCD.total_tracks); */

  for (i=1; i<gVCD.total_tracks; i++) { /* FIXME: check if track 0 contains valid data */
    sprintf (gVCD.filelist[i-1], "vcd://%d",i);
    /* printf ("list[%d] : %d %s\n", i, gVCD.filelist[i-1], gVCD.filelist[i-1]);   */
  }

  return gVCD.filelist;
}

static int input_plugin_is_branch_possible (const char *next_mrl) {

  char *filename;
  int track;

  if (strncasecmp (next_mrl, "vcd://",6))
    return 0;
    
  filename = (char *) &next_mrl[6];

  if (sscanf (filename, "%d", &track) != 1) {
    return 0;
  }

  if ((track>=gVCD.total_tracks) || (track != (gVCD.cur_track+1))) 
    return 0;
  
  return 1;
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
  input_plugin_get_autoplay_list,
  input_plugin_is_branch_possible,
  NULL
};

input_plugin_t *input_plugin_getinfo(uint32_t dbglvl) {

  xine_debug = dbglvl;

  return &plugin_op;
}


