/*
 * Copyright (C) 2000-2002 the xine project
 * 
 * Copyright (C) Christian Vogler 
 *               cvogler@gradient.cis.upenn.edu - December 2001
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
 * $Id: cc_decoder.c,v 1.1 2002/01/05 21:41:18 miguelfreitas Exp $
 *
 * stuff needed to provide closed captioning decoding and display
 *
 * Some small bits and pieces of the EIA-608 captioning decoder were
 * adapted from CCDecoder 0.9.1 by Mike Baker. The latest version is
 * available at http://sourceforge.net/projects/ccdecoder/.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>

#include <inttypes.h>

#include "video_out.h"
#include "xine_internal.h"
#include "osd.h"
#include "cc_decoder.h"

#ifdef XINE_COMPILE
#include "libspudec/spu.h"
#else
#include "spu.h"
#endif
#include "osd.h"

/*
#define LOG_DEBUG 3
*/

/* at 29.97 fps, each NTSC frame takes 3003 metronom ticks on the average. */
#define NTSC_FRAME_DURATION 3003

#define CC_ROWS 15
#define CC_COLUMNS 32
#define CC_CHANNELS 2

/* 1 is the caption background color index in the OSD palettes. */
#define CAP_BG_COL 1

/* colors specified by the EIA 608 standard */
enum { WHITE, GREEN, BLUE, CYAN, RED, YELLOW, MAGENTA, BLACK, TRANSPARENT };

#define TRANSP_SPACE 0x19   /* code for transparent space, essentially 
			       arbitrary */

#define MAX(a, b) ((a) > (b)? (a) : (b))

/* mapping from PAC row code to actual CC row */
static int  rowdata[] = {10, -1, 0, 1, 2, 3, 11, 12, 13, 14, 4, 5, 6,
			 7, 8, 9};
#warning "FIXME: do real TM" 
/* ¶ must be mapped as a music note in the captioning font */ 
static char specialchar[] = {'®','°','½','¿','T','¢','£','¶','à',
			     TRANSP_SPACE,'è','â','ê','î','ô','û'};

/* character translation table - EIA 608 codes are not all the same as ASCII */
static char chartbl[128];

/* CC codes use odd parity for error detection, since they originally were */
/* transmitted via noisy video signals */
static int parity_table[256];


/*---------------- decoder data structures -----------------------*/

/* CC attribute */
typedef struct cc_attribute_s {
  uint8_t italic;
  uint8_t underline;
  uint8_t foreground;
  uint8_t background;
} cc_attribute_t;

/* CC character cell */
typedef struct cc_char_cell_s {
  uint8_t c;                   /* character code, not the same as ASCII */
  cc_attribute_t attributes;   /* attributes of this character, if changed */
			       /* here */
  int midrow_attr;             /* true if this cell changes an attribute */
} cc_char_cell_t;

/* a single row in the closed captioning memory */
typedef struct cc_row_s {
  cc_char_cell_t cells[CC_COLUMNS];
  int pos;                   /* position of the cursor */
  int num_chars;             /* how many characters in the row are data */
  int attr_chg;              /* true if midrow attr. change at cursor pos */
  int pac_attr_chg;          /* true if attribute has changed via PAC */
  cc_attribute_t pac_attr;   /* PAC attr. that hasn't been applied yet */
} cc_row_t;

/* closed captioning memory for a single channel */
typedef struct cc_buffer_s {
  cc_row_t rows[CC_ROWS];
  int rowpos;              /* row cursor position */
} cc_buffer_t;

/* captioning memory for all channels */
typedef struct cc_memory_s {
  cc_buffer_t channel[CC_CHANNELS];
  int channel_no;          /* currently active channel */
} cc_memory_t;

/* The closed captioning decoder data structure */
struct cc_decoder_s {
  /* CC decoder buffer  - one onscreen, one offscreen */
  cc_memory_t buffer[2];
  /* onscreen, offscreen buffer ptrs */
  cc_memory_t *on_buf;
  cc_memory_t *off_buf;
  /* which buffer is active for receiving data */
  cc_memory_t **active;

  /* for logging and debugging purposes, captions are assigned increasing */
  /*   unique ids. */
  uint32_t capid;

  /* the last captioning code seen (control codes are often sent twice
     in a row, but should be processed only once) */
  uint32_t lastcode;

  /* The PTS and SCR at which the captioning chunk started */
  uint32_t pts;
  uint32_t scr;
  /* holds the NTSC frame offset to last known pts/scr */
  uint32_t f_offset;

  /* active OSD renderer */
  osd_renderer_t     *renderer;
  /* caption display object */
  osd_object_t       *cap_display;
  /* true when caption currently is displayed */
  int displayed;

  /* configuration and intrinsics of CC decoder */
  cc_config_t *cc_cfg;

  metronom_t *metronom;
};


/*---------------- general utility functions ---------------------*/

