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
 * $Id: input_file.c,v 1.57 2002/09/06 18:13:11 mroi Exp $
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

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "input_plugin.h"

extern int errno;

#define MAXFILES      65535

#ifdef __GNUC__
#define LOG_MSG_STDERR(xine, message, args...) {                     \
    xine_log(xine, XINE_LOG_MSG, message, ##args);                 \
    fprintf(stderr, message, ##args);                                \
  }
#define LOG_MSG(xine, message, args...) {                            \
    xine_log(xine, XINE_LOG_MSG, message, ##args);                 \
    printf(message, ##args);                                         \
  }
#else
#define LOG_MSG_STDERR(xine, ...) {                                  \
    xine_log(xine, XINE_LOG_MSG, __VA_ARGS__);                     \
    fprintf(stderr, __VA_ARGS__);                                    \
  }
#define LOG_MSG(xine, ...) {                                         \
    xine_log(xine, XINE_LOG_MSG, __VA_ARGS__);                     \
    printf(__VA_ARGS__);                                             \
  }
#endif

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

  xine_t           *xine;
  
  int               fh;
  int               show_hidden_files;
  char             *origin_path;
  FILE             *sub;
  char             *mrl;
  config_values_t  *config;

  int               mrls_allocated_entries;
  xine_mrl_t      **mrls;
  
} file_input_plugin_t;


/* ***************************************************************************
 *                            PRIVATES FUNCTIONS
 */

/*
 * Callback for config changes.
 */
static void hidden_bool_cb(void *data, xine_cfg_entry_t *cfg) {
  file_input_plugin_t *this = (file_input_plugin_t *) data;
  
  this->show_hidden_files = cfg->num_value;
}
static void origin_change_cb(void *data, xine_cfg_entry_t *cfg) {
  file_input_plugin_t *this = (file_input_plugin_t *) data;
  
  this->origin_path = cfg->str_value;
}

/*
 * Sorting function, it comes from GNU fileutils package.
 */
#define S_N        0x0
#define S_I        0x4
#define S_F        0x8
#define S_Z        0xC
#define CMP          2
#define LEN          3
#define ISDIGIT(c)   ((unsigned) (c) - '0' <= 9)
static int strverscmp(const char *s1, const char *s2) {
  const unsigned char *p1 = (const unsigned char *) s1;
  const unsigned char *p2 = (const unsigned char *) s2;
  unsigned char c1, c2;
  int state;
  int diff;
  static const unsigned int next_state[] = {
    S_N, S_I, S_Z, S_N,
    S_N, S_I, S_I, S_I,
    S_N, S_F, S_F, S_F,
    S_N, S_F, S_Z, S_Z
  };
  static const int result_type[] = {
    CMP, CMP, CMP, CMP, CMP, LEN, CMP, CMP,
    CMP, CMP, CMP, CMP, CMP, CMP, CMP, CMP,
    CMP,  -1,  -1, CMP,   1, LEN, LEN, CMP,
      1, LEN, LEN, CMP, CMP, CMP, CMP, CMP,
    CMP, CMP, CMP, CMP, CMP, LEN, CMP, CMP,
    CMP, CMP, CMP, CMP, CMP, CMP, CMP, CMP,
    CMP,   1,   1, CMP,  -1, CMP, CMP, CMP,
     -1, CMP, CMP, CMP
  };

  if(p1 == p2)
    return 0;

  c1 = *p1++;
  c2 = *p2++;

  state = S_N | ((c1 == '0') + (ISDIGIT(c1) != 0));

  while((diff = c1 - c2) == 0 && c1 != '\0') {
    state = next_state[state];
    c1 = *p1++;
    c2 = *p2++;
    state |= (c1 == '0') + (ISDIGIT(c1) != 0);
  }
  
  state = result_type[state << 2 | ((c2 == '0') + (ISDIGIT(c2) != 0))];
  
  switch(state) {
  case CMP:
    return diff;
    
  case LEN:
    while(ISDIGIT(*p1++))
      if(!ISDIGIT(*p2++))
	return 1;
    
    return ISDIGIT(*p2) ? -1 : diff;
    
  default:
    return state;
  }
}

