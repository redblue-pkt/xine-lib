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
 * input plugin for http network streams
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <ctype.h>
#include <errno.h>
#include <zlib.h>

#ifdef WIN32
#include <winsock.h>
#endif

#define LOG_MODULE "input_http"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>
#include "tls/xine_tls.h"
#include "net_buf_ctrl.h"
#include "group_network.h"
#include "http_helper.h"
#include "input_helper.h"

#define BUFSIZE                 1024

#define DEFAULT_HTTP_PORT         80
#define DEFAULT_HTTPS_PORT       443

static inline void uint64_2str (char **s, uint64_t v) {
  uint8_t b[44], *t = b + 21, *q = (uint8_t *)*s;
  uint32_t u;
  *t = 0;
  while (v & ((uint64_t)0xffffffff << 32)) {
    *--t = v % 10u + '0';
    v /= 10u;
  }
  u = v;
  do {
    *--t = u % 10u + '0';
    u /= 10u;
  } while (u);
  memcpy (q, t, 21);
  *s = (char *)(q + (b + 21 - t));
}

static inline void uint32_2str (char **s, uint32_t u) {
  uint8_t b[24], *t = b + 12, *q = (uint8_t *)*s;
  *t = 0;
  do {
    *--t = u % 10u + '0';
    u /= 10u;
  } while (u);
  memcpy (q, t, 12);
  *s = (char *)(q + (b + 12 - t));
}

static inline uint64_t str2uint64 (uint8_t **s) {
  uint8_t *p = *s;
  uint64_t v = 0;
  uint8_t z;
  while ((z = *p ^ '0') < 10)
    v = (v << 3) + (v << 1) + z, p++;
  *s = p;
  return v;
}

static inline uint32_t str2uint32 (uint8_t **s) {
  uint8_t *p = *s, z;
  uint32_t v = 0;
  while ((z = *p ^ '0') < 10)
    v = v * 10u + z, p++;
  *s = p;
  return v;
}

static inline uint32_t hexstr2uint32 (uint8_t **s) {
  static const int8_t tab_unhex[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
     0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  };
  uint8_t *p = *s;
  uint32_t v = 0;
  int32_t z;
  while ((z = tab_unhex[*p]) >= 0)
    v = (v << 4) | z, p++;
  *s = p;
  return v;
}
  
typedef struct {
  input_plugin_t   input_plugin;

  xine_stream_t   *stream;
  xine_t          *xine;

  nbc_t           *nbc;

  off_t            curpos;
  off_t            contentlength;
  off_t            range_start;
  off_t            range_end;
  off_t            range_total;

  char            *mime_type;
  const char      *user_agent;

  xine_url_t       url;
  xine_url_t       proxyurl;

  xine_tls_t      *tls;

  FILE            *head_dump_file;

  int              use_proxy;
  int              use_tls;
  int              again;
  int              ret;
  int              fh;

  /* set to 1 if server replied with Accept-Ranges: bytes */
  uint32_t         accept_range:1;
  /** Set to 1 if the stream is a NSV stream. */
  uint32_t         is_nsv:1;
  /** Set to 1 if the stream comes from last.fm. */
  uint32_t         is_lastfm:1;
  /** Set to 1 if the stream is ShoutCast. */
  uint32_t         shoutcast_mode:1;

  uint32_t         sgot;
  uint32_t         sdelivered;
  uint32_t         schunkleft;
  uint32_t         zgot;
  uint32_t         zskip;
#define MODE_CHUNKED     1
#define MODE_DEFLATED    2
#define MODE_INFLATING   4
#define MODE_EOF         8
#define MODE_DONE       16
#define MODE_HAS_TYPE   32
#define MODE_HAS_LENGTH 64
  uint32_t         smode;
  uint32_t         status;

  z_stream         z_state;

  /* avoid annoying ui messages on fragment streams */
  int              num_msgs;

  /* ShoutCast */
  int              shoutcast_metaint;
  off_t            shoutcast_pos;
  char            *shoutcast_songtitle;

  uint8_t          zbuf[32 << 10];
  uint8_t          zbuf_pad[4];
  uint8_t          sbuf[32 << 10];
  uint8_t          sbuf_pad[4];

  off_t            preview_size;
  char             preview[MAX_PREVIEW_SIZE];

  char             mrl[4096];
} http_input_plugin_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;

  const char       *proxyhost;
  int               proxyport;

  int               prot_version;

  const char       *proxyuser;
  const char       *proxypassword;
  const char       *noproxylist;

  const char       *head_dump_name;
} http_input_class_t;

static void sbuf_init (http_input_plugin_t *this) {
  this->zgot = 0;
  this->zskip = 0;
  this->sgot = 0;
  this->sdelivered = 0;
  this->schunkleft = 0;
  this->smode = 0;
}

static void sbuf_reset (http_input_plugin_t *this) {
  this->zgot = 0;
  this->zskip = 0;
  this->sgot = 0;
  this->sdelivered = 0;
  this->schunkleft = 0;
  if (this->smode & MODE_INFLATING) {
    this->z_state.next_in = this->sbuf;
    this->z_state.avail_in = 0;
    this->z_state.next_out = this->sbuf;
    this->z_state.avail_out = 0;
    inflateEnd (&this->z_state);
  }
  this->smode = 0;
}

static int32_t sbuf_get_string (http_input_plugin_t *this, uint8_t **buf) {
  uint8_t *p = *buf = this->sbuf + this->sdelivered;
  while (1) {
    /* find end of line or data */
    {
      uint8_t *stop = this->sbuf + this->sgot;
      *stop = '\n';
      while (*p != '\n')
        p++;
      /* found */
      if (p != stop) {
        size_t n = p - *buf + 1;
        if (this->head_dump_file)
          fwrite (*buf, 1, n, this->head_dump_file);
        this->sdelivered += n;
        n--;
        if (n && (p[-1] == '\r'))
          p--, n--;
        *p = 0;
        return n;
      }
    }
    /* move to beginning of buffer */
    if (this->sdelivered) {
      size_t n = this->sgot - this->sdelivered;
      if (n) {
        if (this->sdelivered < n)
          memmove (this->sbuf, this->sbuf + this->sdelivered, n);
        else
          memcpy (this->sbuf, this->sbuf + this->sdelivered, n);
      }
      *buf = p = this->sbuf;
      p += n;
      this->sgot = n;
      this->sdelivered = 0;
    }
    {
      uint32_t n = sizeof (this->sbuf) - this->sgot;
      /* buffer full */
      if (!n) {
        this->sgot = 0;
        return -1;
      }
      /* refill fast buffer */
      {
        int32_t i = _x_tls_part_read (this->tls, p, 1, n);
        if (i <= 0) {
          this->smode |= MODE_EOF;
          return -1;
        }
        this->sgot += i;
      }
    }
  }
}

/* zlib dislikes that common 1f 8b 08 00 00 00 00 00 00 00.
 * checksum 0 _is_ wrong but also flagged unset :-/ */
static int sbuf_skip_gzip_head (uint8_t *buf, uint32_t len) {
  uint8_t *b = buf, *stop = b + len;
  uint32_t flags;

  if (len < 10)
    return 0;
  if ((b[0] != 0x1f) || (b[1] != 0x8b))
    return 0;
  if (b[2] != 0x08)
    return 0;
  flags = b[3];
  b += 10; /* timestamp, XFL, OS */
  if (flags & 4) {
    uint32_t len = ((uint32_t)b[1] << 8) | b[0];
    b += 2 + len;
    if (b > stop)
      return 0; /* extra data */
  }
  buf[len] = 0;
  if (flags & 8) {
    while (*b++) ;
    if (b > stop)
      return 0; /* file name */
  }
  if (flags & 16) {
    while (*b++) ;
    if (b > stop)
      return 0; /* comment */
  }
  if (flags & 2) {
    b += 2;
    if (b > stop)
      return 0; /* CRC16 */
  }
  return b - buf;
}

