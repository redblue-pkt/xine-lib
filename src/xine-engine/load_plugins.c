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
 * $Id: load_plugins.c,v 1.41 2001/09/17 11:43:43 jkeil Exp $
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
#include <errno.h>
#include <stdlib.h>

#include "xine_internal.h"
#include "demuxers/demux.h"
#include "input/input_plugin.h"
#include "video_out.h"
#include "metronom.h"
#include "configfile.h"
#include "utils.h"
#include "monitor.h"

#ifndef	__GNUC__
#define	__FUNCTION__	__func__
#endif

extern int errno;

/** ***************************************************************
 *  Demuxers plugins section
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
	  fprintf(stderr, "load_plugins: cannot open demux plugin %s:\n%s\n",
		  str, dlerror());
	  exit(1);
	}
	else {
	  void *(*initplug) (int, config_values_t *);
	  
	  if((initplug = dlsym(plugin, "init_demuxer_plugin")) != NULL) {
	    demux_plugin_t *dxp;
	      
	    dxp = (demux_plugin_t *) initplug(iface_version, config);
	    if (dxp) {
	      this->demuxer_plugins[this->num_demuxer_plugins] = dxp; 
	    
	      printf("load_plugins: demux plugin found : %s\n", 
		     this->demuxer_plugins[this->num_demuxer_plugins]->get_identifier());

	      this->num_demuxer_plugins++;
	    }
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

/** ***************************************************************
 *  Input plugins section
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
	  printf("load_plugins: cannot open input plugin %s:\n%s\n", 
		 str, dlerror());
	} else {
	  void *(*initplug) (int, config_values_t *);
	  
	  if((initplug = dlsym(plugin, "init_input_plugin")) != NULL) {
	    input_plugin_t *ip;
	      
	    ip = (input_plugin_t *) initplug(iface_version, config);
	    if (ip) {
	      this->input_plugins[this->num_input_plugins] = ip; 
	    
	      printf("load_plugins: input plugin found : %s\n", 
		     this->input_plugins[this->num_input_plugins]->get_identifier(this->input_plugins[this->num_input_plugins]));

	      this->num_input_plugins++;
	    }
	  } else {
	    printf ("load_plugins: %s is no valid input plugin (lacks init_input_plugin() function)\n", str);
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
    printf ("load_plugins: no input plugins found in %s! - "
	    "Did you install xine correctly??\n", XINE_PLUGINDIR);
    exit (1);
  }
  
}

static char **_xine_get_featured_input_plugin_ids(xine_t *this, int feature) {
  input_plugin_t  *ip;
  char           **plugin_ids;
  int              i;
  int              n = 0;

  if(!this)
    return NULL;
  
  if(!this->num_input_plugins)
    return NULL;

  plugin_ids = (char **) xmalloc (this->num_input_plugins * sizeof (char *));

  for(i = 0; i < this->num_input_plugins; i++) {

    ip = this->input_plugins[i];

    if(ip->get_capabilities(ip) & feature) {
      plugin_ids[n] = (char *) malloc (strlen(ip->get_identifier(ip)) + 1 );
      strcpy (plugin_ids[n], ip->get_identifier(ip));
      /* printf("%s(%d): %s is featured\n",  */
      /*        __FUNCTION__, feature, ip->get_identifier(ip)); */
      n++;
    }

  }

  plugin_ids[n] = NULL;

  return plugin_ids;
}

char **xine_get_autoplay_input_plugin_ids(xine_t *this) {

  return (_xine_get_featured_input_plugin_ids(this, INPUT_CAP_AUTOPLAY));
}

char **xine_get_browsable_input_plugin_ids(xine_t *this) {

  return (_xine_get_featured_input_plugin_ids(this, INPUT_CAP_GET_DIR));
}

/** ***************************************************************
 *  Decoder plugins section
 */
static int decide_spu_insert(spu_decoder_t *p, int streamtype, int prio) {

  if (!p->can_handle (p, (streamtype<<16) | BUF_SPU_BASE))
    return 0;
  
  if (p->priority < prio)
    return 0;

  /* All conditions successfully passed */
  return p->priority;
}

static int decide_video_insert(video_decoder_t *p, int streamtype, int prio) {

  if (!p->can_handle (p, (streamtype<<16) | BUF_VIDEO_BASE))
    return 0;

  if (p->priority < prio)
    return 0;

  /* All conditions successfully passed */
  return p->priority;
}

