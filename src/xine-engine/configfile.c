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
 * $Id: configfile.c,v 1.33 2002/09/18 14:31:39 mroi Exp $
 *
 * config object (was: file) management - implementation
 *
 */
 
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "configfile.h"
#include "xineutils.h"
#include "xine_internal.h"

/* 
#define LOG
*/


/* 
 * internal utility functions
 */

static char *copy_string (const char *str) {

  char *cpy;
  int   len;

  len = strlen (str);

  cpy = xine_xmalloc (len+256);

  strncpy (cpy, str, len);

  return cpy;
}

static cfg_entry_t *xine_config_add (config_values_t *this, const char *key) {

  cfg_entry_t *entry;

  entry = (cfg_entry_t *) xine_xmalloc (sizeof (cfg_entry_t));
  entry->config        = this;
  entry->key           = copy_string (key);
  entry->type          = CONFIG_TYPE_UNKNOWN;
  entry->unknown_value = NULL;
  entry->str_sticky    = NULL;
  entry->str_value     = NULL;

  entry->next          = NULL;

  if (this->last) 
    this->last->next = entry;
  else
    this->first = entry;
  
  this->last = entry;

#ifdef LOG
  printf ("configfile: add entry key=%s\n", key);
#endif

  return entry;
}

/*
 * external interface
 */

static cfg_entry_t *_xine_config_lookup_entry (config_values_t *this, const char *key) {
  cfg_entry_t *entry;

  entry = this->first;

  while (entry && strcmp (entry->key, key))
    entry = entry->next;

  return entry;
}


static char *_xine_config_register_string (config_values_t *this,
					   const char *key, 
					   const char *def_value,
					   const char *description, 
					   const char *help,
					   int exp_level,
					   xine_config_cb_t changed_cb,
					   void *cb_data) {
  
  cfg_entry_t *entry;

  assert (key);
  assert (def_value);

#ifdef LOG
  printf ("configfile: registering %s\n", key);
#endif

  /* make sure this entry exists, create it if not */

  entry = _xine_config_lookup_entry (this, key);

  pthread_mutex_lock(&this->config_lock);
  if (!entry) {
    entry = xine_config_add (this, key);
    entry->unknown_value = copy_string(def_value);
  }
    
  /* convert entry to string type if necessary */

  if (entry->type != CONFIG_TYPE_STRING) {
    entry->type = CONFIG_TYPE_STRING;
    /* 
     * if there is no unknown_value (made with register_empty) set
     * it to default value 
     */
    if(!entry->unknown_value)
      entry->unknown_value = copy_string(def_value);
    
    /* 
     * Check for sticky string
     */
    if(entry->str_sticky) {
      entry->str_value = (char *) xine_xmalloc(strlen(entry->unknown_value) + 
					       strlen(entry->str_sticky) + 1);
      sprintf(entry->str_value, "%s%s", entry->unknown_value, entry->str_sticky);
    }
    else
      entry->str_value = strdup(entry->unknown_value);
    
  } else
    free (entry->str_default);
  
  /* fill out rest of struct */
  
  entry->str_default    = copy_string(def_value);
  entry->description    = description;
  entry->help           = help;       
  entry->exp_level      = exp_level;
  entry->callback       = changed_cb; 
  entry->callback_data  = cb_data;   
  
  pthread_mutex_unlock(&this->config_lock);
  
  return entry->str_value;
}

static int _xine_config_register_num (config_values_t *this,
				      const char *key, int def_value,
				      const char *description, 
				      const char *help,
				      int exp_level,
				      xine_config_cb_t changed_cb,
				      void *cb_data) {
  
  cfg_entry_t *entry;

  assert (key);

#ifdef LOG
  printf ("configfile: registering %s\n", key);
#endif

  /* make sure this entry exists, create it if not */

  entry = _xine_config_lookup_entry (this, key);

  pthread_mutex_lock(&this->config_lock);
  if (!entry) {
    entry = xine_config_add (this, key);
    entry->unknown_value = NULL;
  }
    
  /* convert entry to num type if necessary */

  if (entry->type != CONFIG_TYPE_NUM) {

    if (entry->type == CONFIG_TYPE_STRING) {
      free (entry->str_value);
      free (entry->str_default);
    }

    entry->type      = CONFIG_TYPE_NUM;

    if (entry->unknown_value)
      sscanf (entry->unknown_value, "%d", &entry->num_value);
    else
      entry->num_value = def_value;
  }

    
  /* fill out rest of struct */

  entry->num_default    = def_value;
  entry->description    = description;
  entry->help           = help;       
  entry->exp_level      = exp_level;
  entry->callback       = changed_cb; 
  entry->callback_data  = cb_data;   

  pthread_mutex_unlock(&this->config_lock);
  
  return entry->num_value;
}

