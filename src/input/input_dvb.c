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
#include <sys/poll.h>

#include "ost/dmx.h"
#include "ost/sec.h"
#include "ost/frontend.h"

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"
#include "net_buf_ctrl.h"

/*
#define LOG
*/

#define FRONTEND_DEVICE "/dev/ost/frontend"
#define SEC_DEVICE      "/dev/ost/sec"
#define DEMUX_DEVICE    "/dev/ost/demux"
#define DVR_DEVICE      "/dev/ost/dvr"

#define BUFSIZE 4096

#define NOPID 0xffff

typedef struct {
  int                       fd_frontend;
  int                       fd_sec;
  int                       fd_demuxa, fd_demuxv, fd_demuxtt;

  FrontendInfo              feinfo;
  FrontendParameters        front_param;

  struct secCommand         scmd;
  struct secCmdSequence     scmds;
  struct dmxPesFilterParams pesFilterParamsV;
  struct dmxPesFilterParams pesFilterParamsA;
  struct dmxPesFilterParams pesFilterParamsTT;

} tuner_t;

typedef struct {

  char *name;
  int   freq; /* freq - lof */
  int   tone; /* SEC_TONE_ON/OFF */
  int   volt; /* SC_VOLTAGE_13/18 */
  int   diseqcnr;
  int   srate;
  int   fec;
  int   vpid;
  int   apid;

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

  int                 out_fd; /* recording function */

} dvb_input_plugin_t;


static tuner_t *tuner_init () {

  tuner_t *this;

  this = malloc (sizeof (tuner_t));

  if ((this->fd_frontend = open(FRONTEND_DEVICE, O_RDWR)) < 0){
    perror("FRONTEND DEVICE: ");
    free (this);
    return NULL;
  }

  ioctl (this->fd_frontend, FE_GET_INFO, &this->feinfo);
  if (this->feinfo.type==FE_QPSK) {

    if ((this->fd_sec = open (SEC_DEVICE, O_RDWR)) < 0) {
      perror ("SEC DEVICE: ");
      free (this);
      return NULL;
    }
  }

  this->fd_demuxtt = open (DEMUX_DEVICE, O_RDWR);
  if (this->fd_demuxtt < 0) {
    perror ("DEMUX DEVICE tt: ");
    free (this);
    return NULL;
  }

  this->fd_demuxa = open (DEMUX_DEVICE, O_RDWR);
  if (this->fd_demuxa < 0) {
    perror ("DEMUX DEVICE audio: ");
    free (this);
    return NULL;
  }

  this->fd_demuxv=open (DEMUX_DEVICE, O_RDWR);
  if (this->fd_demuxv < 0) {
    perror ("DEMUX DEVICE video: ");
    free (this);
    return NULL;
  }

  return this;
}

static void tuner_dispose (tuner_t *this) {

  close (this->fd_frontend);
  close (this->fd_sec);
  close (this->fd_demuxa);
  close (this->fd_demuxv);
  close (this->fd_demuxtt);

  free (this);
}

static void tuner_set_vpid (tuner_t *this, ushort vpid) {

  if (vpid==0 || vpid==NOPID || vpid==0x1fff) {
    ioctl (this->fd_demuxv, DMX_STOP, 0);
    return;
  }

  this->pesFilterParamsV.pid     = vpid;
  this->pesFilterParamsV.input   = DMX_IN_FRONTEND;
  this->pesFilterParamsV.output  = DMX_OUT_TS_TAP;
  this->pesFilterParamsV.pesType = DMX_PES_VIDEO;
  this->pesFilterParamsV.flags   = DMX_IMMEDIATE_START;
  if (ioctl(this->fd_demuxv, DMX_SET_PES_FILTER,
	    &this->pesFilterParamsV) < 0)
    perror("set_vpid");
}

static void tuner_set_apid (tuner_t *this, ushort apid) {
  if (apid==0 || apid==NOPID || apid==0x1fff) {
    ioctl (this->fd_demuxa, DMX_STOP, apid);
    return;
  }
  
  this->pesFilterParamsA.pid     = apid;
  this->pesFilterParamsA.input   = DMX_IN_FRONTEND;
  this->pesFilterParamsA.output  = DMX_OUT_TS_TAP;
  this->pesFilterParamsA.pesType = DMX_PES_AUDIO;
  this->pesFilterParamsA.flags   = DMX_IMMEDIATE_START;
  if (ioctl (this->fd_demuxa, DMX_SET_PES_FILTER,
	     &this->pesFilterParamsA) < 0)
    perror("set_apid");
}