static void get_font_metrics(osd_renderer_t *renderer, 
			     const char *fontname, int font_size,
			     int *maxw, int *maxh)
{
  int c;
  osd_object_t *testc = renderer->new_object(renderer, 640, 480);

  *maxw = 0;
  *maxh = 0;

  renderer->set_font(testc, (char *) fontname, font_size);
  for (c = 32; c < 256; c++) {
    int tw, th;
    char buf[2] = { (char) c, '\0' };
    renderer->get_text_size(testc, buf, &tw, &th);
    *maxw = MAX(*maxw, tw);
    *maxh = MAX(*maxh, th);
  }
  renderer->free_object(testc);
}


static void copy_str(char *d, const char *s, size_t maxbytes)
{
  strncpy(d, s, maxbytes);
  d[maxbytes] = '\0';
}


static int parity(uint8_t byte)
{
  int i;
  int ones = 0;

  for (i = 0; i < 7; i++) {
    if (byte & (1 << i))
      ones++;
  }

  return ones & 1;
}


static void build_parity_table(void)
{
  uint8_t byte;
  int parity_v;
  for (byte = 0; byte <= 127; byte++) {
    parity_v = parity(byte);
    /* CC uses odd parity (i.e., # of 1's in byte is odd.) */
    parity_table[byte] = parity_v;
    parity_table[byte | 0x80] = !parity_v;
  }
}


static int good_parity(uint16_t data)
{
  int ret = parity_table[data & 0xff] && parity_table[(data & 0xff00) >> 8];
  if (! ret)
    printf("Bad parity in EIA-608 data (%x)\n", data);
  return ret;
}


static void build_char_table(void)
{
  int i;
  /* first the normal ASCII codes */
  for (i = 0; i < 128; i++)
    chartbl[i] = (char) i;
  /* now the special codes */
  chartbl[0x2a] = 'á';
  chartbl[0x5c] = 'é';
  chartbl[0x5e] = 'í';
  chartbl[0x5f] = 'ó';
  chartbl[0x60] = 'ú';
  chartbl[0x7b] = 'ç';
  chartbl[0x7c] = '÷';
  chartbl[0x7d] = 'Ñ';
  chartbl[0x7e] = 'ñ';
  chartbl[0x7f] = '¤';    /* FIXME: this should be a solid block */
}


/*----------------- cc_row_t methods --------------------------------*/

static void ccrow_fill_transp(cc_row_t *rowbuf)
{
  int i;

#ifdef LOG_DEBUG
  printf("cc_decoder: ccrow_fill_transp: Filling in %d transparent spaces.\n",
	 rowbuf->pos - rowbuf->num_chars);
#endif
  for (i = rowbuf->num_chars; i < rowbuf->pos; i++) {
    rowbuf->cells[i].c = TRANSP_SPACE;
    rowbuf->cells[i].midrow_attr = 0;
  }
}


static int ccrow_find_next_text_part(cc_row_t *this, int pos)
{
  while (pos < this->num_chars && this->cells[pos].c == TRANSP_SPACE)
    pos++;
  return pos;
}


static int ccrow_find_end_of_text_part(cc_row_t *this, int pos)
{
  while (pos < this->num_chars && this->cells[pos].c != TRANSP_SPACE)
    pos++;
  return pos;
}


static int ccrow_find_current_attr(cc_row_t *this, int pos)
{
  while (pos > 0 && !this->cells[pos].midrow_attr)
    pos--;
  return pos;
}


static int ccrow_find_next_attr_change(cc_row_t *this, int pos, int lastpos)
{
  pos++;
  while (pos < lastpos && !this->cells[pos].midrow_attr)
    pos++;
  return pos;
}


/* CAUTION: THIS FUNCTION ASSUMES THAT THE MUTEX IS ALREADY LOCKED! */
static void ccrow_set_attributes(cc_row_t *this, osd_renderer_t *renderer,
			  osd_object_t *display, int pos,
			  cc_config_t *cap_cfg)
{
  const cc_attribute_t *attr = &this->cells[pos].attributes;
  const char *fontname;
  cc_confvar_t *cap_info = &cap_cfg->vars;

  if (attr->italic)
    fontname = cap_info->italic_font;
  else
    fontname = cap_info->font;
  renderer->set_font(display, (char *) fontname, cap_info->font_size); 
}


