/*
 * Copyright (C) 2000-2003 the xine project
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
 * $Id: load_plugins.c,v 1.152 2003/05/20 14:55:55 mroi Exp $
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
#include <ctype.h>
#include <signal.h>

#include "xine_internal.h"
#include "xine_plugin.h"
#include "plugin_catalog.h"
#include "demuxers/demux.h"
#include "input/input_plugin.h"
#include "video_out.h"
#include "post.h"
#include "metronom.h"
#include "configfile.h"
#include "xineutils.h"
#include "compat.h"

/*
#define LOG
*/

static char *plugin_name;

#if 0
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
#endif /* 0 */

/*
 * plugin list/catalog management functions
 *
 */

static int _get_decoder_priority (xine_t *this, int default_priority,
				  char *id) {

  char str[80];
  int result;

  sprintf (str, "decoder.%s_priority", id);

  result = this->config->register_num (this->config,
				     str,
				     0,
				     "decoder's priority compared to others",
				     "The priority provides a ranking in case some media "
				     "can be handled by more than one decoder.\n"
				     "A priority of 0 enables the decoder's default priority.", 20,
				     NULL, NULL /*FIXME: implement callback*/);

  return result ? result : default_priority;
}


static plugin_info_t *_get_cached_plugin ( xine_list_t *list,
			    char *filename, struct stat *statbuffer,
			    plugin_info_t *previous_info){

  plugin_node_t *node;

  node = xine_list_first_content (list);
  while (node) {
    if( !strcmp( node->filename, filename ) &&
         node->filesize == statbuffer->st_size &&
         node->filemtime == statbuffer->st_mtime &&
         !previous_info ) {
      
      return node->info;
    }
    
    /* skip previously returned items */
    if( node->info == previous_info )
      previous_info = NULL;
    
    node = xine_list_next_content (list);
  }
  return NULL;
}


