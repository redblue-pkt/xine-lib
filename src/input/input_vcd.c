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
 * $Id: input_vcd.c,v 1.37 2002/02/17 17:32:50 guenter Exp $
 *
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
#error "you need to add cdrom / VCD support for your platform to input_vcd and configure.in"
#endif

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"

extern int errno;

#ifdef __GNUC__
#define LOG_MSG_STDERR(xine, message, args...) {                     \
    xine_log(xine, XINE_LOG_MSG, message, ##args);                 \
    fprintf(stderr, message, ##args);                                \
  }
#define LOG_MSG(xine, message, args...) {                            \
    xine_log(xine, XINE_LOG_MSG, message, ##args);                 \
    printf(message, ##args);                                         \
  }
#else
#define LOG_MSG_STDERR(xine, ...) {                                  \
    xine_log(xine, XINE_LOG_MSG, __VA_ARGS__);                     \
    fprintf(stderr, __VA_ARGS__);                                    \
  }
#define LOG_MSG(xine, ...) {                                         \
    xine_log(xine, XINE_LOG_MSG, __VA_ARGS__);                     \
    printf(__VA_ARGS__);                                             \
  }
#endif

#if defined(__sun)
#define	CDROM	       "/vol/dev/aliases/cdrom0"
#else
/* for FreeBSD make a link to the right devnode, like /dev/acd0c */
#define CDROM          "/dev/cdrom"
#endif
#define VCDSECTORSIZE  2324

#if defined (__sun)
struct cdrom_msf0 {
	unsigned char   minute;
	unsigned char   second;
	unsigned char   frame;
};
#endif

typedef struct {
	uint8_t sync		[12];
	uint8_t header		[4];
	uint8_t subheader	[8];
	uint8_t data		[2324];
	uint8_t spare		[4];
} cdsector_t;

typedef struct {

  input_plugin_t         input_plugin;
  
  xine_t                *xine;

  char                  *mrl;
  config_values_t       *config;

  int                    fd;

#if defined (__linux__) || defined(__sun)
  struct cdrom_tochdr    tochdr;
  struct cdrom_tocentry  tocent[100];
#elif defined (__FreeBSD__)
  struct ioc_toc_header  tochdr;
  struct cd_toc_entry    *tocent;
  off_t                  cur_sector;
#endif
  int                    total_tracks;
  int                    cur_track;

#if defined (__linux__) || defined(__sun)
  uint8_t                cur_min, cur_sec, cur_frame;
#endif

  const char            *device;

  char                  *filelist[100];

  int                    mrls_allocated_entries;
  mrl_t                **mrls;

#if defined(__sun)
  int			 controller_type;
#endif
} vcd_input_plugin_t;


/* ***************************************************************** */
/*                        Private functions                          */
/* ***************************************************************** */
/*
 * Callback for configuratoin changes.
 */
static void device_change_cb(void *data, cfg_entry_t *cfg) {
  vcd_input_plugin_t *this = (vcd_input_plugin_t *) data;
  
  this->device = strdup(cfg->str_value);
}

