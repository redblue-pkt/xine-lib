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
 * $Id: load_plugins.c,v 1.93 2002/09/16 21:49:35 miguelfreitas Exp $
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
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>

#include "xine_internal.h"
#include "xine_plugin.h"
#include "plugin_catalog.h"
#include "demuxers/demux.h"
#include "input/input_plugin.h"
#include "video_out.h"
#include "metronom.h"
#include "configfile.h"
#include "xineutils.h"
#include "compat.h"

/*
#define LOG
*/

extern int errno;

static char *plugin_name;

#if DONT_CATCH_SIGSEGV

#define install_segv_handler()
#define remove_segv_handler()

#else

void (*old_segv_handler)(int);

static void segv_handler (int hubba) {
  printf ("\nload_plugins: Initialization of plugin '%s' failed (segmentation fault).\n",plugin_name);
  printf ("load_plugins: You probably need to remove the offending file.\n");
  printf ("load_plugins: (This error is usually due an incorrect plugin version)\n"); 
  abort();
}

static void install_segv_handler(void){
  old_segv_handler = signal (SIGSEGV, segv_handler);
}

static void remove_segv_handler(void){
  signal (SIGSEGV, old_segv_handler );
}

#endif




/*
 * plugin list/catalog management functions
 *
 */

static char  *_strclone(const char *str){
  char *new;

  new = xine_xmalloc(strlen(str)+1);
  strcpy(new, str);
  return new;
}

static void _insert_plugin (xine_list_t *list,
			    char *filename, plugin_info_t *info){

  plugin_node_t     *entry;
  vo_info_t         *vo_new, *vo_old;
  ao_info_t         *ao_new, *ao_old;
  decoder_info_t    *decoder_new, *decoder_old;
  uint32_t          *types;
  int                priority = 0;
  int                i;

  entry = xine_xmalloc(sizeof(plugin_node_t));
  entry->filename = _strclone(filename);
  entry->info = xine_xmalloc(sizeof(plugin_info_t));
  *(entry->info) = *info;
  entry->info->id = _strclone(info->id);
  entry->info->init = NULL;

  switch (info->type){

  case PLUGIN_VIDEO_OUT:
    vo_old = info->special_info;
    vo_new = xine_xmalloc(sizeof(vo_info_t));
    priority = vo_new->priority = vo_old->priority;
    vo_new->description = _strclone(vo_old->description);
    vo_new->visual_type = vo_old->visual_type;
    entry->info->special_info = vo_new;
    break;

  case PLUGIN_AUDIO_OUT:
    ao_old = info->special_info;
    ao_new = xine_xmalloc(sizeof(ao_info_t));
    priority = ao_new->priority = ao_old->priority;
    ao_new->description = _strclone(ao_old->description);
    entry->info->special_info = ao_new;
    break;

  case PLUGIN_AUDIO_DECODER:
  case PLUGIN_VIDEO_DECODER:
  case PLUGIN_SPU_DECODER:
    decoder_old = info->special_info;
    decoder_new = xine_xmalloc(sizeof(decoder_info_t));
    if (decoder_old == NULL) {
      printf ("load_plugins: plugin %s from %s is broken: special_info=NULL\n",
	      info->id, entry->filename);
      abort();
    }
    for (i=0; decoder_old->supported_types[i] != 0; ++i);
    types = xine_xmalloc((i+1)*sizeof(uint32_t));
    for (i=0; decoder_old->supported_types[i] != 0; ++i){
      types[i] = decoder_old->supported_types[i];
    }
    decoder_new->supported_types = types;
    priority = decoder_new->priority = decoder_old->priority;
    entry->info->special_info = decoder_new;
    break;
  }

  xine_list_append_priority_content (list, entry, priority);
}


static plugin_catalog_t *_empty_catalog(void){

  plugin_catalog_t *catalog;
  
  catalog = xine_xmalloc(sizeof(plugin_catalog_t));
  catalog->input = xine_list_new();
  catalog->demux = xine_list_new();
  catalog->spu   = xine_list_new();
  catalog->audio = xine_list_new();
  catalog->video = xine_list_new();
  catalog->aout  = xine_list_new();
  catalog->vout  = xine_list_new();

  return catalog;
}

/*
 * First stage plugin loader (catalog builder)
 *
 ***************************************************************************/

