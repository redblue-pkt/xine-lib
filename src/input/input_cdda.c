/*
 * Copyright (C) 2000-2003 the xine project
 *
 * This file is part of xine, a free video player.
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
 * Compact Disc Digital Audio (CDDA) Input Plugin 
 *   by Mike Melanson (melanson@pcisys.net)
 *
 * $Id: input_cdda.c,v 1.6 2003/01/09 03:26:10 miguelfreitas Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"

#if defined(__sun)
#define	DEFAULT_CDDA_DEVICE	"/vol/dev/aliases/cdrom0"
#else
#define	DEFAULT_CDDA_DEVICE	"/dev/cdrom"
#endif

/* CD-relevant defines and data structures */
#define CD_SECONDS_PER_MINUTE 60
#define CD_FRAMES_PER_SECOND 75
#define CD_RAW_FRAME_SIZE 2352
#define CD_LEADOUT_TRACK 0xAA

typedef struct _cdrom_toc_entry {
  int track_mode;
  int first_frame;
  int first_frame_minute;
  int first_frame_second;
  int first_frame_frame;
  int total_frames;
} cdrom_toc_entry;

typedef struct _cdrom_toc {
  int first_track;
  int last_track;
  int total_tracks;

  cdrom_toc_entry *toc_entries;
  cdrom_toc_entry leadout_track;  /* need to know where last track ends */
} cdrom_toc;

void init_cdrom_toc(cdrom_toc *toc) {

  toc->first_track = toc->last_track = toc->total_tracks = 0;
  toc->toc_entries = NULL;
}

void free_cdrom_toc(cdrom_toc *toc) {

  free(toc->toc_entries);
}

#if defined (__linux__)

#include <linux/cdrom.h>

static void read_cdrom_toc(int fd, cdrom_toc *toc) {

  struct cdrom_tochdr tochdr;
  struct cdrom_tocentry tocentry;
  int i;

  /* fetch the table of contents */
  if (ioctl(fd, CDROMREADTOCHDR, &tochdr) == -1) {
    perror("CDROMREADTOCHDR");
    return;
  }

  toc->first_track = tochdr.cdth_trk0;
  toc->last_track = tochdr.cdth_trk1;
  toc->total_tracks = toc->last_track - toc->first_track + 1;

  /* allocate space for the toc entries */
  toc->toc_entries =
    (cdrom_toc_entry *)malloc(toc->total_tracks * sizeof(cdrom_toc_entry));
  if (!toc->toc_entries) {
    perror("malloc");
    return;
  }

  /* fetch each toc entry */
  for (i = toc->first_track; i <= toc->last_track; i++) {

    memset(&tocentry, 0, sizeof(tocentry));

    tocentry.cdte_track = i;
    tocentry.cdte_format = CDROM_MSF;
    if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
      perror("CDROMREADTOCENTRY");
      return;
    }

    toc->toc_entries[i-1].track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
    toc->toc_entries[i-1].first_frame_minute = tocentry.cdte_addr.msf.minute;
    toc->toc_entries[i-1].first_frame_second = tocentry.cdte_addr.msf.second;
    toc->toc_entries[i-1].first_frame_frame = tocentry.cdte_addr.msf.frame;
    toc->toc_entries[i-1].first_frame =
      (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.cdte_addr.msf.frame;
  }

  /* fetch the leadout as well */
  memset(&tocentry, 0, sizeof(tocentry));

  tocentry.cdte_track = CD_LEADOUT_TRACK;
  tocentry.cdte_format = CDROM_MSF;
  if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
    perror("CDROMREADTOCENTRY");
    return;
  }

  toc->leadout_track.track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
  toc->leadout_track.first_frame_minute = tocentry.cdte_addr.msf.minute;
  toc->leadout_track.first_frame_second = tocentry.cdte_addr.msf.second;
  toc->leadout_track.first_frame_frame = tocentry.cdte_addr.msf.frame;
  toc->leadout_track.first_frame =
    (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
     tocentry.cdte_addr.msf.frame;
}