static int sbuf_get_bytes (http_input_plugin_t *this, uint8_t *buf, size_t len) {
  uint8_t *q = buf;
  size_t left = len;

  switch (this->smode & (MODE_CHUNKED | MODE_DEFLATED)) {

    case 0:
      /* get from fast buffer */
      do {
        uint32_t fast = this->sgot - this->sdelivered;
        if (fast == 0)
          break;
        if (fast > left) {
          memcpy (q, this->sbuf + this->sdelivered, left);
          this->sdelivered += left;
          return left;
        }
        memcpy (q, this->sbuf + this->sdelivered, fast);
        q += fast;
        left -= fast;
        this->sgot = this->sdelivered = 0;
      } while (0);
      /* get the usual way */
      if (left > 0) {
        ssize_t r = _x_tls_read (this->tls, q, left);
        if (r <= 0)
          break;
        q += r;
        left -= r;
      }
      break;

    case MODE_CHUNKED + MODE_DEFLATED:
      if (this->smode & MODE_DONE)
        return 0;
      this->z_state.next_out = q;
      this->z_state.avail_out = left;
      while (left > 0) {
        if (this->schunkleft == 0) {
          uint8_t *p;
          int32_t n = sbuf_get_string (this, &p);
          if (n == 0)
            n = sbuf_get_string (this, &p);
          if (n > 0) {
            this->schunkleft = hexstr2uint32 (&p);
            if (this->schunkleft == 0)
              this->smode |= MODE_EOF;
          } else {
            this->smode |= MODE_EOF;
          }
        }
        {
          uint32_t bytes = this->sgot - this->sdelivered;
          if (bytes > this->schunkleft)
            bytes = this->schunkleft;
          if (bytes > sizeof (this->zbuf) - this->zgot)
            bytes = sizeof (this->zbuf) - this->zgot;
          if (bytes > 0) {
            memcpy (this->zbuf + this->zgot, this->sbuf + this->sdelivered, bytes);
            this->zgot += bytes;
            this->sdelivered += bytes;
            this->schunkleft -= bytes;
          }
          bytes = sizeof (this->zbuf) - this->zgot;
          if (bytes > this->schunkleft)
            bytes = this->schunkleft;
          if (bytes > 0) {
            int32_t r = _x_tls_read (this->tls, this->zbuf + this->zgot, bytes);
            if (r > 0)
              this->zgot += r;
            else
              this->smode |= MODE_EOF;
          }
        }
        if ((this->zgot < sizeof (this->zbuf) - 32) && !(this->smode & MODE_EOF))
          continue;
        this->z_state.next_in = this->zbuf;
        this->z_state.avail_in = this->zgot;
        if ((this->smode & MODE_INFLATING) == 0) {
          uint32_t head_len = sbuf_skip_gzip_head (this->zbuf, this->zgot);
          this->zskip = head_len;
          this->z_state.next_in += head_len;
          this->z_state.avail_in -= head_len;
          this->z_state.zalloc = NULL;
          this->z_state.zfree  = NULL;
          this->z_state.opaque = NULL;
          if (inflateInit2 (&this->z_state, -15) != Z_OK) {
            this->smode |= MODE_DONE;
            break;
          }
          this->smode |= MODE_INFLATING;
        }
        {
          int32_t bytes_in = this->z_state.avail_in;
          int32_t bytes_out = this->z_state.avail_out;
          int z_ret_code1 = inflate (&this->z_state, Z_SYNC_FLUSH);
          if ((z_ret_code1 != Z_OK) && (z_ret_code1 != Z_STREAM_END)) {
            xprintf (this->xine, XINE_VERBOSITY_DEBUG,
              "input_http: zlib error #%d.\n", z_ret_code1);
            this->smode |= MODE_DONE;
            break;
          }
          bytes_in -= this->z_state.avail_in;
          if (bytes_in < 0)
            break;
          bytes_in += this->zskip;
          if (bytes_in > 0) {
            int32_t stay = this->zgot - bytes_in;
            if (stay > 0) {
              if (stay <= bytes_in)
                memcpy (this->zbuf, this->zbuf + bytes_in, stay);
              else
                memmove (this->zbuf, this->zbuf + bytes_in, stay);
            }
            this->zgot = stay;
            this->zskip = 0;
          }
          bytes_out -= this->z_state.avail_out;
          if (bytes_out < 0)
            break;
          q += bytes_out;
          left -= bytes_out;
          if (z_ret_code1 == Z_STREAM_END) {
            this->smode |= MODE_DONE;
            break;
          }
        }
      }
      if ((this->smode & (MODE_DONE | MODE_INFLATING)) == (MODE_DONE | MODE_INFLATING)) {
        int bytes_out;
        bytes_out = this->z_state.avail_out;
        inflateEnd (&this->z_state);
        bytes_out -= this->z_state.avail_out;
        if (bytes_out > 0)
          q += bytes_out;
        this->smode &= ~MODE_INFLATING;
      }
      break;

    case MODE_CHUNKED:
      if (this->smode & MODE_DONE)
        return 0;
      while (left > 0) {
        uint32_t fast, chunkleft;
        if (this->schunkleft == 0) {
          uint8_t *p;
          int32_t n;
          if (this->smode & MODE_DONE)
            return 0;
          n = sbuf_get_string (this, &p);
          if (n == 0)
            n = sbuf_get_string (this, &p);
          if (n <= 0)
            break;
          this->schunkleft = hexstr2uint32 (&p);
          if (this->schunkleft == 0) {
            this->smode |= MODE_DONE;
            break;
          }
        }
        chunkleft = left < this->schunkleft ? left : this->schunkleft;
        fast = this->sgot - this->sdelivered;
        if (fast > chunkleft)
          fast = chunkleft;
        if (fast > 0) {
          /* get from fast buffer */
          memcpy (q, this->sbuf + this->sdelivered, fast);
          q += fast;
          left -= fast;
          this->schunkleft -= fast;
          this->sdelivered += fast;
          if (this->sgot == this->sdelivered)
            this->sgot = this->sdelivered = 0;
        } else {
          /* get the usual way */
          ssize_t r = _x_tls_read (this->tls, q, chunkleft);
          if (r <= 0) {
            this->smode |= MODE_EOF | MODE_DONE;
            break;
          }
          q += r;
          this->schunkleft -= r;
          left -= r;
        }
      }
      break;

    case MODE_DEFLATED:
      if (this->smode & MODE_DONE)
        return 0;
      this->z_state.next_out = q;
      this->z_state.avail_out = left;
      while (left > 0) {
        if (this->sdelivered > 0) {
          uint32_t bytes = this->sgot - this->sdelivered;
          if (bytes > 0) {
            if (bytes <= this->sdelivered)
              memcpy (this->sbuf, this->sbuf + this->sdelivered, bytes);
            else
              memmove (this->sbuf, this->sbuf + this->sdelivered, bytes);
          }
          this->sgot = bytes;
          this->sdelivered = 0;
        }
        if ((this->sgot < sizeof (this->sbuf) - 32) && !(this->smode & MODE_EOF)) {
          ssize_t r = _x_tls_read (this->tls, this->sbuf + this->sgot, sizeof (this->sbuf) - this->sgot);
          if (r > 0)
            this->sgot += r;
          else
            this->smode |= MODE_EOF;
        }
        this->z_state.next_in = this->sbuf;
        this->z_state.avail_in = this->sgot;
        if (!(this->smode & MODE_INFLATING)) {
          uint32_t head_len = sbuf_skip_gzip_head (this->sbuf, this->sgot);
          this->sdelivered = head_len;
          this->z_state.next_in += head_len;
          this->z_state.avail_in -= head_len;
          this->z_state.zalloc = NULL;
          this->z_state.zfree  = NULL;
          this->z_state.opaque = NULL;
          if (inflateInit2 (&this->z_state, -15) != Z_OK) {
            this->smode |= MODE_DONE;
            break;
          }
          this->smode |= MODE_INFLATING;
        }
        {
          int32_t bytes_in = this->z_state.avail_in;
          int32_t bytes_out = this->z_state.avail_out;
          int z_ret_code1 = inflate (&this->z_state, Z_SYNC_FLUSH);
          if ((z_ret_code1 != Z_OK) && (z_ret_code1 != Z_STREAM_END)) {
            xprintf (this->xine, XINE_VERBOSITY_DEBUG,
              "input_http: zlib error #%d.\n", z_ret_code1);
            this->smode |= MODE_DONE;
            break;
          }
          bytes_in -= this->z_state.avail_in;
          if (bytes_in < 0)
            break;
          this->sdelivered += bytes_in;
          bytes_out -= this->z_state.avail_out;
          if (bytes_out < 0)
            break;
          q += bytes_out;
          left -= bytes_out;
          if (z_ret_code1 == Z_STREAM_END) {
            this->smode |= MODE_DONE;
            break;
          }
        }
      }
      if ((this->smode & (MODE_DONE | MODE_INFLATING)) == (MODE_DONE | MODE_INFLATING)) {
        int bytes_out;
        bytes_out = this->z_state.avail_out;
        inflateEnd (&this->z_state);
        bytes_out -= this->z_state.avail_out;
        if (bytes_out > 0)
          q += bytes_out;
        this->smode &= ~MODE_INFLATING;
      }
      break;
  }
  return q - buf;
}

