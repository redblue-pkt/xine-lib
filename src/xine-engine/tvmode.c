/* 
 * Copyright (C) 2000-2002 the xine project
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
 * $Id: tvmode.c,v 1.11 2003/01/19 14:55:56 miguelfreitas Exp $
 *
 * tvmode - TV output selection
 *
 * Currently uses nvtvd (Dirk Thierbach <dthierbach@gmx.de>)
 * for setting TV mode
 * xine support hacked in by Matthias Hopf <mat@mshopf.de>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "xine_internal.h"
#include "xineutils.h"

#ifdef XINE_HAVE_NVTV
#include "nvtv/back_client.h"

/*
 * PRIVATE
 */

/* FIXME: currently the used backend only supports one (1) connection
 * to a server and needs this global, external pointer */
BackCardPtr        back_card   = 0;
BackAccessPtr      back_access = 0;

static int          current_type, current_width, current_height;
static double       current_fps;
/* static TVCrtRegs    old_crt; */
static TVRegs       old_tvregs;
static TVSettings   old_settings;

static int          tvmode_enabled = 1;
static int          was_enabled = 0;


/* TODO: config, and better */
static TVSystem  opt_system  = TV_SYSTEM_PAL;
static TVConnect opt_connect = CONNECT_NONE;

/* This is the list of possible modes for the used TV system.
 * TODO:
 * if select_origsize == false:
 *   The one closest (but larger) to the input stream size is selected.
 * if select_origsize == true:
 *   The first available mode is selected (stick to tv resolution) */
static int scan_mode_pal[][2] = {
    { 768, 576 }, { 800, 576 }, { 720, 576 },
    { 800, 600 },
    { 720, 480 }, { 640, 480 },
    { 800, 450 },
    { 1024, 768 },
    { 0 }
} ;

/* Overscan sizes to be scaned for - note that we do *not* scan for 'Small' */
static char *scan_overscan[] = {
    "Interl", "Huge", "Large", "DVD", "Normal", 0
} ;

/* TODO: flexible */
static double opt_aspect  = 4.0 / 3.0;

/* Just turn off warnings */
static void  _tvmode_init(xine_t *this);
void xine_tvmode_exit (xine_t *this);

/*
 * Config callback for tvmode enability.
 */
static void nvtvmode_enable_cb(void *this_gen, xine_cfg_entry_t *entry) {
  xine_t *this = (xine_t *) this_gen;

  tvmode_enabled = entry->num_value;
  
  if(!tvmode_enabled && was_enabled) {
    xine_tvmode_exit(this);
    was_enabled = 0;
  }
}



/* Try to connect to nvtvd server */
static void tvmode_connect(xine_t *this) {
    CardPtr main_card_list, main_card = NULL;
    
    if (back_client_avail ()) {
        main_card_list = back_client_init ();
    } else {
        fprintf (stderr, "tvmode: Nvtvd not detected, make sure nvtvd is running.\n");
        /* Handle well in some way... */
        return;
    }
    
    if (back_access) {
        back_access->closeCard ();
    }
    
    if (!main_card_list) {
        fprintf (stderr, "tvmode: No supported video card found.\n");
        /* Handle well in some way... */
        return;
    }

    /* Just pick the first, here we might want to pick an alternative card... */
    main_card = main_card_list;
    
    if (!main_card) {
        fprintf (stderr, "tvmode: No supported video card found at specified address.\n"); 
        /* Handle well in some way... */
        return;
    }

    
    if (back_access) {
        back_access->openCard (main_card);
        fprintf(stderr, "tvmode: Using card %s for tvout\n", main_card->name);
        was_enabled = 1;
    } else {
        fprintf(stderr, "tvmode: cannot connect to nvtvd - no TV mode switching available\n");
    }

    /* 

    CardInfo *card = 0;

    if (back_card)
	back_card->closeCard ();

    if (back_client_avail ()) {
	if (! (card = back_client_init ()))
	    back_card = 0;
    } else {
	back_card = 0;
    }

    if (back_card) {
      back_card->openCard (card);
      was_enabled = 1;
    }
    else
      printf("tvmode: cannot connect to nvtvd - no TV mode switching available\n");
      */
}


/* Disconnect from server */
static void tvmode_disconnect (xine_t *this) {
    back_access->closeCard ();
    back_access = 0;
}


/* Save current CRT and TV register configuration */
static void tvmode_savestate (xine_t *this) {
/*    back_card->getMode (&old_crt, &old_tv); */
    back_card->getSettings(&old_settings);
    back_card->getMode (&old_tvregs);
}


/* Restore CRT and TV register configuration */
static void tvmode_restorestate (xine_t *this) {
/*    back_card->setMode (0, &old_crt, &old_tv); */

    back_card->setModeSettings (&old_tvregs,&old_settings); 
    fprintf(stderr,"tvmode: Old mode saved!");
    current_type = 0;
}


