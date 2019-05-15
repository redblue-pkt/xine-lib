/*
 * Copyright (C) 2000-2019 the xine project
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * stream metainfo helper functions
 * hide some xine engine details from demuxers and reduce code duplication
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

#ifdef HAVE_ICONV
#  include <iconv.h>
#endif

#include <xine/info_helper.h>
#include "xine_private.h"

/* *******************  Stream Info  *************************** */

/*
 * Check if 'info' is in bounds.
 */
static int info_valid (xine_stream_private_t *stream, int info) {
  if ((info >= 0) && (info < XINE_STREAM_INFO_MAX))
    return 1;
  else {
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
      "info_helper: invalid STREAM_INFO %d. Ignored.\n", info);
    return 0;
  }
}

/*
 * Reset private info.
 */
void _x_stream_info_reset (xine_stream_t *s, int info) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  if (info_valid (stream, info)) {
    xine_rwlock_wrlock (&stream->info_lock);
    stream->stream_info[info] = 0;
    xine_rwlock_unlock (&stream->info_lock);
  }
}

/*
 * Reset public info value.
 */
void _x_stream_info_public_reset (xine_stream_t *s, int info) {
  (void)s;
  (void)info;
}

/*
 * Set private info value.
 */
void _x_stream_info_set (xine_stream_t *s, int info, int value) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s, *m;
  m = stream->side_streams[0];
  if (info_valid (m, info)) {
    xine_rwlock_wrlock (&m->info_lock);
    if ((m != stream) &&
      ((info == XINE_STREAM_INFO_HAS_CHAPTERS) ||
       (info == XINE_STREAM_INFO_HAS_VIDEO) ||
       (info == XINE_STREAM_INFO_HAS_AUDIO))) {
      if (m->stream_info[info] == 0)
        m->stream_info[info] = value;
    } else {
      m->stream_info[info] = value;
    }
    xine_rwlock_unlock (&m->info_lock);
  }
}

/*
 * Retrieve private info value.
 */
uint32_t _x_stream_info_get (xine_stream_t *s, int info) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  uint32_t stream_info;
  stream = stream->side_streams[0];
  xine_rwlock_rdlock (&stream->info_lock);
  stream_info = stream->stream_info[info];
  xine_rwlock_unlock (&stream->info_lock);
  return stream_info;
}

/*
 * Retrieve public info value
 */
uint32_t _x_stream_info_get_public (xine_stream_t *s, int info) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  uint32_t stream_info;
  stream = stream->side_streams[0];
  xine_rwlock_rdlock (&stream->info_lock);
  stream_info = stream->stream_info[info];
  xine_rwlock_unlock (&stream->info_lock);
  return stream_info;
}

/* ****************  Meta Info  *********************** */

/*
 * Remove trailing separator chars (\n,\r,\t, space,...)
 * at the end of the string
 */
static void meta_info_chomp(char *str) {
  ssize_t i, len;

  len = strlen(str);
  if (!len)
    return;
  i = len - 1;

  while ((i >= 0) && ((unsigned char)str[i] <= 32)) {
    str[i] = 0;
    i--;
  }
}

/*
 * Check if 'info' is in bounds.
 */
static int meta_valid (xine_stream_private_t *stream, int info) {
  if ((info >= 0) && (info < XINE_STREAM_INFO_MAX))
    return 1;
  else {
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
      "info_helper: invalid META_INFO %d. Ignored.\n", info);
    return 0;
  }
}

/*
 * Set private meta info to utf-8 string value (can be NULL).
 */
static void _meta_info_set_utf8 (xine_stream_private_t *stream, int info, const char *value) {
  if (meta_valid (stream, info)) {
    xine_rwlock_wrlock (&stream->meta_lock);
    if  (( value && !stream->meta_info[info])
      || ( value &&  stream->meta_info[info] && strcmp (value, stream->meta_info[info]))
      || (!value &&  stream->meta_info[info])) {
      if (stream->meta_info_public[info] != stream->meta_info[info])
        free (stream->meta_info[info]);
      stream->meta_info[info] = (value) ? strdup(value) : NULL;
      if (stream->meta_info[info])
        meta_info_chomp (stream->meta_info[info]);
    }
    xine_rwlock_unlock (&stream->meta_lock);
  }
}

