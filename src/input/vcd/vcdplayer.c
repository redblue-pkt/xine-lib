/* 
  $Id: vcdplayer.c,v 1.4 2004/03/11 08:08:48 rockyb Exp $
 
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
#include <unistd.h>
#include <time.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <errno.h>

#ifdef HAVE_VCDNAV
#include <libvcd/files.h>
#include <cdio/iso9660.h>
#else
#include "libvcd/files.h"
#include "cdio/iso9660.h"
#endif

#include "vcdplayer.h"
#include "vcdio.h"

#define LOG_ERR(this, s, args...) \
       if (this != NULL && this->log_err != NULL) \
          this->log_err("%s:  "s, __func__ , ##args)

unsigned long int vcdplayer_debug = 0;

static void  _vcdplayer_set_origin(vcdplayer_input_t *this);

/*!
  Return true if playback control (PBC) is on
*/
bool
vcdplayer_pbc_is_on(const vcdplayer_input_t *this) 
{
  return VCDINFO_INVALID_ENTRY != this->cur_lid; 
}

/* Given an itemid, return the size for the object (via information
   previously stored when opening the vcd). */
static size_t
_vcdplayer_get_item_size(vcdplayer_input_t *this, vcdinfo_itemid_t itemid) 
{
  switch (itemid.type) {
  case VCDINFO_ITEM_TYPE_ENTRY:
    return this->entry[itemid.num].size;
    break;
  case VCDINFO_ITEM_TYPE_SEGMENT:
    return this->segment[itemid.num].size;
    break;
  case VCDINFO_ITEM_TYPE_TRACK:
    return this->track[itemid.num-1].size;
    break;
  case VCDINFO_ITEM_TYPE_LID:
    /* Play list number (LID) */
    return 0;
    break;
  case VCDINFO_ITEM_TYPE_NOTFOUND:
  case VCDINFO_ITEM_TYPE_SPAREID2:
  default:
    LOG_ERR(this, "%s %d\n", _("bad item type"), itemid.type);
    return 0;
  }
}

#define add_format_str_info(val)			\
  {							\
    const char *str = val;				\
    unsigned int len;					\
    if (val != NULL) {					\
      len=strlen(str);					\
      if (len != 0) {					\
	strncat(tp, str, TEMP_STR_LEN-(tp-temp_str));	\
	tp += len;					\
      }							\
      saw_control_prefix = false;			\
    }							\
  }

#define add_format_num_info(val, fmt)			\
  {							\
    char num_str[10];					\
    unsigned int len;                                   \
    sprintf(num_str, fmt, val);				\
    len=strlen(num_str);                                \
    if (len != 0) {					\
      strncat(tp, num_str, TEMP_STR_LEN-(tp-temp_str));	\
      tp += len;					\
    }							\
    saw_control_prefix = false;				\
  }

