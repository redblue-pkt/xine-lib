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
 * $Id: pnm.c,v 1.11 2003/01/31 14:06:17 miguelfreitas Exp $
 *
 * pnm protocol implementation 
 * based upon code from joschka
 */

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>

#include "pnm.h"
#include "libreal/rmff.h"
#include "bswap.h"
#include "xineutils.h"

/*
#define LOG
*/

#define BUF_SIZE 4096
#define HEADER_SIZE 4096

struct pnm_s {

  int           s;

  char         *host;
  int           port;
  char         *path;
  char         *url;

  /* scratch buffer */
  char          buffer[BUF_SIZE];

  /* receive buffer */
  uint8_t       recv[BUF_SIZE];
  int           recv_size;
  int           recv_read;

  uint8_t       header[HEADER_SIZE];
  int           header_len;
  int           header_read;
  unsigned int  seq_num[4];     /* two streams with two indices   */
  unsigned int  seq_current[2]; /* seqs of last stream chunk read */
  uint32_t      ts_current;     /* timestamp of current chunk     */
  uint32_t      ts_last[2];     /* timestamps of last chunks      */
  unsigned int  packet;         /* number of last recieved packet */
};


/* sizes */
#define PREAMBLE_SIZE 8
#define CHECKSUM_SIZE 3

/*
 * data to construct header for real demuxer
 */

/* header of rm files */
#define RM_HEADER_SIZE 0x12
const unsigned char rm_header[]={
        0x2e, 0x52, 0x4d, 0x46, /* object_id      ".RMF" */
        0x00, 0x00, 0x00, 0x12, /* header_size    0x12   */
        0x00, 0x00,             /* object_version 0x00   */
        0x00, 0x00, 0x00, 0x00, /* file_version   0x00   */
        0x00, 0x00, 0x00, 0x06  /* num_headers    0x06   */
};

/* data chunk header */
#define PNM_DATA_HEADER_SIZE 18
const unsigned char pnm_data_header[]={
        'D','A','T','A',
         0,0,0,0,       /* data chunk size  */
         0,0,           /* object version   */
         0,0,0,0,       /* num packets      */
         0,0,0,0};      /* next data header */


/*
 * data to be transmitted during stream request
 */

/* pnm request chunk ids */

#define PNA_CLIENT_CAPS      0x03
#define PNA_CLIENT_CHALLANGE 0x04
#define PNA_BANDWIDTH        0x05
#define PNA_GUID             0x13
#define PNA_TIMESTAMP        0x17
#define PNA_TWENTYFOUR       0x18

#define PNA_CLIENT_STRING    0x63
#define PNA_PATH_REQUEST     0x52

const unsigned char pnm_challenge[] = "0990f6b4508b51e801bd6da011ad7b56";
const unsigned char pnm_timestamp[] = "[15/06/1999:22:22:49 00:00]";
const unsigned char pnm_guid[]      = "3eac2411-83d5-11d2-f3ea-d7c3a51aa8b0";
const unsigned char pnm_response[]  = "97715a899cbe41cee00dd434851535bf";
const unsigned char client_string[] = "WinNT_4.0_6.0.6.45_plus32_MP60_en-US_686l";

#define PNM_HEADER_SIZE 11
const unsigned char pnm_header[] = {
        'P','N','A',
        0x00, 0x0a,
        0x00, 0x14,
        0x00, 0x02,
        0x00, 0x01 };

