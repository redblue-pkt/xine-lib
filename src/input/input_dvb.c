/* 
 * Copyright (C) 2000-2004 the xine project
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
 * TODO: (not in any order)
 * - Parse all Administrative PIDs - NIT,SDT,CAT etc
 * - As per James' suggestion, we need a way for the demuxer
 *   to request PIDs from the input plugin.
 * - Timeshift ability.
 * - Pipe teletext infomation to a named fifo so programs such as
 *   Alevtd can read it.
 * - Allow the user to view one set of PIDs (channel) while
 *   recording another on the same transponder - this will require either remuxing or
 *   perhaps bypassing the demuxer completely - we could easily have access to the 
 *   individual audio/video streams via seperate read calls, so send them to the decoders
 *   and save the TS output to disk instead of passing it to the demuxer.
 *   This also gives us full control over the streams being played..hmm..control...
 * - Parse and use EIT for programming info. - DONE (well, partially)...
 * - Allow the user to find and tune new stations from within xine, and
 *   do away with the need for dvbscan & channels.conf file.
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
#include <poll.h>
#ifdef __sun
#include <sys/ioccom.h>
#endif
#include <sys/poll.h>
#include <time.h>
#include <dirent.h>

/* These will eventually be #include <linux/dvb/...> */
#include "dvb/dmx.h"
#include "dvb/frontend.h"

#define LOG_MODULE "input_dvb"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"
#include "net_buf_ctrl.h"

/* comment this out to have audio-only streams in the menu as well */
/* workaround for xine's unability to handle audio-only ts streams */
#define FILTER_RADIO_STREAMS 

#define FRONTEND_DEVICE "/dev/dvb/adapter0/frontend0"
#define DEMUX_DEVICE    "/dev/dvb/adapter0/demux0"
#define DVR_DEVICE      "/dev/dvb/adapter0/dvr0"

#define BUFSIZE 16384

#define NOPID 0xffff

/* define stream types 
 * administrative/system PIDs first */
#define INTERNAL_FILTER 0
#define PATFILTER 1
#define CATFILTER 2
#define NITFILTER 3
#define SDTFILTER 4
#define EITFILTER 5
#define PCRFILTER 6
#define VIDFILTER 7
#define AUDFILTER 8
#define AC3FILTER 9
#define TXTFILTER 10
#define SUBFILTER 11
#define TSDTFILTER 12
#define STFILTER 13
#define TDTFILTER 14
#define DITFILTER 15
#define RSTFILTER 16
/* define other pids to filter for */
#define PMTFILTER 17
#define MAXFILTERS 18

#define MAX_AUTOCHANNELS 70

#define bcdtoint(i) ((((i & 0xf0) >> 4) * 10) + (i & 0x0f))

typedef struct {
  int                            fd_frontend;
  int                            fd_pidfilter[MAXFILTERS];

  struct dvb_frontend_info       feinfo;

  struct dmx_pes_filter_params   pesFilterParams[MAXFILTERS];
  struct dmx_sct_filter_params	 sectFilterParams[MAXFILTERS];
  xine_t                        *xine;
} tuner_t;

typedef struct {
    /* EIT Information */
  char 		     *progname;
  char		     *description;
  char		     *starttime;
  
  char		     *duration;
  char 		     *content;
  int 		      rating;
} eit_info_t;

typedef struct {

  char                            *name;
  struct dvb_frontend_parameters   front_param;
  int                              pid[MAXFILTERS];
  int				   service_id;
  int                              sat_no;
  int                              tone;
  int                              pol;
  int				   pmtpid;
  eit_info_t			   eit[2];
} channel_t;

typedef struct {

  input_class_t     input_class;

  xine_t           *xine;

  char             *mrls[5];

  int 		    numchannels;

  char		   *autoplaylist[MAX_AUTOCHANNELS];
} dvb_input_class_t;

typedef struct {
  input_plugin_t      input_plugin;

  dvb_input_class_t  *class;

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
  osd_object_t       *rec_osd;
  osd_object_t	     *name_osd;
  osd_object_t	     *paused_osd;
  osd_object_t 	     *proginfo_osd;
  osd_object_t	     *channel_osd;
  osd_object_t	     *background;
  
  xine_event_queue_t *event_queue;

  /* scratch buffer for forward seeking */
  char                seek_buf[BUFSIZE];

  /* simple vcr-like functionality */
  int                 record_fd;
  int		      record_paused;
  /* centre cutout zoom */
  int 		      zoom_ok;
  /* display channel name */
  int                 displaying;
  /* buffer for EIT data */
  char 		     *eitbuffer;
  int 		      num_streams_in_this_ts;
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

struct tm * dvb_mjdtime (char *buf);
static void do_eit(dvb_input_plugin_t *this);


/* Utility Functions */
static unsigned int getbits(unsigned char *buffer, unsigned int bitpos, unsigned int bitcount)
{
    unsigned int i;
    unsigned int val = 0;

    if (bitcount > 32)
      printf("sorry bitcount too big\n");
    for (i = bitpos; i < bitcount + bitpos; i++) {
      val = val << 1;
      val = val + ((buffer[i >> 3] & (0x80 >> (i & 7))) ? 1 : 0);
    }
    return val;
}


static int find_descriptor(uint8_t tag, const unsigned char *buf, int descriptors_loop_len, 
                                                const unsigned char **desc, int *desc_len)
{

  while (descriptors_loop_len > 0) {
    unsigned char descriptor_tag = buf[0];
    unsigned char descriptor_len = buf[1] + 2;

    if (!descriptor_len) {
      printf("descriptor_tag == 0x%02x, len is 0\n", descriptor_tag);
      break;
    }

    if (tag == descriptor_tag) {
      if (desc)
        *desc = buf;

      if (desc_len)
        *desc_len = descriptor_len;
	 return 1;
    }

    buf += descriptor_len;
    descriptors_loop_len -= descriptor_len;
  }
  return 0;
}

/* extract UTC time and date encoded in modified julian date format and return pointer to tm 
 * in localtime 
 */
struct tm * dvb_mjdtime (char *buf)
{
  int i;
  unsigned int year, month, day, hour, min, sec;
  unsigned long int mjd;
  struct tm *tma = malloc(sizeof(struct tm));
  struct tm *dvb_time; 
  time_t t;
  
  memset(tma,0,sizeof(struct tm));
  
  mjd =	(unsigned int)(buf[0] & 0xff) << 8;
  mjd +=(unsigned int)(buf[1] & 0xff);
  hour =(unsigned char)bcdtoint(buf[2] & 0xff);
  min = (unsigned char)bcdtoint(buf[3] & 0xff);
  sec = (unsigned char)bcdtoint(buf[4] & 0xff);
  year =(unsigned long)((mjd - 15078.2)/365.25);
  month=(unsigned long)((mjd - 14956.1 - (unsigned long)(year * 365.25))/30.6001);
  day = mjd - 14956 - (unsigned long)(year * 365.25) - (unsigned long)(month * 30.6001);
  
  if (month == 14 || month == 15)
    i = 1;
  else
    i = 0;
  year += i;
  month = month - 1 - i * 12;
  
  tma->tm_sec=sec;
  tma->tm_min=min;
  tma->tm_hour=hour;
  tma->tm_mday=day;
  tma->tm_mon=month-1;
  tma->tm_year=year;

  t=timegm(tma);
  dvb_time=localtime(&t);
  
  free(tma);
  return dvb_time;
}


static void tuner_dispose(tuner_t * this)
{

    int x;
    if (this->fd_frontend >= 0)
      close(this->fd_frontend);
    
    /* close all pid filter filedescriptors */
    for (x = 0; x < MAXFILTERS; x++)
      if (this->fd_pidfilter[x] >= 0)
        close(this->fd_pidfilter[x]);
    

    free(this);
}


static tuner_t *tuner_init(xine_t * xine)
{

    tuner_t *this;
    int x;
    this = (tuner_t *) xine_xmalloc(sizeof(tuner_t));
    
    this->fd_frontend = -1;
    for (x = 0; x < MAXFILTERS; x++)
      this->fd_pidfilter[x] = 0;

    this->xine = xine;

    if ((this->fd_frontend = open(FRONTEND_DEVICE, O_RDWR)) < 0) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "FRONTEND DEVICE: %s\n", strerror(errno));
      tuner_dispose(this);
      return NULL;
    }

    if ((ioctl(this->fd_frontend, FE_GET_INFO, &this->feinfo)) < 0) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "FE_GET_INFO: %s\n", strerror(errno));
      tuner_dispose(this);
      return NULL;
    }

    for (x = 0; x < MAXFILTERS; x++) {
      this->fd_pidfilter[x] = open(DEMUX_DEVICE, O_RDWR);
      if (this->fd_pidfilter[x] < 0) {
        xprintf(this->xine, XINE_VERBOSITY_DEBUG, "DEMUX DEVICE PIDfilter: %s\n", strerror(errno));
        tuner_dispose(this);
	return NULL;
      }
   }
   /* open EIT with NONBLOCK */
   if(fcntl(this->fd_pidfilter[EITFILTER], F_SETFL, O_NONBLOCK)<0)
     xprintf(this->xine,XINE_VERBOSITY_DEBUG,"input_dvb: couldn't set EIT to nonblock: %s\n",strerror(errno));
    /* and the frontend */
    fcntl(this->fd_frontend, F_SETFL, O_NONBLOCK);
   
  return this;
}