static int decide_audio_insert(audio_decoder_t *p, int streamtype, int prio) {

  if (!p->can_handle (p, (streamtype<<16) | BUF_AUDIO_BASE))
    return 0;

  if (p->priority < prio)
    return 0;

  /* All conditions successfully passed */
  return p->priority;
}

/*
 * load audio and video decoder plugins 
 */
void load_decoder_plugins (xine_t *this, 
			   config_values_t *config, int iface_version) {
  DIR *dir;
  int  i;
  int spu_prio[DECODER_PLUGIN_MAX], video_prio[DECODER_PLUGIN_MAX],
   audio_prio[DECODER_PLUGIN_MAX];
   

  if(this == NULL || config == NULL) {
    printf("%s(%s@%d): parameter should be non null, exiting\n",
	   __FILE__, __FUNCTION__, __LINE__);
    exit(1);
  }

  /*
   * clean up first
   */
  this->cur_spu_decoder_plugin = NULL;
  for (i=0; i<DECODER_PLUGIN_MAX; i++) {
    this->spu_decoder_plugins[i] = NULL;
    spu_prio[i] = 0;
  }

  this->cur_video_decoder_plugin = NULL;
  for (i=0; i<DECODER_PLUGIN_MAX; i++) {
    this->video_decoder_plugins[i] = NULL;
    video_prio[i] = 0;
  }

  this->cur_audio_decoder_plugin = NULL;
  for (i=0; i<DECODER_PLUGIN_MAX; i++) {
    this->audio_decoder_plugins[i] = NULL;
    audio_prio[i] = 0;
  }

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

	  printf ("load_plugins: failed to load plugin %s:\n%s\n",
		  str, dlerror());

	} else {
	  void *(*initplug) (int, config_values_t *);
	  int plugin_prio;

	  /*
	   * does this plugin provide an spu decoder plugin?
	   */

	  if((initplug = dlsym(plugin, "init_spu_decoder_plugin")) != NULL) {

            spu_decoder_t *sdp;
            int            streamtype;

            sdp = (spu_decoder_t *) initplug(3, config);
	    if (sdp) {
	      sdp->metronom = this->metronom;

	      for (streamtype = 0; streamtype<DECODER_PLUGIN_MAX; streamtype++)
		if ((plugin_prio =
		     decide_spu_insert(sdp, streamtype, spu_prio[streamtype]))) {
		  this->spu_decoder_plugins[streamtype] = sdp;
		  spu_prio[streamtype] = plugin_prio;
		}
	      
	      printf("spu decoder plugin found : %s\n",
		     sdp->get_identifier());
	    }
	  }

	  /*
	   * does this plugin provide an video decoder plugin?
	   */

	  if((initplug = dlsym(plugin, "init_video_decoder_plugin")) != NULL) {

	    video_decoder_t *vdp;
	    int              streamtype;
	      
	    vdp = (video_decoder_t *) initplug(iface_version, config);
	    if (vdp) {
	      vdp->metronom = this->metronom;

	      for (streamtype = 0; streamtype<DECODER_PLUGIN_MAX; streamtype++)
		if ((plugin_prio =
		     decide_video_insert(vdp, streamtype, video_prio[streamtype]))) {
		  this->video_decoder_plugins[streamtype] = vdp;
		  video_prio[streamtype] = plugin_prio;
		}
	    
	      printf("video decoder plugin found : %s\n", 
		     vdp->get_identifier());
	    }
	  }
	  
	  /*
	   * does this plugin provide an audio decoder plugin?
	   */

	  if((initplug = dlsym(plugin, "init_audio_decoder_plugin")) != NULL) {

	    audio_decoder_t *adp;
	    int              streamtype;
	      
	    adp = (audio_decoder_t *) initplug(iface_version, config);
	    if (adp) {
	    
	      for (streamtype = 0; streamtype<DECODER_PLUGIN_MAX; streamtype++)
		if ((plugin_prio =
		     decide_audio_insert(adp, streamtype, audio_prio[streamtype]))) {
		  this->audio_decoder_plugins[streamtype] = adp; 
		  audio_prio[streamtype] = plugin_prio;
		}
	      
	      printf("audio decoder plugin found : %s\n", 
		     adp->get_identifier());
	    }
	  }
	  
	}
      }
    }
  }

  this->cur_spu_decoder_plugin = NULL;
  this->cur_video_decoder_plugin = NULL;
  this->cur_audio_decoder_plugin = NULL;
}

