/*
 * Copyright (C) 2010-2019 the xine project
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
 * DVB Subtitle decoder (ETS 300 743)
 * (c) 2004 Mike Lampard <mlampard@users.sourceforge.net>
 * based on the application dvbsub by Dave Chapman
 *
 * TODO:
 * - Implement support for teletext based subtitles
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>
#include <pthread.h>
#include <errno.h>

/*#define LOG*/
#define LOG_MODULE "spudvb"

#include <xine/xine_internal.h>
#include <xine/spu.h>
#include <xine/osd.h>

#define MAX_REGIONS 16

#define SPU_MAX_WIDTH 1920
#define SPU_MAX_HEIGHT 1080

/* sparse_array - handle large arrays efficiently when only a few entries are used */

typedef struct {
  uint32_t key, value;
} sparse_array_entry_t;

typedef struct {
  uint32_t sorted_entries, used_entries, max_entries;
  sparse_array_entry_t *entries;
} sparse_array_t;

static void sparse_array_new (sparse_array_t *sa) {
  sa->sorted_entries =
  sa->used_entries   =
  sa->max_entries    = 0;
  sa->entries        = NULL;
}

static void sparse_array_delete (sparse_array_t *sa) {
  sa->sorted_entries =
  sa->used_entries   =
  sa->max_entries    = 0;
  _x_freep (&sa->entries);
}

static int _sparse_array_find (sparse_array_t *sa, uint32_t key, uint32_t *pos) {
  uint32_t b = 0, m, e = sa->sorted_entries, l = e, k;
  while (1) {
    m = (b + e) >> 1;
    if (m == l)
      break;
    l = m;
    k = sa->entries[m].key;
    if (k > key)
      e = m;
    else if (k < key)
      b = m;
    else {
      *pos = m;
      return 1;
    }
  }
  *pos = e;
  return 0;
}

static void _sparse_array_sort (sparse_array_t *sa) {
  uint32_t left = sa->max_entries - sa->used_entries;
  /* move unsorted part to end of buf */
  uint32_t i = left + sa->sorted_entries;
  memmove (sa->entries + i, sa->entries + sa->sorted_entries,
    (sa->used_entries - sa->sorted_entries) * sizeof (sparse_array_entry_t));
  /* iterate it */
  while (i < sa->max_entries) {
    uint32_t j, pos, startkey, stopkey, lastkey;
    startkey = sa->entries[i].key;
    if (_sparse_array_find (sa, startkey, &pos)) {
      /* eliminate duplicate */
      sa->entries[pos].value = sa->entries[i].value;
      i++;
      continue;
    }
    /* find sorted range */
    stopkey = pos < sa->sorted_entries ? sa->entries[pos].key : 0xffffffff;
    lastkey = startkey;
    for (j = i + 1; j < sa->max_entries; j++) {
      uint32_t thiskey = sa->entries[j].key;
      if ((thiskey <= lastkey) || (thiskey >= stopkey))
        break;
      lastkey = thiskey;
    }
    j -= i;
    if (j > left)
      j = left;
    /* insert it */
    if (pos < sa->sorted_entries)
      memmove (sa->entries + pos + j, sa->entries + pos,
        (sa->sorted_entries - pos) * sizeof (sparse_array_entry_t));
    memcpy (sa->entries + pos, sa->entries + i, j * sizeof (sparse_array_entry_t));
    sa->sorted_entries += j;
    i += j;
  }
  sa->used_entries = sa->sorted_entries;
  lprintf ("sparse_array_sort: %u entries\n", (unsigned int)sa->used_entries);
}

static int sparse_array_set (sparse_array_t *sa, uint32_t key, uint32_t value) {
  /* give some room for later sorting too */
  if (!sa->entries || (sa->used_entries + 8 >= sa->max_entries)) {
    uint32_t n = sa->max_entries + 128;
    sparse_array_entry_t *se = realloc (sa->entries, n * sizeof (sparse_array_entry_t));
    if (!se)
      return 0;
    sa->max_entries = n;
    sa->entries = se;
  }
  sa->entries[sa->used_entries].key = key;
  sa->entries[sa->used_entries++].value = value;
  return 1;
}

static int sparse_array_get (sparse_array_t *sa, uint32_t key, uint32_t *value) {
  uint32_t pos;
  if (sa->sorted_entries != sa->used_entries)
    _sparse_array_sort (sa);
  if (!_sparse_array_find (sa, key, &pos))
    return 0;
  *value = sa->entries[pos].value;
  return 1;
}

static void sparse_array_unset (sparse_array_t *sa, uint32_t key, uint32_t mask) {
  sparse_array_entry_t *here = sa->entries, *p = NULL, *q = sa->entries;
  uint32_t i, n = 0;
  if (sa->sorted_entries != sa->used_entries)
    _sparse_array_sort (sa);
  key &= mask;
  for (i = sa->used_entries; i > 0; i--) {
    if ((here->key & mask) == key) {
      if (p) {
        n = here - p;
        if (n && (p != q))
          memmove (q, p, n * sizeof (sparse_array_entry_t));
        p = NULL;
        q += n;
      }
    } else {
      if (!p)
        p = here;
    }
    here++;
  }
  if (p) {
    n = here - p;
    if (n && (p != q))
      memmove (q, p, n * sizeof (sparse_array_entry_t));
    q += n;
  }
  sa->sorted_entries =
  sa->used_entries   = q - sa->entries;
}

/* ! sparse_array */

typedef struct {
  int                   x, y;
  unsigned char         is_visible;
} visible_region_t;

typedef struct {
  int                   page_time_out;
  int                   page_version_number;
  int                   page_state;
  int                   page_id;
  visible_region_t      regions[MAX_REGIONS];
} page_t;

typedef struct {
  int                   version_number;
  int                   width, height;
  int                   empty;
  int                   depth;
  int                   CLUT_id;
  int                   objects_start;
  int                   objects_end;
  unsigned char         *img;
  osd_object_t          *osd;
} region_t;

typedef union {
  clut_t   c;
  uint32_t u32;
} clut_union_t;

