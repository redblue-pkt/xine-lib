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
 * $Id: dxr3_decode_spu.c,v 1.4 2002/05/25 19:19:17 siggi Exp $
 */
 
/* dxr3 spu decoder plugin.
 * Accepts the spu data from xine and sends it directly to the
 * corresponding dxr3 device. Also handles dvd menu button highlights.
 * Takes precedence over libspudec due to a higher priority.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "buffer.h"
#include "xine-engine/bswap.h"
#include "nav_types.h"
#include "nav_read.h"
#include "dxr3.h"

#define LOG_PTS 0
#define LOG_SPU 0
#define LOG_BTN 0

#define MAX_SPU_STREAMS 32


/* plugin initialization function */
spu_decoder_t *init_spu_decoder_plugin(int iface_version, xine_t *xine);

/* functions required by xine api */
static char   *dxr3_spudec_get_id(void);
static int     dxr3_spudec_can_handle(spu_decoder_t *this_gen, int buf_type);
static void    dxr3_spudec_init(spu_decoder_t *this_gen, vo_instance_t *vo_out);
static void    dxr3_spudec_decode_data(spu_decoder_t *this_gen, buf_element_t *buf);
static void    dxr3_spudec_reset(spu_decoder_t *this_gen);
static void    dxr3_spudec_close(spu_decoder_t *this_gen);
static void    dxr3_spudec_dispose(spu_decoder_t *this_gen);

/* helper functions */
static int     dxr3_present(xine_t *xine);
static void    dxr3_spudec_event_listener(void *this_gen, xine_event_t *event_gen);
static int     dxr3_spudec_copy_nav_to_btn(pci_t *nav_pci, int32_t button, int32_t mode, em8300_button_t *btn);
static void    dxr3_swab_clut(int* clut);

/* plugin structures */
typedef struct dxr3_spu_stream_state_s {
  uint32_t                 stream_filter;
} dxr3_spu_stream_state_t;

typedef struct dxr3_spudec_s {
  spu_decoder_t            spu_decoder;
  vo_instance_t           *vo_out;
  xine_t                  *xine;
  
  char                     devname[128];
  char                     devnum[3];
  int                      fd_spu;   /* to access the dxr3 spu device */
  
  dxr3_spu_stream_state_t  spu_stream_state[MAX_SPU_STREAMS];
  int                      menu;     /* are we in a menu? */
  pci_t                    pci;
  uint32_t                 buttonN;  /* currently highlighted button */
} dxr3_spudec_t;


spu_decoder_t *init_spu_decoder_plugin(int iface_version, xine_t *xine)
{
  dxr3_spudec_t *this;
  const char *confstr;
  int dashpos;
  
  if (iface_version != 8) {
    printf( "dxr3_decode_spu: plugin doesn't support plugin API version %d.\n"
      "dxr3_decode_spu: this means there's a version mismatch between xine and this "
      "dxr3_decode_spu: decoder plugin. Installing current plugins should help.\n",
      iface_version);
    return NULL;
  }
  
  if (!dxr3_present(xine)) return NULL;
  
  this = (dxr3_spudec_t *)malloc(sizeof(dxr3_spudec_t));
  if (!this) return NULL;
  
  confstr = xine->config->register_string(xine->config,
    CONF_LOOKUP, CONF_DEFAULT, CONF_NAME, CONF_HELP, NULL, NULL);
  strncpy(this->devname, confstr, 128);
  this->devname[127] = '\0';
  dashpos = strlen(this->devname) - 2; /* the dash in the new device naming scheme would be here */
  if (this->devname[dashpos] == '-') {
    /* use new device naming scheme with trailing number */
    strncpy(this->devnum, &this->devname[dashpos], 3);
    this->devname[dashpos] = '\0';
  } else {
    /* use old device naming scheme without trailing number */
    /* FIXME: remove this when everyone uses em8300 >=0.12.0 */
    this->devnum[0] = '\0';
  }
  
  this->spu_decoder.interface_version = iface_version;
  this->spu_decoder.get_identifier    = dxr3_spudec_get_id;
  this->spu_decoder.can_handle        = dxr3_spudec_can_handle;
  this->spu_decoder.init              = dxr3_spudec_init;
  this->spu_decoder.decode_data       = dxr3_spudec_decode_data;
  this->spu_decoder.reset             = dxr3_spudec_reset;
  this->spu_decoder.close             = dxr3_spudec_close;
  this->spu_decoder.dispose           = dxr3_spudec_dispose;
  this->spu_decoder.priority          = 10;
  
  this->xine                          = xine;
  this->fd_spu                        = 0;
  this->menu                          = 0;
  this->pci.hli.hl_gi.hli_ss          = 0;
  this->buttonN                       = 1;
  
  xine_register_event_listener(xine, dxr3_spudec_event_listener, this);
    
  return &this->spu_decoder;
}

