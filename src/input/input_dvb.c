/* 
 * Copyright (C) 2000-2003 the xine project
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
 *
 * input plugin for Digital TV (Digital Video Broadcast - DVB) devices
 * e.g. Hauppauge WinTV Nova supported by DVB drivers from Convergence
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#ifdef __sun
#include <sys/ioccom.h>
#endif
#include <sys/poll.h>

/* These will eventually be #include <linux/dvb/...> */
#include "dvb/dmx.h"
#include "dvb/frontend.h"

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"
#include "net_buf_ctrl.h"

/*
#define LOG
*/

#define FRONTEND_DEVICE "/dev/dvb/adapter0/frontend0"
#define DEMUX_DEVICE    "/dev/dvb/adapter0/demux0"
#define DVR_DEVICE      "/dev/dvb/adapter0/dvr0"

#define BUFSIZE 4096

#define NOPID 0xffff

typedef struct {
  int                            fd_frontend;
  int                            fd_demuxa, fd_demuxv;

  struct dvb_frontend_info       feinfo;

  struct dmx_pes_filter_params   pesFilterParamsV;
  struct dmx_pes_filter_params   pesFilterParamsA;

} tuner_t;

typedef struct {

  char *name;
  struct dvb_frontend_parameters front_param;
  int vpid;
  int apid;
  int sat_no;
  int tone;
  int pol;   
} channel_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;

  char             *mrls[2];

} dvb_input_class_t;

typedef struct {
  input_plugin_t      input_plugin;

  dvb_input_class_t  *cls;

  xine_stream_t      *stream;

  char               *mrl;

  off_t               curpos;

  nbc_t              *nbc;

  tuner_t            *tuner;
  channel_t          *channels;
  int                 fd;
  int                 num_channels;
  int                 channel;
  pthread_mutex_t     mutex;

  osd_object_t       *osd;

  xine_event_queue_t *event_queue;

  /* scratch buffer for forward seeking */
  char                seek_buf[BUFSIZE];

} dvb_input_plugin_t;

typedef struct {
	char *name;
	int value;
} Param;

static const Param inversion_list [] = {
	{ "INVERSION_OFF", INVERSION_OFF },
	{ "INVERSION_ON", INVERSION_ON },
	{ "INVERSION_AUTO", INVERSION_AUTO },
        { NULL, 0 }
};

static const Param bw_list [] = {
	{ "BANDWIDTH_6_MHZ", BANDWIDTH_6_MHZ },
	{ "BANDWIDTH_7_MHZ", BANDWIDTH_7_MHZ },
	{ "BANDWIDTH_8_MHZ", BANDWIDTH_8_MHZ },
        { NULL, 0 }
};

static const Param fec_list [] = {
	{ "FEC_1_2", FEC_1_2 },
	{ "FEC_2_3", FEC_2_3 },
	{ "FEC_3_4", FEC_3_4 },
	{ "FEC_4_5", FEC_4_5 },
	{ "FEC_5_6", FEC_5_6 },
	{ "FEC_6_7", FEC_6_7 },
	{ "FEC_7_8", FEC_7_8 },
	{ "FEC_8_9", FEC_8_9 },
	{ "FEC_AUTO", FEC_AUTO },
	{ "FEC_NONE", FEC_NONE },
        { NULL, 0 }
};

static const Param guard_list [] = {
	{"GUARD_INTERVAL_1_16", GUARD_INTERVAL_1_16},
	{"GUARD_INTERVAL_1_32", GUARD_INTERVAL_1_32},
	{"GUARD_INTERVAL_1_4", GUARD_INTERVAL_1_4},
	{"GUARD_INTERVAL_1_8", GUARD_INTERVAL_1_8},
        { NULL, 0 }
};

static const Param hierarchy_list [] = {
	{ "HIERARCHY_1", HIERARCHY_1 },
	{ "HIERARCHY_2", HIERARCHY_2 },
	{ "HIERARCHY_4", HIERARCHY_4 },
	{ "HIERARCHY_NONE", HIERARCHY_NONE },
        { NULL, 0 }
};

static const Param qam_list [] = {
	{ "QPSK", QPSK },
	{ "QAM_128", QAM_128 },
	{ "QAM_16", QAM_16 },
	{ "QAM_256", QAM_256 },
	{ "QAM_32", QAM_32 },
	{ "QAM_64", QAM_64 },
        { NULL, 0 }
};