static void proxy_host_change_cb (void *this_gen, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *)this_gen;

  this->proxyhost = cfg->str_value;
}

static void proxy_port_change_cb(void *this_gen, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *)this_gen;

  this->proxyport = cfg->num_value;
}

static void proxy_user_change_cb(void *this_gen, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *)this_gen;

  this->proxyuser = cfg->str_value;
}

static void proxy_password_change_cb(void *this_gen, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *)this_gen;

  this->proxypassword = cfg->str_value;
}

static void no_proxy_list_change_cb(void *this_gen, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *)this_gen;

  this->noproxylist = cfg->str_value;
}

static void prot_version_change_cb (void *this_gen, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *)this_gen;

  this->prot_version = cfg->num_value;
}

static void head_dump_name_change_cb (void *this_gen, xine_cfg_entry_t *cfg) {
  http_input_class_t *this = (http_input_class_t *)this_gen;

  this->head_dump_name = cfg->str_value;
}

/*
 * handle no-proxy list config option and returns, if use the proxy or not
 * if error occurred, is expected using the proxy
 */
static int _x_use_proxy(xine_t *xine, http_input_class_t *this, const char *host) {
  const char *target;
  char *no_proxy, *domain, *ptr = NULL;
  struct hostent *info;
  size_t i = 0, host_len, noprox_len;

  /*
   * get full host name
   */
  if ((info = gethostbyname(host)) == NULL) {
    xine_log(xine, XINE_LOG_MSG,
        _("input_http: gethostbyname(%s) failed: %s\n"), host,
        hstrerror(h_errno));
    return 1;
  }
  if (!info->h_name) return 1;

  if ( info->h_addr_list[0] ) {
    /* \177\0\0\1 is the *octal* representation of 127.0.0.1 */
    if ( info->h_addrtype == AF_INET && !memcmp(info->h_addr_list[0], "\177\0\0\1", 4) ) {
      lprintf("host '%s' is localhost\n", host);
      return 0;
    }
    /* TODO: IPv6 check */
  }

  target = info->h_name;

  host_len = strlen(target);
  no_proxy = strdup(this->noproxylist);
  domain = strtok_r(no_proxy, ",", &ptr);
  while (domain) {
    /* skip leading spaces */
    while (isspace (*domain))
      ++domain;
    /* only check for matches if we've not reached the end of the token */
    if (*domain) {
      /* special-case domain beginning with '=' -> is a host name */
      if (domain[0] == '=' && strcmp(target, domain + 1) == 0) {
	lprintf("host '%s' is in no-proxy domain '%s'\n", target, domain);
        free(no_proxy);
	return 0;
      }
      noprox_len = strlen(domain);
      /* special-case host==domain, avoiding dot checks */
      if (host_len == noprox_len && strcmp(target, domain) == 0) {
	lprintf("host '%s' is in no-proxy domain '%s'\n", target, domain);
        free(no_proxy);
	return 0;
      }
      /* check for host in domain, and require that (if matched) the domain
       * name is preceded by a dot, either in the host or domain strings,
       * e.g. "a.foo.bar" is in "foo.bar" and ".foo.bar" but not "o.bar"
       */
      if (host_len > noprox_len
	  && (domain[0] == '.' || target[host_len - noprox_len - 1] == '.')
	  && strcmp(target + host_len - noprox_len, domain) == 0) {
	lprintf("host '%s' is in no-proxy domain '%s'\n", target, domain);
        free(no_proxy);
	return 0;
      }
      lprintf("host '%s' isn't in no-proxy domain '%s'\n", target, domain);
    }
    domain = strtok_r(NULL, ",", &ptr);
    i++;
  }
  free(no_proxy);

  return 1;
}

static void http_plugin_basicauth (const char *user, const char *password, char** dest) {
  const size_t totlen = strlen(user) + (password ? strlen(password) : 0) + 1;
  const size_t enclen = ((totlen + 2) * 4 ) / 3 + 12;
  char         tmp[totlen + 5];

  snprintf(tmp, totlen + 1, "%s:%s", user, password ? : "");

  *dest = malloc(enclen);
  xine_base64_encode(tmp, *dest, totlen);
}

static int http_plugin_read_metainf (http_input_plugin_t *this) {

  char metadata_buf[255 * 16];
  unsigned char len = 0;
  char *title_end;
  const char *songtitle;
  const char *radio;
  xine_event_t uevent;
  xine_ui_data_t data;

  /* get the length of the metadata */
  if (sbuf_get_bytes (this, (char*)&len, 1) != 1)
    return 0;

  lprintf ("http_plugin_read_metainf: len=%d\n", len);

  if (len > 0) {
    if (sbuf_get_bytes (this, metadata_buf, len * 16) != (len * 16))
      return 0;

    metadata_buf[len * 16] = '\0';

    lprintf ("http_plugin_read_metainf: %s\n", metadata_buf);

    if (!this->stream)
      return 1;

    /* Extract the title of the current song */
    if ((songtitle = strstr(metadata_buf, "StreamTitle="))) {
      char terminator[] = { ';', 0, 0 };
      songtitle += 12; /* skip "StreamTitle=" */
      if (*songtitle == '\'' || *songtitle == '"') {
        terminator[0] = *songtitle++;
        terminator[1] = ';';
      }
      if ((title_end = strstr(songtitle, terminator))) {
        *title_end = '\0';

        if ((!this->shoutcast_songtitle ||
             (strcmp(songtitle, this->shoutcast_songtitle))) &&
            (strlen(songtitle) > 0)) {

          lprintf ("http_plugin_read_metainf: songtitle: %s\n", songtitle);

          free(this->shoutcast_songtitle);
          this->shoutcast_songtitle = strdup(songtitle);

          _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, songtitle);

          /* prepares the event */
          radio = _x_meta_info_get(this->stream, XINE_META_INFO_ALBUM);
          if (radio) {
	    snprintf (data.str, sizeof(data.str), "%s - %s", radio, songtitle);
          } else {
            strncpy(data.str, songtitle, sizeof(data.str)-1);
          }
          data.str[sizeof(data.str) - 1] = '\0';
          data.str_len = strlen(data.str) + 1;

          /* sends the event */
          uevent.type = XINE_EVENT_UI_SET_TITLE;
          uevent.stream = this->stream;
          uevent.data = &data;
          uevent.data_length = sizeof(data);
          xine_event_send(this->stream, &uevent);
        }
      }
    }
  }
  return 1;
}