/** ***************************************************************
 * Video output plugins section
 */
char **xine_list_video_output_plugins (int visual_type) {

  char **plugin_ids;
  int    num_plugins = 0;
  DIR   *dir;
  int    i,j;
  int    plugin_prios[50];

  plugin_ids = (char **) xmalloc (50 * sizeof (char *));
  plugin_ids[0] = NULL;

  dir = opendir (XINE_PLUGINDIR);
  
  if (dir) {
    struct dirent *dir_entry;
    
    while ((dir_entry = readdir (dir)) != NULL) {
      char  str[1024];
      void *plugin;
      int nLen = strlen (dir_entry->d_name);
      
      if ((strncasecmp(dir_entry->d_name,
 		       XINE_VIDEO_OUT_PLUGIN_PREFIXNAME, 
		       XINE_VIDEO_OUT_PLUGIN_PREFIXNAME_LENGTH) == 0) &&
	  ((dir_entry->d_name[nLen-3]=='.') 
	   && (dir_entry->d_name[nLen-2]=='s')
	   && (dir_entry->d_name[nLen-1]=='o'))) {
	
	/*printf ("load_plugins: found a video output plugin: %s\n",
	  dir_entry->d_name); */

	sprintf (str, "%s/%s", XINE_PLUGINDIR, dir_entry->d_name);

	/*
	 * now, see if we can open this plugin,
	 * check if it has got the right visual type 
	 * and finally if all this went through get it's id
	 */

	if(!(plugin = dlopen (str, RTLD_LAZY))) {

	  printf("load_plugins: cannot load plugin %s:\n%s\n",
		 str, dlerror()); 

	} else {

	  vo_info_t* (*getinfo) ();
	  vo_info_t   *vo_info;

	  if ((getinfo = dlsym(plugin, "get_video_out_plugin_info")) != NULL) {
	    vo_info = getinfo();

	    if ( (vo_info->visual_type == visual_type)
		 && (vo_info->interface_version == VIDEO_OUT_IFACE_VERSION) ) {
	      /* printf("video output plugin found : %s (%s)\n",
		     vo_info->id, vo_info->description); */

	      /* sort the list by vo_info->priority */

	      i = 0;
	      while ( (i<num_plugins) && (vo_info->priority<plugin_prios[i]) )
		i++;
	      
	      j = num_plugins;
	      while (j>i) {
		plugin_ids[j]   = plugin_ids[j-1];
		plugin_prios[j] = plugin_prios[j-1];
		j--;
	      }

	      plugin_ids[i] = (char *) malloc (strlen(vo_info->id)+1);
	      strcpy (plugin_ids[i], vo_info->id);
	      plugin_prios[i] = vo_info->priority;

	      num_plugins++;
	      plugin_ids[num_plugins] = NULL;
	    }
	  } else {

	    printf("load_plugins: %s seems to be an invalid plugin "
		   "(lacks get_video_out_plugin_info() function)\n",  str);

	  }
	  dlclose (plugin);
	}
      }
    }
  } else {
    fprintf(stderr, "load_plugins: %s - cannot access plugin dir: %s",
	    __FUNCTION__, strerror(errno));
  }
  
  return plugin_ids;
}