static void read_cdrom_frame(int fd, int frame,
  unsigned char data[CD_RAW_FRAME_SIZE]) {

  struct cdrom_msf msf;

  /* read from starting frame... */
  msf.cdmsf_min0 = frame / CD_SECONDS_PER_MINUTE / CD_FRAMES_PER_SECOND;
  msf.cdmsf_sec0 = (frame / CD_FRAMES_PER_SECOND) % CD_SECONDS_PER_MINUTE;
  msf.cdmsf_frame0 = frame % CD_FRAMES_PER_SECOND;

  /* read until ending track (starting frame + 1)... */
  msf.cdmsf_min1 = (frame + 1) / CD_SECONDS_PER_MINUTE / CD_FRAMES_PER_SECOND;
  msf.cdmsf_sec1 = ((frame + 1) / CD_FRAMES_PER_SECOND) % CD_SECONDS_PER_MINUTE;
  msf.cdmsf_frame1 = (frame + 1) % CD_FRAMES_PER_SECOND;

  /* MSF structure is the input to the ioctl */
  memcpy(data, &msf, sizeof(msf));

  /* read a frame */
  if(ioctl(fd, CDROMREADRAW, data, data) < 0) {
    perror("CDROMREADRAW");
    return;
  }
}

#elif defined(__sun)

#include <sys/cdio.h>

static void read_cdrom_toc(int fd, cdrom_toc *toc) {

  struct cdrom_tochdr tochdr;
  struct cdrom_tocentry tocentry;
  int i;

  /* fetch the table of contents */
  if (ioctl(fd, CDROMREADTOCHDR, &tochdr) == -1) {
    perror("CDROMREADTOCHDR");
    return;
  }

  toc->first_track = tochdr.cdth_trk0;
  toc->last_track = tochdr.cdth_trk1;
  toc->total_tracks = toc->last_track - toc->first_track + 1;

  /* allocate space for the toc entries */
  toc->toc_entries =
    (cdrom_toc_entry *)malloc(toc->total_tracks * sizeof(cdrom_toc_entry));
  if (!toc->toc_entries) {
    perror("malloc");
    return;
  }

  /* fetch each toc entry */
  for (i = toc->first_track; i <= toc->last_track; i++) {

    memset(&tocentry, 0, sizeof(tocentry));

    tocentry.cdte_track = i;
    tocentry.cdte_format = CDROM_MSF;
    if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
      perror("CDROMREADTOCENTRY");
      return;
    }

    toc->toc_entries[i-1].track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
    toc->toc_entries[i-1].first_frame_minute = tocentry.cdte_addr.msf.minute;
    toc->toc_entries[i-1].first_frame_second = tocentry.cdte_addr.msf.second;
    toc->toc_entries[i-1].first_frame_frame = tocentry.cdte_addr.msf.frame;
    toc->toc_entries[i-1].first_frame =
      (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.cdte_addr.msf.frame;
  }

  /* fetch the leadout as well */
  memset(&tocentry, 0, sizeof(tocentry));

  tocentry.cdte_track = CD_LEADOUT_TRACK;
  tocentry.cdte_format = CDROM_MSF;
  if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
    perror("CDROMREADTOCENTRY");
    return;
  }

  toc->leadout_track.track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
  toc->leadout_track.first_frame_minute = tocentry.cdte_addr.msf.minute;
  toc->leadout_track.first_frame_second = tocentry.cdte_addr.msf.second;
  toc->leadout_track.first_frame_frame = tocentry.cdte_addr.msf.frame;
  toc->leadout_track.first_frame =
    (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
     tocentry.cdte_addr.msf.frame;
}

static void read_cdrom_frame(int fd, int frame,
  unsigned char data[CD_RAW_FRAME_SIZE]) {

  struct cdrom_cdda cdda;

  cdda.cdda_addr = frame - 2 * CD_FRAMES_PER_SECOND;
  cdda.cdda_length = 1;
  cdda.cdda_data = data;
  cdda.cdda_subcode = CDROM_DA_NO_SUBCODE;

  /* read a frame */
  if(ioctl(fd, CDROMCDDA, &cdda) < 0) {
    perror("CDROMCDDA");
    return;
  }
}

#else



static void read_cdrom_toc(int fd, cdrom_toc *toc) {

}