static void dvb_set_pidfilter(dvb_input_plugin_t * this, int filter, ushort pid, int pidtype, int taptype)
{
    tuner_t *tuner = this->tuner;
    
    ioctl(tuner->fd_pidfilter[filter], DMX_STOP);

    this->channels[this->channel].pid[filter] = pid;
    tuner->pesFilterParams[filter].pid = pid;
    tuner->pesFilterParams[filter].input = DMX_IN_FRONTEND;
    tuner->pesFilterParams[filter].output = taptype;
    tuner->pesFilterParams[filter].pes_type = pidtype;
    tuner->pesFilterParams[filter].flags = DMX_IMMEDIATE_START;
    if (ioctl(tuner->fd_pidfilter[filter], DMX_SET_PES_FILTER, &tuner->pesFilterParams[filter]) < 0)
	   xprintf(tuner->xine, XINE_VERBOSITY_DEBUG, "input_dvb: set_pid: %s\n", strerror(errno));

}


static void dvb_set_sectfilter(dvb_input_plugin_t * this, int filter, ushort pid, int pidtype, char table, char mask)
{
    tuner_t *tuner = this->tuner;
    
    this->channels[this->channel].pid [filter] = pid;
    
    tuner->sectFilterParams[filter].pid = pid;
    memset(&tuner->sectFilterParams[filter].filter.filter,0,DMX_FILTER_SIZE);
    memset(&tuner->sectFilterParams[filter].filter.mask,0,DMX_FILTER_SIZE);
    tuner->sectFilterParams[filter].timeout = 0;
    tuner->sectFilterParams[filter].filter.filter[0] = table;
    tuner->sectFilterParams[filter].filter.mask[0] = mask;
    tuner->sectFilterParams[filter].flags = DMX_IMMEDIATE_START;
    if (ioctl(tuner->fd_pidfilter[filter], DMX_SET_FILTER, &tuner->sectFilterParams[filter]) < 0)
	   xprintf(tuner->xine, XINE_VERBOSITY_DEBUG, "input_dvb: set_pid: %s\n", strerror(errno));

}


static int find_param(const Param *list, const char *name)
{
  while (list->name && strcmp(list->name, name))
    list++;
  return list->value;;
}

static int extract_channel_from_string(channel_t * channel,char * str,fe_type_t fe_type)
{
	/*
		try to extract channel data from a string in the following format
		(DVBS) QPSK: <channel name>:<frequency>:<polarisation>:<sat_no>:<sym_rate>:<vpid>:<apid>
		(DVBC) QAM: <channel name>:<frequency>:<inversion>:<sym_rate>:<fec>:<qam>:<vpid>:<apid>
		(DVBT) OFDM: <channel name>:<frequency>:<inversion>:
						<bw>:<fec_hp>:<fec_lp>:<qam>:
						<transmissionm>:<guardlist>:<hierarchinfo>:<vpid>:<apid>
		
		<channel name> = any string not containing ':'
		<frequency>    = unsigned long
		<polarisation> = 'v' or 'h'
		<sat_no>       = unsigned long, usually 0 :D
		<sym_rate>     = symbol rate in MSyms/sec
		
		
		<inversion>    = INVERSION_ON | INVERSION_OFF | INVERSION_AUTO
		<fec>          = FEC_1_2, FEC_2_3, FEC_3_4 .... FEC_AUTO ... FEC_NONE
		<qam>          = QPSK, QAM_128, QAM_16 ...

		<bw>           = BANDWIDTH_6_MHZ, BANDWIDTH_7_MHZ, BANDWIDTH_8_MHZ
		<fec_hp>       = <fec>
		<fec_lp>       = <fec>
		<transmissionm> = TRANSMISSION_MODE_2K, TRANSMISSION_MODE_8K
		<vpid>         = video program id
		<apid>         = audio program id

	*/
	unsigned long freq;
	char *field, *tmp;

	tmp = str;
	
	/* find the channel name */
	if(!(field = strsep(&tmp,":")))return -1;
	channel->name = strdup(field);

	/* find the frequency */
	if(!(field = strsep(&tmp, ":")))return -1;
	freq = strtoul(field,NULL,0);

	switch(fe_type)
	{
		case FE_QPSK:
			if(freq > 11700)
			{
				channel->front_param.frequency = (freq - 10600)*1000;
				channel->tone = 1;
			} else {
				channel->front_param.frequency = (freq - 9750)*1000;
				channel->tone = 0;
			}
			channel->front_param.inversion = INVERSION_OFF;
	  
			/* find out the polarisation */ 
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->pol = (field[0] == 'h' ? 0 : 1);

			/* satellite number */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->sat_no = strtoul(field, NULL, 0);

			/* symbol rate */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.qpsk.symbol_rate = strtoul(field, NULL, 0) * 1000;

			channel->front_param.u.qpsk.fec_inner = FEC_AUTO;
		break;
		case FE_QAM:
			channel->front_param.frequency = freq;
			
			/* find out the inversion */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.inversion = find_param(inversion_list, field);

			/* find out the symbol rate */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.qam.symbol_rate = strtoul(field, NULL, 0);

			/* find out the fec */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.qam.fec_inner = find_param(fec_list, field);

			/* find out the qam */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.qam.modulation = find_param(qam_list, field);
		break;
		case FE_OFDM:
			channel->front_param.frequency = freq;

			/* find out the inversion */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.inversion = find_param(inversion_list, field);

			/* find out the bandwidth */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.bandwidth = find_param(bw_list, field);

			/* find out the fec_hp */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.code_rate_HP = find_param(fec_list, field);

			/* find out the fec_lp */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.code_rate_LP = find_param(fec_list, field);

			/* find out the qam */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.constellation = find_param(qam_list, field);

			/* find out the transmission mode */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.transmission_mode = find_param(transmissionmode_list, field);

			/* guard list */
			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.guard_interval = find_param(guard_list, field);

			if(!(field = strsep(&tmp, ":")))return -1;
			channel->front_param.u.ofdm.hierarchy_information = find_param(hierarchy_list, field);
		break;
	}

   /* Video PID - not used but we'll take it anyway */
    if (!(field = strsep(&tmp, ":")))
        return -1;
    channel->pid[VIDFILTER] = strtoul(field, NULL, 0);

    /* Audio PID - it's only for mpegaudio so we don't use it anymore */
    if (!(field = strsep(&tmp, ":")))
        return -1;
    channel->pid[AUDFILTER] = strtoul(field, NULL, 0);

    /* service ID */
    if (!(field = strsep(&tmp, ":")))
        return -1;
    channel->service_id = strtoul(field, NULL, 0);

	return 0;
}

