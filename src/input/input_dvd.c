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
 * $Id: input_dvd.c,v 1.28 2001/10/05 17:36:28 jkeil Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <dlfcn.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef HAVE_SYS_CDIO_H
# include <sys/cdio.h>
#endif
#ifdef HAVE_LINUX_CDROM_H
# include <linux/cdrom.h>
#elif defined __FreeBSD__
# include "sys/dvdio.h"
#endif
#if ! defined (HAVE_LINUX_CDROM_H) && ! defined (HAVE_SYS_CDIO_H)
#error "you need to add dvd support for your platform to input_dvd.c and configure.in"
#endif

#include "xine_internal.h"
#include "monitor.h"
#include "input_plugin.h"
#include "dvd_udf.h"
#include "read_cache.h"

static uint32_t xine_debug;

#if defined(__sun)
#define RDVD    "/vol/dev/aliases/cdrom0"
#define DVD     RDVD
#else
#define DVD     "/dev/dvd"
#define RDVD    "/dev/rdvd"
#endif

typedef struct {

  input_plugin_t    input_plugin;
  
  char             *mrl;
  config_values_t  *config;

  int               dvd_fd;
  int               raw_fd;
  read_cache_t     *read_cache;
  off_t             file_size;
  off_t             file_size_left;
  int               file_lbstart;
  int               file_lbcur;
  int               gVTSMinor;
  int               gVTSMajor;
  
  const char       *device;
  const char       *raw_device;

  /*
   * udf dir function 
   */
#define MAX_DIR_ENTRIES 250
  
  char             *filelist[MAX_DIR_ENTRIES];
  char             *filelist2[MAX_DIR_ENTRIES];

  int               mrls_allocated_entries;
  mrl_t           **mrls;

} dvd_input_plugin_t;



/* ***************************************************************** */
/*                        Private functions                          */
/* ***************************************************************** */
static int openDrive (dvd_input_plugin_t *this) {
  
  this->dvd_fd = open(this->device, O_RDONLY /* | O_NONBLOCK */ );

  if (this->dvd_fd < 0) {
    printf ("input_dvd: unable to open dvd drive (%s): %s\n",
            this->device, strerror(errno));
    return -1;
  }

  this->raw_fd = open(this->raw_device, O_RDONLY /* | O_NONBLOCK */ );
  if (this->raw_fd < 0) {
    this->raw_fd = this->dvd_fd;
  }

  read_cache_set_fd (this->read_cache, this->raw_fd);

  return this->raw_fd;
}

static void closeDrive (dvd_input_plugin_t *this) {

  if (this->dvd_fd < 0)
    return;

  close (this->dvd_fd);
  if (this->raw_fd != this->dvd_fd)
    close (this->raw_fd);

  this->dvd_fd = -1;

}

/*
 * try to open dvd and prepare to read >filename<
 *
 * returns lbnum on success, 0 otherwise
 */