static char *dxr3_spudec_get_id(void)
{
  return "dxr3-spudec";
}

static int dxr3_spudec_can_handle(spu_decoder_t *this_gen, int buf_type)
{
  int type = buf_type & 0xFFFF0000;
  return (type == BUF_SPU_PACKAGE || type == BUF_SPU_CLUT || 
    type == BUF_SPU_NAV || type == BUF_SPU_SUBP_CONTROL);
}

static void dxr3_spudec_init(spu_decoder_t *this_gen, vo_instance_t *vo_out)
{
  dxr3_spudec_t *this = (dxr3_spudec_t *)this_gen;
  char tmpstr[128];
  int i;
  
  this->vo_out = vo_out;
  
  /* open dxr3 spu device */
  snprintf(tmpstr, sizeof(tmpstr), "%s_sp%s", this->devname, this->devnum);
  if ((this->fd_spu = open(tmpstr, O_WRONLY)) < 0) {
    printf("dxr3_decode_spu: Failed to open spu device %s (%s)\n",
      tmpstr, strerror(errno));
    return;
  }
#if LOG_SPU
  printf ("dxr3_decode_spu: init: SPU_FD = %i\n",this->fd_spu);
#endif
  for (i=0; i < MAX_SPU_STREAMS; i++) /* reset the spu filter for non-dvdnav */
    this->spu_stream_state[i].stream_filter = 1;
}

static void dxr3_spudec_decode_data(spu_decoder_t *this_gen, buf_element_t *buf)
{
  dxr3_spudec_t *this = (dxr3_spudec_t *)this_gen;
  ssize_t written;
  uint32_t stream_id = buf->type & 0x1f;
  
  if (buf->type == BUF_SPU_CLUT) {
#if LOG_SPU
    printf("dxr3_decode_spu: BUF_SPU_CLUT\n");
#endif
    if (buf->content[0] == 0)  /* cheap endianess detection */
      dxr3_swab_clut((int *)buf->content);
    if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_SETPALETTE, buf->content))
      printf("dxr3_decode_spu: failed to set CLUT (%s)\n", strerror(errno));
    return;
  }
  if(buf->type == BUF_SPU_SUBP_CONTROL) {
    /* FIXME: is BUF_SPU_SUBP_CONTROL used anymore? */
    int i;
    uint32_t *subp_control = (uint32_t *)buf->content;
    
    for (i = 0; i < MAX_SPU_STREAMS; i++)
      this->spu_stream_state[i].stream_filter = subp_control[i];
    return;
  }
  if(buf->type == BUF_SPU_NAV) {
#if LOG_BTN
    printf("dxr3_decode_spu: got NAV packet\n");
#endif
    uint8_t *p = buf->content;
    
    /* just watch out for menus */
    if (p[3] == 0xbf && p[6] == 0x00) { /* Private stream 2 */
      pci_t pci;
      
      nav_read_pci(&pci, p + 7);
#if LOG_BTN
      printf("dxr3_decode_spu: PCI packet hli_ss is %d\n", pci.hli.hl_gi.hli_ss);
#endif
      if (pci.hli.hl_gi.hli_ss == 1) {
        em8300_button_t btn;
        
        /* menu ahead, remember pci for later evaluation */
        xine_fast_memcpy(&this->pci, &pci, sizeof(pci_t));
        if ((dxr3_spudec_copy_nav_to_btn(&this->pci, this->buttonN, 0, &btn ) > 0))
          if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_BUTTON, &btn))
            printf("dxr3_decode_spu: failed to set spu button (%s)\n",
              strerror(errno));
      }
      
      if ((pci.hli.hl_gi.hli_ss == 0) && (this->pci.hli.hl_gi.hli_ss == 1)) {
        /* this is (or: should be, I hope I got this right) a
           subpicture plane, that hides all menu buttons */
        uint8_t empty_spu[] = {
          0x00, 0x26, 0x00, 0x08, 0x80, 0x00, 0x00, 0x80,
          0x00, 0x00, 0x00, 0x20, 0x01, 0x03, 0x00, 0x00,
          0x04, 0x00, 0x00, 0x05, 0x00, 0x00, 0x01, 0x00,
          0x00, 0x01, 0x06, 0x00, 0x04, 0x00, 0x07, 0xFF,
          0x00, 0x01, 0x00, 0x20, 0x02, 0xFF };
        /* leaving menu */
        this->pci.hli.hl_gi.hli_ss = 0;
        ioctl(this->fd_spu, EM8300_IOCTL_SPU_BUTTON, NULL);
        write(this->fd_spu, empty_spu, sizeof(empty_spu));
      }
    }
    return;
  }
  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