/* CAUTION: THIS FUNCTION ASSUMES THAT THE MUTEX IS ALREADY LOCKED! */
static void ccrow_render(cc_row_t *this, int rownum,
			 cc_config_t *cap_cfg, osd_renderer_t *renderer,
			 osd_object_t *display)
{
  char buf[CC_COLUMNS + 1];
  int base_y;
  int pos = ccrow_find_next_text_part(this, 0);
  cc_confvar_t *cap_info = &cap_cfg->vars;

  /* find y coordinate of caption */
  if (cap_info->center) {
    /* find y-center of the desired row; the next line computes */
    /* cap_info->height * (rownum + 0.5) / CC_ROWS */
    /* in integer arithmetic for this purpose. */
    base_y = (cap_info->height * rownum * 100 + cap_info->height * 50) /
      (CC_ROWS * 100);
  }
  else
    base_y = cap_info->height * rownum / CC_ROWS;

  /* break down captions into parts separated by transparent space, and */
  /* center each part individually along the x axis */
  while (pos < this->num_chars) {
    int endpos = ccrow_find_end_of_text_part(this, pos);
    int seg_begin = pos;
    int seg_end;
    int i;
    int text_w = 0, text_h = 0;
    int x, y;
    int seg_w, seg_h;
    int seg_pos[CC_COLUMNS + 1];
    int cumulative_seg_width[CC_COLUMNS + 1];
    int num_seg = 0;
    int seg;

    /* break down each part into segments bounded by attribute changes and */
    /* find text metrics of the parts */
    seg_pos[0] = seg_begin;
    cumulative_seg_width[0] = 0;
    while (seg_begin < endpos) {
      int attr_pos = ccrow_find_current_attr(this, seg_begin);
      seg_end = ccrow_find_next_attr_change(this, seg_begin, endpos);

      /* compute text size of segment */
      for (i = seg_begin; i < seg_end; i++)
	buf[i - seg_begin] = this->cells[i].c;
      buf[seg_end - seg_begin] = '\0';
      ccrow_set_attributes(this, renderer, display, attr_pos, cap_cfg);
      renderer->get_text_size(display, buf, &seg_w, &seg_h);

      /* update cumulative segment statistics */
      text_w += seg_w;
      text_h += seg_h;
      seg_pos[num_seg + 1] = seg_end;
      cumulative_seg_width[num_seg + 1] = text_w;
      num_seg++;

      seg_begin = seg_end;
    }

    /* compute x coordinate of part */
    if (cap_info->center) {
      int cell_width = cap_info->width / CC_COLUMNS;
      x = (cap_info->width * (pos + endpos) / 2) / CC_COLUMNS;
      x -= text_w / 2;
      /* clamp x coordinate to nearest character cell */
      x = ((x + cell_width / 2) / CC_COLUMNS) * CC_COLUMNS + cell_width;
      y = base_y - (cap_info->max_char_height + 1) / 2;
    }
    else {
      x = cap_info->width * pos / CC_COLUMNS;
      y = base_y;
    }
    
#ifdef LOG_DEBUG
    printf("text_w, text_h = %d, %d\n", text_w, text_h);
    printf("cc from %d to %d; text plotting from %d, %d (basey = %d)\n", pos, endpos, x, y, base_y);
#endif

    /* make caption background a uniform box. Without this line, the */
    /* background is uneven for superscript characters. Also, pad left and */
    /* right with one character width to make text more readable. */
#warning "FIXME: There may be off-by one errors in the rendering - check with Miguel"
    renderer->filled_rect(display, x - cap_info->max_char_width, y, 
			  x + text_w + cap_info->max_char_width,
			  y + cap_info->max_char_height, CAP_BG_COL);

    /* render text part by rendering each attributed text segment */
    for (seg = 0; seg < num_seg; seg++) {
#ifdef LOG_DEBUG
      printf("ccrow_render: rendering segment %d from %d to %d / %d to %d\n",
	     seg, seg_pos[seg], seg_pos[seg + 1],
	     x + cumulative_seg_width[seg], x + cumulative_seg_width[seg + 1]);
#endif
      for (i = seg_pos[seg]; i < seg_pos[seg + 1]; i++)
	buf[i - seg_pos[seg]] = this->cells[i].c;
      buf[seg_pos[seg + 1] - seg_pos[seg]] = '\0';
      ccrow_set_attributes(this, renderer, display, seg_pos[seg], cap_cfg);
      renderer->render_text(display, x + cumulative_seg_width[seg], y, buf);
    }

    pos = ccrow_find_next_text_part(this, endpos);
  }
}


/*----------------- cc_buffer_t methods --------------------------------*/

static int ccbuf_has_displayable(cc_buffer_t *this)
{
  int i;
  int found = 0;
  for (i = 0; !found && i < CC_ROWS; i++) {
    if (this->rows[i].num_chars > 0)
      found = 1;
  }
  return found;
}


static void ccbuf_add_char(cc_buffer_t *this, uint8_t c)
{
  cc_row_t *rowbuf = &this->rows[this->rowpos];
  int pos = rowbuf->pos;
  int left_displayable = (pos > 0) && (pos <= rowbuf->num_chars);

#if LOG_DEBUG > 2
  printf("cc_decoder: ccbuf_add_char: %c @ %d/%d\n", c, this->rowpos, pos);
#endif

  if (pos >= CC_COLUMNS) {
    printf("cc_decoder: ccbuf_add_char: row buffer overflow\n");
    return;
  }

  if (pos > rowbuf->num_chars) {
    /* fill up to indented position with transparent spaces, if necessary */
    ccrow_fill_transp(rowbuf);
  }

  /* midrow PAC attributes are applied only if there is no displayable */
  /* character to the immediate left. This makes the implementation rather */
  /* complicated, but this is what the EIA-608 standard specifies. :-( */
  if (rowbuf->pac_attr_chg && !rowbuf->attr_chg && !left_displayable) {
    rowbuf->attr_chg = 1;
    rowbuf->cells[pos].attributes = rowbuf->pac_attr;
#ifdef LOG_DEBUG
    printf("cc_decoder: ccbuf_add_char: Applying midrow PAC.\n");
#endif
  }

  rowbuf->cells[pos].c = c;
  rowbuf->cells[pos].midrow_attr = rowbuf->attr_chg;
  rowbuf->pos++;

  if (rowbuf->num_chars < rowbuf->pos)
    rowbuf->num_chars = rowbuf->pos;

  rowbuf->attr_chg = 0;
  rowbuf->pac_attr_chg = 0;
}


