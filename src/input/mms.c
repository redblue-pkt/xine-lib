/*
 * Copyright (C) 2002 the xine project
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
 * $Id: mms.c,v 1.20 2003/01/31 14:06:17 miguelfreitas Exp $
 *
 * based on work from major mms
 * utility functions to handle communication with an mms server
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include "xine_internal.h"
#include "xineutils.h"

#include "bswap.h"
#include "mms.h"
#include "../demuxers/asfheader.h"

/*
#define LOG
*/

/* 
 * mms specific types 
 */

#define MMS_PORT 1755

#define BUF_SIZE 102400

#define CMD_HEADER_LEN   48
#define CMD_BODY_LEN   1024


struct mms_s {

  int           s;

  char         *host;
  char         *path;
  char         *file;
  char         *url;

  /* command to send */
  char          scmd[CMD_HEADER_LEN + CMD_BODY_LEN];
  char         *scmd_body; /* pointer to &scmd[CMD_HEADER_LEN] */
  int           scmd_len; /* num bytes written in header */

  char          str[1024]; /* scratch buffer to built strings */

  /* receive buffer */
  uint8_t       buf[BUF_SIZE];
  int           buf_size;
  int           buf_read;

  uint8_t       asf_header[8192];
  uint32_t      asf_header_len;
  uint32_t      asf_header_read;
  int           seq_num;
  int           num_stream_ids;
  int           stream_ids[ASF_MAX_NUM_STREAMS];
  int           stream_types[ASF_MAX_NUM_STREAMS];
  int           packet_length;
  uint32_t      file_length;
  char          guid[37];
  uint32_t      bitrates[ASF_MAX_NUM_STREAMS];
  uint32_t      bitrates_pos[ASF_MAX_NUM_STREAMS];

  int           has_audio;
  int           has_video;
};

/* network/socket utility functions */

static ssize_t read_timeout(int fd, void *buf, size_t count) {
  
  ssize_t ret, total;

  total = 0;

  while (total < count) {
  
    fd_set rset;
    struct timeval timeout;

    FD_ZERO (&rset);
    FD_SET  (fd, &rset);
    
    timeout.tv_sec  = 30;
    timeout.tv_usec = 0;
    
    if (select (fd+1, &rset, NULL, NULL, &timeout) <= 0) {
      return -1;
    }
    
    ret=read (fd, ((uint8_t*)buf)+total, count-total);

    if (ret<=0) {
      printf ("mms: read error.\n");
      return ret;
    } else
      total += ret;
  }

#ifdef LOG
  printf ("mms: read completed %d/%d\n", total, count);
#endif

  return total;
}

static int host_connect_attempt(struct in_addr ia, int port) {

  int                s;
  struct sockaddr_in sin;

  s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);  
  if (s == -1) {
    printf ("libmms: socket(): %s\n", strerror(errno));
    return -1;
  }

  /* put socket in non-blocking mode */

  fcntl (s, F_SETFL, fcntl (s, F_GETFL) | O_NONBLOCK);   

  sin.sin_family = AF_INET;	
  sin.sin_addr   = ia;
  sin.sin_port   = htons(port);
  
  if (connect(s, (struct sockaddr *)&sin, sizeof(sin))==-1 
      && errno != EINPROGRESS) {
    printf ("libmms: connect(): %s\n", strerror(errno));
    close(s);
    return -1;
  }	
	
  return s;
}

static int host_connect(const char *host, int port) {

  struct hostent *h;
  int             i, s;
  
  h = gethostbyname(host);
  if (h == NULL) {
    printf ("libmms: unable to resolve '%s'.\n", host);
    return -1;
  }
	
  for (i = 0; h->h_addr_list[i]; i++) {
    struct in_addr ia;

    memcpy (&ia, h->h_addr_list[i], 4);
    s = host_connect_attempt(ia, port);
    if(s != -1)
      return s;
  }
  printf ("libmms: unable to connect to '%s'.\n", host);
  return -1;
}