static const Param transmissionmode_list [] = {
	{ "TRANSMISSION_MODE_2K", TRANSMISSION_MODE_2K },
	{ "TRANSMISSION_MODE_8K", TRANSMISSION_MODE_8K },
        { NULL, 0 }
};

static void tuner_dispose (tuner_t *this) {

  if (this->fd_frontend >= 0)
    close (this->fd_frontend);
  if (this->fd_demuxa >= 0)
    close (this->fd_demuxa);
  if (this->fd_demuxv >= 0)
    close (this->fd_demuxv);

  free (this);
}

static tuner_t *tuner_init () {

  tuner_t *this;

  this = malloc (sizeof (tuner_t));

  this->fd_frontend = -1;
  this->fd_demuxa = -1;
  this->fd_demuxv = -1;

  if ((this->fd_frontend = open(FRONTEND_DEVICE, O_RDWR)) < 0){
    perror("FRONTEND DEVICE");
    tuner_dispose(this);
    return NULL;
  }

  if ((ioctl (this->fd_frontend, FE_GET_INFO, &this->feinfo)) < 0) {
    perror("FE_GET_INFO");
    tuner_dispose(this);
    return NULL;
  }

  this->fd_demuxa = open (DEMUX_DEVICE, O_RDWR);
  if (this->fd_demuxa < 0) {
    perror ("DEMUX DEVICE audio");
    tuner_dispose(this);
    return NULL;
  }

  this->fd_demuxv=open (DEMUX_DEVICE, O_RDWR);
  if (this->fd_demuxv < 0) {
    perror ("DEMUX DEVICE video");
    tuner_dispose(this);
    return NULL;
  }

  return this;
}


static void tuner_set_vpid (tuner_t *this, ushort vpid) {

  if (vpid==0 || vpid==NOPID || vpid==0x1fff) {
    ioctl (this->fd_demuxv, DMX_STOP);
    return;
  }

  this->pesFilterParamsV.pid      = vpid;
  this->pesFilterParamsV.input    = DMX_IN_FRONTEND;
  this->pesFilterParamsV.output   = DMX_OUT_TS_TAP;
  this->pesFilterParamsV.pes_type = DMX_PES_VIDEO;
  this->pesFilterParamsV.flags    = DMX_IMMEDIATE_START;
  if (ioctl(this->fd_demuxv, DMX_SET_PES_FILTER,
	    &this->pesFilterParamsV) < 0)
    perror("set_vpid");
}

static void tuner_set_apid (tuner_t *this, ushort apid) {
  if (apid==0 || apid==NOPID || apid==0x1fff) {
    ioctl (this->fd_demuxa, DMX_STOP);
    return;
  }

  this->pesFilterParamsA.pid      = apid;
  this->pesFilterParamsA.input    = DMX_IN_FRONTEND;
  this->pesFilterParamsA.output   = DMX_OUT_TS_TAP;
  this->pesFilterParamsA.pes_type = DMX_PES_AUDIO;
  this->pesFilterParamsA.flags    = DMX_IMMEDIATE_START;
  if (ioctl (this->fd_demuxa, DMX_SET_PES_FILTER,
	     &this->pesFilterParamsA) < 0)
    perror("set_apid");
}

static int tuner_set_diseqc(tuner_t *this, channel_t *c)
{
   struct dvb_diseqc_master_cmd cmd =
      {{0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00}, 4};

   cmd.msg[3] = 0xf0 | ((c->sat_no * 4) & 0x0f) |
      (c->tone ? 1 : 0) | (c->pol ? 0 : 2);

   if (ioctl(this->fd_frontend, FE_SET_TONE, SEC_TONE_OFF) < 0)
      return 0;
   if (ioctl(this->fd_frontend, FE_SET_VOLTAGE,
	     c->pol ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18) < 0)
      return 0;
   usleep(15000);
   if (ioctl(this->fd_frontend, FE_DISEQC_SEND_MASTER_CMD, &cmd) < 0)
      return 0;
   usleep(15000);
   if (ioctl(this->fd_frontend, FE_DISEQC_SEND_BURST,
	     (c->sat_no / 4) % 2 ? SEC_MINI_B : SEC_MINI_A) < 0)
      return 0;
   usleep(15000);
   if (ioctl(this->fd_frontend, FE_SET_TONE,
	     c->tone ? SEC_TONE_ON : SEC_TONE_OFF) < 0)
      return 0;

   return 1;
}