static void ccbuf_set_cursor(cc_buffer_t *this, int row, int column, 
			     int underline, int italics, int color)
{
  cc_row_t *rowbuf = &this->rows[row];
  cc_attribute_t attr;

  attr.italic = italics;
  attr.underline = underline;
  attr.foreground = color;
  attr.background = BLACK;

  rowbuf->pac_attr = attr;
  rowbuf->pac_attr_chg = 1;

  this->rowpos = row; 
  rowbuf->pos = column;
  rowbuf->attr_chg = 0;
}


static void ccbuf_apply_attribute(cc_buffer_t *this, cc_attribute_t *attr)
{
  cc_row_t *rowbuf = &this->rows[this->rowpos];
  int pos = rowbuf->pos;
  
  rowbuf->attr_chg = 1;
  rowbuf->cells[pos].attributes = *attr;
  /* A midrow attribute always counts as a space */
  ccbuf_add_char(this, chartbl[(unsigned int) ' ']);
}


static void ccbuf_tab(cc_buffer_t *this, int tabsize)
{
  cc_row_t *rowbuf = &this->rows[this->rowpos];
  rowbuf->pos += tabsize;
  if (rowbuf->pos > CC_COLUMNS) {
#ifdef LOG_DEBUG
    printf("cc_decoder: ccbuf_tab: row buffer overflow\n");
#endif
    return;
  }
  /* tabs have no effect on pending PAC attribute changes */
}


/* CAUTION: THIS FUNCTION ASSUMES THAT THE MUTEX IS ALREADY LOCKED! */
static void ccbuf_render(cc_buffer_t *this,
			 cc_config_t *cap_info, osd_renderer_t *renderer,
			 osd_object_t *display)
{
  int row;

#ifdef LOG_DEBUG
  printf("cc_decoder: ccbuf_render\n");
#endif

  for (row = 0; row < CC_ROWS; ++row) {
    if (this->rows[row].num_chars > 0)
      ccrow_render(&this->rows[row], row, cap_info, renderer, display);
  }
}


/*----------------- cc_memory_t methods --------------------------------*/

static void ccmem_clear(cc_memory_t *this)
{
#ifdef LOG_DEBUG
  printf("cc_decoder.c: ccmem_clear: Clearing CC memory\n");
#endif
  memset(this, 0, sizeof (cc_memory_t));
}


static void ccmem_init(cc_memory_t *this)
{
  ccmem_clear(this);
}


static void ccmem_exit(cc_memory_t *this)
{
#warning "FIXME: anything to deallocate?"
}


/*----------------- cc_decoder_t methods --------------------------------*/

static void cc_set_channel(cc_decoder_t *this, int channel)
{
  (*this->active)->channel_no = channel;
#ifdef LOG_DEBUG
  printf("cc_decoder: cc_set_channel: selecting channel %d\n", channel);
#endif
}


static cc_buffer_t *active_ccbuffer(cc_decoder_t *this)
{
  cc_memory_t *mem = *this->active;
  return &mem->channel[mem->channel_no];
}


static uint32_t cc_calc_vpts(cc_decoder_t *this)
{
  metronom_t *metronom = this->metronom;
  uint32_t vpts = metronom->got_spu_packet(metronom, this->pts, 0, this->scr);
  return vpts + this->f_offset * NTSC_FRAME_DURATION;
}


static int cc_onscreen_displayable(cc_decoder_t *this)
{
  return ccbuf_has_displayable(&this->on_buf->channel[this->on_buf->channel_no]);
}


/* CAUTION: THIS FUNCTION ASSUMES THAT THE MUTEX IS ALREADY LOCKED! */
static void cc_do_hide(cc_decoder_t *this)
{
  if (this->displayed) {
    uint32_t vpts = cc_calc_vpts(this);

#ifdef LOG_DEBUG
    printf("cc_decoder: cc_do_hide: hiding caption %u at vpts %u\n", this->capid, vpts);
#endif

    this->renderer->hide(this->cap_display, vpts);
    this->displayed = 0;
  }
}


static void cc_hide_displayed(cc_decoder_t *this)
{
#ifdef LOG_DEBUG
  printf("cc_decoder: cc_hide_displayed\n");
#endif

  pthread_mutex_lock(&this->cc_cfg->cc_mutex);
  cc_do_hide(this);
  pthread_mutex_unlock(&this->cc_cfg->cc_mutex);
}


