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
 * URL helper functions
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <string.h>
#include <stdint.h>

#include <xine/xine_internal.h>
#include "http_helper.h"

static const int8_t tab_unhex[256] = {
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
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

static void unescape (char **d, const char *s, size_t len) {
  const uint8_t *r = (const uint8_t *)s, *e = r + len;
  uint8_t *w = (uint8_t *)*d;
  while (r < e) {
    if ((r[0] == '%') && (r + 3 <= e)) {
      int32_t v = ((int32_t)tab_unhex[r[1]] << 4) | (int32_t)tab_unhex[r[2]];
      if (v >= 0) {
        *w++ = v;
        r += 3;
        continue;
      }
    }
    *w++ = *r++;
  }
  *d = (char *)w;
}

static const uint8_t tab_esclen[256] = {
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
};

static const uint8_t tab_hex[16] = "0123456789abcdef";

static size_t esclen (const char *s, size_t len) {
  const uint8_t *r = (const uint8_t *)s, *e = r + len;
  size_t n = 0;
  while (r < e)
    n += tab_esclen[*r++];
  return n;
}

static void escape (char **d, const char *s, size_t len) {
  const uint8_t *r = (const uint8_t *)s, *e = r + len;
  uint8_t *w = (uint8_t *)*d;
  while (r < e) {
    if (tab_esclen[*r] == 3) {
      *w++ = '%';
      *w++ = tab_hex[(*r) >> 4];
      *w++ = tab_hex[(*r) & 15];
      r++;
    } else {
      *w++ = *r++;
    }
  }
  *d = (char *)w;
}

/* XXX: nullsoft paths start with a ';'. supporting this means:
 *      no ';' in host names, and escaping ';' in user names and passwords :-/
 * 0x01  : ; / [ @ ? # end
 * 0x02  ] end
 * 0x04  / ? # end
 * 0x08  ? # end
 * 0x10  # end
 * 0x20  end
 * 0x40  : ; / ? # end
 * 0x80  ; / ? # end
 */
static const uint8_t tab_type[256] = {
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
     0,   0,   0,0xdd,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,0xc5,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,0x41,0xc1,   0,   0,   0,0xcd,
  0x01,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,0x01,   0,0x02,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

typedef struct {
  uint32_t    prot_start, prot_stop;
  uint32_t    user_start, user_stop;
  uint32_t    pass_start, pass_stop;
  uint32_t    host_start, host_stop;
  uint32_t    path_start, path_stop;
  uint32_t    args_start, args_stop;
  uint32_t    info_start, info_stop;
  uint32_t    port;
} mrlp_t;

int _x_url_parse2 (const char *mrl, xine_url_t *url) {
  mrlp_t res;
  const uint8_t *r = (const uint8_t *)mrl, *b = r;

  if (!mrl || !url)
    return 0;

  do {
    const uint8_t *s = r;
    /* prot */
    res.prot_start = res.prot_stop = 0;
    res.pass_start = 0;
    while (!(tab_type[*r] & 0x01))
      r++;
    if ((r[0] == ':') && (r[1] == '/') && (r[2] == '/')) {
      res.prot_stop  = r - b;
      r += 3;
      s = r;
      while (!(tab_type[*r] & 0x01))
        r++;
    }
    /* user */
    res.user_stop = r - b;
    if (r[0] == ':') {
      r++;
      res.pass_start = r - b;
      while (!(tab_type[*r] & 0x01))
        r++;
    }
    if (r[0] == '@') {
      res.user_start = s - b;
      res.pass_stop = r - b;
      r++;
      if (res.pass_start == 0)
        res.user_stop = res.pass_start = res.pass_stop;
    } else {
      res.user_start = res.user_stop = 0;
      res.pass_start = res.pass_stop = 0;
      r = s;
    }
  } while (0);

  /* host */
  if (r[0] == '[') { /* ip6 */
    r++;
    res.host_start = r - b;
    while (!(tab_type[*r] & 0x02))
      r++;
    res.host_stop = r - b;
    if (r[0] != ']')
      return 0;
    r++;
  } else { /* ip4 */
    res.host_start = r - b;
    while (!(tab_type[*r] & 0x40))
      r++;
    res.host_stop = r - b;
  }

  /* port */
  /* res.port_start = r - b; */
  if (r[0] == ':') {
    const uint8_t *s;
    uint32_t v = 0;
    uint8_t z;
    r++;
    s = r;
    /* res.port_start = r - b; */
    while ((z = *r ^ '0') < 10)
      v = v * 10u + z, r++;
    res.port = v;
    /* port set but empty */
    if (r == s)
      return 0;
    /* neither path, args, nor info follows */
    if (!(tab_type[*r] & 0x80))
      return 0;
  } else {
    res.port = 0;
  }
  /* res.port_stop = r - b; */

  /* path */
  res.path_start = r - b;
  while (!(tab_type[*r] & 0x08))
    r++;
  res.path_stop = r - b;

  /* args */
  res.args_start = r - b;
  if (r[0] == '?') {
    r++;
    res.args_start = r - b;
    while (!(tab_type[*r] & 0x10))
      r++;
  }
  res.args_stop = r - b;

  /* info */
  res.info_start = r - b;
  if (r[0] == '#') {
    r++;
    res.info_start = r - b;
    while (!(tab_type[*r] & 0x20))
      r++;
  }
  res.info_stop = r - b;

  if (res.host_start == res.host_stop) {
    /* no host needs no prot as well */
    if (res.prot_start != res.prot_stop)
      return 0;
    /* no host and no path?? */
    if (res.path_start == res.path_stop)
      return 0;
  }

  url->port = res.port;
  {
    char *q;
    size_t pathlen = esclen ((const char *)b + res.path_start, res.path_stop - res.path_start);
    size_t argslen = esclen ((const char *)b + res.args_start, res.args_stop - res.args_start);
    size_t need = res.prot_stop - res.prot_start + 1
                + res.host_stop - res.host_start + 1
                + 2 * pathlen + argslen + 6
                + res.args_stop - res.args_start + 1
                + res.user_stop - res.user_start + 1
                + res.pass_stop - res.pass_start + 1;
    url->buf = malloc (need);
    if (!url->buf)
      return 0;
    q = url->buf;

    url->proto = q;
    need = res.prot_stop - res.prot_start;
    if (need) {
      memcpy (q, b + res.prot_start, need);
      q += need;
    }
    *q++ = 0;

    url->host = q;
    need = res.host_stop - res.host_start;
    if (need) {
      memcpy (q, b + res.host_start, need);
      q += need;
    }
    *q++ = 0;

    url->path = q;
    need = res.path_stop - res.path_start;
    if (need) {
      /* yet another nullsoft HACK. */
      if (b[res.path_start] == ';')
        *q++ = '/';
      escape (&q, (const char *)b + res.path_start, need);
    } else {
      /* empty path default */
      *q++ = '/';
    }
    pathlen = q - url->path;
    *q++ = 0;

    url->uri = q;
    if (pathlen) {
      memcpy (q, url->path, pathlen);
      q += pathlen;
    }

    url->args = q;
    need = res.args_stop - res.args_start;
    if (need) {
      *q++ = '?';
      escape (&q, (const char *)b + res.args_start, need);
    }
    *q++ = 0;

    /* input_ftp wants these NULL if unset. */
    need = res.user_stop - res.user_start;
    if (need) {
      url->user = q;
      unescape (&q, (char *)b + res.user_start, need);
      *q++ = 0;
    } else {
      url->user = NULL;
    }

    need = res.pass_stop - res.pass_start;
    if (need) {
      url->password = q;
      unescape (&q, (char *)b + res.pass_start, need);
      *q++ = 0;
    } else {
      url->password = NULL;
    }
  }

  return 1;
}

void _x_url_init (xine_url_t *url) {
  if (!url)
    return;
#ifdef HAVE_ZERO_SAFE_MEM
  memset (url, 0, sizeof (*url));
#else
  url->proto = NULL;
  url->host  = NULL;
  url->port  = 0;
  url->path  = NULL;
  url->args  = NULL;
  url->uri   = NULL;
  url->user  = NULL;
  url->password  = NULL;
  url->buf = NULL;
#endif
}

void _x_url_cleanup (xine_url_t *url) {
  if (!url)
    return;
  url->proto = NULL;
  url->host  = NULL;
  url->port  = 0;
  url->path  = NULL;
  url->args  = NULL;
  url->uri   = NULL;
  url->user  = NULL;
  if (url->buf && url->password) {
    size_t n = strlen (url->password);
    if (n)
      memset (url->buf + (url->password - (const char *)url->buf), 0, n);
  }
  url->password  = NULL;
  free (url->buf);
  url->buf = NULL;
}

size_t _x_merge_mrl (char *dest, size_t dsize, const char *base_mrl, const char *new_mrl) {
  const uint8_t *b = (const uint8_t *)base_mrl;
  const uint8_t *n = (const uint8_t *)new_mrl;
  uint8_t *d;
  size_t base_len, new_len, ret;

  if (!(n && n[0])) {

    /* "old_stuff" + "" = "old_stuff" */
    base_len = base_mrl ? strlen (base_mrl) : 0;
    new_len = 0;

  } else if (!(b && b[0])) {

    /* "" + "new_stuff" = "new_stuff" */
    base_len = 0;
    new_len = strlen (new_mrl);

  } else {

    while (!(tab_type[*b] & 0x01))
      b++;
    while (!(tab_type[*n] & 0x01))
      n++;
    if ((n[0] == ':') && (n[1] == '/') && (n[2] == '/')) {

      base_len = 0;
      new_len = strlen (new_mrl);
      if (n == (const uint8_t *)new_mrl) {
        /* "https://host1/foo" + "://host2/bar" = "https://host2/bar" (no joke) */
        if ((b[0] == ':') && (b[1] == '/') && (b[2] == '/'))
          base_len = b - (const uint8_t *)base_mrl;
        b = (const uint8_t *)base_mrl;
      } else {
        /* "old_stuff" + "new_stuff" = "new_stuff" */
        n = (const uint8_t *)new_mrl;
      }

    } else {

      /* seek base to path */
      if ((b[0] == ':') && (b[1] == '/') && (b[2] == '/'))
        b += 3;
      if (b[0] == '[') {
        while (!(tab_type[*b] & 0x02))
          b++;
      }
      while (!(tab_type[*b] & 0x80))
        b++;

      /* if new is relative path, seek further to last / */
      n = (const uint8_t *)new_mrl;
      if ((n[0] != ';') && (n[0] != '/')) {
        const uint8_t *last_slash = b;
        while (b[0] == '/') {
          last_slash = b++;
          while (!(tab_type[*b] & 0x04))
            b++;
        }
        b = last_slash;
      } else if (n[0] == '/')
        n++;
      if (b[0] == '/')
        b++;

      base_len = b - (const uint8_t *)base_mrl;
      new_len = strlen ((const char *)n);

    }
  }

  /* size paranoia */
  ret = base_len + new_len;
  if (ret + 1 > dsize) {
    if (base_len + 1 > dsize) {
      base_len = dsize - 1;
      new_len = 0;
    } else {
      new_len = dsize - base_len - 1;
    }
  }

  /* no target, just tell size */
  if (!dest || !dsize)
    return ret;
  d = (uint8_t *)dest;

  /* copy base part */
  if (base_len && ((const char *)dest != base_mrl))
    memcpy (d, base_mrl, base_len);
  d += base_len;

  /* copy new part */
  if (new_len)
    memcpy (d, n, new_len);
  d += new_len;
  *d = 0;

  return ret;
}

const char *_x_url_user_agent (const char *url)
{
  if (!strncasecmp (url, "qthttp://", 9))
    return "QuickTime"; /* needed for Apple trailers */
  return NULL;
}

#ifdef TEST_URL
/*
 * url parser test program
 */

static int check_url (char *mrl, int ok) {
  xine_url_t url;
  int res;

  printf("--------------------------------\n");
  printf ("url=%s\n", mrl);
  res = _x_url_parse2 (mrl, &url);
  if (res) {
    printf ("proto=%s, host=%s, port=%d, user=%s, password=%s, uri=%s\n",
        url.proto, url.host, url.port, url.user, url.password, url.uri);
    free (url.proto);
    free (url.host);
    free (url.user);
    free (url.password);
    free (url.uri);
  } else {
    printf("bad url\n");
  }
  if (res == ok) {
    printf ("%s test OK\n", mrl);
    return 1;
  } else {
    printf ("%s test KO\n", mrl);
    return 0;
  }
}

static int check_paste(const char *base, const char *url, const char *ok) {
  char *res;
  int ret;

  printf("--------------------------------\n");
  printf("base url=%s\n", base);
  printf(" new url=%s\n", url);
  res = _x_canonicalise_url (base, url);
  printf("  result=%s\n", res);
  ret = !strcmp (res, ok);
  free (res);
  puts (ret ? "test OK" : "test KO");
  return ret;
}

int main(int argc, char** argv) {
  char *proto, host, port, user, password, uri;
  int res = 0;

  res += check_url("http://www.toto.com/test1.asx", 1);
  res += check_url("http://www.toto.com:8080/test2.asx", 1);
  res += check_url("http://titi:pass@www.toto.com:8080/test3.asx", 1);
  res += check_url("http://www.toto.com", 1);
  res += check_url("http://www.toto.com/", 1);
  res += check_url("http://www.toto.com:80", 1);
  res += check_url("http://www.toto.com:80/", 1);
  res += check_url("http://www.toto.com:", 0);
  res += check_url("http://www.toto.com:/", 0);
  res += check_url("http://www.toto.com:abc", 0);
  res += check_url("http://www.toto.com:abc/", 0);
  res += check_url("http://titi@www.toto.com:8080/test4.asx", 1);
  res += check_url("http://@www.toto.com:8080/test5.asx", 0);
  res += check_url("http://:@www.toto.com:8080/test6.asx", 0);
  res += check_url("http:///test6.asx", 0);
  res += check_url("http://:/test7.asx", 0);
  res += check_url("http://", 0);
  res += check_url("http://:", 0);
  res += check_url("http://@", 0);
  res += check_url("http://:@", 0);
  res += check_url("http://:/@", 0);
  res += check_url("http://www.toto.com:80a/", 0);
  res += check_url("http://[www.toto.com]", 1);
  res += check_url("http://[www.toto.com]/", 1);
  res += check_url("http://[www.toto.com]:80", 1);
  res += check_url("http://[www.toto.com]:80/", 1);
  res += check_url("http://[12:12]:80/", 1);
  res += check_url("http://user:pass@[12:12]:80/", 1);
  res += check_paste("http://www.toto.com/foo/test.asx", "http://www2.toto.com/www/foo/test1.asx", "http://www2.toto.com/www/foo/test1.asx");
  res += check_paste("http://www.toto.com/foo/test.asx", "/bar/test2.asx", "http://www.toto.com/bar/test2.asx");
  res += check_paste("http://www.toto.com/foo/test.asx", "test3.asx", "http://www.toto.com/foo/test3.asx");
  printf("================================\n");
  if (res != 31) {
    printf("result: KO\n");
  } else {
    printf("result: OK\n");
  }
}
#endif