#define PNM_CLIENT_CAPS_SIZE 126
const unsigned char pnm_client_caps[] = {
    0x07, 0x8a, 'p','n','r','v', 
       0, 0x90, 'p','n','r','v', 
       0, 0x64, 'd','n','e','t', 
       0, 0x46, 'p','n','r','v', 
       0, 0x32, 'd','n','e','t', 
       0, 0x2b, 'p','n','r','v', 
       0, 0x28, 'd','n','e','t', 
       0, 0x24, 'p','n','r','v', 
       0, 0x19, 'd','n','e','t', 
       0, 0x18, 'p','n','r','v', 
       0, 0x14, 's','i','p','r', 
       0, 0x14, 'd','n','e','t', 
       0, 0x24, '2','8','_','8', 
       0, 0x12, 'p','n','r','v', 
       0, 0x0f, 'd','n','e','t', 
       0, 0x0a, 's','i','p','r', 
       0, 0x0a, 'd','n','e','t', 
       0, 0x08, 's','i','p','r', 
       0, 0x06, 's','i','p','r', 
       0, 0x12, 'l','p','c','J', 
       0, 0x07, '0','5','_','6' };

const uint32_t pnm_default_bandwidth=10485800;
const uint32_t pnm_available_bandwidths[]={14400,19200,28800,33600,34430,57600,
                                  115200,262200,393216,524300,1544000,10485800};

#define PNM_TWENTYFOUR_SIZE 16
unsigned char pnm_twentyfour[]={
    0xd5, 0x42, 0xa3, 0x1b, 0xef, 0x1f, 0x70, 0x24,
    0x85, 0x29, 0xb3, 0x8d, 0xba, 0x11, 0xf3, 0xd6 };

/* now other data follows. marked with 0x0000 at the beginning */
int after_chunks_length=6;
unsigned char after_chunks[]={
    0x00, 0x00, /* mark */
    
    0x50, 0x84, /* seems to be fixated */
    0x1f, 0x3a  /* varies on each request (checksum ?)*/
    };



static void hexdump (char *buf, int length);

/*
 * network utilities
 * nothing really sophisticated here, only tools
 * to connect to a host, send and receive data over this connection
 */
 
static int host_connect_attempt(struct in_addr ia, int port) {

  int                s;
  struct sockaddr_in sin;

  s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);  
  if (s == -1) {
    printf ("input_pnm: socket(): %s\n", strerror(errno));
    return -1;
  }

  sin.sin_family = AF_INET;
  sin.sin_addr   = ia;
  sin.sin_port   = htons(port);
  
  if (connect(s, (struct sockaddr *)&sin, sizeof(sin))==-1 
      && errno != EINPROGRESS) {
    printf ("input_pnm: connect(): %s\n", strerror(errno));
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
    printf ("input_pnm: unable to resolve '%s'.\n", host);
    return -1;
  }

  for (i = 0; h->h_addr_list[i]; i++) {
    struct in_addr ia;

    memcpy (&ia, h->h_addr_list[i], 4);
    s = host_connect_attempt(ia, port);
    if(s != -1)
      return s;
  }
  printf ("input_pnm: unable to connect to '%s'.\n", host);
  return -1;
}

static int rm_write(int s, const char *buf, int len) {
  int total, timeout;

  total = 0; timeout = 30;
  while (total < len){ 
    int n;

    n = write (s, &buf[total], len - total);

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

static ssize_t rm_read(int fd, void *buf, size_t count) {
  
  ssize_t ret, total;

  total = 0;

  while (total < count) {
  
    fd_set rset;
    struct timeval timeout;

    FD_ZERO (&rset);
    FD_SET  (fd, &rset);
    
    timeout.tv_sec  = 3;
    timeout.tv_usec = 0;
    
    if (select (fd+1, &rset, NULL, NULL, &timeout) <= 0) {
      return -1;
    }
    
    ret=read (fd, ((uint8_t*)buf)+total, count-total);

    if (ret<=0) {
      printf ("input_pnm: read error.\n");
      return ret;
    } else
      total += ret;
  }

  return total;
}

/*
 * a simple hexdump tool for debugging purposes
 */
 
static void hexdump (char *buf, int length) {

  int i;

  printf ("input_pnm: ascii>");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    if ((c >= 32) && (c <= 128))
      printf ("%c", c);
    else
      printf (".");
  }
  printf ("\n");

  printf ("input_pnm: hexdump> ");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    printf ("%02x", c);

    if ((i % 16) == 15)
      printf ("\npnm:         ");

    if ((i % 2) == 1)
      printf (" ");

  }
  printf ("\n");
}

