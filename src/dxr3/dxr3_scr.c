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
 * $Id: dxr3_scr.c,v 1.7 2003/01/12 20:33:57 komadori Exp $
 */

/* dxr3 scr plugin.
 * enables xine to use the internal clock of the card as its
 * global time reference.
 */

#include <sys/ioctl.h>
#include <sys/ioccom.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "dxr3.h"
#include "dxr3_scr.h"

#define LOG_SCR 0


/* functions required by xine api */
static int     dxr3_scr_get_priority(scr_plugin_t *scr);
static void    dxr3_scr_start(scr_plugin_t *scr, int64_t vpts);
static int64_t dxr3_scr_get_current(scr_plugin_t *scr);
static void    dxr3_scr_adjust(scr_plugin_t *scr, int64_t vpts);
static int     dxr3_scr_set_speed(scr_plugin_t *scr, int speed);
static void    dxr3_scr_exit(scr_plugin_t *scr);

/* helper function */
static int     dxr3_mvcommand(int fd_control, int command);

/* config callback */
static void    dxr3_scr_update_priority(void *this_gen, xine_cfg_entry_t *entry);


dxr3_scr_t *dxr3_scr_init(xine_stream_t *stream)
{
  dxr3_scr_t *this;
  const char *confstr;
  
  this = (dxr3_scr_t *)malloc(sizeof(dxr3_scr_t));
  
  confstr = stream->xine->config->register_string(stream->xine->config,
    CONF_LOOKUP, CONF_DEFAULT, CONF_NAME, CONF_HELP, 0, NULL, NULL);
  if ((this->fd_control = open(confstr, O_WRONLY)) < 0) {
    printf("dxr3_scr: Failed to open control device %s (%s)\n",
      confstr, strerror(errno));
    free(this);
    return NULL;
  }
  
  this->scr_plugin.interface_version = 2;
  this->scr_plugin.get_priority      = dxr3_scr_get_priority;
  this->scr_plugin.start             = dxr3_scr_start;
  this->scr_plugin.get_current       = dxr3_scr_get_current;
  this->scr_plugin.adjust            = dxr3_scr_adjust;
  this->scr_plugin.set_speed         = dxr3_scr_set_speed;
  this->scr_plugin.exit              = dxr3_scr_exit;
  
  this->priority                     = stream->xine->config->register_num(
    stream->xine->config, "dxr3.scr_priority", 10, _("Dxr3: SCR plugin priority"),
    _("Scr priorities greater 5 make the dxr3 xine's master clock."), 20,
    dxr3_scr_update_priority, this);
  this->offset                       = 0;
  this->last_pts                     = 0;
  this->scanning                     = 0;
  
  pthread_mutex_init(&this->mutex, NULL);
  
#if LOG_SCR
  printf("dxr3_scr: init complete\n");
#endif
  return this;
}


static int dxr3_scr_get_priority(scr_plugin_t *scr)
{
  dxr3_scr_t *this = (dxr3_scr_t *)scr;
  return this->priority;
}

static void dxr3_scr_start(scr_plugin_t *scr, int64_t vpts)
{
  dxr3_scr_t *this = (dxr3_scr_t *)scr;
  uint32_t vpts32 = vpts >> 1;
  
  pthread_mutex_lock(&this->mutex);
  this->last_pts = vpts32;
  this->offset = vpts - ((int64_t)vpts32 << 1);
  if (ioctl(this->fd_control, EM8300_IOCTL_SCR_SET, &vpts32))
    printf("dxr3_scr: start failed (%s)\n", strerror(errno));
#if LOG_SCR
  printf("dxr3_scr: started with vpts %lld\n", vpts);
#endif
  /* mis-use vpts32 to set the clock speed to 0x900, which is normal speed */
  vpts32 = 0x900;
  ioctl(this->fd_control, EM8300_IOCTL_SCR_SETSPEED, &vpts32);
  this->scanning = 0;
  pthread_mutex_unlock(&this->mutex);
}