/*
 * Handle shoutcast packets
 */
static off_t http_plugin_read_int (http_input_plugin_t *this,
                                   char *buf, off_t total) {
  int read_bytes = 0;
  int nlen;

  lprintf("total=%"PRId64"\n", total);
  while (total) {
    nlen = total;
    if (this->shoutcast_mode &&
        ((this->shoutcast_pos + nlen) >= this->shoutcast_metaint)) {
      nlen = this->shoutcast_metaint - this->shoutcast_pos;

      nlen = sbuf_get_bytes (this, &buf[read_bytes], nlen);
      if (nlen < 0)
        goto error;

      if (!http_plugin_read_metainf(this))
        goto error;
      this->shoutcast_pos = 0;

    } else {
      nlen = sbuf_get_bytes (this, &buf[read_bytes], nlen);
      if (nlen < 0)
        goto error;

      /* Identify SYNC string for last.fm, this is limited to last.fm
       * streaming servers to avoid hitting on tracks metadata for other
       * servers.
       */
      if ( this->is_lastfm &&
           memmem(&buf[read_bytes], nlen, "SYNC", 4) != NULL &&
           this->stream != NULL) {
	/* Tell frontend to update the UI */
	const xine_event_t event = {
	  .type = XINE_EVENT_UI_CHANNELS_CHANGED,
	  .stream = this->stream,
	  .data = NULL,
	  .data_length = 0
	};

	lprintf("SYNC from last.fm server received\n");

	xine_event_send(this->stream, &event);
      }

      this->shoutcast_pos += nlen;
    }

    /* end of file */
    if (nlen == 0)
      return read_bytes;
    read_bytes          += nlen;
    total               -= nlen;
  }
  return read_bytes;

error:
  if (this->stream && !_x_action_pending(this->stream))
    _x_message (this->stream, XINE_MSG_READ_ERROR, this->url.host, NULL);
  xine_log (this->xine, XINE_LOG_MSG, _("input_http: read error %d\n"), errno);
  return read_bytes;
}

static off_t http_plugin_read (input_plugin_t *this_gen,
                               void *buf_gen, off_t nlen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  char *buf = (char *)buf_gen;
  off_t n, num_bytes;

  num_bytes = 0;

  if (nlen < 0)
    return -1;

  if (this->curpos < this->preview_size) {

    if (nlen > (this->preview_size - this->curpos))
      n = this->preview_size - this->curpos;
    else
      n = nlen;

    lprintf ("%"PRId64" bytes from preview (which has %"PRId64" bytes)\n", n, this->preview_size);
    memcpy (buf, &this->preview[this->curpos], n);

    num_bytes += n;
    this->curpos += n;
  }

  n = nlen - num_bytes;

  if (n > 0) {
    int read_bytes;
    read_bytes = http_plugin_read_int (this, &buf[num_bytes], n);

    if (read_bytes < 0)
      return read_bytes;

    num_bytes += read_bytes;
    this->curpos += read_bytes;
  }

  return num_bytes;
}

static int resync_nsv(http_input_plugin_t *this) {
  uint8_t c;
  int pos = 0;
  int read_bytes = 0;

  lprintf("resyncing NSV stream\n");
  while ((pos < 3) && (read_bytes < (1024*1024))) {

    if (http_plugin_read_int(this, (char*)&c, 1) != 1)
      return 1;

    this->preview[pos] = c;
    switch (pos) {
      case 0:
        if (c == 'N')
          pos++;
        break;
      case 1:
        if (c == 'S')
          pos++;
        else
          if (c != 'N')
            pos = 0;
        break;
      case 2:
        if (c == 'V')
          pos++;
        else
          if (c == 'N')
            pos = 1;
          else
            pos = 0;
        break;
    }
    read_bytes++;
  }
  if (pos == 3) {
    lprintf("NSV stream resynced\n");
  } else {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG,
      "http: cannot resync NSV stream!\n");
    return 0;
  }

  return 1;
}

static off_t http_plugin_get_length (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  return this->contentlength;
}

static uint32_t http_plugin_get_capabilities (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  uint32_t caps = INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW | INPUT_CAP_NEW_MRL;

  /* Nullsoft asked to not allow saving streaming nsv files */
  if (this->url.uri && strlen(this->url.uri) >= 4 &&
      !strncmp(this->url.uri + strlen(this->url.uri) - 4, ".nsv", 4))
    caps |= INPUT_CAP_RIP_FORBIDDEN;

  if (this->accept_range) {
    caps |= INPUT_CAP_SLOW_SEEKABLE;
  }
  return caps;
}

static off_t http_plugin_get_current_pos (input_plugin_t *this_gen){
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  return this->curpos;
}

static void http_close(http_input_plugin_t * this)
{
  _x_tls_deinit (&this->tls);
  if (this->fh >= 0) {
    _x_io_tcp_close (this->stream, this->fh);
    this->fh = -1;
  }
  _x_url_cleanup (&this->proxyurl);
  _x_url_cleanup (&this->url);
}

static int http_restart(http_input_plugin_t * this, off_t abs_offset)
{
  /* save old stream */
  xine_tls_t *old_tls = this->tls;
  off_t old_pos = this->curpos;
  int old_fh = this->fh;

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
    LOG_MODULE ": seek to %" PRId64 ": reconnecting ...\n", (int64_t)abs_offset);

  do {
    this->tls = NULL;
    this->fh = -1;
    _x_url_cleanup (&this->proxyurl);
    _x_url_cleanup (&this->url);

    this->curpos = abs_offset;
    if (this->input_plugin.open (&this->input_plugin) != 1) {
      xprintf (this->xine, XINE_VERBOSITY_LOG,
        LOG_MODULE ": seek to %" PRId64 " failed (http request failed)\n", (int64_t)abs_offset);
      break;
    }
    if (this->curpos != abs_offset) {
      /* something went wrong ... */
      xprintf (this->xine, XINE_VERBOSITY_LOG,
        LOG_MODULE ": seek to %" PRId64 " failed (server returned invalid range)\n", (int64_t)abs_offset);
      break;
    }

    /* close old connection */
    _x_tls_deinit (&old_tls);
    if (old_fh >= 0)
      _x_io_tcp_close (this->stream, old_fh);
    return 0;
  } while (0);

  /* restore old stream */
  _x_tls_deinit (&this->tls);
  if (this->fh >= 0)
    _x_io_tcp_close (this->stream, this->fh);
  this->tls = old_tls;
  this->curpos = old_pos;
  this->fh = old_fh;
  return -1;
}

static off_t http_plugin_seek(input_plugin_t *this_gen, off_t offset, int origin) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;
  off_t abs_offset;

  abs_offset = _x_input_seek_preview(this_gen, offset, origin,
                                     &this->curpos, this->contentlength, this->preview_size);

  if (abs_offset < 0 && this->accept_range) {

    abs_offset = _x_input_translate_seek(offset, origin, this->curpos, this->contentlength);
    if (abs_offset < 0) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
              "input_http: invalid seek request (%d, %" PRId64 ")\n",
              origin, (int64_t)offset);
      return -1;
    }

    if (http_restart(this, abs_offset) < 0)
      return -1;
  }

  return abs_offset;
}

static const char* http_plugin_get_mrl (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  return this->mrl;
}

static void http_plugin_dispose (input_plugin_t *this_gen ) {
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  http_close(this);
  sbuf_reset (this);

  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }

  if (this->head_dump_file) {
    fclose (this->head_dump_file);
    this->head_dump_file = NULL;
  }

  _x_freep (&this->mime_type);
  free (this);
}

static void report_progress (xine_stream_t *stream, int p) {

  xine_event_t             event;
  xine_progress_data_t     prg;

  if (!stream)
    return;

  prg.description = _("Connecting HTTP server...");
  prg.percent = p;

  event.type = XINE_EVENT_PROGRESS;
  event.data = &prg;
  event.data_length = sizeof (xine_progress_data_t);

  xine_event_send (stream, &event);
}