static void cc_show_displayed(cc_decoder_t *this)
{
#ifdef LOG_DEBUG
  printf("cc_decoder: cc_show_displayed\n");
#endif

  pthread_mutex_lock(&this->cc_cfg->cc_mutex);

  if (this->displayed) {
    cc_do_hide(this);
    printf("cc_decoder: cc_show_displayed: OOPS - caption was already displayed!\n");
  }

  if (cc_onscreen_displayable(this)) {
    uint32_t vpts = cc_calc_vpts(this); 
    this->capid++;

#ifdef LOG_DEBUG
    printf("cc_decoder: cc_show_displayed: showing caption %u at vpts %u\n", this->capid, vpts);
#endif    

    this->renderer->clear(this->cap_display);
    ccbuf_render(&this->on_buf->channel[this->on_buf->channel_no],
		 this->cc_cfg, this->renderer, this->cap_display);
    this->renderer->set_position(this->cap_display, this->cc_cfg->vars.x,
    				 this->cc_cfg->vars.y);
    this->renderer->show(this->cap_display, vpts);

    this->displayed = 1;
  }

  pthread_mutex_unlock(&this->cc_cfg->cc_mutex);
}


static void cc_swap_buffers(cc_decoder_t *this)
{
  cc_memory_t *temp;

  /* hide caption in displayed memory */
  cc_hide_displayed(this);

#ifdef LOG_DEBUG
  printf("cc_decoder: cc_swap_buffers: swapping caption memory\n");
#endif
  temp = this->on_buf;
  this->on_buf = this->off_buf;
  this->off_buf = temp;

  /* show new displayed memory */
  cc_show_displayed(this);
}

static void cc_decode_standard_char(cc_decoder_t *this, uint8_t c1, uint8_t c2)
{
  cc_buffer_t *buf = active_ccbuffer(this);
  /* c1 always is a valid character */
  ccbuf_add_char(buf, chartbl[c1]);
  /* c2 might not be a printable character, even if c1 was */
  if (c2 & 0x60)
    ccbuf_add_char(buf, chartbl[c2]);
}


static void cc_decode_PAC(cc_decoder_t *this, int channel,
			  uint8_t c1, uint8_t c2)
{
  cc_buffer_t *buf;
  int row, column = 0;
  int underline, italics = 0, color;

  /* There is one invalid PAC code combination. Ignore it. */
  if (c1 == 0x10 && c2 > 0x5f)
    return;

  cc_set_channel(this, channel);
  buf = active_ccbuffer(this);

  row = rowdata[((c1 & 0x07) << 1) | ((c2 & 0x20) >> 5)];
  if (c2 & 0x10) {
    column = ((c2 & 0x0e) >> 1) * 4;   /* preamble indentation */
    color = 0;                         /* indented lines have white color */
  }
  else if ((c2 & 0x0e) == 0x0e) {
    italics = 1;                       /* italics, they are always white */
    color = 0;
  }
  else
    color = (c2 & 0x0e) >> 1;
  underline = c2 & 0x01;

#ifdef LOG_DEBUG
  printf("cc_decoder: cc_decode_PAC: row %d, col %d, ul %d, it %d, clr %d\n",
	 row, column, underline, italics, color);
#endif

  ccbuf_set_cursor(buf, row, column, underline, italics, color);
}


static void cc_decode_ext_attribute(cc_decoder_t *this, int channel,
				    uint8_t c1, uint8_t c2)
{
  cc_set_channel(this, channel);
}


static void cc_decode_special_char(cc_decoder_t *this, int channel,
				   uint8_t c1, uint8_t c2)
{
  cc_buffer_t *buf;

  cc_set_channel(this, channel);
  buf = active_ccbuffer(this);
#ifdef LOG_DEBUG
  printf("cc_decoder: cc_decode_special_char: Mapping %x to %x\n", c2, specialchar[c2 & 0xf]);
#endif
  ccbuf_add_char(buf, specialchar[c2 & 0xf]);
}


static void cc_decode_midrow_attr(cc_decoder_t *this, int channel,
				  uint8_t c1, uint8_t c2)
{
  cc_buffer_t *buf;
  cc_attribute_t attr;

  cc_set_channel(this, channel);
  buf = active_ccbuffer(this);
  if (c2 < 0x2e) {
    attr.italic = 0;
    attr.foreground = (c2 & 0xe) >> 1;
  }
  else {
    attr.italic = 1;
    attr.foreground = WHITE;
  }
  attr.underline = c2 & 0x1;
  attr.background = BLACK;
#ifdef LOG_DEBUG
  printf("cc_decoder: cc_decode_midrow_attr: attribute %x\n", c2);
  printf("cc_decoder: cc_decode_midrow_attr: ul %d, it %d, clr %d\n",
	 attr.underline, attr.italic, attr.foreground);
#endif

  ccbuf_apply_attribute(buf, &attr);
}