#if defined (__linux__) || defined(__sun)
static int input_vcd_read_toc (vcd_input_plugin_t *this) {
  int i;

  /* read TOC header */
  if ( ioctl(this->fd, CDROMREADTOCHDR, &this->tochdr) == -1 ) {
    LOG_MSG_STDERR(this->xine, _("input_vcd : error in ioctl CDROMREADTOCHDR\n"));
    return -1;
  }

  /* read individual tracks */
  for (i=this->tochdr.cdth_trk0; i<=this->tochdr.cdth_trk1; i++) {
    this->tocent[i-1].cdte_track = i;
    this->tocent[i-1].cdte_format = CDROM_MSF;
    if ( ioctl(this->fd, CDROMREADTOCENTRY, &this->tocent[i-1]) == -1 ) {
      LOG_MSG_STDERR(this->xine, 
		     _("input_vcd: error in ioctl CDROMREADTOCENTRY for track %d\n"), i);
      return -1;
    }
  }

  /* read the lead-out track */
  this->tocent[this->tochdr.cdth_trk1].cdte_track = CDROM_LEADOUT;
  this->tocent[this->tochdr.cdth_trk1].cdte_format = CDROM_MSF;

  if (ioctl(this->fd, CDROMREADTOCENTRY, 
	    &this->tocent[this->tochdr.cdth_trk1]) == -1 ) {
    LOG_MSG_STDERR(this->xine, _("input_vcd: error in ioctl CDROMREADTOCENTRY for lead-out\n"));
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
    LOG_MSG_STDERR(this->xine, _("input_vcd : error in ioctl CDROMREADTOCHDR\n"));
    return -1;
  }

  ntracks = this->tochdr.ending_track 
    - this->tochdr.starting_track + 2;
  this->tocent = (struct cd_toc_entry *)
    xine_xmalloc(sizeof(*this->tocent) * ntracks);
  
  te.address_format = CD_LBA_FORMAT;
  te.starting_track = 0;
  te.data_len = ntracks * sizeof(struct cd_toc_entry);
  te.data = this->tocent;
  
  if ( ioctl(this->fd, CDIOREADTOCENTRYS, &te) == -1 ){
    LOG_MSG_STDERR(this->xine, _("input_vcd: error in ioctl CDROMREADTOCENTRY\n"));
    return -1;
  }

  this->total_tracks = this->tochdr.ending_track
    - this->tochdr.starting_track +1;

  return 0;
}
#endif

#if defined(__sun)
#include <sys/dkio.h>
#include <sys/scsi/generic/commands.h>
#include <sys/scsi/impl/uscsi.h>

#define	SUN_CTRL_SCSI	1
#define	SUN_CTRL_ATAPI	2