static int tuner_tune_it (tuner_t *this, struct dvb_frontend_parameters
			  *front_param) {
  fe_status_t status;

  if (ioctl(this->fd_frontend, FE_SET_FRONTEND, front_param) <0) {
    perror("setfront front");
  }
  
  do {
    if (ioctl(this->fd_frontend, FE_READ_STATUS, &status) < 0) {
      perror("fe get event");
      return 0;
     }
     printf("input_dvb: status: %x\n", status);
     if (status & FE_HAS_LOCK) {
       return 1;
    }
    usleep(500000);
  }
  while (!(status & FE_TIMEDOUT));

  return 0;
}

static void print_channel (channel_t *channel) {
  printf ("input_dvb: channel '%s' freq %d vpid %d apid %d\n",
	  channel->name,
	  channel->front_param.frequency,
	  channel->vpid,
	  channel->apid);
}


static int tuner_set_channel (tuner_t *this,
			      channel_t *c) {

  print_channel (c);

  tuner_set_vpid (this, 0);
  tuner_set_apid (this, 0);

  if (this->feinfo.type==FE_QPSK) {
    if (!tuner_set_diseqc(this, c))
      return 0;
  }

  if (!tuner_tune_it (this, &c->front_param))
    return 0;

  tuner_set_vpid  (this, c->vpid);
  tuner_set_apid  (this, c->apid);

  return 1; /* fixme: error handling */
}

static void osd_show_channel (dvb_input_plugin_t *this) {

  int i, channel ;

  this->stream->osd_renderer->filled_rect (this->osd, 0, 0, 395, 400, 2);

  channel = this->channel - 5;

  for (i=0; i<11; i++) {

    if ( (channel >= 0) && (channel < this->num_channels) )
      this->stream->osd_renderer->render_text (this->osd, 10, 10+i*35,
					     this->channels[channel].name,
					     "iso-8859-1",
					     OSD_TEXT3);
    channel ++;
  }

  this->stream->osd_renderer->line (this->osd,   5, 183, 390, 183, 10);
  this->stream->osd_renderer->line (this->osd,   5, 183,   5, 219, 10);
  this->stream->osd_renderer->line (this->osd,   5, 219, 390, 219, 10);
  this->stream->osd_renderer->line (this->osd, 390, 183, 390, 219, 10);

  this->stream->osd_renderer->show (this->osd, 0);

}

static void switch_channel (dvb_input_plugin_t *this) {

  xine_event_t     event;
  xine_pids_data_t data;

  pthread_mutex_lock (&this->mutex);
  
  close (this->fd);

  if (!tuner_set_channel (this->tuner, &this->channels[this->channel])) {
    printf ("input_dvb: tuner_set_channel failed\n");
    pthread_mutex_unlock (&this->mutex);
    return;
  }

  event.type = XINE_EVENT_PIDS_CHANGE;
  data.vpid = this->channels[this->channel].vpid;
  data.apid = this->channels[this->channel].apid;
  event.data = &data;
  event.data_length = sizeof (xine_pids_data_t);

  printf ("input_dvb: sending event\n");

  xine_event_send (this->stream, &event);

  this->fd = open (DVR_DEVICE, O_RDONLY);

  pthread_mutex_unlock (&this->mutex);

  this->stream->osd_renderer->hide (this->osd, 0);
}

static void dvb_event_handler (dvb_input_plugin_t *this) {

  xine_event_t *event;

  while ((event = xine_event_get (this->event_queue))) {

#ifdef LOG
    printf ("input_dvb: got event %08x\n", event->type);
#endif

    if (this->fd<0) {
      xine_event_free (event);
      return;
    }

    switch (event->type) {

    case XINE_EVENT_INPUT_NEXT:
      if (this->channel < (this->num_channels-1))
	this->channel++;
      osd_show_channel (this);
      break;

    case XINE_EVENT_INPUT_PREVIOUS:
      if (this->channel>0)
	this->channel--;
      osd_show_channel (this);
      break;

    case XINE_EVENT_INPUT_DOWN:
      if (this->channel < (this->num_channels-1)) {
	this->channel++;
	switch_channel (this);
      }
      break;

    case XINE_EVENT_INPUT_UP:
      if (this->channel>0) {
	this->channel--;
	switch_channel (this);
      }
      break;

    case XINE_EVENT_INPUT_SELECT:
      switch_channel (this);
      break;

    case XINE_EVENT_INPUT_MENU1:
      this->stream->osd_renderer->hide (this->osd, 0);
      break;

#if 0
    default:
      printf ("input_dvb: got an event, type 0x%08x\n", event->type);
#endif
    }

    xine_event_free (event);
  }
}



