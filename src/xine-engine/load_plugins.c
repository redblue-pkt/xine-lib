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
 * $Id: load_plugins.c,v 1.106 2002/10/26 16:16:04 mroi Exp $
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

static int _get_decoder_priority (xine_t *this, int default_priority,
				  char *id) {

  char str[80];

  sprintf (str, "decoder.%s_priority", id);

  return this->config->register_num (this->config,
				     str,
				     default_priority,
				     "decoder's priority compared to others",
				     NULL, 20,
				     NULL, NULL /* FIXME: implement callback */);
}

static void _insert_plugin (xine_t *this,
			    xine_list_t *list,
			    char *filename, plugin_info_t *info,
			    int api_version){

  plugin_node_t     *entry;
  vo_info_t         *vo_new, *vo_old;
  ao_info_t         *ao_new, *ao_old;
  decoder_info_t    *decoder_new, *decoder_old;
  uint32_t          *types;
  int                priority = 0;
  int                i;

  if (info->API != api_version) {
    printf ("load_plugins: ignoring plugin %s, wrong iface version %d (should be %d)\n",
	    info->id, info->API, api_version);
    return;
  }

  entry = xine_xmalloc(sizeof(plugin_node_t));
  entry->filename     = _strclone(filename);
  entry->info         = xine_xmalloc(sizeof(plugin_info_t));
  *(entry->info)      = *info;
  entry->info->id     = _strclone(info->id);
  entry->info->init   = NULL;
  entry->plugin_class = NULL;
  entry->ref          = 0;

  switch (info->type){

  case PLUGIN_VIDEO_OUT:
    vo_old = info->special_info;
    vo_new = xine_xmalloc(sizeof(vo_info_t));
    priority = vo_new->priority = vo_old->priority;
    vo_new->visual_type = vo_old->visual_type;
    entry->info->special_info = vo_new;
    break;

  case PLUGIN_AUDIO_OUT:
    ao_old = info->special_info;
    ao_new = xine_xmalloc(sizeof(ao_info_t));
    priority = ao_new->priority = ao_old->priority;
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

    priority = _get_decoder_priority (this, priority, info->id);

    entry->info->special_info = decoder_new;
    break;
  }

  xine_list_append_priority_content (list, entry, priority);
}