static void read_cdrom_frame(int fd, int frame,
  unsigned char data[CD_RAW_FRAME_SIZE]) {

}

#endif



/**************************************************************************
 * xine interface functions
 *************************************************************************/

#define MAX_TRACKS 99

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;
  config_values_t  *config;

  int               show_hidden_files;
  char             *origin_path;

  int               mrls_allocated_entries;
  xine_mrl_t      **mrls;
  
  char             *autoplaylist[MAX_TRACKS];

} cdda_input_class_t;

typedef struct {
  input_plugin_t    input_plugin;

  xine_stream_t    *stream;

  int               fd;
  int               track;
  char             *mrl;
  int               first_frame;
  int               current_frame;
  int               last_frame;

} cdda_input_plugin_t;


static uint32_t cdda_plugin_get_capabilities (input_plugin_t *this_gen) {

  return INPUT_CAP_SEEKABLE | INPUT_CAP_BLOCK;
}


static off_t cdda_plugin_read (input_plugin_t *this_gen, char *buf, off_t len) {

  /* only allow reading in block-sized chunks */

  return 0;
}

static buf_element_t *cdda_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, 
  off_t nlen) {

  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;
  buf_element_t *buf;
  unsigned char frame_data[CD_RAW_FRAME_SIZE];

  if (nlen != CD_RAW_FRAME_SIZE)
    return NULL;

  if (this->current_frame >= this->last_frame)
    return NULL;

  read_cdrom_frame(this->fd, this->current_frame++, frame_data);

  buf = fifo->buffer_pool_alloc(fifo);
  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;
  buf->size = CD_RAW_FRAME_SIZE;
  memcpy(buf->mem, frame_data, CD_RAW_FRAME_SIZE);

  return buf;
}

static off_t cdda_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;
  int seek_to_frame;

  /* compute the proposed frame and check if it is within bounds */
  if (origin == SEEK_SET)
    seek_to_frame = offset / CD_RAW_FRAME_SIZE + this->first_frame;
  else if (origin == SEEK_CUR)
    seek_to_frame = offset / CD_RAW_FRAME_SIZE + this->current_frame;
  else
    seek_to_frame = offset / CD_RAW_FRAME_SIZE + this->last_frame;

  if ((seek_to_frame >= this->first_frame) &&
      (seek_to_frame <= this->last_frame))
    this->current_frame = seek_to_frame;

  return (this->current_frame - this->first_frame) * CD_RAW_FRAME_SIZE;
}

static off_t cdda_plugin_get_current_pos (input_plugin_t *this_gen){
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  return (this->current_frame - this->first_frame) * CD_RAW_FRAME_SIZE;
}

static off_t cdda_plugin_get_length (input_plugin_t *this_gen) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  return (this->last_frame - this->first_frame + 1) * CD_RAW_FRAME_SIZE;
}

static uint32_t cdda_plugin_get_blocksize (input_plugin_t *this_gen) {

  return CD_RAW_FRAME_SIZE;
}

static char* cdda_plugin_get_mrl (input_plugin_t *this_gen) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  return this->mrl;
}

static int cdda_plugin_get_optional_data (input_plugin_t *this_gen,
                                          void *data, int data_type) {
  return 0;
}

static void cdda_plugin_dispose (input_plugin_t *this_gen ) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  close(this->fd);

  free(this->mrl);

  free(this);
}