static int openDVDFile (dvd_input_plugin_t *this,
			char *filename, off_t *size) {
  char  str[256];
  int   lbnum;
  int   encrypted=0;
#if defined HAVE_LINUX_CDROM_H
  dvd_struct         dvd;
#elif defined __FreeBSD__
   struct dvd_struct         dvd;
#endif

  xprintf (VERBOSE|INPUT, "input_dvd : openDVDFile >%s<\n", filename);

  if (openDrive(this) < 0) {
    printf ("input_dvd: cannot open dvd drive >%s<\n", this->device);
    return 0;
  }

#if defined HAVE_LINUX_CDROM_H
  dvd.copyright.type = DVD_STRUCT_COPYRIGHT;
  dvd.copyright.layer_num=0;
  if (ioctl (this->dvd_fd, DVD_READ_STRUCT, &dvd) < 0) {
    printf ("input_dvd: Could not read Copyright Structure\n");
    return 0;
  }
  encrypted = (dvd.copyright.cpst != 0) ;

#elif defined __FreeBSD__

  dvd.format    = DVD_STRUCT_COPYRIGHT;
  dvd.layer_num = 0;

  if (ioctl(this->dvd_fd, DVDIOCREADSTRUCTURE, &dvd) < 0) {
    printf ("input_dvd: Could not read Copyright Structure\n");
    return 0;
  }

  encrypted = (dvd.cpst != 0);
#endif

  if( encrypted ) {
    printf("\ninput_dvd: Sorry, Xine doesn't play encrypted DVDs. The legal status of CSS\n"
           "           decryption is unclear and we will not provide such code.\n\n");
    return 0;
  }

  snprintf (str, sizeof(str), "/VIDEO_TS/%s", filename);

  xprintf (VERBOSE|INPUT, "UDFFindFile %s\n", str);

  if (!(lbnum = UDFFindFile(this->dvd_fd, str, size))) {
    printf ("input_dvd: cannot open file >%s<\n", filename);

    closeDrive (this);

    return 0;
  }

  lseek (this->raw_fd, lbnum * (off_t) DVD_VIDEO_LB_LEN, SEEK_SET) ;

  return lbnum;
}
/* ***************************************************************** */
/*                         END OF PRIVATES                           */
/* ***************************************************************** */

/*
 *
 */
static uint32_t dvd_plugin_get_capabilities (input_plugin_t *this) {
  return INPUT_CAP_SEEKABLE | INPUT_CAP_BLOCK | INPUT_CAP_AUTOPLAY | INPUT_CAP_GET_DIR;
}

/*
 *
 */
static int dvd_plugin_open (input_plugin_t *this_gen, char *mrl) {
  char                *filename;
  dvd_input_plugin_t  *this = (dvd_input_plugin_t *) this_gen;

  this->mrl = mrl;

  xprintf (VERBOSE|INPUT, "input dvd : input_plugin_open >%s<\n", mrl);

  /*
   * do we handle this kind of MRL ?
   */
  if (strncasecmp (mrl, "dvd://", 6))
    return 0;

  filename = (char *) &mrl[6];

  xprintf (VERBOSE|INPUT, "input dvd : dvd_plugin_open media type correct."
	   " file name is %s\n", filename);

  sscanf (filename, "VTS_%d_%d.VOB", &this->gVTSMajor, &this->gVTSMinor);

  this->file_lbstart = openDVDFile (this, filename, &this->file_size) ;
  this->file_lbcur = this->file_lbstart;

  if (!this->file_lbstart) {
    fprintf (stderr, "Unable to find >%s< on dvd.\n", filename);
    return 0;
  }

  this->file_size_left = this->file_size;
 
  return 1 ;
}

static off_t dvd_plugin_read (input_plugin_t *this_gen, 
			      char *buf, off_t nlen) {

  dvd_input_plugin_t *this = (dvd_input_plugin_t *) this_gen;
  int		      bytes_read;

  if (nlen != DVD_VIDEO_LB_LEN) {

    printf ("input_dvd: error read: %Ld bytes is not a sector!\n", 
	    nlen);

    return 0;
  }

  if (this->file_size_left < nlen)
    return 0;

  bytes_read = read (this->raw_fd, buf, DVD_VIDEO_LB_LEN);
  if (bytes_read == DVD_VIDEO_LB_LEN) {

    this->file_lbcur++;
    this->file_size_left -= DVD_VIDEO_LB_LEN;

    return DVD_VIDEO_LB_LEN;
  } else if (bytes_read < 0)
    fprintf (stderr, "read error in input_dvd plugin (%s)\n",
	     strerror (errno));
  else
    fprintf (stderr, "short read in input_dvd (%d != %d)\n",
	     bytes_read, DVD_VIDEO_LB_LEN);
  return 0;
}


