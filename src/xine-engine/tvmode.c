/* 
 * Copyright (C) 2000-2002 the xine project
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
 * $Id: tvmode.c,v 1.6 2002/08/02 14:09:05 mshopf Exp $
 *
 * tvmode - TV output selection
 *
 * Currently uses nvtvd (Dirk Thierbach <dthierbach@gmx.de>)
 * for setting TV mode
 * xine support hacked in by Matthias Hopf <mat@mshopf.de>
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>

#include "nvtvd.h"
#include "xine_internal.h"
/* FIXME: how to include that? */
/*#include "xine.h" */

#include "xine_internal.h"
#include "xineutils.h"

#define ABS(x)      ((x)>0?(x):-(x))
#define APPROX(x,y) (ABS((x)-(y)) < 1e-2)	/* less than 1% difference */

/*
 * PRIVATE
 */

/* TODO: config */
static TVConnect opt_connect = CONNECT_AUTO;
static int       opt_flicker = -1;

typedef struct {
    int width, height;
    int fps;
    int quality;
    char overscan[16];
} scan_mode_t;

/* FIXME: currently the used backend only supports one (1) connection
 * to a server and needs this global, external pointer */
/* Thus we do not care about saving data in a own structure now... */
BackCardRec        *back_card   = 0;
BackAccessRec      *back_access = 0;

static int          tv_stream_width, tv_stream_height;
static double       tv_stream_fps;
static int          tv_current_type, tv_current_system;
static int          tv_current_width, tv_current_height;
static double       tv_current_fps;
static TVCrtRegs    tv_old_crt;
static TVRegs       tv_old_tv;

static int          tv_verbose;
static int          tv_capabilities;
static int          tv_policy;
static int          tv_prefered_fps;
static double       tv_aspect;

/* This is the list of possible modes for the used refresh rates */
static const char *tv_scan_mode;

/* Constant data */
enum { TV_CAP_PAL = 1, TV_CAP_PAL60 = 2, TV_CAP_NTSC = 4 };
static char *tv_systems_list[] = {
    "No TV connected", "PAL only", "NTSC only",
    "PAL and PAL60", "PAL and NTSC", "PAL, PAL60, NTSC", NULL
};
static const int tv_system_caps[] = {
    0, TV_CAP_PAL, TV_CAP_NTSC,
    TV_CAP_PAL|TV_CAP_PAL60, TV_CAP_PAL|TV_CAP_NTSC,
    TV_CAP_PAL|TV_CAP_PAL60|TV_CAP_NTSC
};

#define SCAN_NATIVE_25 576
#define SCAN_NATIVE_30 480
static char *tv_scan_mode_default =
"768x576*25,800x576*25,720x576*25,704x576*25,"	/* Best PAL modes */
"768x480*30,800x480*30,720x480*30,"		/* Best NTSC modes */
"640x576*25,640x480*30,480x576*25,480x480*30,"	/* Smaller width modes */
"800x600,800x450,1024x768";			/* Non-Native modes */
/* Modes that are not included due to common misconfigurations:
 * 640x400*30,640x480*25
 */
enum { TV_POL_FPS_BEST = 0, TV_POL_FPS_MATCH,
       TV_POL_FPS_LIST, TV_POL_MATCH_FPS };
static char *tv_policies_list[] = {
    "FPS - Best Native Match", "FPS - Best Match", "FPS - List Order",
    "Best Match - FPS", NULL
};

static char *tv_aspect_list[] = {
    "4:3", "16:9", NULL
};
static const double tv_aspect_aspects[] = { 4.0/3.0, 16.0/9.0 };

/* Overscan sizes to be scaned for - note that we do *not* scan for 'Small' */
static char *scan_overscan[] = {
    "DVD", "Interl", "Huge", "Large", "Normal", 0
};


/* Just turn off warnings */
void xine_tvmode_exit (xine_t *this);


