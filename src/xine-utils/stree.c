/*
 * Copyright (C) 2021 the xine project
 * Copyright (C) 2021 Torsten Jager <t.jager@gmx.de>
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
 * Xine string tree library.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TEST_THIS_FILE
#  include "../../include/xine/stree.h"
#else
#  include <xine/stree.h>
#endif

/* 128: not a hex digit, 64: value separator, 32: -x placeholder */
static const uint8_t _tab_unhex[256] = {
  128,128,128,128,128,128,128,128,128,192,128,128,128,128,128,128,
  128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
  192,128,128,128,128,128,128,128,128,128,128,128,128,192,128,128,
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,192,128,128,128,128,128,
  128, 10, 11, 12, 13, 14, 15,128,128,128,128,128,128,128,128,128,
  128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
  128, 10, 11, 12, 13, 14, 15,161,162,163,164,165,166,167,168,169,
  170,171,172,173,174,175,176,177,178,179,180,128,128,128,128,128,
  128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
  128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
  128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
  128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
  128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
  128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
  128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
  128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128
};

size_t xine_string_unpercent (char *s) {
  const uint8_t *p = (const uint8_t *)s;
  uint8_t *q, v;

  /* optimization: skip identical write */
  while (*p && (*p != '%'))
    p++;
  q = (uint8_t *)s + (p - (const uint8_t *)s);

  while ((v = *p)) {
    p++;
    if (v == '%') do {
      uint8_t z;

      z = _tab_unhex[*p];
      if (z & 128)
        break;
      v = z;
      p++;
      z = _tab_unhex[*p];
      if (z & 128)
        break;
      v = (v << 4) | z;
      p++;
    } while (0);
    *q++ = v;
  }
  *q = 0;
  return q - (uint8_t *)s;
}

size_t xine_string_unbackslash (char *s) {
  static const uint8_t _tab_unbackslash[128] = {
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    ' ','!','"','#','$','%','&','\'','(',')','*','+',',','-','.','/',
    128,129,130,131,132,133,134,135,'8','9',':',';','<','=','>','?',
    '@',  7,  8,194,'D', 27, 12,'G','H','I','J','K','L','M', 10,'O',
    'P','Q', 13,'S',  9,193, 11,'W',192,'Y','Z','[','\\',']','^','_',
    '`',  7,  8,194,'d', 27, 12,'g','h','i','j','k','l','m', 10,'o',
    'p','q', 13,'s',  9,193, 11,'w',192,'y','z','{','|','}','~',127
  };
  const uint8_t *p = (const uint8_t *)s;
  uint8_t *q, z;

  while (*p && (*p != '\\'))
    p++;
  q = (uint8_t *)s + (p - (const uint8_t *)s);

  while ((z = *p)) {
    p++;
    do {
      if (z != '\\')
        break;
      z = *p;
      if (!z)
        break;
      p++;
      if (z & 128)
        break;
      z = _tab_unbackslash[z];
      if (!(z & 128))
        break;
      if (!(z & 0x78)) { /* oktal */
        uint8_t d;
        z &= 7;
        d = *p ^ '0';
        if (d & 0xf8)
          break;
        z = (z << 3) | d;
        p++;
        d = *p ^ '0';
        if (d & 0xf8)
          break;
        z = (z << 3) | d;
        p++;
        break;
      }
      if (z == 192) { /* x */
        uint8_t v;
        z = p[-1];
        v = _tab_unhex[*p];
        if (v & 128)
          break;
        z = v;
        p++;
        v = _tab_unhex[*p];
        if (v & 128)
          break;
        z = (z << 4) | v;
        p++;
        break;
      }
      if (z == 193) { /* u */
        uint32_t x = p[-1];
        do {
          z = _tab_unhex[*p];
          if (z & 128)
            break;
          x = z;
          p++;
          z = _tab_unhex[*p];
          if (z & 128)
            break;
          x = (x << 4) | z;
          p++;
          z = _tab_unhex[*p];
          if (z & 128)
            break;
          x = (x << 4) | z;
          p++;
          z = _tab_unhex[*p];
          if (z & 128)
            break;
          x = (x << 4) | z;
          p++;
        } while (0);
        z = x;
        if (x & 0xff80) { /* utf8 */
          if (x & 0xf800) { /* 1110xxxx 10xxxxxx 10xxxxxx */
            *q++ = 0xe0 | (x >> 12);
            *q++ = 0x80 | ((x >> 6) & 0x3f);
          } else { /* 110xxxxx 10xxxxxx */
            *q++ = 0xc0 | (x >> 6);
          }
          z &= 0x3f;
          z |= 0x80;
        }
        break;
      }
      /* (z == 194), c */
      z = p[0] & 0x1f, p++;
    } while (0);
    *q++ = z;
  }
  *q = 0;
  return q - (uint8_t *)s;
}

