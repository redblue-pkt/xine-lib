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
 * $Id: load_plugins.c,v 1.5 2001/04/24 15:47:32 guenter Exp $
 *
 *
 * Load input/demux/audio_out/video_out/codec plugins
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
#include <string.h>

#include "xine_internal.h"
#include "demuxers/demux.h"
#include "input/input_plugin.h"
#include "metronom.h"
#include "configfile.h"
#include "monitor.h"

/*
 *
 */
void load_demux_plugins (xine_t *this, 
			 config_values_t *config, int iface_version) {
  DIR *dir;

  this->num_demuxer_plugins = 0;

  dir = opendir (XINE_PLUGINDIR) ;
  
  if (dir) {
    struct dirent *pEntry;
    
    while ((pEntry = readdir (dir)) != NULL) {
      char str[1024];
      void *plugin;
      
      int nLen = strlen (pEntry->d_name);
      
      if ((strncasecmp(pEntry->d_name, 
		       XINE_DEMUXER_PLUGIN_PREFIXNAME, 
		       XINE_DEMUXER_PLUGIN_PREFIXNAME_LENGTH) == 0) && 
	  ((pEntry->d_name[nLen-3]=='.') 
	   && (pEntry->d_name[nLen-2]=='s')
	   && (pEntry->d_name[nLen-1]=='o'))) {
	
	/*
	 * demux plugin found => load it
	 */
	
	sprintf (str, "%s/%s", XINE_PLUGINDIR, pEntry->d_name);
	
	if(!(plugin = dlopen (str, RTLD_LAZY))) {
	  fprintf(stderr, "%s(%d): %s doesn't seem to be installed (%s)\n", 
		  __FILE__, __LINE__, str, dlerror());
	  exit(1);
	}
	else {
	  void *(*initplug) (int, config_values_t *);
	  
	  if((initplug = dlsym(plugin, "init_demuxer_plugin")) != NULL) {
	    demux_plugin_t *dxp;
	      
	    dxp = (demux_plugin_t *) initplug(iface_version, config);
	    this->demuxer_plugins[this->num_demuxer_plugins] = dxp; 
	    
	    printf("demux plugin found : %s(ID: %s, iface: %d)\n", 
		   str,   
		   this->demuxer_plugins[this->num_demuxer_plugins]->get_identifier(),
		   this->demuxer_plugins[this->num_demuxer_plugins]->interface_version);

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
  
  /*
   * init demuxer
   */
  
  this->cur_demuxer_plugin = NULL;
}

/*
 *
 */
void load_input_plugins (xine_t *this, 
			 config_values_t *config, int iface_version) {
  DIR *dir;
  
  this->num_input_plugins = 0;
  
  dir = opendir (XINE_PLUGINDIR) ;
  
  if (dir) {
    struct dirent *pEntry;
    
    while ((pEntry = readdir (dir)) != NULL) {
      
      char str[1024];
      void *plugin;
      
      int nLen = strlen (pEntry->d_name);
      
      if ((strncasecmp(pEntry->d_name,
 		       XINE_INPUT_PLUGIN_PREFIXNAME, 
		       XINE_INPUT_PLUGIN_PREFIXNAME_LENGTH) == 0) &&
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
	  void *(*initplug) (int, config_values_t *);
	  
	  if((initplug = dlsym(plugin, "init_demuxer_plugin")) != NULL) {
	    input_plugin_t *ip;
	      
	    ip = (input_plugin_t *) initplug(iface_version, config);
	    this->input_plugins[this->num_input_plugins] = ip; 
	    
	    printf("input plugin found : %s(ID: %s, iface: %d)\n", 
		   str,   
		   this->input_plugins[this->num_input_plugins]->get_identifier(),
		   this->input_plugins[this->num_input_plugins]->interface_version);

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

/*
 * load audio and video decoder plugins 
 */
void load_decoder_plugins (xine_t *this, 
			   config_values_t *config, int iface_version) {
  DIR *dir;
  int  i;

  /*
   * clean up first
   */

  this->num_video_decoder_plugins = 0;
  this->cur_video_decoder_plugin = NULL;
  for (i=0; i<DECODER_PLUGIN_MAX; i++)
    this->video_decoder_plugins[i] = NULL;

  this->num_audio_decoder_plugins = 0;
  this->cur_audio_decoder_plugin = NULL;
  for (i=0; i<AUDIO_OUT_PLUGIN_MAX; i++)
    this->audio_decoder_plugins[i] = NULL;

  /*
   * now scan for decoder plugins
   */

  dir = opendir (XINE_PLUGINDIR) ;
  
  if (dir) {
    struct dirent *pEntry;
    
    while ((pEntry = readdir (dir)) != NULL) {
      char str[1024];
      void *plugin;
      
      int nLen = strlen (pEntry->d_name);
      
      if ((strncasecmp(pEntry->d_name, 
		       XINE_DECODER_PLUGIN_PREFIXNAME, 
		       XINE_DECODER_PLUGIN_PREFIXNAME_LENGTH) == 0) && 
	  ((pEntry->d_name[nLen-3]=='.') 
	   && (pEntry->d_name[nLen-2]=='s')
	   && (pEntry->d_name[nLen-1]=='o'))) {
	
	/*
	 * decoder plugin found => load it
	 */
	
	sprintf (str, "%s/%s", XINE_PLUGINDIR, pEntry->d_name);
	
	if(!(plugin = dlopen (str, RTLD_LAZY))) {
	  fprintf(stderr, "%s(%d): %s doesn't seem to be installed (%s)\n", 
		  __FILE__, __LINE__, str, dlerror());
	  exit(1);
	}
	else {
	  void *(*initplug) (int, config_values_t *);
	  
	  /*
	   * does this plugin provide an video decoder plugin?
	   */

	  if((initplug = dlsym(plugin, "init_video_decoder_plugin")) != NULL) {
	    video_decoder_t *vdp;
	      
	    vdp = (video_decoder_t *) initplug(iface_version, config);
	    this->video_decoder_plugins[this->num_video_decoder_plugins] = vdp; 
	    
	    printf("video decoder plugin found : %s(ID: %s, iface: %d)\n", 
		   str,   
		   this->video_decoder_plugins[this->num_video_decoder_plugins]->get_identifier(),
		   this->video_decoder_plugins[this->num_video_decoder_plugins]->interface_version);

	    this->num_video_decoder_plugins++;
	  }
	  
	  if(this->num_video_decoder_plugins > VIDEO_DECODER_PLUGIN_MAX) {
	    fprintf(stderr, "%s(%d): too many video decoder plugins installed,"
		    " exiting.\n", __FILE__, __LINE__);
	    exit(1);
	  }

	  /*
	   * does this plugin provide an audio decoder plugin?
	   */

	  if((initplug = dlsym(plugin, "init_audio_decoder_plugin")) != NULL) {
	    audio_decoder_t *adp;
	      
	    adp = (audio_decoder_t *) initplug(iface_version, config);
	    this->audio_decoder_plugins[this->num_audio_decoder_plugins] = adp; 
	    
	    printf("audio decoder plugin found : %s(ID: %s, iface: %d)\n", 
		   str,   
		   this->audio_decoder_plugins[this->num_audio_decoder_plugins]->get_identifier(),
		   this->audio_decoder_plugins[this->num_audio_decoder_plugins]->interface_version);

	    this->num_audio_decoder_plugins++;
	  }
	  
	  if(this->num_audio_decoder_plugins > AUDIO_DECODER_PLUGIN_MAX) {
	    fprintf(stderr, "%s(%d): too many audio decoder plugins installed,"
		    " exiting.\n", __FILE__, __LINE__);
	    exit(1);
	  }


	}
      }
    }
  }
  
  this->cur_video_decoder_plugin = NULL;
  this->cur_audio_decoder_plugin = NULL;
}


void load_video_out_plugins (xine_t *this, 
			     config_values_t *config, int iface_version) {

}

void load_audio_out_plugins (xine_t *this, 
			     config_values_t *config, int iface_version) {

}

/*
vo_instance_t *load_video_output_plugin(char *filename, char *id) {
  DIR *dir;
  vo_instance_t *voi;
  
  if(filename == NULL && id == NULL)
    return NULL;

  dir = opendir (XINE_PLUGINDIR);

  if (dir) {
    struct dirent *pEntry;
    
    while ((pEntry = readdir (dir)) != NULL) {
      
      char str[1024];
      void *plugin;
      
      int nLen = strlen (pEntry->d_name);
      
      if ((strncasecmp(pEntry->d_name,
 		       XINE_VIDEO_OUT_PLUGIN_PREFIXNAME, 
		       XINE_VIDEO_OUT_PLUGIN_PREFIXNAME_LENGTH) == 0) &&
	  ((pEntry->d_name[nLen-3]=='.') 
	  && (pEntry->d_name[nLen-2]=='s')
	  && (pEntry->d_name[nLen-1]=='o'))) {
	
	sprintf (str, "%s/%s", XINE_PLUGINDIR, pEntry->d_name);
	
	if(filename) {
	  if(!strncasecmp(filename, pEntry->d_name, strlen(filename))) {
	    
	    if(!(plugin = dlopen (str, RTLD_LAZY))) {
	      fprintf(stderr, "%s(%d): %s doesn't seem to be installed (%s)\n",
		      __FILE__, __LINE__, str, dlerror());
	      exit(1);
	    }
	    else {
	      void *(*initplug) (int, config_values_t *);
	      
	      if((initplug = dlsym(plugin, "init_video_out_plugin")) != NULL) {
		video_out_plugin_t *vop;
		
		vop = (video_out_plugin_t *) initplug(iface_version, config);
		this->video_out_plugins[this->num_input_plugins] = vop; 
		
		  printf("video output plugin found : %s(ID: %s, iface: %d)\n",
			 str,   
			 this->input_plugins[this->num_input_plugins].get_identifier(),
			 this->input_plugins[this->num_input_plugins].interface_version);
		  

	    if((voi = xmalloc(sizeof(vo_instance_t))) != NULL) {
	      voi = 
	    }
	}

*/	  /*
	if(!(plugin = dlopen (str, RTLD_LAZY))) {
	  fprintf(stderr, "%s(%d): %s doesn't seem to be installed (%s)\n", 
		  __FILE__, __LINE__, str, dlerror());
	  exit(1);
	}
	else {
	  void *(*initplug) (int, config_values_t *);
	  
	  if((initplug = dlsym(plugin, "init_demuxer_plugin")) != NULL) {
	    input_plugin_t *ip;
	      
	    ip = (input_plugin_t *) initplug(iface_version, config);
	    this->input_plugins[this->num_input_plugins] = *ip; 
	    
	    printf("input plugin found : %s(ID: %s, iface: %d)\n", 
		   str,   
		   this->input_plugins[this->num_input_plugins].get_identifier(),
		   this->input_plugins[this->num_input_plugins].interface_version);

	    this->num_input_plugins++;
	  }

	  */


	/*
	 * input plugin found => load it
	 */
/*	
	sprintf (str, "%s/%s", XINE_PLUGINDIR, pEntry->d_name);
	
	if(!(plugin = dlopen (str, RTLD_LAZY))) {
	  fprintf(stderr, "%s(%d): %s doesn't seem to be installed (%s)\n", 
		  __FILE__, __LINE__, str, dlerror());
	  exit(1);
	}
	else {
	  void *(*initplug) (int, config_values_t *);
	  
	  if((initplug = dlsym(plugin, "init_demuxer_plugin")) != NULL) {
	    input_plugin_t *ip;
	      
	    ip = (input_plugin_t *) initplug(iface_version, config);
	    this->input_plugins[this->num_input_plugins] = *ip; 
	    
	    printf("input plugin found : %s(ID: %s, iface: %d)\n", 
		   str,   
		   this->input_plugins[this->num_input_plugins].get_identifier(),
		   this->input_plugins[this->num_input_plugins].interface_version);

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
*/
 
char **enum_video_output_plugins(int output_type) {
  /*
    Add into xine.h and xine_internal.h
    VIDEO_OUTPUT_TYPE_ALL
    VIDEO_OUTPUT_TYPE_X11
    VIDEO_OUTPUT_TYPE_FB
    ...
  */
  return NULL;
}

ao_functions_t *load_audio_output_plugin(char *filename, char *id) {

  if(filename == NULL && id == NULL)
    return NULL;

  // Not implemented

  return NULL;
}
char **enum_audio_output_plugins(int output_type) {
  /*
    Add into xine.h and xine_internal.h
    not sure about names !!
    AUDIO_OUTPUT_TYPE_ALL
    AUDIO_OUTPUT_TYPE_OSS
    AUDIO_OUTPUT_TYPE_ALSA
    AUDIO_OUTPUT_TYPE_ESD
    ...
  */
  return NULL;
}