/* Set CRT and TV registers to given TV-Out configuration */
static void tvmode_settvstate (xine_t *this, int width, int height, double fps) {
    TVSettings settings;
    TVMode     mode;
/*    TVCrtRegs  crt; */
 /*   TVRegs     tv; */
    int        found = 0;
    int        *scanm;
    char       **scano;
  
    
    /* TODO: do that at initialization and save possible combinations */
    /* Find supported TV mode */
    for (scanm = &scan_mode_pal[0][0]; *scanm && ! found; scanm += 2) {
	for (scano = scan_overscan; *scano && ! found; scano++) {
            fprintf(stderr,"tvmode: trying to use %dx%d %s\n",
		     scanm[0], scanm[1], *scano);
            /*
	    if (back_card->findBySize (opt_system, scanm[0], scanm[1], *scano,
                        &mode)) {
		current_width  = scanm[0];
		current_height = scanm[1];
                current_fps    = 25;	
                found++;
            } */
            if (back_card->findBySize (opt_system, 800, 600, "Large",
                        &mode)) {
                current_width  = 800;
                current_height = 600;
		current_fps    = 25;	/* TODO: currently this is PAL only */
		found++;
	    }
	}
    }
  
    /* Switch to mode */
    if (found) {
        /*	back_card->setModeSettings (TV_PRIV_TVMODE | TV_PRIV_DUALVIEW,
            &crt, &tv, &settings); */
        /*    mode.regs.devFlags = DEV_MONITOR | DEV_TELEVISION; */
        /* Modify the settings */

        back_card->getSettings (&settings);
        if (opt_connect > CONNECT_NONE) {
            settings.connector = opt_connect;
        } else {
            settings.connector = CONNECT_BOTH;
        }

        mode.regs.devFlags = DEV_MONITOR | DEV_TELEVISION;
        settings.flags |= TV_DESC_DUALVIEW;    

        back_card->setModeSettings (&mode.regs, &settings);

        printf("tvmode: Trying to use mode %i x %i\n",current_width,current_height);
        /*
           back_card->setMode(&mode.regs);
           back_card->setTestImage (NULL, &settings);
         */
	current_type = 1;
    } else {
	printf("tvmode: cannot find any valid TV mode - TV output disabled\n");
	xine_tvmode_exit (this);
    }
}

/*
 * PUBLIC
 */

/* Set to 'regular'(0) or 'tv'(1) state, that is if it is enabled */
int xine_tvmode_switch (xine_t *this, int type, int width, int height, double fps) {

  if(tvmode_enabled) {
    
    /* 
     * Wasn't initialized
     */
      if(!was_enabled) {
      _tvmode_init(this);
      }
    if (back_card) {
          fprintf(stderr, "tvmode: switching to %s\n", type ? "TV" : "default");

	switch (type) {
	case 0:
	  tvmode_restorestate (this);
	  break;
	case 1:
	    tvmode_settvstate (this, width, height, fps);
	    break;
	default:
	    printf("tvmode: illegal type for switching\n");
	    tvmode_restorestate (this);
	}
    } else {
	printf("tvmode: not connected to nvtvd for switching\n");
    }

  }

  return current_type;
}


/* Addapt (maximum) output size to visible area and set pixel aspect and fps */
void xine_tvmode_size (xine_t *this, int *width, int *height,
		       double *pixelratio, double *fps) {

  if(tvmode_enabled) {
    
    switch (current_type) {
    case 1:
      if (width  && *width > current_width)
	*width  = current_width;
      if (height && *height > current_height)
	*height = current_height;
      if (pixelratio)
	*pixelratio = ((double) current_width / current_height) / opt_aspect;
      if (fps)
	*fps = current_fps;
      break;
    }

  }
}

/* Connect to nvtvd server if possible and  fetch settings */
static void  _tvmode_init(xine_t *this) {
  if(tvmode_enabled) {
    tvmode_connect (this);
    if (back_card)
      tvmode_savestate (this);
  } else {
      printf("tvmode: not enabled\n");
  }
}
void xine_tvmode_init (xine_t *this) {
  
  printf("tvmode: Initializing tvmode\n");
  xine_config_register_bool (this, "misc.nv_tvmode", 0, _("NVidia TV-Out support."), NULL, 0, nvtvmode_enable_cb, this);
  
  /*
  tvmode_enabled = this->config->register_bool(this->config, "misc.nv_tvmode", 
					       0,
					       _("NVidia TV-Out support."),
					       NULL, 10,
					       nvtvmode_enable_cb, this);
  */
  _tvmode_init(this);
}

/* Restore old CRT and TV registers and close nvtvd connection */
void xine_tvmode_exit (xine_t *this) {

  if(tvmode_enabled || was_enabled) {
    if (back_card) {
      tvmode_restorestate (this);
      tvmode_disconnect (this);
    }
  }
}

#else

/* no nvtv support compiled in lib */

int xine_tvmode_switch (xine_t *this, int type, int width, int height, double fps) {
  /* not supported: return regular mode */
  return 0;
}

void xine_tvmode_size (xine_t *this, int *width, int *height,
		       double *pixelratio, double *fps) {
}

void xine_tvmode_init (xine_t *this) {
}

void xine_tvmode_exit (xine_t *this) {
}

#endif