static int64_t dxr3_scr_get_current(scr_plugin_t *scr)
{
  dxr3_scr_t *this = (dxr3_scr_t *)scr;
  uint32_t pts;
  int64_t current;
  
  pthread_mutex_lock(&this->mutex);
  if (ioctl(this->fd_control, EM8300_IOCTL_SCR_GET, &pts))
    printf("dxr3_scr: get current failed (%s)\n", strerror(errno));
  if (this->last_pts > 0xF0000000 && pts < 0x10000000)
    /* wrap around detected, compensate with offset */
    this->offset += (int64_t)1 << 33;
  if (pts == 0)
    printf("dxr3_scr: WARNING: pts dropped to zero.\n");
  this->last_pts = pts;
  current = ((int64_t)pts << 1) + this->offset;
  pthread_mutex_unlock(&this->mutex);
  
  return current;
}

static void dxr3_scr_adjust(scr_plugin_t *scr, int64_t vpts)
{
  dxr3_scr_t *this = (dxr3_scr_t *)scr;
  uint32_t current_pts32;
  int32_t offset32;
 
  pthread_mutex_lock(&this->mutex);
  if (ioctl(this->fd_control, EM8300_IOCTL_SCR_GET, &current_pts32))
    printf("dxr3_scr: adjust get failed (%s)\n", strerror(errno));
  this->last_pts = current_pts32;
  this->offset = vpts - ((int64_t)current_pts32 << 1);
  offset32 = this->offset / 4;
  /* kernel driver ignores diffs < 7200, so abs(offset32) must be > 7200 / 4 */
  if (offset32 < -7200/4 || offset32 > 7200/4) {
    uint32_t vpts32 = vpts >> 1;
    if (ioctl(this->fd_control, EM8300_IOCTL_SCR_SET, &vpts32))
      printf("dxr3_scr: adjust set failed (%s)\n", strerror(errno));
    this->last_pts = vpts32;
    this->offset = vpts - ((int64_t)vpts32 << 1);
  }
#if LOG_SCR
  printf("dxr3_scr: adjusted to vpts %lld\n", vpts);
#endif
  pthread_mutex_unlock(&this->mutex);
}

static int dxr3_scr_set_speed(scr_plugin_t *scr, int speed)
{
  dxr3_scr_t *this = (dxr3_scr_t *)scr;
  uint32_t em_speed;
  int playmode;
  
  switch (speed) {
  case XINE_SPEED_PAUSE:
    em_speed = 0;
    playmode = MVCOMMAND_PAUSE;
    break;
  case XINE_SPEED_SLOW_4:
    em_speed = 0x900 / 4;
    playmode = MVCOMMAND_START;
    break;
  case XINE_SPEED_SLOW_2:
    em_speed = 0x900 / 2;
    playmode = MVCOMMAND_START;
    break;
  case XINE_SPEED_NORMAL:
    em_speed = 0x900;
    playmode = MVCOMMAND_SYNC;
    break;
  case XINE_SPEED_FAST_2:
    em_speed = 0x900 * 2;
    playmode = MVCOMMAND_START;
    break;
  case XINE_SPEED_FAST_4:
    em_speed = 0x900 * 4;
    playmode = MVCOMMAND_START;
    break;
  default:
    speed = em_speed = 0;
    playmode = MVCOMMAND_PAUSE;
  }
  
  if (dxr3_mvcommand(this->fd_control, playmode))
    printf("dxr3_scr: failed to playmode (%s)\n", strerror(errno));
  
  if(em_speed > 0x900)
    this->scanning = 1;
  else
    this->scanning = 0;
  
  if (ioctl(this->fd_control, EM8300_IOCTL_SCR_SETSPEED, &em_speed))
    printf("dxr3_scr: failed to set speed (%s)\n", strerror(errno));
  
#if LOG_SCR
  printf("dxr3_scr: speed set to mode %d\n", speed);
#endif
  return speed;
}

static void dxr3_scr_exit(scr_plugin_t *scr)
{
  dxr3_scr_t *this = (dxr3_scr_t *)scr;
  
  pthread_mutex_destroy(&this->mutex);
  free(this);
}


static int dxr3_mvcommand(int fd_control, int command)
{
  em8300_register_t regs;
  
  regs.microcode_register = 1;
  regs.reg = 0;
  regs.val = command;
  
  return ioctl(fd_control, EM8300_IOCTL_WRITEREG, &regs);
}

static void dxr3_scr_update_priority(void *this_gen, xine_cfg_entry_t *entry)
{
  ((dxr3_scr_t *)this_gen)->priority = entry->num_value;
  printf("dxr3_scr: setting scr priority to %d\n", 
    entry->num_value);
}