static void _insert_plugin (xine_t *this,
			    xine_list_t *list,
			    char *filename, struct stat *statbuffer,
			    plugin_info_t *info,
			    int api_version){

  plugin_node_t     *entry;
  vo_info_t         *vo_new, *vo_old;
  ao_info_t         *ao_new, *ao_old;
  decoder_info_t    *decoder_new, *decoder_old;
  post_info_t       *post_new, *post_old;
  uint32_t          *types;
  int                priority = 0;
  int                i;

  if (info->API != api_version) {
    if (this->verbosity >= XINE_VERBOSITY_DEBUG)
      printf ("load_plugins: ignoring plugin %s, wrong iface version %d (should be %d)\n",
	      info->id, info->API, api_version);
    return;
  }

  entry = xine_xmalloc(sizeof(plugin_node_t));
  entry->filename     = strdup(filename);
  entry->filesize     = statbuffer->st_size;
  entry->filemtime    = statbuffer->st_mtime;
  entry->info         = xine_xmalloc(sizeof(plugin_info_t));
  *(entry->info)      = *info;
  entry->info->id     = strdup(info->id);
  entry->info->init   = NULL;
  entry->plugin_class = NULL;
  entry->ref          = 0;

  switch (info->type & PLUGIN_TYPE_MASK){

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
    priority = decoder_old->priority;

    decoder_new->priority = _get_decoder_priority (this, priority, info->id);

    entry->info->special_info = decoder_new;
    break;
  
  case PLUGIN_POST:
    post_old = info->special_info;
    post_new = xine_xmalloc(sizeof(post_info_t));
    post_new->type = post_old->type;
    entry->info->special_info = post_new;
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
  catalog->post  = xine_list_new();

  catalog->cache = xine_list_new();
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
      plugin_info_t *info;
      
      struct stat statbuffer;

      str = xine_xmalloc(strlen(path) + strlen(pEntry->d_name) + 2);
      sprintf (str, "%s/%s", path, pEntry->d_name);

      if (stat(str, &statbuffer)) {
	xine_log (this, XINE_LOG_PLUGIN,
		  _("load_plugins: unable to stat %s\n"), str);
      } else {

	switch (statbuffer.st_mode & S_IFMT){

	case S_IFREG:
	  /* regular file, ie. plugin library, found => load it */

	  /* this will fail whereever shared libs are called *.dll or such
	   * better solutions:
           * a) don't install .la files on user's system
           * b) also cache negative hits, ie. files that failed to dlopen()
	   */
#if defined(__hpux)
	  if(!strstr(str, ".sl"))
#else
	  if(!strstr(str, ".so")) 
#endif
	    break;
	  
	  plugin_name = str;
	  lib = NULL;
	  info = _get_cached_plugin ( this->plugin_catalog->cache,
			              str, &statbuffer, NULL);
#ifdef LOG
	  if( info )
	    printf("load_plugins: using cached %s\n", str);
	  else
	    printf("load_plugins: %s not cached\n", str);
#endif

	  if(!info && !(lib = dlopen (str, RTLD_LAZY | RTLD_GLOBAL))) {

	    if (this->verbosity >= XINE_VERBOSITY_DEBUG) {
	      char *dl_error_msg = dlerror();
	      /* too noisy -- but good to catch unresolved references */
	      printf ("load_plugins: cannot open plugin lib %s:\n%s\n",
		      str, dl_error_msg);
	    }

	  } else {

	    if (info || (info = dlsym(lib, "xine_plugin_info"))) {
       
	      while ( info && info->type != PLUGIN_NONE ){

		xine_log (this, XINE_LOG_PLUGIN,
			  _("load_plugins: plugin %s found\n"), str);

		switch (info->type & PLUGIN_TYPE_MASK){
		case PLUGIN_INPUT:
		  _insert_plugin (this, this->plugin_catalog->input, str,
				  &statbuffer, info,
				  INPUT_PLUGIN_IFACE_VERSION);
		  break;
		case PLUGIN_DEMUX:
		  _insert_plugin (this, this->plugin_catalog->demux, str,
				  &statbuffer, info,
				  DEMUXER_PLUGIN_IFACE_VERSION);
		  break;
		case PLUGIN_AUDIO_DECODER:
		  _insert_plugin (this, this->plugin_catalog->audio, str,
				  &statbuffer, info,
				  AUDIO_DECODER_IFACE_VERSION);
		  break;
		case PLUGIN_VIDEO_DECODER:
		  _insert_plugin (this, this->plugin_catalog->video, str,
				  &statbuffer, info,
				  VIDEO_DECODER_IFACE_VERSION);
		  break;
		case PLUGIN_SPU_DECODER:
		  _insert_plugin (this, this->plugin_catalog->spu, str,
				  &statbuffer, info,
				  SPU_DECODER_IFACE_VERSION);
		  break;
		case PLUGIN_AUDIO_OUT:
		  _insert_plugin (this, this->plugin_catalog->aout, str,
				  &statbuffer, info,
				  AUDIO_OUT_IFACE_VERSION);
		  break;
		case PLUGIN_VIDEO_OUT:
		  _insert_plugin (this, this->plugin_catalog->vout, str,
				  &statbuffer, info,
				  VIDEO_OUT_DRIVER_IFACE_VERSION);
		  break;
		case PLUGIN_POST:
		  _insert_plugin (this, this->plugin_catalog->post, str,
    				  &statbuffer, info,
				  POST_PLUGIN_IFACE_VERSION);
		  break;
		default:
		  xine_log (this, XINE_LOG_PLUGIN,
			    _("load_plugins: unknown plugin type %d in %s\n"),
			    info->type, str);
		}

		/* get next info either from lib or cache */
		if( lib ) {
		  info++;
		}
		else {
		  info = _get_cached_plugin ( this->plugin_catalog->cache,
			                      str, &statbuffer, info);
		}
	      }
	    }
	    else {
	      char *dl_error_msg = dlerror();

	      xine_log (this, XINE_LOG_PLUGIN,
			_("load_plugins: can't get plugin info from %s:\n%s\n"),
			str, dl_error_msg);
	    }
	    if( lib )
	      dlclose(lib);
	  }
	  break;
	case S_IFDIR:

	  /* unless ".", "..", ".hidden" or vidix driver dirs */
	  if (*pEntry->d_name != '.' && strcmp(pEntry->d_name, "vidix")) {
	    collect_plugins(this, str);
	  }
	} /* switch */
      } /* if (stat(...)) */
      free(str);
    } /* while */
    closedir (dir);
  } /* if (dir) */
  else {
    xine_log (this, XINE_LOG_PLUGIN,
	      _("load_plugins: skipping unreadable plugin directory %s.\n"),
	      path);
  }
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
 *  load plugins that asked to be initialized
 */
static void _load_required_plugins(xine_t *this, xine_list_t *list) {

  plugin_node_t *node;
  int load;

  node = xine_list_first_content (list);
  while (node) {
    
    if( (node->info->type & PLUGIN_TYPE_MASK) == PLUGIN_INPUT ||
        (node->info->type & PLUGIN_TYPE_MASK) == PLUGIN_DEMUX ||
        (node->info->type & PLUGIN_MUST_PRELOAD) )
      load = 1;
    else
      load = 0;
    
    if( load && !node->plugin_class ) {
#ifdef LOG
      printf("load_plugins: preload plugin %s from %s\n",
	     node->info->id, node->filename);
#endif

      node->plugin_class = _load_plugin_class (this, node->filename, node->info, NULL);
        
      /* in case of failure remove from list */
      if( !node->plugin_class ) {

        xine_list_delete_current (list);
        node = list->cur->content;

      } else
	node = xine_list_next_content (list);
    } else
      node = xine_list_next_content (list);
  }
}

static void load_required_plugins(xine_t *this) {

  _load_required_plugins (this, this->plugin_catalog->input);
  _load_required_plugins (this, this->plugin_catalog->demux);
  _load_required_plugins (this, this->plugin_catalog->spu);
  _load_required_plugins (this, this->plugin_catalog->audio);
  _load_required_plugins (this, this->plugin_catalog->video);
  _load_required_plugins (this, this->plugin_catalog->aout);
  _load_required_plugins (this, this->plugin_catalog->vout);
  _load_required_plugins (this, this->plugin_catalog->post);
}