static int _xine_config_register_bool (config_values_t *this,
				       const char *key, 
				       int def_value,
				       const char *description, 
				       const char *help,
				       int exp_level,
				       xine_config_cb_t changed_cb,
				       void *cb_data) {

  cfg_entry_t *entry;

  assert (key);

#ifdef LOG
  printf ("configfile: registering %s\n", key);
#endif

  /* make sure this entry exists, create it if not */

  entry = _xine_config_lookup_entry (this, key);

  pthread_mutex_lock(&this->config_lock);
  if (!entry) {
    entry = xine_config_add (this, key);
    entry->unknown_value = NULL;
  }
    
  /* convert entry to bool type if necessary */

  if (entry->type != CONFIG_TYPE_BOOL) {

    if (entry->type == CONFIG_TYPE_STRING) {
      free (entry->str_value);
      free (entry->str_default);
    }

    entry->type      = CONFIG_TYPE_BOOL;

    if (entry->unknown_value)
      sscanf (entry->unknown_value, "%d", &entry->num_value);
    else
      entry->num_value = def_value;
  }

    
  /* fill out rest of struct */

  entry->num_default    = def_value;
  entry->description    = description;
  entry->help           = help;       
  entry->exp_level      = exp_level;
  entry->callback       = changed_cb; 
  entry->callback_data  = cb_data;   

  pthread_mutex_unlock(&this->config_lock);
  
  return entry->num_value;
}

static int _xine_config_register_range (config_values_t *this,
					const char *key, 
					int def_value,
					int min, int max,
					const char *description, 
					const char *help,
					int exp_level,
					xine_config_cb_t changed_cb,
					void *cb_data) {
  
  cfg_entry_t *entry;

  assert (key);

#ifdef LOG
  printf ("configfile: registering range %s\n", key);
#endif

  /* make sure this entry exists, create it if not */

  entry = _xine_config_lookup_entry (this, key);
  
  pthread_mutex_lock(&this->config_lock);
  if (!entry) {
    entry = xine_config_add (this, key);
    entry->unknown_value = NULL;
  }
    
  /* convert entry to range type if necessary */

  if (entry->type != CONFIG_TYPE_RANGE) {

    if (entry->type == CONFIG_TYPE_STRING) {
      free (entry->str_value);
      free (entry->str_default);
    }

    entry->type      = CONFIG_TYPE_RANGE;

    if (entry->unknown_value)
      sscanf (entry->unknown_value, "%d", &entry->num_value);
    else
      entry->num_value = def_value;
  }

  /* fill out rest of struct */

  entry->num_default   = def_value;
  entry->range_min     = min;
  entry->range_max     = max;
  entry->description   = description;
  entry->help          = help;       
  entry->exp_level     = exp_level;
  entry->callback      = changed_cb; 
  entry->callback_data = cb_data;   

  pthread_mutex_unlock(&this->config_lock);
  
  return entry->num_value;
}

static int xine_config_parse_enum (const char *str, char **values) {

  char **value;
  int    i;
  
  
  value = values;
  i = 0;

  while (*value) {

#ifdef LOG
    printf ("configfile: parse enum, >%s< ?= >%s<\n",
	    *value, str);
#endif

    if (!strcmp (*value, str))
      return i;

    value++;
    i++;
  }

#ifdef LOG
  printf ("configfile: warning, >%s< is not a valid enum here, using 0\n",
	  str);
#endif

  return 0;
}