/*
 * pnm_get_chunk gets a chunk from stream
 * and returns number of bytes read, chunk_type, data and
 * a flag indicating whether the server want a response.
 *
 * following chunk format is expected (all in big endian!):
 * uint32_t chunk_type  |
 * uint32_t chunk_size  | <- preamble
 * uint8_t data[chunk_size-PREAMBLE_SIZE]
 *
 * a few exceptions have to be handeled:
 * if we read 0x72 as the first byte, we ignore CHECKSUM_SIZE bytes.
 * if we have an PNA_TAG, we need a different parsing; see below.
 */

static unsigned int pnm_get_chunk(pnm_t *p, 
                         unsigned int max,
                         unsigned int *chunk_type,
                         char *data, int *need_response) {

  unsigned int chunk_size;
  int n;
  char *ptr;
 
  /* get first PREAMBLE_SIZE bytes and ignore checksum */
  rm_read (p->s, data, CHECKSUM_SIZE);
  if (data[0] == 0x72)
    rm_read (p->s, data, PREAMBLE_SIZE);
  else
    rm_read (p->s, data+CHECKSUM_SIZE, PREAMBLE_SIZE-CHECKSUM_SIZE);
  
  *chunk_type = be2me_32(*((uint32_t *)data));
  chunk_size = be2me_32(*((uint32_t *)(data+4)));

  switch (*chunk_type) {
    case PNA_TAG:
      *need_response=0;
      ptr=data+PREAMBLE_SIZE;
      rm_read (p->s, ptr++, 1);

      while(1) {
	/* The pna chunk is devided into subchunks.
	 * expecting following chunk format (in big endian): 
	 * 0x4f 
	 * uint8_t chunk_size 
	 * uint8_t data[chunk_size] 
	 *
	 * if first byte is 'X', we got a message from server
	 * if first byte is 'F', we got an error
	 */

        rm_read (p->s, ptr, 2);
	if (*ptr == 'X') /* checking for server message */
	{
	  printf("input_pnm: got a message from server:\n");
	  rm_read (p->s, ptr+2, 1);

	  /* two bytes of message length*/
	  n=be2me_16(*(uint16_t*)(ptr+1));

	  /* message itself */
	  rm_read (p->s, ptr+3, n);
	  ptr[3+n]=0;
	  printf("%s\n",ptr+3);
	  return -1;
	}
	
	if (*ptr == 'F') /* checking for server error */
	{
	  /* some error codes after 'F' were ignored */
	  printf("input_pnm: server error.\n");
	  return -1;
	}
	if (*ptr == 'i') /* the server want a response from us. it will be sent after these headers */
	{
	  ptr+=2;
	  *need_response=1;
	  continue;
	}
	if (*ptr != 0x4f) break;
	n=ptr[1];
	rm_read (p->s, ptr+2, n);
	ptr+=(n+2);
      }
      /* the checksum of the next chunk is ignored here */
      rm_read (p->s, ptr+2, 1);
      ptr+=3;
      chunk_size=ptr-data;
      break;
    case RMF_TAG:
    case DATA_TAG:
    case PROP_TAG:
    case MDPR_TAG:
    case CONT_TAG:
      if (chunk_size > max) {
        printf("error: max chunk size exeeded (max was 0x%04x)\n", max);
	/* reading some bytes for debugging */
        n=rm_read (p->s, &data[PREAMBLE_SIZE], 0x100 - PREAMBLE_SIZE);
        hexdump(data,n+PREAMBLE_SIZE);
        return -1;
      }
      rm_read (p->s, &data[PREAMBLE_SIZE], chunk_size-PREAMBLE_SIZE);
      break;
    default:
      *chunk_type = 0;
      chunk_size  = PREAMBLE_SIZE; 
      break;
  }

  return chunk_size;
}

/*
 * writes a chunk to a buffer, returns number of bytes written
 * chunk format (big endian):
 * uint16_t chunk_id
 * uint16_t length
 * uint8_t data[length]
 */

