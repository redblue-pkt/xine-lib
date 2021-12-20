/* kate: tab-indent on; indent-width 4; mixedindent off; indent-mode cstyle; remove-trailing-space on; */
/*
 * Copyright (C) 2008-2013 the xine project
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
 */

#ifndef ALTERH264_BITS_READER_H
#define ALTERH264_BITS_READER_H
#include <sys/types.h>
#include <inttypes.h>
#include <stdio.h>

typedef struct {
  const uint32_t *read;
  const uint8_t *start, *end;
  uint32_t val, bits, oflow;
} bits_reader_t;

static void bits_set_buf (bits_reader_t *br, const uint8_t *buf, uint32_t len) {
  const union {
    uint32_t word;
    uint8_t  little;
  } endian_is = {1};
  uint32_t v;

  br->start = buf;
  br->end = buf + len;
  br->read = (const uint32_t *)((uintptr_t)buf & ~(uintptr_t)3);
  br->bits = 32 - (buf - (const uint8_t *)br->read) * 8;
  v = *br->read++;
  if (endian_is.little)
    v = (v >> 24) | ((v >> 8) & 0x0000ff00) | ((v << 8) & 0x00ff0000) | (v << 24);
  br->val = v << (32 - br->bits);
  br->oflow = 0;
}

static inline uint32_t bits_tell (bits_reader_t *br) {
  return ((const uint8_t *)br->read - br->start) * 8 - br->bits;
}

/* NOTE: mathematically, uint32_t << 32 yields 0.
 * however, real life truncates the shift width to 5 bits (32 == 0),
 * and thus has no effect. lets use some paranoia that gcc will
 * optimize away in most cases where width is constant. */

/** NOTE: old code bailed out when end of bitstream was reached exactly. */
static uint32_t _bits_read_slow (bits_reader_t *br, uint32_t bits) {
  const union {
    uint32_t word;
    uint8_t  little;
  } endian_is = {1};
  uint32_t v1, v2;
  int left = (br->end - (const uint8_t *)br->read) * 8;

  if (br->bits) {
    v1 = br->val >> (32 - br->bits);
    bits -= br->bits;
    v1 <<= bits;
  } else {
    v1 = 0;
  }
  if (left < 32) {
    if (left < (int)bits) {
      br->read = (const uint32_t *)(((uintptr_t)br->end + 3) & ~(uintptr_t)3);
      br->bits = 0;
      br->oflow = 1;
      return 0;
    }
  } else {
    left = 32;
  }
  v2 = *br->read++;
  if (endian_is.little)
    v2 = (v2 >> 24) | ((v2 >> 8) & 0x0000ff00) | ((v2 << 8) & 0x00ff0000) | (v2 << 24);
  /* bits > 0 is sure here. */
  v1 |= v2 >> (32 - bits);
  br->val = v2 << bits;
  br->bits = left - bits;
  return v1;
}

/** bits <= 32 */
static inline uint32_t bits_read (bits_reader_t *br, const uint32_t bits) {
  uint32_t v;

  if (!bits)
    return 0;
  if (br->bits >= bits) {
    v = br->val >> (32 - bits);
    br->val <<= bits;
    br->bits -= bits;
    return v;
  }
  return _bits_read_slow (br, bits);
}

static void _bits_skip_slow (bits_reader_t *br, uint32_t bits) {
  const union {
    uint32_t word;
    uint8_t  little;
  } endian_is = {1};
  uint32_t v2;
  int left = (br->end - (const uint8_t *)br->read) * 8;

  bits -= br->bits;
  left -= bits;
  if (left < 0) {
    br->read = (const uint32_t *)(((uintptr_t)br->end + 3) & ~(uintptr_t)3);
    br->bits = 0;
    br->oflow = 1;
    return;
  }
  br->read += bits >> 5;
  v2 = *br->read++;
  if (endian_is.little)
    v2 = (v2 >> 24) | ((v2 >> 8) & 0x0000ff00) | ((v2 << 8) & 0x00ff0000) | (v2 << 24);
  bits &= 31;
  br->val = v2 << bits;
  br->bits = (left >= 32 ? 32 : left) - bits;
}

/** bits unlimited */
static inline void bits_skip (bits_reader_t *br, const uint32_t bits) {
  if (!bits)
    return;
  if (br->bits >= bits) {
    br->val <<= bits;
    br->bits -= bits;
  } else {
    _bits_skip_slow (br, bits);
  }
}

/* needing this func at all is a nasty kludge.
 * h.264 PPS has an optional extension that has been added without defining
 * a presence flag earlier... */