/*
 *  save plugin list information to file (cached catalog)
 */
static void save_plugin_list(FILE *fp, xine_list_t *plugins) {

  plugin_node_t *node;
  decoder_info_t *decoder_info;
  vo_info_t *vo_info;
  ao_info_t *ao_info;
  post_info_t *post_info;
  
  int i;

  node = xine_list_first_content (plugins);
  while (node) {

    fprintf(fp, "[%s]\n", node->filename );

#ifndef _MSC_VER
    fprintf(fp, "size=%llu\n", (unsigned long long) node->filesize );
    fprintf(fp, "mtime=%llu\n", (unsigned long long) node->filemtime );
#else
    fprintf(fp, "size=%llu\n", (uint64_t) node->filesize );
    fprintf(fp, "mtime=%llu\n", (uint64_t) node->filemtime );
#endif /* _MSC_VER  */
    
    fprintf(fp, "type=%d\n", node->info->type );
    fprintf(fp, "api=%d\n", node->info->API );
    fprintf(fp, "id=%s\n", node->info->id );
    fprintf(fp, "version=%lu\n", (unsigned long) node->info->version );
  
    switch (node->info->type & PLUGIN_TYPE_MASK){
    
      case PLUGIN_VIDEO_OUT:
        vo_info = node->info->special_info;
        fprintf(fp, "visual_type=%d\n", vo_info->visual_type );
        fprintf(fp, "vo_priority=%d\n", vo_info->priority );
        break;
    
      case PLUGIN_AUDIO_OUT:
        ao_info = node->info->special_info;
        fprintf(fp, "ao_priority=%d\n", ao_info->priority );
        break;
    
      case PLUGIN_AUDIO_DECODER:
      case PLUGIN_VIDEO_DECODER:
      case PLUGIN_SPU_DECODER:
        decoder_info = node->info->special_info;
        fprintf(fp, "supported_types=");
        for (i=0; decoder_info->supported_types[i] != 0; ++i){
          fprintf(fp, "%lu ", (unsigned long) decoder_info->supported_types[i]);
        }
        fprintf(fp, "\n");
        fprintf(fp, "decoder_priority=%d\n", decoder_info->priority );
        break;
      
      case PLUGIN_POST:
        post_info = node->info->special_info;
	fprintf(fp, "post_type=%lu\n", (unsigned long)post_info->type);
	break;
    }        
    
    fprintf(fp, "\n");
    node = xine_list_next_content (plugins);
  }
}

/*
 *  load plugin list information from file (cached catalog)
 */
static void load_plugin_list(FILE *fp, xine_list_t *plugins) {

  plugin_node_t *node;
  decoder_info_t *decoder_info = NULL;
  vo_info_t *vo_info = NULL;
  ao_info_t *ao_info = NULL;
  post_info_t *post_info = NULL;
  int i;

#ifndef _MSC_VER
  unsigned long long llu;
#else
  uint64_t llu;
#endif /* _MSC_VER */

  unsigned long lu;
  char line[1024];
  char *value;
  int version_ok = 0;
  
  node = NULL;
  while (fgets (line, 1023, fp)) {
    if (line[0] == '#')
      continue;

    if (line[0] == '[' && version_ok) {
      if((value = strchr (line, ']')))
        *value = (char) 0;
      
      if( node ) {
        xine_list_append_content (plugins, node);
      }
      line[strlen(line)-1]= (char) 0; /* eliminate lf */
      node                = xine_xmalloc(sizeof(plugin_node_t));
      node->filename      = strdup(line+1);
      node->info          = xine_xmalloc(2*sizeof(plugin_info_t));
      node->info[1].type  = PLUGIN_NONE;
      decoder_info        = NULL;
      vo_info             = NULL;
      ao_info             = NULL;
      post_info           = NULL;
    }

    if ((value = strchr (line, '='))) {

      *value = (char) 0;
      value++;

      if( !version_ok ) {
        if( !strcmp("cache_catalog_version",line) ) {
          sscanf(value," %d",&i);
          if( i == CACHE_CATALOG_VERSION )
            version_ok = 1;
          else
            return;  
        }
      } else if (node) {
        if( !strcmp("size",line) ) {
          sscanf(value," %llu",&llu);
          node->filesize = (off_t) llu;
        } else if( !strcmp("mtime",line) ) {
          sscanf(value," %llu",&llu);
          node->filemtime = (time_t) llu;
        } else if( !strcmp("type",line) ) {
          sscanf(value," %d",&i);
          node->info->type = i;
          
          switch (node->info->type & PLUGIN_TYPE_MASK){
          
            case PLUGIN_VIDEO_OUT:
              vo_info = node->info->special_info =
                        xine_xmalloc(sizeof(vo_info_t));
              break;
          
            case PLUGIN_AUDIO_OUT:
              ao_info = node->info->special_info = 
                        xine_xmalloc(sizeof(ao_info_t));
              break;
          
            case PLUGIN_AUDIO_DECODER:
            case PLUGIN_VIDEO_DECODER:
            case PLUGIN_SPU_DECODER:
              decoder_info = node->info->special_info =
                             xine_xmalloc(sizeof(decoder_info_t));
              break;
	    
	    case PLUGIN_POST:
	      post_info = node->info->special_info =
			  xine_xmalloc(sizeof(post_info_t));
	      break;
          }        
          
        } else if( !strcmp("api",line) ) {
          sscanf(value," %d",&i);
          node->info->API = i;
        } else if( !strcmp("id",line) ) {
          node->info->id = strdup(value);
        } else if( !strcmp("version",line) ) {
          sscanf(value," %lu",&lu);
          node->info->version = lu;
        } else if( !strcmp("visual_type",line) && vo_info ) {
          sscanf(value," %d",&i);
          vo_info->visual_type = i;
        } else if( !strcmp("supported_types",line) && decoder_info ) {
          char *s;
          
          for( s = value, i = 0; s && sscanf(s," %lu",&lu) > 0; i++ ) {
            s = strchr(s+1, ' ');
          }
          decoder_info->supported_types = xine_xmalloc((i+1)*sizeof(uint32_t));
          for( s = value, i = 0; s && sscanf(s," %lu",&lu) > 0; i++ ) {
            decoder_info->supported_types[i] = lu;
            s = strchr(s+1, ' ');
          }
        } else if( !strcmp("vo_priority",line) && vo_info ) {
          sscanf(value," %d",&i);
          vo_info->priority = i;
        } else if( !strcmp("ao_priority",line) && ao_info ) {
          sscanf(value," %d",&i);
          ao_info->priority = i;
        } else if( !strcmp("decoder_priority",line) && decoder_info ) {
          sscanf(value," %d",&i);
          decoder_info->priority = i;
        } else if( !strcmp("post_type",line) && post_info ) {
	  sscanf(value," %lu",&lu);
	  post_info->type = lu;
        }
      }
    }
  }
      
  if( node ) {
    xine_list_append_content (plugins, node);
  }
}