/*
 * Wrapper to strverscmp() for qsort() calls, which sort mrl_t type array.
 */
static int _sortfiles_default(const xine_mrl_t *s1, const xine_mrl_t *s2) {
  return(strverscmp(s1->mrl, s2->mrl));
}

/*
 * Return the type (OR'ed) of the given file *fully named*
 */
static uint32_t get_file_type(char *filepathname, char *origin, xine_t *xine) {
  struct stat  pstat;
  int          mode;
  uint32_t     file_type = 0;
  char         buf[XINE_PATH_MAX + XINE_NAME_MAX + 1];

  if((lstat(filepathname, &pstat)) < 0) {
    sprintf(buf, "%s/%s", origin, filepathname);
    if((lstat(buf, &pstat)) < 0) {
      LOG_MSG(xine, _("lstat failed for %s{%s}\n"), filepathname, origin);
      file_type |= mrl_unknown;
      return file_type;
    }
  }
  
  file_type |= mrl_file;
  
  mode = pstat.st_mode;
  
  if(S_ISLNK(mode))
    file_type |= mrl_file_symlink;
  else if(S_ISDIR(mode))
    file_type |= mrl_file_directory;
  else if(S_ISCHR(mode))
    file_type |= mrl_file_chardev;
  else if(S_ISBLK(mode))
    file_type |= mrl_file_blockdev;
  else if(S_ISFIFO(mode))
    file_type |= mrl_file_fifo;
  else if(S_ISSOCK(mode))
    file_type |= mrl_file_sock;
  else {
    if(S_ISREG(mode)) {
      file_type |= mrl_file_normal;
    }
    if(mode & S_IXUGO)
      file_type |= mrl_file_exec;
  }
  
  if(filepathname[strlen(filepathname) - 1] == '~')
    file_type |= mrl_file_backup;
  
  return file_type;
}

/*
 * Return the file size of the given file *fully named*
 */
static off_t get_file_size(char *filepathname, char *origin) {
  struct stat  pstat;
  char         buf[XINE_PATH_MAX + XINE_NAME_MAX + 1];

  if((lstat(filepathname, &pstat)) < 0) {
    sprintf(buf, "%s/%s", origin, filepathname);
    if((lstat(buf, &pstat)) < 0)
      return (off_t) 0;
  }

  return pstat.st_size;
}
/*
 *                              END OF PRIVATES
 *****************************************************************************/

/*
 *
 */
static uint32_t file_plugin_get_capabilities (input_plugin_t *this_gen) {

  return INPUT_CAP_SEEKABLE | INPUT_CAP_PREVIEW | INPUT_CAP_GET_DIR | INPUT_CAP_SPULANG;
}

/*
 *
 */
static int file_plugin_open (input_plugin_t *this_gen, const char *mrl) {

  char                *filename, *subtitle;
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  if (this->mrl)
    free (this->mrl);

  this->mrl = strdup(mrl);

  if (!strncasecmp (this->mrl, "file://", 7))
    filename = &this->mrl[7];
  else
    filename = this->mrl;

  subtitle = strrchr (filename, '%');
  if (subtitle) {
    *subtitle = 0;
    subtitle++;

    LOG_MSG(this->xine, _("input_file: trying to open subtitle file '%s'\n"),
	    subtitle);

    this->sub = fopen (subtitle, "r");

  } else
    this->sub = NULL;


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
 * helper function to release buffer
 * in case demux thread is cancelled
 */
static void pool_release_buffer (void *arg) {
  buf_element_t *buf = (buf_element_t *) arg;
  if( buf != NULL )
    buf->free_buffer(buf);
}

/*
 *
 */
static buf_element_t *file_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {

  off_t                 num_bytes, total_bytes;
  file_input_plugin_t  *this = (file_input_plugin_t *) this_gen;
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
  pthread_cleanup_push( pool_release_buffer, buf );

  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;
  total_bytes = 0;

  while (total_bytes < todo) {
    pthread_testcancel();
    num_bytes = read (this->fh, buf->mem + total_bytes, todo-total_bytes);
    if (num_bytes <= 0) {
      if (num_bytes < 0) 
	LOG_MSG_STDERR(this->xine, _("input_file: read error (%s)\n"), strerror(errno));
      buf->free_buffer (buf);
      buf = NULL;
      break;
    }
    total_bytes += num_bytes;
  }

  if( buf != NULL )
    buf->size = total_bytes;

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL);
  pthread_cleanup_pop(0);

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

  if (this->fh <0)
    return 0;

  return lseek (this->fh, 0, SEEK_CUR);
}