/*!
   Take a format string and expand escape sequences, that is sequences that
   begin with %, with information from the current VCD. 
   The expanded string is returned. Here is a list of escape sequences:

   %A : The album information 
   %C : The VCD volume count - the number of CD's in the collection.
   %c : The VCD volume num - the number of the CD in the collection. 
   %F : The VCD Format, e.g. VCD 1.0, VCD 1.1, VCD 2.0, or SVCD
   %I : The current entry/segment/playback type, e.g. ENTRY, TRACK, SEGMENT...
   %L : The playlist ID prefixed with " LID" if it exists
   %N : The current number of the above - a decimal number
   %P : The publisher ID 
   %p : The preparer ID
   %S : If we are in a segment (menu), the kind of segment
   %T : The track number
   %V : The volume set ID
   %v : The volume ID
       A number between 1 and the volume count.
   %% : a %
*/
char *
vcdplayer_format_str(vcdplayer_input_t *this, const char format_str[])
{
#define TEMP_STR_SIZE 256
#define TEMP_STR_LEN (TEMP_STR_SIZE-1)
  static char    temp_str[TEMP_STR_SIZE];
  size_t i;
  char * tp = temp_str;
  bool saw_control_prefix = false;
  size_t format_len = strlen(format_str);
  vcdinfo_obj_t *obj = this->vcd;

  memset(temp_str, 0, TEMP_STR_SIZE);

  for (i=0; i<format_len; i++) {

    if (!saw_control_prefix && format_str[i] != '%') {
      *tp++ = format_str[i];
      saw_control_prefix = false;
      continue;
    }

    switch(format_str[i]) {
    case '%':
      if (saw_control_prefix) {
	*tp++ = '%';
      }
      saw_control_prefix = !saw_control_prefix;
      break;
    case 'A':
      add_format_str_info(vcdinfo_strip_trail(vcdinfo_get_album_id(obj), 
                                              MAX_ALBUM_LEN));
      break;

    case 'c':
      add_format_num_info(vcdinfo_get_volume_num(obj), "%d");
      break;

    case 'C':
      add_format_num_info(vcdinfo_get_volume_count(obj), "%d");
      break;

    case 'F':
      add_format_str_info(vcdinfo_get_format_version_str(obj));
      break;

    case 'I':
      {
	switch (this->play_item.type) {
	case VCDINFO_ITEM_TYPE_TRACK:
	  strncat(tp, "Track", TEMP_STR_LEN-(tp-temp_str));
	  tp += strlen("Track");
	break;
	case VCDINFO_ITEM_TYPE_ENTRY:  
	  strncat(tp, "Entry", TEMP_STR_LEN-(tp-temp_str));
	  tp += strlen("Entry");
	  break;
	case VCDINFO_ITEM_TYPE_SEGMENT:  
	  strncat(tp, "Segment", TEMP_STR_LEN-(tp-temp_str));
	  tp += strlen("Segment");
	  break;
	case VCDINFO_ITEM_TYPE_LID:  
	  strncat(tp, "List ID", TEMP_STR_LEN-(tp-temp_str));
	  tp += strlen("List ID");
	  break;
	case VCDINFO_ITEM_TYPE_SPAREID2:  
	  strncat(tp, "Navigation", TEMP_STR_LEN-(tp-temp_str));
	  tp += strlen("Navigation");
	  break;
	default:
	  /* What to do? */
          ;
	}
	saw_control_prefix = false;
      }
      break;

    case 'L':
      if (vcdplayer_pbc_is_on(this)) {
        char num_str[10];
        sprintf(num_str, " List ID %d", this->cur_lid);
        strncat(tp, num_str, TEMP_STR_LEN-(tp-temp_str));
        tp += strlen(num_str);
      }
      saw_control_prefix = false;
      break;

    case 'N':
      add_format_num_info(this->play_item.num, "%d");
      break;

    case 'p':
      add_format_str_info(vcdinfo_get_preparer_id(obj));
      break;

    case 'P':
      add_format_str_info(vcdinfo_get_publisher_id(obj));
      break;

    case 'S':
      if ( VCDINFO_ITEM_TYPE_SEGMENT==this->play_item.type ) {
        char seg_type_str[10];

        sprintf(seg_type_str, " %s", 
                vcdinfo_video_type2str(obj, this->play_item.num));
        strncat(tp, seg_type_str, TEMP_STR_LEN-(tp-temp_str));
        tp += strlen(seg_type_str);
      }
      saw_control_prefix = false;
      break;

    case 'T':
      add_format_num_info(this->cur_track, "%d");
      break;

    case 'V':
      add_format_str_info(vcdinfo_get_volumeset_id(obj));
      break;

    case 'v':
      add_format_str_info(vcdinfo_get_volume_id(obj));
      break;

    default:
      *tp++ = '%'; 
      *tp++ = format_str[i];
      saw_control_prefix = false;
    }
  }
  return strdup(temp_str);
}

static void
_vcdplayer_update_entry(vcdinfo_obj_t *obj, uint16_t ofs, uint16_t *entry, 
                        const char *label)
{
  if ( ofs == VCDINFO_INVALID_OFFSET ) {
    *entry = VCDINFO_INVALID_ENTRY;
  } else {
    vcdinfo_offset_t *off = vcdinfo_get_offset_t(obj, ofs);
    if (off != NULL) {
      *entry = off->lid;
      dbg_print(INPUT_DBG_PBC, "%s: %d\n", label, off->lid);
    } else
      *entry = VCDINFO_INVALID_ENTRY;
  }
}