static xio_handshake_status_t http_plugin_handshake (void *userdata, int fh) {
  static const uint8_t tab_tolower[256] = {
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    ' ','!','"','#','$','%','&','\'','(',')','*','+',',','-','.','/',
    '0','1','2','3','4','5','6','7','8','9',':',';','<','=','>','?',
    '@','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
    'p','q','r','s','t','u','v','w','x','y','z','[','\\',']','^','_',
    '`','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
    'p','q','r','s','t','u','v','w','x','y','z','{','|','}','~',127,
    128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
    144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
    160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
    176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
    192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
    208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
    224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
    240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
  };

  http_input_plugin_t *this = (http_input_plugin_t *)userdata;
  http_input_class_t  *this_class = (http_input_class_t *) this->input_plugin.input_class;
  int                  res;
  int                  mpegurl_redirect = 0;
  char                 mime_type[256];

  {
    char httpstatus[128];
    httpstatus[0] = 0;
    mime_type[0] = 0;
    this->again = 0;
    sbuf_reset (this);

    {
      uint32_t timeout, progress;
      timeout = _x_query_network_timeout (this->xine) * 1000;
      if (timeout == 0)
        timeout = 30000;
      progress = 0;
      do {
        if (this->num_msgs) {
          if (this->num_msgs > 0)
            this->num_msgs--;
          report_progress (this->stream, progress);
        }
        res = _x_io_select (this->stream, fh, XIO_WRITE_READY, 500);
        progress += (500 * 100000) / timeout;
      } while ((res == XIO_TIMEOUT) && (progress <= 100000) && !_x_action_pending (this->stream));
      if (res != XIO_READY) {
        _x_message (this->stream, XINE_MSG_NETWORK_UNREACHABLE, this->mrl, NULL);
        this->ret = -3;
        return XIO_HANDSHAKE_TRY_NEXT;
      }
    }

    /* TLS */
    _x_assert (this->tls == NULL);
    this->tls = _x_tls_init (this->xine, this->stream, fh);
    if (!this->tls) {
      this->ret = -2;
      return XIO_HANDSHAKE_INTR;
    }
    if (this->use_tls) {
      int r = _x_tls_handshake (this->tls, this->url.host, -1);
      if (r < 0) {
        _x_message (this->stream, XINE_MSG_CONNECTION_REFUSED, "TLS handshake failed", NULL);
        xprintf (this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": TLS handshake failed\n");
        this->ret = -4;
        _x_tls_deinit (&this->tls);
        return XIO_HANDSHAKE_TRY_NEXT;
      }
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": TLS handshake succeed, connection is encrypted\n");
    }

    /* Request */
    {
#define ADDSTR(s) q += strlcpy (q, s, e - q); if (q > e) q = e
#define ADDLIT(s) { static const char ls[] = s; if (q + sizeof (ls) <= e) memcpy (q, s, sizeof (ls)), q += sizeof (ls) - 1; }
      char *q = this->sbuf, *e = q + sizeof (this->sbuf) - 24;
      char strport[16];
      int vers = this_class->prot_version;

      if (this->url.port != DEFAULT_HTTP_PORT) {
        char *t = strport;
        *t++ = ':';
        uint32_2str (&t, this->url.port);
      } else {
        strport[0] = 0;
      }

      ADDLIT ("GET ");
      if (this->use_proxy) {
        ADDSTR (this->url.proto);
        ADDLIT ("://");
        ADDSTR (this->url.host);
        ADDSTR (strport);
      }
      ADDSTR (this->url.uri);
      if (vers == 1) {
        ADDLIT (" HTTP/1.1\015\012Host: ");
      } else {
        ADDLIT (" HTTP/1.0\015\012Host: ");
      }
      ADDSTR (this->url.host);
      ADDSTR (strport);
      if (this->curpos > 0) {
        /* restart from offset */
        ADDLIT ("\015\012Range: bytes=");
        uint64_2str (&q, this->curpos);
        ADDLIT ("-");
/*      uint64_2str (&q, this->contentlength - 1); */
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
          "input_http: requesting restart from offset %" PRId64 "\n", (int64_t)this->curpos);
      } else if (vers == 1) {
        ADDLIT ("\015\012Accept-Encoding: gzip,deflate");
      }
      if (this->use_proxy && this_class->proxyuser && this_class->proxyuser[0]) {
        char *proxyauth;
        http_plugin_basicauth (this_class->proxyuser, this_class->proxypassword, &proxyauth);
        ADDLIT ("\015\012Proxy-Authorization: Basic ");
        ADDSTR (proxyauth);
        free (proxyauth);
      }
      if (this->url.user && this->url.user[0]) {
        char *auth;
        http_plugin_basicauth (this->url.user, this->url.password, &auth);
        ADDLIT ("\015\012Authorization: Basic ");
        ADDSTR (auth);
        free (auth);
      }
      ADDLIT ("\015\012User-Agent: ");
      if (this->user_agent) {
        ADDSTR (this->user_agent);
        ADDLIT (" ");
      }
      ADDLIT ("xine/" VERSION "\015\012Accept: */*\015\012Icy-MetaData: 1\015\012\015\012");
      if (this->head_dump_file)
        fwrite (this->sbuf, 1, (uint8_t *)q - this->sbuf, this->head_dump_file);
      if (_x_tls_write (this->tls, this->sbuf, (uint8_t *)q - this->sbuf) != (uint8_t *)q - this->sbuf) {
        _x_message (this->stream, XINE_MSG_CONNECTION_REFUSED, "couldn't send request", NULL);
        xprintf (this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": couldn't send request\n");
        this->ret = -4;
        _x_tls_deinit (&this->tls);
        return XIO_HANDSHAKE_TRY_NEXT;
      }
      lprintf ("request sent: >%s<\n", this->sbuf);
#undef ADDSTR
#undef ADDLIT
    }

    /* Response */
    do {
      this->smode &= ~(MODE_HAS_TYPE | MODE_HAS_LENGTH | MODE_DEFLATED | MODE_CHUNKED);
      this->range_start = 0;
      this->range_end = 0;
      this->range_total = 0;
      /* get status */
      {
        uint8_t *line, *p1, *p2;
        int32_t ok = 0, i = sbuf_get_string (this, &line);
        if ((i < 0) && (errno == EINTR)) {
          this->ret = -1;
          _x_tls_deinit (&this->tls);
          return XIO_HANDSHAKE_INTR;
        }
        do {
          if (i < 4)
            break;
          if ((memcmp (line, "HTTP", 4) && memcmp (line, "ICY ", 4)))
            break;
          p1 = line + i;
          *p1 = ' ';
          for (p2 = line + 4; *p2 != ' '; p2++) ;
          *p1 = 0;
          while (*p2 == ' ') p2++;
          this->status = str2uint32 (&p2);
          if (this->status == 0)
            break;
          while (*p2 == ' ') p2++;
          strlcpy (httpstatus, p2, sizeof (httpstatus));
          ok = 1;
        } while (0);
        if (!ok) {
          _x_message (this->stream, XINE_MSG_CONNECTION_REFUSED, "invalid http answer", NULL);
          xine_log (this->xine, XINE_LOG_MSG, _("input_http: invalid http answer\n"));
          this->ret = -6;
          _x_tls_deinit (&this->tls);
          return XIO_HANDSHAKE_TRY_NEXT;
        }
      }
      /* get props */
      while (1) {
        uint32_t key;
        uint8_t *line, *p1, *p2;
        {
          int32_t i = sbuf_get_string (this, &line);
          if (i < 0) {
            /* "httpget.http: invalid HTTP response\n" */
            this->ret = -1;
            _x_tls_deinit (&this->tls);
            return errno == EINTR ? XIO_HANDSHAKE_INTR : XIO_HANDSHAKE_TRY_NEXT;
          }
          /* an empty line marks the end of response head */
          if (!i) break;
          p1 = line + i;
        }
        /* find value string */
        *p1 = ':';
        for (p2 = line; *p2 != ':'; p2++) *p2 = tab_tolower[*p2];
        *p1 = 0;
        if (p2 != p1) {
          *p2++ = 0;
          while (*p2 == ' ') p2++;
        }
        /* find key */
        {
          static const char * const keys[] = {
            "\x06""accept-ranges",
            "\x03""content-encoding",
            "\x01""content-length",
            "\x04""content-range",
            "\x02""content-type",
            "\x0b""icy-genre",
            "\x0d""icy-metaint",
            "\x0a""icy-name",
            "\x0c""icy-notice2",
            "\x07""location",
            "\x08""server",
            "\x05""transfer-encoding",
            "\x09""www-authenticate"
          };
          uint32_t b = 0, e = sizeof (keys) / sizeof (keys[0]), m = e >> 1;
          key = 0;
          do {
            int d = strcmp ((char *)line, keys[m] + 1);
            if (d == 0) {
              key = keys[m][0];
              break;
            }
            if (d < 0)
              e = m;
            else
              b = m + 1;
            m = (b + e) >> 1;
          } while (b != e);
        }
        switch (key) {
          case 0x1: /* content-length */
            if (!this->contentlength)
              this->contentlength = str2uint64 (&p2);
            this->smode |= MODE_HAS_LENGTH;
            break;
          case 0x2: /* content type */
            strlcpy (mime_type, (char *)p2, sizeof (mime_type));
            this->smode |= MODE_HAS_TYPE;
            if (!strncasecmp ((char *)p2, "audio/x-mpegurl", 15)) {
              lprintf ("Opening an audio/x-mpegurl file, late redirect.");
              mpegurl_redirect = 1;
            }
            else if (!strncasecmp ((char *)p2, "video/nsv", 9)) {
              lprintf ("shoutcast nsv detected\n");
              this->is_nsv = 1;
            }
            break;
          case 0x3: /* content-encoding */
            if ((!memcmp (p2, "gzip", 4)) || (!memcmp (p2, "deflate", 7)))
              this->smode |= MODE_DEFLATED;
            break;
          case 0x4: /* content-range */
            if (!memcmp (p2, "bytes", 5))
              p2 += 5;
            while (*p2 == ' ')
              p2++;
            this->range_start = str2uint64 (&p2);
            if (*p2 == '-') {
              p2++;
              this->range_end = str2uint64 (&p2);
            }
            if (*p2 == '/') {
              p2++;
              this->range_total = str2uint64 (&p2);
            }
            break;
          case 0x5: /* transfer-encoding */
            if ((!memcmp (p2, "chunked", 7)))
              this->smode |= MODE_CHUNKED;
            break;
          case 0x6: /* accept-ranges */
            if (strstr (line + 14, "bytes")) {
              xprintf (this->xine, XINE_VERBOSITY_DEBUG,
                "input_http: Server supports request ranges. Enabling seeking support.\n");
              this->accept_range = 1;
            } else {
              xprintf (this->xine, XINE_VERBOSITY_LOG,
                "input_http: Unknown value in header \'%s\'\n", line + 14);
              this->accept_range = 0;
            }
            break;
          case 0x7: /* location */
            /* check redirection */
            if  ((this->status == 302) /* found */
              || (this->status == 301) /* moved permanently */
              || (this->status == 303) /* see other */
              || (this->status == 307)) { /* temporary redirect */
              lprintf ("trying to open target of redirection: >%s<\n", (char *)p2);
              _x_merge_mrl (this->mrl, sizeof (this->mrl), this->mrl, (char *)p2);
            }
            break;
          case 0x8: /* server */
            if (!strncasecmp ((char *)p2, "last.fm", 7)) {
              lprintf ("last.fm streaming server detected\n");
              this->is_lastfm = 1;
            }
            break;
          case 0x9: /* www-authenticate */
            if (this->status == 401)
              _x_message (this->stream, XINE_MSG_AUTHENTICATION_NEEDED, this->mrl, (char *)p2, NULL);
            break;
          /* Icecast / ShoutCast Stuff */
          case 0xa: /* icy-name */
            if (this->stream) {
              _x_meta_info_set (this->stream, XINE_META_INFO_ALBUM, (char *)p2);
              _x_meta_info_set (this->stream, XINE_META_INFO_TITLE, (char *)p2);
            }
            break;
          case 0xb: /* icy-genre */
            if (this->stream)
              _x_meta_info_set (this->stream, XINE_META_INFO_GENRE, (char *)p2);
            break;
          /* icy-notice1 is always the same */
          case 0xc: /* icy-notice2 */
            {
              char *end = strstr ((char *)p2, "<BR>");
              if (end)
              *end = '\0';
              if (this->stream)
                _x_meta_info_set (this->stream, XINE_META_INFO_COMMENT, (char *)p2);
            }
            break;
          case 0xd: /* icy-metaint */
            /* metadata interval (in byte) */
            this->shoutcast_metaint = str2uint32 (&p2);
            lprintf ("shoutcast_metaint: %d\n", this->shoutcast_metaint);
            this->shoutcast_mode = 1;
            this->shoutcast_pos = 0;
            break;
        }
      }
      /* skip non-document content */
      if ((this->status != 200) && (this->status != 206)) while (this->contentlength > 0) {
        int i = sizeof (this->preview);
        if (i > this->contentlength) i = this->contentlength;
        i = sbuf_get_bytes (this, this->sbuf, i);
        if (i <= 0) break;
        this->contentlength -= i;
      }
      /* indicate free content length */
      if ((this->smode & (MODE_HAS_TYPE | MODE_HAS_LENGTH)) == MODE_HAS_TYPE) this->contentlength = -1;
      if (this->smode & MODE_CHUNKED) this->contentlength = 0;
    } while (this->status == 102); /* processing */

    this->curpos = 0;

    if ((this->status >= 300) && (this->status < 400)) {
      xine_log (this->xine, XINE_LOG_MSG,
        _("input_http: 3xx redirection: >%d %s<\n"), this->status, httpstatus);
      this->again = 1;
    } else if (this->status == 404) { /* not found */
      _x_message (this->stream, XINE_MSG_FILE_NOT_FOUND, this->mrl, NULL);
      xine_log (this->xine, XINE_LOG_MSG,
        _("input_http: http status not 2xx: >%d %s<\n"), this->status, httpstatus);
      this->ret = -7;
      _x_tls_deinit (&this->tls);
      return XIO_HANDSHAKE_INTR;
    } else if (this->status == 401) {
      xine_log (this->xine, XINE_LOG_MSG,
        _("input_http: http status not 2xx: >%d %s<\n"), this->status, httpstatus);
      /* don't return - there may be a WWW-Authenticate header... */
    } else if (this->status == 403) {
      _x_message (this->stream, XINE_MSG_PERMISSION_ERROR, this->mrl, NULL);
      xine_log (this->xine, XINE_LOG_MSG,
        _("input_http: http status not 2xx: >%d %s<\n"), this->status, httpstatus);
      this->ret = -8;
      _x_tls_deinit (&this->tls);
      return XIO_HANDSHAKE_INTR;
    } else if ((this->status < 200) || (this->status >= 300)) {
      _x_message (this->stream, XINE_MSG_CONNECTION_REFUSED, "http status not 2xx: ", httpstatus, NULL);
      xine_log (this->xine, XINE_LOG_MSG,
        _("input_http: http status not 2xx: >%d %s<\n"), this->status, httpstatus);
      this->ret = -9;
      _x_tls_deinit (&this->tls);
      return XIO_HANDSHAKE_TRY_NEXT;
    }
    if (this->smode & MODE_HAS_LENGTH)
      xine_log (this->xine, XINE_LOG_MSG,
        _("input_http: content length = %" PRId64 " bytes\n"), (int64_t)this->contentlength);
    if (this->range_start) {
      this->curpos = this->range_start;
      if (this->contentlength != this->range_end + 1) {
        xprintf (this->xine, XINE_VERBOSITY_LOG,
          "input_http: Reveived invalid content range");
        /* truncate - there won't be more data anyway */
        this->contentlength = this->range_end + 1;
      } else {
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
          "input_http: Stream starting at offset %" PRId64 "\n.", this->range_start);
      }
    }