static int _xine_config_register_enum (config_values_t *this,
				       const char *key, 
				       int def_value,
				       char **values,
				       const char *description, 
				       const char *help,
				       int exp_level,
				       xine_config_cb_t changed_cb,
				       void *cb_data) {

  cfg_entry_t *entry;

  assert (key);
  assert (values);

#ifdef LOG
  printf ("configfile: registering enum %s\n", key);
#endif

  /* make sure this entry exists, create it if not */

  entry = _xine_config_lookup_entry (this, key);
  
  pthread_mutex_lock(&this->config_lock);
  if (!entry) {
    entry = xine_config_add (this, key);
    entry->unknown_value = NULL;
  }
    
  /* convert entry to enum type if necessary */

  if (entry->type != CONFIG_TYPE_ENUM) {

    if (entry->type == CONFIG_TYPE_STRING) {
      free (entry->str_value);
      free (entry->str_default);
    }

    entry->type      = CONFIG_TYPE_ENUM;

    if (entry->unknown_value)
      entry->num_value = xine_config_parse_enum (entry->unknown_value, values);
    else
      entry->num_value = def_value;

  }

  /* fill out rest of struct */

  entry->num_default   = def_value;
  entry->enum_values   = values;
  entry->description   = description;
  entry->help          = help;       
  entry->exp_level     = exp_level;
  entry->callback      = changed_cb; 
  entry->callback_data = cb_data;   

  pthread_mutex_unlock(&this->config_lock);
  
  return entry->num_value;
}

static void xine_config_shallow_copy(xine_cfg_entry_t *dest, cfg_entry_t *src)
{
  dest->key           = src->key;
  dest->type          = src->type;
  dest->unknown_value = src->unknown_value;
  dest->str_value     = src->str_value;
  dest->str_default   = src->str_default;
  dest->str_sticky    = src->str_sticky;
  dest->num_value     = src->num_value;
  dest->num_default   = src->num_default;
  dest->range_min     = src->range_min;
  dest->range_max     = src->range_max;
  dest->enum_values   = src->enum_values;
  dest->description   = src->description;
  dest->help          = src->help;
  dest->exp_level     = src->exp_level;
  dest->callback      = src->callback;
  dest->callback_data = src->callback_data;
}

static void xine_config_update_num (config_values_t *this,
				    const char *key, int value) {

  cfg_entry_t *entry;

  entry = this->lookup_entry (this, key);

#ifdef LOG
  printf ("configfile: updating %s to %d\n",
	  key, value);
#endif

  if (!entry) {

#ifdef LOG
    printf ("configfile: WARNING! tried to update unknown key %s (to %d)\n",
	    key, value);
#endif
    return;

  }

  if ((entry->type == CONFIG_TYPE_UNKNOWN) 
      || (entry->type == CONFIG_TYPE_STRING)) {
    printf ("configfile: error - tried to update non-num type %d (key %s, value %d)\n",
	    entry->type, entry->key, value);
    return;
  }

  pthread_mutex_lock(&this->config_lock);
  entry->num_value = value;

  if (entry->callback) {
    xine_cfg_entry_t cb_entry;
    xine_config_shallow_copy(&cb_entry, entry);
    entry->callback (entry->callback_data, &cb_entry);
  }
  pthread_mutex_unlock(&this->config_lock);
}

static void xine_config_update_string (config_values_t *this,
				       const char *key, 
				       const char *value) {

  cfg_entry_t *entry;

#ifdef LOG
  printf ("configfile: updating %s to %s\n",
	  key, value);
#endif

  entry = this->lookup_entry (this, key);

  if (!entry) {

    printf ("configfile: error - tried to update unknown key %s (to %s)\n",
	    key, value);
    return;

  }

  if (entry->type != CONFIG_TYPE_STRING) {
    printf ("configfile: error - tried to update non-string type %d (key %s, value %s)\n",
	    entry->type, entry->key, value);
    return;
  }

  pthread_mutex_lock(&this->config_lock);
  if (value != entry->str_value) {
    free (entry->str_value);

    entry->str_value = copy_string (value);
  }

  if (entry->callback) {
    xine_cfg_entry_t cb_entry;
    xine_config_shallow_copy(&cb_entry, entry);
    entry->callback (entry->callback_data, &cb_entry);
  }
  pthread_mutex_unlock(&this->config_lock);
}