static buf_element_t *dvd_plugin_read_block (input_plugin_t *this_gen,
					     fifo_buffer_t *fifo, off_t nlen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t *) this_gen;
  buf_element_t      *buf;

  if (nlen != DVD_VIDEO_LB_LEN || this->file_size_left < nlen) {
    /*
     * Hide the error reporting now, demuxer try to read 6 bytes
     * at STAGE_BY_CONTENT probe stage
     */
    if(nlen != DVD_VIDEO_LB_LEN)
      printf ("input_dvd: error in input_dvd plugin read: %Ld bytes "
      	     "is not a sector!\n", nlen);
    return NULL;
  }

  if ((buf = read_cache_read_block (this->read_cache, (off_t)this->file_lbcur*DVD_VIDEO_LB_LEN))) {

    this->file_lbcur++;
    this->file_size_left -= DVD_VIDEO_LB_LEN;
    buf->type = BUF_DEMUX_BLOCK;

  } else {
    printf ("input_dvd: read error in input_dvd plugin\n");
  }


  return buf;
}


static off_t dvd_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t *) this_gen;

  offset /= DVD_VIDEO_LB_LEN;

  switch (origin) {
  case SEEK_END:
    offset = (this->file_size / DVD_VIDEO_LB_LEN) - offset;

  case SEEK_SET:
    this->file_lbcur = this->file_lbstart + offset;
    this->file_size_left = this->file_size - (offset * DVD_VIDEO_LB_LEN);
    break;
  case SEEK_CUR:
    if (offset) {
      this->file_lbcur += offset;
      this->file_size_left = this->file_size - 
	((this->file_lbcur - this->file_lbstart) * DVD_VIDEO_LB_LEN);
    } else {
      return (this->file_lbcur - this->file_lbstart) *
	(off_t) DVD_VIDEO_LB_LEN;
    }

    break;
  default:
    printf ("input_dvd: seek: %d is an unknown origin\n", origin); 
  }
  
  return lseek (this->raw_fd, 
		this->file_lbcur * (off_t) DVD_VIDEO_LB_LEN, SEEK_SET) 
    - this->file_lbstart * (off_t) DVD_VIDEO_LB_LEN;
}


static off_t dvd_plugin_get_current_pos (input_plugin_t *this_gen){
  dvd_input_plugin_t *this = (dvd_input_plugin_t *) this_gen;

  return ((this->file_lbcur - this->file_lbstart) * DVD_VIDEO_LB_LEN);
}


static off_t dvd_plugin_get_length (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t *) this_gen;

  return this->file_size;
}


static uint32_t dvd_plugin_get_blocksize (input_plugin_t *this_gen) {

  return DVD_VIDEO_LB_LEN;
}


static int dvd_plugin_eject_media (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t *) this_gen;
  int   ret, status;
  int   fd;

  if((fd = open(this->device, O_RDONLY|O_NONBLOCK)) > -1) {

#if defined (HAVE_LINUX_CDROM_H)
    if((status = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT)) > 0) {
      switch(status) {
      case CDS_TRAY_OPEN:
	if((ret = ioctl(fd, CDROMCLOSETRAY)) != 0) {
	  xprintf(VERBOSE|INPUT, "CDROMCLOSETRAY failed: %s\n", 
		  strerror(errno));  
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

#elif defined (HAVE_CDIO_H)

# if defined (__sun)
    status = 0;
    if ((ret = ioctl(fd, CDROMEJECT)) != 0) {
      xprintf(VERBOSE|INPUT, "CDROMEJECT failed: %s\n", strerror(errno));
    }

# else
    if (ioctl(fd, CDIOCALLOW) == -1) {
      perror("ioctl(cdromallow)");
    } else {
      if (ioctl(fd, CDIOCEJECT) == -1) {
        perror("ioctl(cdromeject)");
      }
    }
# endif

#endif

    close(fd);
  }
  return 1;
}


static void dvd_plugin_close (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t *) this_gen;

  closeDrive (this);
}