static void collect_plugins(xine_t *this, char *path){

  DIR *dir;

#ifdef LOG
  printf ("load_plugins: collect_plugins in %s\n", path);
#endif

  dir = opendir(path);  
  if (dir) {
    struct dirent *pEntry;
    
    while ((pEntry = readdir (dir)) != NULL) {
      char *str;
      void *lib;
      struct stat statbuffer;
	  
      str = xine_xmalloc(strlen(path) + strlen(pEntry->d_name) + 2);
      sprintf (str, "%s/%s", XINE_PLUGINDIR, pEntry->d_name);
	  
      if (stat(str, &statbuffer)) {
	xine_log (this, XINE_LOG_PLUGIN,
		  _("load_plugins: unable to stat %s\n"), str); 
      }
      else if( strstr(str, ".so") ) {
		
	switch (statbuffer.st_mode & S_IFMT){
		  
	case S_IFREG:
	  /* regular file, ie. plugin library, found => load it */
		  
	  plugin_name = str;
		  
	  if(!(lib = dlopen (str, RTLD_LAZY | RTLD_GLOBAL))) {
			
/*#ifdef LOG*/
	    {
	      char *dl_error_msg = dlerror();
	      /* too noisy -- but good to catch unresolved references */
	      printf ("load_plugins: cannot open plugin lib %s:\n%s\n",
		      str, dl_error_msg); 
	    }
/*#endif*/
	  }
	  else {
			
	    plugin_info_t *info;
			
	    if ((info = dlsym(lib, "xine_plugin_info"))) {
			  
	      for (; info->type != PLUGIN_NONE; ++info){

		xine_log (this, XINE_LOG_PLUGIN,
			  _("load_plugins: plugin %s found\n"), str);

		switch (info->type){
		case PLUGIN_INPUT:
		  _insert_plugin(this->plugin_catalog->input, str, info);
		  break;
		case PLUGIN_DEMUX:
		  _insert_plugin(this->plugin_catalog->demux, str, info);
		  break;
		case PLUGIN_AUDIO_DECODER:
		  _insert_plugin(this->plugin_catalog->audio, str, info);
		  break;
		case PLUGIN_VIDEO_DECODER:
		  _insert_plugin(this->plugin_catalog->video, str, info);
		  break;
		case PLUGIN_SPU_DECODER:
		  _insert_plugin(this->plugin_catalog->spu, str, info);
		  break;
		case PLUGIN_AUDIO_OUT:
		  _insert_plugin(this->plugin_catalog->aout, str, info);
		  break;
		case PLUGIN_VIDEO_OUT:
		  _insert_plugin(this->plugin_catalog->vout, str, info);
		  break;
		default:
		  xine_log (this, XINE_LOG_PLUGIN,
			    _("load_plugins: unknown plugin type %d in %s\n"),
			    info->type, str);
		}
	      }
			  
	    }
	    else {
	      char *dl_error_msg = dlerror();
			  
	      xine_log (this, XINE_LOG_PLUGIN,
			_("load_plugins: can't get plugin info from %s:\n%s\n"),
			str, dl_error_msg); 
	    }
	    dlclose(lib);
	  }
	  break;
	case S_IFDIR:
		  
	  if (*pEntry->d_name != '.'){ /* catches ".", ".." or ".hidden" dirs */
	    collect_plugins(this, str);
	  }
	} /* switch */
      } /* if (stat(...)) */
      free(str);
    } /* while */
  } /* if (dir) */
} /* collect_plugins */


/*
 * generic 2nd stage plugin loader
 */

static void *_load_plugin(xine_t *this,
			  char *filename, plugin_info_t *target,
			  void *data) {

  void *lib;

  if(!(lib = dlopen (filename, RTLD_LAZY | RTLD_GLOBAL))) {

    xine_log (this, XINE_LOG_PLUGIN,
	      _("load_plugins: cannot (stage 2) open plugin lib %s:\n%s\n"),
	      filename, dlerror()); 

  } else {

    plugin_info_t *info;
	
    if ((info = dlsym(lib, "xine_plugin_info"))) {
      /* TODO: use sigsegv handler */
      while (info->type != PLUGIN_NONE){
	if (info->type == target->type
	    && info->API == target->API
	    && !strcasecmp(info->id, target->id)
	    && info->version == target->version){
	  
	  return info->init(this, data);
	}
	info++;
      }

    } else {
      xine_log (this, XINE_LOG_PLUGIN,
		"load_plugins: Yikes! %s doesn't contain plugin info.\n",
		filename);
    }
  }
  return NULL; /* something failed if we came here... */
}