static int sun_vcd_read(vcd_input_plugin_t *this, long lba, cdsector_t *data)
{
  struct dk_cinfo cinfo;

  /*
   * CDROMCDXA/CDROMREADMODE2 are broken on IDE/ATAPI devices.
   * Try to send MMC3 SCSI commands via the uscsi interface on
   * ATAPI devices.
   */
  if (this->controller_type == 0) {
    if (ioctl(this->fd, DKIOCINFO, &cinfo) == 0
	&& strcmp(cinfo.dki_cname, "ide") == 0)
      this->controller_type = SUN_CTRL_ATAPI;
    else
      this->controller_type = SUN_CTRL_SCSI;    
  }
  switch (this->controller_type) {
  case SUN_CTRL_SCSI:
#if 0
    {
      struct cdrom_cdxa cdxa;
      cdxa.cdxa_addr = lba;
      cdxa.cdxa_length = 1;
      cdxa.cdxa_data = data->subheader;
      cdxa.cdxa_format = CDROM_XA_SECTOR_DATA;
  
      if(ioctl(this->fd,CDROMCDXA,&cdxa)==-1) {
	  perror("CDROMCDXA");
	  return -1;
      }
    }
#else
    {
      struct cdrom_read cdread;
      cdread.cdread_lba = 4*lba;
      cdread.cdread_bufaddr = (caddr_t)data->subheader;
      cdread.cdread_buflen = 2336;

      if(ioctl(this->fd,CDROMREADMODE2,&cdread)==-1) {
	  perror("CDROMREADMODE2");
	  return -1;
      }
    }
#endif
    break;

  case SUN_CTRL_ATAPI:
    {
      struct uscsi_cmd sc;
      union scsi_cdb cdb;
      int blocks = 1;
      int sector_type;
      int sync, header_code, user_data, edc_ecc, error_field;
      int sub_channel;

      sector_type = 0;		/* all types */
      /*sector_type = 1;*/	/* CD-DA */
      /*sector_type = 2;*/	/* mode1 */
      /*sector_type = 3;*/	/* mode2 */
      /*sector_type = 4;*/	/* mode2/form1 */
      /*sector_type = 5;*/	/* mode2/form2 */
      sync = 0;
      header_code = 2;
      user_data = 1;
      edc_ecc = 0;
      error_field = 0;
      sub_channel = 0;

      memset(data, 0xaa, sizeof(cdsector_t));

      memset(&cdb, 0, sizeof(cdb));
      memset(&sc, 0, sizeof(sc));
      cdb.scc_cmd = 0xBE;
      cdb.cdb_opaque[1] = (sector_type) << 2;
      cdb.cdb_opaque[2] = (lba >> 24) & 0xff;
      cdb.cdb_opaque[3] = (lba >> 16) & 0xff;
      cdb.cdb_opaque[4] = (lba >>  8) & 0xff;
      cdb.cdb_opaque[5] =  lba & 0xff;
      cdb.cdb_opaque[6] = (blocks >> 16) & 0xff;
      cdb.cdb_opaque[7] = (blocks >>  8) & 0xff;
      cdb.cdb_opaque[8] =  blocks & 0xff;
      cdb.cdb_opaque[9] = (sync << 7) |
	  		  (header_code << 5) |
	  		  (user_data << 4) |
	  		  (edc_ecc << 3) |
	  		  (error_field << 1);
      cdb.cdb_opaque[10] = sub_channel;
      
      sc.uscsi_cdb = (caddr_t)&cdb;
      sc.uscsi_cdblen = 12;
      sc.uscsi_bufaddr = (caddr_t)data->subheader;
      sc.uscsi_buflen = 2340;
      sc.uscsi_flags = USCSI_ISOLATE | USCSI_READ;
      sc.uscsi_timeout = 20;
      if (ioctl(this->fd, USCSICMD, &sc)) {
	perror("USCSICMD: READ CD");
	return -1;
      }
      if (sc.uscsi_status) {
	LOG_MSG_STDERR(this->xine, _("scsi command failed with status %d\n"), sc.uscsi_status);
	return -1;
      }
    }
    break;
  }
  return 1;
}
#endif /*__sun*/
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
    
  this->fd = open (this->device, O_RDONLY);

  if (this->fd == -1) {
    return 0;
  }

  if (input_vcd_read_toc (this)) {
    close (this->fd);
    this->fd = -1;
    return 0;
  }

  filename = (char *) &mrl[6];

  if (sscanf (filename, "%d", &this->cur_track) != 1) {
    LOG_MSG_STDERR(this->xine, _("input_vcd: malformed MRL. Use vcd://<track #>\n"));
    close (this->fd);
    this->fd = -1;
    return 0;
  }

  if (this->cur_track>=this->total_tracks) {
    LOG_MSG_STDERR(this->xine, _("input_vcd: invalid track %d (valid range: 0 .. %d)\n"),
	    this->cur_track, this->total_tracks-1);
    close (this->fd);
    this->fd = -1;
    return 0;
  }

#if defined (__linux__) || defined(__sun)
  this->cur_min   = this->tocent[this->cur_track].cdte_addr.msf.minute;
  this->cur_sec   = this->tocent[this->cur_track].cdte_addr.msf.second;
  this->cur_frame = this->tocent[this->cur_track].cdte_addr.msf.frame;