static void dvd_plugin_stop (input_plugin_t *this_gen) {
  dvd_plugin_close(this_gen);
}


static char *dvd_plugin_get_description (input_plugin_t *this_gen) {

  return "dvd device input plugin as shipped with xine";
}


static char *dvd_plugin_get_identifier (input_plugin_t *this_gen) {

  return "DVD";
}


static mrl_t **dvd_plugin_get_dir (input_plugin_t *this_gen, 
				   char *filename, int *nEntries) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t *) this_gen;
  int i, fd;

  *nEntries = 0;
  
  if (filename)
    return NULL;
  
  if((fd = open(this->device, O_RDONLY /* | O_NONBLOCK */ )) > -1) {
    int nFiles, nFiles2;
	
    UDFListDir (fd, "/VIDEO_TS", MAX_DIR_ENTRIES, this->filelist, &nFiles);

    nFiles2 = 0;
    for (i=0; i<nFiles; i++) {
      int nLen;

      nLen = strlen (this->filelist[i]);

      if (nLen<4) 
	continue;

      if (!strcasecmp (&this->filelist[i][nLen-4], ".VOB")) {
	char str[1024];

	if(nFiles2 >= this->mrls_allocated_entries
	   || this->mrls_allocated_entries == 0) {
	  this->mrls[nFiles2] = (mrl_t *) xmalloc(sizeof(mrl_t));
	}
	else {
	  memset(this->mrls[nFiles2], 0, sizeof(mrl_t));
	}
	
	if(this->mrls[nFiles2]->mrl) {
	  this->mrls[nFiles2]->mrl = (char *)
	    realloc(this->mrls[nFiles2]->mrl, strlen(this->filelist[i]) + 7);
	}
	else {
	  this->mrls[nFiles2]->mrl = (char *)
	    xmalloc(strlen(this->filelist[i]) + 7);
	}

	this->mrls[nFiles2]->origin = NULL;
	sprintf(this->mrls[nFiles2]->mrl, "dvd://%s", this->filelist[i]); 
	this->mrls[nFiles2]->link   = NULL;
	this->mrls[nFiles2]->type   = (0 | mrl_dvd);

	/* determine size */
	memset(&str, 0, sizeof(str));
	sprintf (str, "/VIDEO_TS/%s", this->filelist[i]);
	UDFFindFile(fd, str, &this->mrls[nFiles2]->size);

	nFiles2++;
      }

    }

    *nEntries = nFiles2;

    close (fd);

  }
  else {
    printf ("input_dvd: unable to open dvd drive (%s): %s\n",
            this->device, strerror(errno));
    return NULL;
  }
  /*
   * Freeing exceeded mrls if exists.
   */
  if(*nEntries > this->mrls_allocated_entries)
    this->mrls_allocated_entries = *nEntries;
  else if(this->mrls_allocated_entries > *nEntries) {
    while(this->mrls_allocated_entries > *nEntries) {
      MRL_ZERO(this->mrls[this->mrls_allocated_entries - 1]);
      free(this->mrls[this->mrls_allocated_entries--]);
    }
  }
  
  /*
   * This is useful to let UI know where it should stops ;-).
   */
  this->mrls[*nEntries] = NULL;

  return this->mrls;
}


static char **dvd_plugin_get_autoplay_list (input_plugin_t *this_gen, 
					    int *nFiles) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t *) this_gen;
  int i, fd;
  
  if((fd = open(this->device, O_RDONLY /* | O_NONBLOCK */ )) > -1) {
    int    nFiles3, nFiles2;

    UDFListDir (fd, "/VIDEO_TS", MAX_DIR_ENTRIES, this->filelist, &nFiles3);
    
    nFiles2 = 0;
    for (i=0; i<nFiles3; i++) {
      int nLen;

      nLen = strlen (this->filelist[i]);

      if (nLen<4) 
	continue;

      if (!strcasecmp (&this->filelist[i][nLen-4], ".VOB")) {

	sprintf (this->filelist2[nFiles2], "dvd://%s", this->filelist[i]);

	nFiles2++;
      }

    }

    *nFiles = nFiles2;

    this->filelist2[*nFiles] = NULL;
    close (fd);

  } else {
    printf ("input_dvd: unable to open dvd drive (%s): %s\n",
            this->device, strerror(errno));
    *nFiles = 0;
    return NULL;
  }

  return this->filelist2;
}