/*!
  Update next/prev/return/default navigation buttons (via this->cur_lid).
  Update size of play-item (via this->play_item).
*/
void
vcdplayer_update_nav(vcdplayer_input_t *this)
{
  int play_item = this->play_item.num;
  vcdinfo_obj_t *obj = this->vcd;

  int min_entry = 1;
  int max_entry = 0;

  if  (vcdplayer_pbc_is_on(this)) {
    
    vcdinfo_lid_get_pxd(obj, &(this->pxd), this->cur_lid);
    
    switch (this->pxd.descriptor_type) {
    case PSD_TYPE_SELECTION_LIST:
    case PSD_TYPE_EXT_SELECTION_LIST:
      if (this->pxd.psd == NULL) return;
      _vcdplayer_update_entry(obj, vcdinf_psd_get_prev_offset(this->pxd.psd), 
                              &(this->prev_entry), "prev");
      
      _vcdplayer_update_entry(obj, vcdinf_psd_get_next_offset(this->pxd.psd), 
                              &(this->next_entry), "next");
      
      _vcdplayer_update_entry(obj, vcdinf_psd_get_return_offset(this->pxd.psd),
                              &(this->return_entry), "return");

      _vcdplayer_update_entry(obj, 
                              vcdinfo_get_default_offset(obj, this->cur_lid), 
                              &(this->default_entry), "default");
      break;
    case PSD_TYPE_PLAY_LIST:
      if (this->pxd.pld == NULL) return;
      _vcdplayer_update_entry(obj, vcdinf_pld_get_prev_offset(this->pxd.pld), 
                              &(this->prev_entry), "prev");
      
      _vcdplayer_update_entry(obj, vcdinf_pld_get_next_offset(this->pxd.pld), 
                              &(this->next_entry), "next");
      
      _vcdplayer_update_entry(obj, vcdinf_pld_get_return_offset(this->pxd.pld),
                              &(this->return_entry), "return");
      this->default_entry = VCDINFO_INVALID_ENTRY;
      break;
    case PSD_TYPE_END_LIST:
      this->origin_lsn = this->cur_lsn = this->end_lsn = VCDINFO_NULL_LSN;
      /* Fall through */
    case PSD_TYPE_COMMAND_LIST:
      this->next_entry = this->prev_entry = this->return_entry =
      this->default_entry = VCDINFO_INVALID_ENTRY;
      break;
    }
    
    this->update_title();
    return;
  }

  /* PBC is not on. Set up for simplified next, prev, and return. */
  
  switch (this->play_item.type) {
  case VCDINFO_ITEM_TYPE_ENTRY: 
  case VCDINFO_ITEM_TYPE_SEGMENT: 
  case VCDINFO_ITEM_TYPE_TRACK: 

    switch (this->play_item.type) {
    case VCDINFO_ITEM_TYPE_ENTRY: 
      max_entry = this->num_entries;
      min_entry = 0; /* Can remove when Entries start at 1. */
      this->cur_track = vcdinfo_get_track(obj, play_item);
      this->track_lsn = vcdinfo_get_track_lsn(obj, this->cur_track);
      break;
    case VCDINFO_ITEM_TYPE_SEGMENT: 
      max_entry       = this->num_segments;
      this->cur_track = VCDINFO_INVALID_TRACK;
      
      break;
    case VCDINFO_ITEM_TYPE_TRACK: 
      max_entry       = this->num_tracks;
      this->cur_track = this->play_item.num;
      this->track_lsn = vcdinfo_get_track_lsn(obj, this->cur_track);
      break;
    default: ; /* Handle exceptional cases below */
    }
        
    _vcdplayer_set_origin(this);
    /* Set next, prev, return and default to simple and hopefully
       useful values.
     */
    if (play_item+1 >= max_entry) 
      this->next_entry = VCDINFO_INVALID_ENTRY;
    else 
      this->next_entry = play_item+1;
    
    if (play_item-1 >= min_entry) 
      this->prev_entry = play_item-1;
    else 
      this->prev_entry = VCDINFO_INVALID_ENTRY;
    
    this->default_entry = play_item;
    this->return_entry  = min_entry;
    break;

  case VCDINFO_ITEM_TYPE_LID: 
    {
      /* Should have handled above. */
      break;
    }
  default: ;
  }
  this->update_title();
}

/*!
  Set reading to play an entire track.
*/
static void
_vcdplayer_set_track(vcdplayer_input_t *this, unsigned int track_num) 
{
  if (track_num < 1 || track_num > this->num_tracks) 
    return;
  else {
    vcdinfo_obj_t *obj = this->vcd;
    vcdinfo_itemid_t itemid;

    itemid.num      = track_num;
    itemid.type     = VCDINFO_ITEM_TYPE_TRACK;
    this->in_still  = 0;
    this->cur_lsn   = vcdinfo_get_track_lsn(obj, track_num);
    this->play_item = itemid;
    this->cur_track = track_num;
    this->track_lsn = this->cur_lsn;

    _vcdplayer_set_origin(this);

    dbg_print(INPUT_DBG_LSN, "LSN: %u\n", this->cur_lsn);
  }
}