static void put_32 (mms_t *this, uint32_t value) {

  this->scmd[this->scmd_len    ] = value & 0xff;
  this->scmd[this->scmd_len + 1] = (value  >> 8) & 0xff;
  this->scmd[this->scmd_len + 2] = (value  >> 16) & 0xff;
  this->scmd[this->scmd_len + 3] = (value  >> 24) & 0xff;

  this->scmd_len += 4;
}

static uint32_t get_64 (uint8_t *buffer, int offset) {

  uint64_t ret;

  ret = ((uint64_t)buffer[offset]) |
        ((uint64_t)buffer[offset + 1] << 8) |
        ((uint64_t)buffer[offset + 2] << 16) |
        ((uint64_t)buffer[offset + 2] << 24) |
        ((uint64_t)buffer[offset + 2] << 32) |
        ((uint64_t)buffer[offset + 2] << 40) |
        ((uint64_t)buffer[offset + 2] << 48) |
        ((uint64_t)buffer[offset + 2] << 56);

  return ret;
}

static uint32_t get_32 (uint8_t *buffer, int offset) {

  uint32_t ret;

  ret = buffer[offset] |
        buffer[offset + 1] << 8 |
        buffer[offset + 2] << 16 |
        buffer[offset + 3] << 24 ;

  return ret;
}

static uint16_t get_16 (unsigned char *buffer, int offset) {

  uint16_t ret;

  ret = buffer[offset] |
        buffer[offset + 1] << 8;

  return ret;
}

static int get_guid (unsigned char *buffer, int offset) {
  int i;
  GUID g;
  
  g.v1 = get_32(buffer, offset);
  g.v2 = get_16(buffer, offset + 4);
  g.v3 = get_16(buffer, offset + 6);
  for(i = 0; i < 8; i++) {
    g.v4[i] = buffer[offset + 8 + i];
  }
  
  for (i = 1; i < GUID_END; i++) {
    if (!memcmp(&g, &guids[i].guid, sizeof(GUID))) {
#ifdef LOG
      printf ("libmms: GUID: %s\n", guids[i].name);
#endif
      return i;
    }
  }
  
  printf ("libmms: unknown GUID: 0x%x, 0x%x, 0x%x, "
          "{ 0x%hx, 0x%hx, 0x%hx, 0x%hx, 0x%hx, 0x%hx, 0x%hx, 0x%hx }\n",
          g.v1, g.v2, g.v3,
          g.v4[0], g.v4[1], g.v4[2], g.v4[3], g.v4[4], g.v4[5], g.v4[6], g.v4[7]);
  return GUID_ERROR;
}

static int send_data (int s, char *buf, int len) {
  int total, timeout;

  total = 0; timeout = 30;
  while (total < len){ 
    int n;

    n = write (s, &buf[total], len - total);

#ifdef LOG
    printf ("mms: sending data, %d of %d\n", n, len);
#endif

    if (n > 0)
      total += n;
    else if (n < 0) {
      if ((timeout>0) && ((errno == EAGAIN) || (errno == EINPROGRESS))) {
	sleep (1); timeout--;
      } else
	return -1;
    }
  }
  return total;
}