/*
 * load/save config data from/to afile (e.g. $HOME/.xine/config)
 */
void xine_load_config (xine_p xine_ro, const char *filename) {

  config_values_t *this = xine_ro->config;
  FILE *f_config;

#ifdef LOG
  printf ("configfile: reading from file '%s'\n",
	  filename);
#endif

  f_config = fopen (filename, "r");
  
  if (f_config) {
    
    char line[1024];
    char *value;

    while (fgets (line, 1023, f_config)) {
      line[strlen(line)-1]= (char) 0; /* eliminate lf */
      
      if (line[0] == '#')
	continue;

      if ((value = strchr (line, ':'))) {

	cfg_entry_t *entry;

	*value = (char) 0;
	value++;
	
	if (!(entry = _xine_config_lookup_entry(this, line))) {
	  pthread_mutex_lock(&this->config_lock);
	  entry = xine_config_add (this, line);
	  entry->unknown_value = copy_string (value);
	  pthread_mutex_unlock(&this->config_lock);
	} else {
          switch (entry->type) {
          case XINE_CONFIG_TYPE_RANGE:
          case XINE_CONFIG_TYPE_ENUM:
          case XINE_CONFIG_TYPE_NUM:
          case XINE_CONFIG_TYPE_BOOL:
            xine_config_update_num (this, entry->key, atoi(value));
            break;
          case XINE_CONFIG_TYPE_STRING:
            xine_config_update_string (this, entry->key, value);
            break;
          case CONFIG_TYPE_UNKNOWN:
	    pthread_mutex_lock(&this->config_lock);
	    free(entry->unknown_value);
	    entry->unknown_value = copy_string (value);
	    pthread_mutex_unlock(&this->config_lock);
	    break;
          default:
            printf ("xine_interface: error, unknown config entry type %d\n", entry->type);
            abort();
          }
	}
      }
    }
    
    fclose (f_config);
  }
}

void xine_save_config (xine_p xine_ro, const char *filename) {

  config_values_t *this = xine_ro->config;
  FILE *f_config;

#ifdef LOG
  printf ("writing config file to %s\n", filename);
#endif

  f_config = fopen (filename, "w");

  if (f_config) {
    
    cfg_entry_t *entry;

    fprintf (f_config, "#\n# xine config file\n#\n\n");
    
    pthread_mutex_lock(&this->config_lock);
    entry = this->first;

    while (entry) {

#ifdef LOG
      printf ("configfile: saving key '%s'\n", entry->key);
#endif

      if (entry->description)
	fprintf (f_config, "# %s\n", entry->description);

      switch (entry->type) {
      case CONFIG_TYPE_UNKNOWN:

#if 0
	/* discard unclaimed values */
	fprintf (f_config, "%s:%s\n", 
		 entry->key, entry->unknown_value);
#endif

	break;
      case CONFIG_TYPE_RANGE:
	fprintf (f_config, "# [%d..%d], default: %d\n",
		 entry->range_min, entry->range_max, entry->num_default);
	fprintf (f_config, "%s:%d\n", entry->key, entry->num_value);
	fprintf (f_config, "\n");
	break;
      case CONFIG_TYPE_STRING:
	fprintf (f_config, "# string, default: %s\n",
		 entry->str_default);
	fprintf (f_config, "%s:%s\n", entry->key, entry->str_value);
	fprintf (f_config, "\n");
	break;
      case CONFIG_TYPE_ENUM: {
	char **value;
	
	fprintf (f_config, "# {");
	value = entry->enum_values;
	while (*value) {
	  fprintf (f_config, " %s ", *value);
	  value++;
	}
	
	fprintf (f_config, "}, default: %d\n",
		 entry->num_default);
	fprintf (f_config, "%s:", entry->key);
	
	fprintf (f_config, "%s\n", entry->enum_values[entry->num_value]);
	fprintf (f_config, "\n");
	break;
      }
      case CONFIG_TYPE_NUM:
	fprintf (f_config, "# numeric, default: %d\n",
		 entry->num_default);
	fprintf (f_config, "%s:%d\n", entry->key, entry->num_value);
	fprintf (f_config, "\n");
	break;
      case CONFIG_TYPE_BOOL:
	fprintf (f_config, "# bool, default: %d\n",
		 entry->num_default);
	fprintf (f_config, "%s:%d\n", entry->key, entry->num_value);
	fprintf (f_config, "\n");
	break;
      }
      
      entry = entry->next;
    }    
    pthread_mutex_unlock(&this->config_lock);
    fclose (f_config);
  }  
}

