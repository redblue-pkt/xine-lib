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
 * $Id: input_vcd.c,v 1.8 2001/06/02 21:44:01 guenter Exp $
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
#include <linux/config.h> /* Check for DEVFS */
#include <linux/cdrom.h>
#elif defined (__FreeBSD__)
#include <sys/cdio.h>
#include <sys/cdrio.h>
#else
#error "you need to add cdrom / VCD support for your platform to input_vcd"
#endif

#include "xine_internal.h"
#include "monitor.h"
#include "input_plugin.h"

static uint32_t xine_debug;

/* for FreeBSD make a link to the right devnode, like /dev/acd0c */
#ifdef CONFIG_DEVFS_FS
#define CDROM          "/dev/cdroms/cdrom"
#else
#define CDROM          "/dev/cdrom"
#endif
#define VCDSECTORSIZE  2324

typedef struct {
	uint8_t sync		[12];
	uint8_t header		[4];
	uint8_t subheader	[8];
	uint8_t data		[2324];
	uint8_t spare		[4];
} cdsector_t;

typedef struct {

  input_plugin_t         input_plugin;
  
  char                  *mrl;
  config_values_t       *config;

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

  mrl_t                 *mrls[100];
  int                    mrls_allocated_entries;

} vcd_input_plugin_t;


/* ***************************************************************** */
/*                        Private functions                          */
/* ***************************************************************** */
#if defined (__linux__)
static int input_vcd_read_toc (vcd_input_plugin_t *this) {
  int i;

  /* read TOC header */
  if ( ioctl(this->fd, CDROMREADTOCHDR, &this->tochdr) == -1 ) {
    fprintf (stderr, "input_vcd : error in ioctl CDROMREADTOCHDR\n");
    return -1;
  }

  /* read individual tracks */
  for (i=this->tochdr.cdth_trk0; i<=this->tochdr.cdth_trk1; i++) {
    this->tocent[i-1].cdte_track = i;
    this->tocent[i-1].cdte_format = CDROM_MSF;
    if ( ioctl(this->fd, CDROMREADTOCENTRY, &this->tocent[i-1]) == -1 ) {
      fprintf (stderr, "input_vcd: error in ioctl CDROMREADTOCENTRY\n");
      return -1;
    }
  }

  /* read the lead-out track */
  this->tocent[this->tochdr.cdth_trk1].cdte_track = CDROM_LEADOUT;
  this->tocent[this->tochdr.cdth_trk1].cdte_format = CDROM_MSF;

  if (ioctl(this->fd, CDROMREADTOCENTRY, 
	    &this->tocent[this->tochdr.cdth_trk1]) == -1 ) {
    fprintf (stderr, "input_vcd: error in ioctl CDROMREADTOCENTRY\n");
    return -1;
  }

  this->total_tracks = this->tochdr.cdth_trk1;

  return 0;
}
#elif defined (__FreeBSD__)
static int input_vcd_read_toc (vcd_input_plugin_t *this) {

  struct ioc_read_toc_entry te;
  int ntracks;

  /* read TOC header */
  if ( ioctl(this->fd, CDIOREADTOCHEADER, &this->tochdr) == -1 ) {
    fprintf (stderr, "input_vcd : error in ioctl CDROMREADTOCHDR\n");
    return -1;
  }

  ntracks = this->tochdr.ending_track 
    - this->tochdr.starting_track + 2;
  this->tocent = (struct cd_toc_entry *)
    malloc(sizeof(*this->tocent) * ntracks);
  
  te.address_format = CD_LBA_FORMAT;
  te.starting_track = 0;
  te.data_len = ntracks * sizeof(struct cd_toc_entry);
  te.data = gVCD.tocent;
  
  if ( ioctl(this->fd, CDIOREADTOCENTRYS, &te) == -1 ){
    fprintf (stderr, "input_vcd: error in ioctl CDROMREADTOCENTRY\n");
    return -1;
  }

  this->total_tracks = this->tochdr.ending_track
    - this->tochdr.starting_track +1;

  return 0;
}
#endif
/* ***************************************************************** */
/*                         END OF PRIVATES                           */
/* ***************************************************************** */


/*
 *
 */