/* Try to connect to nvtvd server */
static void tvmode_connect(xine_t *this) {

    CardInfo *card = 0;

    if (back_card)
	back_card->closeCard ();

    if (back_client_avail ()) {
	if (! (card = back_client_init ()))
	    back_card = 0;
    } else {
	back_card = 0;
    }

    if (back_card)
    {
	printf ("tvmode: connected to nvtvd\n");
	back_card->openCard (card);
    }
    else
	printf ("tvmode: cannot connect to nvtvd - TV mode switching disabled\n");
}


/* Disconnect from server */
static void tvmode_disconnect (xine_t *this) {
    back_card->closeCard ();
    printf ("tvmode: disconnected\n");
    back_card = 0;
}


/* Save current CRT and TV register configuration */
static void tvmode_savestate (xine_t *this) {
    back_card->getMode (&tv_old_crt, &tv_old_tv);
}


/* Restore CRT and TV register configuration */
static void tvmode_restorestate (xine_t *this) {
    printf ("tvmode: switching back to regular display\n");
    back_card->setMode (0, &tv_old_crt, &tv_old_tv);
    tv_current_type = 0;
}


/* Connect to nvtvd server if possible and save current card config */
static void tvmode_startup (xine_t *this) {
    if (tv_capabilities && ! back_card)
        tvmode_connect (this);
    if (back_card)
        tvmode_savestate (this);
}


/* Disconnect and recover */
static void tvmode_closedown (xine_t *this) {
    if (back_card) {
	tvmode_restorestate (this);
	tvmode_disconnect (this);
    }
}


/* Sort callback */
static int tvmode_cmp_scanmode_cb (const void *a, const void *b) {
    return ((const scan_mode_t *) b) -> quality -
	((const scan_mode_t *) a) -> quality;
}


/* Parse mode string and set mode scan table accordingly */
/* Returns next free mode slot */
/* Table will be sorted based on prefwidth/prefheight/fps, when sortmode!=0 */
static scan_mode_t *set_modes (xine_t *this, scan_mode_t *modes, int maxnum,
			       scan_mode_t *sortmodes,
	       	               const char *string, int fps,
			       int prefwidth,  int minwidth,  int maxwidth,
			       int prefheight, int minheight, int maxheight) {
    const char *s = string;
    int  num = 0;
    int  i, w, h, f;
    char overscan[16], *os;

    /* Check whether system is available */
    if (fps == 30 && ! (tv_capabilities & (TV_CAP_PAL60 | TV_CAP_NTSC)))
        return modes;
    if (fps == 25 && ! (tv_capabilities & TV_CAP_PAL))
        return modes;

    if (tv_verbose)
	printf ("tmode: selecting modes, preferred %dx%d fps %d, min %dx%d\n",
		prefwidth, prefheight, fps, minwidth, minheight);
    /* scan modes */
    while (num < maxnum) {
	while (isspace ((int) *s) || *s == ',')	s++;
	if (! *s)
	    break;
	i = w = h = -1;
	sscanf (s, " %d x %d %n", &w, &h, &i);
	if (i < 2 || w <= 0 || h <= 0) {
	    printf ("tvmode: mode line syntax error after '%s'\n", s);
	    break;
	}
	s += i;
	overscan [0] = 0;
	if (*s == '/') {
	    s++;
	    os = overscan;
	    while (isspace ((int) *s))		s++;
	    while (isalnum ((int) *s) && os < &overscan[16-1])
   	        *os++ = *s++;
	    *os = 0;
	}
	f = fps;
	if (*s == '*') {
	    f = -1;
	    sscanf (s, "* %d %n", &f, &i);
	    if (i < 1 || f <= 0) {
		printf ("tvmode: mode line syntax error after '%s'\n", s);
		break;
	    }
	    s += i;
	}
	if (w >= minwidth && w <= maxwidth
	    && h >= minheight && h <= maxheight
	    && f == fps) {
	    
	    int diff = prefwidth*prefheight - w*h;
	    modes[num].width  = w;
	    modes[num].width  = w;
	    modes[num].height = h;
	    modes[num].fps    = f;
	    strncpy (modes[num].overscan, overscan, 16);
	    /* Set quality = Size diff in pixels + Diff in fps
	     * + penalty for too small sizes */
	    modes[num].quality = - ABS (diff) - ABS (f - fps)
		- (prefwidth  > w ? 1000 * h : 0)
		- (prefheight > h ? 2000 * w : 0);
	    if (tv_verbose)
		printf ("tvmode: entry %dx%d [%s] fps %d quality %d\n",
			w, h, overscan, f, modes[num].quality);
	    num++;
	} else {
	    if (tv_verbose)
		printf ("tvmode: entry %dx%d [%s] fps %d rejected\n",
			w, h, overscan, f);
	}
	if (*s != ',') {
	    if (*s)
		printf ("tvmode: mode line syntax error after '%s'\n", s);
	    break;
	}
    }
    if (num >= maxnum)
	printf ("tvmode: mode array overflow - some modes are not tested\n");
    if (! sortmodes)
	return modes + num;
    /* Now sort */
    qsort (sortmodes, (modes-sortmodes) + num, sizeof (scan_mode_t),
	   tvmode_cmp_scanmode_cb);
    if (tv_verbose)
	printf ("tvmode: sorted\n");
    return modes + num;
}