vo_driver_t *xine_load_video_output_plugin(config_values_t *config,
				           char *id, 
					   int visual_type, void *visual) {
  DIR *dir;
  vo_driver_t *vod;
  
  dir = opendir (XINE_PLUGINDIR);
  
  if (dir) {
    struct dirent *dir_entry;
    
    while ((dir_entry = readdir (dir)) != NULL) {
      char str[1024];
      void *plugin;
      
      int nLen = strlen (dir_entry->d_name);
      
      if ((strncasecmp(dir_entry->d_name,
 		       XINE_VIDEO_OUT_PLUGIN_PREFIXNAME, 
		       XINE_VIDEO_OUT_PLUGIN_PREFIXNAME_LENGTH) == 0) &&
	  ((dir_entry->d_name[nLen-3]=='.') 
	   && (dir_entry->d_name[nLen-2]=='s')
	   && (dir_entry->d_name[nLen-1]=='o'))) {
	
	sprintf (str, "%s/%s", XINE_PLUGINDIR, dir_entry->d_name);
	
	if(!(plugin = dlopen (str, RTLD_LAZY))) {
	  printf("load_plugins: video output plugin %s failed to link:\n%s\n",
		 str, dlerror());
	} else {
	  vo_info_t* (*getinfo) (void);
	  vo_info_t   *vo_info;

	  if ((getinfo = dlsym(plugin, "get_video_out_plugin_info")) != NULL) {
	    vo_info = getinfo();

	    if (!strcmp(id, vo_info->id) ) {

	      if (vo_info->interface_version == VIDEO_OUT_IFACE_VERSION) {

		void *(*initplug) (config_values_t *, void *);
	    
		if((initplug = dlsym(plugin, "init_video_out_plugin")) != NULL) {
		  
		  vod = (vo_driver_t *) initplug(config, visual);
		  
		  if (vod)
		    printf("load_plugins: video output plugin %s successfully"
			   " loaded.\n", id);
		  else
		    printf("load_plugins: video output plugin %s: "
			   "init_video_out_plugin failed.\n", str);
		  
		  return vod;
		}
	      } else {
		
		printf("load_plugins: video output plugin %s: "
		       "wrong interface version %d.\n", str, vo_info->interface_version);
	      }
	    }
	  }
	}
      }
    }
  }

  printf ("load_plugins: failed to find video output plugin <%s>\n", id);
  return NULL;
}

/** ***************************************************************
 *  Audio output plugins section
 */
char **xine_list_audio_output_plugins(void) {

  char **plugin_ids;
  int    num_plugins = 0;
  DIR   *dir;
  int    i,j;
  int    plugin_prios[50];

  plugin_ids = (char **) xmalloc (50 * sizeof (char *));
  plugin_ids[0] = NULL;

  dir = opendir (XINE_PLUGINDIR);
  
  if (dir) {
    struct dirent *dir_entry;
    
    while ((dir_entry = readdir (dir)) != NULL) {
      char  str[1024];
      void *plugin;
      int nLen = strlen (dir_entry->d_name);
      
      if ((strncasecmp(dir_entry->d_name,
 		       XINE_AUDIO_OUT_PLUGIN_PREFIXNAME, 
		       XINE_AUDIO_OUT_PLUGIN_PREFIXNAME_LENGTH) == 0) &&
	  ((dir_entry->d_name[nLen-3]=='.') 
	   && (dir_entry->d_name[nLen-2]=='s')
	   && (dir_entry->d_name[nLen-1]=='o'))) {
	
	sprintf (str, "%s/%s", XINE_PLUGINDIR, dir_entry->d_name);

	/* printf ("load_plugins: trying to load plugin %s\n", str); */

	/*
	 * now, see if we can open this plugin,
	 * and get it's id
	 */

	if(!(plugin = dlopen (str, RTLD_LAZY))) {

	  /* printf("load_plugins: cannot load plugin %s (%s)\n",
		 str, dlerror()); */

	} else {

	  ao_info_t* (*getinfo) ();
	  ao_info_t   *ao_info;

	  /* printf ("load_plugins: plugin %s successfully loaded.\n", str);  */

	  if ((getinfo = dlsym(plugin, "get_audio_out_plugin_info")) != NULL) {
	    ao_info = getinfo();
	    /* printf("load_plugins: id=%s, priority=%d\n", ao_info->id, ao_info->priority);  */

	    if ( ao_info->interface_version == AUDIO_OUT_IFACE_VERSION) {

	      /* sort the list by ao_info->priority */

	      i = 0;
	      while ( (i<num_plugins) && (ao_info->priority<plugin_prios[i]) )
		i++;
	      
	      j = num_plugins;
	      while (j>i) {
		plugin_ids[j]   = plugin_ids[j-1];
		plugin_prios[j] = plugin_prios[j-1];
		j--;
	      }

	      /*printf("i = %d, id=%s, priority=%d\n", i, ao_info->id, ao_info->priority); */
	      plugin_ids[i] = (char *) malloc (strlen(ao_info->id)+1);
	      strcpy (plugin_ids[i], ao_info->id);
	      plugin_prios[i] = ao_info->priority;

	      num_plugins++;
	      plugin_ids[num_plugins] = NULL;
	    }
	  } else {

	    printf("load_plugins: %s seems to be an invalid plugin "
		   "(lacks get_audio_out_plugin_info() function)\n",  str);

	  }
	  dlclose (plugin);
	}
      }
    }
  } else {
    fprintf (stderr, "load_plugins: %s - cannot access plugin dir: %s",
	     __FUNCTION__, strerror(errno));
  }
  
  return plugin_ids;
}

