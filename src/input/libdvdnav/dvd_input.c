/*
 * Copyright (C) 2002 Samuel Hocevar <sam@zoy.org>,
 *                    Håkan Hjort <d95hjort@dtek.chalmers.se>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "dvd_reader.h"
#include "dvd_input.h"


/* The function pointers that is the exported interface of this file. */
dvd_input_t (*dvdinput_open)  (const char *);
int         (*dvdinput_close) (dvd_input_t);
int         (*dvdinput_seek)  (dvd_input_t, int);
int         (*dvdinput_title) (dvd_input_t, int);
int         (*dvdinput_read)  (dvd_input_t, void *, int, int);
char *      (*dvdinput_error) (dvd_input_t);
int         (*dvdinput_is_encrypted) (dvd_input_t);

#ifdef HAVE_DVDCSS_DVDCSS_H
/* linking to libdvdcss */
#include <dvdcss/dvdcss.h>
#define DVDcss_open(a) dvdcss_open((char*)(a))
#define DVDcss_close   dvdcss_close
#define DVDcss_seek    dvdcss_seek
#define DVDcss_title   dvdcss_title
#define DVDcss_read    dvdcss_read
#define DVDcss_error   dvdcss_error
#else

/* dlopening libdvdcss */
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#else
/* Only needed on MINGW at the moment */
#include "../../msvc/contrib/dlfcn.c"
#endif

#define DVDCSS_SEEK_KEY        (1 << 1)

/* Copied from css.h */
#define KEY_SIZE 5

typedef uint8_t dvd_key_t[KEY_SIZE];

typedef struct dvd_title_s
{
    int                 i_startlb;
    dvd_key_t           p_key;
    struct dvd_title_s *p_next;
} dvd_title_t;

typedef struct css_s
{
    int             i_agid;      /* Current Authenication Grant ID. */
    dvd_key_t       p_bus_key;   /* Current session key. */
    dvd_key_t       p_disc_key;  /* This DVD disc's key. */
    dvd_key_t       p_title_key; /* Current title key. */
} css_t;

/* Copied from libdvdcss.h */

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

struct dvdcss_s
{
    /* File descriptor */
    char * psz_device;
    int    i_fd;
    int    i_read_fd;
    int    i_pos;

    /* File handling */
    void *pf_seek;
    void *pf_read;
    void *pf_readv;

    /* Decryption stuff */
    int          i_method;
    css_t        css;
    int          b_ioctls;
    int          b_scrambled;
    dvd_title_t *p_titles;

    /* Key cache directory and pointer to the filename */
    char   psz_cachefile[PATH_MAX];
    char * psz_block;

    /* Error management */
    char * psz_error;
    int    b_errors;
    int    b_debug;

#ifdef WIN32
    int    b_file;
    char * p_readv_buffer;
    int    i_readv_buf_size;
#endif

#ifndef WIN32
    int    i_raw_fd;
#endif
};


typedef struct dvdcss_s *dvdcss_handle;
static dvdcss_handle (*DVDcss_open)  (const char *);
static int           (*DVDcss_close) (dvdcss_handle);
static int           (*DVDcss_seek)  (dvdcss_handle, int, int);
static int           (*DVDcss_title) (dvdcss_handle, int);
static int           (*DVDcss_read)  (dvdcss_handle, void *, int, int);
static char *        (*DVDcss_error) (dvdcss_handle);
static int           (*DVDcss_is_scrambled) (dvdcss_handle);

#endif

/* The DVDinput handle, add stuff here for new input methods. */
struct dvd_input_s {
  /* libdvdcss handle */
  dvdcss_handle dvdcss;

  /* dummy file input */
  int fd;
};


/**
 * initialize and open a DVD device or file.
 */
static dvd_input_t css_open(const char *target)
{
  dvd_input_t dev;

  /* Allocate the handle structure */
  dev = (dvd_input_t) malloc(sizeof(*dev));
  if(dev == NULL) {
    fprintf(stderr, "libdvdread: Could not allocate memory.\n");
    return NULL;
  }

  /* Really open it with libdvdcss */
  dev->dvdcss = DVDcss_open(target);
  if(dev->dvdcss == 0) {
    fprintf(stderr, "libdvdread: Could not open %s with libdvdcss.\n", target);
    free(dev);
    return NULL;
  }

  return dev;
}

/**
 * return the last error message
 */
static char *css_error(dvd_input_t dev)
{
  return DVDcss_error(dev->dvdcss);
}