/*
 *  load input+demuxer plugins
 */
static void load_plugins(xine_t *this) {

  plugin_node_t *node;

  /* 
   * input plugins
   */

  node = xine_list_first_content (this->plugin_catalog->input);
  while (node) {

#ifdef LOG
    printf("load_plugins: load input plugin %s from %s\n",
	   node->info->id, node->filename);
#endif

    node->plugin = _load_plugin(this, node->filename, node->info, NULL);

    node = xine_list_next_content (this->plugin_catalog->input);
  }

  /* 
   * demux plugins
   */

  node = xine_list_first_content (this->plugin_catalog->demux);
  while (node) {

#ifdef LOG
    printf("load_plugins: load demux plugin %s from %s\n",
	   node->info->id, node->filename);
#endif

    node->plugin = _load_plugin(this, node->filename, node->info, NULL);

    node = xine_list_next_content (this->plugin_catalog->demux);
  }
}

static void map_decoders (xine_t *this) {

  plugin_catalog_t *catalog = this->plugin_catalog;
  plugin_node_t    *node;
  int               i, pos;

#ifdef LOG
  printf ("load_plugins: map_decoders\n");
#endif

  /* clean up */

  for (i=0; i<DECODER_MAX; i++) {
    catalog->audio_decoder_map[i][0]=NULL;
    catalog->video_decoder_map[i][0]=NULL;
    catalog->spu_decoder_map[i][0]=NULL;
  }
  
  /* 
   * map audio decoders 
   */
  
  node = xine_list_first_content (this->plugin_catalog->audio);
  while (node) {

    decoder_info_t *di = (decoder_info_t *) node->info->special_info;
    int            *type;

#ifdef LOG
    printf ("load_plugins: mapping decoder %s\n", node->info->id);
#endif

    type = di->supported_types;

    while (type && (*type)) {

      int streamtype = ((*type)>>16) & 0xFF;

#ifdef LOG
      printf ("load_plugins: decoder handles stream type %02x, priority %d\n",
	      streamtype, di->priority);
#endif

      /* find the right place based on the priority */
      for (pos = 0; pos < PLUGINS_PER_TYPE; pos++)
        if (!catalog->audio_decoder_map[streamtype][pos] ||
	    ((decoder_info_t *)catalog->audio_decoder_map[streamtype][pos]->info->special_info)->priority <=
	    di->priority)
          break;
      
      /* shift the decoder list for this type by one to make room for new decoder */
      for (i = PLUGINS_PER_TYPE - 1; i > pos; i--)
        catalog->audio_decoder_map[streamtype][i] = catalog->audio_decoder_map[streamtype][i - 1];
	
      /* insert new decoder */
      catalog->audio_decoder_map[streamtype][pos] = node;
#ifdef LOG
      printf("load_plugins: decoder inserted in decoder map at %d\n", pos);
#endif
      
      type++;
    }

    node = xine_list_next_content (this->plugin_catalog->audio);
  }

  /* 
   * map video decoders 
   */
  
  node = xine_list_first_content (this->plugin_catalog->video);
  while (node) {

    decoder_info_t *di = (decoder_info_t *) node->info->special_info;
    int            *type;

#ifdef LOG
    printf ("load_plugins: mapping decoder %s\n", node->info->id);
#endif

    type = di->supported_types;

    while (type && (*type)) {

      int streamtype = ((*type)>>16) & 0xFF;

#ifdef LOG
      printf ("load_plugins: decoder handles stream type %02x, priority %d\n",
	      streamtype, di->priority);
#endif

      /* find the right place based on the priority */
      for (pos = 0; pos < PLUGINS_PER_TYPE; pos++)
        if (!catalog->video_decoder_map[streamtype][pos] ||
	    ((decoder_info_t *)catalog->video_decoder_map[streamtype][pos]->info->special_info)->priority <=
	    di->priority)
          break;
      
      /* shift the decoder list for this type by one to make room for new decoder */
      for (i = PLUGINS_PER_TYPE - 1; i > pos; i--)
        catalog->video_decoder_map[streamtype][i] = catalog->video_decoder_map[streamtype][i - 1];
	
      /* insert new decoder */
      catalog->video_decoder_map[streamtype][pos] = node;
#ifdef LOG
      printf("load_plugins: decoder inserted in decoder map at %d\n", pos);
#endif
      
      type++;
    }

    node = xine_list_next_content (this->plugin_catalog->video);
  }

  /* 
   * map spu decoders 
   */
  
  node = xine_list_first_content (this->plugin_catalog->spu);
  while (node) {

    decoder_info_t *di = (decoder_info_t *) node->info->special_info;
    int            *type;

#ifdef LOG
    printf ("load_plugins: mapping decoder %s\n", node->info->id);
#endif

    type = di->supported_types;

    while (type && (*type)) {

      int streamtype = ((*type)>>16) & 0xFF;

#ifdef LOG
      printf ("load_plugins: decoder handles stream type %02x, priority %d\n",
	      streamtype, di->priority);
#endif

      /* find the right place based on the priority */
      for (pos = 0; pos < PLUGINS_PER_TYPE; pos++)
        if (!catalog->spu_decoder_map[streamtype][pos] ||
	    ((decoder_info_t *)catalog->spu_decoder_map[streamtype][pos]->info->special_info)->priority <=
	    di->priority)
          break;
      
      /* shift the decoder list for this type by one to make room for new decoder */
      for (i = PLUGINS_PER_TYPE - 1; i > pos; i--)
        catalog->spu_decoder_map[streamtype][i] = catalog->spu_decoder_map[streamtype][i - 1];
	
      /* insert new decoder */
      catalog->spu_decoder_map[streamtype][pos] = node;
#ifdef LOG
      printf("load_plugins: decoder inserted in decoder map at %d\n", pos);
#endif
      
      type++;
    }

    node = xine_list_next_content (this->plugin_catalog->spu);
  }
}