static int send_command (mms_t *this, int command, uint32_t switches, 
			 uint32_t extra, int length) {
  int        len8;

  len8 = (length + (length % 8)) / 8;

  this->scmd_len = 0;

  put_32 (this, 0x00000001); /* start sequence */
  put_32 (this, 0xB00BFACE); /* #-)) */
  put_32 (this, length + 32);
  put_32 (this, 0x20534d4d); /* protocol type "MMS " */
  put_32 (this, len8 + 4);
  put_32 (this, this->seq_num);
  this->seq_num++;
  put_32 (this, 0x0);        /* unknown */
  put_32 (this, 0x0);
  put_32 (this, len8+2);
  put_32 (this, 0x00030000 | command); /* dir | command */
  put_32 (this, switches);
  put_32 (this, extra);

  /* memcpy (&cmd->buf[48], data, length); */

  if (send_data (this->s, this->scmd, length+48) != (length+48)) {
    printf ("libmms: send error\n");
    return 0;
  }

#ifdef LOG
  {
    int i;
    unsigned char c;

  printf ("\nlibmms: ***************************************************\ncommand sent, %d bytes\n", length + 48);

  printf ("start sequence %08x\n", get_32 (this->scmd,  0));
  printf ("command id     %08x\n", get_32 (this->scmd,  4));
  printf ("length         %8x \n", get_32 (this->scmd,  8));
  printf ("len8           %8x \n", get_32 (this->scmd, 16));
  printf ("sequence #     %08x\n", get_32 (this->scmd, 20));
  printf ("len8  (II)     %8x \n", get_32 (this->scmd, 32));
  printf ("dir | comm     %08x\n", get_32 (this->scmd, 36));
  printf ("switches       %08x\n", get_32 (this->scmd, 40));

  printf ("ascii contents>");
  for (i = 48; i < (length + 48); i += 2) {
    c = this->scmd[i];

    if ((c >= 32) && (c <= 128))
      printf ("%c", c);
    else
      printf (".");
  }
  printf ("\n");

  printf ("libmms: complete hexdump of package follows:\n");
  for (i = 0; i < (length + 48); i++) {
    c = this->scmd[i];

    printf ("%02x", c);

    if ((i % 16) == 15)
      printf ("\nlibmms: ");

    if ((i % 2) == 1)
      printf (" ");

  }
  printf ("\n");
  }
#endif

  return 1;
}

static void string_utf16(char *dest, char *src, int len) {
  int i;

  memset (dest, 0, 1000);

  for (i = 0; i < len; i++) {
    dest[i * 2] = src[i];
    dest[i * 2 + 1] = 0;
  }

  dest[i * 2] = 0;
  dest[i * 2 + 1] = 0;
}

static void print_answer (char *data, int len) {

#ifdef LOG
  int i;

  printf ("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\nanswer received, %d bytes\n", len);

  printf ("start sequence %08x\n", get_32 (data, 0));
  printf ("command id     %08x\n", get_32 (data, 4));
  printf ("length         %8x \n", get_32 (data, 8));
  printf ("len8           %8x \n", get_32 (data, 16));
  printf ("sequence #     %08x\n", get_32 (data, 20));
  printf ("len8  (II)     %8x \n", get_32 (data, 32));
  printf ("dir | comm     %08x\n", get_32 (data, 36));
  printf ("switches       %08x\n", get_32 (data, 40));

  for (i = 48; i < len; i += 2) {
    unsigned char c = data[i];
    
    if ((c >= 32) && (c < 128))
      printf ("%c", c);
    else
      printf (" %02x ", c);
    
  }
  printf ("\n");
#endif

}  

static int get_answer (mms_t *this) {
 
  int   command = 0x1b;

  while (command == 0x1b) {
    int len, length;

    len = read_timeout (this->s, this->buf, 12);
    if (len != 12) {
      printf ("\nalert! eof\n");
      return 0;
    }

    length = get_32 (this->buf, 8);

#ifdef LOG
    printf ("\n\npacket length: %d\n", length);
#endif

    len = read_timeout (this->s, this->buf+12, length+4) ;
    if (len<=0) {
      printf ("alert! eof\n");
      return 0;
    }
    
    len += 12;

    print_answer (this->buf, len);

    command = get_32 (this->buf, 36) & 0xFFFF;

    if (command == 0x1b) 
      send_command (this, 0x1b, 0, 0, 0);
  }

  return 1;
}

static int receive (int s, char *buf, size_t count) {

  ssize_t  len;

  len = read_timeout (s, buf, count);
  if (len < 0) {
    perror ("read error:");
    return 0;
  }

  return len;
}