/* Set CRT and TV registers to given TV-Out configuration */
static void tvmode_settvstate (xine_t *this, int width, int height, double fps) {

    TVSettings settings;
    TVMode     mode;
    TVCrtRegs  crt;
    TVRegs     tv;
    int        found = 0;
    scan_mode_t modes[256];		/* FIXME: shouldn't be fix */
    scan_mode_t *m = modes;
    scan_mode_t *last_mode;
    int        best_rate;
    char     **scano;
    char      *current_overscan = "";
    TVSystem   sys;
    int        i;

    if (tv_verbose)
	printf ("tvmode: Requested mode for %dx%d, %g fps\n",
		width, height, fps);

    /* Modify the settings */
    back_card->getSettings (&settings);
    settings.connector = opt_connect;
    if (opt_flicker >= 0)
	settings.flicker = opt_flicker;

    /* Check fps for capability selection */
    best_rate = tv_prefered_fps;
    if (APPROX (fps, 60) || APPROX (fps, 30) ||
	APPROX (fps, 20) || APPROX (fps, 15))
	best_rate = 30;
    if (APPROX (fps, 50) || APPROX (fps, 25) ||
	APPROX (fps, 50.0/3) || APPROX (fps, 12.5))
	best_rate = 25;
    
    /* Scan mode strings and create scan table */
    /* TODO: do that at initialization and save possible combinations ?!? */
    switch (tv_policy) {
    case TV_POL_FPS_BEST:
        if (APPROX (fps, 24))	/* FIXME: hardcoded for this policy only */
	    best_rate = 30;
	if (best_rate == 30)
	{
	    m = set_modes (this, m, &modes[256]-m, m, tv_scan_mode, 30,
			   width, 0, INT_MAX,
			   SCAN_NATIVE_30, SCAN_NATIVE_30, SCAN_NATIVE_30);
	    m = set_modes (this, m, &modes[256]-m, m, tv_scan_mode, 30,
			   width, 0, INT_MAX, height, 0, INT_MAX);
	}
	m = set_modes (this, m, &modes[256]-m, m, tv_scan_mode, 25,
		       width, 0, INT_MAX,
		       SCAN_NATIVE_25, SCAN_NATIVE_25, SCAN_NATIVE_25);
	m = set_modes (this, m, &modes[256]-m, m, tv_scan_mode, 25,
		       width, 0, INT_MAX, height, 0, INT_MAX);
	if (best_rate != 30)
	{
	    m = set_modes (this, m, &modes[256]-m, m, tv_scan_mode, 30,
			   width, 0, INT_MAX,
			   SCAN_NATIVE_30, SCAN_NATIVE_30, SCAN_NATIVE_30);
	    m = set_modes (this, m, &modes[256]-m, m, tv_scan_mode, 30,
			   width, 0, INT_MAX, height, 0, INT_MAX);
	}
	break;
    case TV_POL_FPS_MATCH:
	if (best_rate == 30)
	    m = set_modes (this, m, &modes[256]-m, m, tv_scan_mode, 30,
			   width, 0, INT_MAX, height, 0, INT_MAX);
	m = set_modes (this, m, &modes[256]-m, m, tv_scan_mode, 25,
		       width, 0, INT_MAX, height, 0, INT_MAX);
	if (best_rate != 30)
	    m = set_modes (this, m, &modes[256]-m, m, tv_scan_mode, 30,
			   width, 0, INT_MAX, height, 0, INT_MAX);
	break;
    case TV_POL_FPS_LIST:
	if (best_rate == 30)
	    m = set_modes (this, m, &modes[256]-m, NULL, tv_scan_mode, 30,
			   width, 0, INT_MAX, height, 0, INT_MAX);
	m = set_modes (this, m, &modes[256]-m, NULL, tv_scan_mode, 25,
		       width, 0, INT_MAX, height, 0, INT_MAX);
	if (best_rate != 30)
	    m = set_modes (this, m, &modes[256]-m, NULL, tv_scan_mode, 30,
			   0, 0, INT_MAX, 0, 0, INT_MAX);
	break;
    case TV_POL_MATCH_FPS:
	if (height <= SCAN_NATIVE_30 && best_rate == 30) {
	    m = set_modes (this, m, &modes[256]-m, NULL, tv_scan_mode, 30,
			   width, 0, INT_MAX, height, 0, INT_MAX);
	    m = set_modes (this, m, &modes[256]-m, modes, tv_scan_mode, 25,
			   width, 0, INT_MAX, height, 0, INT_MAX);
	} else {
	    m = set_modes (this, m, &modes[256]-m, NULL, tv_scan_mode, 25,
			   width, 0, INT_MAX, height, 0, INT_MAX);
	    m = set_modes (this, m, &modes[256]-m, modes, tv_scan_mode, 30,
			   width, 0, INT_MAX, height, 0, INT_MAX);
	}
	break;
    default:
	abort ();
    }
    last_mode = m;
    
    if (tv_verbose) {
	printf ("tvmode: ->");
	for (i = 0; i < last_mode-modes; i++)
	    printf (" %dx%d/%s*%d", modes[i].width, modes[i].height,
		    modes[i].overscan, modes[i].fps);
	printf ("\n");
    }

    /* Select first mode that is actually known to the chip */
    for (m = modes; m < last_mode && !found; m++) {
	sys = TV_SYSTEM_PAL;
	if (m->fps == 30 && (tv_capabilities & TV_CAP_PAL60))
	    sys = TV_SYSTEM_PAL_60;
	else if (m->fps == 30 && (tv_capabilities & TV_CAP_NTSC))
	    sys = TV_SYSTEM_NTSC;
	if (! *m->overscan) {
	    for (scano = scan_overscan; *scano && !found; scano++) {
		if (tv_verbose)
		    printf ("tvmore: trying to use %dx%d [%s] fps %d\n",
			    m->width, m->height, *scano, m->fps);
		if (back_card->findBySize (sys, m->width, m->height, *scano,
				  	   &mode, &crt, &tv)) {
		    tv_current_width  = m->width;
		    tv_current_height = m->height;
		    tv_current_system = sys;
		    tv_current_fps    = m->fps;
		    current_overscan  = *scano;
		    found++;
		} else if (sys == TV_SYSTEM_PAL_60 &&
			   (tv_capabilities & TV_CAP_NTSC) &&
			   back_card->findBySize (TV_SYSTEM_NTSC,
						  m->width, m->height, *scano,
				                  &mode, &crt, &tv)) {
		    tv_current_width  = m->width;
		    tv_current_height = m->height;
		    tv_current_system = TV_SYSTEM_NTSC;
		    tv_current_fps    = m->fps;
		    current_overscan  = *scano;
		    found++;
		}
	    }
	} else {
	    if (tv_verbose)
		printf ("tvmore: trying to use %dx%d [%s] fps %d\n",
			m->width, m->height, m->overscan, m->fps);
	    if (back_card->findBySize (sys, m->width, m->height, m->overscan,
				       &mode, &crt, &tv)) {
		tv_current_width  = m->width;
		tv_current_height = m->height;
		tv_current_system = sys;
		tv_current_fps    = m->fps;
		current_overscan  = m->overscan;
		found++;
	    } else if (sys == TV_SYSTEM_PAL_60 &&
		       (tv_capabilities & TV_CAP_NTSC) &&
		       back_card->findBySize (TV_SYSTEM_NTSC,
					      m->width, m->height, m->overscan,
					      &mode, &crt, &tv)) {
		tv_current_width  = m->width;
		tv_current_height = m->height;
		tv_current_system = TV_SYSTEM_NTSC;
		tv_current_fps    = m->fps;
		current_overscan  = m->overscan;
		found++;
	    }
	}
    }

    /* Modify found Crt settings */
    crt.PrivFlags &= ~TV_MODE_MACROVISION;
  
    /* Switch to mode */
    if (found) {
        printf ("tvmode: Switching to TV %dx%d [%s] fps %g %s\n",
		tv_current_width, tv_current_height, current_overscan,
	       	tv_current_fps, tv_current_system == TV_SYSTEM_PAL ? "PAL" :
		tv_current_system == TV_SYSTEM_PAL_60 ? "PAL60" :
		tv_current_system == TV_SYSTEM_NTSC ? "NTSC" : "UNKNOWN");
	back_card->setModeSettings (TV_PRIV_TVMODE | TV_PRIV_DUALVIEW,
				    &crt, &tv, &settings);
	tv_current_type = 1;
    } else {
	printf ("tvmode: cannot find any valid TV mode - TV output disabled\n");
	tvmode_closedown (this);
    }
}