typedef struct {
/* dvbsub stuff */
  int                   x;
  int                   y;
  unsigned int          curr_obj;
  unsigned int          curr_reg[64];
  uint8_t              *buf;
  int                   i;
  int                   i_bits;
  int                   compat_depth;
  int                   max_regions;
  page_t                page;
  region_t              regions[MAX_REGIONS];
  clut_union_t          colours[MAX_REGIONS*256];
  unsigned char         trans[MAX_REGIONS*256];
  struct {
    unsigned char         lut24[4], lut28[4], lut48[16];
  }                     lut[MAX_REGIONS];
  sparse_array_t        object_pos;
} dvbsub_func_t;

typedef struct dvb_spu_decoder_s {
  spu_decoder_t spu_decoder;

  xine_stream_t        *stream;

  spu_dvb_descriptor_t spu_descriptor;

  /* dvbsub_osd_mutex should be locked around all calls to this->osd_renderer->show()
     and this->osd_renderer->hide() */
  pthread_mutex_t       dvbsub_osd_mutex;

  char                 *pes_pkt;
  char                 *pes_pkt_wrptr;
  unsigned int          pes_pkt_size;

  int64_t               vpts;

  pthread_t             dvbsub_timer_thread;
  struct timespec       dvbsub_hide_timeout;
  pthread_cond_t        dvbsub_restart_timeout;
  dvbsub_func_t        *dvbsub;
  int                   show;
} dvb_spu_decoder_t;

static clut_t default_clut[256];
static unsigned char default_trans[256];
static int default_colours_init = 0;

static void reset_clut (dvbsub_func_t *dvbsub)
{
  int i, r;

  /* Reset the colour LUTs */
  for (r = 0; r < MAX_REGIONS; ++r)
  {
    memcpy (dvbsub->colours + r * 256, default_clut, sizeof (default_clut));
    memcpy (dvbsub->trans + r * 256, default_trans, sizeof (default_trans));
  }

  /* Reset the colour index LUTs */
  for (r = 0; r < MAX_REGIONS; ++r)
  {
    dvbsub->lut[r].lut24[0] = 0x0;
    dvbsub->lut[r].lut24[1] = 0x7;
    dvbsub->lut[r].lut24[2] = 0x8;
    dvbsub->lut[r].lut24[3] = 0xF;
    dvbsub->lut[r].lut28[0] = 0x00;
    dvbsub->lut[r].lut28[1] = 0x77;
    dvbsub->lut[r].lut28[2] = 0x88;
    dvbsub->lut[r].lut28[3] = 0xFF;
    for (i = 0; i < 16; ++i)
      dvbsub->lut[r].lut48[i] = i | i << 4;
  }
}

static void update_osd(dvb_spu_decoder_t *this, region_t *reg)
{
  if ( !reg->img ) {
    if ( reg->osd ) {
      pthread_mutex_lock( &this->dvbsub_osd_mutex );
      this->stream->osd_renderer->free_object( reg->osd );
      reg->osd = NULL;
      pthread_mutex_unlock( &this->dvbsub_osd_mutex );
    }
    return;
  }

  if ( reg->osd ) {
    if ( reg->width!=reg->osd->width || reg->height!=reg->osd->height ) {
      pthread_mutex_lock( &this->dvbsub_osd_mutex );
      this->stream->osd_renderer->free_object( reg->osd );
      reg->osd = NULL;
      pthread_mutex_unlock( &this->dvbsub_osd_mutex );
    }
  }

  if ( !reg->osd )
    reg->osd = this->stream->osd_renderer->new_object( this->stream->osd_renderer, reg->width, reg->height );
}

static void update_region (region_t *reg, int region_id, int region_width, int region_height, int fill, int fill_color)
{
  /* reject invalid sizes and set some limits ! */
  if ( region_width<=0 || region_height<=0 || region_width>SPU_MAX_WIDTH || region_height>SPU_MAX_HEIGHT ) {
    _x_freep( &reg->img );
    lprintf("rejected region %d = %dx%d\n", region_id, region_width, region_height );
    return;
  }

  if ( reg->width*reg->height<region_width*region_height ) {
    lprintf("update size of region %d = %dx%d\n", region_id, region_width, region_height);
    _x_freep( &reg->img );
  }

  if ( !reg->img ) {
    if ( !(reg->img = calloc(1, region_width * region_height)) ) {
      lprintf( "can't allocate mem for region %d\n", region_id );
      return;
    }
    fill = 1;
  }

  if ( fill ) {
    memset( reg->img, fill_color, region_width*region_height );
    reg->empty = 1;
    lprintf("FILL REGION %d\n", region_id);
  }
  reg->width = region_width;
  reg->height = region_height;
}


static void do_plot (region_t *reg, int x, int y, unsigned char pixel)
{
  int i;

  i = (y * reg->width) + x;
  /* do some clipping */
  if (i < reg->width * reg->height) {
    reg->img[i] = pixel;
    reg->empty = 0;
  }
}

static void plot (dvbsub_func_t *dvbsub, int r, int run_length, unsigned char pixel)
{
  int x2 = dvbsub->x + run_length;

  while (dvbsub->x < x2) {
    do_plot (&dvbsub->regions[r], dvbsub->x, dvbsub->y, pixel);
    dvbsub->x++;
  }
}