static int get_header (mms_t *this) {

  unsigned char  pre_header[8];

  this->asf_header_len = 0;

  while (1) {

    if (!receive (this->s, pre_header, 8)) {
      printf ("libmms: pre-header read failed\n");
      return 0;
    }

#ifdef LOG    
    {
      int i;
    for (i = 0; i < 8; i++)
      printf ("libmms: pre_header[%d] = %02x (%d)\n",
	      i, pre_header[i], pre_header[i]);
    }
#endif    

    if (pre_header[4] == 0x02) {
      
      int packet_len;
      
      packet_len = (pre_header[7] << 8 | pre_header[6]) - 8;

#ifdef LOG    
      printf ("libmms: asf header packet detected, len=%d\n",
	      packet_len);
#endif

      if (!receive (this->s, &this->asf_header[this->asf_header_len], packet_len)) {
	printf ("libmms: header data read failed\n");
	return 0;
      }

      this->asf_header_len += packet_len;

      if ( (this->asf_header[this->asf_header_len - 1] == 1) 
	   && (this->asf_header[this->asf_header_len - 2] == 1)) {

#ifdef LOG
	printf ("libmms: get header packet finished\n");
#endif

	return 1;

      } 

    } else {

      int packet_len, command;

      if (!receive (this->s, (char *) &packet_len, 4)) {
	printf ("packet_len read failed\n");
	return 0;
      }
      
      packet_len = get_32 ((char *)&packet_len, 0) + 4;
      
#ifdef LOG    
      printf ("command packet detected, len=%d\n",
	      packet_len);
#endif
      
      if (!receive (this->s, this->buf, packet_len)) {
	printf ("command data read failed\n");
	return 0 ;
      }
      
      command = get_32 (this->buf, 24) & 0xFFFF;
      
#ifdef LOG    
      printf ("command: %02x\n", command);
#endif
      
      if (command == 0x1b) 
	if (!send_command (this, 0x1b, 0, 0, 0))
	  return 0;
      
    }

#ifdef LOG
    printf ("mms: get header packet succ\n");
#endif
  }

  return 1;
}

static void interp_header (mms_t *this) {

  int i;

  this->packet_length = 0;

  /*
   * parse header
   */

  i = 30;
  while (i < this->asf_header_len) {
    
    int guid;
    uint64_t length;

    guid = get_guid(this->asf_header, i);
    i += 16;
        
    length = get_64(this->asf_header, i);
    i += 8;

    switch (guid) {
    
      case GUID_ASF_FILE_PROPERTIES:

        this->packet_length = get_32(this->asf_header, i + 92 - 24);
        this->file_length   = get_32(this->asf_header, i + 40 - 24);
#ifdef LOG    
        printf ("libmms: file object, packet length = %d (%d)\n",
                this->packet_length, get_32(this->asf_header, i + 96 - 24));
#endif
        break;

      case GUID_ASF_STREAM_PROPERTIES:
        {
          uint16_t stream_id;
          int      type;  

          guid = get_guid(this->asf_header, i);
          switch (guid) {
            case GUID_ASF_AUDIO_MEDIA:
              type = ASF_STREAM_TYPE_AUDIO;
              this->has_audio = 1;
              break;
    
            case GUID_ASF_VIDEO_MEDIA:
              type = ASF_STREAM_TYPE_VIDEO;
              this->has_video = 1;
              break;
          
            case GUID_ASF_COMMAND_MEDIA:
              type = ASF_STREAM_TYPE_CONTROL;
              break;
        
            default:
              type = ASF_STREAM_TYPE_UNKNOWN;
          }

          stream_id = get_16(this->asf_header, i + 48);

#ifdef LOG    
          printf ("libmms: stream object, stream id: %d\n", stream_id);
#endif
          this->stream_types[stream_id] = type;
          this->stream_ids[this->num_stream_ids] = stream_id;
          this->num_stream_ids++;
      
        }
        break;

      case GUID_ASF_STREAM_BITRATE_PROPERTIES:
        {
          uint16_t streams = get_16(this->asf_header, i);
          uint16_t stream_id;
          int j;

#ifdef LOG    
          printf ("libmms: stream bitrate properties\n");
#endif

#ifdef LOG    
          printf ("libmms: streams %d\n", streams); 
#endif
          for(j = 0; j < streams; j++) {
            stream_id = get_16(this->asf_header, i + 2 + j * 6);
#ifdef LOG    
            printf ("libmms: stream id %d\n", stream_id); 
#endif
            this->bitrates[stream_id] = get_32(this->asf_header, i + 4 + j * 6);
            this->bitrates_pos[stream_id] = i + 4 + j * 6;
            printf ("libmms: stream id %d, bitrate %d\n", stream_id, 
                    this->bitrates[stream_id]);
          }
        }
        break;
    
      default:
#ifdef LOG    
        printf ("libmms: unknown object\n");
#endif
        break;
    }

#ifdef LOG    
    printf ("libmms: length    : %lld\n", length);
#endif

    if (length > 24) {
      i += length - 24;
    }
  }
}

