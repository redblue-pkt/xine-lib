/* 
 * Copyright (C) 2000 the xine project
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
 * $Id: input_file.c,v 1.14 2001/06/23 20:47:29 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <limits.h>	/*PATH_MAX*/

#include "xine_internal.h"
#include "monitor.h"
#include "input_plugin.h"

extern int errno;

static uint32_t xine_debug;

#ifndef S_ISLNK
#define S_ISLNK(mode)  0
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(mode) 0
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(mode) 0
#endif
#ifndef S_ISCHR
#define S_ISCHR(mode)  0
#endif
#ifndef S_ISBLK
#define S_ISBLK(mode)  0
#endif
#ifndef S_ISREG
#define S_ISREG(mode)  0
#endif
#if !S_IXUGO
#define S_IXUGO        (S_IXUSR | S_IXGRP | S_IXOTH)
#endif

typedef struct {
  input_plugin_t    input_plugin;
  
  int               fh;
  char             *mrl;
  config_values_t  *config;

  int               mrls_allocated_entries;
  mrl_t           **mrls;
  
} file_input_plugin_t;

/*
 *
 */
static uint32_t file_plugin_get_capabilities (input_plugin_t *this_gen) {
  return INPUT_CAP_SEEKABLE | INPUT_CAP_GET_DIR;
}

/*
 *
 */
static int file_plugin_open (input_plugin_t *this_gen, char *mrl) {

  char                *filename;
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  this->mrl = mrl;

  if (!strncasecmp (mrl, "file:",5))
    filename = &mrl[5];
  else
    filename = mrl;

  xprintf (VERBOSE|INPUT, "Opening >%s<\n",filename);

  this->fh = open (filename, O_RDONLY);

  if (this->fh == -1) {
    return 0;
  }

  return 1;
}

/*
 *
 */
static off_t file_plugin_read (input_plugin_t *this_gen, char *buf, off_t len) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  return read (this->fh, buf, len);
}

/*
 *
 */
static buf_element_t *file_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {

  off_t                 num_bytes, total_bytes;
  file_input_plugin_t  *this = (file_input_plugin_t *) this_gen;
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);

  buf->content = buf->mem;
  total_bytes = 0;

  while (total_bytes < todo) {
    num_bytes = read (this->fh, buf->mem + total_bytes, todo-total_bytes);
    total_bytes += num_bytes;
    if (!num_bytes) {
      buf->free_buffer (buf);
      return NULL;
    }
  }

  buf->size = total_bytes;

  return buf;
}

/*
 *
 */
static off_t file_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  return lseek (this->fh, offset, origin);
}

/*
 *
 */
static off_t file_plugin_get_current_pos (input_plugin_t *this_gen){
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  return lseek (this->fh, 0, SEEK_CUR);
}

/*
 *
 */
static off_t file_plugin_get_length (input_plugin_t *this_gen) {

  struct stat          buf ;
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  if (fstat (this->fh, &buf) == 0) {
    return buf.st_size;
  } else
    perror ("system call fstat");
  return 0;
}

/*
 *
 */
static uint32_t file_plugin_get_blocksize (input_plugin_t *this_gen) {
  return 0;
}

/*
 *
 */