/*
 * save catalog to cache file
 */
static void save_catalog (xine_t *this) {

  FILE       *fp;
  char       *cachefile, *dirfile; 
  const char *relname = CACHE_CATALOG_FILE;
  const char *dirname = CACHE_CATALOG_DIR;
    
  cachefile = (char *) xine_xmalloc(strlen(xine_get_homedir()) + 
                                    strlen(relname) + 3);
  sprintf(cachefile, "%s/%s", xine_get_homedir(), relname);
  
  /* make sure homedir (~/.xine) exists */
  dirfile = (char *) xine_xmalloc(strlen(xine_get_homedir()) + 
				  strlen(dirname) + 3);
  sprintf(dirfile, "%s/%s", xine_get_homedir(), dirname);
  mkdir (dirfile, 0755);
  free (dirfile);

  if( (fp = fopen(cachefile,"w")) != NULL ) {
  
    fprintf(fp, "# this file is automatically created by xine, do not edit.\n\n");
    fprintf(fp, "cache_catalog_version=%d\n\n", CACHE_CATALOG_VERSION);
    
    save_plugin_list (fp, this->plugin_catalog->input);
    save_plugin_list (fp, this->plugin_catalog->demux);
    save_plugin_list (fp, this->plugin_catalog->spu);
    save_plugin_list (fp, this->plugin_catalog->audio);
    save_plugin_list (fp, this->plugin_catalog->video);
    save_plugin_list (fp, this->plugin_catalog->aout);
    save_plugin_list (fp, this->plugin_catalog->vout);
    save_plugin_list (fp, this->plugin_catalog->post);
    fclose(fp);
  }
  free(cachefile);
}

/*
 * load cached catalog from file
 */
