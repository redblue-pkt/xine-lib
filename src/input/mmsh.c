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
 * $Id: mmsh.c,v 1.15 2003/04/25 20:37:21 tmattern Exp $
 *
 * based on mms.c and specs from avifile
 * (http://avifile.sourceforge.net/asf-1.0.htm)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
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
#include <assert.h>

#include "xine_internal.h"
#include "xineutils.h"

#include "bswap.h"
#include "mmsh.h"
#include "../demuxers/asfheader.h"

/*
#define LOG
*/

#define USERAGENT "User-Agent: NSPlayer/7.1.0.3055\r\n"
#define CLIENTGUID "Pragma: xClientGUID={c77e7400-738a-11d2-9add-0020af0a3278}\r\n"


#define MMSH_PORT                  80
#define MMSH_UNKNOWN                0
#define MMSH_SEEKABLE               1
#define MMSH_LIVE                   2
#define CHUNK_HEADER_LENGTH        12
#define CHUNK_TYPE_DATA        0x4424
#define CHUNK_TYPE_END         0x4524
#define CHUNK_TYPE_ASF_HEADER  0x4824
#define CHUNK_SIZE              65536  /* max chunk size */
#define ASF_HEADER_SIZE          8192  /* max header size */

static const char* mmsh_FirstRequest =
    "GET %s HTTP/1.0\r\n"
    "Accept: */*\r\n"
    USERAGENT
    "Host: %s\r\n"
    "Pragma: no-cache,rate=1.000000,stream-time=0,stream-offset=0:0,request-context=%u,max-duration=0\r\n"
    CLIENTGUID
    "Connection: Close\r\n\r\n";

static const char* mmsh_SeekableRequest =
    "GET %s HTTP/1.0\r\n"
    "Accept: */*\r\n"
    USERAGENT
    "Host: %s\r\n"
    "Pragma: no-cache,rate=1.000000,stream-time=%u,stream-offset=%u:%u,request-context=%u,max-duration=%u\r\n"
    CLIENTGUID
    "Pragma: xPlayStrm=1\r\n"
    "Pragma: stream-switch-count=%d\r\n"
    "Pragma: stream-switch-entry=%s\r\n" /*  ffff:1:0 ffff:2:0 */
    "Connection: Close\r\n\r\n";

static const char* mmsh_LiveRequest =
    "GET %s HTTP/1.0\r\n"
    "Accept: */*\r\n"
    USERAGENT
    "Host: %s\r\n"
    "Pragma: no-cache,rate=1.000000,request-context=%u\r\n"
    "Pragma: xPlayStrm=1\r\n"
    CLIENTGUID
    "Pragma: stream-switch-count=%d\r\n"
    "Pragma: stream-switch-entry=%s\r\n"
    "Connection: Close\r\n\r\n";


#if 0
/* Unused requests */
static const char* mmsh_PostRequest =
    "POST %s HTTP/1.0\r\n"
    "Accept: */*\r\n"
    USERAGENT
    "Host: %s\r\n"
    "Pragma: client-id=%u\r\n"
/*    "Pragma: log-line=no-cache,rate=1.000000,stream-time=%u,stream-offset=%u:%u,request-context=2,max-duration=%u\r\n" */
    "Pragma: Content-Length: 0\r\n"
    CLIENTGUID
    "\r\n";

static const char* mmsh_RangeRequest =
    "GET %s HTTP/1.0\r\n"
    "Accept: */*\r\n"
    USERAGENT
    "Host: %s\r\n"
    "Range: bytes=%Lu-\r\n"
    CLIENTGUID
    "Connection: Close\r\n\r\n";
#endif

/* 
 * mmsh specific types 
 */


struct mmsh_s {

  xine_stream_t *stream;

  int           s;

  char         *host;
  char         *path;
  char         *file;
  char         *url;

  char          str[1024]; /* scratch buffer to built strings */

  int           stream_type;  /* seekable or broadcast */
  
  /* receive buffer */
  
  /* chunk */
  uint16_t      chunk_type;
  uint16_t      chunk_length;
  uint16_t      chunk_seq_number;
  int           chunk_eos;
  uint8_t       buf[CHUNK_SIZE];

  int           buf_size;
  int           buf_read;

  uint8_t       asf_header[ASF_HEADER_SIZE];
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


static int host_connect_attempt(struct in_addr ia, int port) {

  int                s;
  struct sockaddr_in sin;

  s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);  
  if (s == -1) {
    printf ("libmmsh: socket(): %s\n", strerror(errno));
    return -1;
  }

  /* put socket in non-blocking mode */
  fcntl (s, F_SETFL, fcntl (s, F_GETFL) | O_NONBLOCK);

  sin.sin_family = AF_INET;
  sin.sin_addr   = ia;
  sin.sin_port   = htons(port);
  
  if (connect(s, (struct sockaddr *)&sin, sizeof(sin))==-1 
      && errno != EINPROGRESS) {
    printf ("libmmsh: connect(): %s\n", strerror(errno));
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
    printf ("libmmsh: unable to resolve '%s'.\n", host);
    return -1;
  }

  for (i = 0; h->h_addr_list[i]; i++) {
    struct in_addr ia;

    memcpy (&ia, h->h_addr_list[i], 4);
    s = host_connect_attempt(ia, port);
    if(s != -1)
      return s;
  }
  printf ("libmmsh: unable to connect to '%s'.\n", host);
  return -1;
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
      printf ("libmmsh: GUID: %s\n", guids[i].name);
#endif
      return i;
    }
  }

  printf ("libmmsh: unknown GUID: 0x%x, 0x%x, 0x%x, "
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
    printf ("libmmsh: sending data, %d of %d\n", n, len);
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

static int send_command (mmsh_t *this, char *cmd)  {
  int length;

#ifdef LOG
  printf ("libmmsh: send_command:\n%s\n", cmd);
#endif
  length = strlen(cmd);
  if (send_data (this->s, cmd, length) != length) {
    printf ("libmmsh: send error\n");
    return 0;
  }
  return 1;
}

static int get_answer (mmsh_t *this) {
 
  int done, len, linenum;
  char *features;

  done = 0; len = 0; linenum = 0;
  this->stream_type = MMSH_UNKNOWN;

  while (!done) {

    if (xine_read_abort (this->stream, this->s, &(this->buf[len]), 1) <= 0) {
      printf ("libmmsh: alert: end of stream\n");
      return 0;
    }

    if (this->buf[len] == '\012') {

      this->buf[len] = '\0';
      len--;
      
      if (len >= 0 && this->buf[len] == '\015') {
        this->buf[len] = '\0';
        len--;
      }

      linenum++;
      
#ifdef LOG
      printf ("libmmsh: answer: >%s<\n", this->buf);
#endif

      if (linenum == 1) {
        int httpver, httpsub, httpcode;
        char httpstatus[100];

        if (sscanf(this->buf, "HTTP/%d.%d %d", &httpver, &httpsub,
            &httpcode) != 3) {
          printf( "libmmsh: bad response format\n");
          return 0;
        }

        if (httpcode >= 300 && httpcode < 400) {
          printf( _("libmmsh: 3xx redirection not implemented: >%d %s<\n"),
                 httpcode, httpstatus);
          return 0;
        }

        if (httpcode < 200 || httpcode >= 300) {
          printf( _("libmmsh: http status not 2xx: >%d %s<\n"),
                 httpcode, httpstatus);
          return 0;
        }
      } else {

        if (!strncasecmp(this->buf, "Location: ", 10)) {
          printf( _("libmmsh: Location redirection not implemented\n"));
          return 0;
        }
        
        if (!strncasecmp(this->buf, "Pragma:", 7)) {
          features = strstr(this->buf + 7, "features=");
          if (features) {
            if (strstr(features, "seekable")) {
              printf("libmmsh: seekable stream\n");
              this->stream_type = MMSH_SEEKABLE;
            } else {
              if (strstr(features, "broadcast")) {
                printf("libmmsh: live stream\n");
                this->stream_type = MMSH_LIVE;
              }
            }
          }
        }
      }
      
      if (len == -1) {
        done = 1;
      } else {
        len = 0;
      }
    } else {
      len ++;
    }
  }
  if (this->stream_type == MMSH_UNKNOWN) {
    printf("libmmsh: unknown stream type\n");
    this->stream_type = MMSH_SEEKABLE; /* FIXME ? */
  }
  return 1;
}

static int get_chunk_header (mmsh_t *this) {
  char chunk_header[CHUNK_HEADER_LENGTH];
  int len;

#ifdef LOG
  printf ("libmmsh: get_chunk\n");
#endif
  /* chunk header */
  len = xine_read_abort(this->stream, this->s, chunk_header, CHUNK_HEADER_LENGTH);
  if (len != CHUNK_HEADER_LENGTH) {
#ifdef LOG
    printf ("libmmsh: chunk header read failed, %d != %d\n", len, CHUNK_HEADER_LENGTH);
#endif
    return 0;
  }
  this->chunk_type       = get_16 (chunk_header, 0);
  this->chunk_length     = get_16 (chunk_header, 2) - 8;
  this->chunk_seq_number = get_32 (chunk_header, 4);

  /* display debug infos */
#ifdef LOG
  switch (this->chunk_type) {
    case CHUNK_TYPE_DATA:
      printf ("libmmsh: chunk type:       CHUNK_TYPE_DATA\n");
      break;
    case CHUNK_TYPE_END:
      printf ("libmmsh: chunk type:       CHUNK_TYPE_END\n");
      break;
    case CHUNK_TYPE_ASF_HEADER:
      printf ("libmmsh: chunk type:       CHUNK_TYPE_ASF_HEADER\n");
      break;
  }
  printf ("libmmsh: chunk length:     %d\n", this->chunk_length);
  printf ("libmmsh: chunk seq_number: %d\n", this->chunk_seq_number);
#endif

  return 1;
}

static int get_header (mmsh_t *this) {
  int len = 0;

  this->asf_header_len = 0;

  /* read chunk */
  while (1) {
    if (get_chunk_header(this)) {
      if (this->chunk_type == CHUNK_TYPE_ASF_HEADER) {
        if ((this->asf_header_len + this->chunk_length) > ASF_HEADER_SIZE) {
          printf ("libmmsh: the asf header exceed %d bytes\n", ASF_HEADER_SIZE);
          return 0;
        } else {
          len = xine_read_abort (this->stream, this->s, this->asf_header + this->asf_header_len,
                              this->chunk_length);
          this->asf_header_len += len;
          if (len != this->chunk_length) {
            return 0;
          }
        }
      } else {
        break;
      }
    } else {
      return 0;
    }
  }

  /* read the first data chunk */
  len = xine_read_abort (this->stream, this->s, this->buf, this->chunk_length);
  if (len != this->chunk_length) {
    return 0;
  } else {
    this->buf_size = this->packet_length;
    return 1;
  }
}

static void interp_header (mmsh_t *this) {

  int i;

  this->packet_length = 0;

  /*
   * parse asf header
   */

  i = 30;
  while ((i + 24) < this->asf_header_len) {

    int guid;
    uint64_t length;

    guid = get_guid(this->asf_header, i);
    i += 16;

    length = get_64(this->asf_header, i);
    i += 8;

    if ((i + length) >= this->asf_header_len) return;

    switch (guid) {

      case GUID_ASF_FILE_PROPERTIES:

        this->packet_length = get_32(this->asf_header, i + 92 - 24);
        this->file_length   = get_32(this->asf_header, i + 40 - 24);
#ifdef LOG
        printf ("libmmsh: file object, packet length = %d (%d)\n",
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
          printf ("libmmsh: stream object, stream id: %d\n", stream_id);
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
          printf ("libmmsh: stream bitrate properties\n");
#endif

#ifdef LOG
          printf ("libmmsh: streams %d\n", streams);
#endif
          for(j = 0; j < streams; j++) {
            stream_id = get_16(this->asf_header, i + 2 + j * 6);
#ifdef LOG
            printf ("libmmsh: stream id %d\n", stream_id);
#endif
            this->bitrates[stream_id] = get_32(this->asf_header, i + 4 + j * 6);
            this->bitrates_pos[stream_id] = i + 4 + j * 6;
            printf ("libmmsh: stream id %d, bitrate %d\n", stream_id,
                    this->bitrates[stream_id]);
          }
        }
        break;

      default:
#ifdef LOG
        printf ("libmmsh: unknown object\n");
#endif
        break;
    }

#ifdef LOG
    printf ("libmmsh: length    : %lld\n", length);
#endif

    if (length > 24) {
      i += length - 24;
    }
  }
}

const static char *const mmsh_url_s[] = { "MMS://", "MMSH://", NULL };

static int mmsh_valid_url (char* url, const char *const * mms_url) {
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

char* mmsh_connect_common(int *s, int *port, char *url, char **host, char **path, char **file) {
  int     proto_len;
  char   *hostend;
  char   *forport;
  char   *_url;
  char   *_host;

  if ((proto_len = mmsh_valid_url(url, mmsh_url_s)) <= 0) {
#ifdef LOG
    printf ("libmms: invalid url >%s< (should be mmsh:// - style)\n", url);
#endif
    return NULL;
  }

  /* Create a local copy (alloca()'ed), avoid to corrupt the original URL */
  xine_strdupa(_url, &url[proto_len]);
  
  _host = _url;

  /* extract hostname */
#ifdef LOG
  printf ("libmmsh: extracting host name \n");
#endif
  hostend = strchr(_host, '/');
  
  /*
  if ((!hostend) || (strlen(hostend) <= 1)) {
    printf ("libmmsh: invalid url >%s<, failed to find hostend\n", url);
    return NULL;
  }
  */
  if (!hostend) {
#ifdef LOG
    printf ("libmmsh: no trailing /\n");
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
    *path = &url[proto_len] + (hostend - _url - 1);
  
  if(file)
    *file = strrchr (url, '/');

  /* 
   * try to connect 
   */
#ifdef LOG
  printf("libmmsh: try to connect to %s on port %d \n", *host, *port);
#endif
  *s = host_connect (*host, *port);
  
  if (*s == -1) {
    printf ("libmmsh: failed to connect '%s'\n", *host);
    free (*host);
    return NULL;
  }

#ifdef LOG
  printf ("libmmsh: connected\n");
#endif

  return url;
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

mmsh_t *mmsh_connect (xine_stream_t *stream, const char *url_, int bandwidth) {
  mmsh_t *this;
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
  char   stream_selection[9 * 20]; /* 9 chars per stream */
 
  if (!url_)
    return NULL;

  report_progress (stream, 0);

  url = strdup (url_);
  port = MMSH_PORT;
  url1 = mmsh_connect_common(&s, &port, url, &host, &path, &file);

  if(!url1){
    free(url);
    return NULL;
  }
  
  report_progress (stream, 10);

  this = (mmsh_t*) xine_xmalloc (sizeof (mmsh_t));

  this->stream          = stream;
  this->url             = url;
  this->host            = host;
  this->path            = path;
  this->file            = file;
  this->s               = s;
  this->asf_header_len  = 0;
  this->asf_header_read = 0;
  this->num_stream_ids  = 0;
  this->packet_length   = 0;
  this->buf_size        = 0;
  this->buf_read        = 0;
  this->has_audio       = 0;
  this->has_video       = 0;
  this->chunk_eos       = 0;
  
#ifdef LOG
  printf ("libmmsh: url=%s\nlibmmsh:   host=%s\nlibmmsh:   "
          "path=%s\nlibmmsh:   file=%s\n", url, host, path, file);
#endif

  /*
   * let the negotiations begin...
   */

  /* first request */
  printf("libmmsh: first http request\n");
  
  sprintf (this->str, mmsh_FirstRequest, path, host, 1);

  if (!send_command (this, this->str))
    goto fail;

  if (!get_answer (this)) 
    goto fail;

    
  get_header(this);
  interp_header(this);
  
  close(this->s);
  report_progress (stream, 20);

  
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
  printf("libmmsh: bandwitdh %d, left %d\n", bandwidth, bandwitdh_left);
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

  /* choose the stream with the lower bitrate */
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

  printf("libmmsh: audio stream %d, video stream %d\n", audio_stream, video_stream);

  
    /* second request */
  printf("libmmsh: second http request\n");
  url1 = mmsh_connect_common(&s, &port, url, &host, &path, &file);
  if(!url1){
    free(url);
    return NULL;
  }
  this->s = s;

  /* stream selection string */
  /* The same selection is done with mmst */
  /* 0 means selected */
  /* 2 means disabled */
  for (i = 0; i < this->num_stream_ids; i++) {
    if ((this->stream_ids[i] == audio_stream) ||
        (this->stream_ids[i] == video_stream)) {
      sprintf(stream_selection + i * 9, "ffff:%d:0 ", this->stream_ids[i]);
    } else {
#ifdef LOG
      printf("libmms: disabling stream %d\n", this->stream_ids[i]);
#endif
      sprintf(stream_selection + i * 9, "ffff:%d:2 ", this->stream_ids[i]);
    }
  }

  switch (this->stream_type) {
    case MMSH_SEEKABLE:
      sprintf (this->str, mmsh_SeekableRequest, path, host, 0, 0, 0, 2, 0,
               this->num_stream_ids, stream_selection);
      break;
    case MMSH_LIVE:
      sprintf (this->str, mmsh_LiveRequest, path, host, 2,
               this->num_stream_ids, stream_selection);
      break;
    default:
      assert(1);
  }
  
  if (!send_command (this, this->str))
    goto fail;
  
#ifdef LOG
  printf("libmmsh: before read \n");
#endif

  if (!get_answer (this))
    goto fail;

  get_header(this);
  interp_header(this);
  
  for (i = 0; i < this->num_stream_ids; i++) {
    if ((this->stream_ids[i] != audio_stream) &&
        (this->stream_ids[i] != video_stream)) {
      printf("libmms: disabling stream %d\n", this->stream_ids[i]);
      /* forces the asf demuxer to not choose this stream */
      this->asf_header[this->bitrates_pos[this->stream_ids[i]]]     = 0;
      this->asf_header[this->bitrates_pos[this->stream_ids[i]] + 1] = 0;
      this->asf_header[this->bitrates_pos[this->stream_ids[i]] + 2] = 0;
      this->asf_header[this->bitrates_pos[this->stream_ids[i]] + 3] = 0;
    }
  }

  report_progress (stream, 100);

#ifdef LOG
  printf("libmms: mmsh_connect: passed\n" );
#endif
  return this;

 fail:

  close (this->s);
  free (url);
  free (this);
  return NULL;

}


static int get_media_packet (mmsh_t *this) {
  int len = 0;

#ifdef LOG
  printf("libmmsh: get_media_packet: this->packet_length: %d\n", this->packet_length);
#endif

  if (!this->chunk_eos && get_chunk_header(this)) {
    switch (this->chunk_type) {
      case CHUNK_TYPE_END:
        this->chunk_eos = 1;
      case CHUNK_TYPE_DATA:
        break;
      default:
        printf("libmmsh: invalid chunk type\n");
        return 0;
    }

    if (this->chunk_length > CHUNK_SIZE) {
      printf("libmmsh: invalid chunk length\n");
      return 0;
    } else {
      len = xine_read_abort (this->stream, this->s, this->buf, this->chunk_length);

      if (len == this->chunk_length) {
        /* explicit padding with 0 */
        memset(this->buf + this->chunk_length, 0,
               this->packet_length - this->chunk_length);
        this->buf_size = this->packet_length;
        return 1;
      } else {
        printf("libmmsh: read error, %d != %d\n", len, this->chunk_length);
        return 0;
      }
    }
  } else {
    return 0;
  }
}

int mmsh_peek_header (mmsh_t *this, char *data, int maxsize) {

  int len;

  len = (this->asf_header_len < maxsize) ? this->asf_header_len : maxsize;

  memcpy(data, this->asf_header, len);
  return len;
}

int mmsh_read (mmsh_t *this, char *data, int len) {
  int total;

  total = 0;

#ifdef LOG
  printf ("libmmsh: mmsh_read: len: %d\n", len);
#endif

  while (total < len) {

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
	  printf ("libmmsh: get_media_packet failed\n");
	  return total;
	}
	bytes_left = this->buf_size;
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


void mmsh_close (mmsh_t *this) {

  if (this->s >= 0) {
    close(this->s);
  }

  free (this->host);
  free (this->url);
  free (this);
}


uint32_t mmsh_get_length (mmsh_t *this) {
  return this->file_length;
}