/**
 * seek into the device.
 */
static int css_seek(dvd_input_t dev, int blocks)
{
  /* DVDINPUT_NOFLAGS should match the DVDCSS_NOFLAGS value. */
  return DVDcss_seek(dev->dvdcss, blocks, DVDINPUT_NOFLAGS);
}

/**
 * set the block for the begining of a new title (key).
 */
static int css_title(dvd_input_t dev, int block)
{
#ifndef HAVE_DVDCSS_DVDCSS_H
  /* DVDcss_title was removed in libdvdcss 1.3.0 */
  if (!DVDcss_title) {
    return DVDcss_seek(dev->dvdcss, block, DVDCSS_SEEK_KEY);
  }
#endif
  return DVDcss_title(dev->dvdcss, block);
}

/**
 * read data from the device.
 */
static int css_read(dvd_input_t dev, void *buffer, int blocks, int flags)
{
  return DVDcss_read(dev->dvdcss, buffer, blocks, flags);
}

/**
 * close the DVD device and clean up the library.
 */
static int css_close(dvd_input_t dev)
{
  int ret;

  ret = DVDcss_close(dev->dvdcss);

  if(ret < 0)
    return ret;

  free(dev);

  return 0;
}

static int css_is_encrypted (dvd_input_t dev)
{
  if (dev->dvdcss == NULL) {
    return 0;
  }
#ifndef HAVE_DVDCSS_DVDCSS_H
  if (DVDcss_is_scrambled) {
    return DVDcss_is_scrambled(dev->dvdcss);
  }
#endif

  /* this won't work with recent libdvdcss versions ... */
  return dev->dvdcss->b_scrambled;
}



/**
 * initialize and open a DVD device or file.
 */
static dvd_input_t file_open(const char *target)
{
  dvd_input_t dev;

  /* Allocate the library structure */
  dev = (dvd_input_t) malloc(sizeof(*dev));
  if(dev == NULL) {
    fprintf(stderr, "libdvdread: Could not allocate memory.\n");
    return NULL;
  }

  /* Open the device */
#ifndef WIN32
  dev->fd = open(target, O_RDONLY);
#else
  dev->fd = open(target, O_RDONLY | O_BINARY);
#endif
  if(dev->fd < 0) {
    perror("libdvdread: Could not open input");
    free(dev);
    return NULL;
  }

  return dev;
}

/**
 * return the last error message
 */
static char *file_error(dvd_input_t dev)
{
  /* use strerror(errno)? */
  (void)dev;
  return (char *)"unknown error";
}

/**
 * seek into the device.
 */
static int file_seek(dvd_input_t dev, int blocks)
{
  off_t pos;

  pos = lseek(dev->fd, (off_t)blocks * (off_t)DVD_VIDEO_LB_LEN, SEEK_SET);
  if(pos < 0) {
      return pos;
  }
  /* assert pos % DVD_VIDEO_LB_LEN == 0 */
  return (int) (pos / DVD_VIDEO_LB_LEN);
}

/**
 * set the block for the begining of a new title (key).
 */
static int file_title(dvd_input_t dev, int block)
{
  /* FIXME: implement */
  (void)dev;
  (void)block;
  return -1;
}

/**
 * read data from the device.
 */
static int file_read(dvd_input_t dev, void *buffer, int blocks, int flags)
{
  size_t len;
  ssize_t ret;
  char *q = buffer;

  (void)flags;

  len = (size_t)blocks * DVD_VIDEO_LB_LEN;

  while(len > 0) {

    ret = read(dev->fd, q, len);

    if(ret < 0) {
      /* One of the reads failed, too bad.  We won't even bother
       * returning the reads that went ok, and as in the posix spec
       * the file postition is left unspecified after a failure. */
      return ret;
    }

    if(ret == 0) {
      /* Nothing more to read.  Return the whole blocks, if any, that we got.
         and adjust the file possition back to the previous block boundary. */
      size_t bytes = (size_t)blocks * DVD_VIDEO_LB_LEN - len;
      off_t over_read = -(bytes % DVD_VIDEO_LB_LEN);
      /*off_t pos =*/ lseek(dev->fd, over_read, SEEK_CUR);
      /* should have pos % 2048 == 0 */
      return (int) (bytes / DVD_VIDEO_LB_LEN);
    }

    q += ret;
    len -= ret;
  }

  return blocks;
}

/**
 * close the DVD device and clean up.
 */
static int file_close(dvd_input_t dev)
{
  int ret;

  ret = close(dev->fd);

  if(ret < 0)
    return ret;

  free(dev);

  return 0;
}