static channel_t *load_channels(dvb_input_plugin_t *this, int *num_ch, fe_type_t fe_type) {

  FILE      *f;
  char       str[BUFSIZE];
  char       filename[BUFSIZE];
  channel_t *channels;
  int        num_channels;
  xine_t *xine = this->class->xine;
  
  snprintf(filename, BUFSIZE, "%s/.xine/channels.conf", xine_get_homedir());

  f = fopen(filename, "rb");
  if (!f) {
    xprintf(xine, XINE_VERBOSITY_LOG, _("input_dvb: failed to open dvb channel file '%s'\n"), filename);
    _x_message(this->stream, XINE_MSG_FILE_NOT_FOUND, filename, "Please run the dvbscan utility.");
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

  if(num_channels > 0) 
    xprintf (xine, XINE_VERBOSITY_DEBUG, "input_dvb: expecting %d channels...\n", num_channels);
  else {
    xprintf (xine, XINE_VERBOSITY_DEBUG, "input_dvb: no channels found in the file: giving up.\n");
    return NULL;
  }

  channels = malloc (sizeof (channel_t) * num_channels);

  /*
   * load channel list 
   */

  f = fopen (filename, "rb");
  num_channels = 0;
  while ( fgets (str, BUFSIZE, f)) {
    if(extract_channel_from_string(&(channels[num_channels]),str,fe_type) < 0)continue;

    num_channels++;
  }

  if(num_channels > 0) 
    xprintf (xine, XINE_VERBOSITY_DEBUG, "input_dvb: found %d channels...\n", num_channels);
  else {
    xprintf (xine, XINE_VERBOSITY_DEBUG, "input_dvb: no channels found in the file: giving up.\n");
    free(channels);
    return NULL;
  }

  *num_ch = num_channels;
  return channels;
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

/* this new tuning algorithm is taken from dvbstream - much faster than the older method */
static int tuner_tune_it (tuner_t *this, struct dvb_frontend_parameters
			  *front_param) {
  fe_status_t status;
  struct dvb_frontend_event event;
  struct pollfd pfd[1]; 

  while(1)  {
    if (ioctl(this->fd_frontend, FE_GET_EVENT, &event) < 0)       /* empty the event queue */
    break;
  }

  if (ioctl(this->fd_frontend, FE_SET_FRONTEND, front_param) <0) {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, "setfront front: %s\n", strerror(errno));
  }

  pfd[0].fd=this->fd_frontend;
  pfd[0].events=POLLPRI;
  event.status=0;

  while (((event.status & FE_TIMEDOUT)==0) && ((event.status & FE_HAS_LOCK)==0)) {
    if (poll(pfd,1,100) > 0){
      if (pfd[0].revents & POLLPRI){
        if ((status = ioctl(this->fd_frontend, FE_GET_EVENT, &event)) < 0){
	  if (errno != EOVERFLOW) {
	    return 0;
	  }
        }
      }
    }
  }
  return 1;
}

/* Parse the PMT, and add filters for all stream types associated with 
 * the 'channel'. We leave it to the demuxer to sort out which PIDs to 
 * use. to simplify things slightly, (and because the demuxer can't handle it)
 * allow only one of each media type */
static void parse_pmt(dvb_input_plugin_t *this, const unsigned char *buf, int section_length)
{
 
  int program_info_len;
  int x;
  int pcr_pid;
  int has_video=0;
  int has_audio=0;
  int has_ac3=0;
  int has_subs=0;
  int has_text=0;

  /* Clear all pids, the pmt will tell us which to use */
  for (x = 0; x < MAXFILTERS; x++)
    this->channels[this->channel].pid[x] = 0;
    
  pcr_pid = ((buf[0] & 0x1f) << 8) | buf[1];

  dvb_set_pidfilter(this, PCRFILTER, pcr_pid, DMX_PES_PCR,DMX_OUT_TS_TAP);

  program_info_len = ((buf[2] & 0x0f) << 8) | buf[3];
  buf += program_info_len + 4;
  section_length -= program_info_len + 4;

  while (section_length >= 5) {
    int elementary_pid = ((buf[1] & 0x1f) << 8) | buf[2];
    int descriptor_len = ((buf[3] & 0x0f) << 8) | buf[4];
    switch (buf[0]) {
      case 0x01:
      case 0x02:
        if(!has_video) {
          printf("input_dvb: Adding VIDEO     : PID 0x%04x\n", elementary_pid);
	  dvb_set_pidfilter(this, VIDFILTER, elementary_pid, DMX_PES_VIDEO,DMX_OUT_TS_TAP);
	  has_video=1;
	} 
	break;
	
      case 0x03:
      case 0x04:
        if(!has_audio) {
	  printf("input_dvb: Adding AUDIO     : PID 0x%04x\n", elementary_pid);
	  dvb_set_pidfilter(this, AUDFILTER, elementary_pid, DMX_PES_AUDIO,DMX_OUT_TS_TAP);
	  has_audio=1;
	}
        break;
        
      case 0x06:
        if (find_descriptor(0x56, buf + 5, descriptor_len, NULL, NULL)) {
	  if(!has_text) {
	     printf("input_dvb: Adding TELETEXT  : PID 0x%04x\n", elementary_pid);
	     dvb_set_pidfilter(this,TXTFILTER, elementary_pid, DMX_PES_OTHER,DMX_OUT_TS_TAP);
             has_text=1;
          } 
	  break;
	} else if (find_descriptor (0x59, buf + 5, descriptor_len, NULL, NULL)) {
           /* Note: The subtitling descriptor can also signal
	    * teletext subtitling, but then the teletext descriptor
	    * will also be present; so we can be quite confident
	    * that we catch DVB subtitling streams only here, w/o
	    * parsing the descriptor. */
	    if(!has_subs) {
              printf("input_dvb: Adding SUBTITLES: PID 0x%04x\n", elementary_pid);
	      dvb_set_pidfilter(this, SUBFILTER, elementary_pid, DMX_PES_OTHER,DMX_OUT_TS_TAP);
              has_subs=1;
            }
	    break;
        } else if (find_descriptor (0x6a, buf + 5, descriptor_len, NULL, NULL)) {
            if(!has_ac3) {
 	      dvb_set_pidfilter(this, AC3FILTER, elementary_pid, DMX_PES_OTHER,DMX_OUT_TS_TAP);
              printf("input_dvb: Adding AC3       : PID 0x%04x\n", elementary_pid);
              has_ac3=1;
        }
	break;
	}
      break;
      };

    buf += descriptor_len + 5;
    section_length -= descriptor_len + 5;
  };
}

static void dvb_parse_si(dvb_input_plugin_t *this) {

  char *tmpbuffer;
  char *bufptr;
  int 	service_id;
  int	result;
  int  	section_len;
  int 	x;
  tuner_t *tuner = this->tuner;
  tmpbuffer = malloc (8192);
  bufptr = tmpbuffer;

  /* first - the PAT */  
  dvb_set_pidfilter (this, INTERNAL_FILTER, 0, DMX_PES_OTHER, DMX_OUT_TAP);
  result = read (tuner->fd_pidfilter[INTERNAL_FILTER], tmpbuffer, 3);
  if(result!=3)
    printf("error\n");
  section_len = getbits(tmpbuffer,12,12);
  result = read (tuner->fd_pidfilter[INTERNAL_FILTER], tmpbuffer+3,section_len);
  if(result!=section_len)
    printf("input_dvb: error reading in the PAT table\n");

  ioctl(tuner->fd_pidfilter[INTERNAL_FILTER], DMX_STOP);
  
  bufptr+=13;
  this->num_streams_in_this_ts=-1;
  
  while(section_len>0){
    service_id = getbits (bufptr,0,16);
    for (x=0;x<this->num_channels;x++){
      if(this->channels[x].service_id==service_id) {
        this->channels[x].pmtpid = getbits (bufptr, 19, 13);
      }
    }
    if(getbits(bufptr, 19,13)==0x1FFF)
      break;
    section_len-=4;
    bufptr+=4;
    this->num_streams_in_this_ts++;        
  }
  printf("3");
  bufptr = tmpbuffer;
    /* next - the PMT */
  dvb_set_pidfilter(this, INTERNAL_FILTER, this->channels[this->channel].pmtpid , DMX_PES_OTHER, DMX_OUT_TAP);
  result = read(tuner->fd_pidfilter[INTERNAL_FILTER],tmpbuffer,3);

  section_len = getbits (bufptr, 12, 12);
  result = read(tuner->fd_pidfilter[INTERNAL_FILTER],tmpbuffer+3,section_len);

  ioctl(tuner->fd_pidfilter[INTERNAL_FILTER], DMX_STOP);

  parse_pmt(this,tmpbuffer+9,section_len);
  
  dvb_set_pidfilter(this, PATFILTER, 0, DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, PMTFILTER, this->channels[this->channel].pmtpid, DMX_PES_OTHER,DMX_OUT_TS_TAP);
/*
  dvb_set_pidfilter(this, TSDTFILTER, 0x02,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, RSTFILTER, 0x13,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, TDTFILTER, 0x14,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, DITFILTER, 0x1e,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, CATFILTER, 0x01,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, NITFILTER, 0x10,DMX_PES_OTHER,DMX_OUT_TS_TAP);
  dvb_set_pidfilter(this, SDTFILTER, 0x11,DMX_PES_OTHER,DMX_OUT_TS_TAP);
*/
  /* we use the section filter for EIT because we are guarenteed a complete section */
  if(ioctl(tuner->fd_pidfilter[EITFILTER],DMX_SET_BUFFER_SIZE,8192*this->num_streams_in_this_ts)<0)
    printf("input_dvb: couldn't increase buffer size for EIT: %s \n",strerror(errno)); 
  dvb_set_sectfilter(this, EITFILTER, 0x12,DMX_PES_OTHER,0x4e, 0xff); 
  free(tmpbuffer);
}


/* this function parses the EIT table.  It needs to be called at least twice - once for 
   current program, once for next */
static void do_eit(dvb_input_plugin_t *this)
{

  int table_id;
  int descriptor_id;
  int section_len=0;
  unsigned int service_id=-1;
  int n,y,x;
  char *eit=NULL;
  char *foo=NULL;
  int text_len;
  int current_next=0;
  int running_status=0;
  struct pollfd fd;
  foo=malloc(8192);
  memset(foo,0,8192);
  char *buffer;
  int loops;
  int current_channel;    
  fd.fd = this->tuner->fd_pidfilter[EITFILTER];
  fd.events = POLLPRI;

  /* reset all pointers to NULL - if there's info available, we'll find it */
  for(x=0;x<this->num_channels;x++){
    this->channels[x].eit[0].progname=NULL;
    this->channels[x].eit[0].description=NULL;
    this->channels[x].eit[0].starttime=NULL;
    this->channels[x].eit[0].duration=NULL;
    this->channels[x].eit[0].content=NULL;
    this->channels[x].eit[1].progname=NULL;
    this->channels[x].eit[1].description=NULL;
    this->channels[x].eit[1].starttime=NULL;
    this->channels[x].eit[1].duration=NULL;
    this->channels[x].eit[1].content=NULL;
  }
  
  /* we assume that because we are displaying the channel, service_id for this channel
     must be known.  We should be accepting all information into the various channel
     structs, with a time value so we only really check once every five minutes or so
     per TS. Each section delivered by the driver contains info about 1 service. so we
     may need to read it multiple times to retrieve the one we're after. */

  for(loops=0;loops<=this->num_streams_in_this_ts*2;loops++){
    eit=foo;
    if (poll(&fd,1,2000)<0) {
     printf("(TImeout in EPG loop!! Quitting\n");
       return;  
    }
    current_channel=-1;
    n = read (this->tuner->fd_pidfilter[EITFILTER],eit,3);
    table_id=getbits(eit,0,8);
    section_len=(unsigned int)getbits(eit,12,12);
    n = read (this->tuner->fd_pidfilter[EITFILTER],eit+3,section_len);

    service_id=(unsigned int)getbits(eit, 24, 16);
    for (n=0;n<this->num_channels;n++)
      if(this->channels[n].service_id==service_id) 
        current_channel=n;
        
    current_next=getbits(foo,47,1);
  
    if(section_len>15){
    /* do we have information about the current program, or the next? */

      if(getbits(foo,192,3) > 2){
        running_status=0; /* not currently running - must be next */
      } else {
        running_status=1; /* currently running */
      }

      /* allocate an area in the eit buffer & clear it */
      buffer=this->eitbuffer+(current_channel*2048);

      y=running_status*1024;
      this->channels[current_channel].eit[running_status].progname=buffer+y;
      this->channels[current_channel].eit[running_status].description=buffer+y+256;
      this->channels[current_channel].eit[running_status].starttime=buffer+y+768;
      this->channels[current_channel].eit[running_status].duration=buffer+y+812;
      this->channels[current_channel].eit[running_status].content=buffer+y+900;
      memset(buffer+y,0,1024);

      /* gather up the start time and duration for this program unless duration is 0.
         no point in cluttering up the OSD... */
      if(bcdtoint(eit[21])+bcdtoint(eit[22])+bcdtoint(eit[23])>0){
          strftime(this->channels[current_channel].eit[running_status].starttime,21,"%a %l:%M%p",dvb_mjdtime(eit+16));
          snprintf(this->channels[current_channel].eit[running_status].duration,21,"%ihr%imin",(char)bcdtoint(eit[21] & 0xff),(char)bcdtoint(eit[22] & 0xff));
      }
      descriptor_id=eit[26];
      eit+=27;	
      section_len-=27;
      /* run the descriptor loop for the length of section_len */
      while (section_len>1)
      {
        switch(descriptor_id)
        {
          case 0x4A: /* linkage descriptor */
            break; 
          case 0x4D: { /* simple program info descriptor */
              int name_len;
              int desc_len;
              desc_len=getbits(eit,0,8);
              /*  printf("LANG Code : %C%C%C\n",eit[1],eit[2],eit[3]); */
              /* program name */
              name_len=(unsigned char)eit[4];
              memcpy(this->channels[current_channel].eit[running_status].progname,eit+5,name_len);
              /* detailed program information (max 256 chars)*/      
              text_len=(unsigned char)eit[5+name_len];
              memcpy(this->channels[current_channel].eit[running_status].description,eit+6+name_len,text_len);
            }
            break;
          case 0x4E: {
             /* extended descriptor - not currently used as the simple descriptor gives us enough for now
                and the only broadcaster in my locale using it sends an empty one...  */
             /*
              int item_desc_len;
              int total_item_len;
              int y;
              int desc_len;
      
              desc_len=getbits(eit,0,8);
        
              printf("Descriptornum: %i\n",getbits(eit,8,4));
              printf("LastDescNum: %i\n",getbits(eit,12,4));
              printf("Language: %C%C%C\n",(char)getbits(eit,16,8),(char)getbits(eit,24,8),(char)getbits(eit,32,8));
      
              total_item_len = getbits(eit,40,8);
              item_desc_len = getbits(eit,48,8);
            
              if(item_desc_len==1)
                printf("-");
              else
                for(y=0;y<item_desc_len;y++)
                  printf("%c",eit[y]);
              printf("\n");
              */  
            }
            break;
          case 0x4F: /* timeshifted event not used */
            break;
          case 0x50:{  /* video content descriptor nothing here we can reliably use */
              /* if(getbits(eit,12,4)==1)
                  printf("Video Content - flags are %X\n",getbits(eit,16,8)); */
            }
            break;
          case 0x53: /* CA descriptor */
            break;
          case 0x54: {  /* Content Descriptor, riveting stuff */
              int content_bits=getbits(eit,8,4);
              char *content[]={"UNKNOWN","MOVIE","NEWS","ENTERTAINMENT","SPORT","CHILDRENS","MUSIC","ARTS/CULTURE","CURRENT AFFAIRS","EDUCATIONAL","INFOTAINMENT","SPECIAL","COMEDY","DRAMA","DOCUMENTARY","UNK"};
              snprintf(this->channels[current_channel].eit[running_status].content,40,content[content_bits]);
            }
            break;
          case 0x55: {  /* Parental Rating descriptor describes minimum recommened age -3 */ 
             /*
              printf("descriptor Len: %i\n",getbits(eit,0,8));
              printf("Country Code: %C%C%C\n",eit[1],eit[2],eit[3]);
             */
             this->channels[current_channel].eit[running_status].rating=eit[4]+3;
            }
            break; 
          case 0x57: /* telephone descriptor not used */
            break;
          case 0x5E: /* multilingual component descriptor - we should be using this... */
            break;
          case 0x5F: /* private data specifier */
          case 0x61: /* short smoothing buffer should use this to reduce buffers when avail */
          case 0x64: /* data broadcast descriptor */
          case 0x69: /* PDC descriptor?? */
          case 0x75: /* TVA_ID descriptor */
          case 0x76: /* content identifier */
          default:
            break;
        }

        section_len-=getbits(eit,0,8)+2;
        eit+=getbits(eit,0,8);
        descriptor_id=eit[1];
        eit+=2;
      }
    }
  }
  free(foo); 
}

static void show_eit(dvb_input_plugin_t *this) {

  char *line;
  char *description;
  int x,y,ok;

  if(this->displaying==0){
    this->displaying=1;

    line=malloc(512);
  
    this->stream->osd_renderer->clear(this->proginfo_osd);
    /* Channel Name */       
    this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 32);
    this->stream->osd_renderer->render_text (this->proginfo_osd,350,150,"Searching for Info",OSD_TEXT4);
    this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 24);

    this->stream->osd_renderer->show (this->background, 0);
    this->stream->osd_renderer->show_unscaled (this->proginfo_osd, 0);

    do_eit(this); 

    this->stream->osd_renderer->hide (this->proginfo_osd, 0);

    this->stream->osd_renderer->clear(this->proginfo_osd);
    /* Channel Name */       
    this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 32);
    this->stream->osd_renderer->render_text (this->proginfo_osd,15,10,this->channels[this->channel].name,OSD_TEXT4);
    this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 24);

    this->stream->osd_renderer->render_text (this->proginfo_osd, 15, 50, "NOW:",OSD_TEXT3);
    this->stream->osd_renderer->render_text (this->proginfo_osd, 15, 300, "NEXT:",OSD_TEXT3);
    
    if(this->channels[this->channel].eit[0].progname!=NULL)
    {  
      this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 32);
      /* we trim the program name down to 23 chars so it'll fit although with kerning you just don't know */
      snprintf(line,28,"%s",this->channels[this->channel].eit[0].progname);
      this->stream->osd_renderer->render_text (this->proginfo_osd, 100, 46, line,OSD_TEXT4);
      this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 24);

      /*start time and duration */
      y=0;
      if(strlen(this->channels[this->channel].eit[0].starttime)>3){
        snprintf(line,100,"%s(%s)",this->channels[this->channel].eit[0].starttime, this->channels[this->channel].eit[0].duration);
        if(strlen(this->channels[this->channel].eit[0].content)>3)
          y=15; /* offset vertically */
        this->stream->osd_renderer->render_text (this->proginfo_osd, 670, 50+y, line,OSD_TEXT3);
      }
      /*Content Type and Rating if any*/
      if(strlen(this->channels[this->channel].eit[0].content)>3){
        snprintf(line,100,"%s(%i+)",this->channels[this->channel].eit[0].content, this->channels[this->channel].eit[0].rating);
        this->stream->osd_renderer->render_text (this->proginfo_osd, 670, 20+y, line,OSD_TEXT3);
      }
      /* some quick'n'dirty formatting to keep words whole */  
      ok=0;  y=0;
      description=this->channels[this->channel].eit[0].description;
      while(ok<250){
        x=65;
        while(getbits(description,x*8,8)>' ' && getbits(description,x*8,8)<254){
          x--; 
        }
        x++;
        ok+=x;	
        snprintf(line,x,"%s",description);
        this->stream->osd_renderer->render_text (this->proginfo_osd, 55, 110+(y),line,OSD_TEXT3);
        description+=x;
        y+=30;    
      }
    } else {
      this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 32);
      /* we trim the program name down to 23 chars so it'll fit although with kerning you just don't know */
      snprintf(line,28,"%s","No Information Available");
      this->stream->osd_renderer->render_text (this->proginfo_osd, 100, 46, line,OSD_TEXT4);
      this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 24);
    }

    if(this->channels[this->channel].eit[1].progname!=NULL)
    {  
      /* and now the next program */ 
      snprintf(line,28,"%s",this->channels[this->channel].eit[1].progname);
      this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 32);
      this->stream->osd_renderer->render_text (this->proginfo_osd, 100, 296, line,OSD_TEXT4);
      this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 24);

      y=0;	
      if(strlen(this->channels[this->channel].eit[1].starttime)>3){
        snprintf(line,100,"%s(%s)",this->channels[this->channel].eit[1].starttime, this->channels[this->channel].eit[1].duration);
        if(strlen(this->channels[this->channel].eit[1].content)>3)
          y=15; /* offset vertically */
        this->stream->osd_renderer->render_text (this->proginfo_osd, 670, 300+y, line,OSD_TEXT3);
      }
      /*Content Type and Rating if any*/
      if(strlen(this->channels[this->channel].eit[1].content)>3){
        snprintf(line,100,"%s(%i+)",this->channels[this->channel].eit[1].content, this->channels[this->channel].eit[1].rating);
        this->stream->osd_renderer->render_text (this->proginfo_osd, 670, 270+y, line,OSD_TEXT3);
      }
      /* some quick'n'dirty formatting to keep words whole */  
      ok=0;  y=0;
      description=this->channels[this->channel].eit[1].description;
      while( ok < 250 ) {
        x=65;
        while(getbits(description,x*8,8)>' ' && getbits(description,x*8,8)<254){
          x--; 
        }
        x++;
        ok+=x;
        snprintf(line,x,"%s",description);
        this->stream->osd_renderer->render_text (this->proginfo_osd, 55, 360+(y),line,OSD_TEXT3);
        description+=x;
        y+=30;    
      }
    } else {
      this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 32);
      /* we trim the program name down to 23 chars so it'll fit although with kerning you just don't know */
      snprintf(line,28,"%s","No Information Available");
      this->stream->osd_renderer->render_text (this->proginfo_osd, 100, 296, line,OSD_TEXT4);
      this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 24);
    }

    this->stream->osd_renderer->show(this->background,0);
    this->stream->osd_renderer->show_unscaled (this->proginfo_osd, 0);