static char* dvd_plugin_get_mrl (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t *) this_gen;

  return this->mrl;
}


static int dvd_plugin_get_optional_data (input_plugin_t *this_gen, 
					 void *data, int data_type) {
  /*
  switch(data_type) {

  case INPUT_OPTIONAL_DATA_CLUT:
    ...
    return INPUT_OPTIONAL_SUCCESS;
    break;
    
  case INPUT_OPTIONAL_DATA_AUDIOLANG:
    ...
    return INPUT_OPTIONAL_SUCCESS;
    break;

  }
  */
  return INPUT_OPTIONAL_UNSUPPORTED;
}


input_plugin_t *init_input_plugin (int iface, config_values_t *config) {

  dvd_input_plugin_t *this;
  int i;

  xine_debug = config->lookup_int (config, "xine_debug", 0);
  
  if (iface != 3) {
    printf("dvd input plugin doesn't support plugin API version %d.\n"
	   "PLUGIN DISABLED.\n"
	   "This means there's a version mismatch between xine and this input"
	   "plugin.\nInstalling current input plugins should help.\n",
	   iface);
    return NULL;
  }

  
  this = (dvd_input_plugin_t *) xmalloc (sizeof (dvd_input_plugin_t));
  
  for (i = 0; i < MAX_DIR_ENTRIES; i++) {
    this->filelist[i]       = (char *) xmalloc (256);
    this->filelist2[i]      = (char *) xmalloc (256);
  }
  
  this->input_plugin.interface_version = INPUT_PLUGIN_IFACE_VERSION;
  this->input_plugin.get_capabilities  = dvd_plugin_get_capabilities;
  this->input_plugin.open              = dvd_plugin_open;
  this->input_plugin.read              = dvd_plugin_read;
  this->input_plugin.read_block        = dvd_plugin_read_block;
  this->input_plugin.seek              = dvd_plugin_seek;
  this->input_plugin.get_current_pos   = dvd_plugin_get_current_pos;
  this->input_plugin.get_length        = dvd_plugin_get_length;
  this->input_plugin.get_blocksize     = dvd_plugin_get_blocksize;
  this->input_plugin.eject_media       = dvd_plugin_eject_media;
  this->input_plugin.close             = dvd_plugin_close;
  this->input_plugin.stop              = dvd_plugin_stop;
  this->input_plugin.get_identifier    = dvd_plugin_get_identifier;
  this->input_plugin.get_description   = dvd_plugin_get_description;
  this->input_plugin.get_dir           = dvd_plugin_get_dir;
  this->input_plugin.get_mrl           = dvd_plugin_get_mrl;
  this->input_plugin.get_autoplay_list = dvd_plugin_get_autoplay_list;
  this->input_plugin.get_optional_data = dvd_plugin_get_optional_data;
  this->input_plugin.handle_input_event= NULL;
  this->input_plugin.is_branch_possible= NULL;

  this->device = config->lookup_str(config, "dvd_device", DVD);
  this->raw_device = config->lookup_str(config, "dvd_raw_device", RDVD);

  this->mrls = (mrl_t **) xmalloc(sizeof(mrl_t));
  this->mrls_allocated_entries = 0;

  this->mrl     = NULL;
  this->config  = config;
  this->dvd_fd  = -1;
  this->raw_fd  = -1;

  this->read_cache = read_cache_new ();
  
  return (input_plugin_t *) this;
}