/*!
  Set reading to play an entry
*/
static void
_vcdplayer_set_entry(vcdplayer_input_t *this, unsigned int num) 
{
  vcdinfo_obj_t *obj = this->vcd;
  unsigned int num_entries = vcdinfo_get_num_entries(obj);

  if (num >= num_entries) {
    LOG_ERR(this, "%s %d\n", _("bad entry number"), num);
    return;
  } else {
    vcdinfo_itemid_t itemid;

    itemid.num          = num;
    itemid.type         = VCDINFO_ITEM_TYPE_ENTRY;
    this->in_still      = 0;
    this->cur_lsn       = vcdinfo_get_entry_lsn(obj, num);
    this->play_item     = itemid;
    this->cur_track     = vcdinfo_get_track(obj, num);
    this->track_lsn     = vcdinfo_get_track_lsn(obj, this->cur_track);
    this->track_end_lsn = this->track_lsn + 
      this->track[this->cur_track-1].size;

    _vcdplayer_set_origin(this);

    dbg_print(INPUT_DBG_LSN, "LSN: %u, track_end LSN: %u\n", 
              this->cur_lsn, this->track_end_lsn);
  }
}

/*!
  Set reading to play an segment (e.g. still frame)
*/
static void
_vcdplayer_set_segment(vcdplayer_input_t *this, unsigned int num) 
{
  vcdinfo_obj_t *obj = this->vcd;
  segnum_t num_segs  = vcdinfo_get_num_segments(obj);

  if (num >= num_segs) {
    LOG_ERR(this, "%s %d\n", _("bad segment number"), num);
    return;
  } else {
    vcdinfo_itemid_t itemid;

    this->cur_lsn   = vcdinfo_get_seg_lsn(obj, num);
    this->cur_track = 0;

    if (VCDINFO_NULL_LSN==this->cur_lsn) {
      LOG_ERR(this, "%s %d\n", 
              _("Error in getting current segment number"), num);
      return;
    }
    
    itemid.num = num;
    itemid.type = VCDINFO_ITEM_TYPE_SEGMENT;
    this->play_item = itemid;

    _vcdplayer_set_origin(this);
    
    dbg_print(INPUT_DBG_LSN, "LSN: %u\n", this->cur_lsn);
  }
}

/* Play entry. */
/* Play a single item. */
static void
vcdplayer_play_single_item(vcdplayer_input_t *this, vcdinfo_itemid_t itemid)
{
  vcdinfo_obj_t *obj = this->vcd;

  dbg_print(INPUT_DBG_CALL, "called itemid.num: %d, itemid.type: %d\n", 
            itemid.num, itemid.type);

  this->in_still = 0;

  switch (itemid.type) {
  case VCDINFO_ITEM_TYPE_SEGMENT: 
    {
      vcdinfo_video_segment_type_t segtype 
        = vcdinfo_get_video_type(obj, itemid.num);
      segnum_t num_segs = vcdinfo_get_num_segments(obj);

      dbg_print(INPUT_DBG_PBC, "%s (%d), itemid.num: %d\n", 
                vcdinfo_video_type2str(obj, itemid.num), 
                (int) segtype, itemid.num);

      if (itemid.num >= num_segs) return;
      _vcdplayer_set_segment(this, itemid.num);
      
      switch (segtype)
        {
        case VCDINFO_FILES_VIDEO_NTSC_STILL:
        case VCDINFO_FILES_VIDEO_NTSC_STILL2:
        case VCDINFO_FILES_VIDEO_PAL_STILL:
        case VCDINFO_FILES_VIDEO_PAL_STILL2:
          this->in_still = -5;
          break;
        default:
          this->in_still = 0;
        }
      
      break;
    }
    
  case VCDINFO_ITEM_TYPE_TRACK:
    dbg_print(INPUT_DBG_PBC, "track %d\n", itemid.num);
    if (itemid.num < 1 || itemid.num > this->num_tracks) return;
    _vcdplayer_set_track(this, itemid.num);
    break;
    
  case VCDINFO_ITEM_TYPE_ENTRY: 
    {
      unsigned int num_entries = vcdinfo_get_num_entries(obj);
      dbg_print(INPUT_DBG_PBC, "entry %d\n", itemid.num);
      if (itemid.num >= num_entries) return;
      _vcdplayer_set_entry(this, itemid.num);
      break;
    }
    
  case VCDINFO_ITEM_TYPE_LID:
    LOG_ERR(this, "%s\n", _("Should have converted this above"));
    break;

  case VCDINFO_ITEM_TYPE_NOTFOUND:
    dbg_print(INPUT_DBG_PBC, "play nothing\n");
    this->cur_lsn = this->end_lsn;
    return;

  default:
    LOG_ERR(this, "item type %d not implemented.\n", itemid.type);
    return;
  }
  
  this->play_item = itemid;

  vcdplayer_update_nav(this);

  /* Some players like xine, have a fifo queue of audio and video buffers
     that need to be flushed when playing a new selection. */
  /*  if (this->flush_buffers)
      this->flush_buffers(); */

}