static off_t dvb_plugin_read (input_plugin_t *this_gen,
			      char *buf, off_t len) {
  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;
  off_t n, total;

  dvb_event_handler (this);

#ifdef LOG
  printf ("input_dvb: reading %lld bytes...\n", len);
#endif

  nbc_check_buffers (this->nbc);

  pthread_mutex_lock( &this->mutex ); /* protect agains channel changes */
  total=0;
  while (total<len){ 
    n = read (this->fd, &buf[total], len-total);

#ifdef LOG
    printf ("input_dvb: got %lld bytes (%lld/%lld bytes read)\n",
	    n,total,len);
#endif
  
    if (n > 0){
      this->curpos += n;
      total += n;
    } else if (n<0 && errno!=EAGAIN) {
      pthread_mutex_unlock( &this->mutex );
      return total;
    }
  }

  pthread_mutex_unlock( &this->mutex );
  return total;
}

static buf_element_t *dvb_plugin_read_block (input_plugin_t *this_gen,
					     fifo_buffer_t *fifo, off_t todo) {
  /* dvb_input_plugin_t   *this = (dvb_input_plugin_t *) this_gen;  */
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);
  int                   total_bytes;


  buf->content = buf->mem;
  buf->type    = BUF_DEMUX_BLOCK;

  total_bytes = dvb_plugin_read (this_gen, buf->content, todo);

  if (total_bytes != todo) {
    buf->free_buffer (buf);
    return NULL;
  }

  buf->size = total_bytes;

  return buf;
}

static off_t dvb_plugin_seek (input_plugin_t *this_gen, off_t offset,
			      int origin) {

  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;

#ifdef LOG
  printf ("input_dvb: seek %lld bytes, origin %d\n",
	  offset, origin);
#endif

  /* only relative forward-seeking is implemented */

  if ((origin == SEEK_CUR) && (offset >= 0)) {

    for (;((int)offset) - BUFSIZE > 0; offset -= BUFSIZE) {
      this->curpos += dvb_plugin_read (this_gen, this->seek_buf, BUFSIZE);
    }

    this->curpos += dvb_plugin_read (this_gen, this->seek_buf, offset);
  }

  return this->curpos;
}

static off_t dvb_plugin_get_length (input_plugin_t *this_gen) {
  return 0;
}

static uint32_t dvb_plugin_get_capabilities (input_plugin_t *this_gen) {
  return 0; /* where did INPUT_CAP_AUTOPLAY go ?!? */
}

static uint32_t dvb_plugin_get_blocksize (input_plugin_t *this_gen) {
  return 0;
}

static off_t dvb_plugin_get_current_pos (input_plugin_t *this_gen){
  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;

  return this->curpos;
}

static void dvb_plugin_dispose (input_plugin_t *this_gen) {
  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;

  if (this->fd != -1) {
    close(this->fd);
    this->fd = -1;
  }
  
  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }

  if (this->event_queue)
    xine_event_dispose_queue (this->event_queue);

  free (this->mrl);
  
  if (this->channels)
    free (this->channels);
    
  if (this->tuner)
    tuner_dispose (this->tuner);
  
  free (this);
}

static char* dvb_plugin_get_mrl (input_plugin_t *this_gen) {
  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;

  return this->mrl;
}

static int dvb_plugin_get_optional_data (input_plugin_t *this_gen,
					 void *data, int data_type) {

  return INPUT_OPTIONAL_UNSUPPORTED;
}

static int find_param(const Param *list, const char *name)
{
  while (list->name && strcmp(list->name, name))
    list++;
  return list->value;;
}