#if LOG_SPU
    printf("dxr3_decode_spu: Dropping SPU channel %d. Preview data\n", stream_id);
#endif
    return;
  }
  
  if (this->spu_stream_state[stream_id].stream_filter == 0) {
#if LOG_SPU
    printf("dxr3_decode_spu: Dropping SPU channel %d. Stream filtered\n", stream_id);
#endif
    return;
  }
  if ((this->xine->spu_channel & 0x1f) != stream_id  ) { 
#if LOG_SPU
    printf("dxr3_decode_spu: Dropping SPU channel %d. Not selected stream_id\n", stream_id);
#endif
    return;
  }
  if ((this->menu == 0) && (this->xine->spu_channel & 0x80)) { 
#if LOG_SPU
    printf("dxr3_decode_spu: Dropping SPU channel %d. Only allow forced display SPUs\n", stream_id);
#endif
    return;
  }
  
  if (buf->pts) {
    int64_t vpts;
    uint32_t vpts32;
    
    vpts = this->xine->metronom->got_spu_packet(
      this->xine->metronom, buf->pts);
#if LOG_PTS
    printf ("dxr3_decode_spu: pts=%lld vpts=%lld\n", buf->pts, vpts);
#endif
    vpts32 = vpts;
    if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_SETPTS, &vpts32))
      printf("dxr3_decode_spu: spu setpts failed (%s)\n", strerror(errno));
  }
  
#if LOG_SPU
  printf ("dxr3_decode_spu: write: SPU_FD = %i\n",this->fd_spu);
#endif
  written = write(this->fd_spu, buf->content, buf->size);
  if (written < 0) {
    printf("dxr3_decode_spu: spu device write failed (%s)\n",
      strerror(errno));
    return;
  }
  if (written != buf->size)
    printf("dxr3_decode_spu: Could only write %d of %d spu bytes.\n",
      written, buf->size);
}

static void dxr3_spudec_reset(spu_decoder_t *this_gen)
{
}

static void dxr3_spudec_close(spu_decoder_t *this_gen)
{
  dxr3_spudec_t *this = (dxr3_spudec_t *)this_gen;
#if LOG_SPU
  printf("dxr3_decode_spu: close: SPU_FD = %i\n",this->fd_spu);
#endif
  close(this->fd_spu);
  this->fd_spu = 0;
}

static void dxr3_spudec_dispose(spu_decoder_t *this_gen)
{
  dxr3_spudec_t *this = (dxr3_spudec_t *)this_gen;
  
  xine_remove_event_listener(this->xine, dxr3_spudec_event_listener);
  free (this);
}