static int _xine_stree_node_new (xine_stree_t **root, uint32_t *have, uint32_t *used, uint32_t parent) {
  xine_stree_t *p, *n;
  if (*used >= *have) {
    xine_stree_t *new = realloc (*root, (*have + 64) * sizeof (**root));
    if (!new)
      return 0;
    *root = new;
    *have += 64;
  }
  p = *root + parent;
  n = *root + *used;
  n->key = 0;
  n->value = 0;
  n->level = p->level + 1;
  n->parent = parent;
  n->next = 0;
  n->first_child = 0;
  n->last_child = 0;
  n->num_children = 0;
  if (p->last_child) {
    n->prev = p->last_child;
    (*root)[p->last_child].next = *used;
  } else {
    p->first_child = *used;
    n->prev = 0;
  }
  p->last_child = *used;
  p->num_children += 1;
  *used += 1;
  return *used - 1;
}

static uint8_t *_xine_stree_get_string (uint8_t **p, uint8_t **q, const uint8_t *tab) {
  uint8_t *r = *q;
  if (**p == '\"') {
    (*p)++;
    while (!(tab[**p] & (2 | 128))) {
      while (!(tab[**p] & (2 | 64 | 128)))
        *(*q)++ = *(*p)++;
      if (**p == '\\')
        *(*q)++ = *(*p)++;
    }
    if (**p == '\"')
      (*p)++;
  } else if (**p == '\'') {
    (*p)++;
    while (!(tab[**p] & (4 | 128))) {
      while (!(tab[**p] & (4 | 64 | 128)))
        *(*q)++ = *(*p)++;
      if (**p == '\\')
        *(*q)++ = *(*p)++;
    }
    if (**p == '\'')
      (*p)++;
  } else {
    while (!(tab[**p] & (1 | 8 | 16 | 32 | 128))) {
      while (!(tab[**p] & (1 | 8 | 16 | 32 | 64 | 128)))
        *(*q)++ = *(*p)++;
      if (**p == '\\')
        *(*q)++ = *(*p)++;
    }
  }
  return r;
}

static const uint8_t _tab_xml[256] = {
  ['\r'] = 1,
  ['\n'] = 1,
  ['\t'] = 1,
  [' '] = 1,
  ['\"'] = 2,
  ['\''] = 4,
  ['<'] = 8,
  ['>'] = 16,
  ['='] = 32,
  ['\\'] = 64,
  [0] = 128
};

static xine_stree_t *_xine_stree_load_xml (char *buf) {
  xine_stree_t *root;
  uint32_t have, used, here;
  uint8_t *p, *q, *e;

  have = 64;
  root = (xine_stree_t *)malloc (have * sizeof (*root));
  if (!root)
    return NULL;

  here = 0;
  used = 1;
  root->next = root->prev = 0;
  root->first_child = root->last_child = root->parent = 0;
  root->num_children = root->level = root->index = 0;
  root->key = root->value = 0;

  e = p = (uint8_t *)buf;
  q = (uint8_t *)buf + 1;
  while (*p) {
    while (_tab_xml[p[0]] & 1)
      p++;
    if (p[0] == '<') {
      p++;
      if (p[0] == '!') { /* <!comment ... /> */
        p++;
        /* NOTE: CDATA pseudo comments not yet supported. */
        while (!(_tab_xml[*p] & (16 | 128)))
          p++;
        if (*p == '>')
          p++;
      } else if (p[0] == '/') { /* </tag> */
        uint8_t *v, z;
        p++;
        /* defer writing string end until that location is not read any more. */
        *e = 0;
        v = _xine_stree_get_string (&p, &q, _tab_xml);
        e = q++;
        z = *e;
        *e = 0;
        if (*v) { /* _not_ just </> */
          while (root[here].level) {
            if (!strcasecmp ((const char *)buf + root[here].key, (const char *)v))
              break;
            here = root[here].parent;
          }
        }
        *e = z;
        here = root[here].parent;
        while (!(_tab_xml[*p] & (16 | 128)))
          p++;
        if (*p == '>')
          p++;
      } else { /* <reguler_tag ... */
        uint32_t new = _xine_stree_node_new (&root, &have, &used, here);
        if (!new)
          return root;
        *e = 0;
        root[new].key = _xine_stree_get_string (&p, &q, _tab_xml) - (uint8_t *)buf;
        e = q++;
        here = new;
      }
    } else if (*p == '/') { /* ... /> */
      p++;
      if (*p == '>') {
        p++;
        here = root[here].parent;
      }
    } else if (!(_tab_xml[*p] & (1 | 16 | 128))) { /* ... key=value ... */
      uint32_t new = _xine_stree_node_new (&root, &have, &used, here);
      if (!new)
        return root;
      *e = 0;
      root[new].key = _xine_stree_get_string (&p, &q, _tab_xml) - (uint8_t *)buf;
      e = q++;
      if (*p == '=')
        p++;
      *e = 0;
      root[new].value = _xine_stree_get_string (&p, &q, _tab_xml) - (uint8_t *)buf;
      e = q++;
    } else if (*p == '>') { /* <tag_with_inner_text ...>here</... */
      p++;
      while (_tab_xml[*p] & 1)
        p++;
      if (!(_tab_xml[*p] & (8 | 128))) {
        *e = 0;
        root[here].value = _xine_stree_get_string (&p, &q, _tab_xml) - (uint8_t *)buf;
        e = q++;
      }
    } else {
      p++;
    }
  }
  *e = 0;
  buf[0] = 0;
  return root;
}

