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
 * $Id: configfile.h,v 1.12 2002/09/04 23:31:13 guenter Exp $
 *
 * config file management
 *
 */

#ifndef HAVE_CONFIGFILE_H
#define HAVE_CONFIGFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

#include "xine.h"

typedef struct cfg_entry_s cfg_entry_t;
typedef struct config_values_s config_values_t;

struct cfg_entry_s {
  cfg_entry_t     *next;
  config_values_t *config;

  char            *key;
  int              type;

  /* type unknown */
  char            *unknown_value;

  /* type string */
  char            *str_value;
  char            *str_default;
  char            *str_sticky;

  /* common to range, enum, num, bool: */

  int              num_value;
  int              num_default;

  /* type range specific: */
  int              range_min;
  int              range_max;

  /* type enum specific: */
  char           **enum_values;

  /* help info for the user */
  char            *description;
  char            *help;

  /* user experience level */
  int              exp_level;

  /* callback function and data for live changeable values */
  xine_config_cb_t callback;
  void            *callback_data;
};

/*
 * config entry data types
 */

#define CONFIG_TYPE_UNKNOWN 0
#define CONFIG_TYPE_RANGE   1
#define CONFIG_TYPE_STRING  2
#define CONFIG_TYPE_ENUM    3
#define CONFIG_TYPE_NUM     4
#define CONFIG_TYPE_BOOL    5

struct config_values_s {

  /*
   * register config values
   *
   * these functions return the current value of the
   * registered item, i.e. the default value if it was
   * not found in the config file or the current value
   * from the config file otherwise
   */

  char* (*register_string) (config_values_t *this,
			    char *key, 
			    char *def_value,
			    char *description, 
			    char *help,
			    int exp_level,
			    xine_config_cb_t changed_cb,
			    void *cb_data);

  int (*register_range) (config_values_t *this,
			 char *key,
			 int def_value,
			 int min, int max,
			 char *description, 
			 char *help,
			 int exp_level,
			 xine_config_cb_t changed_cb,
			 void *cb_data);

  int (*register_enum) (config_values_t *this,
			char *key,
			int def_value,
			char **values,
			char *description, 
			char *help,
			int exp_level,
			xine_config_cb_t changed_cb,
			void *cb_data);

  int (*register_num) (config_values_t *this,
		       char *key, 
		       int def_value,
		       char *description, 
		       char *help,
		       int exp_level,
		       xine_config_cb_t changed_cb,
		       void *cb_data);

  int (*register_bool) (config_values_t *this,
			char *key, 
			int def_value,
			char *description, 
			char *help,
			int exp_level,
			xine_config_cb_t changed_cb,
			void *cb_data);

  /* convenience function to update range, enum, num and bool values */
  void (*update_num) (config_values_t *this,
		      char *key, int value);

  /* convenience function to update string values */
  void (*update_string) (config_values_t *this,
			 char *key, char *value);

  /* small utility function for enum handling */
  int (*parse_enum) (char *str, char **values);

  /*
   * lookup config entries
   * 
   * remember to call the changed_cb if it exists
   * and you changed the value of this item
   */

  cfg_entry_t* (*lookup_entry) (config_values_t *this,
				char *key);

  /*
   * unregister callback function
   */
  void (*unregister_callback) (config_values_t *this,
			       char *key);

  /* 
   * config values are stored here:
   */
  cfg_entry_t         *first, *last, *cur;
  xine_cfg_entry_t     public_entry;
};

/*
 * allocate and init a new xine config object
 */
config_values_t *xine_config_init ();

/*
 * hack: intepret "opt:"-style mrls for config value changes
 */

int xine_config_change_opt(config_values_t *config, char *opt) ;


#ifdef __cplusplus
}
#endif

#endif