static int file_is_encrypted (dvd_input_t dev)
{
  /* FIXME: ?? */
  (void)dev;
  return 0;
}

/**
 * Setup read functions with either libdvdcss or minimal DVD access.
 */
int dvdinput_setup(void)
{
  void *dvdcss_library = NULL;
  char **dvdcss_version = NULL;

#ifdef HAVE_DVDCSS_DVDCSS_H
  /* linking to libdvdcss */
  dvdcss_library = &dvdcss_library;  /* Give it some value != NULL */
  /* the DVDcss_* functions have been #defined at the top */
  dvdcss_version = &dvdcss_interface_2;

#else
  /* dlopening libdvdcss */

#ifdef HOST_OS_DARWIN
  dvdcss_library = dlopen("libdvdcss.2.dylib", RTLD_LAZY);
#elif defined(WIN32)
  dvdcss_library = dlopen("libdvdcss.dll", RTLD_LAZY);
#else
  dvdcss_library = dlopen("libdvdcss.so.2", RTLD_LAZY);
#endif

  if(dvdcss_library != NULL) {
#if defined(__OpenBSD__) && !defined(__ELF__)
#define U_S "_"
#else
#define U_S
#endif
    DVDcss_open = (dvdcss_handle (*)(const char*))
      dlsym(dvdcss_library, U_S "dvdcss_open");
    DVDcss_close = (int (*)(dvdcss_handle))
      dlsym(dvdcss_library, U_S "dvdcss_close");
    DVDcss_title = (int (*)(dvdcss_handle, int))
      dlsym(dvdcss_library, U_S "dvdcss_title");
    DVDcss_seek = (int (*)(dvdcss_handle, int, int))
      dlsym(dvdcss_library, U_S "dvdcss_seek");
    DVDcss_read = (int (*)(dvdcss_handle, void*, int, int))
      dlsym(dvdcss_library, U_S "dvdcss_read");
    DVDcss_error = (char* (*)(dvdcss_handle))
      dlsym(dvdcss_library, U_S "dvdcss_error");
    DVDcss_is_scrambled = (int (*)(dvdcss_handle))
      dlsym(dvdcss_library, U_S "dvdcss_is_scrambled");

    dvdcss_version = (char **)dlsym(dvdcss_library, U_S "dvdcss_interface_2");

    if(dlsym(dvdcss_library, U_S "dvdcss_crack")) {
      fprintf(stderr,
              "libdvdread: Old (pre-0.0.2) version of libdvdcss found.\n"
              "libdvdread: You should get the latest version from "
              "http://www.videolan.org/\n" );
      dlclose(dvdcss_library);
      dvdcss_library = NULL;
    } else if(!DVDcss_open  || !DVDcss_close || !DVDcss_seek
              || !DVDcss_read || !DVDcss_error) {
      fprintf(stderr,  "libdvdread: Missing symbols in libdvdcss, "
              "this shouldn't happen !\n");
      dlclose(dvdcss_library);
      dvdcss_library = NULL;
    }
  }
#endif /* HAVE_DVDCSS_DVDCSS_H */

  if(dvdcss_library != NULL) {
    /*
    char *psz_method = getenv( "DVDCSS_METHOD" );
    char *psz_verbose = getenv( "DVDCSS_VERBOSE" );
    fprintf(stderr, "DVDCSS_METHOD %s\n", psz_method);
    fprintf(stderr, "DVDCSS_VERBOSE %s\n", psz_verbose);
    */
    fprintf(stderr, "libdvdread: Using libdvdcss version %s for DVD access\n",
            dvdcss_version ? *dvdcss_version : "?");

    /* libdvdcss wrapper functions */
    dvdinput_open  = css_open;
    dvdinput_close = css_close;
    dvdinput_seek  = css_seek;
    dvdinput_title = css_title;
    dvdinput_read  = css_read;
    dvdinput_error = css_error;
    dvdinput_is_encrypted = css_is_encrypted;
    return 1;

  } else {
    fprintf(stderr, "libdvdread: Encrypted DVD support unavailable.\n");

    /* libdvdcss replacement functions */
    dvdinput_open  = file_open;
    dvdinput_close = file_close;
    dvdinput_seek  = file_seek;
    dvdinput_title = file_title;
    dvdinput_read  = file_read;
    dvdinput_error = file_error;
    dvdinput_is_encrypted = file_is_encrypted;
    return 0;
  }
}