static const uint8_t _tab_json1[256] = {
  ['\r'] = 1,
  ['\n'] = 1,
  ['\t'] = 1,
  [' '] = 1,
  ['\"'] = 2,
  ['\''] = 4,
  ['{'] = 8,
  ['['] = 8,
  [']'] = 16,
  ['}'] = 16,
  [','] = 32,
  [':'] = 32,
  ['\\'] = 64,
  [0] = 128
};

static const uint8_t _tab_json2[256] = {
  ['/'] = 1,
  ['*'] = 2,
  ['\n'] = 4,
  [0] = 128
};

static xine_stree_t *_xine_stree_load_json (char *buf) {
  xine_stree_t *root;
  uint32_t have, used, here, new;
  uint8_t *p, *q, *e;

  have = 64;
  root = (xine_stree_t *)malloc (have * sizeof (*root));
  if (!root)
    return NULL;

  here = 0;
  used = 1;
  root->next = root->prev = 0;
  root->first_child = root->last_child = root->parent = 0;
  root->num_children = root->level = root->index = 0;
  root->key = root->value = 0;
  new = 0;

  e = p = (uint8_t *)buf;
  q = (uint8_t *)buf + 1;
  if (_tab_json1[*p] & 8) /* {[ */
    p++;
  while (*p) {
    /* skip whitespace */
    while (_tab_json1[*p] & 1)
      p++;
    /* skip comment */
    if (*p == '/') {
      if (p[1] == '*') { /* C comment */
        p += 2;
        do {
          while (!(_tab_json2[*p] & (2 | 128)))
            p++;
          if (!*p)
            break;
          p++;
        } while (*p != '/');
        if (*p == '/')
          p++;
        continue;
      } 
      if (p[1] == '/') { /* C++ comment */
        p += 2;
        while (!(_tab_json2[*p] & (4 | 128)))
          p++;
        if (*p == '\n')
          p++;
        continue;
      }
    }
    if (_tab_json1[*p] & 8) { /* {[ */
      uint32_t new = root[here].last_child;
      p++;
      if (!new) {
        new = _xine_stree_node_new (&root, &have, &used, here);
        if (!new)
          break;
      }
      root[new].level &= ~0x80000000;
      here = new;
      continue;
    }
    if (_tab_json1[*p] & 32) { /* :, */
      uint32_t item = root[here].last_child;
      root[item].level &= ~0x80000000;
      if (*p == ',') {
        item = _xine_stree_node_new (&root, &have, &used, here);
        if (!item)
          break;
        root[item].level |= 0x80000000;
      }
      p++;
      continue;
    }
    if (_tab_json1[*p] & 16) { /* ]} */
      uint32_t item = root[here].last_child;
      if (item)
        root[item].level &= ~0x80000000;
      p++;
      if (!root[here].level)
        break;
      here = root[here].parent;
      continue;
    }
    {
      uint8_t *v;
      uint32_t item = root[here].last_child;
      if (!item) {
        item = _xine_stree_node_new (&root, &have, &used, here);
        if (!item)
          break;
        root[item].level |= 0x80000000;
      }
      v = _xine_stree_get_string (&p, &q, _tab_json1);
      if (v != q) {
        /* defer writing string end until that location is not read any more. */
        *e = 0;
        e = q++;
        if (root[item].level & 0x80000000) {
          root[item].key = v - (uint8_t *)buf;
        } else {
          root[item].value = v - (uint8_t *)buf;
          /* this will be just right in most cases :-) */
          xine_string_unbackslash ((char *)v);
        }
      }
    }
  }
  *e = 0;
  buf[0] = 0;
  return root;
}