#ifdef HAVE_ICONV
static int is_utf8 (const char *s) {
  const uint8_t *p = (const uint8_t *)s;
  while (1) {
    if ((*p & 0x80) == 0x00) {
      if (*p == 0x00)
        break;
      p += 1;
    } else if ((*p & 0xe0) == 0xc0) {
      if ((p[1] & 0xc0) != 0x80)
        return -1;
      p += 2;
    } else if ((*p & 0xf0) == 0xe0) {
      if  (((p[1] & 0xc0) != 0x80)
        || ((p[2] & 0xc0) != 0x80))
        return -1;
      p += 3;
    } else if ((*p & 0xf8) == 0xf0) {
      if  (((p[1] & 0xc0) != 0x80)
        || ((p[2] & 0xc0) != 0x80)
        || ((p[3] & 0xc0) != 0x80))
        return -1;
      p += 4;
    } else if ((*p & 0xfc) == 0xf8) {
      if  (((p[1] & 0xc0) != 0x80)
        || ((p[2] & 0xc0) != 0x80)
        || ((p[3] & 0xc0) != 0x80)
        || ((p[4] & 0xc0) != 0x80))
        return -1;
      p += 5;
    } else if ((*p & 0xfe) == 0xfc) {
      if  (((p[1] & 0xc0) != 0x80)
        || ((p[2] & 0xc0) != 0x80)
        || ((p[3] & 0xc0) != 0x80)
        || ((p[4] & 0xc0) != 0x80)
        || ((p[5] & 0xc0) != 0x80))
        return -1;
      p += 6;
    } else {
      return -1;
    }
  }
  return p - (const uint8_t *)s;
}
#endif

/*
 * Set private meta info to value (can be NULL) with a given encoding.
 * if encoding is NULL assume locale.
 */
static void _meta_info_set_encoding (xine_stream_private_t *stream, int info, const char *value, const char *enc) {
  const char *buf_set = value;
  char *buf_free = NULL;

#ifdef HAVE_ICONV
  char *system_enc = NULL;
  iconv_t cd = (iconv_t)-1;

  do {
    if (!value)
      break;

    if (enc == NULL) {
      if ((enc = system_enc = xine_get_system_encoding()) == NULL) {
        xprintf (stream->s.xine, XINE_VERBOSITY_LOG,
          _("info_helper: can't find out current locale character set\n"));
        break;
      }
    }

    if (strcmp (enc, "UTF-8")) {
      /* Don't bother converting if it's already in UTF-8, but the encoding
       * is badly reported */
      if (is_utf8 (value) >= 0)
        break;
    }

    cd = iconv_open ("UTF-8", enc);
    if (cd == (iconv_t)-1) {
      xprintf (stream->s.xine, XINE_VERBOSITY_LOG,
        _("info_helper: unsupported conversion %s -> UTF-8, no conversion performed\n"), enc);
      break;
    }

    {
      ICONV_CONST char *inbuf;
      char *outbuf;
      size_t inbytesleft, outbytesleft;

      if (!strncmp (enc, "UTF-16", 6) || !strncmp (enc, "UCS-2", 5)) {
        /* strlen() won't work with UTF-16* or UCS-2* */
        inbytesleft = 0;
        while (value[inbytesleft] || value[inbytesleft + 1])
          inbytesleft += 2;
      } /* ... do we need to handle UCS-4? Probably not. */
      else
        inbytesleft = strlen(value);
      outbytesleft = 4 * inbytesleft; /* estimative (max) */
      buf_free = malloc (outbytesleft + 1);
      if (!buf_free)
        break;

      inbuf = (ICONV_CONST char *)value;
      outbuf = buf_free;
      if (iconv (cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft) != (size_t)-1) {
        *outbuf = '\0';
        buf_set = buf_free;
      }
    }
  } while (0);

  if (cd != (iconv_t)-1)
    iconv_close (cd);
  free (system_enc);
#endif

  _meta_info_set_utf8 (stream, info, buf_set);
  free (buf_free);
}