static int dxr3_present(xine_t *xine)
{
  int info;
  
  if (xine && xine->video_driver) {
    info = xine->video_driver->get_property(xine->video_driver, VO_PROP_VO_TYPE);
#ifdef LOG_SPU
    printf("dxr3_decode_spu: dxr3 presence test: info = %d\n", info);
#endif
    if (info != VO_TYPE_DXR3)
      return 0;
  }
  return 1;
}

static void dxr3_spudec_event_listener(void *this_gen, xine_event_t *event_gen)
{
  dxr3_spudec_t *this = (dxr3_spudec_t *)this_gen;
  xine_spu_event_t *event = (xine_spu_event_t *)event_gen;
  
#if LOG_SPU
  printf ("dxr3_decode_spu: event caught: SPU_FD = %i\n",this->fd_spu);
#endif
  
  switch (event->event.type) {
  case XINE_EVENT_SPU_BUTTON:
    {
      spu_button_t *but = event->data;
      em8300_button_t btn;
#if LOG_BTN
      printf ("dxr3_decode_spu: got SPU_BUTTON\n");
#endif
      this->buttonN = but->buttonN;
      if ((but->show > 0) && (dxr3_spudec_copy_nav_to_btn(
        &this->pci, this->buttonN, but->show - 1, &btn) > 0))
        if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_BUTTON, &btn))
          printf("dxr3_decode_spu: failed to set spu button (%s)\n",
            strerror(errno));
#if LOG_BTN
      printf ("dxr3_decode_spu: buttonN = %u\n",but->buttonN);
#endif
    }
    break;
    
  case XINE_EVENT_SPU_CLUT:
    {
      spudec_clut_table_t *clut = event->data;
#if LOG_SPU
      printf ("dxr3_spu: got SPU_CLUT\n");
#endif
#ifdef WORDS_BIGENDIAN
      dxr3_swab_clut(clut->clut);
#endif
      if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_SETPALETTE, clut->clut))
        printf("dxr3_decode_spu: failed to set CLUT (%s)\n",
          strerror(errno));
    }
    break;
  case XINE_EVENT_SPU_FORCEDISPLAY:
    {
      this->menu = (int)event->data;
    }
    break;
  }  
}

static int dxr3_spudec_copy_nav_to_btn(pci_t *nav_pci, int32_t button, int32_t mode, em8300_button_t *btn)
{
  btni_t *button_ptr;
  
  /* FIXME: Need to communicate with dvdnav vm to get/set
   * "self->vm->state.HL_BTNN_REG" info.
   * now done via button events from dvdnav.
   *
   * if ( this->pci.hli.hl_gi.fosl_btnn > 0) {
   *   button = this->pci.hli.hl_gi.fosl_btnn ;
   * }
   */
   
  if ((button <= 0) || (button > nav_pci->hli.hl_gi.btn_ns)) {
    printf("dxr3_decode_spu: Unable to select button number %i as it doesn't exist. Forcing button 1\n",
      button);
    button = 1;
  }
  
  button_ptr = &nav_pci->hli.btnit[button-1];
  if(button_ptr->btn_coln != 0) {
#if LOG_BTN
    fprintf(stderr, "dxr3_decode_spu: normal button clut, mode %d\n", mode);
#endif
    btn->color = (nav_pci->hli.btn_colit.btn_coli[button_ptr->btn_coln-1][mode] >> 16);
    btn->contrast = (nav_pci->hli.btn_colit.btn_coli[button_ptr->btn_coln-1][mode]);
    /* FIXME: Only the first grouping of buttons are used at the moment */
    btn->left = button_ptr->x_start;
    btn->top  = button_ptr->y_start;
    btn->right = button_ptr->x_end;
    btn->bottom = button_ptr->y_end;
    return 1;
  } 
  return -1;
}

static void dxr3_swab_clut(int *clut)
{
  int i;
  for (i=0; i<16; i++)
    clut[i] = bswap_32(clut[i]);
}