/*
  Get the next play-item in the list given in the LIDs. Note play-item
  here refers to list of play-items for a single LID It shouldn't be
  confused with a user's list of favorite things to play or the 
  "next" field of a LID which moves us to a different LID.
 */
static bool
_vcdplayer_inc_play_item(vcdplayer_input_t *this)
{
  int noi;

  dbg_print(INPUT_DBG_CALL, "called pli: %d\n", this->pdi);

  if ( NULL == this || NULL == this->pxd.pld  ) return false;

  noi = vcdinf_pld_get_noi(this->pxd.pld);
  
  if ( noi <= 0 ) return false;
  
  /* Handle delays like autowait or wait here? */

  this->pdi++;

  if ( this->pdi < 0 || this->pdi >= noi ) return false;

  else {
    uint16_t trans_itemid_num=vcdinf_pld_get_play_item(this->pxd.pld, 
                                                       this->pdi);
    vcdinfo_itemid_t trans_itemid;

    if (VCDINFO_INVALID_ITEMID == trans_itemid_num) return false;
    
    vcdinfo_classify_itemid(trans_itemid_num, &trans_itemid);
    dbg_print(INPUT_DBG_PBC, "  play-item[%d]: %s\n",
              this->pdi, vcdinfo_pin2str (trans_itemid_num));
    vcdplayer_play_single_item(this, trans_itemid);
    return true;
  }
}

void
vcdplayer_play(vcdplayer_input_t *this, vcdinfo_itemid_t itemid)
{
  dbg_print(INPUT_DBG_CALL, "called itemid.num: %d itemid.type: %d\n", 
            itemid.num, itemid.type);

  if  (!vcdplayer_pbc_is_on(this)) {
    vcdplayer_play_single_item(this, itemid);
  } else {
    /* PBC on - Itemid.num is LID. */

    vcdinfo_obj_t *obj = this->vcd;

    if (obj == NULL) return;

    this->cur_lid = itemid.num;
    vcdinfo_lid_get_pxd(obj, &(this->pxd), itemid.num);
    
    switch (this->pxd.descriptor_type) {
      
    case PSD_TYPE_SELECTION_LIST:
    case PSD_TYPE_EXT_SELECTION_LIST: {
      vcdinfo_itemid_t trans_itemid;
      uint16_t trans_itemid_num;

      if (this->pxd.psd == NULL) return;
      trans_itemid_num  = vcdinf_psd_get_itemid(this->pxd.psd);
      vcdinfo_classify_itemid(trans_itemid_num, &trans_itemid);
      this->loop_count = 1;
      this->loop_item  = trans_itemid;
      vcdplayer_play_single_item(this, trans_itemid);
      break;
    }
      
    case PSD_TYPE_PLAY_LIST: {
      if (this->pxd.pld == NULL) return;
      this->pdi = -1;
      _vcdplayer_inc_play_item(this);
      break;
    }
      
    case PSD_TYPE_END_LIST:
    case PSD_TYPE_COMMAND_LIST:
      
    default:
      ;
    }
  }
}

/* 
   Set's start origin and size for subsequent seeks.  
   input: this->cur_lsn, this->play_item
   changed: this->origin_lsn, this->end_lsn
*/
static void 
_vcdplayer_set_origin(vcdplayer_input_t *this)
{
  size_t size = _vcdplayer_get_item_size(this, this->play_item);

  this->end_lsn    = this->cur_lsn + size;
  this->origin_lsn = this->cur_lsn;

  dbg_print((INPUT_DBG_CALL|INPUT_DBG_LSN), "end LSN: %u\n", this->end_lsn);
}

#define RETURN_NULL_BLOCK \
  memset (buf, 0, M2F2_SECTOR_SIZE); \
  buf[0] = 0;  buf[1] = 0; buf[2] = 0x01; \
  return READ_BLOCK