static int vcd_plugin_open (input_plugin_t *this_gen, char *mrl) {
  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  char *filename;

  this->mrl = mrl;

  if (strncasecmp (mrl, "vcd://",6))
    return 0;
    
  this->fd = open (CDROM, O_RDONLY);

  if (this->fd == -1) {
    return 0;
  }

  if (input_vcd_read_toc (this)) {
    close (this->fd);
    this->fd = -1;
    return 0;
  }

  filename = (char *) &mrl[6];

  xprintf (VERBOSE|INPUT, "Opening >%s<\n",filename);

  if (sscanf (filename, "%d", &this->cur_track) != 1) {
    fprintf (stderr, "input_vcd: malformed MRL. Use vcd://<track #>\n");
    close (this->fd);
    this->fd = -1;
    return 0;
  }

  if (this->cur_track>=this->total_tracks) {
    fprintf (stderr, "input_vcd: invalid track %d (valid range: 0 .. %d)\n",
	    this->cur_track, this->total_tracks-1);
    close (this->fd);
    this->fd = -1;
    return 0;
  }

#if defined (__linux__)
  this->cur_min   = this->tocent[this->cur_track].cdte_addr.msf.minute;
  this->cur_sec   = this->tocent[this->cur_track].cdte_addr.msf.second;
  this->cur_frame = this->tocent[this->cur_track].cdte_addr.msf.frame;
#elif defined (__FreeBSD__)
  {
    int bsize = 2352;
    if (ioctl (this->fd, CDRIOCSETBLOCKSIZE, &bsize) == -1) {
      fprintf (stderr, "input_vcd: error in CDRIOCSETBLOCKSIZE %d\n", errno);
      return 0;
    }
  
    this->cur_sector = 
      ntohl(this->tocent
	    [this->cur_track+1 - this->tochdr.starting_track].addr.lba);
    
  }
#endif
  
  return 1;
}

/*
 *
 */