/*
 * PUBLIC
 */

/* Set to 'regular'(0) or 'tv'(1) state, that is if it is enabled */
int xine_tvmode_switch (xine_t *this, int type, int width, int height, double fps) {

    if (back_card) {
	switch (type) {
	case 0:
	  tvmode_restorestate (this);
	  break;
	case 1:
	    tv_stream_width  = width;
	    tv_stream_height = height;
	    tv_stream_fps    = fps;
	    tvmode_settvstate (this, width, height, fps);
	    break;
	default:
	    printf ("tvmode: illegal type for switching\n");
	    tvmode_restorestate (this);
	}
    }
    return tv_current_type;
}


/* Addapt (maximum) output size to visible area and set pixel aspect and fps */
void xine_tvmode_size (xine_t *this, int *width, int *height,
		       double *pixelratio, double *fps) {

    switch (tv_current_type) {
    case 1:
	if (width  && *width > tv_current_width)
	    *width  = tv_current_width;
	if (height && *height > tv_current_height)
	    *height = tv_current_height;
	if (pixelratio)
	    *pixelratio = ((double) tv_current_width / tv_current_height)
		/ tv_aspect;
	if (fps)
	    *fps = tv_current_fps;
	break;
    }
}


/* Configuration callbacks */
static void tvmode_system_cb (void *data, cfg_entry_t *entry) {
    xine_t *this = (xine_t *) data;
    tv_capabilities = tv_system_caps [entry->num_value];
    if (tv_capabilities && !back_card)
	tvmode_startup (this);
    else if (!tv_capabilities && back_card)
	tvmode_closedown (this);
}
static void tvmode_policy_cb (void *data, cfg_entry_t *entry) {
    xine_t *this = (xine_t *) data;
    tv_policy = entry->num_value;
    xine_tvmode_switch (this, tv_current_type, tv_stream_width,
			tv_stream_height, tv_stream_fps);
}
static void tvmode_mode_cb (void *data, cfg_entry_t *entry) {
    xine_t *this = (xine_t *) data;
    tv_scan_mode = entry->str_value;
    if (*tv_scan_mode == '-')
        this->config->update_string (this->config, "tv.modes", tv_scan_mode_default);
    xine_tvmode_switch (this, tv_current_type, tv_stream_width,
			tv_stream_height, tv_stream_fps);
}
static void tvmode_aspect_cb (void *data, cfg_entry_t *entry) {
    xine_t *this = (xine_t *) data;
    tv_aspect = tv_aspect_aspects[entry->num_value];
    xine_tvmode_switch (this, tv_current_type, tv_stream_width,
			tv_stream_height, tv_stream_fps);
}
static void tvmode_preferred_fps_cb (void *data, cfg_entry_t *entry) {
    xine_t *this = (xine_t *) data;
    tv_prefered_fps = entry->num_value ? 25 : 30;
    xine_tvmode_switch (this, tv_current_type, tv_stream_width,
			tv_stream_height, tv_stream_fps);
}
static void tvmode_verbose_cb (void *data, cfg_entry_t *entry) {
    tv_verbose = entry->num_value;
}