/*
 *
 */
static off_t file_plugin_get_length (input_plugin_t *this_gen) {

  struct stat          buf ;
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  if (this->fh <0)
    return 0;

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
 * Return 1 is filepathname is a directory, otherwise 0
 */
static int is_a_dir(char *filepathname) {
  struct stat  pstat;
  
  stat(filepathname, &pstat);

  return (S_ISDIR(pstat.st_mode));
}

/*
 *
 */
static const xine_mrl_t *const *file_plugin_get_dir (input_plugin_t *this_gen, 
						     const char *filename, int *nFiles) {
  file_input_plugin_t  *this = (file_input_plugin_t *) this_gen;
  struct dirent        *pdirent;
  DIR                  *pdir;
  xine_mrl_t           *hide_files, *dir_files, *norm_files;
  char                  current_dir[XINE_PATH_MAX + 1];
  char                  current_dir_slashed[XINE_PATH_MAX + 1];
  char                  fullfilename[XINE_PATH_MAX + XINE_NAME_MAX + 1];
  int                   num_hide_files  = 0;
  int                   num_dir_files   = 0;
  int                   num_norm_files  = 0;
  int                   num_files       = -1;
  int                 (*func) ()        = _sortfiles_default;
  int                   already_tried   = 0;

  *nFiles = 0;
  memset(current_dir, 0, sizeof(current_dir));

  /* 
   * No origin location, so got the content of the current directory
   */
  if(!filename) {
    snprintf(current_dir, XINE_PATH_MAX, "%s", this->origin_path);
  }
  else {
    snprintf(current_dir, XINE_PATH_MAX, "%s", filename);
    
    /* Remove exceed '/' */
    while((current_dir[strlen(current_dir) - 1] == '/') && strlen(current_dir) > 1)
      current_dir[strlen(current_dir) - 1] = '\0';
  }

  /* Store new origin path */
 __try_again_from_home:
  
  this->config->update_string(this->config, "input.file_origin_path", current_dir);

  if(strcasecmp(current_dir, "/"))
    sprintf(current_dir_slashed, "%s/", current_dir);
  else
    sprintf(current_dir_slashed, "/");
  
  /*
   * Ooch!
   */
  if((pdir = opendir(current_dir)) == NULL) {

    if(!already_tried) {
      /* Try one more time with user homedir */
      snprintf(current_dir, XINE_PATH_MAX, "%s", xine_get_homedir());
      already_tried++;
      goto __try_again_from_home;
    }

    return NULL;
  }
  
  dir_files  = (xine_mrl_t *) xine_xmalloc(sizeof(xine_mrl_t) * MAXFILES);
  hide_files = (xine_mrl_t *) xine_xmalloc(sizeof(xine_mrl_t) * MAXFILES);
  norm_files = (xine_mrl_t *) xine_xmalloc(sizeof(xine_mrl_t) * MAXFILES);
  
  while((pdirent = readdir(pdir)) != NULL) {
    
    memset(fullfilename, 0, sizeof(fullfilename));
    sprintf(fullfilename, "%s/%s", current_dir, pdirent->d_name);
    
    if(is_a_dir(fullfilename)) {
      
      /* if user don't want to see hidden files, ignore them */
      if(this->show_hidden_files == 0 && 
	 ((strlen(pdirent->d_name) > 1)
	  && (pdirent->d_name[0] == '.' &&  pdirent->d_name[1] != '.'))) {
	;
      }
      else {
	
	dir_files[num_dir_files].mrl    = (char *) 
	  xine_xmalloc(strlen(current_dir_slashed) + 1 + strlen(pdirent->d_name) + 1);
	
	dir_files[num_dir_files].origin = strdup(current_dir);
	sprintf(dir_files[num_dir_files].mrl, "%s%s", 
		current_dir_slashed, pdirent->d_name);
	dir_files[num_dir_files].link   = NULL;
	dir_files[num_dir_files].type   = get_file_type(fullfilename, current_dir, this->xine);
	dir_files[num_dir_files].size   = get_file_size(fullfilename, current_dir);

	/* The file is a link, follow it */
	if(dir_files[num_dir_files].type & mrl_file_symlink) {
	  char linkbuf[XINE_PATH_MAX + XINE_NAME_MAX + 1];
	  int linksize;
	  
	  memset(linkbuf, 0, sizeof(linkbuf));
	  linksize = readlink(fullfilename, linkbuf, XINE_PATH_MAX + XINE_NAME_MAX);
	  
	  if(linksize < 0) {
	    LOG_MSG_STDERR(this->xine, _("%s(%d): readlink() failed: %s\n"), 
			   __XINE_FUNCTION__, __LINE__, strerror(errno));
	  }
	  else {
	    dir_files[num_dir_files].link = (char *) xine_xmalloc(linksize + 1);
	    strncpy(dir_files[num_dir_files].link, linkbuf, linksize);
	    dir_files[num_dir_files].type |= get_file_type(dir_files[num_dir_files].link, current_dir, this->xine);
	  }
	}
	
	num_dir_files++;
      }

    } /* Hmmmm, an hidden file ? */
    else if((strlen(pdirent->d_name) > 1)
	    && (pdirent->d_name[0] == '.' &&  pdirent->d_name[1] != '.')) {

      /* if user don't want to see hidden files, ignore them */
      if(this->show_hidden_files) {

	hide_files[num_hide_files].mrl    = (char *) 
	  xine_xmalloc(strlen(current_dir_slashed) + 1 + strlen(pdirent->d_name) + 1);
	
	hide_files[num_hide_files].origin = strdup(current_dir);
	sprintf(hide_files[num_hide_files].mrl, "%s%s", 
		current_dir_slashed, pdirent->d_name);
	hide_files[num_hide_files].link   = NULL;
	hide_files[num_hide_files].type   = get_file_type(fullfilename, current_dir, this->xine);
	hide_files[num_hide_files].size   = get_file_size(fullfilename, current_dir);
	
	/* The file is a link, follow it */
	if(hide_files[num_hide_files].type & mrl_file_symlink) {
	  char linkbuf[XINE_PATH_MAX + XINE_NAME_MAX + 1];
	  int linksize;
	  
	  memset(linkbuf, 0, sizeof(linkbuf));
	  linksize = readlink(fullfilename, linkbuf, XINE_PATH_MAX + XINE_NAME_MAX);
	  
	  if(linksize < 0) {
	    LOG_MSG_STDERR(this->xine, _("%s(%d): readlink() failed: %s\n"), 
			   __XINE_FUNCTION__, __LINE__, strerror(errno));
	  }
	  else {
	    hide_files[num_hide_files].link = (char *) 
	      xine_xmalloc(linksize + 1);
	    strncpy(hide_files[num_hide_files].link, linkbuf, linksize);
	    hide_files[num_hide_files].type |= get_file_type(hide_files[num_hide_files].link, current_dir, this->xine);
	  }
	}
	
	num_hide_files++;
      }

    } /* So a *normal* one. */
    else {

      norm_files[num_norm_files].mrl    = (char *) 
	xine_xmalloc(strlen(current_dir_slashed) + 1 + strlen(pdirent->d_name) + 1);

      norm_files[num_norm_files].origin = strdup(current_dir);
      sprintf(norm_files[num_norm_files].mrl, "%s%s", 
	      current_dir_slashed, pdirent->d_name);
      norm_files[num_norm_files].link   = NULL;
      norm_files[num_norm_files].type   = get_file_type(fullfilename, current_dir, this->xine);
      norm_files[num_norm_files].size   = get_file_size(fullfilename, current_dir);
      
      /* The file is a link, follow it */
      if(norm_files[num_norm_files].type & mrl_file_symlink) {
	char linkbuf[XINE_PATH_MAX + XINE_NAME_MAX + 1];
	int linksize;
	
	memset(linkbuf, 0, sizeof(linkbuf));
	linksize = readlink(fullfilename, linkbuf, XINE_PATH_MAX + XINE_NAME_MAX);
	
	if(linksize < 0) {
	  LOG_MSG_STDERR(this->xine, _("%s(%d): readlink() failed: %s\n"), 
			 __XINE_FUNCTION__, __LINE__, strerror(errno));
	}
	else {
	  norm_files[num_norm_files].link = (char *) 
	    xine_xmalloc(linksize + 1);
	  strncpy(norm_files[num_norm_files].link, linkbuf, linksize);
	  norm_files[num_norm_files].type |= get_file_type(norm_files[num_norm_files].link, current_dir, this->xine);
	}
      }
      
      num_norm_files++;
    }
    
    num_files++;
  }
  
  closedir(pdir);
  
  /*
   * Ok, there are some files here, so sort
   * them then store them into global mrls array.
   */
  if(num_files > 0) {
    int i;

    num_files = 0;

    /*
     * Sort arrays
     */
    if(num_dir_files)
      qsort(dir_files, num_dir_files, sizeof(xine_mrl_t), func);
    
    if(num_hide_files)
      qsort(hide_files, num_hide_files, sizeof(xine_mrl_t), func);
    
    if(num_norm_files)
      qsort(norm_files, num_norm_files, sizeof(xine_mrl_t), func);
    
    /*
     * Add directories entries
     */
    for(i = 0; i < num_dir_files; i++) {
      
      if(num_files >= this->mrls_allocated_entries) {
	++this->mrls_allocated_entries;
	this->mrls = realloc(this->mrls, (this->mrls_allocated_entries+1) * sizeof(xine_mrl_t*));
	this->mrls[num_files] = (xine_mrl_t *) xine_xmalloc(sizeof(xine_mrl_t));
      }
      else
	memset(this->mrls[num_files], 0, sizeof(xine_mrl_t));
      
      MRL_DUPLICATE(&dir_files[i], this->mrls[num_files]); 

      num_files++;
    }

    /*
     * Add hidden files entries
     */
    for(i = 0; i < num_hide_files; i++) {
      
      if(num_files >= this->mrls_allocated_entries) {
	++this->mrls_allocated_entries;
	this->mrls = realloc(this->mrls, (this->mrls_allocated_entries+1) * sizeof(xine_mrl_t*));
	this->mrls[num_files] = (xine_mrl_t *) xine_xmalloc(sizeof(xine_mrl_t));
      }
      else
	memset(this->mrls[num_files], 0, sizeof(xine_mrl_t));
      
      MRL_DUPLICATE(&hide_files[i], this->mrls[num_files]); 

      num_files++;
    }
    
    /* 
     * Add other files entries
     */
    for(i = 0; i < num_norm_files; i++) {

      if(num_files >= this->mrls_allocated_entries) {
	++this->mrls_allocated_entries;
	this->mrls = realloc(this->mrls, (this->mrls_allocated_entries+1) * sizeof(xine_mrl_t*));
	this->mrls[num_files] = (xine_mrl_t *) xine_xmalloc(sizeof(xine_mrl_t));
      }
      else
	memset(this->mrls[num_files], 0, sizeof(xine_mrl_t));

      MRL_DUPLICATE(&norm_files[i], this->mrls[num_files]); 

      num_files++;
    }
    
    /* Some cleanups before leaving */
    for(i = num_dir_files; i == 0; i--)
      MRL_ZERO(&dir_files[i]);
    free(dir_files);
    
    for(i = num_hide_files; i == 0; i--)
      MRL_ZERO(&hide_files[i]);
    free(hide_files);
    
    for(i = num_norm_files; i == 0; i--)
      MRL_ZERO(&norm_files[i]);
    free(norm_files);
    
  }
  else 
    return NULL;
  
  /*
   * Inform caller about files found number.
   */
  *nFiles = num_files;
  
  /*
   * Freeing exceeded mrls if exists.
   */
  while(this->mrls_allocated_entries > num_files) {
    MRL_ZERO(this->mrls[this->mrls_allocated_entries - 1]);
    free(this->mrls[this->mrls_allocated_entries--]);
  }
  
  /*
   * This is useful to let UI know where it should stops ;-).
   */
  this->mrls[num_files] = NULL;

  /*
   * Some debugging info
   */
  /*
  {
    int j = 0;
    while(this->mrls[j]) {
      printf("mrl[%d] = '%s'\n", j, this->mrls[j]->mrl);
      j++;
    }
  }
  */

  return (const xine_mrl_t *const *)this->mrls;
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

  close(this->fh);
  this->fh = -1;

  if (this->sub) {
    fclose (this->sub);
    this->sub = NULL;
  }
}

/*
 *
 */
static void file_plugin_stop (input_plugin_t *this_gen) {

  file_plugin_close(this_gen);
}

/*
 *
 */
static char *file_plugin_get_description (input_plugin_t *this_gen) {
  return _("plain file input plugin as shipped with xine");
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
  
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

#ifdef LOG
  LOG_MSG(this->xine, _("input_file: get optional data, type %08x, sub %p\n"),
	  data_type, this->sub);
#endif

  switch(data_type) {
  case INPUT_OPTIONAL_DATA_TEXTSPU0:
    if(this->sub) {
      FILE **tmp;
      
      /* dirty hacks... */
      tmp = data;
      *tmp = this->sub;
      
      return INPUT_OPTIONAL_SUCCESS;
    }
    break;
    
  case INPUT_OPTIONAL_DATA_SPULANG:
    sprintf(data, "%3s", (this->sub) ? "on" : "off");
    return INPUT_OPTIONAL_SUCCESS;
    break;
    
  default:
    return INPUT_OPTIONAL_UNSUPPORTED;
    break;

  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static void file_plugin_dispose (input_plugin_t *this_gen ) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  if (this->mrl)
    free (this->mrl);

  free (this->mrls);
  free (this);
}

static void *init_input_plugin (xine_t *xine, void *data) {

  file_input_plugin_t *this;
  config_values_t     *config;

  this       = (file_input_plugin_t *) xine_xmalloc (sizeof (file_input_plugin_t));
  config     = xine->config;
  this->xine = xine;

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
  this->input_plugin.stop               = file_plugin_stop;
  this->input_plugin.get_description    = file_plugin_get_description;
  this->input_plugin.get_identifier     = file_plugin_get_identifier;
  this->input_plugin.get_autoplay_list  = NULL;
  this->input_plugin.get_optional_data  = file_plugin_get_optional_data;
  this->input_plugin.dispose            = file_plugin_dispose;
  this->input_plugin.is_branch_possible = NULL;

  this->fh                     = -1;
  this->sub                    = NULL;
  this->mrl                    = NULL;
  this->config                 = config;
  
  this->mrls = (xine_mrl_t **) xine_xmalloc(sizeof(xine_mrl_t*));
  this->mrls_allocated_entries = 0;

  {
    char current_dir[XINE_PATH_MAX + 1];
    
    if(getcwd(current_dir, sizeof(current_dir)) == NULL)
      strcpy(current_dir, ".");

    this->origin_path = config->register_string(this->config, "input.file_origin_path",
						current_dir, _("origin path to grab file mrls"),
						NULL, 0, origin_change_cb, (void *) this);
  }
  
  this->show_hidden_files = this->config->register_bool(this->config, "input.file_hidden_files", 
							1, _("hidden files displaying."),
							NULL, 10, hidden_bool_cb, (void *) this);
  
  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_INPUT, 8, "file", XINE_VERSION_CODE, NULL, init_input_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