static int pnm_write_chunk(uint16_t chunk_id, uint16_t length, 
    const char *chunk, char *data) {

  uint16_t be_id, be_len;

  be_id=be2me_16(chunk_id);
  be_len=be2me_16(length);

  memcpy(data  , &be_id , 2);
  memcpy(data+2, &be_len, 2);
  memcpy(data+4, chunk  , length);
  
  return length+4;
}

/*
 * constructs a request and sends it
 */

static void pnm_send_request(pnm_t *p, uint32_t bandwidth) {

  uint16_t i16;
  int c=PNM_HEADER_SIZE;
  char fixme[]={0,1};

  memcpy(p->buffer,pnm_header,PNM_HEADER_SIZE);
  c+=pnm_write_chunk(PNA_CLIENT_CHALLANGE,strlen(pnm_challenge),
          pnm_challenge,&p->buffer[c]);
  c+=pnm_write_chunk(PNA_CLIENT_CAPS,PNM_CLIENT_CAPS_SIZE,
          pnm_client_caps,&p->buffer[c]);
  c+=pnm_write_chunk(0x0a,0,NULL,&p->buffer[c]);
  c+=pnm_write_chunk(0x0c,0,NULL,&p->buffer[c]);
  c+=pnm_write_chunk(0x0d,0,NULL,&p->buffer[c]);
  c+=pnm_write_chunk(0x16,2,fixme,&p->buffer[c]);
  c+=pnm_write_chunk(PNA_TIMESTAMP,strlen(pnm_timestamp),
          pnm_timestamp,&p->buffer[c]);
  c+=pnm_write_chunk(PNA_BANDWIDTH,4,
          (const char *)&pnm_default_bandwidth,&p->buffer[c]);
  c+=pnm_write_chunk(0x08,0,NULL,&p->buffer[c]);
  c+=pnm_write_chunk(0x0e,0,NULL,&p->buffer[c]);
  c+=pnm_write_chunk(0x0f,0,NULL,&p->buffer[c]);
  c+=pnm_write_chunk(0x11,0,NULL,&p->buffer[c]);
  c+=pnm_write_chunk(0x10,0,NULL,&p->buffer[c]);
  c+=pnm_write_chunk(0x15,0,NULL,&p->buffer[c]);
  c+=pnm_write_chunk(0x12,0,NULL,&p->buffer[c]);
  c+=pnm_write_chunk(PNA_GUID,strlen(pnm_guid),
          pnm_guid,&p->buffer[c]);
  c+=pnm_write_chunk(PNA_TWENTYFOUR,PNM_TWENTYFOUR_SIZE,
          pnm_twentyfour,&p->buffer[c]);
  
  /* data after chunks */
  memcpy(&p->buffer[c],after_chunks,after_chunks_length);
  c+=after_chunks_length;

  /* client id string */
  p->buffer[c]=PNA_CLIENT_STRING;
  i16=be2me_16(strlen(client_string)-1); /* dont know why do we have -1 here */
  memcpy(&p->buffer[c+1],&i16,2);
  memcpy(&p->buffer[c+3],client_string,strlen(client_string)+1);
  c=c+3+strlen(client_string)+1;

  /* file path */
  p->buffer[c]=0;
  p->buffer[c+1]=PNA_PATH_REQUEST;
  i16=be2me_16(strlen(p->path));
  memcpy(&p->buffer[c+2],&i16,2);
  memcpy(&p->buffer[c+4],p->path,strlen(p->path));
  c=c+4+strlen(p->path);

  /* some trailing bytes */
  p->buffer[c]='y';
  p->buffer[c+1]='B';
  
  rm_write(p->s,p->buffer,c+2);
}

/*
 * pnm_send_response sends a response of a challenge
 */

static void pnm_send_response(pnm_t *p, const char *response) {

  int size=strlen(response);

  p->buffer[0]=0x23;
  p->buffer[1]=0;
  p->buffer[2]=(unsigned char) size;

  memcpy(&p->buffer[3], response, size);

  rm_write (p->s, p->buffer, size+3);

}

