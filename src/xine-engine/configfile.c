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
 * $Id: configfile.c,v 1.1 2001/04/18 22:36:01 f1rmb Exp $
 *
 * config file management - implementation
 *
 */
 
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "configfile.h"
#include "utils.h"

typedef struct cfg_entry_s {
  struct cfg_entry_s *next;
  char               *key, *value;
} cfg_entry_t;

struct cfg_data_s {
  cfg_entry_t *gConfig, *gConfigLast;
};



/*
 * internal utility functions
 *******************************/

void config_file_add (config_values_t *this, char *key, char *value) {

  cfg_entry_t *entry;
  int          len;

  entry = (cfg_entry_t *) xmalloc (sizeof (cfg_entry_t));

  len = strlen (key);
  entry->key = (char *) xmalloc (len+2);
  strncpy (entry->key, key, len+1);

  len = strlen (value);
  entry->value = (char *) xmalloc (len+21);
  strncpy (entry->value, value, len+1);

  entry->next = NULL;

  if (this->data->gConfigLast) 
    this->data->gConfigLast->next = entry;
  else
    this->data->gConfig = entry;
  
  this->data->gConfigLast = entry;

}




cfg_entry_t *config_file_search (config_values_t *this, char *key) {
  cfg_entry_t *entry;

  entry = this->data->gConfig;

  while (entry && strcmp (entry->key, key))
    entry = entry->next;

  return entry;
}



/*
 * external interface
 ***********************/

static char *config_file_lookup_str (config_values_t *this,
				     char *key, char*str_default) {
  cfg_entry_t *entry;

  entry = config_file_search (this, key);

  if (entry)
    return entry->value;

  config_file_add (this, key, str_default);

  return str_default;
}




static int config_file_lookup_int (config_values_t *this,
				   char *key, int n_default) {

  cfg_entry_t *entry;
  char str[25];

  entry = config_file_search (this, key);

  if (entry) {
    int n;

    if (sscanf (entry->value, "%d", &n) == 1) 
      return n;
  }

  sprintf (str, "%d", n_default);

  config_file_add (this, key, str);

  return n_default; 
}




static void config_file_set_int (config_values_t *this,
				 char *key, int value) {
  
  cfg_entry_t *entry;

  entry = config_file_search (this, key);

  if (entry) {
    sprintf (entry->value, "%d", value);
  }
  else {
    char str[25];
    sprintf (str, "%d", value);

    config_file_add (this, key, str);
  }
}




static void config_file_set_str (config_values_t *this,
				 char *key, char *value) {

  cfg_entry_t *entry;

  entry = config_file_search (this, key);

  if (entry) {
    int len;

    free (entry->value);

    len = strlen (value);
    entry->value = (char *) xmalloc (len+20);
    strncpy (entry->value, value, len);

  }
  else {
    config_file_add (this, key, value);
  }
}




static void config_file_save (config_values_t *this) {
  FILE *f_config;
  char filename[1024];

  sprintf (filename, "%s/.xinerc", get_homedir());

  f_config = fopen (filename, "w");

  if (f_config) {
    
    cfg_entry_t *entry;

    fprintf (f_config, "#\n# xine config file\n#\n");

    entry = this->data->gConfig;

    while (entry) {
      fprintf (f_config, "%s:%s\n",entry->key,entry->value);
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
	*value = (char) 0;
	  value++;
	  
	  config_file_add (this, line, value);
      }
      
    }
    
    fclose (f_config);
  }
}




config_values_t *config_file_init (char *filename) {

  config_values_t *this;
  cfg_data_t *data;

  if ( (this = xmalloc(sizeof(config_values_t))) ) {
    if ( (data = xmalloc(sizeof(cfg_data_t))) ) {
      data->gConfig = NULL;
      data->gConfigLast = NULL;
      this->data = data;
      config_file_read (this, filename);

    }
    else {
      fprintf (stderr, "WARNING: could not allocate config data\n");
    }
  }
  else {
    fprintf (stderr, "WARNING: could not allocate config values list\n");
  }

  this->lookup_str = config_file_lookup_str;
  this->lookup_int = config_file_lookup_int;
  this->set_str    = config_file_set_str;
  this->set_int    = config_file_set_int;
  this->save       = config_file_save;
  this->read       = config_file_read;

  return this;
}


/*
 * $Log: configfile.c,v $
 * Revision 1.1  2001/04/18 22:36:01  f1rmb
 * Initial revision
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