const static char *const mms_url_s[] = { "MMS://", "MMSU://", "MMST://", NULL };

static int mms_valid_url (char* url, const char *const * mms_url) {
  int i = 0;
  int len;
    
  if(!url )
    return 0;

  while(mms_url[i]) {
    len = strlen(mms_url[i]);
    if(!strncasecmp(url, mms_url[i], len)) {
      return len;
    }
    i++;
  }
  return 0;
} 

char* mms_connect_common(int *s, int *port, char *url, char **host, char **path, char **file) {
  int     proto_len;
  char   *hostend;
  char   *forport;
  char   *_url;
  char   *_host;
    
  if ((proto_len = mms_valid_url(url, mms_url_s)) <= 0) {
    /* 
     * DCB: Why warning if it isn't a mms*:// mrl like ?
     */
    /*  printf ("libmms: invalid url >%s< (should be mms:// - style)\n", url); */
    return NULL;
  }
  
  /* Create a local copy (alloca()'ed), avoid to corrupt the original URL */
  xine_strdupa(_url, &url[proto_len]);
  
  _host = _url;
  
  /* extract hostname */
#ifdef LOG
  printf ("libmms: extracting host name \n");
#endif
  hostend = strchr(_host, '/');
/*
  if ((!hostend) || (strlen(hostend) <= 1)) {
    printf ("libmms: invalid url >%s<, failed to find hostend\n", url);
    return NULL;
  }
*/
  if (!hostend) {
#ifdef LOG
    printf ("libmms: no trailing /\n");
#endif
    hostend = _host + strlen(_host);
  } else {
    *hostend++ = '\0';
  }

  /* Is port specified ? */
  forport = strchr(_host, ':');
  if(forport) {
    *forport++ = '\0';
    *port = atoi(forport);
  }
  
  *host = strdup(_host);
  
  if(path)
    *path = &url[proto_len] + (hostend - _url);
  
  if(file)
    *file = strrchr (url, '/');
  
  /* 
   * try to connect 
   */
#ifdef LOG
  printf("libmms: try to connect to %s on port %d \n", *host, *port);
#endif
  *s = host_connect (*host, *port);
  
  if (*s == -1) {
    printf ("libmms: failed to connect '%s'\n", *host);
    free (*host);
    return NULL;
  }

#ifdef LOG
  printf ("libmms: connected\n");
#endif

  return url;
}

void mms_gen_guid(char guid[]) {
  static char digit[16] = "0123456789ABCDEF";
  int i = 0;

  srand(time(NULL));
  for (i = 0; i < 36; i++) {
    guid[i] = digit[(int) ((16.0*rand())/(RAND_MAX+1.0))];
  }
  guid[8] = '-'; guid[13] = '-'; guid[18] = '-'; guid[23] = '-';
  guid[36] = '\0';
}