static void tuner_set_ttpid (tuner_t *this, ushort ttpid) {

  if (ttpid==0 || ttpid== NOPID || ttpid==0x1fff) {
    ioctl (this->fd_demuxtt, DMX_STOP, 0);
    return;
  }
  this->pesFilterParamsTT.pid     = ttpid;
  this->pesFilterParamsTT.input   = DMX_IN_FRONTEND;
  this->pesFilterParamsTT.output  = DMX_OUT_DECODER;
  this->pesFilterParamsTT.pesType = DMX_PES_TELETEXT;
  this->pesFilterParamsTT.flags   = DMX_IMMEDIATE_START;
  if (ioctl(this->fd_demuxtt, DMX_SET_PES_FILTER,
	    &this->pesFilterParamsTT) < 0) {
    /* printf("PID=%04x\n", ttpid); */
    perror("set_ttpid");
  }
}

static void tuner_get_front (tuner_t *this) {
  tuner_set_vpid (this, 0);
  tuner_set_apid (this, 0);
  tuner_set_ttpid(this, 0);
  this->scmds.voltage         = SEC_VOLTAGE_13;
  this->scmds.miniCommand     = SEC_MINI_NONE;
  this->scmds.continuousTone  = SEC_TONE_OFF;
  this->scmds.numCommands     = 1;
  this->scmds.commands        = &this->scmd;
}

static void tuner_set_diseqc_nr (tuner_t *this, int nr) {

  this->scmd.type=0;
  this->scmd.u.diseqc.addr = 0x10;
  this->scmd.u.diseqc.cmd = 0x38;
  this->scmd.u.diseqc.numParams = 1;
  this->scmd.u.diseqc.params[0] = 0xF0 | ((nr * 4) & 0x0F) |
    (this->scmds.continuousTone == SEC_TONE_ON ? 1 : 0) |
    (this->scmds.voltage==SEC_VOLTAGE_18 ? 2 : 0);
}

static void tuner_set_tp (tuner_t *this, int freq, int tone, 
			  int volt, int diseqcnr,
			  int srate, int fec) {

  static const uint8_t rfectab[9] = {1,2,3,0,4,0,5,0,0};

  this->front_param.Frequency = freq;
  this->scmds.continuousTone  = tone;
  this->scmds.voltage         = volt;
  tuner_set_diseqc_nr (this, diseqcnr);
  this->front_param.u.qpsk.SymbolRate = srate;
  this->front_param.u.qpsk.FEC_inner = (CodeRate)rfectab[fec];
  this->front_param.Inversion = INVERSION_AUTO;
}

static int tuner_tune_it (tuner_t *this, FrontendParameters *front_param) {
  FrontendEvent event;
  struct pollfd pfd[1];
  
  if (ioctl(this->fd_frontend, FE_SET_FRONTEND, front_param) <0)
    perror("setfront front");
  
  pfd[0].fd=this->fd_frontend;
  pfd[0].events=POLLIN;
  if (poll(pfd,1,2000)) {
    if (pfd[0].revents & POLLIN){
      if (ioctl(this->fd_frontend, FE_GET_EVENT, &event)
	  == -EBUFFEROVERFLOW){
	perror("fe get event");
	return 0;
      }
      switch(event.type){
      case FE_UNEXPECTED_EV:
	perror("unexpected event\n");
	return 0;
      case FE_FAILURE_EV:
	perror("failure event\n");
	return 0;
	
      case FE_COMPLETION_EV:
	fprintf(stderr, "completion event\n");
	return 1;
      }
    }
  }
  return 0;
}


static int tuner_set_front (tuner_t *this) {
  this->scmds.miniCommand = SEC_MINI_NONE;
  this->scmds.numCommands=1;
  this->scmds.commands=&this->scmd;
  
  tuner_set_vpid (this, 0);
  tuner_set_apid (this, 0);
  tuner_set_ttpid(this,0);
  
  if (this->feinfo.type==FE_QPSK) {
    if (ioctl(this->fd_sec, SEC_SEND_SEQUENCE, &this->scmds) < 0)
      perror("setfront sec");
    usleep(70000);
  }
  return tuner_tune_it(this, &this->front_param);
}

static void print_channel (channel_t *channel) {

  printf ("input_dvb: channel '%s' diseqc %d freq %d volt %d srate %d fec %d vpid %d apid %d\n",
	  channel->name,
	  channel->diseqcnr,
	  channel->freq,
	  channel->volt,
	  channel->srate,
	  channel->fec,
	  channel->vpid,
	  channel->apid);

}


static int tuner_set_channel (tuner_t *this, 
			      channel_t *c) {

  print_channel (c);

  tuner_get_front (this);
  tuner_set_tp (this, c->freq, c->tone, c->volt, c->diseqcnr, c->srate, c->fec);
  if (!tuner_set_front (this))
    return 0;
  
  tuner_set_vpid  (this, c->vpid);
  tuner_set_apid  (this, c->apid);
  tuner_set_ttpid (this, 0);
  
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

  if (this->out_fd>0)
    write (this->out_fd, buf, total);

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

  close(this->fd);
  this->fd = -1;

  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }

  if (this->out_fd>0)
    close (this->out_fd);

  xine_event_dispose_queue (this->event_queue);

  free (this->mrl);
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