/*      
    vpts=this->stream->xine->clock->get_current_time(this->stream->xine->clock);
    vpts+=1080000; 
    this->stream->osd_renderer->hide (this->proginfo_osd,vpts);
    this->stream->osd_renderer->hide (this->background,vpts);
*/
    free(line);
  } else {
    this->displaying=0;
    this->stream->osd_renderer->hide (this->proginfo_osd,0);
    this->stream->osd_renderer->hide (this->background,0);
  }
  
  return;
}


static int tuner_set_channel (dvb_input_plugin_t *this, 
			      channel_t *c) {
  tuner_t *tuner=this->tuner;

  if (tuner->feinfo.type==FE_QPSK) {
    if (!tuner_set_diseqc(tuner, c))
      return 0;
  }

  if (!tuner_tune_it (tuner, &c->front_param))
    return 0;

  /* now read the pat,find all accociated PIDs and add them to the stream */
  dvb_parse_si(this);
  
  return 1; /* fixme: error handling */
}


static void osd_show_channel (dvb_input_plugin_t *this) {

  int i, channel ;

  this->stream->osd_renderer->filled_rect (this->channel_osd, 0, 0, 395, 400, 2);

  channel = this->channel - 5;

  for (i=0; i<11; i++) {

    if ( (channel >= 0) && (channel < this->num_channels) )
      this->stream->osd_renderer->render_text (this->channel_osd, 110, 10+i*35,
					     this->channels[channel].name,
					     OSD_TEXT3);
    channel ++;
  }

  this->stream->osd_renderer->line (this->channel_osd, 105, 183, 390, 183, 10);
  this->stream->osd_renderer->line (this->channel_osd, 105, 183, 105, 219, 10);
  this->stream->osd_renderer->line (this->channel_osd, 105, 219, 390, 219, 10);
  this->stream->osd_renderer->line (this->channel_osd, 390, 183, 390, 219, 10);

  this->stream->osd_renderer->show (this->channel_osd, 0);

}


