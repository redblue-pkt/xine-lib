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
 * $Id: load_plugins.c,v 1.9 2001/04/27 10:42:38 f1rmb Exp $
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
#include "video_out.h"
#include "metronom.h"
#include "configfile.h"
#include "monitor.h"

/*
 *
 */
void load_demux_plugins (xine_t *this, 
			 config_values_t *config, int iface_version) {
  DIR *dir;

  if(this == NULL || config == NULL) {
    printf("%s(%s@%d): parameter should be non null, exiting\n",
	   __FILE__, __FUNCTION__, __LINE__);
    exit(1);
  }

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
  
  if(this == NULL || config == NULL) {
    printf("%s(%s@%d): parameter should be non null, exiting\n",
	   __FILE__, __FUNCTION__, __LINE__);
    exit(1);
  }

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
	  
	  if((initplug = dlsym(plugin, "init_input_plugin")) != NULL) {
	    input_plugin_t *ip;
	      
	    ip = (input_plugin_t *) initplug(iface_version, config);
	    this->input_plugins[this->num_input_plugins] = ip; 
	    
	    printf("input plugin found : %s(ID: %s, iface: %d)\n", 
		   str,   
		   this->input_plugins[this->num_input_plugins]->get_identifier(this->input_plugins[this->num_input_plugins]),
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

  if(this == NULL || config == NULL) {
    printf("%s(%s@%d): parameter should be non null, exiting\n",
	   __FILE__, __FUNCTION__, __LINE__);
    exit(1);
  }

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

  if(this == NULL || config == NULL) {
    printf("%s(%s@%d): parameter should be non null, exiting\n",
	   __FILE__, __FUNCTION__, __LINE__);
    exit(1);
  }

}

void load_audio_out_plugins (xine_t *this, 
			     config_values_t *config, int iface_version) {

  if(this == NULL || config == NULL) {
    printf("%s(%s@%d): parameter should be non null, exiting\n",
	   __FILE__, __FUNCTION__, __LINE__);
    exit(1);
  }

}

vo_driver_t *xine_load_video_output_plugin(config_values_t *config,
				      char *filename, char *id, 
				      int visual_type, void *visual) {
  DIR *dir;
  vo_driver_t *vod;
  
  if((filename == NULL && id == NULL) || visual == NULL || config == NULL) {
    printf("%s(%s@%d): parameter(s) should be non null.\n",
	   __FILE__, __FUNCTION__, __LINE__);
    return NULL;
  }
  
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
	
	if(filename) { /* load by name */
	  if(!strncasecmp(filename, pEntry->d_name, strlen(pEntry->d_name))) {
	    
	    if(!(plugin = dlopen (str, RTLD_LAZY))) {
	      fprintf(stderr, "%s(%d): %s doesn't seem to be installed (%s)\n",
		      __FILE__, __LINE__, str, dlerror());
	      exit(1);
	    }
	    else {
	      void *(*initplug) (int, config_values_t *, void *, int);
	      
	      if((initplug = dlsym(plugin, "init_video_out_plugin")) != NULL) {
		
		vod = (vo_driver_t *) initplug(VIDEO_OUT_PLUGIN_IFACE_VERSION,
					       config, visual, visual_type);
		
		printf("video output plugin found : %s(ID: %s, iface: %d)\n",
		       str, vod->get_identifier(), vod->interface_version);
		
		return vod;
	      } 
	    }   
	  }
	}
	else { /* load by ID */
	  if(!(plugin = dlopen (str, RTLD_LAZY))) {
	    fprintf(stderr, "%s(%d): %s doesn't seem to be installed (%s)\n",
		    __FILE__, __LINE__, str, dlerror());
	    exit(1);
	  }
	  else {
	    void *(*initplug) (int, config_values_t *, void *, int);
	    
	    if((initplug = dlsym(plugin, "init_video_out_plugin")) != NULL) {
	      
	      vod = (vo_driver_t *) initplug(VIDEO_OUT_PLUGIN_IFACE_VERSION,
					     config, visual, visual_type);
	      
	      printf("video output plugin found : %s(ID: %s, iface: %d)\n",
		     str, vod->get_identifier(), vod->interface_version);
	      
	      if(!strcasecmp(id, vod->get_identifier())) {
		return vod;
	      }
	    }
	  }
	}
      }
    }
  }
  return NULL;
}
 
char **enum_video_output_plugins(int visual_type) {
  // Not implemented
  return NULL;
}

ao_functions_t *xine_load_audio_output_plugin(config_values_t *config,
					      char *filename, char *id) {
  DIR *dir;
  ao_functions_t *aod;
  
  if(filename == NULL && id == NULL) {
    printf("%s(%s@%d): parameter(s) should be non null.\n",
	   __FILE__, __FUNCTION__, __LINE__);
    return NULL;
  }
  
  dir = opendir (XINE_PLUGINDIR);
  
  if (dir) {
    struct dirent *pEntry;
    
    while ((pEntry = readdir (dir)) != NULL) {
      char str[1024];
      void *plugin;
      
      int nLen = strlen (pEntry->d_name);
      
      if ((strncasecmp(pEntry->d_name,
 		       XINE_AUDIO_OUT_PLUGIN_PREFIXNAME, 
		       XINE_AUDIO_OUT_PLUGIN_PREFIXNAME_LENGTH) == 0) &&
	  ((pEntry->d_name[nLen-3]=='.') 
	   && (pEntry->d_name[nLen-2]=='s')
	   && (pEntry->d_name[nLen-1]=='o'))) {
	
	sprintf (str, "%s/%s", XINE_PLUGINDIR, pEntry->d_name);
	
	if(filename) { /* load by name */
	  if(!strncasecmp(filename, pEntry->d_name, strlen(pEntry->d_name))) {
	    
	    if(!(plugin = dlopen (str, RTLD_LAZY))) {
	      fprintf(stderr, "%s(%d): %s doesn't seem to be installed (%s)\n",
		      __FILE__, __LINE__, str, dlerror());
	      exit(1);
	    }
	    else {
	      void *(*initplug) (int, config_values_t *);
	      
	      if((initplug = dlsym(plugin, "init_audio_out_plugin")) != NULL) {
		
		aod = (ao_functions_t *) initplug(AUDIO_OUT_PLUGIN_IFACE_VERSION, config);
		
		printf("audio output plugin found : %s(ID: %s, iface: %d)\n",
		       str, aod->get_identifier(), aod->interface_version);
		
		return aod;
	      } 
	    }   
	  }
	}
	else { /* load by ID */
	  if(!(plugin = dlopen (str, RTLD_LAZY))) {
	    fprintf(stderr, "%s(%d): %s doesn't seem to be installed (%s)\n",
		    __FILE__, __LINE__, str, dlerror());
	    exit(1);
	  }
	  else {
	    void *(*initplug) (int, config_values_t *);
	    
	    if((initplug = dlsym(plugin, "init_audio_out_plugin")) != NULL) {
	      
	      aod = (ao_functions_t *) initplug(AUDIO_OUT_PLUGIN_IFACE_VERSION,
						config);
	      
	      printf("audio output plugin found : %s(ID: %s, iface: %d)\n",
		     str, aod->get_identifier(), aod->interface_version);
	      
	      if(!strcasecmp(id, aod->get_identifier())) {
		return aod;
	      }
	    }
	  }
	}
      }
    }
  }
  return NULL;
}

char **enum_audio_output_plugins(int output_type) {

  // Not implemented

  return NULL;
}