#if 0
    if ( len >= (int)sizeof(buf) ) {
       _x_message(this->stream, XINE_MSG_PERMISSION_ERROR, this->mrl, NULL);
       xine_log (this->xine, XINE_LOG_MSG,
         _("input_http: buffer exhausted after %zu bytes."), sizeof(buf));
       this->ret = -10;
      _x_tls_deinit (&this->tls);
      return XIO_HANDSHAKE_TRY_NEXT;
    }
#endif
    lprintf ("end of headers\n");

    if (mpegurl_redirect) {
      char urlbuf[4096] = { 0, };
      char *newline = NULL;
      http_plugin_read_int (this, urlbuf, sizeof (urlbuf) - 1);
      newline = strstr (urlbuf, "\r\n");
      /* If the newline can't be found, either the 4K buffer is too small, or
       * more likely something is fuzzy. */
      if (newline) {
        *newline = '\0';
        lprintf ("mpegurl pointing to %s\n", urlbuf);
        _x_merge_mrl (this->mrl, sizeof (this->mrl), this->mrl, urlbuf);
        this->again = 1;
      }
    }
  }

  if (*mime_type) {
    free (this->mime_type);
    this->mime_type = strdup (mime_type);
  }
  return XIO_HANDSHAKE_OK;
}

static int http_plugin_open (input_plugin_t *this_gen) {
  http_input_plugin_t *this = (http_input_plugin_t *)this_gen;
  http_input_class_t  *this_class = (http_input_class_t *) this->input_plugin.input_class;
  int redirections = 20;

  do {
    int proxyport, mrl_tls, proxy_tls;

    http_close (this);
    this->again = 0;

    if (--redirections <= 0) {
      xprintf (this->xine, XINE_VERBOSITY_LOG,
        "input_http: too many redirections, giving up.\n");
      return -1;
    }

    this->user_agent = _x_url_user_agent (this->mrl);
    if (!_x_url_parse2 (this->mrl, &this->url)) {
      _x_message (this->stream, XINE_MSG_GENERAL_WARNING, "malformed url", NULL);
      return 0;
    }
    /* NOTE: mrl = http | https
     *     proxy = http | https
     * all 4 combinations are valid! */
    this->use_tls = mrl_tls = !strcasecmp (this->url.proto, "https");
    if (this->url.port == 0)
      this->url.port = mrl_tls ? DEFAULT_HTTPS_PORT : DEFAULT_HTTP_PORT;

    this->use_proxy = (this_class->proxyhost && this_class->proxyhost[0]);
    if (this->use_proxy && !_x_use_proxy (this->xine, this_class, this->url.host))
      this->use_proxy = 0;
    if (this->use_proxy && !_x_url_parse2 (this_class->proxyhost, &this->proxyurl)) {
      _x_message (this->stream, XINE_MSG_GENERAL_WARNING, "malformed proxy url", NULL);
      this->use_proxy = 0;
    }
    proxyport = this_class->proxyport;
    if (this->use_proxy) {
      this->use_tls = proxy_tls = !strcasecmp (this->proxyurl.proto, "https");
      if (proxyport == 0)
        proxyport = proxy_tls ? DEFAULT_HTTPS_PORT : DEFAULT_HTTP_PORT;
    }

#ifdef LOG
    printf ("input_http: host     : >%s<\n", this->url.host);
    printf ("input_http: port     : >%d<\n", this->url.port);
    printf ("input_http: user     : >%s<\n", this->url.user);
    printf ("input_http: password : >%s<\n", this->url.password);
    printf ("input_http: path     : >%s<\n", this->url.uri);
    if (this->use_proxy)
      printf (" via proxy >%s:%d<", this_class->proxyhost, proxyport);
    printf ("\n");
#endif

    this->ret = -2;
    if (this->use_proxy)
      this->fh = _x_io_tcp_handshake_connect (this->stream, this_class->proxyhost, proxyport, http_plugin_handshake, this);
    else
      this->fh = _x_io_tcp_handshake_connect (this->stream, this->url.host, this->url.port, http_plugin_handshake, this);
    if (this->fh < 0)
      return this->ret;
  } while (this->again);

  if (this->head_dump_file)
    fflush (this->head_dump_file);

  if (this->curpos > 0) {
    /* restarting after seek */
    this->preview_size = 0;
    return 1;
  }

  /* fill preview buffer */
  this->preview_size = MAX_PREVIEW_SIZE;
  if (this->is_nsv) {
    if (!resync_nsv (this)) {
      _x_tls_deinit (&this->tls);
      _x_io_tcp_close (this->stream, this->fh);
      this->fh = -1;
      return -11;
    }
    /* the first 3 chars are "NSV" */
    this->preview_size = http_plugin_read_int (this, this->preview + 3, MAX_PREVIEW_SIZE - 3);
  } else {
    this->preview_size = http_plugin_read_int (this, this->preview, MAX_PREVIEW_SIZE);
  }
  if (this->preview_size < 0) {
    this->preview_size = 0;
    xine_log (this->xine, XINE_LOG_MSG, _("input_http: read error %d\n"), errno);
    _x_tls_deinit (&this->tls);
    _x_io_tcp_close (this->stream, this->fh);
    this->fh = -1;
    return -12;
  }
  lprintf ("preview_size=%"PRId64"\n", this->preview_size);
  this->curpos = 0;

  this->ret = 1;
  return 1;
}