#define RETURN_NULL_STILL \
  memset (buf, 0, M2F2_SECTOR_SIZE); \
  buf[0] = 0;  buf[1] = 0; buf[2] = 0x01; \
  return READ_STILL_FRAME

#define SLEEP_1_SEC_AND_HANDLE_EVENTS \
  if (this->handle_events()) goto skip_next_play; \
  this->sleep(250000);                            \
  if (this->handle_events()) goto skip_next_play; \
  this->sleep(250000);                            \
  if (this->handle_events()) goto skip_next_play; \
  this->sleep(250000);                            \
  if (this->handle_events()) goto skip_next_play; \
  this->sleep(250000);                            
/*  if (this->in_still) this->force_redisplay();    */


/* Handles PBC navigation when reaching the end of a play item. */
static vcdplayer_read_status_t
vcdplayer_pbc_nav (vcdplayer_input_t *this, uint8_t *buf)
{
  /* We are in playback control. */
  vcdinfo_itemid_t itemid;

  if (0 != this->in_still && this->in_still != -5) {
      SLEEP_1_SEC_AND_HANDLE_EVENTS;
      if (this->in_still > 0) this->in_still--;
      return READ_STILL_FRAME;
  }
  
  /* The end of an entry is really the end of the associated 
     sequence (or track). */
  
  if ( (VCDINFO_ITEM_TYPE_ENTRY == this->play_item.type) && 
       (this->cur_lsn < this->track_end_lsn) ) {
    /* Set up to just continue to the next entry */
    this->play_item.num++;
    dbg_print( (INPUT_DBG_LSN|INPUT_DBG_PBC), 
               "continuing into next entry: %u\n", this->play_item.num);
    vcdplayer_play_single_item(this, this->play_item);
    this->update_title();
    goto skip_next_play;
  }
  
  switch (this->pxd.descriptor_type) {
  case PSD_TYPE_END_LIST:
    return READ_END;
    break;
  case PSD_TYPE_PLAY_LIST: {
    int wait_time = vcdinf_get_wait_time(this->pxd.pld);
    
    dbg_print(INPUT_DBG_PBC, "playlist wait_time: %d\n", wait_time);
    
    if (_vcdplayer_inc_play_item(this))
      goto skip_next_play;

    /* Handle any wait time given. */
    if (-5 == this->in_still) {
      if (wait_time != 0) {
        this->in_still = wait_time - 1;
        SLEEP_1_SEC_AND_HANDLE_EVENTS ;
        return READ_STILL_FRAME;
      }
    }
    break;
  }
  case PSD_TYPE_SELECTION_LIST:     /* Selection List (+Ext. for SVCD) */
  case PSD_TYPE_EXT_SELECTION_LIST: /* Extended Selection List (VCD2.0) */
    {
      int wait_time         = vcdinf_get_timeout_time(this->pxd.psd);
      uint16_t timeout_offs = vcdinf_get_timeout_offset(this->pxd.psd);
      uint16_t max_loop     = vcdinf_get_loop_count(this->pxd.psd);
      vcdinfo_offset_t *offset_timeout_LID = 
        vcdinfo_get_offset_t(this->vcd, timeout_offs);
      
      dbg_print(INPUT_DBG_PBC, "wait_time: %d, looped: %d, max_loop %d\n", 
                wait_time, this->loop_count, max_loop);
      
      /* Handle any wait time given */
      if (-5 == this->in_still) {
        this->in_still = wait_time - 1;
        SLEEP_1_SEC_AND_HANDLE_EVENTS ;
        return READ_STILL_FRAME;
      }
      
      /* Handle any looping given. */
      if ( max_loop == 0 || this->loop_count < max_loop ) {
        this->loop_count++;
        if (this->loop_count == 0x7f) this->loop_count = 0;
        vcdplayer_play_single_item(this, this->loop_item);
        if (this->in_still) this->force_redisplay();
        goto skip_next_play;
      }
      
      /* Looping finished and wait finished. Move to timeout
         entry or next entry, or handle still. */
      
      if (NULL != offset_timeout_LID) {
        /* Handle timeout_LID */
        itemid.num  = offset_timeout_LID->lid;
        itemid.type = VCDINFO_ITEM_TYPE_LID;
        dbg_print(INPUT_DBG_PBC, "timeout to: %d\n", itemid.num);
        vcdplayer_play(this, itemid);
        goto skip_next_play;
      } else {
        int num_selections = vcdinf_get_num_selections(this->pxd.psd);
        if (num_selections > 0) {
          /* Pick a random selection. */
          unsigned int bsn=vcdinf_get_bsn(this->pxd.psd);
          int rand_selection=bsn +
            (int) ((num_selections+0.0)*rand()/(RAND_MAX+1.0));
          lid_t rand_lid=vcdplayer_selection2lid (this, rand_selection);
          itemid.num = rand_lid;
          itemid.type = VCDINFO_ITEM_TYPE_LID;
          dbg_print(INPUT_DBG_PBC, "random selection %d, lid: %d\n", 
                    rand_selection - bsn, rand_lid);
          vcdplayer_play(this, itemid);
          goto skip_next_play;
        } else if (this->in_still) {
          /* Hack: Just go back and do still again */
          SLEEP_1_SEC_AND_HANDLE_EVENTS ;
          RETURN_NULL_STILL ;
        }
      }
      
      break;
    }
  case VCDINFO_ITEM_TYPE_NOTFOUND:  
    LOG_ERR(this, "NOTFOUND in PBC -- not supposed to happen\n");
    break;
  case VCDINFO_ITEM_TYPE_SPAREID2:  
    LOG_ERR(this, "SPAREID2 in PBC -- not supposed to happen\n");
    break;
  case VCDINFO_ITEM_TYPE_LID:  
    LOG_ERR(this, "LID in PBC -- not supposed to happen\n");
    break;
    
  default:
    ;
  }
  /* FIXME: Should handle autowait ...  */
  itemid.num  = this->next_entry;
  itemid.type = VCDINFO_ITEM_TYPE_LID;
  vcdplayer_play(this, itemid);
 skip_next_play: ;
  return READ_BLOCK;
}