#if defined (__linux__)
static off_t vcd_plugin_read (input_plugin_t *this_gen, 
				char *buf, off_t nlen) {
  
  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  static struct cdrom_msf  msf ;
  static cdsector_t        data;
  struct cdrom_msf0       *end_msf;

  if (nlen != VCDSECTORSIZE)
    return 0;

  do
  {  
    end_msf = &this->tocent[this->cur_track+1].cdte_addr.msf;

    /*
    printf ("cur: %02d:%02d:%02d  end: %02d:%02d:%02d\n",
  	  this->cur_min, this->cur_sec, this->cur_frame,
  	  end_msf->minute, end_msf->second, end_msf->frame);
    */

    if ( (this->cur_min>=end_msf->minute) && (this->cur_sec>=end_msf->second)
         && (this->cur_frame>=end_msf->frame))
      return 0;

    msf.cdmsf_min0	= this->cur_min;
    msf.cdmsf_sec0	= this->cur_sec;
    msf.cdmsf_frame0	= this->cur_frame;

    memcpy (&data, &msf, sizeof (msf));

    if (ioctl (this->fd, CDROMREADRAW, &data) == -1) {
      fprintf (stderr, "input_vcd: error in CDROMREADRAW\n");
      return 0;
    }


    this->cur_frame++;
    if (this->cur_frame>=75) {
      this->cur_frame = 0;
      this->cur_sec++;
      if (this->cur_sec>=60) {
        this->cur_sec = 0;
        this->cur_min++;
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
static off_t vcd_plugin_read (input_plugin_t *this_gen, 
				char *buf, off_t nlen) {
  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  static cdsector_t data;
  int bsize = 2352;

  if (nlen != VCDSECTORSIZE)
    return 0;

  do {
    if (lseek (this->fd, this->cur_sector * bsize, SEEK_SET) == -1) {
      fprintf (stderr, "input_vcd: seek error %d\n", errno);
      return 0;
    }
    if (read (this->fd, &data, bsize) == -1) {
      fprintf (stderr, "input_vcd: read error %d\n", errno);
      return 0;
    }
    this->cur_sector++;
  } while ((data.subheader[2]&~0x01)==0x60);
  memcpy (buf, data.data, VCDSECTORSIZE);
  return VCDSECTORSIZE;
}
#endif

/*
 *
 */
#if defined (__linux__)
static buf_element_t *vcd_plugin_read_block (input_plugin_t *this_gen, 
					     fifo_buffer_t *fifo, off_t nlen) {
  
  vcd_input_plugin_t      *this = (vcd_input_plugin_t *) this_gen;
  buf_element_t           *buf = fifo->buffer_pool_alloc (fifo);
  static struct cdrom_msf  msf ;
  static cdsector_t        data;
  struct cdrom_msf0       *end_msf;

  if (nlen != VCDSECTORSIZE)
    return NULL;

  do
  {  
    end_msf = &this->tocent[this->cur_track+1].cdte_addr.msf;

    /*
    printf ("cur: %02d:%02d:%02d  end: %02d:%02d:%02d\n",
  	  this->cur_min, this->cur_sec, this->cur_frame,
  	  end_msf->minute, end_msf->second, end_msf->frame);
    */

    if ( (this->cur_min>=end_msf->minute) && (this->cur_sec>=end_msf->second)
         && (this->cur_frame>=end_msf->frame))
      return NULL;

    msf.cdmsf_min0	= this->cur_min;
    msf.cdmsf_sec0	= this->cur_sec;
    msf.cdmsf_frame0	= this->cur_frame;

    memcpy (&data, &msf, sizeof (msf));

    if (ioctl (this->fd, CDROMREADRAW, &data) == -1) {
      fprintf (stderr, "input_vcd: error in CDROMREADRAW\n");
      return NULL;
    }


    this->cur_frame++;
    if (this->cur_frame>=75) {
      this->cur_frame = 0;
      this->cur_sec++;
      if (this->cur_sec>=60) {
        this->cur_sec = 0;
        this->cur_min++;
      }
    }
    
    /* Header ID check for padding sector. VCD uses this to keep constant
       bitrate so the CD doesn't stop/start */
  }
  while((data.subheader[2]&~0x01)==0x60);
  
  buf->content = buf->mem;
  memcpy (buf->mem, data.data, VCDSECTORSIZE); /* FIXME */
  return buf;
}
#elif defined (__FreeBSD__)
static buf_element_t *vcd_plugin_read_block (input_plugin_t *this_gen, 
					     fifo_buffer_t *fifo, off_t nlen) {
  
  vcd_input_plugin_t  *this = (vcd_input_plugin_t *) this_gen;
  buf_element_t       *buf = fifo->buffer_pool_alloc (fifo);
  static cdsector_t    data;
  int                  bsize = 2352;

  if (nlen != VCDSECTORSIZE)
    return NULL;

  do {
    if (lseek (this->fd, this->cur_sector * bsize, SEEK_SET) == -1) {
      fprintf (stderr, "input_vcd: seek error %d\n", errno);
      return NULL;
    }
    if (read (this->fd, &data, bsize) == -1) {
      fprintf (stderr, "input_vcd: read error %d\n", errno);
      return NULL;
    }
    this->cur_sector++;
  } while ((data.subheader[2]&~0x01)==0x60);

  buf->content = buf->mem;
  memcpy (buf->mem, data.data, VCDSECTORSIZE);
  return buf;
}
#endif

//

/*
 *
 */
#if defined (__linux__)
static off_t vcd_plugin_seek (input_plugin_t *this_gen, 
			      off_t offset, int origin) {

  vcd_input_plugin_t  *this = (vcd_input_plugin_t *) this_gen;
  struct cdrom_msf0   *start_msf;
  uint32_t             dist ;
  off_t                sector_pos;

  start_msf = &this->tocent[this->cur_track].cdte_addr.msf;

  switch (origin) {
  case SEEK_SET:
    dist = offset / VCDSECTORSIZE;

    this->cur_min = dist / (60*75) + start_msf->minute;
    dist %= 60;
    this->cur_sec = dist / 75 + start_msf->second;
    dist %= 75;
    this->cur_frame = dist + start_msf->frame;

    xprintf (VERBOSE|INPUT, "%Ld => %02d:%02d:%02d\n", offset,
	     this->cur_min, this->cur_sec, this->cur_frame);

    break;
  case SEEK_CUR:
    if (offset) 
      fprintf (stderr, "input_vcd: SEEK_CUR not implemented for offset != 0\n");

    sector_pos = 75 - start_msf->frame;
    
    if (start_msf->second<60)
      sector_pos += (59 - start_msf->second) * 75;
    
    if ( this->cur_min > start_msf->minute) {
      sector_pos += (this->cur_min - start_msf->minute-1) * 60 * 75;
      
      sector_pos += this->cur_sec * 60;
      
      sector_pos += this->cur_frame ;
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
static off_t vcd_plugin_seek (input_plugin_t *this_gen, 
				off_t offset, int origin) {


  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  u_long start;
  uint32_t dist ;
  off_t sector_pos;

  start = 
    ntohl(this->tocent
	  [this->cur_track+1 - this->tochdr.starting_track].addr.lba);

  /*  printf("seek: start sector:%lu, origin: %d, offset:%qu\n", 
	 start, origin, offset);
  */

  switch (origin) {
  case SEEK_SET:
    dist = offset / VCDSECTORSIZE;
    this->cur_sector = start + dist;
    break;
  case SEEK_CUR:

    if (offset) 
      fprintf (stderr, "input_vcd: SEEK_CUR not implemented for offset != 0\n");

    sector_pos = this->cur_sector;

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

/*
 *
 */
#if defined (__linux__)
static off_t vcd_plugin_get_length (input_plugin_t *this_gen) {
  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  struct cdrom_msf0       *end_msf, *start_msf;
  off_t len ;

  start_msf = &this->tocent[this->cur_track].cdte_addr.msf;
  end_msf   = &this->tocent[this->cur_track+1].cdte_addr.msf;

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
static off_t vcd_plugin_get_length (input_plugin_t *this_gen) {
  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  off_t len ;


  len = 
    ntohl(this->tocent
	  [this->cur_track+2 
	  - this->tochdr.starting_track].addr.lba)
    - ntohl(this->tocent
	    [this->cur_track+1 
	    - this->tochdr.starting_track].addr.lba);
  
  return len * 2352; /*VCDSECTORSIZE;*/

}
#endif

/*
 *
 */
static off_t vcd_plugin_get_current_pos (input_plugin_t *this_gen){


  return (vcd_plugin_seek (this_gen, 0, SEEK_CUR));
}

/*
 *
 */
static uint32_t vcd_plugin_get_capabilities (input_plugin_t *this_gen) {
  
  return INPUT_CAP_SEEKABLE | INPUT_CAP_BLOCK | INPUT_CAP_AUTOPLAY | INPUT_CAP_GET_DIR;
}

/*
 *
 */
static uint32_t vcd_plugin_get_blocksize (input_plugin_t *this_gen) {

  return VCDSECTORSIZE;
}

/*
 *
 */
#if defined (__linux__)
static int vcd_plugin_eject_media (input_plugin_t *this_gen) {
  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  int ret, status;

  if((this->fd = open(CDROM, O_RDONLY|O_NONBLOCK)) > -1) {
    if((status = ioctl(this->fd, CDROM_DRIVE_STATUS, CDSL_CURRENT)) > 0) {
      switch(status) {
      case CDS_TRAY_OPEN:
	if((ret = ioctl(this->fd, CDROMCLOSETRAY)) != 0) {
	  xprintf(VERBOSE|INPUT, "CDROMCLOSETRAY failed: %s\n", strerror(errno));  
	}
	break;
      case CDS_DISC_OK:
	if((ret = ioctl(this->fd, CDROMEJECT)) != 0) {
	  xprintf(VERBOSE|INPUT, "CDROMEJECT failed: %s\n", strerror(errno));  
	}
	break;
      }
    }
    else {
      xprintf(VERBOSE|INPUT, "CDROM_DRIVE_STATUS failed: %s\n", 
	      strerror(errno));
      close(this->fd);
      return 0;
    }
  }

  close(this->fd);
  
  return 1;
}
#elif defined (__FreeBSD__)
static int vcd_plugin_eject_media (input_plugin_t *this_gen) {
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

/*
 *
 */
static void vcd_plugin_close (input_plugin_t *this_gen) {
  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;

  xprintf (VERBOSE|INPUT, "closing input\n");

  close(this->fd);
  this->fd = -1;
}

/*
 *
 */
static char *vcd_plugin_get_description (input_plugin_t *this_gen) {
  return "plain file input plugin as shipped with xine";
}

/*
 *
 */
static char *vcd_plugin_get_identifier (input_plugin_t *this_gen) {
  return "VCD";
}

/*
 *
 */
static mrl_t **vcd_plugin_get_dir (input_plugin_t *this_gen, 
				   char *filename, int *nEntries) {

  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  int i;

  if (filename) {
    *nEntries = 0;
    return NULL;
  }

  this->fd = open (CDROM, O_RDONLY);

  if (this->fd == -1) {
#ifdef CONFIG_DEVFS_FS
    perror ("unable to open /dev/cdroms/cdrom");
#else
    perror ("unable to open /dev/cdrom");
#endif
    return NULL;
  }

  if (input_vcd_read_toc (this)) {
    close (this->fd);
    this->fd = -1;

    printf ("vcd_read_toc failed\n");

    return NULL;
  }

  close (this->fd);
  this->fd = -1;

  *nEntries = this->total_tracks;
  
  /* printf ("%d tracks\n", this->total_tracks); */

  for (i=1; i<this->total_tracks; i++) { /* FIXME: check if track 0 contains valid data */
    sprintf (this->mrls[i-1]->mrl, "vcd://%d",i);
    this->mrls[i-1]->type = mrl_vcd;

    /* hack */
    this->cur_track = i;
    this->mrls[i-1]->size = vcd_plugin_get_length ((input_plugin_t *) this);
  }

  return this->mrls;
}

/*
 *
 */
static char **vcd_plugin_get_autoplay_list (input_plugin_t *this_gen, 
					    int *nFiles) {

  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  int i;

  this->fd = open (CDROM, O_RDONLY);

  if (this->fd == -1) {
#ifdef CONFIG_DEVFS_FS
    perror ("unable to open /dev/cdroms/cdrom");
#else
    perror ("unable to open /dev/cdrom");
#endif
    return NULL;
  }

  if (input_vcd_read_toc (this)) {
    close (this->fd);
    this->fd = -1;

    printf ("vcd_read_toc failed\n");

    return NULL;
  }

  close (this->fd);
  this->fd = -1;

  *nFiles = this->total_tracks;
  
  /* printf ("%d tracks\n", this->total_tracks); */

  for (i=1; i<this->total_tracks; i++) { /* FIXME: check if track 0 contains valid data */
    sprintf (this->filelist[i-1], "vcd://%d",i);
    /* printf ("list[%d] : %d %s\n", i, this->filelist[i-1], this->filelist[i-1]);   */
  }

  this->filelist[i-1] = NULL;

  return this->filelist;
}

/*
 *
 */
static char* vcd_plugin_get_mrl (input_plugin_t *this_gen) {
  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;

  return this->mrl;
}

/*
 *
 */
static int vcd_plugin_get_optional_data (input_plugin_t *this_gen, 
					 void *data, int data_type) {

  return INPUT_OPTIONAL_UNSUPPORTED;
}

/*
 *
 */
input_plugin_t *init_input_plugin (int iface, config_values_t *config) {
  vcd_input_plugin_t *this;

  xine_debug = config->lookup_int (config, "xine_debug", 0);
  
  switch (iface) {
  case 1: {
    int i;
    
    this = (vcd_input_plugin_t *) malloc (sizeof (vcd_input_plugin_t));

    for (i = 0; i < 100; i++) {
      this->filelist[i]       = (char *) malloc (256);
      this->mrls[i]           = (mrl_t *) malloc(sizeof(mrl_t));
      this->mrls[i]->mrl      = (char *) malloc (256);
      this->mrls[i]->size     = 0;
    }

    this->mrls_allocated_entries = 100;

    this->input_plugin.interface_version = INPUT_PLUGIN_IFACE_VERSION;
    this->input_plugin.get_capabilities  = vcd_plugin_get_capabilities;
    this->input_plugin.open              = vcd_plugin_open;
    this->input_plugin.read              = vcd_plugin_read;
    this->input_plugin.read_block        = vcd_plugin_read_block;
    this->input_plugin.seek              = vcd_plugin_seek;
    this->input_plugin.get_current_pos   = vcd_plugin_get_current_pos;
    this->input_plugin.get_length        = vcd_plugin_get_length;
    this->input_plugin.get_blocksize     = vcd_plugin_get_blocksize;
    this->input_plugin.eject_media       = vcd_plugin_eject_media;
    this->input_plugin.close             = vcd_plugin_close;
    this->input_plugin.get_identifier    = vcd_plugin_get_identifier;
    this->input_plugin.get_description   = vcd_plugin_get_description;
    this->input_plugin.get_dir           = vcd_plugin_get_dir;
    this->input_plugin.get_mrl           = vcd_plugin_get_mrl;
    this->input_plugin.get_autoplay_list = vcd_plugin_get_autoplay_list;
    this->input_plugin.get_optional_data = vcd_plugin_get_optional_data;

    this->fd      = -1;
    this->mrl     = NULL;
    this->config  = config;
    
    return (input_plugin_t *) this;
  }
    break;
  default:
    fprintf(stderr,
	    "Dvd input plugin doesn't support plugin API version %d.\n"
	    "PLUGIN DISABLED.\n"
	    "This means there's a version mismatch between xine and this input"
	    "plugin.\nInstalling current input plugins should help.\n",
	    iface);
    return NULL;
  }
}