static void load_cached_catalog (xine_t *this) {

  FILE *fp;
  char *cachefile;                                               
  const char *relname = CACHE_CATALOG_FILE;
    
  cachefile = (char *) xine_xmalloc(strlen(xine_get_homedir()) + 
                                    strlen(relname) + 3);
  sprintf(cachefile, "%s/%s", xine_get_homedir(), relname);
  
  if( (fp = fopen(cachefile,"r")) != NULL ) {
    load_plugin_list (fp, this->plugin_catalog->cache);
    fclose(fp);
  }
  free(cachefile);
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
  
  const char *homedir;
  char *plugindir;
  char *pluginpath;
  int i,j;
  int lenpluginpath;
  
#ifdef LOG
  printf("load_plugins: scan_plugins()\n");
#endif

/* TODO - This needs to be fixed for WIN32 */
#ifndef WIN32
  if (this == NULL || this->config == NULL) {
    fprintf(stderr, "%s(%s@%d): parameter should be non null, exiting\n",
	    __FILE__, __XINE_FUNCTION__, __LINE__);
    abort();
  }
#endif

  homedir = xine_get_homedir();
  this->plugin_catalog = _new_catalog();
  load_cached_catalog (this);

  if ( !(pluginpath = getenv("XINE_PLUGIN_PATH")) ){
#ifndef _MSC_VER
    pluginpath = "~/.xine/plugins:" XINE_PLUGINDIR;
#else
	pluginpath = XINE_PLUGINDIR;
#endif
  }
  plugindir = xine_xmalloc(strlen(pluginpath)+strlen(homedir)+2);
  j=0;
  lenpluginpath = strlen(pluginpath);
  for (i=0; i <= lenpluginpath; ++i){
    switch (pluginpath[i]){
    case ':':
    case '\0':
      plugindir[j] = '\0';
      collect_plugins(this, plugindir);
      j = 0;
      break;
    case '~':
      if (j == 0){
	strcpy(plugindir, homedir);
	j = strlen(plugindir);
	break;
      }
    default:
      plugindir[j++] = pluginpath[i];
    }
  }
  free(plugindir);

  save_catalog (this);
    
  load_required_plugins (this);

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

    if ((plugin = ((input_class_t *)node->plugin_class)->get_instance(node->plugin_class, stream, mrl))) {
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

      if (stream->xine->verbosity >= XINE_VERBOSITY_DEBUG) 
	printf ("load_plugins: probing demux '%s'\n", node->info->id);

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

demux_plugin_t *find_demux_plugin_by_name(xine_stream_t *stream, const char *name, input_plugin_t *input) {

  plugin_catalog_t  *catalog = stream->xine->plugin_catalog;
  plugin_node_t     *node;
  demux_plugin_t    *plugin;

  pthread_mutex_lock(&catalog->lock);
  node = xine_list_first_content(catalog->demux);
  stream->content_detection_method = METHOD_EXPLICIT;

  while (node) {
    if (strcasecmp(node->info->id, name) == 0) {
      if ((plugin = ((demux_class_t *)node->plugin_class)->open_plugin(node->plugin_class, stream, input))) {
	pthread_mutex_unlock (&catalog->lock);
	return plugin;
      }
    }
    node = xine_list_next_content(catalog->demux);
  }

  pthread_mutex_unlock(&catalog->lock);
  return NULL;
}

/*
 * this is a special test mode for content detection: all demuxers are probed
 * by content and extension except last_demux_name which is tested after
 * every other demuxer.
 *
 * this way we can make sure no demuxer will interfere on probing of a
 * known stream.
 */

demux_plugin_t *find_demux_plugin_last_probe(xine_stream_t *stream, const char *last_demux_name, input_plugin_t *input) {

  int               i;
  int               methods[3];
  xine_t           *xine = stream->xine;
  plugin_catalog_t *catalog = xine->plugin_catalog;
  plugin_node_t    *last_demux = NULL;
  demux_plugin_t   *plugin;

  methods[0] = METHOD_BY_CONTENT;
  methods[1] = METHOD_BY_EXTENSION;
  methods[2] = -1;

  i = 0;
  while (methods[i] != -1) {

    plugin_node_t *node;

    stream->content_detection_method = methods[i];

    pthread_mutex_lock (&catalog->lock);

    node = xine_list_first_content (catalog->demux);

    while (node) {

#ifdef LOG
      printf ("load_plugins: probing demux '%s'\n", node->info->id);
#endif
      if (strcasecmp(node->info->id, last_demux_name) == 0) {
        last_demux = node;
      } else {
	printf("load_plugin: probing '%s' (method %d)...\n", node->info->id, stream->content_detection_method );
        if ((plugin = ((demux_class_t *)node->plugin_class)->open_plugin(node->plugin_class, stream, input))) {
	  printf ("load_plugins: using demuxer '%s' (instead of '%s')\n", node->info->id, last_demux_name);
	  pthread_mutex_unlock (&catalog->lock);
	  return plugin;
        }
      }

      node = xine_list_next_content (stream->xine->plugin_catalog->demux);
    }

    pthread_mutex_unlock (&catalog->lock);

    i++;
  }

  if( !last_demux )
    return NULL;

  stream->content_detection_method = METHOD_BY_CONTENT;

  if ((plugin = ((demux_class_t *)last_demux->plugin_class)->open_plugin(last_demux->plugin_class, stream, input))) {
    printf ("load_plugins: using demuxer '%s'\n", last_demux_name);
    return plugin;
  }

  return NULL;
}


const char *const *xine_get_autoplay_input_plugin_ids(xine_t *this) {

  plugin_catalog_t   *catalog;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  pthread_mutex_lock (&catalog->lock);

  catalog->ids[0] = NULL;
  node = xine_list_first_content (catalog->input);
  while (node) {
    input_class_t *ic;

    ic = (input_class_t *) node->plugin_class;
    if (ic->get_autoplay_list) {
      int i = 0, j;

      while (catalog->ids[i] && strcmp(catalog->ids[i], node->info->id) < 0)
        i++;
      for (j = PLUGIN_MAX - 1; j > i; j--)
        catalog->ids[j] = catalog->ids[j - 1];

      catalog->ids[i] = node->info->id;
    }
    node = xine_list_next_content (catalog->input);
  }

  pthread_mutex_unlock (&catalog->lock);

  return catalog->ids;
}

const char *const *xine_get_browsable_input_plugin_ids(xine_t *this) {


  plugin_catalog_t   *catalog;
  plugin_node_t      *node;

  catalog = this->plugin_catalog;

  pthread_mutex_lock (&catalog->lock);

  catalog->ids[0] = NULL;
  node = xine_list_first_content (catalog->input);
  while (node) {
    input_class_t *ic;

    ic = (input_class_t *) node->plugin_class;
    if (ic->get_dir) {
      int i = 0, j;

      while (catalog->ids[i] && strcmp(catalog->ids[i], node->info->id) < 0)
        i++;
      for (j = PLUGIN_MAX - 1; j > i; j--)
        catalog->ids[j] = catalog->ids[j - 1];

      catalog->ids[i] = node->info->id;
    }
    node = xine_list_next_content (catalog->input);
  }

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

static vo_driver_t *_load_video_driver (xine_t *this, plugin_node_t *node,
					void *data) {

  vo_driver_t *driver;

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

xine_video_port_t *xine_open_video_driver (xine_t *this,
					   const char *id,
					   int visual_type, void *visual) {

  plugin_node_t      *node;
  vo_driver_t        *driver;
  xine_video_port_t  *port;
  vo_info_t          *vo_info;
  plugin_catalog_t   *catalog = this->plugin_catalog;

  driver = NULL;

  if (id && !strcasecmp(id, "auto"))
    id = NULL;
  
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

	  break;
	}
      }
    }

    node = xine_list_next_content (catalog->vout);
  }

  pthread_mutex_unlock (&catalog->lock);

  if (!driver) {
    printf ("load_plugins: failed to load video output plugin <%s>\n", id);
    return NULL;
  }

  port = vo_new_port(this, driver, 0);
  
  return port;
}

