/* 
  $Id: vcdplayer.h,v 1.1 2003/10/13 11:47:11 f1rmb Exp $

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

#ifndef _VCDPLAYER_H_
#define _VCDPLAYER_H_

#ifdef HAVE_VCDNAV
#include <libvcd/info.h>
#else
#include "libvcd/info.h"
#endif

#ifdef ENABLE_NLS
#include <locale.h>
#    include <libintl.h>
#    define _(String) dgettext ("libxine1", String)
#else
/* Stubs that do something close enough.  */
#    define _(String) (String)
#endif

/*------------------------------------------------------------------
  DEBUGGING
---------------------------------------------------------------------*/

/* Print *any* debug messages? */
#define INPUT_DEBUG 1

/* Debugging masks */

#define INPUT_DBG_META        1 /* Meta information */
#define INPUT_DBG_EVENT       2 /* input (keyboard/mouse) events */
#define INPUT_DBG_MRL         4 
#define INPUT_DBG_EXT         8 /* Calls from external routines */
#define INPUT_DBG_CALL       16 /* routine calls */
#define INPUT_DBG_LSN        32 /* LSN changes */
#define INPUT_DBG_PBC        64 /* Playback control */
#define INPUT_DBG_CDIO      128 /* Debugging from CDIO */
#define INPUT_DBG_SEEK_SET  256 /* Seeks to set location */
#define INPUT_DBG_SEEK_CUR  512 /* Seeks to find current location */
#define INPUT_DBG_STILL    1024 /* Still-frame */
#define INPUT_DBG_VCDINFO  2048 /* Debugging from VCDINFO */

/* Current debugging setting use above masks to interpret meaning of value. */
extern unsigned long int vcdplayer_debug;