static void report_progress (xine_stream_t *stream, int p) {

  xine_event_t             event;
  xine_progress_data_t     prg;

  prg.description = _("Connecting MMS server...");
  prg.percent = p;
  
  event.type = XINE_EVENT_PROGRESS;
  event.data = &prg;
  event.data_length = sizeof (xine_progress_data_t);
  
  xine_event_send (stream, &event);
}

mms_t *mms_connect (xine_stream_t *stream, const char *url_, int bandwidth) {
  mms_t *this;
  char  *url     = NULL;
  char  *url1    = NULL;
  char  *path    = NULL;
  char  *file    = NULL;
  char  *host    = NULL;
  int    port;
  int    i, s;
  int    video_stream = 0;
  int    audio_stream = 0;
  int    max_arate    = 0;
  int    min_vrate    = 0;
  int    min_bw_left  = 0;
  int    stream_id;
  int    bandwitdh_left;
 
  if (!url_)
    return NULL;

  report_progress (stream, 0);

  url = strdup (url_);
  port = MMS_PORT;
  url1 = mms_connect_common(&s, &port, url, &host, &path, &file);

  if(!url1){
    free(url);
    return NULL;
  }
  
  report_progress (stream, 10);

  this = (mms_t*) xine_xmalloc (sizeof (mms_t));

  this->url             = url;
  this->host            = host;
  this->path            = path;
  this->file            = file;
  this->s               = s;
  this->seq_num         = 0;
  this->scmd_body       = &this->scmd[CMD_HEADER_LEN];
  this->asf_header_len  = 0;
  this->asf_header_read = 0;
  this->num_stream_ids  = 0;
  this->packet_length   = 0;
  this->buf_size        = 0;
  this->buf_read        = 0;
  this->has_audio       = 0;
  this->has_video       = 0;
  
#ifdef LOG
  printf ("libmms: url=%s\nlibmms:   host=%s\nlibmms:   "
          "path=%s\nlibmms:   file=%s\n", url, host, path, file);
#endif

  /*
   * let the negotiations begin...
   */

  /* cmd1 */
  
  mms_gen_guid(this->guid);
  sprintf (this->str, "\x1c\x03NSPlayer/7.0.0.1956; {%s}; Host: %s",
    this->guid, this->host);
  string_utf16 (this->scmd_body, this->str, strlen(this->str) + 2);

  if (!send_command (this, 1, 0, 0x0004000b, strlen(this->str) * 2 + 8))
    goto fail;
  
#ifdef LOG
  printf("libmms: before read \n");
#endif

  if (!get_answer (this)) 
    goto fail;

  report_progress (stream, 20);

  /* cmd2 */

  string_utf16 (&this->scmd_body[8], "\002\000\\\\192.168.0.129\\TCP\\1037\0000", 28);
  memset (this->scmd_body, 0, 8);
  send_command (this, 2, 0, 0, 28 * 2 + 8);

  if (!get_answer (this)) 
    goto fail;

  report_progress (stream, 30);

  /* 0x5 */

  string_utf16 (&this->scmd_body[8], path, strlen(path));
  memset (this->scmd_body, 0, 8);
  if (!send_command (this, 5, 0, 0, strlen(path) * 2 + 12))
    goto fail;

  if (!get_answer (this))
    goto fail;

  report_progress (stream, 40);

  /* 0x15 */

  memset (this->scmd_body, 0, 40);
  this->scmd_body[32] = 2;

  if (!send_command (this, 0x15, 1, 0, 40))
    goto fail;

  if (!get_answer (this))
    goto fail;

  this->num_stream_ids = 0;

  if (!get_header (this))
    goto fail;

  interp_header (this);

  report_progress (stream, 50);

  /* 0x33 */
  /* choose the best quality for the audio stream */
  /* i've never seen more than one audio stream */
  for (i = 0; i < this->num_stream_ids; i++) {
    stream_id = this->stream_ids[i];
    switch (this->stream_types[stream_id]) {
      case ASF_STREAM_TYPE_AUDIO:
        if (this->bitrates[stream_id] > max_arate) {
          audio_stream = stream_id;
          max_arate = this->bitrates[stream_id];
        }
        break;
      default:
        break;
    }
  }
  
  /* choose a video stream adapted to the user bandwidth */
  bandwitdh_left = bandwidth - max_arate;
  if (bandwitdh_left < 0) {
    bandwitdh_left = 0;
  }
#ifdef LOG
  printf("libmms: bandwitdh %d, left %d\n", bandwidth, bandwitdh_left);
#endif

  min_bw_left = bandwitdh_left;
  for (i = 0; i < this->num_stream_ids; i++) {
    stream_id = this->stream_ids[i];
    switch (this->stream_types[stream_id]) {
      case ASF_STREAM_TYPE_VIDEO:
        if (((bandwitdh_left - this->bitrates[stream_id]) < min_bw_left) &&
            (bandwitdh_left >= this->bitrates[stream_id])) {
          video_stream = stream_id;
          min_bw_left = bandwitdh_left - this->bitrates[stream_id];
        }
        break;
      default:
        break;
    }
  }  

  /* choose the lower bitrate of */
  if (!video_stream && this->has_video) {
    for (i = 0; i < this->num_stream_ids; i++) {
      stream_id = this->stream_ids[i];
      switch (this->stream_types[stream_id]) {
        case ASF_STREAM_TYPE_VIDEO:
          if ((this->bitrates[stream_id] < min_vrate) ||
              (!min_vrate)) {
            video_stream = stream_id;
            min_vrate = this->bitrates[stream_id];
          }
          break;
        default:
          break;
      }
    }
  }
    
  printf("libmms: audio stream %d, video stream %d\n", audio_stream, video_stream);
  
  memset (this->scmd_body, 0, 40);
  for (i = 1; i < this->num_stream_ids; i++) {
    this->scmd_body [ (i - 1) * 6 + 2 ] = 0xFF;
    this->scmd_body [ (i - 1) * 6 + 3 ] = 0xFF;
    this->scmd_body [ (i - 1) * 6 + 4 ] = this->stream_ids[i] ;
    this->scmd_body [ (i - 1) * 6 + 5 ] = this->stream_ids[i] >> 8;
    if ((this->stream_ids[i] == audio_stream) ||
        (this->stream_ids[i] == video_stream)) {
      this->scmd_body [ (i - 1) * 6 + 6 ] = 0x00;
      this->scmd_body [ (i - 1) * 6 + 7 ] = 0x00;
    } else {
      printf("libmms: disabling stream %d\n", this->stream_ids[i]);
      this->scmd_body [ (i - 1) * 6 + 6 ] = 0x02;
      this->scmd_body [ (i - 1) * 6 + 7 ] = 0x00;
      
      /* forces the asf demuxer to not choose this stream */
      this->asf_header[this->bitrates_pos[this->stream_ids[i]]]     = 0;
      this->asf_header[this->bitrates_pos[this->stream_ids[i]] + 1] = 0;
      this->asf_header[this->bitrates_pos[this->stream_ids[i]] + 2] = 0;
      this->asf_header[this->bitrates_pos[this->stream_ids[i]] + 3] = 0;
    }
  }

  if (!send_command (this, 0x33, this->num_stream_ids, 
		     0xFFFF | this->stream_ids[0] << 16, 
		     this->num_stream_ids * 6 + 2))
    goto fail;

  if (!get_answer (this))
    goto fail;

  report_progress (stream, 75);

  /* 0x07 */

  
  memset (this->scmd_body, 0, 40);

  for (i = 8; i < 16; i++)
    this->scmd_body[i] = 0xFF;

  this->scmd_body[20] = 0x04;

  if (!send_command (this, 0x07, 1, 
		     0xFFFF | this->stream_ids[0] << 16, 
		     24))
    goto fail;

  report_progress (stream, 100);

#ifdef LOG
  printf(" mms_connect: passed\n" );
#endif
  return this;

 fail:

  close (this->s);
  free (url);
  free (this);
  return NULL;

}