static mrl_t **file_plugin_get_dir (input_plugin_t *this_gen, 
				    char *filename, int *nFiles) {
  file_input_plugin_t  *this = (file_input_plugin_t *) this_gen;
  char                  current_dir[PATH_MAX + 1];
  char                 *fullpathname   = NULL;
  struct dirent        *pdirent;
  DIR                  *pdir;
  mode_t                mode;
  struct stat           pstat;
  int                   num_files      = 0;

  *nFiles = 0;
  memset(&current_dir, 0, strlen(current_dir));

  /* 
   * No origin location, so got the content of the current directory
   */
  if(!filename) {
    char *pwd;
    
    if((pwd = getenv("PWD")) == NULL)
      snprintf(current_dir, 1, "%s", ".");
    else
      snprintf(current_dir, PATH_MAX, "%s", pwd);
  }
  else
    snprintf(current_dir, PATH_MAX, "%s", filename);
    
  /*
   * Ooch!
   */
  if((pdir = opendir(current_dir)) == NULL) {
    return NULL; 
  }

  while((pdirent = readdir(pdir)) != NULL) {
    /* 
     * full pathname creation 
     */
    if(!fullpathname) {
      fullpathname = (char *) 
	malloc((strlen(current_dir) + strlen(pdirent->d_name) + 2));
    }
    else {
      fullpathname = (char *) 
	realloc(fullpathname, 
		(strlen(current_dir) + strlen(pdirent->d_name) + 2));
    }

    sprintf(fullpathname, "%s/%s", current_dir, pdirent->d_name); 
    
    /* 
     * stat the file 
     */
    if(lstat(fullpathname, &pstat) < 0) {
      fprintf(stderr, "lstat() failed: %s\n", strerror(errno));
      free(fullpathname);
      return NULL;
    }
    
    /* 
     * alloc enought memory in private plugin structure to
     * store found mrls.
     */
    if(num_files >= this->mrls_allocated_entries
       || this->mrls_allocated_entries == 0) {

      if((this->mrls[num_files] = (mrl_t *) malloc(sizeof(mrl_t))) == NULL) {
	fprintf(stderr, "malloc() failed: %s\n", strerror(errno));
	return NULL;
      }

      this->mrls[num_files]->mrl = (char *) malloc(strlen(fullpathname) + 1);

    }
    else {
      printf("realloc\n");
      this->mrls[num_files]->mrl = (char *) 
	realloc(this->mrls[num_files]->mrl, strlen(fullpathname) + 1);
    }
    
    sprintf(this->mrls[num_files]->mrl, "%s", fullpathname);
    
    this->mrls[num_files]->size = pstat.st_size;

    /* 
     * Ok, now check file type 
     */
    mode = pstat.st_mode;
    
    if(S_ISLNK(mode)) {
      this->mrls[num_files]->type = mrl_symbolic_link;
      /*
       * So follow the link
       */
      {
	char *linkbuf;
	int linksize;
	
	linkbuf = (char *) alloca(PATH_MAX + 2);
	memset(linkbuf, 0, sizeof(linkbuf));
	linksize = readlink(fullpathname, linkbuf, PATH_MAX + 1);
	
	if(linksize < 0) {
	  fprintf(stderr, "readlink() failed: %s\n", strerror(errno));
	}
	else {
	  this->mrls[num_files]->mrl = (char *) 
	    realloc(this->mrls[num_files]->mrl, (linksize + 1));
	  memset(this->mrls[num_files]->mrl, 0, linksize + 1);
	  strncpy(this->mrls[num_files]->mrl, linkbuf, linksize);
	}
      }
    }
    else if(S_ISDIR(mode))
      this->mrls[num_files]->type = mrl_directory;
    else if(S_ISCHR(mode))
      this->mrls[num_files]->type = mrl_chardev;
    else if(S_ISBLK(mode))
      this->mrls[num_files]->type = mrl_blockdev;
    else if(S_ISFIFO(mode))
      this->mrls[num_files]->type = mrl_fifo;
    else if(S_ISSOCK(mode))
      this->mrls[num_files]->type = mrl_sock;
    else {
      this->mrls[num_files]->type = mrl_normal;
      if(mode & S_IXUGO)
	this->mrls[num_files]->type |= mrl_type_exec;
      }

    num_files++;
  }

  closedir(pdir);

  *nFiles = num_files;

  if(num_files > this->mrls_allocated_entries)
    this->mrls_allocated_entries = num_files;

  if(fullpathname)
    free(fullpathname);
  
  this->mrls[num_files] = NULL;
  
  return this->mrls;
}

/*
 *
 */
static int file_plugin_eject_media (input_plugin_t *this_gen) {
  return 1; /* doesn't make sense */
}

/*
 *
 */
static char* file_plugin_get_mrl (input_plugin_t *this_gen) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  return this->mrl;
}

/*
 *
 */
static void file_plugin_close (input_plugin_t *this_gen) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  xprintf (VERBOSE|INPUT, "closing input\n");

  close(this->fh);
  this->fh = -1;
}

/*
 *
 */
static char *file_plugin_get_description (input_plugin_t *this_gen) {
  return "plain file input plugin as shipped with xine";
}

/*
 *
 */
static char *file_plugin_get_identifier (input_plugin_t *this_gen) {
  return "file";
}

/*
 *
 */
static int file_plugin_get_optional_data (input_plugin_t *this_gen, 
					  void *data, int data_type) {
  
  return INPUT_OPTIONAL_UNSUPPORTED;
}

/*
 *
 */
input_plugin_t *init_input_plugin (int iface, config_values_t *config) {
  file_input_plugin_t *this;

  xine_debug = config->lookup_int (config, "xine_debug", 0);

  switch (iface) {
  case 1:
    this = (file_input_plugin_t *) malloc (sizeof (file_input_plugin_t));

    this->input_plugin.interface_version  = INPUT_PLUGIN_IFACE_VERSION;
    this->input_plugin.get_capabilities   = file_plugin_get_capabilities;
    this->input_plugin.open               = file_plugin_open;
    this->input_plugin.read               = file_plugin_read;
    this->input_plugin.read_block         = file_plugin_read_block;
    this->input_plugin.seek               = file_plugin_seek;
    this->input_plugin.get_current_pos    = file_plugin_get_current_pos;
    this->input_plugin.get_length         = file_plugin_get_length;
    this->input_plugin.get_blocksize      = file_plugin_get_blocksize;
    this->input_plugin.get_dir            = file_plugin_get_dir;
    this->input_plugin.eject_media        = file_plugin_eject_media;
    this->input_plugin.get_mrl            = file_plugin_get_mrl;
    this->input_plugin.close              = file_plugin_close;
    this->input_plugin.get_description    = file_plugin_get_description;
    this->input_plugin.get_identifier     = file_plugin_get_identifier;
    this->input_plugin.get_autoplay_list  = NULL;
    this->input_plugin.get_optional_data  = file_plugin_get_optional_data;

    this->fh                     = -1;
    this->mrl                    = NULL;
    this->config                 = config;

    this->mrls = (mrl_t **) malloc(sizeof(mrl_t));
    this->mrls_allocated_entries = 0;

    return (input_plugin_t *) this;
    break;
  default:
    fprintf(stderr,
	    "File input plugin doesn't support plugin API version %d.\n"
	    "PLUGIN DISABLED.\n"
	    "This means there's a version mismatch between xine and this input"
	    "plugin.\nInstalling current input plugins should help.\n",
	    iface);
    return NULL;
  }
}