/*
 *  initialize catalog, load all plugins into new catalog
 */
void scan_plugins (xine_t *this) {

#ifdef LOG
  printf("load_plugins: scan_plugins()\n");
#endif
  if (this == NULL || this->config == NULL) {
    fprintf(stderr, "%s(%s@%d): parameter should be non null, exiting\n",
	    __FILE__, __XINE_FUNCTION__, __LINE__);
    abort();
  }

  this->plugin_catalog = _empty_catalog();
  /* TODO: add more plugin dir(s), maybe ~/.xine/plugins or /usr/local/... */
  collect_plugins(this, XINE_PLUGINDIR);

  load_plugins (this);

  map_decoders (this);
}

static const char **_xine_get_featured_input_plugin_ids(xine_p this, int feature) {

  plugin_catalog_t   *catalog;
  int                 i;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  i = 0;
  node = xine_list_first_content (catalog->input);
  while (node) {
    input_plugin_t *ip;

    ip = (input_plugin_t *) node->plugin;
    if (ip->get_capabilities(ip) & feature) {

      catalog->ids[i] = node->info->id;

      i++;
    }
    node = xine_list_next_content (catalog->input);
  }

  catalog->ids[i] = NULL;

  return catalog->ids;
}

const char *const *xine_get_autoplay_input_plugin_ids(xine_p this) {

  return (_xine_get_featured_input_plugin_ids(this, INPUT_CAP_AUTOPLAY));
}

const char *const *xine_get_browsable_input_plugin_ids(xine_p this) {

  return (_xine_get_featured_input_plugin_ids(this, INPUT_CAP_GET_DIR));
}

const char *xine_get_input_plugin_description(xine_p this, const char *plugin_id) {

  plugin_catalog_t   *catalog;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  node = xine_list_first_content (catalog->input);
  while (node) {

    if (!strcasecmp (node->info->id, plugin_id)) {

      input_plugin_t *ip = (input_plugin_t *) node->plugin;

      return ip->get_description(ip);
    }
    node = xine_list_next_content (catalog->input);
  }
  return NULL;
}

/*
 *  video out plugins section
 */