#elif defined (__FreeBSD__)
  {
    int bsize = 2352;
    if (ioctl (this->fd, CDRIOCSETBLOCKSIZE, &bsize) == -1) {
      LOG_MSG_STDERR(this->xine, _("input_vcd: error in CDRIOCSETBLOCKSIZE %d\n"), errno);
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
      LOG_MSG_STDERR(this->xine, _("input_vcd: error in CDROMREADRAW\n"));
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
      LOG_MSG_STDERR(this->xine, _("input_vcd: seek error %d\n"), errno);
      return 0;
    }
    if (read (this->fd, &data, bsize) == -1) {
      LOG_MSG_STDERR(this->xine, _("input_vcd: read error %d\n"), errno);
      return 0;
    }
    this->cur_sector++;
  } while ((data.subheader[2]&~0x01)==0x60);
  memcpy (buf, data.data, VCDSECTORSIZE);
  return VCDSECTORSIZE;
}
#elif defined (__sun)
static off_t vcd_plugin_read (input_plugin_t *this_gen, 
				char *buf, off_t nlen) {
  
  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  static cdsector_t        data;
  struct cdrom_msf0       *end_msf;
  long			   lba;

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

    lba = (this->cur_min * 60 + this->cur_sec) * 75L + this->cur_frame;

    if (sun_vcd_read(this, lba, &data) < 0) {
      LOG_MSG_STDERR(this->xine, _("input_vcd: read data failed\n"));
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
#endif

/*
 *
 */
#if defined (__linux__)
static buf_element_t *vcd_plugin_read_block (input_plugin_t *this_gen, 
					     fifo_buffer_t *fifo, off_t nlen) {
  
  vcd_input_plugin_t      *this = (vcd_input_plugin_t *) this_gen;
  buf_element_t           *buf;
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
      LOG_MSG_STDERR(this->xine, _("input_vcd: error in CDROMREADRAW\n"));
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
  
  buf = fifo->buffer_pool_alloc (fifo);
  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;
  memcpy (buf->mem, data.data, VCDSECTORSIZE); /* FIXME */
  return buf;
}
#elif defined (__FreeBSD__)
static buf_element_t *vcd_plugin_read_block (input_plugin_t *this_gen, 
					     fifo_buffer_t *fifo, off_t nlen) {
  
  vcd_input_plugin_t  *this = (vcd_input_plugin_t *) this_gen;
  buf_element_t       *buf;
  static cdsector_t    data;
  int                  bsize = 2352;

  if (nlen != VCDSECTORSIZE)
    return NULL;

  do {
    if (lseek (this->fd, this->cur_sector * bsize, SEEK_SET) == -1) {
      LOG_MSG_STDERR(this->xine, _("input_vcd: seek error %d\n"), errno);
      return NULL;
    }
    if (read (this->fd, &data, bsize) == -1) {
      LOG_MSG_STDERR(this->xine, _("input_vcd: read error %d\n"), errno);
      return NULL;
    }
    this->cur_sector++;
  } while ((data.subheader[2]&~0x01)==0x60);

  buf = fifo->buffer_pool_alloc (fifo);
  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;
  memcpy (buf->mem, data.data, VCDSECTORSIZE);
  return buf;
}
#elif defined(__sun)
static buf_element_t *vcd_plugin_read_block (input_plugin_t *this_gen, 
					     fifo_buffer_t *fifo, off_t nlen) {
  
  vcd_input_plugin_t      *this = (vcd_input_plugin_t *) this_gen;
  buf_element_t           *buf;
  static cdsector_t        data;
  struct cdrom_msf0       *end_msf;
  long			   lba;

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

    lba = (this->cur_min * 60 + this->cur_sec) * 75L + this->cur_frame;

    if (sun_vcd_read (this, lba, &data) < 0) {
      LOG_MSG_STDERR(this->xine, _("input_vcd: read data failed\n"));
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
  
  buf = fifo->buffer_pool_alloc (fifo);
  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;
  memcpy (buf->mem, data.data, VCDSECTORSIZE); /* FIXME */
  return buf;
}
#endif

static off_t vcd_time_to_offset (int min, int sec, int frame) {
  return min * 60 * 75 + sec * 75 + frame;
}

static void vcd_offset_to_time (off_t offset, uint8_t *min, uint8_t *sec, 
				uint8_t *frame) {

  *min = offset / (60*75);
  offset %= (60*75);
  *sec = offset / 75; 
  *frame = offset % 75; 

}

#if defined (__linux__) || defined(__sun)
static off_t vcd_plugin_seek (input_plugin_t *this_gen, 
			      off_t offset, int origin) {

  vcd_input_plugin_t  *this = (vcd_input_plugin_t *) this_gen;
  struct cdrom_msf0   *start_msf;
  off_t                sector_pos;

  start_msf = &this->tocent[this->cur_track].cdte_addr.msf;

  switch (origin) {
  case SEEK_SET:
    sector_pos = (offset / VCDSECTORSIZE) 
      +  vcd_time_to_offset (start_msf->minute,
			     start_msf->second,
			     start_msf->frame);
    

    vcd_offset_to_time (sector_pos, &this->cur_min,
			&this->cur_sec, &this->cur_frame);
    /*
    printf ("input_vcd: seek to %lld => %02d:%02d:%02d (start is %02d:%02d:%02d)\n", offset,
	    this->cur_min, this->cur_sec, this->cur_frame,
	    start_msf->minute, start_msf->second, start_msf->frame);
    */

    break;
  case SEEK_CUR:
    if (offset) 
      LOG_MSG_STDERR(this->xine, _("input_vcd: SEEK_CUR not implemented for offset != 0\n"));

    /*
    printf ("input_vcd: current pos: %02d:%02d:%02d\n",
	    this->cur_min, this->cur_sec, this->cur_frame);
    */

    sector_pos = vcd_time_to_offset (this->cur_min,
				     this->cur_sec,
				     this->cur_frame)
      -  vcd_time_to_offset (start_msf->minute,
			     start_msf->second,
			     start_msf->frame);

    return sector_pos * VCDSECTORSIZE;

    break;
  default:
    LOG_MSG_STDERR(this->xine, _("input_vcd: error seek to origin %d not implemented!\n"),
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
      LOG_MSG_STDERR(this->xine, _("input_vcd: SEEK_CUR not implemented for offset != 0\n"));

    sector_pos = this->cur_sector;

    return sector_pos * VCDSECTORSIZE;

    break;
  default:
    LOG_MSG_STDERR(this->xine, _("input_vcd: error seek to origin %d not implemented!\n"),
	     origin);
    return 0;
  }

  return offset ; /* FIXME */
}
#endif

/*
 *
 */
#if defined (__linux__) || defined(__sun)
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

  if((this->fd = open(this->device, O_RDONLY|O_NONBLOCK)) > -1) {
    if((status = ioctl(this->fd, CDROM_DRIVE_STATUS, CDSL_CURRENT)) > 0) {
      switch(status) {
      case CDS_TRAY_OPEN:
	if((ret = ioctl(this->fd, CDROMCLOSETRAY)) != 0) {
	  LOG_MSG(this->xine, _("input_vcd: CDROMCLOSETRAY failed: %s\n"), strerror(errno));  
	}
	break;
      case CDS_DISC_OK:
	if((ret = ioctl(this->fd, CDROMEJECT)) != 0) {
	  LOG_MSG(this->xine, _("input_vcd: CDROMEJECT failed: %s\n"), strerror(errno));  
	}
	break;
      }
    }
    else {
      LOG_MSG(this->xine, _("input_vcd: CDROM_DRIVE_STATUS failed: %s\n"), 
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
  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  int fd;

  if ((fd = open(this->device, O_RDONLY|O_NONBLOCK)) > -1) {
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
#elif defined (__sun)
static int vcd_plugin_eject_media (input_plugin_t *this_gen) {
  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  int fd, ret;

  if ((fd = open(this->device, O_RDONLY|O_NONBLOCK)) > -1) {
    if ((ret = ioctl(fd, CDROMEJECT)) != 0) {
      LOG_MSG(this->xine, _("input_vcd: CDROMEJECT failed: %s\n"), strerror(errno));  
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

  close(this->fd);
  this->fd = -1;
}

/*
 *
 */
static void vcd_plugin_stop (input_plugin_t *this_gen) {
  vcd_plugin_close(this_gen);
}

/*
 *
 */
static char *vcd_plugin_get_description (input_plugin_t *this_gen) {
  return _("vcd device input plugin as shipped with xine");
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

  *nEntries = 0;

  if (filename)
    return NULL;

  
  this->fd = open (this->device, O_RDONLY);

  if (this->fd == -1) {
    LOG_MSG_STDERR(this->xine, _("unable to open %s: %s.\n"), this->device, strerror(errno));
    return NULL;
  }

  if (input_vcd_read_toc (this)) {
    close (this->fd);
    this->fd = -1;

    LOG_MSG(this->xine, _("vcd_read_toc failed\n"));

    return NULL;
  }

  close (this->fd);
  this->fd = -1;

  *nEntries = this->total_tracks - 1;
  
  /* printf ("%d tracks\n", this->total_tracks); */

  for (i=1; i<this->total_tracks; i++) { /* FIXME: check if track 0 contains valid data */
    char mrl[1024];
    
    memset(&mrl, 0, sizeof (mrl));
    sprintf(mrl, "vcd://%d",i);
    
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
    this->mrls[i-1]->link = NULL;
    this->mrls[i-1]->type = (0 | mrl_vcd);

    /* hack */
    this->cur_track = i;
    this->mrls[i-1]->size = vcd_plugin_get_length ((input_plugin_t *) this);
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
 *
 */
static char **vcd_plugin_get_autoplay_list (input_plugin_t *this_gen, 
					    int *nFiles) {

  vcd_input_plugin_t *this = (vcd_input_plugin_t *) this_gen;
  int i;

  this->fd = open (this->device, O_RDONLY);

  if (this->fd == -1) {
    LOG_MSG_STDERR(this->xine, _("unable to open %s: %s."), this->device, strerror(errno));
    return NULL;
  }

  if (input_vcd_read_toc (this)) {
    close (this->fd);
    this->fd = -1;

    LOG_MSG(this->xine, _("vcd_read_toc failed\n"));

    return NULL;
  }

  close (this->fd);
  this->fd = -1;

  *nFiles = this->total_tracks - 1;
  
  /* printf ("%d tracks\n", this->total_tracks); */

  for (i = 1; i < this->total_tracks; i++) { /* FIXME: check if track 0 contains valid data */

    if(this->filelist[i - 1] == NULL)
      this->filelist[i - 1] = (char *) realloc(this->filelist[i - 1], sizeof(char *) * 256);

    sprintf (this->filelist[i - 1], "vcd://%d",i);
    /* printf ("list[%d] : %d %s\n", i, this->filelist[i-1], this->filelist[i-1]);   */
  }

  this->filelist[i - 1] = (char *) realloc(this->filelist[i-1], sizeof(char *));
  this->filelist[i - 1] = NULL;

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
input_plugin_t *init_input_plugin (int iface, xine_t *xine) {

  vcd_input_plugin_t *this;
  config_values_t    *config;
  int                 i;

  if (iface != 5) {
    LOG_MSG(xine, 
	    _("vcd input plugin doesn't support plugin API version %d.\n"
	      "PLUGIN DISABLED.\n"
	      "This means there's a version mismatch between xine and this input"
	      "plugin.\nInstalling current input plugins should help.\n"),
	    iface);
    return NULL;
  }
    
  this       = (vcd_input_plugin_t *) xine_xmalloc(sizeof(vcd_input_plugin_t));
  config     = xine->config;
  this->xine = xine;

  for (i = 0; i < 100; i++) {
    this->filelist[i]       = (char *) xine_xmalloc(sizeof(char *) * 256);
  }
  
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
  this->input_plugin.stop              = vcd_plugin_stop;
  this->input_plugin.get_identifier    = vcd_plugin_get_identifier;
  this->input_plugin.get_description   = vcd_plugin_get_description;
  this->input_plugin.get_dir           = vcd_plugin_get_dir;
  this->input_plugin.get_mrl           = vcd_plugin_get_mrl;
  this->input_plugin.get_autoplay_list = vcd_plugin_get_autoplay_list;
  this->input_plugin.get_optional_data = vcd_plugin_get_optional_data;
  this->input_plugin.is_branch_possible= NULL;
  
  this->device = config->register_string(config, "input.vcd_device", CDROM,
					 "path to your local vcd device file",
					 NULL, device_change_cb, (void *)this);

  this->mrls = (mrl_t **) xine_xmalloc(sizeof(mrl_t*));
  this->mrls_allocated_entries = 0;

  this->fd      = -1;
  this->mrl     = NULL;
  this->config  = config;
  
  return (input_plugin_t *) this;
}