static void cc_decode_misc_control_code(cc_decoder_t *this, int channel,
					uint8_t c1, uint8_t c2)
{
#ifdef LOG_DEBUG
  printf("cc_decoder: decode_misc: decoding %x %x\n", c1, c2);
#endif

  cc_set_channel(this, channel);

  switch (c2) {          /* 0x20 <= c2 <= 0x2f */

  case 0x20:             /* RCL */
    break;

  case 0x21:             /* backspace */
#ifdef LOG_DEBUG
    printf("cc_decoder: backspace\n");
#endif
    break;

  case 0x24:             /* DER */
    break;

  case 0x25:             /* RU2 */
    break;

  case 0x26:             /* RU3 */
    break;

  case 0x27:             /* RU4 */
    break;

  case 0x28:             /* FON */
    break;

  case 0x29:             /* RDC */
    break;

  case 0x2a:             /* TR */
    break;

  case 0x2b:             /* RTD */
    break;

  case 0x2c:             /* EDM - erase displayed memory */
    cc_hide_displayed(this);
    ccmem_clear(this->on_buf);
    break;

  case 0x2d:             /* carriage return */
    break;

  case 0x2e:             /* ENM - erase non-displayed memory */
    ccmem_clear(this->off_buf);
    break;

  case 0x2f:             /* EOC - swap displayed and non displayed memory */
    cc_swap_buffers(this);
    break;
  }
}


static void cc_decode_tab(cc_decoder_t *this, int channel,
			  uint8_t c1, uint8_t c2)
{
  cc_buffer_t *buf;

  cc_set_channel(this, channel);
  buf = active_ccbuffer(this);
  ccbuf_tab(buf, c2 & 0x3);
}


static void cc_decode_EIA608(cc_decoder_t *this, uint16_t data)
{
  uint8_t c1 = data & 0x7f;
  uint8_t c2 = (data >> 8) & 0x7f;

  if (c1 & 0x60) {             /* normal character, 0x20 <= c1 <= 0x7f */
    cc_decode_standard_char(this, c1, c2);
  }
  else if (c1 & 0x10) {        /* control code or special character */
                               /* 0x10 <= c1 <= 0x1f */
    int channel = (c1 & 0x08) >> 3;
    c1 &= ~0x08;

    /* control sequences are often repeated. In this case, we should */
    /* evaluate it only once. */
    if (data != this->lastcode) {

      if (c2 & 0x40) {         /* preamble address code: 0x40 <= c2 <= 0x7f */
	cc_decode_PAC(this, channel, c1, c2);
      }
      else {
	switch (c1) {
	  
	case 0x10:             /* extended background attribute code */
	  cc_decode_ext_attribute(this, channel, c1, c2);
	  break;

	case 0x11:             /* attribute or special character */
	  if ((c2 & 0x30) == 0x30) { /* special char: 0x30 <= c2 <= 0x3f  */
	    cc_decode_special_char(this, channel, c1, c2);
	  }
	  else if (c2 & 0x20) {     /* midrow attribute: 0x20 <= c2 <= 0x2f */
	    cc_decode_midrow_attr(this, channel, c1, c2);
	  }
	  break;

	case 0x14:             /* possibly miscellaneous control code */
	  cc_decode_misc_control_code(this, channel, c1, c2);
	  break;

	case 0x17:            /* possibly misc. control code TAB offset */
	                      /* 0x21 <= c2 <= 0x23 */
	  if (c2 >= 0x21 && c2 <= 0x23) {
	    cc_decode_tab(this, channel, c1, c2);
	  }
	  break;
	}
      }
    }
  }
  
  this->lastcode = data;
}


void decode_cc(cc_decoder_t *this, uint8_t *buffer, uint32_t buf_len,
	       uint32_t pts, uint32_t scr)
{
  /* The first number may denote a channel number. I don't have the
   * EIA-708 standard, so it is hard to say.
   * From what I could figure out so far, the general format seems to be:
   *
   * repeat
   *
   *   0xfe starts 2 byte sequence of unknown purpose. It might denote
   *        field #2 in line 21 of the VBI. We'll ignore it for the
   *        time being.
   *
   *   0xff starts 2 byte EIA-608 sequence, field #1 in line 21 of the VBI
   *
   *   0x00 is padding, followed by 2 more 0x00.
   *
   *   0x01 always seems to appear at the beginning, always seems to
   *        be followed by 0xf8, 0x9e. Ignored for the time being.
   *
   * until end of packet
   */
  uint8_t *current = buffer;
  uint32_t curbytes = 0;
  uint8_t data1, data2;
  uint8_t cc_code;

  this->f_offset = 0;
  this->pts = pts;
  this->scr = scr;
  
  while (curbytes < buf_len) {
    cc_code = *current++;
    curbytes++;
    
    if (buf_len - curbytes < 2) {
#ifdef LOG_DEBUG
      fprintf(stderr, "Not enough data for 2-byte CC encoding\n");
#endif
      break;
    }
    
    data1 = *current++;
    data2 = *current++;
    curbytes += 2;
    
    switch (cc_code) {
    case 0xfe:
      /* expect 2 byte encoding (perhaps CC3, CC4?) */
      /* ignore for time being */
      break;
      
    case 0xff:
      /* expect EIA-608 CC1/CC2 encoding */
      if (good_parity(data1 | (data2 << 8))) {
	cc_decode_EIA608(this, data1 | (data2 << 8));
	this->f_offset++;
      }
      break;
      
    case 0x00:
      /* This seems to be just padding */
      break;
      
    case 0x01:
      /* unknown Header info, ignore for the time being */
      break;
      
    default:
#ifdef LOG_DEBUG
      fprintf(stderr, "Unknown CC encoding: %x\n", cc_code);
#endif
      break;
    }
  }
}