static void switch_channel (dvb_input_plugin_t *this) {

  xine_event_t     event;
  xine_pids_data_t data;
  xine_ui_data_t   ui_data;

  _x_demux_flush_engine(this->stream); 
  pthread_mutex_lock (&this->mutex);
  
  close (this->fd);
  if (!tuner_set_channel (this, &this->channels[this->channel])) {
    xprintf (this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: tuner_set_channel failed\n"));
    pthread_mutex_unlock (&this->mutex);
    return;
  }

  event.type = XINE_EVENT_PIDS_CHANGE;
  data.vpid = this->channels[this->channel].pid[VIDFILTER];
  data.apid = this->channels[this->channel].pid[AUDFILTER];
  event.data = &data;
  event.data_length = sizeof (xine_pids_data_t);

  xprintf (this->class->xine, XINE_VERBOSITY_DEBUG, "input_dvb: sending event\n");

  xine_event_send (this->stream, &event);

  snprintf (ui_data.str, 256, "%04d - %s", this->channel, 
      	    this->channels[this->channel].name);
  ui_data.str_len = strlen (ui_data.str);

  _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, ui_data.str);

  event.type        = XINE_EVENT_UI_SET_TITLE;
  event.stream      = this->stream;
  event.data        = &ui_data;
  event.data_length = sizeof(ui_data);
  xine_event_send(this->stream, &event);

  lprintf ("ui title event sent\n");
  
  this->fd = open (DVR_DEVICE, O_RDONLY);

  pthread_mutex_unlock (&this->mutex);

  this->stream->osd_renderer->hide(this->channel_osd,0);
}