/*
 * get headers and challenge and fix headers
 * write headers to p->header
 * write challenge to p->buffer
 *
 * return 0 on error.  != 0 on success
 */

static int pnm_get_headers(pnm_t *p, int *need_response) {

  uint32_t chunk_type;
  uint8_t  *ptr=p->header;
  uint8_t  *prop_hdr=NULL;
  int      chunk_size,size=0;
  int      nr;
/*  rmff_header_t *h; */

  *need_response=0;

  while(1) {
    if (HEADER_SIZE-size<=0)
    {
      printf("input_pnm: header buffer overflow. exiting\n");
      return 0;
    }
    chunk_size=pnm_get_chunk(p,HEADER_SIZE-size,&chunk_type,ptr,&nr);
    if (chunk_size < 0) return 0;
    if (chunk_type == 0) break;
    if (chunk_type == PNA_TAG)
    {
      memcpy(ptr, rm_header, RM_HEADER_SIZE);
      chunk_size=RM_HEADER_SIZE;
      *need_response=nr;
    }
    if (chunk_type == DATA_TAG)
      chunk_size=0;
    if (chunk_type == RMF_TAG)
      chunk_size=0;
    if (chunk_type == PROP_TAG)
        prop_hdr=ptr;
    size+=chunk_size;
    ptr+=chunk_size;
  }

  if (!prop_hdr) {
    printf("input_pnm: error while parsing headers.\n");
    return 0;
  }
  
  /* set data offset */
  {
    uint32_t be_size;

    be_size = be2me_32(size-1);
    memcpy(prop_hdr+42, &be_size, 4);
  }
  
  /* read challenge */
  memcpy (p->buffer, ptr, PREAMBLE_SIZE);
  rm_read (p->s, &p->buffer[PREAMBLE_SIZE], 64);

  /* now write a data header */
  memcpy(ptr, pnm_data_header, PNM_DATA_HEADER_SIZE);
  size+=PNM_DATA_HEADER_SIZE;
/*  
  h=rmff_scan_header(p->header);
  rmff_fix_header(h);
  p->header_len=rmff_get_header_size(h);
  rmff_dump_header(h, p->header, HEADER_SIZE);
*/
  p->header_len=size;
  
  return 1;
}

/* 
 * determine correct stream number by looking at indices
 * FIXME: this doesn't work with all streams! There must be
 * another way to determine correct stream numbers!
 */

static int pnm_calc_stream(pnm_t *p) {

  uint8_t str0, str1;

  str0=0;
  str1=0;

  /* looking at the first index to
   * find possible stream types
   */
  if (p->seq_current[0]==p->seq_num[0]) str0=1;
  if (p->seq_current[0]==p->seq_num[2]) str1=1;

  switch (str0+str1) {
    case 1: /* one is possible, good. */
      if (str0)
      {
        p->seq_num[0]++;
        p->seq_num[1]=p->seq_current[1]+1;
        return 0;
      } else
      {
        p->seq_num[2]++;
        p->seq_num[3]=p->seq_current[1]+1;
        return 1;
      }
      break;
    case 0:
    case 2: /* both types or none possible, not so good */
      /* try to figure out by second index */
      if (  (p->seq_current[1] == p->seq_num[1])
          &&(p->seq_current[1] != p->seq_num[3]))
      {
        /* ok, only stream0 matches */
        p->seq_num[0]=p->seq_current[0]+1;
        p->seq_num[1]++;
        return 0;
      }
      if (  (p->seq_current[1] == p->seq_num[3])
          &&(p->seq_current[1] != p->seq_num[1]))
      {
        /* ok, only stream1 matches */
        p->seq_num[2]=p->seq_current[0]+1;
        p->seq_num[3]++;
        return 1;
      }
      /* wow, both streams match, or not.   */
      /* now we try to decide by timestamps */
      if (p->ts_current < p->ts_last[1])
        return 0;
      if (p->ts_current < p->ts_last[0])
        return 1;
      /* does not help, we guess type 0     */
#ifdef LOG
      printf("guessing stream# 0\n");
#endif
      p->seq_num[0]=p->seq_current[0]+1;
      p->seq_num[1]=p->seq_current[1]+1;
      return 0;
      break;
  }
  printf("input_pnm: wow, something very nasty happened in pnm_calc_stream\n");
  return 2;
}