static const uint8_t _tab_url[256] = {
  ['?'] = 1,
  ['#'] = 2,
  ['&'] = 4,
  ['='] = 8,
  ['\\'] = 64,
  [0] = 128
};

static xine_stree_t *_xine_stree_load_url (char *buf) {
  xine_stree_t *root;
  uint32_t have, used, here, new, key;
  uint8_t *p, *e, dummy;

  have = 64;
  root = (xine_stree_t *)malloc (have * sizeof (*root));
  if (!root)
    return NULL;

  used = 1;
  root->next = root->prev = 0;
  root->first_child = root->last_child = root->parent = 0;
  root->num_children = root->level = root->index = 0;
  root->key = root->value = 0;
  new = 0;

  p = (uint8_t *)buf;

  /* skip [http://host/path?]key1=val1&key2=val2#extra */
  while (1) {
    while (!(_tab_url[*p] & (1 | 64 | 128))) /* ? \\ \0 */
      p++;
    if (*p != '\\')
      break;
    p++;
  }
  if (*p)
    p++;
  else
    p = (uint8_t *)buf;

  /* cut key1=val1&key2=val2[#extra] */
  e = p;
  while (1) {
    while (!(_tab_url[*p] & (2 | 64 | 128))) /* # \\ \0 */
      p++;
    if (*p != '\\')
      break;
    p++;
  }
  *p = 0;
  p = e;

  e = &dummy;
  here = 0;
  key = 1;
  while (*p) {
    uint8_t *r = p;
    if (key) {
      while (1) {
        while (!(_tab_url[*p] & (4 | 8 | 64 | 128))) /* & = \\ \0 */
          p++;
        if (*p != '\\')
          break;
        p++;
      }
    } else {
      /* value may contain any number of equal signs. */
      while (1) {
        while (!(_tab_url[*p] & (4 | 64 | 128))) /* & \\ \0 */
          p++;
        if (*p != '\\')
          break;
        p++;
      }
    }
    if (!here) {
      here = _xine_stree_node_new (&root, &have, &used, 0);
      if (!here)
        break;
      /* NOTE: buf may start with a key, thus we cannot put a global empty string there. */
      root[here].key = root[here].value = p - (uint8_t *)buf;
    }
    if (key) {
      root[here].key = r - (uint8_t *)buf;
    } else {
      root[here].value = r - (uint8_t *)buf;
      /* this will be just right in most cases :-) */
      xine_string_unpercent ((char *)r);
    }
    if (*p == '&') {
      *e = 0;
      e = p++;
      here = 0;
      key = 1;
    } else if (*p == '=') {
      *e = 0;
      e = p++;
      key = 0;
    }
  }
  *e = 0;
  root->key = root->value = p - (uint8_t *)buf;
  return root;
}

xine_stree_t *xine_stree_load (char *buf, xine_stree_mode_t *mode) {
  xine_stree_mode_t m2 = XINE_STREE_AUTO;
  if (!buf)
    return NULL;
  if (!mode)
    mode = &m2;
  if (*mode >= XINE_STREE_LAST)
    return NULL;
  if (*mode == XINE_STREE_AUTO) {
    const uint8_t *p = (const uint8_t *)buf;
    while (_tab_xml[*p] & 1)
      p++;
    if (*p == '<') {
      *mode = XINE_STREE_XML;
    } else if ((*p == '{') || (*p == '[')) {
      *mode = XINE_STREE_JSON;
    } else {
      *mode = XINE_STREE_URL;
    }
  }
  switch (*mode) {
    case XINE_STREE_XML: return _xine_stree_load_xml (buf);
    case XINE_STREE_JSON: return _xine_stree_load_json (buf);
    case XINE_STREE_URL: return _xine_stree_load_url (buf);
    default: return NULL;
  }
}