/* Handles navigation when NOT in PBC reaching the end of a play item. 
   The navigations rules here we are sort of made up, but the intent 
   is to do something that's probably right or helpful.
*/
static vcdplayer_read_status_t
vcdplayer_non_pbc_nav (vcdplayer_input_t *this, uint8_t *buf)
{
  /* Not in playback control. Do we advance automatically or stop? */
  switch (this->play_item.type) {
  case VCDINFO_ITEM_TYPE_TRACK:
  case VCDINFO_ITEM_TYPE_ENTRY:
    if (this->autoadvance && this->next_entry != VCDINFO_INVALID_ENTRY) {
      this->play_item.num=this->next_entry;
      vcdplayer_update_nav(this);
    } else 
      return READ_END;
    break;
  case VCDINFO_ITEM_TYPE_SPAREID2:  
    /* printf("SPAREID2\n"); */
    if (this->in_still) {
      RETURN_NULL_STILL ;
      /* Hack: Just go back and do still again */
      /*this->force_redisplay();
        this->cur_lsn = this->origin_lsn;*/
    } 
    return READ_END;

  case VCDINFO_ITEM_TYPE_NOTFOUND:  
    LOG_ERR(this, "NOTFOUND outside PBC -- not supposed to happen\n");
    if (this->in_still) {
      RETURN_NULL_STILL ;
      /* Hack: Just go back and do still again */
      /*this->force_redisplay();
        this->cur_lsn = this->origin_lsn;*/
    } else 
      return READ_END;
    break;

  case VCDINFO_ITEM_TYPE_LID:  
    LOG_ERR(this, "LID outside PBC -- not supposed to happen\n");
    if (this->in_still) {
      RETURN_NULL_STILL ;
      /* Hack: Just go back and do still again */
      /* this->force_redisplay();
         this->cur_lsn = this->origin_lsn; */
    } else 
      return READ_END;
    break;

  case VCDINFO_ITEM_TYPE_SEGMENT:
    if (this->in_still) {
      /* Hack: Just go back and do still again */
      RETURN_NULL_STILL ;
    }
    return READ_END;
  }
  return READ_BLOCK;
}