ao_driver_t *xine_load_audio_output_plugin(config_values_t *config, 
					      char *id) {

  DIR *dir;
  ao_driver_t *aod = NULL;
  
  dir = opendir (XINE_PLUGINDIR);
  
  if (dir) {
    struct dirent *pEntry;
    
    while ((pEntry = readdir (dir)) != NULL) {
      char str[1024];
      void *plugin;
      int nLen = strlen (pEntry->d_name);

      aod = NULL;
      memset(&str, 0, 1024);
      
      if ((strncasecmp(pEntry->d_name,
 		       XINE_AUDIO_OUT_PLUGIN_PREFIXNAME, 
		       XINE_AUDIO_OUT_PLUGIN_PREFIXNAME_LENGTH) == 0) &&
	  ((pEntry->d_name[nLen-3]=='.') 
	   && (pEntry->d_name[nLen-2]=='s')
	   && (pEntry->d_name[nLen-1]=='o'))) {
	
	sprintf (str, "%s/%s", XINE_PLUGINDIR, pEntry->d_name);
	
	if(!(plugin = dlopen (str, RTLD_LAZY | RTLD_GLOBAL))) {
	  printf("load_plugins: audio output plugin %s failed to link: %s\n",
		 str, dlerror());
	  return NULL;
	} else {
	  void *(*initplug) (config_values_t *);
	  ao_info_t* (*getinfo) ();
	  ao_info_t   *ao_info;

	  if ((getinfo = dlsym(plugin, "get_audio_out_plugin_info")) != NULL) {
	    ao_info = getinfo();
	  
	    if (!strcmp(id, ao_info->id)) {
	    
	      if((initplug = dlsym(plugin, "init_audio_out_plugin")) != NULL) {
		
		aod = (ao_driver_t *) initplug(config);
		
		if (aod)
		  printf("load_plugins: audio output plugin %s successfully"
			 " loaded.\n", id);
		else
		  printf("load_plugins: audio output plugin %s: "
			 "init_audio_out_plugin failed.\n", str);
		
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

/** ***************************************************************
 *  Autoplay featured plugins section
 */
char **xine_get_autoplay_mrls (xine_t *this, char *plugin_id, int *num_mrls) {
  input_plugin_t  *ip;
  char           **autoplay_mrls = NULL;
  int              i;
  
  if(!this || !plugin_id)
    return NULL;
  
  if(!this->num_input_plugins)
    return NULL;

  for(i = 0; i < this->num_input_plugins; i++) {

    ip = this->input_plugins[i];

    if(!strcasecmp((ip->get_identifier(ip)), plugin_id)) {
      if(((ip->get_capabilities(ip)) & INPUT_CAP_AUTOPLAY)) {

	if(ip->get_autoplay_list) {
	  autoplay_mrls = ip->get_autoplay_list(ip, num_mrls);
	  this->cur_input_plugin = this->input_plugins[i];
	}

      }
      goto autoplay_mrls_done;
    }
  }

 autoplay_mrls_done:
  return autoplay_mrls;
}

/** ***************************************************************
 *  Browse featured plugins section
 */
mrl_t **xine_get_browse_mrls (xine_t *this, char *plugin_id, char *start_mrl, int *num_mrls) {
  input_plugin_t  *ip;
  mrl_t **browse_mrls = NULL;
  int              i;
  
  if(!this || !plugin_id)
    return NULL;
  
  if(!this->num_input_plugins)
    return NULL;

  for(i = 0; i < this->num_input_plugins; i++) {

    ip = this->input_plugins[i];

    if(!strcasecmp((ip->get_identifier(ip)), plugin_id)) {
      if(((ip->get_capabilities(ip)) & INPUT_CAP_GET_DIR)) {

	if(ip->get_dir) {
	  browse_mrls = ip->get_dir(ip, start_mrl, num_mrls);
	  this->cur_input_plugin = this->input_plugins[i];
	}

      }
      goto browse_mrls_done;
    }
  }

 browse_mrls_done:
  return browse_mrls;
}
