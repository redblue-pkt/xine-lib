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
 * $Id: configfile.h,v 1.8 2002/02/06 10:57:15 f1rmb Exp $
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

typedef struct cfg_entry_s cfg_entry_t;
typedef struct config_values_s config_values_t;

typedef void (*config_cb_t) (void *, cfg_entry_t *);

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

  /* callback function and data for live changeable values */
  config_cb_t      callback;
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
			    config_cb_t changed_cb,
			    void *cb_data);

  int (*register_range) (config_values_t *this,
			 char *key,
			 int def_value,
			 int min, int max,
			 char *description, 
			 char *help,
			 config_cb_t changed_cb,
			 void *cb_data);

  int (*register_enum) (config_values_t *this,
			char *key,
			int def_value,
			char **values,
			char *description, 
			char *help,
			config_cb_t changed_cb,
			void *cb_data);

  int (*register_num) (config_values_t *this,
		       char *key, 
		       int def_value,
		       char *description, 
		       char *help,
		       config_cb_t changed_cb,
		       void *cb_data);

  int (*register_bool) (config_values_t *this,
			char *key, 
			int def_value,
			char *description, 
			char *help,
			config_cb_t changed_cb,
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
   * write config file to disk
   */
  void (*save) (config_values_t *this);

  /*
   * read config file from disk, overriding values in memory
   */
  void (*read) (config_values_t *this, char *filename);

  /* 
   * config values are stored here:
   */
  cfg_entry_t         *first, *last;
};

/*
 * init internal data structures, read config file
 * (if it exists)
 */
config_values_t *xine_config_file_init (char *filename);

#ifdef __cplusplus
}
#endif

#endif

/*
 * $Log: configfile.h,v $
 * Revision 1.8  2002/02/06 10:57:15  f1rmb
 * rename config_file_init to xine_config_file_init.
 *
 * Revision 1.7  2001/12/01 22:38:32  guenter
 * add avi subtitle decoder (based on mplayer code), minor cleanups, removed register_empty function from configfile (undocumented and doesn't make sense)
 *
 * Revision 1.6  2001/11/30 21:55:06  f1rmb
 * Add an automatic way for input plugin to add extra valid mrls:
 * add at bottom of init_input_plugin() a line like this:
 * REGISTER_VALID_MRLS(this->config, "mrl.mrls_mpeg_block", "xxx");
 *
 * Revision 1.5  2001/11/20 17:22:14  miguelfreitas
 * testing some configfile stuff...
 *
 * Revision 1.4  2001/11/18 03:53:25  guenter
 * new configfile interface, code cleanup, xprintf is gone
 *
 * Revision 1.3  2001/07/26 11:12:26  f1rmb
 * Updated doxy sections in xine.h.tmpl.in. Added man3. Removed french man page. Added API doc in html. Add new rpm package (doc). Fixes some little bugs in
 * proto decl, etc...
 *
 * Revision 1.2  2001/07/18 21:38:17  f1rmb
 * Split alsa drivers, more checks about versions. Made xine lib c++ compliant.
 *
 * Revision 1.1.1.1  2001/04/18 22:36:05  f1rmb
 * Initial import into CVS
 *
 * Revision 1.6  2001/03/31 03:42:25  guenter
 * more cleanups, started xv driver
 *
 * Revision 1.5  2001/03/28 12:30:25  siggi
 * fixed init function
 * added read function (multiple config files now supported)
 *
 * Revision 1.4  2001/03/27 21:49:02  siggi
 * started touching demuxers
 *
 */
