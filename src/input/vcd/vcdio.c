/*
  $Id: vcdio.c,v 1.1 2003/10/13 11:47:11 f1rmb Exp $
 
  Copyright (C) 2002,2003 Rocky Bernstein <rocky@panix.com>
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Standard includes */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <errno.h>

#include <sys/wait.h>
#include <sys/ioctl.h>

#ifdef HAVE_VCDNAV
#include <libvcd/types.h>
#include <libvcd/files.h>
#include <cdio/iso9660.h>
#else
#include "libvcd/types.h"
#include "libvcd/files.h"
#include "cdio/iso9660.h"
#endif

#include "vcdplayer.h"
#include "vcdio.h"

#define LOG_ERR(this, s, args...) \
       if (this != NULL && this->log_err != NULL) \
          this->log_err("%s:  "s, __func__ , ##args)

#define FREE_AND_NULL(ptr) if (NULL != ptr) free(ptr); ptr = NULL;

/*! Closes VCD device specified via "this", and also wipes memory of it 
   from it inside "this". */
int
vcdio_close(vcdplayer_input_t *this) 
{
  this->opened = false;

  FREE_AND_NULL(this->current_vcd_device);
  FREE_AND_NULL(this->track);
  FREE_AND_NULL(this->segment);
  FREE_AND_NULL(this->entry); 
  
  return vcdinfo_close(this->vcd);
}


/*! Opens VCD device and initializes things.

   - do nothing if the device had already been open and is the same device. 
   - if the device had been open and is a different, close it before trying
     to open new device. 
*/
bool
vcdio_open(vcdplayer_input_t *this, char *intended_vcd_device) 
{
  vcdinfo_obj_t *obj = this->vcd;
  unsigned int i;

  dbg_print(INPUT_DBG_CALL, "called with %s\n", intended_vcd_device);

  if ( this->opened ) {
    if ( strcmp(intended_vcd_device, this->current_vcd_device)==0 ) {
      /* Already open and the same device, so do nothing */
      return true;
    } else {
      /* Changing VCD device */
      vcdio_close(this);
    }
  }

  if ( vcdinfo_open(&this->vcd, &intended_vcd_device, DRIVER_UNKNOWN, NULL) != 
       VCDINFO_OPEN_VCD) {
    return false;
  }

  obj = this->vcd;

  this->current_vcd_device=strdup(intended_vcd_device);
  this->opened            = true;
  this->num_LIDs          = vcdinfo_get_num_LIDs(obj);

  if (vcdinfo_read_psd (obj)) {

    vcdinfo_visit_lot (obj, false);

    if (vcdinfo_get_psd_x_size(obj))
      vcdinfo_visit_lot (obj, true);
  }

  /* 
     Save summary info on tracks, segments and entries... 
   */

  if ( 0 < (this->num_tracks = vcdinfo_get_num_tracks(obj)) ) {
    this->track = (vcdplayer_play_item_info *) 
      calloc(this->num_tracks, sizeof(vcdplayer_play_item_info));
    
    for (i=0; i<this->num_tracks; i++) { 
      unsigned int track_num=i+1;
      this->track[i].size      = vcdinfo_get_track_sect_count(obj, track_num);
      this->track[i].start_LSN = vcdinfo_get_track_lsn(obj, track_num);
    }
  } else 
    this->track = NULL;
    
  if ( 0 < (this->num_entries = vcdinfo_get_num_entries(obj)) ) {
    this->entry = (vcdplayer_play_item_info *) 
      calloc(this->num_entries, sizeof(vcdplayer_play_item_info));

    for (i=0; i<this->num_entries; i++) { 
      this->entry[i].size      = vcdinfo_get_entry_sect_count(obj, i);
      this->entry[i].start_LSN = vcdinfo_get_entry_lsn(obj, i);
    }
  } else 
    this->entry = NULL;
  
  if ( 0 < (this->num_segments = vcdinfo_get_num_segments(obj)) ) {
    this->segment = (vcdplayer_play_item_info *) 
      calloc(this->num_segments,  sizeof(vcdplayer_play_item_info));
    
    for (i=0; i<this->num_segments; i++) { 
      this->segment[i].size        = vcdinfo_get_seg_sector_count(obj, i);
      this->segment[i].start_LSN   = vcdinfo_get_seg_lsn(obj, i);
    }
  } else 
    this->segment = NULL;
  
  return true;
}

/*!
  seek position, return new position 

  if seeking failed, -1 is returned
*/
off_t 
vcdio_seek (vcdplayer_input_t *this, off_t offset, int origin) 
{

  switch (origin) {
  case SEEK_SET:
    {
      lsn_t old_lsn = this->cur_lsn;
      this->cur_lsn = this->origin_lsn + (offset / M2F2_SECTOR_SIZE);
      
      dbg_print(INPUT_DBG_SEEK_SET, "seek_set to %ld => %u (start is %u)\n", 
		(long int) offset, this->cur_lsn, this->origin_lsn);

      /* Seek was successful. Invalidate entry location by setting
         entry number back to 1. Over time it will adjust upward 
         to the correct value. */
      if ( !vcdplayer_pbc_is_on(this) 
           && this->play_item.type != VCDINFO_ITEM_TYPE_TRACK 
           && this->cur_lsn < old_lsn) {
        dbg_print(INPUT_DBG_SEEK_SET, "seek_set entry backwards\n");
        this->next_entry = 1;
      }
      break;
    }
    
  case SEEK_CUR: 
    {
      off_t diff;
      if (offset) {
        LOG_ERR(this, "%s: %d\n",
                _("SEEK_CUR not implemented for nozero offset"), 
                (int) offset);
        return (off_t) -1;
      }
      
      if (this->slider_length == VCDPLAYER_SLIDER_LENGTH_TRACK) {
        diff = this->cur_lsn - this->track_lsn;
        dbg_print(INPUT_DBG_SEEK_CUR, 
                  "current pos: %u, track diff %ld\n", 
                  this->cur_lsn, (long int) diff);
      } else {
        diff = this->cur_lsn - this->origin_lsn;
        dbg_print(INPUT_DBG_SEEK_CUR, 
                  "current pos: %u, entry diff %ld\n", 
                  this->cur_lsn, (long int) diff);
      }
      
      if (diff < 0) {
        dbg_print(INPUT_DBG_SEEK_CUR, "Error: diff < 0\n");
        return (off_t) 0;
      } else {
        return (off_t)diff * M2F2_SECTOR_SIZE;
      }
      
      break;
    }
    
  case SEEK_END:
    LOG_ERR(this, "%s\n", _("SEEK_END not implemented yet."));
    return (off_t) -1;
  default:
    LOG_ERR(this, "%s %d\n", _("seek not implemented yet for"),
	     origin);
    return (off_t) -1;
  }

  return offset ; /* FIXME */
}

/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