/* CAUTION: THIS FUNCTION ASSUMES THAT THE MUTEX IS ALREADY LOCKED! */
static void cc_free_osd_object(cc_decoder_t *this)
{
  /* hide and free old displayed caption object if necessary */
  if (this->cap_display) {
    cc_do_hide(this);
    this->renderer->free_object(this->cap_display);
    this->cap_display = NULL;
  }
}


/* CAUTION: THIS FUNCTION ASSUMES THAT THE MUTEX IS ALREADY LOCKED! */
static void cc_adjust_osd_object(cc_decoder_t *this)
{
  cc_free_osd_object(this);

#ifdef LOG_DEBUG
  printf("cc_decoder: cc_adjust_osd_object: creating %dx%d OSD object\n",
	 this->cc_cfg->vars.width, this->cc_cfg->vars.height);
#endif

  /* create display object */
  this->cap_display = this->renderer->new_object(this->renderer,
						 this->cc_cfg->vars.width,
						 this->cc_cfg->vars.height);
  this->renderer->set_text_palette(this->cap_display, 2);  
}


/* CAUTION: THIS FUNCTION ASSUMES THAT THE MUTEX IS ALREADY LOCKED! */
static void cc_adjust_cap_area(cc_config_t *cfg)
{
  int fontw, fonth;
  int required_w, required_h;
  cc_confvar_t *cfgvars = &cfg->vars;

#ifdef LOG_DEBUG
  printf("cc_decoder: cc_adjust_cap_area\n");
#endif

  if (cfgvars->decoder && cfgvars->cc_enabled) {
    /* calculate preferred captioning area, as per the EIA-608 standard */
    cfgvars->x =  cfgvars->video_width * 10 / 100;
    cfgvars->y = cfgvars->video_height * 10 / 100;
    cfgvars->width = cfgvars->video_width * 80 / 100;
    cfgvars->height = cfgvars->video_height * 80 / 100;

    /* find maximum text width and height for normal & italic captioning */
    /* font */
    get_font_metrics(cfgvars->decoder->renderer, cfgvars->font, cfgvars->font_size,
		     &fontw, &fonth);
    cfgvars->max_char_width = fontw;
    cfgvars->max_char_height = fonth;
    get_font_metrics(cfgvars->decoder->renderer, cfgvars->italic_font,
		     cfgvars->font_size, &fontw, &fonth);
    cfgvars->max_char_width = MAX(fontw, cfgvars->max_char_width);
    cfgvars->max_char_height = MAX(fonth, cfgvars->max_char_height);
#ifdef LOG_DEBUG
    printf("cc_decoder: cc_adjust_cap_area: max text extents: %d, %d\n",
	   cfgvars->max_char_width, cfgvars->max_char_height);
#endif 

    /* need to adjust captioning area to accommodate font? */
    required_w = CC_COLUMNS * (cfgvars->max_char_width + 1);
    required_h = CC_ROWS * (cfgvars->max_char_height + 1);
    if (required_w > cfgvars->width) {
#ifdef LOG_DEBUG
      printf("cc_decoder: cc_adjust_cap_area: adjusting cap area width: %d\n",
	     required_w);
#endif
      cfgvars->width = required_w;
      cfgvars->x = (cfgvars->video_width - required_w) / 2;
    }
    if (required_h > cfgvars->height) {
#ifdef LOG_DEBUG
      printf("cc_decoder: cc_adjust_cap_area: adjusting cap area height: %d\n",
	     required_h);
#endif
      cfgvars->height = required_h;
      cfgvars->y = (cfgvars->video_height - required_h) / 2;
    }

    if (required_w <= cfgvars->video_width && required_h <= cfgvars->video_height) {
      cfgvars->can_cc = 1;
      cc_adjust_osd_object(cfgvars->decoder);
    }
    else {
      cfgvars->can_cc = 0;
      cc_free_osd_object(cfgvars->decoder);
      printf("cc_decoder: required captioning area %dx%d exceeds screen %dx%d,"
	     " captions disabled\n"
	     "            Perhaps you should choose a smaller font?\n",
	     required_w, required_h, cfgvars->video_width, cfgvars->video_height);
    }
  }
}


cc_decoder_t *cc_decoder_open(osd_renderer_t *renderer, metronom_t *metronom,
                              config_values_t *cfg, cc_config_t *cc_cfg)
{
  cc_decoder_t *this = (cc_decoder_t *) malloc(sizeof (cc_decoder_t));
  /* configfile stuff */
  this->cc_cfg = cc_cfg;

  this->metronom = metronom;

  ccmem_init(&this->buffer[0]);
  ccmem_init(&this->buffer[1]);
  this->on_buf = &this->buffer[0];
  this->off_buf = &this->buffer[1];
  this->active = &this->off_buf;

  this->lastcode = 0;
  this->capid = 0;

  this->pts = this->scr = this->f_offset = 0;

  /* create text renderer */
  this->renderer = renderer;

  pthread_mutex_lock(&this->cc_cfg->cc_mutex);
  this->displayed = 0;
  this->cap_display = NULL;
  this->cc_cfg->vars.decoder = this;
  cc_adjust_cap_area(this->cc_cfg);
  pthread_mutex_unlock(&this->cc_cfg->cc_mutex);

  return this;
}