/*
 * gets a stream chunk and writes it to a recieve buffer
 */

static int pnm_get_stream_chunk(pnm_t *p) {

  int  n;
  char keepalive='!';
  unsigned int fof1, fof2, stream;

  /* send a keepalive                               */
  /* realplayer seems to do that every 43th package */
  if ((p->packet%43) == 42)  
  {
    rm_write(p->s,&keepalive,1);
  }

  /* data chunks begin with: 'Z' <o> <o> <i1> 'Z' <i2>
   * where <o> is the offset to next stream chunk,
   * <i1> is a 16 bit index (big endian)
   * <i2> is a 8 bit index which counts from 0x10 to somewhere
   */
  
  n = rm_read (p->s, p->buffer, 8);
  if (n<8) return 0;
  
  /* skip 8 bytes if 0x62 is read */
  if (p->buffer[0] == 0x62)
  {
    n = rm_read (p->s, p->buffer, 8);
    if (n<8) return 0;
#ifdef LOG
    printf("input_pnm: had to seek 8 bytes on 0x62\n");
#endif
  }
  
  /* a server message */
  if (p->buffer[0] == 'X')
  {
    int size=be2me_16(*(uint16_t*)(&p->buffer[1]));

    rm_read (p->s, &p->buffer[8], size-5);
    p->buffer[size+3]=0;
    printf("input_pnm: got message from server while reading stream:\n%s\n", &p->buffer[3]);
    return 0;
  }
  if (p->buffer[0] == 'F')
  {
    printf("input_pnm: server error.\n");
    return 0;
  }

  /* skip bytewise to next chunk.
   * seems, that we dont need that, if we send enough
   * keepalives
   */
  n=0;
  while (p->buffer[0] != 0x5a) {
    int i;
    for (i=1; i<8; i++) {
      p->buffer[i-1]=p->buffer[i];
    }
    rm_read (p->s, &p->buffer[7], 1);
    n++;
  }

#ifdef LOG
  if (n) printf("input_pnm: had to seek %i bytes to next chunk\n", n);
#endif

  /* check for 'Z's */
  if ((p->buffer[0] != 0x5a)||(p->buffer[7] != 0x5a))
  {
    printf("input_pnm: bad boundaries\n");
    hexdump(p->buffer, 8);
    return 0;
  }

  /* check offsets */
  fof1=be2me_16(*(uint16_t*)(&p->buffer[1]));
  fof2=be2me_16(*(uint16_t*)(&p->buffer[3]));
  if (fof1 != fof2)
  {
    printf("input_pnm: frame offsets are different: 0x%04x 0x%04x\n",fof1,fof2);
    return 0;
  }

  /* get first index */
  p->seq_current[0]=be2me_16(*(uint16_t*)(&p->buffer[5]));
  
  /* now read the rest of stream chunk */
  n = rm_read (p->s, &p->recv[5], fof1-5);
  if (n<(fof1-5)) return 0;

  /* get second index */
  p->seq_current[1]=p->recv[5];

  /* get timestamp */
  p->ts_current=be2me_32(*(uint32_t*)(&p->recv[6]));
  
  /* get stream number */
  stream=pnm_calc_stream(p);

  /* saving timestamp */
  p->ts_last[stream]=p->ts_current;
  
  /* constructing a data packet header */
  
  p->recv[0]=0;        /* object version */
  p->recv[1]=0;

  fof2=be2me_16(fof2);
  memcpy(&p->recv[2], &fof2, 2);
  /*p->recv[2]=(fof2>>8)%0xff;*/   /* length */
  /*p->recv[3]=(fof2)%0xff;*/

  p->recv[4]=0;         /* stream number */
  p->recv[5]=stream;
  
  p->recv[10]=p->recv[10] & 0xfe; /* streambox seems to do that... */

  p->packet++;

  p->recv_size=fof1;

  return fof1;
}