static int get_media_packet (mms_t *this) {
  unsigned char  pre_header[8];

  if (!receive (this->s, pre_header, 8)) {
    printf ("pre-header read failed\n");
    return 0;
  }

#ifdef LOG
  {
    int i;
    for (i = 0; i < 8; i++)
      printf ("pre_header[%d] = %02x (%d)\n",
	      i, pre_header[i], pre_header[i]);
  }
#endif

  if (pre_header[4] == 0x04) {

    int packet_len;

    packet_len = (pre_header[7] << 8 | pre_header[6]) - 8;

#ifdef LOG
    printf ("asf media packet detected, len=%d\n",
	    packet_len);
#endif

    if (!receive (this->s, this->buf, packet_len)) {
      printf ("media data read failed\n");
      return 0;
    }

    /* explicit padding with 0 */
    memset(this->buf + packet_len, 0, this->packet_length - packet_len);
    this->buf_size = this->packet_length;

  } else {

    int packet_len, command;

    if (!receive (this->s, (char *)&packet_len, 4)) {
      printf ("packet_len read failed\n");
      return 0;
    }

    packet_len = get_32 ((char *)&packet_len, 0) + 4;

#ifdef LOG
    printf ("command packet detected, len=%d\n",
	    packet_len);
#endif

    if (!receive (this->s, this->buf, packet_len)) {
      printf ("command data read failed\n");
      return 0;
    }

    if ( (pre_header[7] != 0xb0) || (pre_header[6] != 0x0b)
	 || (pre_header[5] != 0xfa) || (pre_header[4] != 0xce) ) {

      printf ("missing signature\n");
      abort();

    }

    command = get_32 (this->buf, 24) & 0xFFFF;

#ifdef LOG
    printf ("command: %02x\n", command);
#endif

    if (command == 0x1b) 
      send_command (this, 0x1b, 0, 0, 0);
    else if (command == 0x1e) {

      printf ("libmms: everything done. Thank you for downloading a media file containing proprietary and patentend technology.\n");

      return 0;
    } else if (command != 0x05) {
      printf ("unknown command %02x\n", command);
      abort();
    }
  }

#ifdef LOG
  printf ("get media packet succ\n");
#endif

  return 1;
}