static channel_t *load_channels (int *num_ch, fe_type_t fe_type) {

  FILE      *f;
  char       str[BUFSIZE];
  char       filename[BUFSIZE];
  channel_t *channels;
  int        num_channels;

  snprintf (filename, BUFSIZE, "%s/.xine/channels.conf", xine_get_homedir());

  f = fopen (filename, "rb");
  if (!f) {
    printf ("input_dvb: failed to open dvb channel file '%s'\n", filename);
    return NULL;
  }

  /*
   * count and alloc channels
   */
  num_channels = 0;
  while ( fgets (str, BUFSIZE, f)) {
    num_channels++;
  }
  fclose (f);
  printf ("input_dvb: %d channels found.\n", num_channels);

  channels = malloc (sizeof (channel_t) * num_channels);

  /*
   * load channel list 
   */

  f = fopen (filename, "rb");
  num_channels = 0;
  while ( fgets (str, BUFSIZE, f)) {

    unsigned long freq;
    char *field, *tmp;

    tmp = str;
    if (!(field = strsep(&tmp, ":")))
	continue;

    channels[num_channels].name = strdup(field);

    if (!(field = strsep(&tmp, ":")))
	continue;

    freq = strtoul(field, NULL, 0);

    switch (fe_type)
    {
    case FE_QPSK:

      if (freq > 11700) {
        channels[num_channels].front_param.frequency = (freq - 10600)*1000;
        channels[num_channels].tone = 1;
      } else {
        channels[num_channels].front_param.frequency = (freq - 9750)*1000;
        channels[num_channels].tone = 0;
      }

      channels[num_channels].front_param.inversion = INVERSION_OFF;
      
      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].pol = (field[0] == 'h' ? 0 : 1);
      
      if (!(field = strsep(&tmp, ":")))
	break;
      
      channels[num_channels].sat_no = strtoul(field, NULL, 0);
      
      if (!(field = strsep(&tmp, ":")))
	break;
      
      channels[num_channels].front_param.u.qpsk.symbol_rate =
	strtoul(field, NULL, 0) * 1000;

      channels[num_channels].front_param.u.qpsk.fec_inner = FEC_AUTO;

      break;

    case FE_QAM:

      channels[num_channels].front_param.frequency = freq;

      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].front_param.inversion =
	find_param(inversion_list, field);

      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].front_param.u.qam.symbol_rate =
	strtoul(field, NULL, 0);

      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].front_param.u.qam.fec_inner =
	find_param(fec_list, field);

      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].front_param.u.qam.modulation =
	find_param(qam_list, field);

      break;

    case FE_OFDM:

      channels[num_channels].front_param.frequency = freq;

      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].front_param.inversion =
	find_param(inversion_list, field);

      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].front_param.u.ofdm.bandwidth =
	find_param(bw_list, field);

      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].front_param.u.ofdm.code_rate_HP =
	find_param(fec_list, field);

      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].front_param.u.ofdm.code_rate_LP =
	find_param(fec_list, field);

      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].front_param.u.ofdm.constellation =
	find_param(qam_list, field);


      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].front_param.u.ofdm.transmission_mode =
	find_param(transmissionmode_list, field);

      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].front_param.u.ofdm.guard_interval =
	find_param(guard_list, field);

      if (!(field = strsep(&tmp, ":")))
	break;

      channels[num_channels].front_param.u.ofdm.hierarchy_information =
	find_param(hierarchy_list, field);

      break;

    }

    if (!(field = strsep(&tmp, ":")))
	continue;

    channels[num_channels].vpid = strtoul(field, NULL, 0);

    if (!(field = strsep(&tmp, ":")))
	continue;

    channels[num_channels].apid = strtoul(field, NULL, 0);

#ifdef LOG
    printf ("input: dvb channel %s loaded\n", channels[num_channels].name);
#endif

    num_channels++;
  }

  *num_ch = num_channels;
  return channels;
}