static channel_t *load_channels (int *num_ch) {

  FILE         *f;
  unsigned char str[BUFSIZE];
  unsigned char filename[BUFSIZE];
  channel_t    *channels;
  int           num_channels;

  snprintf (filename, BUFSIZE, "%s/.xine/dvb_channels", xine_get_homedir());

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
    fgets (str, BUFSIZE, f);
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
    
    int freq;

    channels[num_channels].name = strdup (str);

    fgets (str, BUFSIZE, f);

    sscanf (str, "%d %d %d %d %d %d %d\n", 
	    &channels[num_channels].diseqcnr,
	    &freq,
	    &channels[num_channels].volt,
	    &channels[num_channels].srate,
	    &channels[num_channels].fec,
	    &channels[num_channels].vpid,
	    &channels[num_channels].apid);

    if (freq > 11700000) {
      channels[num_channels].freq = freq - 10600000;
      channels[num_channels].tone = SEC_TONE_ON;
    } else {
      channels[num_channels].freq = freq - 9750000;
      channels[num_channels].tone = SEC_TONE_OFF;
    }

#ifdef LOG
    printf ("input: dvb channel %s loaded\n", channels[num_channels].name);
#endif

    num_channels++;
  } 

  *num_ch = num_channels;
  return channels;
}

static input_plugin_t *open_plugin (input_class_t *cls_gen, 
				    xine_stream_t *stream, 
				    const char *data) {

  dvb_input_class_t  *cls = (dvb_input_class_t *) cls_gen; 
  dvb_input_plugin_t *this;
  tuner_t            *tuner;
  channel_t          *channels;
  int                 num_channels;
  char               *mrl = (char *) data;

  if (strncasecmp (mrl, "dvb:/",5)) 
    return NULL;

  if ( !(tuner = tuner_init()) ) {
    printf ("input_dvb: cannot open dvb device\n");
    return NULL;
  }

  if ( !(channels = load_channels(&num_channels)) ) {
    tuner_dispose (tuner);
    return NULL;
  }
  
  this = (dvb_input_plugin_t *) xine_xmalloc (sizeof(dvb_input_plugin_t));

  this->tuner    = tuner;
  this->channels = channels;

  if ( sscanf (mrl, "dvb://%d", &this->channel) != 1)
    this->channel = 0;

  if (!tuner_set_channel (this->tuner, &this->channels[this->channel])) {
    printf ("input_dvb: tuner_set_channel failed\n");
    free (this);
    return NULL;
  }

  if ((this->fd = open (DVR_DEVICE, O_RDONLY)) < 0){
    printf ("input_dvb: cannot open dvr device '%s'\n", DVR_DEVICE);
    free (this);
    return NULL;
  }

  this->mrl = strdup(mrl); 

  this->curpos       = 0;
  this->nbc          = nbc_init (stream);
  nbc_set_high_water_mark (this->nbc, 80);
  this->stream       = stream;
  this->tuner        = tuner;
  this->channels     = channels;
  this->num_channels = num_channels;
  this->osd          = NULL;

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
  this->cls                            = cls;

  pthread_mutex_init (&this->mutex, NULL);

#if 0
  this->out_fd = open ("foo.ts", O_CREAT | O_WRONLY | O_TRUNC, 0644);
#else
  this->out_fd = 0;
#endif

  this->event_queue = xine_event_new_queue (this->stream);

  this->osd = this->stream->osd_renderer->new_object (this->stream->osd_renderer, 
						      410, 410);
  this->stream->osd_renderer->set_position (this->osd, 20, 20);
  this->stream->osd_renderer->set_font (this->osd, "cetus", 32);
  this->stream->osd_renderer->set_text_palette (this->osd,
						TEXTPALETTE_WHITE_NONE_TRANSLUCID,
						OSD_TEXT3);

  return (input_plugin_t *) this;
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

  dvb_input_class_t  *cls = (dvb_input_class_t *) this_gen;

  free (cls);
}

static int dvb_class_eject_media (input_class_t *this_gen) {
  return 1;
}

static char ** dvb_class_get_autoplay_list (input_class_t *this_gen, 
					    int *num_files) {
  dvb_input_class_t  *cls = (dvb_input_class_t *) this_gen;

  *num_files = 1;
  return cls->mrls;
}

static void *init_class (xine_t *xine, void *data) {

  dvb_input_class_t  *this;

  this = (dvb_input_class_t *) xine_xmalloc (sizeof (dvb_input_class_t));

  this->xine   = xine;

  this->input_class.open_plugin        = open_plugin;
  this->input_class.get_identifier     = dvb_class_get_identifier;
  this->input_class.get_description    = dvb_class_get_description;
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = dvb_class_get_autoplay_list;
  this->input_class.dispose            = dvb_class_dispose;
  this->input_class.eject_media        = dvb_class_eject_media;

  this->mrls[0] = "dvb://";
  this->mrls[1] = 0;

  printf ("input_dvb: init class succeeded\n");

  return this;
}


/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_INPUT, 11, "DVB", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