static int http_can_handle (xine_stream_t *stream, const char *mrl) {
  if (!strncasecmp (mrl, "https://", 8)) {
    /* check for tls plugin here to allow trying another https plugin (avio) */
    if (!_x_tls_available(stream->xine)) {
      xine_log (stream->xine, XINE_LOG_MSG, "input_http: TLS plugin not found\n");
      return 0;
    }
  } else
  if (strncasecmp (mrl, "http://", 7) &&
      strncasecmp (mrl, "unsv://", 7) &&
      strncasecmp (mrl, "peercast://pls/", 15) &&
      !_x_url_user_agent (mrl) /* user agent hacks */) {
    return 0;
  }
  return 1;
}

static int http_plugin_get_optional_data (input_plugin_t *this_gen,
					  void *const data, int data_type) {

  void **const ptr = (void **const) data;
  http_input_plugin_t *this = (http_input_plugin_t *) this_gen;

  switch (data_type) {
    case INPUT_OPTIONAL_DATA_PREVIEW:
      if (!data || (this->preview_size <= 0))
        break;
      memcpy (data, this->preview, this->preview_size);
      return this->preview_size;

    case INPUT_OPTIONAL_DATA_SIZED_PREVIEW:
      if (!data || (this->preview_size <= 0))
        break;
      {
        int want;
        memcpy (&want, data, sizeof (want));
        want = want < 0 ? 0
             : want > this->preview_size ? this->preview_size
             : want;
        memcpy (data, this->preview, want);
        return want;
      }

    case INPUT_OPTIONAL_DATA_MIME_TYPE:
      *ptr = this->mime_type;
      /* fall through */
    case INPUT_OPTIONAL_DATA_DEMUX_MIME_TYPE:
      return *this->mime_type ? INPUT_OPTIONAL_SUCCESS : INPUT_OPTIONAL_UNSUPPORTED;

    case INPUT_OPTIONAL_DATA_NEW_MRL:
      if (!data)
        break;
      if (!http_can_handle (this->stream, data))
        break;
      http_close (this);
      sbuf_reset (this);
      this->mrl[0] = 0;
      _x_freep (&this->mime_type);
      _x_freep (&this->user_agent);
      _x_freep (&this->shoutcast_songtitle);
      this->curpos              = 0;
      this->contentlength       = 0;
      this->is_nsv              = 0;
      this->is_lastfm           = 0;
      this->shoutcast_mode      = 0;
      this->accept_range        = 0;
      this->shoutcast_metaint   = 0;
      this->shoutcast_pos       = 0;
      this->preview_size        = 0;
      if ((this->num_msgs < 0) || (this->num_msgs > 8))
        this->num_msgs = 8;
      if (!strncasecmp ((const char *)data, "peercast://pls/", 15)) {
        char *w = this->mrl, *e = w + sizeof (this->mrl);
        w += strlcpy (w, "http://127.0.0.1:7144/stream/", e - w);
        strlcpy (w, (const char *)data + 15, e - w);
      } else
        strlcpy (this->mrl, (const char *)data, sizeof (this->mrl));
      return INPUT_OPTIONAL_SUCCESS;
  }

  return INPUT_OPTIONAL_UNSUPPORTED;
}