static void xine_config_dispose (config_values_t *this) {

  cfg_entry_t *entry, *last;

  pthread_mutex_lock(&this->config_lock);
  entry = this->first;

#ifdef LOG
  printf ("configfile: dispose\n");
#endif

  while (entry) {
    last = entry;
    entry = entry->next;

    if (last->key)
      free (last->key);
    if (last->unknown_value)
      free (last->unknown_value);

    if (last->type == CONFIG_TYPE_STRING) {
      free (last->str_value);
      free (last->str_default);
    }

    free (last);
  }
  pthread_mutex_unlock(&this->config_lock);
  
  pthread_mutex_destroy(&this->config_lock);
  free (this);
}


static void xine_config_unregister_cb (config_values_t *this,
				       const char *key) {

  cfg_entry_t *entry;
  
  assert (this);
  assert (key);
  
  entry = _xine_config_lookup_entry (this, key);
  if (entry) {
    entry->callback = NULL;
    entry->callback_data = NULL;
  }
}


config_values_t *xine_config_init () {

#ifdef HAVE_IRIXAL
  volatile /* is this a (old, 2.91.66) irix gcc bug?!? */
#endif
  config_values_t *this;

  if (!(this = xine_xmalloc(sizeof(config_values_t)))) {

    printf ("configfile: could not allocate config object\n");
    abort();
  }

  this->first = NULL;
  this->last  = NULL;
  
  pthread_mutex_init(&this->config_lock, NULL);

  this->register_string = _xine_config_register_string;
  this->register_range  = _xine_config_register_range;
  this->register_enum   = _xine_config_register_enum;
  this->register_num    = _xine_config_register_num;
  this->register_bool   = _xine_config_register_bool;
  this->update_num      = xine_config_update_num;
  this->update_string   = xine_config_update_string;
  this->parse_enum      = xine_config_parse_enum;
  this->lookup_entry    = _xine_config_lookup_entry;
  this->unregister_callback = xine_config_unregister_cb;
  this->dispose         = xine_config_dispose;

  return this;
}

int xine_config_change_opt(config_values_t *config, const char *opt) {
  cfg_entry_t *entry;
  int          handled = 0;

#ifdef LOG
  printf ("configfile: change_opt '%s'\n", opt);
#endif
  
  if(config && opt && (!strncasecmp(opt, "opt:", 4))) {
    char *optsafe;
    char *key, *value;

    xine_strdupa(optsafe, opt);
    key = &optsafe[4];
    value = strrchr(optsafe, '=');    

    if(key && strlen(key) && value && strlen(value)) {
      
      *value++ = '\0';

      entry = config->lookup_entry(config, key);
      
      if(entry) {

	switch(entry->type) {
	  
	case CONFIG_TYPE_STRING:
	  config->update_string(config, key, value);
	  handled = 1;
	  break;
	  
	case CONFIG_TYPE_RANGE:
	case CONFIG_TYPE_ENUM:
	case CONFIG_TYPE_NUM:
	case CONFIG_TYPE_BOOL:
	  config->update_num(config, key, (atoi(value)));
	  handled = 1;
	  break;
	  
	case CONFIG_TYPE_UNKNOWN:
#ifdef LOG
	  printf("configfile: change_opt() try to update an CONFIG_TYPE_UNKNOWN entry\n");
#endif
	  break;

	}
      }
    }
  }
  
  return handled;
}