int mms_peek_header (mms_t *this, char *data, int maxsize) {

  int len;

  len = (this->asf_header_len < maxsize) ? this->asf_header_len : maxsize;

  memcpy(data, this->asf_header, len);
  return len;
}

int mms_read (mms_t *this, char *data, int len) {
  int total;

  total = 0;

  while (total < len) {

#ifdef LOG
    printf ("libmms: read, got %d / %d bytes\n", total, len);
#endif

    if (this->asf_header_read < this->asf_header_len) {
      int n, bytes_left ;

      bytes_left = this->asf_header_len - this->asf_header_read ;

      if ((len-total) < bytes_left)
	n = len-total;
      else
	n = bytes_left;

      memcpy (&data[total], &this->asf_header[this->asf_header_read], n);

      this->asf_header_read += n;
      total += n;
    } else {

      int n, bytes_left ;

      bytes_left = this->buf_size - this->buf_read;

      while (!bytes_left) {
	
	this->buf_read = 0;

	if (!get_media_packet (this)) {
	  printf ("libmms: get_media_packet failed\n");
	  return total;
	}
	bytes_left = this->buf_size - this->buf_read;
      }
      

      if ((len-total)<bytes_left)
	n = len-total;
      else
	n = bytes_left;

      memcpy (&data[total], &this->buf[this->buf_read], n);

      this->buf_read += n;
      total += n;
    }
  }

  return total;

}


void mms_close (mms_t *this) {

  if (this->s >= 0) {
    close(this->s);
  }

  free (this->host);
  free (this->url);
  free (this);
}

uint32_t mms_get_length (mms_t *this) {
  return this->file_length;
}