xine_video_port_t *xine_new_framegrab_video_port (xine_t *this) {

  plugin_node_t      *node;
  vo_driver_t        *driver;
  xine_video_port_t  *port;
  vo_info_t          *vo_info;
  plugin_catalog_t   *catalog = this->plugin_catalog;
  char               *id;

  driver = NULL;
  id     = "none";

  pthread_mutex_lock (&catalog->lock);

  node = xine_list_first_content (catalog->vout);
  while (node) {

    vo_info = node->info->special_info;
    if (!strcasecmp (node->info->id, id)) {
      driver = _load_video_driver (this, node, NULL);
      break;
    }
    node = xine_list_next_content (catalog->vout);
  }

  pthread_mutex_unlock (&catalog->lock);

  if (!driver) {
    printf ("load_plugins: failed to load video output plugin <%s>\n", id);
    return NULL;
  }

  port = vo_new_port(this, driver, 1);
  
  return port;
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

static ao_driver_t *_load_audio_driver (xine_t *this, plugin_node_t *node,
					void *data) {

  ao_driver_t *driver;

  if (!node->plugin_class)
    node->plugin_class = _load_plugin_class (this, node->filename, node->info, data);

  if (!node->plugin_class) {
    printf ("load_plugins: failed to load plugin class %s\n", node->info->id);
    return NULL;
  }

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

xine_audio_port_t *xine_open_audio_driver (xine_t *this, const char *id,
					   void *data) {

  plugin_node_t      *node;
  ao_driver_t        *driver;
  xine_audio_port_t  *port;
  ao_info_t          *ao_info;
  plugin_catalog_t   *catalog = this->plugin_catalog;

  if (id && !strcasecmp(id, "auto") )
    id = NULL;
  
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

	break;
      }
    }

    node = xine_list_next_content (this->plugin_catalog->aout);
  }

  pthread_mutex_unlock (&catalog->lock);
  
  if (!driver) {
    if (id)
      printf ("load_plugins: failed to load audio output plugin <%s>\n", id);
    else
      printf ("load_plugins: audio output auto-probing didn't find any usable audio driver.\n");
    return NULL;
  }

  port = ao_new_port(this, driver, 0);

  return port;
}

xine_audio_port_t *xine_new_framegrab_audio_port (xine_t *this) {

  xine_audio_port_t  *port;

  port = ao_new_port (this, NULL, 1);

  return port;
}