/* Connect to nvtvd server if possible and register settings */
void xine_tvmode_init (xine_t *this) {
    /* TODO:
     * more config options that can be imagined:
     * - disable deinterlacing for tv mode only
     * - flickerfilter
     * - input filters
     * - connectors + dual view
     * - color systems (PAL / PAL_M / etc.)
     * - FILM (24 fps) -> 50 / 60Hz / 50Hz+Speedup ?
     */
    tv_capabilities = tv_system_caps [this->config->register_enum (
	this->config, "tv.capabilities",
	0, tv_systems_list, _("TV System"),
	"Capabilities of the connected TV system",
	tvmode_system_cb, this)];
    tv_policy = this->config->register_enum (
	this->config, "tv.policy",
	0, tv_policies_list, _("Mode Selection Policy"),
	"FPS - Best Native Match:\n"
	"Select system (50/60Hz) according to frame rate and TV system,\n"
	"select 60Hz for FILM (24 fps),\n"
	"select native resolutions only if available,\n"
	"select best matching mode for given video size.\n"
	"This policy prefers correct frame rates to better resolutions."
	"Recommended.\n\n"
	"FPS - Best Match:\n"
	"Select system (50/60Hz) according to frame rate,\n"
	"select 50Hz for FILM (24 fps),\n"
	"select best matching mode for given video size.\n\n"
	"FPS - List Order:\n"
	"Select system (50/60Hz) according to frame rate,\n"
	"select 50Hz for FILM (24 fps),\n"
	"select first available mode in the modes list.\n\n"
	"Best Match - FPS:\n"
	"Select best matching mode for the given video size,\n"
	"select system (50/60Hz) according to frame rate and TV system.\n"
	"This policy prefers better resolutions to correct frame rates.",
	tvmode_policy_cb, this);
    tv_scan_mode = this->config->register_string (
	this->config, "tv.modes",
	tv_scan_mode_default, _("Modes"),
	"Specify valid resolutions.\n"
	"<width>x<height>[/<overscan>][*<fps>][, ...]\n"
	"Enter '-' to use default list.\n"
	"When no overscan mode is given, the following list is tried:\n"
	"'Interl', 'Huge', 'Large', 'DVD', 'Normal'\n"
	"When no FPS value is given, mode is valid for both, "
	"25 (PAL) and 30 (PAL60/NTSC).",
	tvmode_mode_cb, this);
    tv_prefered_fps = this->config->register_bool (
	this->config, "tv.preferPAL", 1, _("Prefer PAL"),
	"Prefer 50Hz modes to 60Hz modes for videos "
	"with nonstandard frame rates",
	tvmode_preferred_fps_cb, this)
	? 25 : 30;
    tv_verbose = this->config->register_bool (
	this->config, "tv.verbose", 0, _("Verbose resolution selection"),
	NULL, tvmode_verbose_cb, this);
    if (*tv_scan_mode == '-')
        this->config->update_string (this->config, "tv.modes", tv_scan_mode_default);
    tv_aspect = tv_aspect_aspects [this->config->register_enum (
	this->config, "tv.aspect",
	0, tv_aspect_list, _("Screen Aspect Ratio"), NULL,
	tvmode_aspect_cb, this)];
    tvmode_startup (this);
}

/* Restore old CRT and TV registers and close nvtvd connection */
void xine_tvmode_exit (xine_t *this) {

    tvmode_closedown (this);
}