static const uint8_t *lookup_lut (const dvbsub_func_t *dvbsub, int r)
{
  static const uint8_t identity_lut[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

  switch (dvbsub->compat_depth)
  {
  case 012: return dvbsub->lut[r].lut24;
  case 013: return dvbsub->lut[r].lut28;
  case 023: return dvbsub->lut[r].lut48;
  default:  return identity_lut;
  }
}

static unsigned char next_datum (dvbsub_func_t *dvbsub, int width)
{
  unsigned char x = 0;

  if (!dvbsub->i_bits)
    dvbsub->i_bits = 8;

  if (dvbsub->i_bits < width)
  {
    /* need to read from more than one byte; split it up */
    width -= dvbsub->i_bits;
    x = dvbsub->buf[dvbsub->i++] & ((1 << dvbsub->i_bits) - 1);
    dvbsub->i_bits = 8;
    return x << width | next_datum (dvbsub, width);
  }

  dvbsub->i_bits = (dvbsub->i_bits - width) & 7;
  x = (dvbsub->buf[dvbsub->i] >> dvbsub->i_bits) & ((1 << width) - 1);

  if (!dvbsub->i_bits)
    ++dvbsub->i;

  return x;
}

static void decode_2bit_pixel_code_string (dvbsub_func_t *dvbsub, int r, int object_id, int ofs, int n)
{
  int j;
  const uint8_t *lut = lookup_lut (dvbsub, r);

  (void)object_id;
  (void)ofs;

  dvbsub->i_bits = 0;
  j = dvbsub->i + n;

  while (dvbsub->i < j)
  {
    int next_bits = next_datum (dvbsub, 2);
    int run_length;

    if (next_bits)
    {
      /* single pixel */
      plot (dvbsub, r, 1, lut[next_bits]);
      continue;
    }

    /* switch 1 */
    if (next_datum (dvbsub, 1) == 0)
    {
      /* run length, 3 to 10 pixels, colour given */
      run_length = next_datum (dvbsub, 3);
      plot (dvbsub, r, run_length + 3, lut[next_datum (dvbsub, 2)]);
      continue;
    }

    /* switch 2 */
    if (next_datum (dvbsub, 1) == 1)
    {
      /* single pixel, colour 0 */
      plot (dvbsub, r, 1, lut[0]);
      continue;
    }

    /* switch 3 */
    switch (next_datum (dvbsub, 2))
    {
    case 0: /* end-of-string */
      j = dvbsub->i; /* set the while cause FALSE */
      break;
    case 1: /* two pixels, colour 0 */
      plot (dvbsub, r, 2, lut[0]);
      break;
    case 2: /* run length, 12 to 27 pixels (4-bit), colour given */
      run_length = next_datum (dvbsub, 4);
      plot (dvbsub, r, run_length + 12, lut[next_datum (dvbsub, 2)]);
      break;
    case 3: /* run length, 29 to 284 pixels (8-bit), colour given */
      run_length = next_datum (dvbsub, 8);
      plot (dvbsub, r, run_length + 29, lut[next_datum (dvbsub, 2)]);
    }
  }

  if (dvbsub->i_bits) {
    dvbsub->i++;
    dvbsub->i_bits = 0;
  }
}

static void decode_4bit_pixel_code_string (dvbsub_func_t *dvbsub, int r, int object_id, int ofs, int n)
{
  int j;
  const uint8_t *lut = lookup_lut (dvbsub, r);

  (void)object_id;
  (void)ofs;

  dvbsub->i_bits = 0;
  j = dvbsub->i + n;

  while (dvbsub->i < j)
  {
    int next_bits = next_datum (dvbsub, 4);
    int run_length;

    if (next_bits)
    {
      /* single pixel */
      plot (dvbsub, r, 1, lut[next_bits]);
      continue;
    }

    /* switch 1 */
    if (next_datum (dvbsub, 1) == 0)
    {
      run_length = next_datum (dvbsub, 3);
      if (!run_length)
        /* end-of-string */
        break;

      /* run length, 3 to 9 pixels, colour 0 */
      plot (dvbsub, r, run_length + 2, lut[0]);
      continue;
    }

    /* switch 2 */
    if (next_datum (dvbsub, 1) == 0)
    {
      /* run length, 4 to 7 pixels, colour given */
      run_length = next_datum (dvbsub, 2);
      plot (dvbsub, r, run_length + 4, lut[next_datum (dvbsub, 4)]);
      continue;
    }

    /* switch 3 */
    switch (next_datum (dvbsub, 2))
    {
    case 0: /* single pixel, colour 0 */
      plot (dvbsub, r, 1, lut[0]);
      break;
    case 1: /* two pixels, colour 0 */
      plot (dvbsub, r, 2, lut[0]);
      break;
    case 2: /* run length, 9 to 24 pixels (4-bit), colour given */
      run_length = next_datum (dvbsub, 4);
      plot (dvbsub, r, run_length + 9, lut[next_datum (dvbsub, 4)]);
      break;
    case 3: /* run length, 25 to 280 pixels (8-bit), colour given */
      run_length = next_datum (dvbsub, 8);
      plot (dvbsub, r, run_length + 25, lut[next_datum (dvbsub, 4)]);
    }
  }

  if (dvbsub->i_bits) {
    dvbsub->i++;
    dvbsub->i_bits = 0;
  }
}

static void decode_8bit_pixel_code_string (dvbsub_func_t *dvbsub, int r, int object_id, int ofs, int n)
{
  int j;

  (void)object_id;
  (void)ofs;

  j = dvbsub->i + n;

  while (dvbsub->i < j)
  {
    int next_bits = dvbsub->buf[dvbsub->i++];
    int run_length;

    if (next_bits)
    {
      /* single pixel */
      plot (dvbsub, r, 1, next_bits);
      continue;
    }

    /* switch 1 */
    run_length = dvbsub->buf[dvbsub->i] & 127;

    if (dvbsub->buf[dvbsub->i++] & 128)
    {
      /* run length, 3 to 127 pixels, colour given */
      if (run_length > 2)
        plot (dvbsub, r, run_length + 4, dvbsub->buf[dvbsub->i++]);
      continue;
    }

    if (!run_length)
      /* end-of-string */
      break;

    /* run length, 1 to 127 pixels, colour 0 */
    plot (dvbsub, r, run_length + 2, 0);
  }
}

static void recalculate_trans (dvb_spu_decoder_t *this)
{
  dvbsub_func_t *const dvbsub = this->dvbsub;
  xine_spu_opacity_t opacity;
  int i;

  _x_spu_get_opacity (this->stream->xine, &opacity);
  for (i = 0; i < dvbsub->max_regions * 256; ++i) {
    /* ETSI-300-743 says "full transparency if Y == 0". */
    if (dvbsub->colours[i].c.y == 0)
      dvbsub->trans[i] = 0;
    else {
      int v = _x_spu_calculate_opacity (&dvbsub->colours[i].c, dvbsub->colours[i].c.foo, &opacity);
      dvbsub->trans[i] = v * 14 / 255 + 1;
    }
  }

}

static void set_clut(dvbsub_func_t *dvbsub, int CLUT_id,int CLUT_entry_id,int Y_value, int Cr_value, int Cb_value, int T_value) {

  if ((CLUT_id>=MAX_REGIONS) || (CLUT_entry_id>255)) {
    return;
  }

  dvbsub->colours[(CLUT_id*256)+CLUT_entry_id].c.y   = Y_value;
  dvbsub->colours[(CLUT_id*256)+CLUT_entry_id].c.cr  = Cr_value;
  dvbsub->colours[(CLUT_id*256)+CLUT_entry_id].c.cb  = Cb_value;
  dvbsub->colours[(CLUT_id*256)+CLUT_entry_id].c.foo = T_value;
}

static void process_CLUT_definition_segment(dvbsub_func_t *dvbsub) {
  int /*page_id,*/
      segment_length,
      CLUT_id/*,
      CLUT_version_number*/;

  int CLUT_entry_id,
      /*CLUT_flag_8_bit,*/
      /*CLUT_flag_4_bit,*/
      /*CLUT_flag_2_bit,*/
      full_range_flag,
      Y_value,
      Cr_value,
      Cb_value,
      T_value;

  int j;

  /*page_id=(dvbsub->buf[dvbsub->i]<<8)|dvbsub->buf[dvbsub->i+1];*/ dvbsub->i+=2;
  segment_length=(dvbsub->buf[dvbsub->i]<<8)|dvbsub->buf[dvbsub->i+1]; dvbsub->i+=2;
  j=dvbsub->i+segment_length;

  CLUT_id=dvbsub->buf[dvbsub->i++];
  lprintf ("process_CLUT_definition_segment: CLUT_id %d\n", CLUT_id);
  /*CLUT_version_number=(dvbsub->buf[dvbsub->i]&0xf0)>>4;*/
  dvbsub->i++;

  while (dvbsub->i < j) {
    CLUT_entry_id=dvbsub->buf[dvbsub->i++];

    /*CLUT_flag_2_bit=(dvbsub->buf[dvbsub->i]&0x80)>>7;*/
    /*CLUT_flag_4_bit=(dvbsub->buf[dvbsub->i]&0x40)>>6;*/
    /*CLUT_flag_8_bit=(dvbsub->buf[dvbsub->i]&0x20)>>5;*/
    full_range_flag=dvbsub->buf[dvbsub->i]&1;
    dvbsub->i++;

    if (full_range_flag==1) {
      Y_value=dvbsub->buf[dvbsub->i++];
      Cr_value=dvbsub->buf[dvbsub->i++];
      Cb_value=dvbsub->buf[dvbsub->i++];
      T_value=dvbsub->buf[dvbsub->i++];
    } else {
      Y_value = dvbsub->buf[dvbsub->i] & 0xfc;
      Cr_value = (dvbsub->buf[dvbsub->i] << 6 | dvbsub->buf[dvbsub->i + 1] >> 2) & 0xf0;
      Cb_value = (dvbsub->buf[dvbsub->i + 1] << 2) & 0xf0;
      T_value = (dvbsub->buf[dvbsub->i + 1] & 3) * 0x55; /* expand only this one to full range! */
      dvbsub->i+=2;
    }
    set_clut(dvbsub, CLUT_id,CLUT_entry_id,Y_value,Cr_value,Cb_value,T_value);
  }
}

static void process_pixel_data_sub_block (dvbsub_func_t *dvbsub, int r, int o, unsigned int pos, int ofs, int n)
{
  int data_type;
  int j;

  j = dvbsub->i + n;

  dvbsub->x = pos >> 16;
  dvbsub->y = (pos & 0xffff) + ofs;
  while (dvbsub->i < j) {
    data_type = dvbsub->buf[dvbsub->i++];

    switch (data_type) {
    case 0:
      dvbsub->i++;
    case 0x10:
      decode_2bit_pixel_code_string (dvbsub, r, o, ofs, n - 1);
      break;
    case 0x11:
      decode_4bit_pixel_code_string (dvbsub, r, o, ofs, n - 1);
      break;
    case 0x12:
      decode_8bit_pixel_code_string (dvbsub, r, o, ofs, n - 1);
      break;
    case 0x20: /* 2-to-4bit colour index map */
      /* should this be implemented since we have an 8-bit overlay? */
      dvbsub->lut[r].lut24[0] = dvbsub->buf[dvbsub->i    ] >> 4;
      dvbsub->lut[r].lut24[1] = dvbsub->buf[dvbsub->i    ] & 0x0f;
      dvbsub->lut[r].lut24[2] = dvbsub->buf[dvbsub->i + 1] >> 4;
      dvbsub->lut[r].lut24[3] = dvbsub->buf[dvbsub->i + 1] & 0x0f;
      dvbsub->i += 2;
      break;
    case 0x21: /* 2-to-8bit colour index map */
      memcpy (dvbsub->lut[r].lut28, dvbsub->buf + dvbsub->i, 4);
      dvbsub->i += 4;
      break;
    case 0x22:
      memcpy (dvbsub->lut[r].lut48, dvbsub->buf + dvbsub->i, 16);
      dvbsub->i += 16;
      break;
    case 0xf0:
      dvbsub->x = pos >> 16;
      dvbsub->y += 2;
      break;
    default:
      lprintf ("unimplemented data_type %02x in pixel_data_sub_block\n", data_type);
    }
  }

  dvbsub->i = j;
}

static void process_page_composition_segment (dvbsub_func_t *dvbsub)
{
  int segment_length;
  int region_id, region_x, region_y;

  dvbsub->page.page_id = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
  dvbsub->i += 2;
  segment_length = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
  dvbsub->i += 2;

  int j = dvbsub->i + segment_length;

  dvbsub->page.page_time_out = dvbsub->buf[dvbsub->i++];
  if ( dvbsub->page.page_time_out>6 ) /* some timeout are insane, e.g. 65s ! */
    dvbsub->page.page_time_out = 6;

  int version = (dvbsub->buf[dvbsub->i] & 0xf0) >> 4;
  if ( version == dvbsub->page.page_version_number )
    return;
  dvbsub->page.page_version_number = version;
  dvbsub->page.page_state = (dvbsub->buf[dvbsub->i] & 0x0c) >> 2;
  dvbsub->i++;

  int r;
  for (r=0; r<dvbsub->max_regions; r++) { /* reset */
    dvbsub->page.regions[r].is_visible = 0;
  }

  while (dvbsub->i < j) {
    region_id = dvbsub->buf[dvbsub->i++];
    dvbsub->i++;                /* reserved */
    region_x = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
    dvbsub->i += 2;
    region_y = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
    dvbsub->i += 2;
    lprintf ("process_page_composition_segment: page_id %d, region_id %d, x %d, y %d\n",
      dvbsub->page.page_id, region_id, region_x, region_y);
    if (region_id >= MAX_REGIONS)
      continue;
    if (region_id >= dvbsub->max_regions)
      dvbsub->max_regions = region_id + 1;

    dvbsub->page.regions[region_id].x = region_x;
    dvbsub->page.regions[region_id].y = region_y;
    dvbsub->page.regions[region_id].is_visible = 1;
  }
}


static void process_region_composition_segment (dvbsub_func_t *dvbsub)
{
  int segment_length,
    region_id,
    region_version_number,
    region_fill_flag, region_width, region_height, region_level_of_compatibility, region_depth, CLUT_id,
    /*region_8_bit_pixel_code,*/ region_4_bit_pixel_code /*, region_2_bit_pixel_code*/;
  int object_id, object_type, /*object_provider_flag,*/ object_x, object_y /*, foreground_pixel_code, background_pixel_code*/;
  int j;

  dvbsub->page.page_id = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
  dvbsub->i += 2;
  segment_length = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
  dvbsub->i += 2;
  j = dvbsub->i + segment_length;

  region_id = dvbsub->buf[dvbsub->i++];
  region_version_number = (dvbsub->buf[dvbsub->i] & 0xf0) >> 4;
  region_fill_flag = (dvbsub->buf[dvbsub->i] & 0x08) >> 3;
  dvbsub->i++;
  region_width = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
  dvbsub->i += 2;
  region_height = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
  dvbsub->i += 2;
  region_level_of_compatibility = (dvbsub->buf[dvbsub->i] & 0xe0) >> 5;
  region_depth = (dvbsub->buf[dvbsub->i] & 0x1c) >> 2;
  dvbsub->compat_depth = region_level_of_compatibility << 3 | region_depth;
  dvbsub->i++;
  CLUT_id = dvbsub->buf[dvbsub->i++];
  /*region_8_bit_pixel_code = dvbsub->buf[*/dvbsub->i++/*]*/;
  region_4_bit_pixel_code = (dvbsub->buf[dvbsub->i] & 0xf0) >> 4;
  /*region_2_bit_pixel_code = (dvbsub->buf[dvbsub->i] & 0x0c) >> 2;*/
  dvbsub->i++;
  lprintf ("process_region_composition_segment: region_id %d (%d x %d)\n", region_id, region_width, region_height);

  if(region_id>=MAX_REGIONS)
    return;

  if (region_id >= dvbsub->max_regions)
    dvbsub->max_regions = region_id + 1;

  if ( dvbsub->regions[region_id].version_number == region_version_number )
    return;

  dvbsub->regions[region_id].version_number = region_version_number;

  /* Check if region size has changed and fill background. */
  update_region (&dvbsub->regions[region_id], region_id, region_width, region_height, region_fill_flag, region_4_bit_pixel_code);
  if ( CLUT_id<MAX_REGIONS )
    dvbsub->regions[region_id].CLUT_id = CLUT_id;

  dvbsub->regions[region_id].objects_start = dvbsub->i;
  dvbsub->regions[region_id].objects_end = j;

  sparse_array_unset (&dvbsub->object_pos, region_id << 16, 0xff0000);

  while (dvbsub->i < j) {
    object_id = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
    dvbsub->i += 2;
    object_type = (dvbsub->buf[dvbsub->i] & 0xc0) >> 6;
    /*object_provider_flag = (dvbsub->buf[dvbsub->i] & 0x30) >> 4;*/
    object_x = ((dvbsub->buf[dvbsub->i] & 0x0f) << 8) | dvbsub->buf[dvbsub->i + 1];
    dvbsub->i += 2;
    object_y = ((dvbsub->buf[dvbsub->i] & 0x0f) << 8) | dvbsub->buf[dvbsub->i + 1];
    dvbsub->i += 2;

    sparse_array_set (&dvbsub->object_pos, (region_id << 16) | object_id, (object_x << 16) | object_y);

    if ((object_type == 0x01) || (object_type == 0x02)) {
      /*foreground_pixel_code = dvbsub->buf[*/dvbsub->i++/*]*/;
      /*background_pixel_code = dvbsub->buf[*/dvbsub->i++/*]*/;
    }
  }

}

static void process_object_data_segment (dvbsub_func_t *dvbsub)
{
  int /*segment_length,*/ object_id/*, object_version_number*/, object_coding_method/*, non_modifying_colour_flag*/;

  int top_field_data_block_length, bottom_field_data_block_length;

  int old_i;
  int r;

  dvbsub->page.page_id = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
  dvbsub->i += 2;
  /*segment_length = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];*/
  dvbsub->i += 2;

  object_id = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
  dvbsub->i += 2;
  dvbsub->curr_obj = object_id;
  /*object_version_number = (dvbsub->buf[dvbsub->i] & 0xf0) >> 4;*/
  object_coding_method = (dvbsub->buf[dvbsub->i] & 0x0c) >> 2;
  /*non_modifying_colour_flag = (dvbsub->buf[dvbsub->i] & 0x02) >> 1;*/
  dvbsub->i++;

  old_i = dvbsub->i;
  for (r = 0; r < dvbsub->max_regions; r++) {

    /* If this object is in this region... */
    if (dvbsub->regions[r].img) {
      uint32_t pos;
      if (sparse_array_get (&dvbsub->object_pos, (r << 16) | object_id, &pos)) {
        dvbsub->i = old_i;
        if (object_coding_method == 0) {
          top_field_data_block_length = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
          dvbsub->i += 2;
          bottom_field_data_block_length = (dvbsub->buf[dvbsub->i] << 8) | dvbsub->buf[dvbsub->i + 1];
          dvbsub->i += 2;

          process_pixel_data_sub_block (dvbsub, r, object_id, pos, 0, top_field_data_block_length);

          if (bottom_field_data_block_length == 0)
          {
            /* handle bottom field == top field */
            bottom_field_data_block_length = top_field_data_block_length;
            dvbsub->i =  old_i + 4;
          }

          process_pixel_data_sub_block (dvbsub, r, object_id, pos, 1, bottom_field_data_block_length);
        }
      }
    }
  }
}

static void process_display_definition_segment(dvb_spu_decoder_t *this)
{
  /* FIXME: not implemented. */
  (void)this;
}

static void unlock_mutex_cancellation_func(void *mutex_gen)
{
  pthread_mutex_t *mutex = (pthread_mutex_t*) mutex_gen;
  pthread_mutex_unlock(mutex);
}

/* Thread routine that checks for subtitle timeout periodically.
   To avoid unexpected subtitle hiding, calls to this->stream->osd_renderer->show()
   should be in blocks like:

   pthread_mutex_lock(&this->dvbsub_osd_mutex);
   this->stream->osd_renderer->show(...);
   this->dvbsub_hide_timeout.tv_sec = time(NULL) + timeout value;
   pthread_cond_signal(&this->dvbsub_restart_timeout);
   pthread_mutex_unlock(&this->dvbsub_osd_mutex);

   This ensures that the timeout is changed with the lock held, and
   that the thread is signalled to pick up the new timeout.
*/
static void* dvbsub_timer_func(void *this_gen)
{
  dvb_spu_decoder_t *this = (dvb_spu_decoder_t *) this_gen;
  pthread_mutex_lock(&this->dvbsub_osd_mutex);
  int i;

 /* If we're cancelled via pthread_cancel, unlock the mutex */
  pthread_cleanup_push(unlock_mutex_cancellation_func, &this->dvbsub_osd_mutex);

  while(1)
  {
    /* Record the current timeout, and wait - note that pthread_cond_timedwait
       will unlock the mutex on entry, and lock it on exit */
    struct timespec timeout = this->dvbsub_hide_timeout;
    int result = pthread_cond_timedwait(&this->dvbsub_restart_timeout,
                                        &this->dvbsub_osd_mutex,
                                        &this->dvbsub_hide_timeout);
    if(result == ETIMEDOUT &&
       timeout.tv_sec == this->dvbsub_hide_timeout.tv_sec &&
       timeout.tv_nsec == this->dvbsub_hide_timeout.tv_nsec)
    {
      /* We timed out, and no-one changed the timeout underneath us.
         Hide the OSD, then wait until we're signalled. */
      if(this->stream && this->stream->osd_renderer)
      {
        for ( i=0; i<this->dvbsub->max_regions; i++ ) {
          if ( this->dvbsub->regions[i].osd ) {
            this->stream->osd_renderer->hide( this->dvbsub->regions[i].osd, 0 );
            lprintf("thread hiding = %d\n",i);
          }
        }
      }
      pthread_cond_wait(&this->dvbsub_restart_timeout, &this->dvbsub_osd_mutex);
    }
  }

  pthread_cleanup_pop(1);
  return NULL;
}

static void downscale_region_image( region_t *reg, unsigned char *dest, int dest_width )
{
  float i, k, inc=reg->width/(float)dest_width;
  int j;
  for ( j=0; j<reg->height; j++ ) {
    for ( i=0,k=0; i<reg->width && k<dest_width; i+=inc,k++ ) {
      dest[(j*dest_width)+(int)k] = reg->img[(j*reg->width)+(int)i];
    }
  }
}

static void draw_subtitles (dvb_spu_decoder_t * this)
{
  int r;
  int display=0;
  int64_t dum;
  int dest_width=0, dest_height;

  this->stream->video_out->status(this->stream->video_out, NULL, &dest_width, &dest_height, &dum);
  if ( !dest_width || !dest_height )
    return;

  /* render all regions onto the page */

  for ( r=0; r<this->dvbsub->max_regions; r++ ) {
    if ( this->dvbsub->page.regions[r].is_visible )
      display++;
  }
  if ( !display )
    return;

  for (r = 0; r < this->dvbsub->max_regions; r++) {
    region_t *reg = &this->dvbsub->regions[r];
    if (reg->img) {
      if (this->dvbsub->page.regions[r].is_visible && !reg->empty) {
        unsigned char tmp[dest_width*576];
        unsigned char *img;
        int img_width;

        update_osd( this, reg );
        if (!reg->osd)
          continue;
        /* clear osd */
        this->stream->osd_renderer->clear( reg->osd );
        if (reg->width > dest_width && !(this->stream->video_out->get_capabilities(this->stream->video_out) & VO_CAP_CUSTOM_EXTENT_OVERLAY)) {
          downscale_region_image(reg, tmp, dest_width);
          img = tmp;
          img_width = dest_width;
        }
        else {
          img = reg->img;
          img_width = reg->width;
        }
        /* All DVB subs I have seen so far use same color matrix as main video. */
        _X_SET_CLUT_CM (&this->dvbsub->colours[reg->CLUT_id * 256].u32, 4);
        this->stream->osd_renderer->set_palette( reg->osd,
                                                 &this->dvbsub->colours[reg->CLUT_id * 256].u32,
                                                 &this->dvbsub->trans[reg->CLUT_id * 256]);
        this->stream->osd_renderer->draw_bitmap( reg->osd, img, 0, 0, img_width, reg->height, NULL );
      }
    }
  }

  pthread_mutex_lock(&this->dvbsub_osd_mutex);
  lprintf("this->vpts=%"PRId64"\n",this->vpts);
  for ( r=0; r<this->dvbsub->max_regions; r++ ) {
    region_t *reg = &this->dvbsub->regions[r];
    lprintf("region=%d, visible=%d, osd=%d, empty=%d\n",
            r, this->dvbsub->page.regions[r].is_visible, reg->osd ? 1 : 0, reg->empty );
    if (this->dvbsub->page.regions[r].is_visible && reg->osd && !reg->empty ) {
      this->stream->osd_renderer->set_position( reg->osd, this->dvbsub->page.regions[r].x, this->dvbsub->page.regions[r].y );
      this->stream->osd_renderer->show( reg->osd, this->vpts );
      lprintf("show region = %d\n",r);
    }
    else {
      if (reg->osd) {
        this->stream->osd_renderer->hide( reg->osd, this->vpts );
        lprintf("hide region = %d\n",r);
      }
    }
  }
  this->dvbsub_hide_timeout.tv_nsec = 0;
  this->dvbsub_hide_timeout.tv_sec = time(NULL) + this->dvbsub->page.page_time_out;
  lprintf("page_time_out %d\n",this->dvbsub->page.page_time_out);
  pthread_cond_signal(&this->dvbsub_restart_timeout);
  pthread_mutex_unlock(&this->dvbsub_osd_mutex);
}

static void spudec_decode_data (spu_decoder_t * this_gen, buf_element_t * buf)
{
  dvb_spu_decoder_t *this = (dvb_spu_decoder_t *) this_gen;
  int new_i;
  /*int data_identifier, subtitle_stream_id;*/
  int segment_length, segment_type;
  int i;

  if((buf->type & 0xffff0000)!=BUF_SPU_DVB)
    return;

  if (buf->decoder_flags & BUF_FLAG_SPECIAL) {
    if (buf->decoder_info[1] == BUF_SPECIAL_SPU_DVB_DESCRIPTOR) {
      if (buf->decoder_info[2] == 0) {
        /* Hide the osd - note that if the timeout thread times out, it'll rehide, which is harmless */
        pthread_mutex_lock(&this->dvbsub_osd_mutex);
        for ( i=0; i<this->dvbsub->max_regions; i++ ) {
          if ( this->dvbsub->regions[i].osd )
            this->stream->osd_renderer->hide( this->dvbsub->regions[i].osd, 0 );
        }
        pthread_mutex_unlock(&this->dvbsub_osd_mutex);
      }
      else {
        if (buf->decoder_info[2] < sizeof(this->spu_descriptor)) {
          xprintf (this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": too small spu_descriptor, ignoring\n");
        } else {
          if (buf->decoder_info[2] > sizeof(this->spu_descriptor))
            xprintf (this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": too large spu_descriptor\n");
          memcpy (&this->spu_descriptor, buf->decoder_info_ptr[2], sizeof(this->spu_descriptor));
        }
      }
    }
    return;
  }

  /* accumulate data */
  if (buf->decoder_info[2]) {
    memset (this->pes_pkt, 0xff, 64*1024);
    this->pes_pkt_wrptr = this->pes_pkt;
    this->pes_pkt_size = buf->decoder_info[2];

    xine_fast_memcpy (this->pes_pkt, buf->content, buf->size);
    this->pes_pkt_wrptr += buf->size;

    this->vpts = 0;
  }
  else {
    if (this->pes_pkt && (this->pes_pkt_wrptr != this->pes_pkt)) {
      xine_fast_memcpy (this->pes_pkt_wrptr, buf->content, buf->size);
      this->pes_pkt_wrptr += buf->size;
    }
  }

  /* don't ask metronom for a vpts but rather do the calculation
   * because buf->pts could be too far in future and metronom won't accept
   * further backwards pts (see metronom_got_spu_packet) */
  if (buf->pts > 0) {
    metronom_t *metronom = this->stream->metronom;
    int64_t vpts_offset = metronom->get_option( metronom, METRONOM_VPTS_OFFSET );
    int64_t spu_offset = metronom->get_option( metronom, METRONOM_SPU_OFFSET );
    int64_t vpts = (int64_t)(buf->pts)+vpts_offset+spu_offset;
    metronom_clock_t *clock = this->stream->xine->clock;
    int64_t curvpts = clock->get_current_time( clock );
    /* if buf->pts is unreliable, show page asap (better than nothing) */
    lprintf("spu_vpts=%"PRId64" - current_vpts=%"PRId64"\n", vpts, curvpts);
    if ( vpts<=curvpts || (vpts-curvpts)>(5*90000) )
      this->vpts = 0;
    else
      this->vpts = vpts;
  }

  /* completely ignore pts since it makes a lot of problems with various providers */
  /* this->vpts = 0; */

  /* process the pes section */

      this->dvbsub->buf = this->pes_pkt;

      this->dvbsub->i = 0;

      /*data_identifier = this->dvbsub->buf[*/this->dvbsub->i++/*]*/;
      /*subtitle_stream_id = this->dvbsub->buf[*/this->dvbsub->i++/*]*/;

      while (this->dvbsub->i <= (int)this->pes_pkt_size) {
        /* SUBTITLING SEGMENT */
        this->dvbsub->i++;
        segment_type = this->dvbsub->buf[this->dvbsub->i++];

        this->dvbsub->page.page_id = (this->dvbsub->buf[this->dvbsub->i] << 8) | this->dvbsub->buf[this->dvbsub->i + 1];
        segment_length = (this->dvbsub->buf[this->dvbsub->i + 2] << 8) | this->dvbsub->buf[this->dvbsub->i + 3];
        new_i = this->dvbsub->i + segment_length + 4;

        /* only process complete segments */
        if(new_i > (this->pes_pkt_wrptr - this->pes_pkt))
          break;
        /* verify we've the right segment */
        if (this->dvbsub->page.page_id == this->spu_descriptor.comp_page_id) {
          /* SEGMENT_DATA_FIELD */
          switch (segment_type & 0xff) {
            case 0x10:
              process_page_composition_segment (this->dvbsub);
              break;
            case 0x11:
              process_region_composition_segment (this->dvbsub);
              break;
            case 0x12:
              process_CLUT_definition_segment(this->dvbsub);
              break;
            case 0x13:
              process_object_data_segment (this->dvbsub);
              break;
            case 0x14:
              process_display_definition_segment(this);
              break;
            case 0x80:          /* Page is now completely rendered */
              recalculate_trans(this);
              draw_subtitles( this );
              break;
            case 0xFF:          /* stuffing */
              break;
            default:
              return;
              break;
          }
        }
        this->dvbsub->i = new_i;
      }
}

static void spudec_reset (spu_decoder_t * this_gen)
{
  dvb_spu_decoder_t *this = (dvb_spu_decoder_t *) this_gen;
  int i;

  /* Hide the osd - if the timeout thread times out, it'll rehide harmlessly */
  pthread_mutex_lock(&this->dvbsub_osd_mutex);
  for ( i=0; i<MAX_REGIONS; i++ ) {
    if ( this->dvbsub->regions[i].osd )
      this->stream->osd_renderer->hide(this->dvbsub->regions[i].osd, 0);
    this->dvbsub->regions[i].version_number = -1;
  }
  this->dvbsub->page.page_version_number = -1;
  reset_clut (this->dvbsub);

  pthread_mutex_unlock(&this->dvbsub_osd_mutex);
}

static void spudec_discontinuity (spu_decoder_t * this_gen)
{
  /* do nothing */
  (void)this_gen;
}

static void spudec_dispose_stopped (dvb_spu_decoder_t * this)
{
  int i;

  pthread_mutex_destroy(&this->dvbsub_osd_mutex);
  pthread_cond_destroy(&this->dvbsub_restart_timeout);

  for ( i=0; i<MAX_REGIONS; i++ ) {
    _x_freep( &this->dvbsub->regions[i].img );
    if ( this->dvbsub->regions[i].osd )
      this->stream->osd_renderer->free_object( this->dvbsub->regions[i].osd );
  }

  _x_freep (&this->pes_pkt);

  if (this->dvbsub) {
    sparse_array_delete (&this->dvbsub->object_pos);
    _x_freep (&this->dvbsub);
  }

  free (this);
}

static void spudec_dispose (spu_decoder_t * this_gen)
{
  dvb_spu_decoder_t *this = (dvb_spu_decoder_t *) this_gen;

  pthread_cancel(this->dvbsub_timer_thread);
  pthread_join(this->dvbsub_timer_thread, NULL);

  spudec_dispose_stopped(this);
}

static spu_decoder_t *dvb_spu_class_open_plugin (spu_decoder_class_t * class_gen, xine_stream_t * stream)
{
  dvb_spu_decoder_t *this;

  (void)class_gen;
  this = calloc(1, sizeof (dvb_spu_decoder_t));
  if (!this)
    return NULL;

#define YUVA(r, g, b, a) (clut_t) { COMPUTE_V(r, g, b), COMPUTE_U(r, g, b), COMPUTE_V(r, g, b), a }
#define GETBIT(s, v1, v2, tr) \
    r = s + ((i & 1) ? v1 : 0) + ((i & 0x10) ? v2 : 0); \
    g = s + ((i & 2) ? v1 : 0) + ((i & 0x20) ? v2 : 0); \
    b = s + ((i & 4) ? v1 : 0) + ((i & 0x40) ? v2 : 0); \
    a = tr

  if (!default_colours_init)
  {
    int i;
    default_clut[0] = YUVA(0, 0, 0, 0);
    for (i = 1; i < 256; i++) {
      uint8_t r, g, b, a;
      if (i < 8) {
        GETBIT(0, 255, 0, 63);
      } else switch (i & 0x88) {
      case 0x00: GETBIT(  0, 85, 170, 255); break;
      case 0x08: GETBIT(  0, 85, 170, 127); break;
      case 0x80: GETBIT(127, 43,  85, 255); break;
      default  : GETBIT(  0, 43,  85, 255); break;
      }
      default_trans[i] = a;
      default_clut[i] = YUVA(r, g, b, a);
    }
    default_colours_init = 1;
  }


  this->spu_decoder.decode_data = spudec_decode_data;
  this->spu_decoder.reset = spudec_reset;
  this->spu_decoder.discontinuity = spudec_discontinuity;
  this->spu_decoder.dispose = spudec_dispose;
  this->spu_decoder.get_interact_info = NULL;
  this->spu_decoder.set_button = NULL;

  this->stream = stream;

  pthread_mutex_init(&this->dvbsub_osd_mutex, NULL);
  pthread_cond_init(&this->dvbsub_restart_timeout, NULL);

  this->pes_pkt        = calloc(65, 1024);
  this->dvbsub         = calloc(1, sizeof (dvbsub_func_t));
  if (!this->pes_pkt || !this->dvbsub) {
    spudec_dispose_stopped(this);
    return NULL;
  }

  int i;
  for (i = 0; i < MAX_REGIONS; i++) {
    this->dvbsub->page.regions[i].is_visible = 0;
    this->dvbsub->regions[i].img = NULL;
    this->dvbsub->regions[i].osd = NULL;
    this->dvbsub->regions[i].CLUT_id = 0;
  }

  {
    xine_spu_opacity_t opacity;
    static const clut_t black = { 0, 0, 0, 0 };
    int t;

    _x_spu_get_opacity (this->stream->xine, &opacity);
    t = _x_spu_calculate_opacity (&black, 0, &opacity);

    for (i = 0; i < MAX_REGIONS * 256; i++)
      this->dvbsub->colours[i].c.foo = t;
  }

  sparse_array_new (&this->dvbsub->object_pos);

  this->dvbsub_hide_timeout.tv_nsec = 0;
  this->dvbsub_hide_timeout.tv_sec = time(NULL);

  if (pthread_create(&this->dvbsub_timer_thread, NULL, dvbsub_timer_func, this)) {
    xprintf (stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": pthread_create() failed\n");
    spudec_dispose_stopped(this);
    return NULL;
  }

  return (spu_decoder_t *) this;
}

static void *init_spu_decoder_plugin (xine_t * xine, const void *data)
{
  (void)xine;
  (void)data;
  static const spu_decoder_class_t decode_dvb_spu_class = {
    .open_plugin = dvb_spu_class_open_plugin,
    .identifier  = "spudvb",
    .description = N_("DVB subtitle decoder plugin"),
    .dispose = NULL,
  };
  return (void *)&decode_dvb_spu_class;
}


/* plugin catalog information */
static const uint32_t supported_types[] = { BUF_SPU_DVB, 0 };

static const decoder_info_t spudec_info = {
  .supported_types = supported_types,
  .priority        = 1,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
/* type, API, "name", version, special_info, init_function */
  {PLUGIN_SPU_DECODER, 17, "spudvb", XINE_VERSION_CODE, &spudec_info,
   &init_spu_decoder_plugin},
  {PLUGIN_NONE, 0, NULL, 0, NULL, NULL}
};