/** how many bits are left from here to the last "1"? NOTE: old code was off by -1. */
static uint32_t bits_valid_left (bits_reader_t *br) {
  static const uint32_t mask[4] = {0x00000000, 0xff000000, 0xffff0000, 0xffffff00};
  const union {
    uint32_t word;
    uint8_t  little;
  } endian_is = {1};
  uint32_t v;
  int n;

  do {
    /* search yet unread bits for last "1". */
    if ((const uint8_t *)br->read < br->end) {
      const uint32_t *p = (const uint32_t *)((uintptr_t)br->end & ~(uintptr_t)3);

      n = br->end - (const uint8_t *)p;
      if (n > 0) {
        v = *p;
        if (endian_is.little)
          v = (v >> 24) | ((v >> 8) & 0x0000ff00) | ((v << 8) & 0x00ff0000) | (v << 24);
        v &= mask[n];
      } else {
        v = 0;
      }

      while (!v && (p > br->read)) {
        v = *--p;
        if (endian_is.little)
          v = (v >> 24) | ((v >> 8) & 0x0000ff00) | ((v << 8) & 0x00ff0000) | (v << 24);
      }
      n = (p - br->read) * 32 + br->bits;
      if (v)
        break;
    }
    /* well, only value cache is left to test. */
    if (!br->bits)
      return 0;
    /* for performance, we dont end mask br->val generally. */
    n = 32 - br->bits;
    v = br->val >> n << n;
    n = 0;
  } while (0);

  while (v)
    n++, v <<= 1;
  return n;
}

static uint32_t bits_exp_ue (bits_reader_t * br) {
  const union {
    uint32_t word;
    uint8_t  little;
  } endian_is = {1};
  uint32_t size;
  /* count leading 0 bits */
  if (br->bits && br->val) {
    uint32_t v1 = br->val;

    size = 0;
    while (!(v1 & 0x80000000))
      v1 <<= 1, size++;
    br->val = v1;
    br->bits -= size;
  } else {
    int left = (br->end - (const uint8_t *)br->read) * 8;
    uint32_t v2, rest;

    if (left <= 0) {
      br->read = (const uint32_t *)(((uintptr_t)br->end + 3) & ~(uintptr_t)3);
      br->bits = 0;
      br->oflow = 1;
      return 0;
    }
    size = br->bits;
    rest = 32 - size;
    if (rest > (uint32_t)left)
      rest = left;
    v2 = *br->read++;
    if (endian_is.little)
      v2 = (v2 >> 24) | ((v2 >> 8) & 0x0000ff00) | ((v2 << 8) & 0x00ff0000) | (v2 << 24);
    if (v2 & (0xffffffff << (32 - rest))) {
      while (!(v2 & 0x80000000))
        v2 <<= 1, size++;
    } else {
      v2 <<= rest;
      size += rest;
    }
    br->val = v2;
    br->bits = (left > 32 ? 32 : left) + br->bits - size;
  }
  /* get sized value */
  size++;
  if (br->bits >= size) {
    uint32_t res = br->val >> (32 - size);

    br->val <<= size;
    br->bits -= size;
    return res - 1;
  } else {
    uint32_t v2, res;
    int left = (br->end - (const uint8_t *)br->read) * 8;

    size -= br->bits;
    if (left < (int)size) {
      br->read = (const uint32_t *)(((uintptr_t)br->end + 3) & ~(uintptr_t)3);
      br->bits = 0;
      br->oflow = 1;
      return 0;
    }
    res = br->bits ? br->val >> (32 - br->bits) : 0;
    v2 = *br->read++;
    if (endian_is.little)
      v2 = (v2 >> 24) | ((v2 >> 8) & 0x0000ff00) | ((v2 << 8) & 0x00ff0000) | (v2 << 24);
    res = (res << size) + (v2 >> (32 - size));
    br->val = v2 << size;
    br->bits = (left > 32 ? 32 : left) - size;
    return res - 1;
  }
}

static inline int32_t bits_exp_se (bits_reader_t * br) {
  uint32_t res = bits_exp_ue (br);

  return (res & 1) ? (int32_t)((res + 1) >> 1) : -(int32_t)(res >> 1);
}

#ifdef TEST_THIS_FILE
#  include <stdio.h>

int main (int argc, char **argv) {
  static const uint8_t test[] = "\x75\x99\xfb\x07\x55\xd8\xff\x23\x11\xab\xa8";
  bits_reader_t br;
  unsigned int v1, v2, v3, v4, v5, v6, v7, v8, v9, m;

  (void)argc;
  (void)argv;
  bits_set_buf (&br, test + 1, sizeof (test) - 1);
  bits_skip (&br, 1);
  v1 = bits_read (&br, 3);
  v2 = bits_read (&br, 7);
  v3 = bits_read (&br, 5);
  bits_skip (&br, 8);
  v4 = bits_read (&br, 8);
  bits_skip (&br, 7);
  v5 = bits_read (&br, 1);
  m = bits_valid_left (&br);
  v6 = bits_read (&br, 12);
  v7 = bits_read (&br, 4);
  v8 = bits_read (&br, 23);
  v9 = bits_read (&br, 9);
  printf ("%s\n", __FILE__);
  printf ("(1)        3        7        5 (8)        8 (7)        1       12        4       23        9\n");
  printf ("--- %08x %08x %08x --- %08x --- %08x %08x %08x %08x %08x\n", v1, v2, v3, v4, v5, v6, v7, v8, v9);
  printf ("more: %08x\n", m);
  return 0;
}

#endif
#endif /* ALTERH264_BITS_READER_H */