xine_vo_driver_p xine_open_video_driver (xine_p this_ro,
					 const char *id, 
					 int visual_type, void *visual) {
  xine_t *this = (xine_t *)this_ro;

  plugin_node_t      *node;
  xine_vo_driver_t   *driver;
  vo_info_t          *vo_info;

  driver = NULL;

  node = xine_list_first_content (this->plugin_catalog->vout);
  while (node) {

    vo_info = node->info->special_info;
    if (vo_info->visual_type == visual_type) {
      if (id) {
	if (!strcasecmp (node->info->id, id)) {
	  driver = (xine_vo_driver_t*)_load_plugin (this, node->filename, 
						    node->info, visual);
	  break;
	}

      } else {

	driver = (xine_vo_driver_t*)_load_plugin (this, node->filename, 
						  node->info, visual);
	if (driver)
	  break;
	
      }
    }
    
    node = xine_list_next_content (this->plugin_catalog->vout);
  }

  if (!driver) 
    printf ("load_plugins: failed to load video output plugin <%s>\n", id);

  return driver;
}

/*
 *  audio output plugins section
 */

const char *const *xine_list_audio_output_plugins (xine_p this) {

  plugin_catalog_t   *catalog;
  int                 i;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  i = 0;
  node = xine_list_first_content (catalog->aout);
  while (node) {

    catalog->ids[i] = node->info->id;

    i++;

    node = xine_list_next_content (catalog->aout);
  }

  catalog->ids[i] = NULL;

  return catalog->ids;
}

const char *const *xine_list_video_output_plugins (xine_p this) {

  plugin_catalog_t   *catalog;
  int                 i;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  i = 0;
  node = xine_list_first_content (catalog->vout);
  while (node) {

    catalog->ids[i] = node->info->id;

    i++;

    node = xine_list_next_content (catalog->vout);
  }

  catalog->ids[i] = NULL;

  return catalog->ids;
}

xine_ao_driver_p xine_open_audio_driver (xine_p this_ro, const char *id,
					 void *data) {
  xine_t *this = (xine_t *)this_ro;
  
  plugin_node_t      *node;
  xine_ao_driver_t   *driver;
  ao_info_t          *ao_info;

  driver = NULL;

  node = xine_list_first_content (this->plugin_catalog->aout);
  while (node) {

    ao_info = node->info->special_info;

    if (id) {
      if (!strcasecmp(node->info->id, id)) {
	driver = (xine_ao_driver_t*)_load_plugin(this, node->filename, node->info, data);
	break;
      }
    } else {
      driver = (xine_ao_driver_t*)_load_plugin (this, node->filename, node->info, data);
      if (driver)
	  break;
    }

    node = xine_list_next_content (this->plugin_catalog->aout);
  }

  if (!driver) {
    if (id)
      printf ("load_plugins: failed to load audio output plugin <%s>\n", id);
    else
      printf ("load_plugins: audio output auto-probing didn't find any usable audio driver.\n");
  }
  return driver; 
} 

/* 
 * get autoplay mrl list from input plugin
 */

const char *const *xine_get_autoplay_mrls (xine_p this, const char *plugin_id, 
					   int *num_mrls) {

  plugin_catalog_t   *catalog;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  node = xine_list_first_content (catalog->input);
  while (node) {

    if (!strcasecmp (node->info->id, plugin_id)) {

      input_plugin_t *ip = (input_plugin_t *) node->plugin;

      if (!( ip->get_capabilities(ip) & INPUT_CAP_AUTOPLAY)) 
	return NULL;

      /* this->cur_input_plugin = ip;  FIXME: needed? */

      return ip->get_autoplay_list (ip, num_mrls);
    }
    node = xine_list_next_content (catalog->input);
  }
  return NULL;
}

/*
 * input plugin mrl browser support
 */
const xine_mrl_t *const *xine_get_browse_mrls (xine_p this, const char *plugin_id, 
				               const char *start_mrl, int *num_mrls) {

  plugin_catalog_t   *catalog;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  node = xine_list_first_content (catalog->input);
  while (node) {

    if (!strcasecmp (node->info->id, plugin_id)) {

      input_plugin_t *ip = (input_plugin_t *) node->plugin;

      if (!( ip->get_capabilities(ip) & INPUT_CAP_GET_DIR)) 
	return NULL;

      return ip->get_dir (ip, start_mrl, num_mrls);
    }
    node = xine_list_next_content (catalog->input);
  }
  return NULL;
}