/*!
  Read nlen bytes into buf and return the status back.

  This routine is a bit complicated because on reaching the end of 
  a track or entry we may automatically advance to the item, or 
  interpret the next item in the playback-control list.
*/
vcdplayer_read_status_t
vcdplayer_read (vcdplayer_input_t *this, uint8_t *buf, const off_t nlen) 
{

  this->handle_events ();

  if ( this->cur_lsn >= this->end_lsn ) {
    vcdplayer_read_status_t read_status;

    /* We've run off of the end of this entry. Do we continue or stop? */
    dbg_print( (INPUT_DBG_LSN|INPUT_DBG_PBC), 
              "end reached, cur: %u, end: %u\n", this->cur_lsn, this->end_lsn);

  handle_item_continuation:
    read_status = vcdplayer_pbc_is_on(this) 
      ? vcdplayer_pbc_nav(this, buf) 
      : vcdplayer_non_pbc_nav(this, buf);

    if (READ_BLOCK != read_status) return read_status;
  }

  /* Read the next block. 
     
    Important note: we probably speed things up by removing "data"
    and the memcpy to it by extending vcd_image_source_read_mode2
    to allow a mode to do what's below in addition to its 
    "raw" and "block" mode. It also would probably improve the modularity
    a little bit as well.
  */

  {
    CdIo *img = vcdinfo_get_cd_image(this->vcd);
    typedef struct {
      uint8_t subheader	[8];
      uint8_t data	[M2F2_SECTOR_SIZE];
      uint8_t spare     [4];
      
    } vcdsector_t;
    vcdsector_t vcd_sector;

    do {
      dbg_print(INPUT_DBG_LSN, "LSN: %u\n", this->cur_lsn);
      if (cdio_read_mode2_sector(img, &vcd_sector, this->cur_lsn, true)!=0) {
        dbg_print(INPUT_DBG_LSN, "read error\n");
        return READ_ERROR;
      }
      this->cur_lsn++;

      if ( this->cur_lsn >= this->end_lsn ) {
        /* We've run off of the end of this entry. Do we continue or stop? */
        dbg_print( (INPUT_DBG_LSN|INPUT_DBG_PBC), 
                   "end reached in reading, cur: %u, end: %u\n", 
                   this->cur_lsn, this->end_lsn);
        break;
      }
      
      /* Check header ID for a padding sector and simply discard
         these.  It is alleged that VCD's put these in to keep the
         bitrate constant.
      */
    } while((vcd_sector.subheader[2]&~0x01)==0x60);

    if ( this->cur_lsn >= this->end_lsn ) 
      /* We've run off of the end of this entry. Do we continue or stop? */
      goto handle_item_continuation;
      
    memcpy (buf, vcd_sector.data, M2F2_SECTOR_SIZE);
    return READ_BLOCK;
  }
}

/* Do if needed */
void 
vcdplayer_send_button_update(vcdplayer_input_t *this, const int mode)
{
  /* dbg_print(INPUT_DBG_CALL, "Called\n"); */
  return;
}

lid_t
vcdplayer_selection2lid (vcdplayer_input_t *this, int entry_num) 
{
  /* FIXME: Some of this probably gets moved to vcdinfo. */
  /* Convert selection number to lid and then entry number...*/
  unsigned int offset;
  unsigned int bsn=vcdinf_get_bsn(this->pxd.psd);
  vcdinfo_obj_t *obj = this->vcd;

  dbg_print( (INPUT_DBG_CALL|INPUT_DBG_PBC), 
            "Called lid %u, entry_num %d bsn %d\n", this->cur_lid, 
             entry_num, bsn);

  if ( (entry_num - bsn + 1) > 0) {
    offset = vcdinfo_lid_get_offset(obj, this->cur_lid, entry_num-bsn+1);
  } else {
    LOG_ERR(this, "Selection number %u too small. bsn %u\n", 
            entry_num, bsn);
    return VCDINFO_INVALID_LID;
  }
  
  if (offset != VCDINFO_INVALID_OFFSET) {
    vcdinfo_offset_t *ofs;
    int old = entry_num;
    
    switch (offset) {
    case PSD_OFS_DISABLED:
      LOG_ERR(this, "Selection %u disabled\n", entry_num);
      return VCDINFO_INVALID_LID;
    case PSD_OFS_MULTI_DEF:
      LOG_ERR(this, "Selection %u multi_def\n", entry_num);
      return VCDINFO_INVALID_LID;
    case PSD_OFS_MULTI_DEF_NO_NUM:
      LOG_ERR(this, "Selection %u multi_def_no_num\n", entry_num);
      return VCDINFO_INVALID_LID;
    default: ;
    }
    
    ofs = vcdinfo_get_offset_t(obj, offset);

    if (NULL == ofs) {
      LOG_ERR(this, "error in vcdinfo_get_offset\n");
      return -1;
    }
    dbg_print(INPUT_DBG_PBC,
              "entry %u turned into selection lid %u\n", 
              old, ofs->lid);
    return ofs->lid;
    
  } else {
    LOG_ERR(this, "invalid or unset entry %u\n", entry_num);
    return VCDINFO_INVALID_LID;
  }
}

/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