void xine_stree_dump (const xine_stree_t *tree, const char *buf, uint32_t base) {
  static const char spc[] = "                                ";
  const xine_stree_t *here, *test, *stop;
  uint32_t index;

  if (!tree || !buf)
    return;

  here = tree + base;
  stop = base ? here : NULL;
  for (index = 1, test = here; test->prev; index += 1, test = tree + test->prev) ;
  while (1) {
    printf ("%s[%d:%d] \"%s\" = \"%s\"\n",
      spc + (sizeof (spc) - 1) - 2 * (here->level > (sizeof (spc) - 1) / 2 ? sizeof (spc) - 1 : here->level),
      (int)here->level, (int)index, buf + here->key, buf + here->value);
    if (here->first_child) {
      index = 0;
      here = tree + here->first_child;
    } else if (here == stop) {
      break;
    } else if (here->next) {
      index += 1;
      here = tree + here->next;
    } else {
      while (here->level) {
        here = tree + here->parent;
        if (here->next)
          break;
      }
      if (!here->next)
        break;
      for (index = 1, test = here; test->prev; index += 1, test = tree + test->prev) ;
      here = tree + here->next;
    }
  }
}

uint32_t xine_stree_find (const xine_stree_t *tree, const char *buf, const char *path, uint32_t base, int case_sens) {
  const xine_stree_t *here;
  const uint8_t *s;
  uint8_t part[512], *q, *e;

  if (!tree || !buf)
    return 0;
  if (!path)
    return base;

  here = tree + base;
  e = part + sizeof (part) - 1;
  s = (const uint8_t *)path;
  while (1) {
    uint32_t v;
    while (*s == '.')
      s++;
    if (!*s)
      return here - tree;
    if (!here->first_child)
      return 0;
    here = tree + here->first_child;
    q = part;
    while (*s && (*s != '[') && (*s != '.') && (q < e))
      *q++ = *s++;
    if (q >= e)
      return 0;
    *q = 0;
    v = 0;
    if (*s == '[') {
      uint8_t z;
      s++;
      while (_tab_xml[*s] & 1)
        s++;
      while ((z = *s ^ '0') < 10)
        v = 10u * v + z, s++;
      while (_tab_xml[*s] & 1)
        s++;
      if (*s == ']')
        s++;
    }
    if (*part) {
      if (case_sens) {
        while (1) {
          if (!strcmp ((const char *)buf + here->key, (const char *)part)) {
            if (!v)
              break;
            v--;
          }
          if (!here->next)
            return 0;
          here = tree + here->next;
        }
      } else {
        while (1) {
          if (!strcasecmp ((const char *)buf + here->key, (const char *)part)) {
            if (!v)
              break;
            v--;
          }
          if (!here->next)
            return 0;
          here = tree + here->next;
        }
      }
    } else {
      while (v && here->next)
        v--, here = tree + here->next;
      if (v)
        return 0;
    }
  }
}

void xine_stree_delete (xine_stree_t **tree) {
  if (tree) {
    free (*tree);
    *tree = NULL;
  }
}

#ifdef TEST_THIS_FILE
int main (int argc, char **argv) {
  (void)argc;

  do {
    FILE *f;
    size_t fsize;
    char *buf;
    xine_stree_t *tree;
    uint32_t here;
    xine_stree_mode_t mode;

    if (!argv[0])
      break;
    if (!argv[1])
      break;

    f = fopen (argv[1], "rb");
    if (!f)
      break;

    fseek (f, 0, SEEK_END);
    fsize = ftell (f);
    fseek (f, 0, SEEK_SET);
    buf = malloc (fsize + 1);
    if (!buf) {
      fclose (f);
      break;
    }

    fread (buf, 1, fsize, f);
    fclose (f);
    buf[fsize] = 0;
    mode = XINE_STREE_AUTO;
    tree = xine_stree_load (buf, &mode);
    if (!tree) {
      free (buf);
      break;
    }

    here = xine_stree_find (tree, buf, argv[2], 0, mode == XINE_STREE_JSON);
    if (!argv[2] || here)
      xine_stree_dump (tree, buf, here);
    xine_stree_delete (&tree);
    free (buf);
    return 0;
  } while (0);

  {
    static const char helptext[] = "usage: stree <file> [<path>]\n";

    fwrite (helptext, 1, sizeof (helptext) - 1, stdout);
  }
  return 1;
}
#endif