#if INPUT_DEBUG
#define dbg_print(mask, s, args...) \
   if (vcdplayer_debug & mask) \
     fprintf(stderr, "%s: "s, __func__ , ##args)
#else
#define dbg_print(mask, s, args...) 
#endif

/*------------------------------------------------------------------
  General definitions and structures.
---------------------------------------------------------------------*/

#define VCDPLAYER_IN_STILL  65535

/* Some configuration enumerations. */
typedef enum {
  VCDPLAYER_SLIDER_LENGTH_AUTO,
  VCDPLAYER_SLIDER_LENGTH_TRACK,
  VCDPLAYER_SLIDER_LENGTH_ENTRY, 
} vcdplayer_slider_length_t;

typedef enum {
  VCDPLAYER_AUTOPLAY_TRACK   = VCDINFO_ITEM_TYPE_TRACK,
  VCDPLAYER_AUTOPLAY_ENTRY   = VCDINFO_ITEM_TYPE_ENTRY,
  VCDPLAYER_AUTOPLAY_SEGMENT = VCDINFO_ITEM_TYPE_SEGMENT,
  VCDPLAYER_AUTOPLAY_PBC     = VCDINFO_ITEM_TYPE_LID, 
} vcdplayer_autoplay_t;

typedef struct {
  lsn_t  start_LSN; /* LSN where play item starts */
  size_t size;      /* size in sector units of play item. */
} vcdplayer_play_item_info;

typedef int (*generic_fn)();

typedef struct vcdplayer_input_struct {
  void             *user_data;  /* environment. Passed to called routines. */
  int32_t           buttonN;
  vcdinfo_obj_t     *vcd;       /* Pointer to libvcd structures. */
  int               in_still;   /*  0 if not in still, 
                                   -2 if in infinite loop
                                   -5 if a still but haven't read wait time yet
                                   >0 number of seconds yet to wait */

  /*------------------------------------------------------------------
    Callback functions - players and higher-level routines can use
    this to customize their behavior when using this player-independent
    code. 
  ---------------------------------------------------------------------*/

  generic_fn        log_msg;     /* function to log a message in the player */
  generic_fn        log_err;     /* function to log an error in the player */


  /* Function to flush any audio or  video buffers */
  void (*flush_buffers) (void); 

  /* Function to flush sleep for a number of milliseconds. */
  void (*sleep) (unsigned int usecs); 

  /* Function to flush sleep for a number of milliseconds. */
  void (*force_redisplay) (void); 

  /* Function to handle player events. Returns true if play item changed. */
  bool (*handle_events) (void); 

  /* Function to update title of selection. */
  void (*update_title) ();

  /*-------------------------------------------------------------
     Playback control fields 
   --------------------------------------------------------------*/
  int              cur_lid;       /* LID that play item is in. Implies PBC is.
                                     on. VCDPLAYER_BAD_ENTRY if not none or 
                                     not in PBC */
  PsdListDescriptor pxd;          /* If PBC is on, the relevant PSD/PLD */
  int               pdi;          /* current pld index of pxd. -1 if 
                                     no index*/

  vcdinfo_itemid_t play_item;     /* play-item, VCDPLAYER_BAD_ENTRY if none */
  vcdinfo_itemid_t loop_item;     /* Where do we loop back to? Meaningful only
                                     in a selection list */
  int              loop_count;    /* # of times play-item has been played. 
                                     Meaningful only in a selection list.
                                   */
  track_t          cur_track;     /* current track number. */

  /*-----------------------------------
     Navigation and location fields
   ------------------------------------*/
  uint16_t   next_entry;    /* where to go if next is pressed, 
                               VCDPLAYER_BAD_ENTRY if none */
  uint16_t   prev_entry;    /* where to fo if prev is pressed, 
                               VCDPLAYER_BAD_ENTRY if none */
  uint16_t   return_entry;  /* Entry index to use if return is pressed */
  uint16_t   default_entry; /* Default selection entry. */

  lsn_t      cur_lsn;       /* LSN of where we are right now */
  lsn_t      end_lsn;       /* LSN of end of current entry/segment/track. */
  lsn_t      origin_lsn;    /* LSN of start of slider position. */
  lsn_t      track_lsn;     /* LSN of start track origin of track we are in. */
  lsn_t      track_end_lsn; /* LSN of end of current track (if entry). */

  /*--------------------------------------------------------------
    Medium information
   ---------------------------------------------------------------*/

  char       *current_vcd_device;   /* VCD device currently open */
  bool       opened;                /* true if initialized */

  track_t       num_tracks;   /* Number of tracks in medium */
  segnum_t      num_segments; /* Number of segments in medium */
  unsigned int  num_entries;  /* Number of entries in medium */
  lid_t         num_LIDs;     /* Number of LIDs in medium  */

  /* Tracks, segment, and entry information. The number of entries for
     each is given by the corresponding num_* field above.  */
  vcdplayer_play_item_info *track;
  vcdplayer_play_item_info *segment;
  vcdplayer_play_item_info *entry;

  /*--------------------------------------------------------------
    Configuration variables
   ---------------------------------------------------------------*/

  /* What type to use on autoplay */
  vcdplayer_autoplay_t default_autoplay;  

  /* When hitting end of entry or track do we advance automatically 
     to next entry/track or stop? Only valid if PBC is off. */
  bool                 autoadvance;     

  /* Do next/prev wrap around? Only valid if PBC is off. */   
  bool		       wrap_next_prev;	  

  /* Show and be able to select rejected LIDs? */
  bool		       show_rejected;	  

  /* Whether GUI slider is track size or entry size. */
  vcdplayer_slider_length_t slider_length; 

} vcdplayer_input_t;

/* vcdplayer_read return status */
typedef enum {
  READ_BLOCK,
  READ_STILL_FRAME,
  READ_ERROR,
  READ_END,
} vcdplayer_read_status_t;


/* ----------------------------------------------------------------------
   Function Prototypes 
  -----------------------------------------------------------------------*/

/*!
  Return true if playback control (PBC) is on
*/
bool
vcdplayer_pbc_is_on(const vcdplayer_input_t *this);

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
   %V : The volume set ID
   %v : The volume ID
       A number between 1 and the volume count.
   %% : a %
*/
char *
vcdplayer_format_str(vcdplayer_input_t *this, const char format_str[]);

/*!
  Update next/prev/return/default navigation buttons. 
*/
void
vcdplayer_update_nav(vcdplayer_input_t *this);

/*! Update the player title text. */
void 
vcdplayer_update_title_display(vcdplayer_input_t *this);

/*! Play title part. If part is -1, use the first title. */
void
vcdplayer_play(vcdplayer_input_t *this, vcdinfo_itemid_t itemid);

bool
vcdplayer_open(vcdplayer_input_t *this, char *intended_vcd_device);

/*!
  Read nlen bytes into buf and return the status back.
*/
vcdplayer_read_status_t
vcdplayer_read (vcdplayer_input_t *this, uint8_t *buf, const off_t nlen);

/*!
  seek position, return new position 

  if seeking failed, -1 is returned
*/
off_t 
vcdplayer_seek (vcdplayer_input_t *this, off_t offset, int origin);

/*!
  Get the number of tracks or titles of the VCD. The result is stored
  in "titles".
 */
void 
vcdplayer_send_button_update(vcdplayer_input_t *this, int mode);

lid_t
vcdplayer_selection2lid (vcdplayer_input_t *this, int entry_num);

#endif /* _VCDPLAYER_H_ */
/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