video_decoder_t *get_video_decoder (xine_t *this, uint8_t stream_type) {

  plugin_node_t   *node;
  int             i, j;

#ifdef LOG
  printf ("load_plugins: looking for video decoder for streamtype %02x\n",
	  stream_type);
#endif

  for (i = 0; i < PLUGINS_PER_TYPE; i++) {
    node = this->plugin_catalog->video_decoder_map[stream_type][i];

    if (!node)
      return NULL;

    if (!node->plugin) 
      node->plugin = _load_plugin(this, node->filename, node->info, NULL);

    if (node->plugin)
      return node->plugin;
    else {
      /* remove non working plugin from catalog */
      for (j = i + 1; j < PLUGINS_PER_TYPE; j++)
        this->plugin_catalog->video_decoder_map[stream_type][j - 1] =
          this->plugin_catalog->video_decoder_map[stream_type][j];
      this->plugin_catalog->video_decoder_map[stream_type][PLUGINS_PER_TYPE] = NULL;
      i--;
    }
  }
  return NULL;
}

audio_decoder_t *get_audio_decoder (xine_t *this, uint8_t stream_type) {

  plugin_node_t   *node;
  int             i, j;

#ifdef LOG
  printf ("load_plugins: looking for audio decoder for streamtype %02x\n",
	  stream_type);
#endif

  for (i = 0; i < PLUGINS_PER_TYPE; i++) {
    node = this->plugin_catalog->audio_decoder_map[stream_type][i];

    if (!node)
      return NULL;

    if (!node->plugin) 
      node->plugin = _load_plugin(this, node->filename, node->info, NULL);

    if (node->plugin)
      return node->plugin;
    else {
      /* remove non working plugin from catalog */
      for (j = i + 1; j < PLUGINS_PER_TYPE; j++)
        this->plugin_catalog->audio_decoder_map[stream_type][j - 1] =
          this->plugin_catalog->audio_decoder_map[stream_type][j];
      this->plugin_catalog->audio_decoder_map[stream_type][PLUGINS_PER_TYPE] = NULL;
      i--;
    }
  }
  return NULL;
}

spu_decoder_t   *get_spu_decoder   (xine_t *this, uint8_t stream_type) {

  plugin_node_t   *node;
  int             i, j;

#ifdef LOG
  printf ("load_plugins: looking for spu decoder for streamtype %02x\n",
	  stream_type);
#endif

  for (i = 0; i < PLUGINS_PER_TYPE; i++) {
    node = this->plugin_catalog->spu_decoder_map[stream_type][i];

    if (!node)
      return NULL;

    if (!node->plugin) 
      node->plugin = _load_plugin(this, node->filename, node->info, NULL);

    if (node->plugin)
      return node->plugin;
    else {
      /* remove non working plugin from catalog */
      for (j = i + 1; j < PLUGINS_PER_TYPE; j++)
        this->plugin_catalog->spu_decoder_map[stream_type][j - 1] =
          this->plugin_catalog->spu_decoder_map[stream_type][j];
      this->plugin_catalog->spu_decoder_map[stream_type][PLUGINS_PER_TYPE] = NULL;
      i--;
    }
  }
  return NULL;
}


/*
 * dispose all currently loaded plugins (shutdown)
 */

void dispose_plugins (xine_t *this) {

  /* FIXME: adapt old code */
  plugin_node_t *node;

  node = xine_list_first_content (this->plugin_catalog->demux);
  while (node) {
    demux_plugin_t *dp = node->plugin;

    if (dp)
      dp->close (dp);

    node = xine_list_next_content (this->plugin_catalog->demux);
  }

  node = xine_list_first_content (this->plugin_catalog->input);
  while (node) {
    input_plugin_t *ip = node->plugin;

    if (ip)
      ip->dispose (ip);

    node = xine_list_next_content (this->plugin_catalog->input);
  }

#if 0
  for (i = 0; i < this->num_audio_decoders_loaded; i++) 
    this->audio_decoders_loaded[i]->dispose (this->audio_decoders_loaded[i]);

  for (i = 0; i < this->num_video_decoders_loaded; i++) 
    this->video_decoders_loaded[i]->dispose (this->video_decoders_loaded[i]);

  for (i = 0; i < this->num_spu_decoders_loaded; i++) 
    this->spu_decoders_loaded[i]->dispose (this->spu_decoders_loaded[i]);

#endif
}