static void do_record (dvb_input_plugin_t *this) {

 struct tm *tma;
 time_t *t; 
 char filename [256];
 char dates[64];
 int x=0;
 xine_cfg_entry_t savedir;
 if (this->record_fd > -1) {

    /* stop recording */
    close (this->record_fd);
    this->record_fd = -1;

    this->stream->osd_renderer->hide (this->rec_osd, 0);
    this->stream->osd_renderer->hide (this->paused_osd, 0);
    this->record_paused=0;
  } else {
    t=malloc(sizeof(time_t));
    time(t);
    tma=localtime(t);
    free(t);
    strftime(dates,63,"%F_%H%M",tma);
    
    if (xine_config_lookup_entry(this->stream->xine, "misc.save_dir", &savedir)){
      if(strlen(savedir.str_value)>1){
        if(opendir(savedir.str_value)==NULL){
          snprintf (filename, 256, "%s/%s_%s.ts",xine_get_homedir(),this->channels[this->channel].name, dates);
          printf("savedir is wrong... saving to home directory\n");
        } else {
          snprintf (filename, 256, "%s/%s_%s.ts",savedir.str_value,this->channels[this->channel].name, dates);
          printf("saving to savedir\n");
        }
      } else {
        snprintf (filename, 256, "%s/%s_%s.ts",xine_get_homedir(),this->channels[this->channel].name, dates);
        printf("Saving to HomeDir\n");
      }
    }
    /* remove spaces from name */
    while((filename[x]!=0) && x<255){
      if(filename[x]==' ') filename[x]='_';
      x++;
    }

    /* start recording */
    this->record_fd = open (filename, O_CREAT | O_APPEND | O_WRONLY, 0644);

    this->stream->osd_renderer->clear (this->rec_osd);
    
    this->stream->osd_renderer->render_text (this->rec_osd, 10, 10, "Recording to:",
					     OSD_TEXT3);

    this->stream->osd_renderer->render_text (this->rec_osd, 160, 10, filename,
					     OSD_TEXT3);

    this->stream->osd_renderer->show_unscaled (this->rec_osd, 0);

  }
}