/*
 * Reset (nullify) private info value.
 */
void _x_meta_info_reset (xine_stream_t *s, int info) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  _meta_info_set_utf8 (stream, info, NULL);
}

/*
 * Reset (nullify) public info value.
 */
void _x_meta_info_public_reset (xine_stream_t *s, int info) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  if (meta_valid (stream, info)) {
    xine_rwlock_wrlock (&stream->meta_lock);
    if (stream->meta_info_public[info] != stream->meta_info[info])
      _x_freep (&stream->meta_info[info]);
    xine_rwlock_unlock (&stream->meta_lock);
  }
}

/*
 * Set private meta info value using current locale.
 */
void _x_meta_info_set (xine_stream_t *s, int info, const char *str) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  if (str)
    _meta_info_set_encoding (stream, info, str, NULL);
}

/*
 * Set private meta info value using specified encoding.
 */
void _x_meta_info_set_generic (xine_stream_t *s, int info, const char *str, const char *enc) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  if (str)
    _meta_info_set_encoding (stream, info, str, enc);
}

/*
 * Set private meta info value using utf8.
 */
void _x_meta_info_set_utf8 (xine_stream_t *s, int info, const char *str) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  if (str)
    _meta_info_set_utf8 (stream, info, str);
}

/*
 * Set private meta info from buf, 'len' bytes long.
 */
void _x_meta_info_n_set (xine_stream_t *s, int info, const char *buf, int len) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  if (meta_valid (stream, info) && len) {
    char *str = strndup (buf, len);
    _meta_info_set_encoding (stream, info, str, NULL);
    free(str);
  }
}

/*
 * Set private meta info value, from multiple arguments.
 */
void _x_meta_info_set_multi (xine_stream_t *s, int info, ...) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;

  stream = stream->side_streams[0];
  if (meta_valid (stream, info)) {
    va_list   ap;
    char     *args[1025];
    char     *buf;
    size_t    n, len;

    len = n = 0;

    va_start(ap, info);
    while((buf = va_arg(ap, char *)) && (n < 1024)) {
      len += strlen(buf) + 1;
      args[n] = buf;
      n++;
    }
    va_end(ap);

    args[n] = NULL;

    if(len) {
      char *p, *meta;

      p = meta = (char *) malloc(len + 1);

      n = 0;
      while(args[n]) {
	strcpy(meta, args[n]);
	meta += strlen(args[n]) + 1;
	n++;
      }

      *meta = '\0';

      xine_rwlock_wrlock (&stream->meta_lock);
      if (stream->meta_info_public[info] != stream->meta_info[info])
        free (stream->meta_info[info]);
      stream->meta_info[info] = p;
      if (p)
        meta_info_chomp (p);
      xine_rwlock_unlock (&stream->meta_lock);
    }
  }
}

/*
 * Retrieve private info value.
 */
const char *_x_meta_info_get (xine_stream_t *s, int info) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  const char *meta_info;
  stream = stream->side_streams[0];
  xine_rwlock_rdlock (&stream->meta_lock);
  meta_info = stream->meta_info[info];
  xine_rwlock_unlock (&stream->meta_lock);
  return meta_info;
}

/*
 * Retrieve public info value.
 */
const char *_x_meta_info_get_public (xine_stream_t *s, int info) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  char *pub_meta_info = NULL;
  stream = stream->side_streams[0];
  if (meta_valid (stream, info)) {
    xine_rwlock_rdlock (&stream->meta_lock);
    pub_meta_info = stream->meta_info_public[info];
    if (pub_meta_info != stream->meta_info[info]) {
      xine_rwlock_unlock (&stream->meta_lock);
      xine_rwlock_wrlock (&stream->meta_lock);
      free (pub_meta_info);
      stream->meta_info_public[info] = pub_meta_info = stream->meta_info[info];
    }
    xine_rwlock_unlock (&stream->meta_lock);
  }
  return pub_meta_info;
}