pnm_t *pnm_connect(const char *mrl) {
  
  char *mrl_ptr=strdup(mrl);
  char *slash, *colon;
  int pathbegin, hostend;
  pnm_t *p=xine_xmalloc(sizeof(pnm_t));
  int fd;
  int need_response=0;
  
  if (strncmp(mrl,"pnm://",6))
  {
    return NULL;
  }
  
  mrl_ptr+=6;

  p->port=7070;
  p->url=strdup(mrl);
  p->packet=0;

  slash=strchr(mrl_ptr,'/');
  colon=strchr(mrl_ptr,':');

  if(!slash) slash=mrl_ptr+strlen(mrl_ptr)+1;
  if(!colon) colon=slash; 
  if(colon > slash) colon=slash;

  pathbegin=slash-mrl_ptr;
  hostend=colon-mrl_ptr;

  p->host=malloc(sizeof(char)*hostend+1);
  strncpy(p->host, mrl_ptr, hostend);
  p->host[hostend]=0;

  if (pathbegin < strlen(mrl_ptr)) p->path=strdup(mrl_ptr+pathbegin+1);
  if (colon != slash) {
    strncpy(p->buffer,mrl_ptr+hostend+1, pathbegin-hostend-1);
    p->buffer[pathbegin-hostend-1]=0;
    p->port=atoi(p->buffer);
  }

  free(mrl_ptr-6);

#ifdef LOG
  printf("input_pnm: got mrl: %s %i %s\n",p->host,p->port,p->path);
#endif
  
  fd = host_connect (p->host, p->port);

  if (fd == -1) {
    printf ("input_pnm: failed to connect '%s'\n", p->host);
    free(p->path);
    free(p->host);
    free(p->url);
    free(p);
    return NULL;
  }
  p->s=fd;

  pnm_send_request(p,pnm_available_bandwidths[10]);
  if (!pnm_get_headers(p, &need_response)) {
    printf ("input_pnm: failed to set up stream\n");
    free(p->path);
    free(p->host);
    free(p->url);
    free(p);
    return NULL;
  }
  if (need_response)
    pnm_send_response(p, pnm_response);
  p->ts_last[0]=0;
  p->ts_last[1]=0;
  
  /* copy header to recv */

  memcpy(p->recv, p->header, p->header_len);
  p->recv_size = p->header_len;
  p->recv_read = 0;

  return p;
}

int pnm_read (pnm_t *this, char *data, int len) {
  
  int to_copy=len;
  char *dest=data;
  char *source=this->recv + this->recv_read;
  int fill=this->recv_size - this->recv_read;
  
  if (len < 0) return 0;
  while (to_copy > fill) {
    
    memcpy(dest, source, fill);
    to_copy -= fill;
    dest += fill;
    this->recv_read=0;

    if (!pnm_get_stream_chunk (this)) {
#ifdef LOG
      printf ("input_pnm: %d of %d bytes provided\n", len-to_copy, len);
#endif
      return len-to_copy;
    }
    source = this->recv;
    fill = this->recv_size - this->recv_read;
  }

  memcpy(dest, source, to_copy);
  this->recv_read += to_copy;

#ifdef LOG
  printf ("input_pnm: %d bytes provided\n", len);
#endif

  return len;
}

int pnm_peek_header (pnm_t *this, char *data, int maxsize) {

  int len;

  len = (this->header_len < maxsize) ? this->header_len : maxsize;

  memcpy(data, this->header, len);
  return len;
}

void pnm_close(pnm_t *p) {

  if (p->s >= 0) close(p->s);
  free(p->path);
  free(p->host);
  free(p->url);
  free(p);
}