static void dvb_event_handler (dvb_input_plugin_t *this) {

  xine_event_t *event;

  while ((event = xine_event_get (this->event_queue))) {

    lprintf ("got event %08x\n", event->type);

    if (this->fd<0) {
      xine_event_free (event);
      return;
    }

    switch (event->type) {

    case XINE_EVENT_INPUT_DOWN:
      if (this->channel < (this->num_channels-1))
	this->channel++;
      osd_show_channel (this);
      break;

    case XINE_EVENT_INPUT_UP:
      if (this->channel>0)
	this->channel--;
      osd_show_channel (this);
      break;

    case XINE_EVENT_INPUT_NEXT:
      if (this->channel < (this->num_channels-1)) {
	this->channel++;
	switch_channel (this);
      }
      break;

    case XINE_EVENT_INPUT_PREVIOUS:
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

    case XINE_EVENT_INPUT_MENU2:
      do_record (this);
      break;
    case XINE_EVENT_INPUT_MENU3:
      /* zoom for cropped 4:3 in a 16:9 window */
      if (!this->zoom_ok) {
       this->zoom_ok = 1;
       this->stream->video_out->set_property (this->stream->video_out, VO_PROP_ZOOM_X, 133);
       this->stream->video_out->set_property (this->stream->video_out, VO_PROP_ZOOM_Y, 133);
      } else {
       this->zoom_ok=0;
       this->stream->video_out->set_property (this->stream->video_out, VO_PROP_ZOOM_X, 100);
       this->stream->video_out->set_property (this->stream->video_out, VO_PROP_ZOOM_Y, 100);
      }
      break;
    case XINE_EVENT_INPUT_MENU4:
      /* Pause recording.. */
      if ((this->record_fd>-1) && (!this->record_paused)) {
       this->record_paused = 1;
       this->stream->osd_renderer->render_text (this->paused_osd, 15, 10, "Recording Paused",OSD_TEXT3);
       this->stream->osd_renderer->show_unscaled (this->paused_osd, 0);
      } else {
       this->record_paused=0;
       this->stream->osd_renderer->hide (this->paused_osd, 0);
      }
      break;

    case XINE_EVENT_INPUT_MENU7:
         show_eit(this);
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

  lprintf ("reading %lld bytes...\n", len);

  nbc_check_buffers (this->nbc);

  pthread_mutex_lock( &this->mutex ); /* protect agains channel changes */
  total=0;
  while (total<len){ 
    n = read (this->fd, &buf[total], len-total);

    lprintf ("got %lld bytes (%lld/%lld bytes read)\n", n,total,len);
  
    if (n > 0){
      this->curpos += n;
      total += n;
    } else if (n<0 && errno!=EAGAIN) {
      pthread_mutex_unlock( &this->mutex );
      return total;
    }
  }

  if ((this->record_fd)&&(!this->record_paused))
    write (this->record_fd, buf, total);

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

  lprintf ("seek %lld bytes, origin %d\n", offset, origin);

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
  return 0; /* INPUT_CAP_CHAPTERS */ /* where did INPUT_CAP_AUTOPLAY go ?!? */
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

  if(this->mrl)
    free (this->mrl);
  
  if (this->channels)
    free (this->channels);
    
  if(this->eitbuffer)
    free (this->eitbuffer);
    
  if (this->tuner)
    tuner_dispose (this->tuner);
  
      this->stream->osd_renderer->hide (this->proginfo_osd,0);
    this->stream->osd_renderer->hide (this->background,0);

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

/* allow center cutout zoom for dvb content */
static void
dvb_zoom_cb (void *this_gen, xine_cfg_entry_t *cfg)
{
  dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;

  this->zoom_ok = cfg->num_value;

  if (!this)
    return;

  if (this->zoom_ok) {
    this->stream->video_out->set_property (this->stream->video_out, VO_PROP_ZOOM_X, 133);
    this->stream->video_out->set_property (this->stream->video_out, VO_PROP_ZOOM_Y, 133);
  } else {
    this->stream->video_out->set_property (this->stream->video_out, VO_PROP_ZOOM_X, 100);
    this->stream->video_out->set_property (this->stream->video_out, VO_PROP_ZOOM_Y, 100);
  }
}


static int dvb_plugin_open(input_plugin_t * this_gen)
{
    dvb_input_plugin_t *this = (dvb_input_plugin_t *) this_gen;
    tuner_t *tuner;
    channel_t *channels;
    int num_channels;
    char str[256];
    char *ptr;
    xine_cfg_entry_t zoomdvb;
    config_values_t *config = this->stream->xine->config;
    xine_cfg_entry_t lastchannel;

    if (!(tuner = tuner_init(this->class->xine))) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: cannot open dvb device\n"));
      return 0;
    }

    if (strncasecmp(this->mrl, "dvb://", 6) == 0) {
     /*
      * This is either dvb://<number>
      * or the "magic" dvb://<channel name>
      * We load the channels from ~/.xine/channels.conf
      * and assume that its format is valid for our tuner type
      */

      if (!(channels = load_channels(this, &num_channels, tuner->feinfo.type))) 
      {
        /* failed to load the channels */
	 tuner_dispose(tuner);
	 return 0;
      }

      if (sscanf(this->mrl, "dvb://%d", &this->channel) == 1) 
      {
        /* dvb://<number> format: load channels from ~/.xine/channels.conf */
	if (this->channel >= num_channels) {
          xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	            _("input_dvb: channel %d out of range, defaulting to 0\n"),
		    this->channel);
          this->channel = 0;
	}
      } else {
        /* dvb://<channel name> format ? */
        char *channame = this->mrl + 6;
	if (*channame) {
	  /* try to find the specified channel */
	  int idx = 0;
	  xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	          _("input_dvb: searching for channel %s\n"), channame);

	  while (idx < num_channels) {
	    if (strcasecmp(channels[idx].name, channame) == 0)
	      break;
            idx++;
          }

	 if (idx < num_channels) {
	   this->channel = idx;
         } else {
           /*
            * try a partial match too
	    * be smart and compare starting from the first char, then from 
	    * the second etc..
	    * Yes, this is expensive, but it happens really often
	    * that the channels have really ugly names, sometimes prefixed
	    * by numbers...
	    */
	    int chanlen = strlen(channame);
	    int offset = 0;

	    xprintf(this->class->xine, XINE_VERBOSITY_LOG,
		     _("input_dvb: exact match for %s not found: trying partial matches\n"), channame);

            do {
	      idx = 0;
	      while (idx < num_channels) {
	        if (strlen(channels[idx].name) > offset) {
		  if (strncasecmp(channels[idx].name + offset, channame, chanlen) == 0) {
                     xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: found matching channel %s\n"), channels[idx].name);
                     break;			  
                  }
		}
		idx++;
              }
	      offset++;
	      printf("%d,%d,%d\n", offset, idx, num_channels);
            }
            while ((offset < 6) && (idx == num_channels));
              if (idx < num_channels) {
                this->channel = idx;
              } else {
                xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: channel %s not found in channels.conf, defaulting to channel 0\n"), channame);
                this->channel = 0;
              }
            }
	  } else {
	    /* just default to channel 0 */
	    xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: invalid channel specification, defaulting to channel 0\n"));
            this->channel = 0;
	  }
        }

    } else if (strncasecmp(this->mrl, "dvbs://", 7) == 0) {
	/*
	 * This is dvbs://<channel name>:<qpsk tuning parameters>
	 */
	if (tuner->feinfo.type != FE_QPSK) {
	  xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: dvbs mrl specified but the tuner doesn't appear to be QPSK (DVB-S)\n"));
	  tuner_dispose(tuner);
	  return 0;
	}
	ptr = this->mrl;
	ptr += 7;
	channels = malloc(sizeof(channel_t));
	if (extract_channel_from_string(channels, ptr, tuner->feinfo.type) < 0) {
          free(channels);
	  tuner_dispose(tuner);
	  return 0;
	}
	this->channel = 0;
    } else if (strncasecmp(this->mrl, "dvbt://", 7) == 0) {
	/*
	 * This is dvbt://<channel name>:<ofdm tuning parameters>
	 */
	 if (tuner->feinfo.type != FE_OFDM) {
	   xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("input_dvb: dvbt mrl specified but the tuner doesn't appear to be OFDM (DVB-T)\n"));
	   tuner_dispose(tuner);
	   return 0;
         }
	   ptr = this->mrl;
	   ptr += 7;
	   channels = malloc(sizeof(channel_t));
	   if (extract_channel_from_string(channels, ptr, tuner->feinfo.type) < 0) {
              free(channels);
              tuner_dispose(tuner);
              return 0;
	   }
	   this->channel = 0;
    } else if (strncasecmp(this->mrl, "dvbc://", 7) == 0) 
    {
      /*
       * This is dvbc://<channel name>:<qam tuning parameters>
       */
       if (tuner->feinfo.type != FE_QAM) 
       {
         xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	 _("input_dvb: dvbc mrl specified but the tuner doesn't appear to be QAM (DVB-C)\n"));
         tuner_dispose(tuner);
         return 0;
      }
      ptr = this->mrl;
      ptr += 7;
      channels = malloc(sizeof(channel_t));
      if (extract_channel_from_string(channels, ptr, tuner->feinfo.type) < 0)
      {
        free(channels);
        tuner_dispose(tuner);
        return 0;
      }
      this->channel = 0;
    }else {
	   /* not our mrl */
	   tuner_dispose(tuner);
	   return 0;
    }

    this->tuner = tuner;
    this->channels = channels;
    this->num_channels = num_channels;

    if (!tuner_set_channel(this, &this->channels[this->channel])) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
	   _("input_dvb: tuner_set_channel failed\n"));
      tuner_dispose(this->tuner);
      free(this->channels);
      return 0;
    }

    if ((this->fd = open(DVR_DEVICE, O_RDONLY)) < 0) {
      xprintf(this->class->xine, XINE_VERBOSITY_LOG,
             _("input_dvb: cannot open dvr device '%s'\n"), DVR_DEVICE);
      tuner_dispose(this->tuner);
      free(this->channels);
      return 0;
    }
    if(ioctl(this->fd,DMX_SET_BUFFER_SIZE,262144)<0)
      printf("input_dvb: couldn't increase buffer size for DVR: %s \n",strerror(errno)); 

    this->curpos = 0;
    this->osd = NULL;

    pthread_mutex_init(&this->mutex, NULL);

    this->event_queue = xine_event_new_queue(this->stream);

    this->eitbuffer=malloc(2048*this->num_channels);

    /*
     * this osd is used to draw the "recording" sign
     */
    this->rec_osd = this->stream->osd_renderer->new_object(this->stream->osd_renderer, 900, 61);
    this->stream->osd_renderer->set_position(this->rec_osd, 20, 10);
    this->stream->osd_renderer->set_font(this->rec_osd, "cetus", 26);
    this->stream->osd_renderer->set_encoding(this->rec_osd, NULL);
    this->stream->osd_renderer->set_text_palette(this->rec_osd, XINE_TEXTPALETTE_YELLOW_BLACK_TRANSPARENT, OSD_TEXT3);

    /*
     * this osd is used to draw the channel switching OSD
     */
    this->channel_osd = this->stream->osd_renderer->new_object(this->stream->osd_renderer, 900, 600);
    this->stream->osd_renderer->set_position(this->channel_osd, 20, 10);
    this->stream->osd_renderer->set_font(this->channel_osd, "cetus", 26);
    this->stream->osd_renderer->set_encoding(this->channel_osd, NULL);
    this->stream->osd_renderer->set_text_palette(this->channel_osd, XINE_TEXTPALETTE_WHITE_NONE_TRANSLUCID, OSD_TEXT3);

    /* 
     * this osd is for displaying currently shown channel name 
     */
    this->name_osd = this->stream->osd_renderer->new_object(this->stream->osd_renderer, 301, 61);
    this->stream->osd_renderer->set_position(this->name_osd, 20, 10);
    this->stream->osd_renderer->set_font(this->name_osd, "cetus", 40);
    this->stream->osd_renderer->set_encoding(this->name_osd, NULL);
    this->stream->osd_renderer->set_text_palette(this->name_osd, XINE_TEXTPALETTE_YELLOW_BLACK_TRANSPARENT, OSD_TEXT3);

    /* 
     * this osd is for displaying Recording Paused 
     */
    this->paused_osd = this->stream->osd_renderer->new_object(this->stream->osd_renderer, 301, 161);
    this->stream->osd_renderer->set_position(this->paused_osd, 10, 50);
    this->stream->osd_renderer->set_font(this->paused_osd, "cetus", 40);
    this->stream->osd_renderer->set_encoding(this->paused_osd, NULL);
    this->stream->osd_renderer->set_text_palette(this->paused_osd, XINE_TEXTPALETTE_YELLOW_BLACK_TRANSPARENT, OSD_TEXT3);

    /* 
     * this osd is for displaying Program Information (EIT) 
     */
    this->proginfo_osd = this->stream->osd_renderer->new_object(this->stream->osd_renderer, 1000, 600);
    this->stream->osd_renderer->set_position(this->proginfo_osd, 10, 10);
    this->stream->osd_renderer->set_font(this->proginfo_osd, "sans", 24);
    this->stream->osd_renderer->set_encoding(this->proginfo_osd, NULL);
    this->stream->osd_renderer->set_text_palette(this->proginfo_osd, XINE_TEXTPALETTE_WHITE_NONE_TRANSLUCID, OSD_TEXT3);
    this->stream->osd_renderer->set_text_palette(this->proginfo_osd, XINE_TEXTPALETTE_YELLOW_BLACK_TRANSPARENT, OSD_TEXT4);

    this->background = this->stream->osd_renderer->new_object(this->stream->osd_renderer, 1000, 600);
    this->stream->osd_renderer->set_position(this->background, 1, 1);
    this->stream->osd_renderer->set_font(this->background, "cetus", 32);
    this->stream->osd_renderer->set_encoding(this->background, NULL);
    this->stream->osd_renderer->set_text_palette(this->background, XINE_TEXTPALETTE_YELLOW_BLACK_TRANSPARENT, OSD_TEXT3);
    this->stream->osd_renderer->filled_rect(this->background, 1, 1, 1000, 600, 4);
    this->displaying=0;
    /* zoom for 4:3 in a 16:9 window */
    config->register_bool(config, "input.dvbzoom",
				 0,
				 _("use DVB 'center cutout' (zoom)"),
				 _("This will allow fullscreen "
				   "playback of 4:3 content "
				   "transmitted in a 16:9 frame."),
				 0, &dvb_zoom_cb, (void *) this);

    if (xine_config_lookup_entry(this->stream->xine, "input.dvbzoom", &zoomdvb))
      dvb_zoom_cb((input_plugin_t *) this, &zoomdvb);

    if (xine_config_lookup_entry(this->stream->xine, "input.dvb_last_channel_enable", &lastchannel))
      if (lastchannel.num_value){
      /* Remember last watched channel. never show this entry*/
        config->update_num(config, "input.dvb_last_channel_watched", this->channel+1);
      }

    /*
     * init metadata (channel title)
     */
    snprintf(str, 256, "%04d - %s", this->channel, this->channels[this->channel].name);

    _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, str);

    return 1;
}