void cc_decoder_close(cc_decoder_t *this)
{
  pthread_mutex_lock(&this->cc_cfg->cc_mutex);
  cc_free_osd_object(this);
  pthread_mutex_unlock(&this->cc_cfg->cc_mutex);

  ccmem_exit(&this->buffer[0]);
  ccmem_exit(&this->buffer[1]);

  free(this);
}

/*----------------- configuration listeners --------------------------------*/

static void cc_cfg_enable_change(void *cfg, cfg_entry_t *value)
{
  cc_config_t *cc_cfg = (cc_config_t *) cfg;

  pthread_mutex_lock(&cc_cfg->cc_mutex);
  cc_cfg->vars.cc_enabled = value->num_value;
  if (cc_cfg->vars.cc_enabled)
    cc_adjust_cap_area(cc_cfg);
  else if (cc_cfg->vars.decoder)
    cc_free_osd_object(cc_cfg->vars.decoder);
  pthread_mutex_unlock(&cc_cfg->cc_mutex);

#ifdef LOG_DEBUG
  printf("cc_decoder: closed captions are now %s.\n", cc_cfg->vars.cc_enabled?
	 "enabled" : "disabled");
#endif
  
}


static void cc_font_change(void *cfg, cfg_entry_t *value)
{
  cc_config_t *cc_cfg = (cc_config_t *) cfg;
  char *font;
  
  if (strcmp(value->key, "misc.cc_font") == 0)
    font = cc_cfg->vars.font;
  else
    font = cc_cfg->vars.italic_font;

  pthread_mutex_lock(&cc_cfg->cc_mutex);
  copy_str(font, value->str_value, CC_FONT_MAX);
  cc_adjust_cap_area(cc_cfg);
  pthread_mutex_unlock(&cc_cfg->cc_mutex);
#ifdef LOG_DEBUG
  printf("cc_decoder: changing %s to font %s\n", value->key, font);
#endif
}


static void cc_num_change(void *cfg, cfg_entry_t *value)
{
  cc_config_t *cc_cfg = (cc_config_t *) cfg;
  int *num;
  if (strcmp(value->key, "misc.cc_font_size") == 0)
    num = &cc_cfg->vars.font_size;
  else
    num = &cc_cfg->vars.center;

  pthread_mutex_lock(&cc_cfg->cc_mutex);
  *num = value->num_value;
  cc_adjust_cap_area(cc_cfg);
  pthread_mutex_unlock(&cc_cfg->cc_mutex);

#ifdef LOG_DEBUG
  printf("cc_decoder: changing %s to %d\n", value->key, *num);
#endif
}


/* called when the video frame size changes */
void cc_notify_frame_change(cc_decoder_t *this, int width, int height)
{
#ifdef LOG_DEBUG
  printf("cc_decoder: new frame size: %dx%d\n", width, height);
#endif

  pthread_mutex_lock(&this->cc_cfg->cc_mutex);
  this->cc_cfg->vars.video_width = width;
  this->cc_cfg->vars.video_height = height;
  cc_adjust_cap_area(this->cc_cfg);
  pthread_mutex_unlock(&this->cc_cfg->cc_mutex);
}


/*-------- initialization methods and main hook --------------------------*/

void cc_decoder_init(config_values_t *cfg, cc_config_t *cc_cfg)
{
  cc_confvar_t *cc_vars = &cc_cfg->vars;

  build_parity_table();
  build_char_table();

  pthread_mutex_init(&cc_cfg->cc_mutex, NULL);

  cc_vars->cc_enabled = cfg->register_bool(cfg, 
					   "misc.cc_enabled", 0,
					   "Enable closed captions in MPEG-2 streams",
					   NULL, cc_cfg_enable_change,
					   cc_cfg);

  copy_str(cc_vars->font, 
	   cfg->register_string(cfg, "misc.cc_font", "cc",
				"Standard closed captioning font",
				NULL, cc_font_change, cc_cfg),
	   CC_FONT_MAX);

  copy_str(cc_vars->italic_font,
	   cfg->register_string(cfg, "misc.cc_italic_font", "cci",
				"Italic closed captioning font",
				NULL, cc_font_change, cc_cfg),
	   CC_FONT_MAX);

  cc_vars->font_size = cfg->register_num(cfg, "misc.cc_font_size", 24,
					 "Closed captioning font size",
					 NULL, cc_num_change,
					 cc_cfg);

  cc_vars->center = cfg->register_bool(cfg, "misc.cc_center", 1,
				      "Center-adjust closed captions",
				      NULL, cc_num_change,
				      cc_cfg);
}


