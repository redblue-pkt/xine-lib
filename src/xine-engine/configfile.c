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
 * $Id: configfile.c,v 1.20 2002/04/11 07:17:43 esnel Exp $
 *
 * config file management - implementation
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

/* 
#define CONFIG_LOG
*/


/* 
 * internal utility functions
 */

static char *copy_string (char *str) {

  char *cpy;
  int   len;

  len = strlen (str);

  cpy = xine_xmalloc (len+256);

  strncpy (cpy, str, len);

  return cpy;
}

static cfg_entry_t *config_file_add (config_values_t *this, char *key) {

  cfg_entry_t *entry;

  entry = (cfg_entry_t *) xine_xmalloc (sizeof (cfg_entry_t));
  entry->config        = this;
  entry->key           = copy_string (key);
  entry->type          = CONFIG_TYPE_UNKNOWN;
  entry->str_sticky    = NULL;

  entry->next          = NULL;

  if (this->last) 
    this->last->next = entry;
  else
    this->first = entry;
  
  this->last = entry;

#ifdef CONFIG_LOG
  printf ("configfile: add entry key=%s\n", key);
#endif

  return entry;
}

/*
 * external interface
 */

static cfg_entry_t *config_file_lookup_entry (config_values_t *this, char *key) {
  cfg_entry_t *entry;

  entry = this->first;

  while (entry && strcmp (entry->key, key))
    entry = entry->next;

  return entry;
}