static input_plugin_t *dvb_class_get_instance (input_class_t *class_gen,
				    xine_stream_t *stream,
				    const char *data) {

  dvb_input_class_t  *class = (dvb_input_class_t *) class_gen;
  dvb_input_plugin_t *this;
  char               *mrl = (char *) data;

  if(strncasecmp (mrl, "dvb://",6))
    if(strncasecmp(mrl,"dvbs://",7))
      if(strncasecmp(mrl,"dvbt://",7))
        if(strncasecmp(mrl,"dvbc://",7))
          return NULL;

  this = (dvb_input_plugin_t *) xine_xmalloc (sizeof(dvb_input_plugin_t));

  this->stream       = stream;
  this->mrl          = strdup(mrl);
  this->class        = class;
  this->tuner        = NULL;
  this->channels     = NULL;
  this->fd           = -1;
  this->nbc          = nbc_init (this->stream);
  this->osd          = NULL;
  this->event_queue  = NULL;
  this->record_fd    = -1;
    
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
  this->input_plugin.input_class       = class_gen;

  return &this->input_plugin;
}

/*
 * dvb input plugin class stuff
 */

static char *dvb_class_get_description (input_class_t *this_gen) {
  return _("DVB (Digital TV) input plugin");
}

static const char *dvb_class_get_identifier (input_class_t *this_gen) {
  return "dvb";
}


static void dvb_class_dispose(input_class_t * this_gen)
{
    dvb_input_class_t *class = (dvb_input_class_t *) this_gen;
    int x;
    
    for(x=0;x<class->numchannels;x++)
       free(class->autoplaylist[x]);

    free(class);
}

static int dvb_class_eject_media (input_class_t *this_gen) {
  return 1;
}


static char **dvb_class_get_autoplay_list(input_class_t * this_gen,
						  int *num_files)
{
    dvb_input_class_t *class = (dvb_input_class_t *) this_gen;
    channel_t *channels;
    FILE *f;
    char *tmpbuffer=malloc(BUFSIZE);
    char *foobuffer=malloc(BUFSIZE);
    char *str=tmpbuffer;
    int num_channels;
    int nlines=0;
    int x=0;
    int default_channel;    
    xine_cfg_entry_t lastchannel_enable;
    xine_cfg_entry_t lastchannel;
    
    
    snprintf(tmpbuffer, BUFSIZE, "%s/.xine/channels.conf", xine_get_homedir());
    
    num_channels = 0;

    f=fopen (tmpbuffer,"rb");
    if(!f){ /* channels.conf not found in .xine */
       class->mrls[0]="Sorry, No channels.conf found";
       class->mrls[1]="Please run the dvbscan utility";
       class->mrls[2]="from the dvb drivers apps package";
       class->mrls[3]="and place the file in ~/.xine/";
       *num_files=4;
       return class->mrls;
    } else {  
      while (fgets(str, BUFSIZE, f)) 
        nlines++;
    }
    fclose (f);
   
    for(x=0;x<nlines;x++){
      if(class->autoplaylist[x])
        free(class->autoplaylist[x]);
      class->autoplaylist[x]=malloc(128);
    }
    snprintf(tmpbuffer, BUFSIZE, "%s/.xine/channels.conf",
		   xine_get_homedir());
    
    if (xine_config_lookup_entry(class->xine, "input.dvb_last_channel_enable", &lastchannel_enable))
      if (lastchannel_enable.num_value){
        num_channels++;
        if (xine_config_lookup_entry(class->xine, "input.dvb_last_channel_watched", &lastchannel))
            default_channel = lastchannel.num_value;
      }

    f=fopen (tmpbuffer,"rb");
    channels=malloc(sizeof(channel_t)*(nlines+lastchannel_enable.num_value));
    

    if(f>0)
      while(num_channels < nlines+lastchannel_enable.num_value){
        fgets(str,BUFSIZE,f);
        if (extract_channel_from_string (&(channels[num_channels]), str,  0) < 0)
          continue;
          
        sprintf(foobuffer,"dvb://%s",channels[num_channels].name);
        class->autoplaylist[num_channels]=strdup(foobuffer);
	  num_channels++;
      }

    if (lastchannel_enable.num_value){
      if (lastchannel.num_value>-1) /* plugin has been used before - channel is valid */
       sprintf(foobuffer,"dvb://%s",channels[lastchannel.num_value].name);
      else 			    /* set a reasonable default - the first channel */
       sprintf(foobuffer,"dvb://%s",channels[lastchannel_enable.num_value].name);
      class->autoplaylist[0]=strdup(foobuffer);
    }

    free(tmpbuffer);
    free(foobuffer);
    free(channels);
    fclose(f);

    *num_files = num_channels; 
    class->numchannels=nlines;        

   return class->autoplaylist;
}

static void *init_class (xine_t *xine, void *data) {

  dvb_input_class_t  *this;
  config_values_t *config = xine->config;

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
  this->mrls[1] = "dvbs://";
  this->mrls[2] = "dvbc://";
  this->mrls[3] = "dvbt://";
  this->mrls[4] = 0;

  lprintf ("init class succeeded\n");


    /* dislay channel name in top left of display */
   config->register_bool(config, "input.dvbdisplaychan",
				 0,
				 _("display DVB channel name"),
				 _("This will display the current "
				   "channel name in xine's on-screen-display. "
				   "Menu button 7 will disable this temporarily."),
				 0, NULL, NULL);

    /* Enable remembering of last watched channel */
   config->register_bool(config, "input.dvb_last_channel_enable",
				 1,
				 _("Remember last DVB channel watched"),
				 _("On autoplay, xine will remember and "
				   "switch to this channel. "),
				 0, NULL, NULL);


    /* Enable remembering of last watched channel never show this entry*/
   config->register_num(config, "input.dvb_last_channel_watched",
				 -1,
				 _("Remember last DVB channel watched"),
				 _("If enabled, xine will remember and "
				   "switch to this channel. "),
				 11, NULL, NULL);


  return this;
}


/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT, 15, "DVB", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