static plugin_catalog_t *_new_catalog(void){

  plugin_catalog_t *catalog;

  catalog = xine_xmalloc(sizeof(plugin_catalog_t));
  catalog->input = xine_list_new();
  catalog->demux = xine_list_new();
  catalog->spu   = xine_list_new();
  catalog->audio = xine_list_new();
  catalog->video = xine_list_new();
  catalog->aout  = xine_list_new();
  catalog->vout  = xine_list_new();

  pthread_mutex_init (&catalog->lock, NULL);

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
		  _insert_plugin (this, this->plugin_catalog->input, str, info,
				  INPUT_PLUGIN_IFACE_VERSION);
		  break;
		case PLUGIN_DEMUX:
		  _insert_plugin (this, this->plugin_catalog->demux, str, info,
				  DEMUXER_PLUGIN_IFACE_VERSION);
		  break;
		case PLUGIN_AUDIO_DECODER:
		  _insert_plugin (this, this->plugin_catalog->audio, str, info,
				  AUDIO_DECODER_IFACE_VERSION);
		  break;
		case PLUGIN_VIDEO_DECODER:
		  _insert_plugin (this, this->plugin_catalog->video, str, info,
				  VIDEO_DECODER_IFACE_VERSION);
		  break;
		case PLUGIN_SPU_DECODER:
		  _insert_plugin (this, this->plugin_catalog->spu, str, info,
				  SPU_DECODER_IFACE_VERSION);
		  break;
		case PLUGIN_AUDIO_OUT:
		  _insert_plugin (this, this->plugin_catalog->aout, str, info,
				  AUDIO_OUT_IFACE_VERSION);
		  break;
		case PLUGIN_VIDEO_OUT:
		  _insert_plugin (this, this->plugin_catalog->vout, str, info,
				  VIDEO_OUT_DRIVER_IFACE_VERSION);
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

static void *_load_plugin_class(xine_t *this,
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

    node->plugin_class = _load_plugin_class (this, node->filename, node->info, NULL);

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

    node->plugin_class = _load_plugin_class (this, node->filename, node->info, NULL);

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

  this->plugin_catalog = _new_catalog();
  /* TODO: add more plugin dir(s), maybe ~/.xine/plugins or /usr/local/... */
  collect_plugins(this, XINE_PLUGINDIR);

  load_plugins (this);

  map_decoders (this);
}

/*
 * input / demuxer plugin loading
 */

input_plugin_t *find_input_plugin (xine_stream_t *stream, const char *mrl) {

  xine_t           *xine = stream->xine;
  plugin_catalog_t *catalog = xine->plugin_catalog;
  plugin_node_t    *node;

  pthread_mutex_lock (&catalog->lock);

  node = xine_list_first_content (catalog->input);
  while (node) {
    input_plugin_t   *plugin;

    if ((plugin = ((input_class_t *)node->plugin_class)->open_plugin(node->plugin_class, stream, mrl))) {
      pthread_mutex_unlock (&catalog->lock);
      return plugin;
    }

    node = xine_list_next_content (stream->xine->plugin_catalog->input);
  }

  pthread_mutex_unlock (&catalog->lock);

  return NULL;
}

static demux_plugin_t *probe_demux (xine_stream_t *stream, int method1, int method2,
				    input_plugin_t *input) {

  int               i;
  int               methods[3];
  xine_t           *xine = stream->xine;
  plugin_catalog_t *catalog = xine->plugin_catalog;

  methods[0] = method1;
  methods[1] = method2;
  methods[2] = -1;

  if (methods[0] == -1) {
    printf ("load_plugins: probe_demux method1 = %d is not allowed \n", method1);
    abort();
  }

  i = 0;
  while (methods[i] != -1) {

    plugin_node_t *node;

    stream->content_detection_method = methods[i];

    pthread_mutex_lock (&catalog->lock);

    node = xine_list_first_content (catalog->demux);

    while (node) {
      demux_plugin_t *plugin;

      if ((plugin = ((demux_class_t *)node->plugin_class)->open_plugin(node->plugin_class, stream, input))) {
	pthread_mutex_unlock (&catalog->lock);
	return plugin;
      }

      node = xine_list_next_content (stream->xine->plugin_catalog->demux);
    }

    pthread_mutex_unlock (&catalog->lock);

    i++;
  }

  return NULL;
}

demux_plugin_t *find_demux_plugin (xine_stream_t *stream, input_plugin_t *input) {

  switch (stream->xine->demux_strategy) {

  case XINE_DEMUX_DEFAULT_STRATEGY:
    return probe_demux (stream, METHOD_BY_CONTENT, METHOD_BY_EXTENSION, input);

  case XINE_DEMUX_REVERT_STRATEGY:
    return probe_demux (stream, METHOD_BY_EXTENSION, METHOD_BY_CONTENT, input);

  case XINE_DEMUX_CONTENT_STRATEGY:
    return probe_demux (stream, METHOD_BY_CONTENT, -1, input);

  case XINE_DEMUX_EXTENSION_STRATEGY:
    return probe_demux (stream, METHOD_BY_EXTENSION, -1, input);

  default:
    printf ("load_plugins: unknown content detection strategy %d\n",
	    stream->xine->demux_strategy);
    abort();
  }

  return NULL;
}

const char *const *xine_get_autoplay_input_plugin_ids(xine_t *this) {

  plugin_catalog_t   *catalog;
  int                 i;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  pthread_mutex_lock (&catalog->lock);

  i = 0;
  node = xine_list_first_content (catalog->input);
  while (node) {
    input_class_t *ic;

    ic = (input_class_t *) node->plugin_class;
    if (ic->get_autoplay_list) {

      catalog->ids[i] = node->info->id;

      i++;
    }
    node = xine_list_next_content (catalog->input);
  }

  catalog->ids[i] = NULL;

  pthread_mutex_unlock (&catalog->lock);

  return catalog->ids;
}

const char *const *xine_get_browsable_input_plugin_ids(xine_t *this) {


  plugin_catalog_t   *catalog;
  int                 i;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  pthread_mutex_lock (&catalog->lock);

  i = 0;
  node = xine_list_first_content (catalog->input);
  while (node) {
    input_class_t *ic;

    ic = (input_class_t *) node->plugin_class;
    if (ic->get_dir) {

      catalog->ids[i] = node->info->id;

      i++;
    }
    node = xine_list_next_content (catalog->input);
  }

  catalog->ids[i] = NULL;

  pthread_mutex_unlock (&catalog->lock);

  return catalog->ids;
}

const char *xine_get_input_plugin_description (xine_t *this, const char *plugin_id) {

  plugin_catalog_t   *catalog;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  node = xine_list_first_content (catalog->input);
  while (node) {

    if (!strcasecmp (node->info->id, plugin_id)) {

      input_class_t *ic = (input_class_t *) node->plugin_class;

      return ic->get_description(ic);
    }
    node = xine_list_next_content (catalog->input);
  }
  return NULL;
}

/*
 *  video out plugins section
 */

static xine_vo_driver_t *_load_video_driver (xine_t *this, plugin_node_t *node,
					     void *data) {

  xine_vo_driver_t *driver;

  if (!node->plugin_class)
    node->plugin_class = _load_plugin_class (this, node->filename, node->info, data);

  if (!node->plugin_class)
    return NULL;

  driver = ((video_driver_class_t *)node->plugin_class)->open_plugin(node->plugin_class, data);

  if (driver) {
    driver->node = node;
    node->ref ++;
  } else {

    /* FIXME
    if (!node->ref)
    unload class
    */
  }

  return driver;
}

xine_vo_driver_t *xine_open_video_driver (xine_t *this,
					  const char *id,
					  int visual_type, void *visual) {

  plugin_node_t      *node;
  xine_vo_driver_t   *driver;
  vo_info_t          *vo_info;
  plugin_catalog_t   *catalog = this->plugin_catalog;

  driver = NULL;

  pthread_mutex_lock (&catalog->lock);

  node = xine_list_first_content (catalog->vout);
  while (node) {

    vo_info = node->info->special_info;
    if (vo_info->visual_type == visual_type) {
      if (id) {
	if (!strcasecmp (node->info->id, id)) {
	  driver = _load_video_driver (this, node, visual);
	  break;
	}

      } else {

	driver = _load_video_driver (this, node, visual);

	if (driver) {

	  xine_cfg_entry_t entry;

	  /* remember plugin id */

	  if (xine_config_lookup_entry (this, "video.driver", &entry)) {
	    entry.str_value = node->info->id;
	    xine_config_update_entry (this, &entry);
	  }

	  break;
	}
      }
    }

    node = xine_list_next_content (catalog->vout);
  }

  if (!driver)
    printf ("load_plugins: failed to load video output plugin <%s>\n", id);

  pthread_mutex_unlock (&catalog->lock);

  return driver;
}

/*
 *  audio output plugins section
 */

const char *const *xine_list_audio_output_plugins (xine_t *this) {

  plugin_catalog_t   *catalog;
  int                 i;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  pthread_mutex_lock (&catalog->lock);

  i = 0;
  node = xine_list_first_content (catalog->aout);
  while (node) {

    catalog->ids[i] = node->info->id;

    i++;

    node = xine_list_next_content (catalog->aout);
  }

  catalog->ids[i] = NULL;

  pthread_mutex_unlock (&catalog->lock);

  return catalog->ids;
}

const char *const *xine_list_video_output_plugins (xine_t *this) {

  plugin_catalog_t   *catalog;
  int                 i;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  pthread_mutex_lock (&catalog->lock);

  i = 0;
  node = xine_list_first_content (catalog->vout);
  while (node) {

    catalog->ids[i] = node->info->id;

    i++;

    node = xine_list_next_content (catalog->vout);
  }

  catalog->ids[i] = NULL;

  pthread_mutex_unlock (&catalog->lock);

  return catalog->ids;
}

static xine_ao_driver_t *_load_audio_driver (xine_t *this, plugin_node_t *node,
					     void *data) {

  xine_ao_driver_t *driver;

  if (!node->plugin_class)
    node->plugin_class = _load_plugin_class (this, node->filename, node->info, data);

  if (!node->plugin_class)
    return NULL;

  driver = ((audio_driver_class_t *)node->plugin_class)->open_plugin(node->plugin_class, data);

  if (driver) {
    driver->node = node;
    node->ref ++;
  } else {

    /* FIXME
    if (!node->ref)
    unload class
    */
  }

  return driver;
}

xine_ao_driver_t *xine_open_audio_driver (xine_t *this, const char *id,
					  void *data) {

  plugin_node_t      *node;
  xine_ao_driver_t   *driver;
  ao_info_t          *ao_info;
  plugin_catalog_t   *catalog = this->plugin_catalog;

  pthread_mutex_lock (&catalog->lock);

  driver = NULL;

  node = xine_list_first_content (this->plugin_catalog->aout);
  while (node) {

    ao_info = node->info->special_info;

    if (id) {
      if (!strcasecmp(node->info->id, id)) {
	driver = _load_audio_driver (this, node, data);
	break;
      }
    } else {
      driver = _load_audio_driver (this, node, data);
      if (driver) {

	xine_cfg_entry_t entry;

	/* remember plugin id */

	if (xine_config_lookup_entry (this, "audio.driver", &entry)) {
	  entry.str_value = node->info->id;
	  xine_config_update_entry (this, &entry);
	}

	break;
      }
    }

    node = xine_list_next_content (this->plugin_catalog->aout);
  }

  if (!driver) {
    if (id)
      printf ("load_plugins: failed to load audio output plugin <%s>\n", id);
    else
      printf ("load_plugins: audio output auto-probing didn't find any usable audio driver.\n");
  }

  pthread_mutex_unlock (&catalog->lock);

  return driver;
}

void xine_close_audio_driver (xine_t *this, xine_ao_driver_t  *driver) {

  /* FIXME : implement */

}
void xine_close_video_driver (xine_t *this, xine_vo_driver_t  *driver) {

  /* FIXME : implement */

}


/*
 * get autoplay mrl list from input plugin
 */

char **xine_get_autoplay_mrls (xine_t *this, const char *plugin_id,
			       int *num_mrls) {

  plugin_catalog_t   *catalog;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  node = xine_list_first_content (catalog->input);
  while (node) {

    if (!strcasecmp (node->info->id, plugin_id)) {

      input_class_t *ic = (input_class_t *) node->plugin_class;

      if (!ic->get_autoplay_list)
	return NULL;

      return ic->get_autoplay_list (ic, num_mrls);
    }
    node = xine_list_next_content (catalog->input);
  }
  return NULL;
}

/*
 * input plugin mrl browser support
 */
xine_mrl_t **xine_get_browse_mrls (xine_t *this, const char *plugin_id,
				   const char *start_mrl, int *num_mrls) {

  plugin_catalog_t   *catalog;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  node = xine_list_first_content (catalog->input);
  while (node) {

    if (!strcasecmp (node->info->id, plugin_id)) {

      input_class_t *ic = (input_class_t *) node->plugin_class;

      if (!ic->get_dir)
	return NULL;

      return ic->get_dir (ic, start_mrl, num_mrls);
    }
    node = xine_list_next_content (catalog->input);
  }
  return NULL;
}

video_decoder_t *get_video_decoder (xine_stream_t *stream, uint8_t stream_type) {

  plugin_node_t    *node;
  int               i, j;
  plugin_catalog_t *catalog = stream->xine->plugin_catalog;

#ifdef LOG
  printf ("load_plugins: looking for video decoder for streamtype %02x\n",
	  stream_type);
#endif

  pthread_mutex_lock (&catalog->lock);

  for (i = 0; i < PLUGINS_PER_TYPE; i++) {

    video_decoder_t *vd=NULL;

    node = catalog->video_decoder_map[stream_type][i];

    if (!node) {
      pthread_mutex_unlock (&catalog->lock);
      return NULL;
    }

    if (!node->plugin_class)
      node->plugin_class = _load_plugin_class (stream->xine, node->filename,
					       node->info, NULL);

    if (!node->plugin_class) {
      /* remove non working plugin from catalog */
      printf("load_plugins: plugin %s failed to init its class.\n", node->info->id);
      for (j = i + 1; j < PLUGINS_PER_TYPE; j++)
        catalog->video_decoder_map[stream_type][j - 1] =
          catalog->video_decoder_map[stream_type][j];
      catalog->video_decoder_map[stream_type][PLUGINS_PER_TYPE] = NULL;
      i--;
      continue;
    }

    vd = ((video_decoder_class_t *)node->plugin_class)->open_plugin(node->plugin_class, stream);

    if (vd) {
      vd->node = node;
      node->ref ++;
      pthread_mutex_unlock (&catalog->lock);
      return vd;
    } else {
      /* remove non working plugin from catalog */
      printf("load_plugins: plugin %s failed to instantiate itself.\n", node->info->id);
      for (j = i + 1; j < PLUGINS_PER_TYPE; j++)
        catalog->video_decoder_map[stream_type][j - 1] =
          catalog->video_decoder_map[stream_type][j];
      catalog->video_decoder_map[stream_type][PLUGINS_PER_TYPE] = NULL;
      i--;
    }
  }

  pthread_mutex_unlock (&catalog->lock);
  return NULL;
}

void free_video_decoder (xine_stream_t *stream, video_decoder_t *vd) {
  plugin_catalog_t *catalog = stream->xine->plugin_catalog;
  plugin_node_t    *node = vd->node;

  pthread_mutex_lock (&catalog->lock);

  vd->dispose (vd);

  node->ref--;
  /* FIXME: unload plugin if no-longer used */

  pthread_mutex_unlock (&catalog->lock);
}


audio_decoder_t *get_audio_decoder (xine_stream_t *stream, uint8_t stream_type) {

  plugin_node_t    *node;
  int               i, j;
  plugin_catalog_t *catalog = stream->xine->plugin_catalog;

#ifdef LOG
  printf ("load_plugins: looking for audio decoder for streamtype %02x\n",
	  stream_type);
#endif

  pthread_mutex_lock (&catalog->lock);

  for (i = 0; i < PLUGINS_PER_TYPE; i++) {

    audio_decoder_t *ad;

    node = catalog->audio_decoder_map[stream_type][i];

    if (!node) {
      pthread_mutex_unlock (&catalog->lock);
      return NULL;
    }

    if (!node->plugin_class)
      node->plugin_class = _load_plugin_class (stream->xine, node->filename,
					       node->info, NULL);

    if (!node->plugin_class) {
      /* remove non working plugin from catalog */
      printf("load_plugins: plugin %s failed to init its class.\n", node->info->id);
      for (j = i + 1; j < PLUGINS_PER_TYPE; j++)
        catalog->audio_decoder_map[stream_type][j - 1] =
          catalog->audio_decoder_map[stream_type][j];
      catalog->audio_decoder_map[stream_type][PLUGINS_PER_TYPE] = NULL;
      i--;
      continue;
    }

    ad = ((audio_decoder_class_t *)node->plugin_class)->open_plugin(node->plugin_class, stream);

    if (ad) {
      ad->node = node;
      node->ref ++;
      pthread_mutex_unlock (&catalog->lock);
      return ad;
    } else {
      /* remove non working plugin from catalog */
      printf("load_plugins: plugin %s failed to instantiate itself.\n", node->info->id);
      for (j = i + 1; j < PLUGINS_PER_TYPE; j++)
        catalog->audio_decoder_map[stream_type][j - 1] =
          catalog->audio_decoder_map[stream_type][j];
      catalog->audio_decoder_map[stream_type][PLUGINS_PER_TYPE] = NULL;
      i--;
    }
  }

  pthread_mutex_unlock (&catalog->lock);
  return NULL;
}

void free_audio_decoder (xine_stream_t *stream, audio_decoder_t *ad) {
  plugin_catalog_t *catalog = stream->xine->plugin_catalog;
  plugin_node_t    *node = ad->node;

  pthread_mutex_lock (&catalog->lock);

  ad->dispose (ad);

  node->ref--;
  /* FIXME: unload plugin if no-longer used */

  pthread_mutex_unlock (&catalog->lock);
}


spu_decoder_t *get_spu_decoder (xine_stream_t *stream, uint8_t stream_type) {

  plugin_node_t    *node;
  int               i, j;
  plugin_catalog_t *catalog = stream->xine->plugin_catalog;

#ifdef LOG
  printf ("load_plugins: looking for spu decoder for streamtype %02x\n",
	  stream_type);
#endif

  pthread_mutex_lock (&catalog->lock);

  for (i = 0; i < PLUGINS_PER_TYPE; i++) {
    spu_decoder_t *sd;

    node = catalog->spu_decoder_map[stream_type][i];

    if (!node) {
      pthread_mutex_unlock (&catalog->lock);
      return NULL;
    }

    if (!node->plugin_class)
      node->plugin_class = _load_plugin_class (stream->xine, node->filename,
					       node->info, NULL);

    if (!node->plugin_class) {
      /* remove non working plugin from catalog */
      printf("load_plugins: plugin %s failed to init its class.\n", node->info->id);
      for (j = i + 1; j < PLUGINS_PER_TYPE; j++)
        catalog->spu_decoder_map[stream_type][j - 1] =
          catalog->spu_decoder_map[stream_type][j];
      catalog->spu_decoder_map[stream_type][PLUGINS_PER_TYPE] = NULL;
      i--;
      continue;
    }

    sd = ((spu_decoder_class_t *)node->plugin_class)->open_plugin(node->plugin_class, stream);

    if (sd) {
      sd->node = node;
      node->ref ++;
      pthread_mutex_unlock (&catalog->lock);
      return sd;
    } else {
      /* remove non working plugin from catalog */
      printf("load_plugins: plugin %s failed to instantiate itself.\n", node->info->id);
      for (j = i + 1; j < PLUGINS_PER_TYPE; j++)
        catalog->spu_decoder_map[stream_type][j - 1] =
          catalog->spu_decoder_map[stream_type][j];
      catalog->spu_decoder_map[stream_type][PLUGINS_PER_TYPE] = NULL;
      i--;
    }
  }

  pthread_mutex_unlock (&catalog->lock);
  return NULL;
}

void free_spu_decoder (xine_stream_t *stream, spu_decoder_t *sd) {
  plugin_catalog_t *catalog = stream->xine->plugin_catalog;
  plugin_node_t    *node = sd->node;

  pthread_mutex_lock (&catalog->lock);

  sd->dispose (sd);

  node->ref--;
  /* FIXME: unload plugin if no-longer used */

  pthread_mutex_unlock (&catalog->lock);
}

/* get a list of file extensions for file types supported by xine
 * the list is separated by spaces 
 *
 * the pointer returned can be free()ed when no longer used */
char *xine_get_file_extensions (xine_t *self) {

  plugin_catalog_t *catalog = self->plugin_catalog;
  int               len, pos;
  plugin_node_t    *node;
  char             *str;

  pthread_mutex_lock (&catalog->lock);

  /* calc length of output */

  len = 0; node = xine_list_first_content (catalog->demux);
  while (node) {
    demux_class_t *cls = (demux_class_t *)node->plugin_class;

    len += strlen(cls->get_extensions (cls))+1;

    node = xine_list_next_content (catalog->demux);
  }

  /* create output */

  str = malloc (len+1);
  pos = 0;

  node = xine_list_first_content (catalog->demux);
  while (node) {
    demux_class_t *cls = (demux_class_t *)node->plugin_class;
    char *e;
    int l;

    e = cls->get_extensions (cls);
    l = strlen(e);
    memcpy (&str[pos], e, l);

    node = xine_list_next_content (catalog->demux);
    pos += l;
    str[pos] = ' ';
    pos++;
  }

  str[pos] = 0;
  
  pthread_mutex_unlock (&catalog->lock);

  return str;
}

/* get a list of mime types supported by xine
 *
 * the pointer returned can be free()ed when no longer used */
char *xine_get_mime_types (xine_t *self) {

  plugin_catalog_t *catalog = self->plugin_catalog;
  int               len, pos;
  plugin_node_t    *node;
  char             *str;

  pthread_mutex_lock (&catalog->lock);

  /* calc length of output */

  len = 0; node = xine_list_first_content (catalog->demux);
  while (node) {
    demux_class_t *cls = (demux_class_t *)node->plugin_class;
    char *s;

    s = cls->get_mimetypes (cls);
    if (s)
      len += strlen(s);

    node = xine_list_next_content (catalog->demux);
  }

  /* create output */

  str = malloc (len+1);
  pos = 0;

  node = xine_list_first_content (catalog->demux);
  while (node) {
    demux_class_t *cls = (demux_class_t *)node->plugin_class;
    char *s;
    int l;

    s = cls->get_mimetypes (cls);
    if (s) {
      l = strlen(s);
      memcpy (&str[pos], s, l);

      pos += l;
    }
    node = xine_list_next_content (catalog->demux);
  }

  str[pos] = 0;
  
  pthread_mutex_unlock (&catalog->lock);

  return str;
}


/*
 * dispose all currently loaded plugins (shutdown)
 */

void dispose_plugins (xine_t *this) {

/* FIXME */

#if 0
  plugin_node_t *node;

  node = xine_list_first_content (this->plugin_catalog->demux);
  while (node) {
    demux_plugin_t *dp = node->plugin;

    if (dp)
      dp->dispose (dp);

    node = xine_list_next_content (this->plugin_catalog->demux);
  }

  node = xine_list_first_content (this->plugin_catalog->input);
  while (node) {
    input_plugin_t *ip = node->plugin;

    if (ip)
      ip->dispose (ip);

    node = xine_list_next_content (this->plugin_catalog->input);
  }

  for (i = 0; i < this->num_audio_decoders_loaded; i++)
    this->audio_decoders_loaded[i]->dispose (this->audio_decoders_loaded[i]);

  for (i = 0; i < this->num_video_decoders_loaded; i++)
    this->video_decoders_loaded[i]->dispose (this->video_decoders_loaded[i]);

  for (i = 0; i < this->num_spu_decoders_loaded; i++)
    this->spu_decoders_loaded[i]->dispose (this->spu_decoders_loaded[i]);

#endif
}