void xine_close_audio_driver (xine_t *this, xine_audio_port_t  *ao_port) {

  ao_port->exit(ao_port);

}
void xine_close_video_driver (xine_t *this, xine_video_port_t  *vo_port) {

  vo_port->exit(vo_port);

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
      if (stream->xine->verbosity >= XINE_VERBOSITY_DEBUG) 
	printf("load_plugins: plugin %s failed to init its class.\n", 
	       node->info->id);
      for (j = i + 1; j < PLUGINS_PER_TYPE; j++)
        catalog->video_decoder_map[stream_type][j - 1] =
          catalog->video_decoder_map[stream_type][j];
      catalog->video_decoder_map[stream_type][PLUGINS_PER_TYPE-1] = NULL;
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
      if (stream->xine->verbosity >= XINE_VERBOSITY_DEBUG) 
	printf("load_plugins: plugin %s failed to instantiate itself.\n", node->info->id);
      for (j = i + 1; j < PLUGINS_PER_TYPE; j++)
        catalog->video_decoder_map[stream_type][j - 1] =
          catalog->video_decoder_map[stream_type][j];
      catalog->video_decoder_map[stream_type][PLUGINS_PER_TYPE-1] = NULL;
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
      catalog->audio_decoder_map[stream_type][PLUGINS_PER_TYPE-1] = NULL;
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
      catalog->audio_decoder_map[stream_type][PLUGINS_PER_TYPE-1] = NULL;
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
      catalog->spu_decoder_map[stream_type][PLUGINS_PER_TYPE-1] = NULL;
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
      catalog->spu_decoder_map[stream_type][PLUGINS_PER_TYPE-1] = NULL;
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

const char *const *xine_list_post_plugins(xine_t *xine) {
  plugin_catalog_t *catalog = xine->plugin_catalog;
  plugin_node_t    *node;
  int               i;
  
  pthread_mutex_lock (&catalog->lock);

  i = 0;
  node = xine_list_first_content (catalog->post);
  while (node) {
    catalog->ids[i] = node->info->id;
    i++;
    node = xine_list_next_content (catalog->post);
  }
  catalog->ids[i] = NULL;

  pthread_mutex_unlock (&catalog->lock);
  return catalog->ids;
}

const char *const *xine_list_post_plugins_typed(xine_t *xine, int type) {
  plugin_catalog_t *catalog = xine->plugin_catalog;
  plugin_node_t    *node;
  int               i;
  
  pthread_mutex_lock (&catalog->lock);

  i = 0;
  node = xine_list_first_content (catalog->post);
  while (node) {
    if (((post_info_t *)node->info->special_info)->type == type)
      catalog->ids[i++] = node->info->id;
    node = xine_list_next_content (catalog->post);
  }
  catalog->ids[i] = NULL;

  pthread_mutex_unlock (&catalog->lock);
  return catalog->ids;
}

xine_post_t *xine_post_init(xine_t *xine, const char *name, int inputs,
			    xine_audio_port_t **audio_target,
			    xine_video_port_t **video_target) {
  plugin_catalog_t *catalog = xine->plugin_catalog;
  plugin_node_t    *node;
  
  if( !name )
    return NULL;
  
  pthread_mutex_lock(&catalog->lock);
  
  node = xine_list_first_content(catalog->post);
  while (node) {
    
    if (strcmp(node->info->id, name) == 0) {
      post_plugin_t *post;
      
      if (!node->plugin_class)
        node->plugin_class = _load_plugin_class(xine, node->filename,
						node->info, NULL);
      if (!node->plugin_class) {
        printf("load_plugins: requested post plugin %s failed to load\n", name);
	pthread_mutex_unlock(&catalog->lock);
	return NULL;
      }
      
      post = ((post_class_t *)node->plugin_class)->open_plugin(node->plugin_class,
        inputs, audio_target, video_target);

      if (post) {
        xine_post_in_t  *input;
	xine_post_out_t *output;
	int i;
	
        post->node = node;
        node->ref++;
	pthread_mutex_unlock(&catalog->lock);
	
	/* init the lists of announced connections */
	i = 0;
	input = xine_list_first_content(post->input);
	while (input) {
	  i++;
	  input = xine_list_next_content(post->input);
	}
	post->input_ids = malloc(sizeof(char *) * (i + 1));
	i = 0;
	input = xine_list_first_content(post->input);
	while (input)  {
	  post->input_ids[i++] = input->name;
	  input = xine_list_next_content(post->input);
	}
	post->input_ids[i] = NULL;
	
	i = 0;
	output = xine_list_first_content(post->output);
	while (output) {
	  i++;
	  output = xine_list_next_content(post->output);
	}
	post->output_ids = malloc(sizeof(char *) * (i + 1));
	i = 0;
	output = xine_list_first_content(post->output);
	while (output)  {
	  post->output_ids[i++] = output->name;
	  output = xine_list_next_content(post->output);
	}
	post->output_ids[i] = NULL;
	
	/* copy the post plugin type to the public part */
	post->xine_post.type = ((post_info_t *)node->info->special_info)->type;
	
	return &post->xine_post;
      } else {
        printf("load_plugins: post plugin %s failed to instantiate itself\n", name);
	pthread_mutex_unlock(&catalog->lock);
	return NULL;
      }
    }
    
    node = xine_list_next_content(catalog->post);
  }
  
  pthread_mutex_unlock(&catalog->lock);
  
  printf("load_plugins: no post plugin named %s found\n", name);
  return NULL;
}

void xine_post_dispose(xine_t *xine, xine_post_t *post_gen) {
  post_plugin_t *post = (post_plugin_t *)post_gen;
  plugin_node_t *node = post->node;
  
  pthread_mutex_lock(&xine->plugin_catalog->lock);
  free(post->input_ids);
  free(post->output_ids);
  post->dispose(post);
  node->ref--;
  pthread_mutex_unlock(&xine->plugin_catalog->lock);
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

  len = 0; 
  node = xine_list_first_content (catalog->demux);
  while (node) {
    demux_class_t *cls = (demux_class_t *)node->plugin_class;
    char          *exts;

    if((exts = cls->get_extensions(cls)) != NULL)
      len += strlen(exts) + 1;

    node = xine_list_next_content (catalog->demux);
  }

  /* create output */
  str = malloc (len); /* '\0' space is already counted in the previous loop */
  pos = 0;

  node = xine_list_first_content (catalog->demux);
  while (node) {
    demux_class_t *cls = (demux_class_t *)node->plugin_class;
    char          *e;
    int            l;

    if((e = cls->get_extensions (cls)) != NULL) {
      l = strlen(e);
      memcpy (&str[pos], e, l);
      
      pos += l;

      /* Don't add ' ' char at the end of str */
      if((pos + 1) < len) {
	str[pos] = ' ';
	pos++;
      }

    }
    
    node = xine_list_next_content (catalog->demux);
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


/* get the demuxer identifier that handles a given mime type
 *
 * the pointer returned can be free()ed when no longer used
 * returns NULL if no demuxer is available to handle this. */
char *xine_get_demux_for_mime_type (xine_t *self, const char *mime_type) {

  plugin_catalog_t *catalog = self->plugin_catalog;
  plugin_node_t    *node;
  char             *id = NULL;
  char             *mime_arg, *mime_demux;
  char             *s;

  /* create a copy and convert to lower case */  
  mime_arg = strdup(mime_type);
  for(s=mime_arg; *s; s++)
    *s = tolower(*s);
  
  pthread_mutex_lock (&catalog->lock);

  node = xine_list_first_content (catalog->demux);
  while (node && !id) {
    demux_class_t *cls = (demux_class_t *)node->plugin_class;

    s = cls->get_mimetypes (cls);
    if (s) {
      mime_demux = strdup(s);
      
      for(s=mime_demux; *s; s++)
        *s = tolower(*s);
      
      if( strstr(mime_demux, mime_arg) )
        id = strdup(node->info->id);
      
      free(mime_demux);
    }
    node = xine_list_next_content (catalog->demux);
  }

  pthread_mutex_unlock (&catalog->lock);

  free(mime_arg);
  
  return id;
}


static void dispose_plugin_list (xine_list_t *list) {

  plugin_node_t *node;
  decoder_info_t *decoder_info;

  if(list) {
    
    node = xine_list_first_content (list);
    while (node) {
      if (node->plugin_class) {
	void *cls = node->plugin_class;
	
	/* dispose of plugin class */
	switch (node->info->type & PLUGIN_TYPE_MASK) {
	case PLUGIN_INPUT:
	  ((input_class_t *)cls)->dispose ((input_class_t *)cls);
	  break;
	case PLUGIN_DEMUX:
	  ((demux_class_t *)cls)->dispose ((demux_class_t *)cls);
	  break;
	case PLUGIN_SPU_DECODER:
	  ((spu_decoder_class_t *)cls)->dispose ((spu_decoder_class_t *)cls);
	  break;
	case PLUGIN_AUDIO_DECODER:
	  ((audio_decoder_class_t *)cls)->dispose ((audio_decoder_class_t *)cls);
	  break;
	case PLUGIN_VIDEO_DECODER:
	  ((video_decoder_class_t *)cls)->dispose ((video_decoder_class_t *)cls);
	  break;
	case PLUGIN_AUDIO_OUT:
	  ((audio_driver_class_t *)cls)->dispose ((audio_driver_class_t *)cls);
	  break;
	case PLUGIN_VIDEO_OUT:
	  ((video_driver_class_t *)cls)->dispose ((video_driver_class_t *)cls);
	  break;
	case PLUGIN_POST:
	  ((post_class_t *)cls)->dispose ((post_class_t *)cls);
	  break;
	}
      }
      
      /* free special info */
      switch (node->info->type & PLUGIN_TYPE_MASK) {
      case PLUGIN_SPU_DECODER:
      case PLUGIN_AUDIO_DECODER:
      case PLUGIN_VIDEO_DECODER:
	decoder_info = (decoder_info_t *)node->info->special_info;
	free (decoder_info->supported_types);
	
      default:
	free (node->info->special_info);
	break;
      }
      
      /* free info structure and string copies */
      free (node->info->id);
      free (node->info);
      free (node->filename);
      free (node);
      
      node = xine_list_next_content (list);
    }
    xine_list_free(list);
  }
}


/*
 * dispose all currently loaded plugins (shutdown)
 */

void dispose_plugins (xine_t *this) {

  if(this->plugin_catalog) {
    dispose_plugin_list (this->plugin_catalog->input);
    dispose_plugin_list (this->plugin_catalog->demux);
    dispose_plugin_list (this->plugin_catalog->spu);
    dispose_plugin_list (this->plugin_catalog->audio);
    dispose_plugin_list (this->plugin_catalog->video);
    dispose_plugin_list (this->plugin_catalog->aout);
    dispose_plugin_list (this->plugin_catalog->vout);
    dispose_plugin_list (this->plugin_catalog->post);
    
    dispose_plugin_list (this->plugin_catalog->cache);
    free (this->plugin_catalog);
  }
}