static input_plugin_t *open_plugin (input_class_t *cls_gen, xine_stream_t *stream,
                                    const char *data) {

  cdda_input_plugin_t *this;
  cdrom_toc toc;
  int fd;
  int track;

  /* fetch the CD track to play */
  if (!strncasecmp (data, "cdda:", 5)) {
    if (data[5] != '/')
      track = atoi(&data[5]);
    else
      track = atoi(&data[6]);
    /* CD tracks start at 1, reject illegal tracks */
    if (track <= 0)
      return NULL;
  } else
    return NULL;

  /* get the CD TOC */
  init_cdrom_toc(&toc);
  fd = open (DEFAULT_CDDA_DEVICE, O_RDONLY);
  if (fd == -1)
    return NULL;
  read_cdrom_toc(fd, &toc);

  if ((toc.first_track > track) || 
      (toc.last_track < track)) {
    free_cdrom_toc(&toc);
    return NULL;
  }

  this = (cdda_input_plugin_t *) xine_xmalloc (sizeof (cdda_input_plugin_t));
  this->stream = stream;
  this->fd = fd;

  /* CD tracks start from 1; internal data structure indexes from 0 */
  this->track = track - 1;

  /* set up the frame boundaries for this particular track */
  this->first_frame = this->current_frame = 
    toc.toc_entries[this->track].first_frame;
  if (this->track + 1 == toc.last_track)
    this->last_frame = toc.leadout_track.first_frame - 1;
  else
    this->last_frame = toc.toc_entries[this->track + 1].first_frame - 1;
  free_cdrom_toc(&toc);

  this->input_plugin.get_capabilities   = cdda_plugin_get_capabilities;
  this->input_plugin.read               = cdda_plugin_read;
  this->input_plugin.read_block         = cdda_plugin_read_block;
  this->input_plugin.seek               = cdda_plugin_seek;
  this->input_plugin.get_current_pos    = cdda_plugin_get_current_pos;
  this->input_plugin.get_length         = cdda_plugin_get_length;
  this->input_plugin.get_blocksize      = cdda_plugin_get_blocksize;
  this->input_plugin.get_mrl            = cdda_plugin_get_mrl;
  this->input_plugin.get_optional_data  = cdda_plugin_get_optional_data;
  this->input_plugin.dispose            = cdda_plugin_dispose;
  this->input_plugin.input_class        = cls_gen;

  this->mrl = strdup(data);

  return &this->input_plugin;
}

static char ** cdda_class_get_autoplay_list (input_class_t *this_gen, 
					    int *num_files) {

  cdda_input_class_t *this = (cdda_input_class_t *) this_gen;
  cdrom_toc toc;
  char trackmrl[20];
  int fd, i;

  /* free old playlist */
  for( i = 0; this->autoplaylist[i]; i++ ) {
    free( this->autoplaylist[i] );
    this->autoplaylist[i] = NULL; 
  }  
  
  /* get the CD TOC */
  init_cdrom_toc(&toc);
  fd = open (DEFAULT_CDDA_DEVICE, O_RDONLY);
  if (fd == -1)
    return NULL;
  read_cdrom_toc(fd, &toc);
  
  for( i = 0; i <= toc.last_track - toc.first_track; i++ ) {
    sprintf(trackmrl,"cdda:%d",i+toc.first_track);
    this->autoplaylist[i] = strdup(trackmrl);    
  }

  *num_files = toc.last_track - toc.first_track + 1;

  free_cdrom_toc(&toc);
  return this->autoplaylist;
}

static char *cdda_class_get_identifier (input_class_t *this_gen) {
  return "cdda";
}

static char *cdda_class_get_description (input_class_t *this_gen) {
  return _("cdda input plugin");
}

static xine_mrl_t **cdda_class_get_dir (input_class_t *this_gen,
                                        const char *filename, int *nFiles) {

  cdda_input_class_t   *this = (cdda_input_class_t *) this_gen;

  *nFiles = 0; /* Unsupported */
  return this->mrls;
}

static void cdda_class_dispose (input_class_t *this_gen) {
  cdda_input_class_t  *this = (cdda_input_class_t *) this_gen;

  free (this->mrls);
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  cdda_input_class_t  *this;
  config_values_t     *config;

  this = (cdda_input_class_t *) xine_xmalloc (sizeof (cdda_input_class_t));

  this->xine   = xine;
  this->config = xine->config;
  config       = xine->config;

  this->input_class.open_plugin        = open_plugin;
  this->input_class.get_identifier     = cdda_class_get_identifier;
  this->input_class.get_description    = cdda_class_get_description;
  this->input_class.get_dir            = cdda_class_get_dir;
  this->input_class.get_autoplay_list  = cdda_class_get_autoplay_list;
  this->input_class.dispose            = cdda_class_dispose;
  this->input_class.eject_media        = NULL;

  this->mrls = (xine_mrl_t **) xine_xmalloc(sizeof(xine_mrl_t*));
  this->mrls_allocated_entries = 0;

  return this;
}

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT, 11, "CDDA", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

