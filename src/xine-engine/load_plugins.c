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
 * $Id: load_plugins.c,v 1.1 2001/04/18 22:36:09 f1rmb Exp $
 *
 *
 * Load input/demux/audio_out/video_out plugins
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <dlfcn.h>

#include "xine.h"
#include "xine_internal.h"
#include "demuxers/demux.h"
#include "input/input_plugin.h"
#include "metronom.h"
#include "configfile.h"
#include "monitor.h"

/* debugging purposes only */
extern  uint32_t xine_debug;

/*
 *
 */
void xine_load_demux_plugins (xine_t *this) {
  DIR *dir;

  this->demuxer_plugins[0]    = *(init_demux_mpeg (xine_debug));
  this->demuxer_plugins[1]    = *(init_demux_mpeg_block (xine_debug));
  this->demuxer_plugins[2]    = *(init_demux_avi (xine_debug));
  this->demuxer_plugins[3]    = *(init_demux_mpeg_audio (xine_debug));
  this->demuxer_plugins[4]    = *(init_demux_mpeg_elem (xine_debug));
  this->num_demuxer_plugins   = 5;

  dir = opendir (XINE_DEMUXDIR) ;
  
  if (dir) {
    struct dirent *pEntry;
    
    while ((pEntry = readdir (dir)) != NULL) {
      char str[1024];
      void *plugin;
      
      int nLen = strlen (pEntry->d_name);
      
      if ((strncasecmp(pEntry->d_name, "demux_", 6) == 0) && 
	  ((pEntry->d_name[nLen-3]=='.') 
	   && (pEntry->d_name[nLen-2]=='s')
	   && (pEntry->d_name[nLen-1]=='o'))) {
	
	/*
	 * demux plugin found => load it
	 */
	
	sprintf (str, "%s/%s", XINE_DEMUXDIR, pEntry->d_name);
	
	if(!(plugin = dlopen (str, RTLD_LAZY))) {
	  fprintf(stderr, "%s(%d): %s doesn't seem to be installed (%s)\n", 
		  __FILE__, __LINE__, str, dlerror());
	  exit(1);
	}
	else {
	  void *(*getinfo) (fifobuf_functions_t *, uint32_t);
	  
	  if((getinfo = dlsym(plugin, "demux_plugin_getinfo")) != NULL) {
	    demux_functions_t *dxp;
	      
	    dxp = (demux_functions_t *) getinfo(this->fifo_funcs, xine_debug);
	    dxp->handle = plugin;
	    dxp->filename = str;
	    this->demuxer_plugins[this->num_demuxer_plugins] = *dxp; 
	    
	    
	    printf("demux plugin found : %s(%s)\n", 
		   this->demuxer_plugins[this->num_demuxer_plugins].filename,
		   pEntry->d_name);
	    
	    this->num_demuxer_plugins++;
	  }
	  
	  if(this->num_demuxer_plugins > DEMUXER_PLUGIN_MAX) {
	    fprintf(stderr, "%s(%d): too many demux plugins installed, "
		    "exiting.\n", __FILE__, __LINE__);
	    exit(1);
	  }
	}
      }
    }
  }
  
  if (this->num_demuxer_plugins == 5)
    printf ("No extra demux plugins found in %s\n", XINE_DEMUXDIR);
  
  /*
   * init demuxer
   */
  
  this->cur_demuxer_plugin = NULL;
}

/*
 *
 */
void xine_load_input_plugins (xine_t *this) {
  DIR *dir;
  
  this->num_input_plugins = 0;
  
  dir = opendir (XINE_PLUGINDIR) ;
  
  if (dir) {
    struct dirent *pEntry;
    
    while ((pEntry = readdir (dir)) != NULL) {
      
      char str[1024];
      void *plugin;
      
      int nLen = strlen (pEntry->d_name);
      
      if ((strncasecmp(pEntry->d_name, "input_", 6) == 0) &&
	  ((pEntry->d_name[nLen-3]=='.') 
	  && (pEntry->d_name[nLen-2]=='s')
	  && (pEntry->d_name[nLen-1]=='o'))) {

	/*
	 * input plugin found => load it
	 */
	
	sprintf (str, "%s/%s", XINE_PLUGINDIR, pEntry->d_name);
	
	if(!(plugin = dlopen (str, RTLD_LAZY))) {
	  fprintf(stderr, "%s(%d): %s doesn't seem to be installed (%s)\n", 
		  __FILE__, __LINE__, str, dlerror());
	  exit(1);
	}
	else {
	  void *(*getinfo) (uint32_t);
	  
	  if((getinfo = dlsym(plugin, "input_plugin_getinfo")) != NULL) {
	    input_plugin_t *ipp;
	    
	    ipp = (input_plugin_t *) getinfo(xine_debug);
	    ipp->handle = plugin;
	    ipp->filename = str;
	    this->input_plugins[this->num_input_plugins] = *ipp; 
	    
	    this->input_plugins[this->num_input_plugins].init();
	    
	    printf("input plugin found : %s(%s)\n", 
		   this->input_plugins[this->num_input_plugins].filename,
		   pEntry->d_name);
	    
	    this->num_input_plugins++;
	    
	  }
	  
	  if(this->num_input_plugins > INPUT_PLUGIN_MAX) {
	    fprintf(stderr, "%s(%d): too many input plugins installed, "
		    "exiting.\n", __FILE__, __LINE__);
	    exit(1);
	  }
	}
      }
    }
  }
  
  if (this->num_input_plugins == 0) {
    printf ("No input plugins found in %s! - "
	    "Did you install xine correctly??\n", XINE_PLUGINDIR);
    exit (1);
  }
  
}