static char *config_file_register_string (config_values_t *this,
					  char *key, char *def_value,
					  char *description, 
					  char *help,
					  config_cb_t changed_cb,
					  void *cb_data) {
  
  cfg_entry_t *entry;

  assert (key);
  assert (def_value);

#ifdef CONFIG_LOG
  printf ("configfile: registering %s\n", key);
#endif

  /* make sure this entry exists, create it if not */

  entry = config_file_lookup_entry (this, key);

  if (!entry) {
    entry = config_file_add (this, key);
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
      entry->unknown_value = strdup(def_value);
    
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
  entry->callback       = changed_cb; 
  entry->callback_data  = cb_data;   
  
  return entry->str_value;
}

static int config_file_register_num (config_values_t *this,
				     char *key, int def_value,
				     char *description, 
				     char *help,
				     config_cb_t changed_cb,
				     void *cb_data) {
  
  cfg_entry_t *entry;

  assert (key);

#ifdef CONFIG_LOG
  printf ("configfile: registering %s\n", key);
#endif

  /* make sure this entry exists, create it if not */

  entry = config_file_lookup_entry (this, key);

  if (!entry) {
    entry = config_file_add (this, key);
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
  entry->callback       = changed_cb; 
  entry->callback_data  = cb_data;   

  return entry->num_value;
}

static int config_file_register_bool (config_values_t *this,
				      char *key, int def_value,
				      char *description, 
				      char *help,
				      config_cb_t changed_cb,
				      void *cb_data) {

  cfg_entry_t *entry;

  assert (key);

#ifdef CONFIG_LOG
  printf ("configfile: registering %s\n", key);
#endif

  /* make sure this entry exists, create it if not */

  entry = config_file_lookup_entry (this, key);

  if (!entry) {
    entry = config_file_add (this, key);
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
  entry->callback       = changed_cb; 
  entry->callback_data  = cb_data;   

  return entry->num_value;
}

static int config_file_register_range (config_values_t *this,
				       char *key, int def_value,
				       int min, int max,
				       char *description, 
				       char *help,
				       config_cb_t changed_cb,
				       void *cb_data) {
  
  cfg_entry_t *entry;

  assert (key);

#ifdef CONFIG_LOG
  printf ("configfile: registering %s\n", key);
#endif

  /* make sure this entry exists, create it if not */

  entry = config_file_lookup_entry (this, key);
  if (!entry) {
    entry = config_file_add (this, key);
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
  entry->callback      = changed_cb; 
  entry->callback_data = cb_data;   

  return entry->num_value;
}

static int config_file_parse_enum (char *str, char **values) {

  char **value;
  int    i;
  
  
  value = values;
  i = 0;

  while (*value) {

#ifdef CONFIG_LOG
    printf ("configfile: parse enum, >%s< ?= >%s<\n",
	    *value, str);
#endif

    if (!strcmp (*value, str))
      return i;

    value++;
    i++;
  }

#ifdef CONFIG_LOG
  printf ("configfile: warning, >%s< is not a valid enum here, using 0\n",
	  str);
#endif

  return 0;
}

static int config_file_register_enum (config_values_t *this,
				      char *key, int def_value,
				      char **values,
				      char *description, 
				      char *help,
				      config_cb_t changed_cb,
				      void *cb_data) {

  cfg_entry_t *entry;

  assert (key);
  assert (values);

#ifdef CONFIG_LOG
  printf ("configfile: registering %s\n", key);
#endif

  /* make sure this entry exists, create it if not */

  entry = config_file_lookup_entry (this, key);
  if (!entry) {
    entry = config_file_add (this, key);
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
      entry->num_value = config_file_parse_enum (entry->unknown_value, values);
    else
      entry->num_value = def_value;

  }

  /* fill out rest of struct */

  entry->num_default   = def_value;
  entry->enum_values   = values;
  entry->description   = description;
  entry->help          = help;       
  entry->callback      = changed_cb; 
  entry->callback_data = cb_data;   

  return entry->num_value;
}

static void config_file_update_num (config_values_t *this,
				    char *key, int value) {

  cfg_entry_t *entry;

  entry = this->lookup_entry (this, key);

  if (!entry) {

#ifdef CONFIG_LOG
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

  entry->num_value = value;

  if (entry->callback) 
    entry->callback (entry->callback_data, entry);
}

static void config_file_update_string (config_values_t *this,
				       char *key, char *value) {

  cfg_entry_t *entry;

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

  if (value != entry->str_value) {
    free (entry->str_value);

    entry->str_value = copy_string (value);
  }

  if (entry->callback) 
    entry->callback (entry->callback_data, entry);
}

static void config_file_save (config_values_t *this) {
  FILE *f_config;
  char filename[1024];

  sprintf (filename, "%s/.xine", xine_get_homedir());
  mkdir (filename, 0755);

  sprintf (filename, "%s/.xine/config", xine_get_homedir());

#ifdef CONFIG_LOG
  printf ("writing config file to %s\n", filename);
#endif

  f_config = fopen (filename, "w");

  if (f_config) {
    
    cfg_entry_t *entry;

    fprintf (f_config, "#\n# xine config file\n#\n\n");

    entry = this->first;

    while (entry) {
      if (entry->description)
	fprintf (f_config, "# %s\n", entry->description);

      switch (entry->type) {
      case CONFIG_TYPE_UNKNOWN:

	fprintf (f_config, "%s:%s\n", 
		 entry->key, entry->unknown_value);

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
    fclose (f_config);
  }  
}

static void config_file_read (config_values_t *this, char *filename){

  FILE *f_config;

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
	
	entry = config_file_add (this, line);
	entry->unknown_value = copy_string (value);
      }
    }
    
    fclose (f_config);
  }
}

static void config_file_dispose (config_values_t *this)
{
  cfg_entry_t *entry, *last;

  entry = this->first;

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
  free (this);
}

config_values_t *xine_config_file_init (char *filename) {

#ifdef HAVE_IRIXAL
  volatile /* is this a (old, 2.91.66) irix gcc bug?!? */
#endif
  config_values_t *this;

  if ( (this = xine_xmalloc(sizeof(config_values_t))) ) {

    this->first = NULL;
    this->last  = NULL;

    config_file_read (this, filename);
   
  } else {
    printf ("configfile: could not allocate config object\n");
    exit (1);
  }

  this->register_string = config_file_register_string;
  this->register_range  = config_file_register_range;
  this->register_enum   = config_file_register_enum;
  this->register_num    = config_file_register_num;
  this->register_bool   = config_file_register_bool;
  this->update_num      = config_file_update_num;
  this->update_string   = config_file_update_string;
  this->parse_enum      = config_file_parse_enum;
  this->lookup_entry    = config_file_lookup_entry;
  this->save            = config_file_save;
  this->read            = config_file_read;
  this->dispose         = config_file_dispose;

  return this;
}


/*
 * $Log: configfile.c,v $
 * Revision 1.20  2002/04/11 07:17:43  esnel
 * Fix configfile corruption reported by Chris Rankin
 *
 * Revision 1.19  2002/03/16 13:33:47  esnel
 * fix memory leak, add dispose() function to config_values_s
 *
 * Revision 1.18  2002/02/06 10:57:15  f1rmb
 * rename config_file_init to xine_config_file_init.
 *
 * Revision 1.17  2002/01/13 23:08:27  jcdutton
 * Fix another compiler warning.
 *
 * Revision 1.16  2002/01/13 21:21:05  jcdutton
 * Undo last change.
 *
 * Revision 1.15  2002/01/13 21:15:48  jcdutton
 * Fix a few compile warnings.
 *
 * Revision 1.14  2002/01/09 15:16:37  mshopf
 * IRIX port finally compiles (and actually works) again.
 *
 * Revision 1.13  2001/12/01 22:38:32  guenter
 * add avi subtitle decoder (based on mplayer code), minor cleanups, removed register_empty function from configfile (undocumented and doesn't make sense)
 *
 * Revision 1.12  2001/11/30 21:55:06  f1rmb
 * Add an automatic way for input plugin to add extra valid mrls:
 * add at bottom of init_input_plugin() a line like this:
 * REGISTER_VALID_MRLS(this->config, "mrl.mrls_mpeg_block", "xxx");
 *
 * Revision 1.11  2001/11/20 19:13:28  guenter
 * add more checks against incorrect configfile usage
 *
 * Revision 1.10  2001/11/20 17:22:14  miguelfreitas
 * testing some configfile stuff...
 *
 * Revision 1.9  2001/11/19 02:57:10  guenter
 * make description strings optional - config options without description string will not appear in setup dialog
 *
 * Revision 1.8  2001/11/18 21:38:23  miguelfreitas
 * fix enum value saving
 *
 * Revision 1.7  2001/11/18 15:08:31  guenter
 * more cleanups, config stuff bugfixes
 *
 * Revision 1.6  2001/11/18 03:53:25  guenter
 * new configfile interface, code cleanup, xprintf is gone
 *
 * Revision 1.5  2001/11/17 14:26:39  f1rmb
 * Add 'xine_' prefix to all of xine-utils functions (what about cpu
 * acceleration?). Merge xine-utils header files to a new one "xineutils.h".
 * Update xine-lib C/headers to reflect those changes.
 * dxr3 headers are no more installed ine $includdir, but $includdir/xine.
 *
 * Revision 1.4  2001/07/26 11:12:26  f1rmb
 * Updated doxy sections in xine.h.tmpl.in. Added man3. Removed french man page. Added API doc in html. Add new rpm package (doc). Fixes some little bugs in
 * proto decl, etc...
 *
 * Revision 1.3  2001/06/15 11:08:13  f1rmb
 * Check arguments in public functions.
 *
 * Revision 1.2  2001/06/15 10:17:53  f1rmb
 * Passing NULL to config_file_lookup_str() is valid.
 *
 * Revision 1.1.1.1  2001/04/18 22:36:01  f1rmb
 * Initial import into CVS
 *
 * Revision 1.8  2001/03/31 03:42:25  guenter
 * more cleanups, started xv driver
 *
 * Revision 1.7  2001/03/28 12:30:25  siggi
 * fixed init function
 * added read function (multiple config files now supported)
 *
 * Revision 1.6  2001/03/27 17:12:49  siggi
 * made config file handler a dynamic "object"
 *
 */
