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
 * $Id: configfile.h,v 1.1 2001/04/18 22:36:05 f1rmb Exp $
 *
 * config file management
 *
 */


#ifndef HAVE_CONFIGFILE_H
#define HAVE_CONFIGFILE_H

#include <inttypes.h>

typedef struct config_values_s config_values_t;
typedef struct cfg_data_s cfg_data_t;

struct config_values_s {
  /*
   * lookup config values
   */
  char* (*lookup_str) (config_values_t *this,
		       char *key, char *str_default);
  
  int (*lookup_int) (config_values_t *this,
		     char *key, int n_default);
  
  /*
   * set config values
   */
  
  void (*set_str) (config_values_t *this,
		   char *key, char *value) ;
  
  void (*set_int) (config_values_t *this,
		   char *key, int value) ;
  
  /*
   * write config file to disk
   */
  void (*save) (config_values_t *this);

  /*
   * read config file from disk, ovverriding values in memory
   * if you also want to clear values that are not in the file,
   * use _init instead!
   */
  void (*read) (config_values_t *this, char *filename);

  /* 
   * contains private data of this config file
   */
  cfg_data_t *data;
};

/*
 * init internal data structures, read config file
 * (if it exists)
 */
config_values_t *config_file_init (char *filename);


#endif

/*
 * $Log: configfile.h,v $
 * Revision 1.1  2001/04/18 22:36:05  f1rmb
 * Initial revision
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