/*
 * http input plugin class
 */
static input_plugin_t *http_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream,
				    const char *mrl) {
  http_input_class_t  *cls = (http_input_class_t *)cls_gen;
  http_input_plugin_t *this;

  if (!http_can_handle (stream, mrl))
    return NULL;

  this = calloc(1, sizeof(http_input_plugin_t));
  if (!this)
    return NULL;

#ifndef HAVE_ZERO_SAFE_MEM
  this->curpos              = 0;
  this->contentlength       = 0;
  this->mime_type           = NULL;
  this->user_agent          = NULL;
  this->tls                 = NULL;
  this->is_nsv              = 0;
  this->is_lastfm           = 0;
  this->shoutcast_mode      = 0;
  this->accept_range        = 0;
  this->shoutcast_metaint   = 0;
  this->shoutcast_pos       = 0;
  this->shoutcast_songtitle = NULL;
  this->preview_size        = 0;
  this->url.proto           = NULL;
  this->url.host            = NULL;
  this->url.user            = NULL;
  this->url.password        = NULL;
  this->url.uri             = NULL;
  this->proxyurl.proto      = NULL;
  this->proxyurl.host       = NULL;
  this->proxyurl.user       = NULL;
  this->proxyurl.password   = NULL;
  this->proxyurl.uri        = NULL;
  this->head_dump_file      = NULL;
#endif

  if (!strncasecmp (mrl, "peercast://pls/", 15)) {
    char *w = this->mrl, *e = w + sizeof (this->mrl);
    w += strlcpy (w, "http://127.0.0.1:7144/stream/", e - w);
    strlcpy (w, mrl + 15, e - w);
  } else {
    strlcpy (this->mrl, mrl, sizeof (this->mrl));
  }

  this->fh = -1;

  this->num_msgs = -1;
  this->stream = stream;
  this->xine   = cls->xine;
  this->nbc = stream ? nbc_init (stream) : NULL;
  sbuf_init (this);

  if (cls->head_dump_name && cls->head_dump_name[0]) {
    this->head_dump_file = fopen (cls->head_dump_name, "ab");
    if (this->head_dump_file)
      fseek (this->head_dump_file, 0, SEEK_END);
  }

  this->input_plugin.open              = http_plugin_open;
  this->input_plugin.get_capabilities  = http_plugin_get_capabilities;
  this->input_plugin.read              = http_plugin_read;
  this->input_plugin.read_block        = _x_input_default_read_block;
  this->input_plugin.seek              = http_plugin_seek;
  this->input_plugin.get_current_pos   = http_plugin_get_current_pos;
  this->input_plugin.get_length        = http_plugin_get_length;
  this->input_plugin.get_blocksize     = _x_input_default_get_blocksize;
  this->input_plugin.get_mrl           = http_plugin_get_mrl;
  this->input_plugin.get_optional_data = http_plugin_get_optional_data;
  this->input_plugin.dispose           = http_plugin_dispose;
  this->input_plugin.input_class       = cls_gen;

  return &this->input_plugin;
}

static void http_class_dispose (input_class_t *this_gen) {
  http_input_class_t  *this = (http_input_class_t *) this_gen;
  config_values_t     *config = this->xine->config;

  config->unregister_callbacks (config, NULL, NULL, this, sizeof (*this));

  free (this);
}

void *input_http_init_class (xine_t *xine, const void *data) {
  http_input_class_t  *this;
  config_values_t     *config;
  char                *proxy_env;
  char                *proxyhost_env = NULL;
  int                  proxyport_env = DEFAULT_HTTP_PORT;

  (void)data;
  this = calloc(1, sizeof (http_input_class_t));
  if (!this)
    return NULL;

  this->xine   = xine;
  config       = xine->config;

  this->input_class.get_instance       = http_class_get_instance;
  this->input_class.identifier         = "http";
  this->input_class.description        = N_("http/https input plugin");
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = http_class_dispose;
  this->input_class.eject_media        = NULL;

  /*
   * honour http_proxy envvar
   */
  if((proxy_env = getenv("http_proxy")) && *proxy_env) {
    char  *p;

    if(!strncmp(proxy_env, "http://", 7))
      proxy_env += 7;

    proxyhost_env = strdup(proxy_env);

    if((p = strrchr(proxyhost_env, ':')) && (strlen(p) > 1)) {
      *p++ = '\0';
      proxyport_env = (int) strtol(p, &p, 10);
    }
  }

  /*
   * proxy settings
   */
  this->proxyhost = config->register_string (config, "media.network.http_proxy_host",
    proxyhost_env ? proxyhost_env : "",
    _("HTTP proxy host"),
    _("The hostname of the HTTP proxy."),
    10, proxy_host_change_cb, (void *) this);

  this->proxyport = config->register_num (config, "media.network.http_proxy_port",
    proxyport_env,
    _("HTTP proxy port"),
    _("The port number of the HTTP proxy."),
    10, proxy_port_change_cb, (void *) this);

  /* registered entries could be empty. Don't ignore envvar */
  if(!strlen(this->proxyhost) && (proxyhost_env && strlen(proxyhost_env))) {
    config->update_string(config, "media.network.http_proxy_host", proxyhost_env);
    config->update_num(config, "media.network.http_proxy_port", proxyport_env);
  }
  _x_freep(&proxyhost_env);

  this->proxyuser = config->register_string (config, "media.network.http_proxy_user",
    "",
    _("HTTP proxy username"),
    _("The user name for the HTTP proxy."),
    10, proxy_user_change_cb, (void *) this);

  this->proxypassword = config->register_string (config, "media.network.http_proxy_password",
    "",
    _("HTTP proxy password"),
    _("The password for the HTTP proxy."),
    10, proxy_password_change_cb, (void *) this);

  this->noproxylist = config->register_string (config, "media.network.http_no_proxy",
    "",
    _("Domains for which to ignore the HTTP proxy"),
    _("A comma-separated list of domain names for which the proxy is to be ignored.\n"
      "If a domain name is prefixed with '=' then it is treated as a host name only "
      "(full match required)."),
    10, no_proxy_list_change_cb, (void *) this);

  /* protocol version */
  {
    static const char * const versions[] = {"http/1.0", "http/1.1", NULL};
    this->prot_version = config->register_enum (config, "media.network.http_version",
      0, (char **)versions,
      _("HTTP protocol version to use"),
      _("Try these when there are communication problems."),
      10, prot_version_change_cb, this);
  }

  /* head dump file */
  this->head_dump_name = config->register_string (config, "media.network.http_head_dump_file",
    "",
    _("Dump HTTP request and response heads to this file"),
    _("Set this for debugging."),
    20, head_dump_name_change_cb, this);

  return this;
}