static int dvb_plugin_open (input_plugin_t *this_gen) {
  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;
  tuner_t            *tuner;
  channel_t          *channels;
  int                 num_channels;

  if ( !(tuner = tuner_init()) ) {
    printf ("input_dvb: cannot open dvb device\n");
    return 0;
  }

  if ( !(channels = load_channels(&num_channels, tuner->feinfo.type)) ) {
    tuner_dispose (tuner);
    return 0;
  }

  this->tuner    = tuner;
  this->channels = channels;
  this->num_channels = num_channels;

  if ( sscanf (this->mrl, "dvb://%d", &this->channel) != 1)
    this->channel = 0;

  if (!tuner_set_channel (this->tuner, &this->channels[this->channel])) {
    printf ("input_dvb: tuner_set_channel failed\n");
    tuner_dispose(this->tuner);
    free(this->channels);
    return 0;
  }

  if ((this->fd = open (DVR_DEVICE, O_RDONLY)) < 0){
    printf ("input_dvb: cannot open dvr device '%s'\n", DVR_DEVICE);
    tuner_dispose(this->tuner);
    free(this->channels);
    return 0;
  }

  this->curpos       = 0;
  this->osd          = NULL;

  pthread_mutex_init (&this->mutex, NULL);

  this->event_queue = xine_event_new_queue (this->stream);

  this->osd = this->stream->osd_renderer->new_object (this->stream->osd_renderer,
						      410, 410);
  this->stream->osd_renderer->set_position (this->osd, 20, 20);
  this->stream->osd_renderer->set_font (this->osd, "cetus", 32);
  this->stream->osd_renderer->set_text_palette (this->osd,
						TEXTPALETTE_WHITE_NONE_TRANSLUCID,
						OSD_TEXT3);

  return 1;
}

static input_plugin_t *dvb_class_get_instance (input_class_t *cls_gen,
				    xine_stream_t *stream,
				    const char *data) {

  dvb_input_class_t  *cls = (dvb_input_class_t *) cls_gen;
  dvb_input_plugin_t *this;
  char               *mrl = (char *) data;

  if (strncasecmp (mrl, "dvb:/",5))
    return NULL;

  this = (dvb_input_plugin_t *) xine_xmalloc (sizeof(dvb_input_plugin_t));

  this->stream       = stream;
  this->mrl          = strdup(mrl);
  this->cls          = cls;
  this->tuner        = NULL;
  this->channels     = NULL;
  this->fd           = -1;
  this->nbc          = nbc_init (this->stream);
  this->osd          = NULL;
  this->event_queue  = NULL;
    
  this->input_plugin.open              = dvb_plugin_open;
  this->input_plugin.get_capabilities  = dvb_plugin_get_capabilities;
  this->input_plugin.read              = dvb_plugin_read;
  this->input_plugin.read_block        = dvb_plugin_read_block;
  this->input_plugin.seek              = dvb_plugin_seek;
  this->input_plugin.get_current_pos   = dvb_plugin_get_current_pos;
  this->input_plugin.get_length        = dvb_plugin_get_length;
  this->input_plugin.get_blocksize     = dvb_plugin_get_blocksize;
  this->input_plugin.get_mrl           = dvb_plugin_get_mrl;
  this->input_plugin.get_optional_data = dvb_plugin_get_optional_data;
  this->input_plugin.dispose           = dvb_plugin_dispose;
  this->input_plugin.input_class       = cls_gen;

  return &this->input_plugin;
}

/*
 * dvb input plugin class stuff
 */

static char *dvb_class_get_description (input_class_t *this_gen) {
  return _("DVB (Digital TV) input plugin");
}

static char *dvb_class_get_identifier (input_class_t *this_gen) {
  return "dvb";
}

static void dvb_class_dispose (input_class_t *this_gen) {
  dvb_input_class_t *cls = (dvb_input_class_t *) this_gen;
  free (cls);
}

static int dvb_class_eject_media (input_class_t *this_gen) {
  return 1;
}

static char ** dvb_class_get_autoplay_list (input_class_t *this_gen,
					    int *num_files) {
  dvb_input_class_t *cls = (dvb_input_class_t *) this_gen;

  *num_files = 1;
  return cls->mrls;
}

static void *init_class (xine_t *xine, void *data) {

  dvb_input_class_t  *this;

  this = (dvb_input_class_t *) xine_xmalloc (sizeof (dvb_input_class_t));

  this->xine   = xine;

  this->input_class.get_instance       = dvb_class_get_instance;
  this->input_class.get_identifier     = dvb_class_get_identifier;
  this->input_class.get_description    = dvb_class_get_description;
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = dvb_class_get_autoplay_list;
  this->input_class.dispose            = dvb_class_dispose;
  this->input_class.eject_media        = dvb_class_eject_media;

  this->mrls[0] = "dvb://";
  this->mrls[1] = 0;

#ifdef LOG
  printf ("input_dvb: init class succeeded\n");
#endif

  return this;
}


/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT, 13, "DVB", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
