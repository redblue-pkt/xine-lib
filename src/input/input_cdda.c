/*
 * Copyright (C) 2000-2019 the xine project
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
 * Compact Disc Digital Audio (CDDA) Input Plugin
 *   by Mike Melanson (melanson@pcisys.net)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include <sys/types.h>
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_ALLOCA_H
#  include <alloca.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif
#ifdef WIN32
#  include <windows.h>
#  include <windef.h>
#  include <winioctl.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#include <signal.h>
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#include <basedir.h>

#define LOG_MODULE "input_cdda"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>
#include "../xine-engine/bswap.h"
#include "media_helper.h"

#if defined(__sun)
#define	DEFAULT_CDDA_DEVICE	"/vol/dev/aliases/cdrom0"
#elif defined(WIN32)
#define DEFAULT_CDDA_DEVICE "d:\\"
#elif defined(__OpenBSD__)
#define	DEFAULT_CDDA_DEVICE	"/dev/rcd0c"
#else
#define	DEFAULT_CDDA_DEVICE	"/dev/cdrom"
#endif

#define CDDB_SERVER             "freedb.freedb.org"
#define CDDB_PORT               8880
#define CDDB_PROTOCOL           6
#define CDDB_TIMEOUT            5000

/* CD-relevant defines and data structures */
#define CD_SECONDS_PER_MINUTE   60
#define CD_FRAMES_PER_SECOND    75
#define CD_RAW_FRAME_SIZE       2352
#define CD_LEADOUT_TRACK        0xAA
#define CD_BLOCK_OFFSET         150

typedef struct {
  int   track_mode;
  int   first_frame;
  int   first_frame_minute;
  int   first_frame_second;
  int   first_frame_frame;
  int   total_frames;
} cdrom_toc_entry_t;

typedef struct {
  int   first_track;
  int   last_track;
  int   total_tracks;
  int   ignore_last_track;
  /* really: first_track, ... , last_track, leadout_track. */
  cdrom_toc_entry_t toc_entries[1];
} cdrom_toc_t;

/*
 * Quick and dirty sha160 implementation.
 * We only will hash a few bytes per disc, so there is
 * no need for high end optimization.
 */

#define sha160_digest_len 20

typedef struct {
  uint8_t buf[64];
  uint32_t state[5];
  uint32_t n;
} sha160_t;

static void sha160_init (sha160_t *s) {
  s->state[0] = 0x67452301;
  s->state[1] = 0xefcdab89;
  s->state[2] = 0x98badcfe;
  s->state[3] = 0x10325476;
  s->state[4] = 0xc3d2e1f0;
  s->n = 0;
}

static void sha160_trans (sha160_t *s) {
  uint32_t l[80], a, b, c, d, e, i;
  a = s->state[0];
  b = s->state[1];
  c = s->state[2];
  d = s->state[3];
  e = s->state[4];
  for (i = 0; i < 16; i++) {
    uint32_t t;
    l[i] = t = _X_BE_32 (s->buf + i * 4);
    t += e + ((a << 5) | (a >> 27));
    t += ((b & (c ^ d)) ^ d) + 0x5a827999;
    e = d;
    d = c;
    c = (b << 30) | (b >> 2);
    b = a;
    a = t;
  }
  for (; i < 20; i++) {
    uint32_t t;
    t = l[i - 3] ^ l[i - 8] ^ l[i - 14] ^ l[i - 16];
    l[i] = t = (t << 1) | (t >> 31);
    t += e + ((a << 5) | (a >> 27));
    t += ((b & (c ^ d)) ^ d) + 0x5a827999;
    e = d;
    d = c;
    c = (b << 30) | (b >> 2);
    b = a;
    a = t;
  }
  for (; i < 40; i++) {
    uint32_t t;
    t = l[i - 3] ^ l[i - 8] ^ l[i - 14] ^ l[i - 16];
    l[i] = t = (t << 1) | (t >> 31);
    t += e + ((a << 5) | (a >> 27));
    t += (b ^ c ^ d) + 0x6ed9eba1;
    e = d;
    d = c;
    c = (b << 30) | (b >> 2);
    b = a;
    a = t;
  }
  for (; i < 60; i++) {
    uint32_t t;
    t = l[i - 3] ^ l[i - 8] ^ l[i - 14] ^ l[i - 16];
    l[i] = t = (t << 1) | (t >> 31);
    t += e + ((a << 5) | (a >> 27));
    t += (((b | c) & d) | (b & c)) + 0x8f1bbcdc;
    e = d;
    d = c;
    c = (b << 30) | (b >> 2);
    b = a;
    a = t;
  }
  for (; i < 80; i++) {
    uint32_t t;
    t = l[i - 3] ^ l[i - 8] ^ l[i - 14] ^ l[i - 16];
    l[i] = t = (t << 1) | (t >> 31);
    t += e + ((a << 5) | (a >> 27));
    t += (b ^ c ^ d) + 0xca62c1d6;
    e = d;
    d = c;
    c = (b << 30) | (b >> 2);
    b = a;
    a = t;
  }
  s->state[0] += a;
  s->state[1] += b;
  s->state[2] += c;
  s->state[3] += d;
  s->state[4] += e;
}

static void sha160_update (sha160_t *s, const uint8_t *data, size_t len) {
  while (len) {
    size_t part = 64 - (s->n & 63);
    if (part > len)
      part = len;
    memcpy (s->buf + (s->n & 63), data, part);
    data += part;
    s->n += part;
    if (!(s->n & 63))
      sha160_trans (s);
    len -= part;
  }
}

static void sha160_final (sha160_t *s, uint8_t *dest) {
  uint32_t p = s->n & 63;
  s->buf[p++] = 128;
  if (p == 64) {
    sha160_trans (s);
    p = 0;
  }
  memset (s->buf + p, 0, 64 - p);
  if (p >= 57) {
    sha160_trans (s);
    p = 0;
    memset (s->buf, 0, 64);
  }
  p = s->n << 3;
  s->buf[60] = p >> 24;
  s->buf[61] = p >> 16;
  s->buf[62] = p >> 8;
  s->buf[63] = p;
  sha160_trans (s);
  for (p = 0; p < 5; p++) {
    uint32_t v = s->state[p];
    dest[4 * p] = v >> 24;
    dest[4 * p + 1] = v >> 16;
    dest[4 * p + 2] = v >> 8;
    dest[4 * p + 3] = v;
  }
}

/**************************************************************************
 * xine interface functions
 *************************************************************************/

#define MAX_TRACKS     99
#define CACHED_FRAMES  90 /* be a multiple of 3, see read_block () */

typedef struct {
  int                  start;
  char                *title;
} trackinfo_t;

typedef struct {
  input_plugin_t       input_plugin;
  xine_stream_t       *stream;

  struct  {
    char              *cdiscid;
    char              *disc_title;
    char              *disc_year;
    char              *disc_artist;
    char              *disc_category;

    int                fd;
    uint32_t           disc_id;

    int                disc_length;
    trackinfo_t       *track;
    int                num_tracks;
    int                have_cddb_info;
  } cddb;

  int                  fd;
  int                  net_fd;
  int                  track;
  char                *mrl;
  int                  first_frame;
  int                  current_frame;
  int                  last_frame;

  char                *device;

  unsigned char        cache[CACHED_FRAMES][CD_RAW_FRAME_SIZE];
  int                  cache_first;
  int                  cache_last;
  int                  tripple;
  time_t               last_read_time;

#ifdef WIN32
  HANDLE  h_device_handle;   /* vcd device descriptor */
  HMODULE hASPI;             /* wnaspi32.dll */
  short i_sid;
  long  (*lpSendCommand)( void* );
#endif

} cdda_input_plugin_t;

typedef struct {

  input_class_t        input_class;

  xine_t              *xine;
  config_values_t     *config;

  pthread_mutex_t      mutex;

  time_t               last_read_time;

  cdrom_toc_t         *last_toc;
  const char          *cdda_device;

  int                  speed;
  const char          *cddb_server;
  int                  cddb_port;
  int                  cddb_error;
  int                  cddb_enable;

  char               **autoplaylist;

} cdda_input_class_t;


#ifdef WIN32

/* size of a CD sector */
#define CD_SECTOR_SIZE 2048

/* Win32 DeviceIoControl specifics */
typedef struct _TRACK_DATA {
    UCHAR Reserved;
    UCHAR Control : 4;
    UCHAR Adr : 4;
    UCHAR TrackNumber;
    UCHAR Reserved1;
    UCHAR Address[4];
} TRACK_DATA, *PTRACK_DATA;
typedef struct _CDROM_TOC {
    UCHAR Length[2];
    UCHAR FirstTrack;
    UCHAR LastTrack;
    TRACK_DATA TrackData[MAX_TRACKS+1];
} CDROM_TOC, *PCDROM_TOC;
typedef enum _TRACK_MODE_TYPE {
    YellowMode2,
    XAForm2,
    CDDA
} TRACK_MODE_TYPE, *PTRACK_MODE_TYPE;
typedef struct __RAW_READ_INFO {
    LARGE_INTEGER DiskOffset;
    ULONG SectorCount;
    TRACK_MODE_TYPE TrackMode;
} RAW_READ_INFO, *PRAW_READ_INFO;

#ifndef IOCTL_CDROM_BASE
#    define IOCTL_CDROM_BASE FILE_DEVICE_CD_ROM
#endif
#ifndef IOCTL_CDROM_READ_TOC
#    define IOCTL_CDROM_READ_TOC CTL_CODE(IOCTL_CDROM_BASE, 0x0000, \
                                          METHOD_BUFFERED, FILE_READ_ACCESS)
#endif
#ifndef IOCTL_CDROM_RAW_READ
#define IOCTL_CDROM_RAW_READ CTL_CODE(IOCTL_CDROM_BASE, 0x000F, \
                                      METHOD_OUT_DIRECT, FILE_READ_ACCESS)
#endif

/* Win32 aspi specific */
#define WIN_NT               ( GetVersion() < 0x80000000 )
#define ASPI_HAID           0
#define ASPI_TARGET         0
#define DTYPE_CDROM         0x05

#define SENSE_LEN           0x0E
#define SC_GET_DEV_TYPE     0x01
#define SC_EXEC_SCSI_CMD    0x02
#define SC_GET_DISK_INFO    0x06
#define SS_COMP             0x01
#define SS_PENDING          0x00
#define SS_NO_ADAPTERS      0xE8
#define SRB_DIR_IN          0x08
#define SRB_DIR_OUT         0x10
#define SRB_EVENT_NOTIFY    0x40

#define READ_CD 0xbe
#define SECTOR_TYPE_MODE2 0x14
#define READ_CD_USERDATA_MODE2 0x10

#define READ_TOC 0x43
#define READ_TOC_FORMAT_TOC 0x0

#pragma pack(1)

struct SRB_GetDiskInfo
{
    unsigned char   SRB_Cmd;
    unsigned char   SRB_Status;
    unsigned char   SRB_HaId;
    unsigned char   SRB_Flags;
    unsigned long   SRB_Hdr_Rsvd;
    unsigned char   SRB_Target;
    unsigned char   SRB_Lun;
    unsigned char   SRB_DriveFlags;
    unsigned char   SRB_Int13HDriveInfo;
    unsigned char   SRB_Heads;
    unsigned char   SRB_Sectors;
    unsigned char   SRB_Rsvd1[22];
};

struct SRB_GDEVBlock
{
    unsigned char SRB_Cmd;
    unsigned char SRB_Status;
    unsigned char SRB_HaId;
    unsigned char SRB_Flags;
    unsigned long SRB_Hdr_Rsvd;
    unsigned char SRB_Target;
    unsigned char SRB_Lun;
    unsigned char SRB_DeviceType;
    unsigned char SRB_Rsvd1;
};

struct SRB_ExecSCSICmd
{
    unsigned char   SRB_Cmd;
    unsigned char   SRB_Status;
    unsigned char   SRB_HaId;
    unsigned char   SRB_Flags;
    unsigned long   SRB_Hdr_Rsvd;
    unsigned char   SRB_Target;
    unsigned char   SRB_Lun;
    unsigned short  SRB_Rsvd1;
    unsigned long   SRB_BufLen;
    unsigned char   *SRB_BufPointer;
    unsigned char   SRB_SenseLen;
    unsigned char   SRB_CDBLen;
    unsigned char   SRB_HaStat;
    unsigned char   SRB_TargStat;
    unsigned long   *SRB_PostProc;
    unsigned char   SRB_Rsvd2[20];
    unsigned char   CDBByte[16];
    unsigned char   SenseArea[SENSE_LEN+2];
};

#pragma pack()

#endif /* WIN32 */

static void print_cdrom_toc (xine_t *xine, cdrom_toc_t *toc) {
  cdrom_toc_entry_t *e;

  if (xine->verbosity < XINE_VERBOSITY_DEBUG)
    return;

  e = &toc->toc_entries[0];

  xprintf (xine, XINE_VERBOSITY_DEBUG,
    "input_cdda: toc: first_track = %d, last_track = %d, total_tracks = %d.\n",
    toc->first_track, toc->last_track, toc->total_tracks);

  /* fetch each toc entry */
  if (toc->first_track > 0) {
    int i;
    xprintf (xine, XINE_VERBOSITY_DEBUG,
      "input_cdda: track  mode  MSF            time    first_frame\n");
    for (i = 0; i < toc->total_tracks; i++) {
      int time1, time2, timediff;
      time1 = e[i].first_frame_minute * 60 + e[i].first_frame_second;
      time2 = e[i + 1].first_frame_minute * 60 + e[i + 1].first_frame_second;
      timediff = time2 - time1;
      xprintf (xine, XINE_VERBOSITY_DEBUG,
        "input_cdda: %5d  %4d  %02d:%02d:%02d       %02d:%02d   %11d\n",
        toc->first_track + i,
        e[i].track_mode, 
        e[i].first_frame_minute,
        e[i].first_frame_second,
        e[i].first_frame_frame,
        timediff / 60,
        timediff % 60,
        e[i].first_frame);
    }
    xprintf (xine, XINE_VERBOSITY_DEBUG,
      "input_cdda: leadout%4d  %02d:%02d:%02d               %11d\n",
      e[i].track_mode,
      e[i].first_frame_minute,
      e[i].first_frame_second,
      e[i].first_frame_frame,
      e[i].first_frame);
  }
}

static void free_cdrom_toc(cdrom_toc_t *toc) {
  free (toc);
}

#if defined (__linux__)

#include <linux/cdrom.h>

static cdrom_toc_t *read_cdrom_toc (int fd) {

  cdrom_toc_t *toc;
  struct cdrom_tochdr tochdr;
  struct cdrom_tocentry tocentry;
  struct cdrom_multisession ms;
  int i, first_track, last_track, total_tracks, ignore_last_track;

  /* fetch the table of contents */
  if (ioctl(fd, CDROMREADTOCHDR, &tochdr) == -1) {
    perror("CDROMREADTOCHDR");
    return NULL;
  }

  ms.addr_format = CDROM_LBA;
  if (ioctl(fd, CDROMMULTISESSION, &ms) == -1) {
    perror("CDROMMULTISESSION");
    return NULL;
  }

  first_track = tochdr.cdth_trk0;
  last_track  = tochdr.cdth_trk1;
  if (last_track > first_track + MAX_TRACKS - 1)
    last_track = first_track + MAX_TRACKS - 1;
  total_tracks = last_track - first_track + 1;
  ignore_last_track = ms.xa_flag ? 1 : 0;

  /* allocate space for the toc entries */
  toc = calloc (1, sizeof (cdrom_toc_t) + total_tracks * sizeof (cdrom_toc_entry_t));
  if (!toc) {
    perror("calloc");
    return NULL;
  }
  toc->first_track = first_track;
  toc->last_track  = last_track;
  toc->total_tracks = total_tracks;
  toc->ignore_last_track = ignore_last_track;

  /* fetch each toc entry */
  for (i = 0; i < toc->total_tracks; i++) {

    memset(&tocentry, 0, sizeof(tocentry));

    tocentry.cdte_track = toc->first_track + i;
    tocentry.cdte_format = CDROM_MSF;
    if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
      perror("CDROMREADTOCENTRY");
      break;
    }

    toc->toc_entries[i].track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
    toc->toc_entries[i].first_frame_minute = tocentry.cdte_addr.msf.minute;
    toc->toc_entries[i].first_frame_second = tocentry.cdte_addr.msf.second;
    toc->toc_entries[i].first_frame_frame = tocentry.cdte_addr.msf.frame;
    toc->toc_entries[i].first_frame =
      (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.cdte_addr.msf.frame;
  }

  /* fetch the leadout as well */
  memset(&tocentry, 0, sizeof(tocentry));

  tocentry.cdte_track = CD_LEADOUT_TRACK;
  tocentry.cdte_format = CDROM_MSF;
  if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
    perror("CDROMREADTOCENTRY");
    free (toc);
    return NULL;
  }

#define XA_INTERVAL ((60 + 90 + 2) * CD_FRAMES)

  toc->toc_entries[i].track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
  toc->toc_entries[i].first_frame_minute = tocentry.cdte_addr.msf.minute;
  toc->toc_entries[i].first_frame_second = tocentry.cdte_addr.msf.second;
  toc->toc_entries[i].first_frame_frame = tocentry.cdte_addr.msf.frame;
  if (!ms.xa_flag) {
    toc->toc_entries[i].first_frame =
      (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.cdte_addr.msf.frame;
  } else {
    toc->toc_entries[i].first_frame = ms.addr.lba - XA_INTERVAL + 150;
  }

  return toc;
}

static int read_cdrom_frames(cdda_input_plugin_t *this_gen, int frame, int num_frames,
  unsigned char *data) {

  int fd = this_gen->fd;
  struct cdrom_msf msf;

  while( num_frames ) {
    /* read from starting frame... */
    msf.cdmsf_min0 = frame / CD_SECONDS_PER_MINUTE / CD_FRAMES_PER_SECOND;
    msf.cdmsf_sec0 = (frame / CD_FRAMES_PER_SECOND) % CD_SECONDS_PER_MINUTE;
    msf.cdmsf_frame0 = frame % CD_FRAMES_PER_SECOND;

    /* read until ending track (starting frame + 1)... */
    msf.cdmsf_min1 = (frame + 1) / CD_SECONDS_PER_MINUTE / CD_FRAMES_PER_SECOND;
    msf.cdmsf_sec1 = ((frame + 1) / CD_FRAMES_PER_SECOND) % CD_SECONDS_PER_MINUTE;
    msf.cdmsf_frame1 = (frame + 1) % CD_FRAMES_PER_SECOND;

    /* MSF structure is the input to the ioctl */
    memcpy(data, &msf, sizeof(msf));

    /* read a frame */
    if(ioctl(fd, CDROMREADRAW, data, data) < 0) {
      perror("CDROMREADRAW");
      return -1;
    }

    data += CD_RAW_FRAME_SIZE;
    frame++;
    num_frames--;
  }
  return 0;
}

#elif defined(__sun)

#include <sys/cdio.h>

static cdrom_toc_t *read_cdrom_toc (int fd) {

  cdrom_toc_t *toc;
  struct cdrom_tochdr tochdr;
  struct cdrom_tocentry tocentry;
  int i, first_track, last_track, total_tracks;

  /* fetch the table of contents */
  if (ioctl(fd, CDROMREADTOCHDR, &tochdr) == -1) {
    perror("CDROMREADTOCHDR");
    return NULL;
  }

  first_track = tochdr.cdth_trk0;
  last_track  = tochdr.cdth_trk1;
  if (last_track > first_track + MAX_TRACKS - 1)
    last_track = first_track + MAX_TRACKS - 1;
  total_tracks = last_track - first_track + 1;

  /* allocate space for the toc entries */
  toc = calloc (1, sizeof (cdrom_toc_t) + total_tracks * sizeof (cdrom_toc_entry_t));
  if (!toc) {
    perror("calloc");
    return NULL;
  }
  toc->first_track = first_track;
  toc->last_track  = last_track;
  toc->total_tracks = total_tracks;

  /* fetch each toc entry */
  for (i = 0; i < toc->total_tracks; i++) {

    memset(&tocentry, 0, sizeof(tocentry));

    tocentry.cdte_track = toc->first_track + i;
    tocentry.cdte_format = CDROM_MSF;
    if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
      perror("CDROMREADTOCENTRY");
      free (toc);
      return NULL;
    }

    toc->toc_entries[i].track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
    toc->toc_entries[i].first_frame_minute = tocentry.cdte_addr.msf.minute;
    toc->toc_entries[i].first_frame_second = tocentry.cdte_addr.msf.second;
    toc->toc_entries[i].first_frame_frame = tocentry.cdte_addr.msf.frame;
    toc->toc_entries[i].first_frame =
      (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.cdte_addr.msf.frame;
  }

  if (tocentry.cdte_ctrl & CDROM_DATA_TRACK) {
    toc->ignore_last_track = 1;
  }
  else {
    toc->ignore_last_track = 0;
  }

  /* fetch the leadout as well */
  memset(&tocentry, 0, sizeof(tocentry));

  tocentry.cdte_track = CD_LEADOUT_TRACK;
  tocentry.cdte_format = CDROM_MSF;
  if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
    perror("CDROMREADTOCENTRY");
    free (toc);
    return NULL;
  }

  toc->toc_entries[i].track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
  toc->toc_entries[i].first_frame_minute = tocentry.cdte_addr.msf.minute;
  toc->toc_entries[i].first_frame_second = tocentry.cdte_addr.msf.second;
  toc->toc_entries[i].first_frame_frame = tocentry.cdte_addr.msf.frame;
  toc->toc_entries[i].first_frame =
    (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
     tocentry.cdte_addr.msf.frame;

  return toc;
}

static int read_cdrom_frames(cdda_input_plugin_t *this_gen, int frame, int num_frames,
  unsigned char *data) {

  int fd = this_gen->fd;
  struct cdrom_cdda cdda;

  while( num_frames ) {
    cdda.cdda_addr = frame - 2 * CD_FRAMES_PER_SECOND;
    cdda.cdda_length = 1;
    cdda.cdda_data = data;
    cdda.cdda_subcode = CDROM_DA_NO_SUBCODE;

    /* read a frame */
    if(ioctl(fd, CDROMCDDA, &cdda) < 0) {
      perror("CDROMCDDA");
      return -1;
    }

    data += CD_RAW_FRAME_SIZE;
    frame++;
    num_frames--;
  }
  return 0;
}

#elif defined(__FreeBSD_kernel__) || defined(__NetBSD__) || defined(__OpenBSD__)

#include <sys/cdio.h>

#ifdef HAVE_SYS_SCSIIO_H
#include <sys/scsiio.h>
#endif

static cdrom_toc_t *read_cdrom_toc (int fd) {

  cdrom_toc_t *toc;
  struct ioc_toc_header tochdr;
#if defined(__FreeBSD_kernel__)
  struct ioc_read_toc_single_entry tocentry;
#elif defined(__NetBSD__) || defined(__OpenBSD__)
  struct ioc_read_toc_entry tocentry;
  struct cd_toc_entry data;
#endif
  int i, first_track, last_track, total_tracks;

  /* fetch the table of contents */
  if (ioctl(fd, CDIOREADTOCHEADER, &tochdr) == -1) {
    perror("CDIOREADTOCHEADER");
    return NULL;
  }

  first_track = tochdr.starting_track;
  last_track  = tochdr.ending_track;
  if (last_track > first_track + MAX_TRACKS - 1)
    last_track = first_track + MAX_TRACKS - 1;
  total_tracks = last_track - first_track + 1;

  /* allocate space for the toc entries */
  toc = calloc (1, sizeof (cdrom_toc_t) + total_tracks * sizeof (cdrom_toc_entry_t));
  if (!toc) {
    perror("calloc");
    return NULL;
  }
  toc->first_track = first_track;
  toc->last_track  = last_track;
  toc->total_tracks = total_tracks;

  /* fetch each toc entry */
  for (i = 0; i < toc->total_tracks; i++) {

    memset(&tocentry, 0, sizeof(tocentry));

#if defined(__FreeBSD_kernel__)
    tocentry.track = toc->first_track + i;
    tocentry.address_format = CD_MSF_FORMAT;
    if (ioctl(fd, CDIOREADTOCENTRY, &tocentry) == -1) {
      perror("CDIOREADTOCENTRY");
      free (toc);
      return NULL;
    }
#elif defined(__NetBSD__) || defined(__OpenBSD__)
    memset(&data, 0, sizeof(data));
    tocentry.data_len = sizeof(data);
    tocentry.data = &data;
    tocentry.starting_track = toc->first_track + i;
    tocentry.address_format = CD_MSF_FORMAT;
    if (ioctl(fd, CDIOREADTOCENTRYS, &tocentry) == -1) {
      perror("CDIOREADTOCENTRYS");
      free (toc);
      return NULL;
    }
#endif

#if defined(__FreeBSD_kernel__)
    toc->toc_entries[i].track_mode = (tocentry.entry.control & 0x04) ? 1 : 0;
    toc->toc_entries[i].first_frame_minute = tocentry.entry.addr.msf.minute;
    toc->toc_entries[i].first_frame_second = tocentry.entry.addr.msf.second;
    toc->toc_entries[i].first_frame_frame = tocentry.entry.addr.msf.frame;
    toc->toc_entries[i].first_frame =
      (tocentry.entry.addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.entry.addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.entry.addr.msf.frame;
#elif defined(__NetBSD__) || defined(__OpenBSD__)
    toc->toc_entries[i].track_mode = (tocentry.data->control & 0x04) ? 1 : 0;
    toc->toc_entries[i].first_frame_minute = tocentry.data->addr.msf.minute;
    toc->toc_entries[i].first_frame_second = tocentry.data->addr.msf.second;
    toc->toc_entries[i].first_frame_frame = tocentry.data->addr.msf.frame;
    toc->toc_entries[i].first_frame =
      (tocentry.data->addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.data->addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.data->addr.msf.frame - CD_BLOCK_OFFSET;
#endif
  }

  /* fetch the leadout as well */
  memset(&tocentry, 0, sizeof(tocentry));

#if defined(__FreeBSD_kernel__)
  tocentry.track = CD_LEADOUT_TRACK;
  tocentry.address_format = CD_MSF_FORMAT;
  if (ioctl(fd, CDIOREADTOCENTRY, &tocentry) == -1) {
    perror("CDIOREADTOCENTRY");
    free (toc);
    return NULL;
  }
#elif defined(__NetBSD__) || defined(__OpenBSD__)
  memset(&data, 0, sizeof(data));
  tocentry.data_len = sizeof(data);
  tocentry.data = &data;
  tocentry.starting_track = CD_LEADOUT_TRACK;
  tocentry.address_format = CD_MSF_FORMAT;
  if (ioctl(fd, CDIOREADTOCENTRYS, &tocentry) == -1) {
    perror("CDIOREADTOCENTRYS");
    free (toc);
    return NULL;
  }
#endif

#if defined(__FreeBSD_kernel__)
  toc->toc_entries[i].track_mode = (tocentry.entry.control & 0x04) ? 1 : 0;
  toc->toc_entries[i].first_frame_minute = tocentry.entry.addr.msf.minute;
  toc->toc_entries[i].first_frame_second = tocentry.entry.addr.msf.second;
  toc->toc_entries[i].first_frame_frame = tocentry.entry.addr.msf.frame;
  toc->toc_entries[i].first_frame =
    (tocentry.entry.addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (tocentry.entry.addr.msf.second * CD_FRAMES_PER_SECOND) +
     tocentry.entry.addr.msf.frame;
#elif defined(__NetBSD__) || defined(__OpenBSD__)
  toc->toc_entries[i].track_mode = (tocentry.data->control & 0x04) ? 1 : 0;
  toc->toc_entries[i].first_frame_minute = tocentry.data->addr.msf.minute;
  toc->toc_entries[i].first_frame_second = tocentry.data->addr.msf.second;
  toc->toc_entries[i].first_frame_frame = tocentry.data->addr.msf.frame;
  toc->toc_entries[i].first_frame =
    (tocentry.data->addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (tocentry.data->addr.msf.second * CD_FRAMES_PER_SECOND) +
     tocentry.data->addr.msf.frame - CD_BLOCK_OFFSET;
#endif

  return toc;
}

static int read_cdrom_frames(cdda_input_plugin_t *this_gen, int frame, int num_frames,
  unsigned char *data) {

  int fd = this_gen->fd;

  while( num_frames ) {
#if defined(__FreeBSD_kernel__)
#if __FreeBSD_kernel_version < 501106
    struct ioc_read_audio cdda;

    cdda.address_format = CD_MSF_FORMAT;
    cdda.address.msf.minute = frame / CD_SECONDS_PER_MINUTE / CD_FRAMES_PER_SECOND;
    cdda.address.msf.second = (frame / CD_FRAMES_PER_SECOND) % CD_SECONDS_PER_MINUTE;
    cdda.address.msf.frame = frame % CD_FRAMES_PER_SECOND;
    cdda.nframes = 1;
    cdda.buffer = data;
    /* read a frame */
    if (ioctl(fd, CDIOCREADAUDIO, &cdda) < 0)
#else
    if (pread (fd, data, CD_RAW_FRAME_SIZE, frame * CD_RAW_FRAME_SIZE) != CD_RAW_FRAME_SIZE)
#endif
    {
      perror("CDIOCREADAUDIO");
      return -1;
    }
#elif defined(__NetBSD__) || defined(__OpenBSD__)
    scsireq_t req;
    int nblocks = 1;

    memset(&req, 0, sizeof(req));
    req.cmd[0] = 0xbe;
    req.cmd[1] = 0;
    req.cmd[2] = (frame >> 24) & 0xff;
    req.cmd[3] = (frame >> 16) & 0xff;
    req.cmd[4] = (frame >> 8) & 0xff;
    req.cmd[5] = (frame >> 0) & 0xff;
    req.cmd[6] = (nblocks >> 16) & 0xff;
    req.cmd[7] = (nblocks >> 8) & 0xff;
    req.cmd[8] = (nblocks >> 0) & 0xff;
    req.cmd[9] = 0x78;
    req.cmdlen = 10;

    req.datalen = nblocks * CD_RAW_FRAME_SIZE;
    req.databuf = data;
    req.timeout = 10000;
    req.flags = SCCMD_READ;

    if(ioctl(fd, SCIOCCOMMAND, &req) < 0) {
      perror("SCIOCCOMMAND");
      return -1;
    }
#endif

    data += CD_RAW_FRAME_SIZE;
    frame++;
    num_frames--;
  }
  return 0;
}

#elif defined(WIN32)

static cdrom_toc_t *read_cdrom_toc (cdda_input_plugin_t *this_gen) {

  if( this_gen->hASPI )
  {
    /* This is for ASPI which obviously isn't supported! */
    lprintf("Windows ASPI support is not complete yet!\n");
    return NULL;

  }

      cdrom_toc_t *toc;
      DWORD dwBytesReturned;
      CDROM_TOC cdrom_toc;
      int i, first_track, last_track, total_tracks;

      if( DeviceIoControl( this_gen->h_device_handle,
			   IOCTL_CDROM_READ_TOC,
			   NULL, 0, &cdrom_toc, sizeof(CDROM_TOC),
			   &dwBytesReturned, NULL ) == 0 )
	{
#ifdef LOG
	  DWORD dw;
	  printf( "input_cdda: could not read TOCHDR\n" );
	  dw = GetLastError();
	  printf("GetLastError returned %u\n", dw);
#endif
	  return NULL;
	}

      first_track = cdrom_toc.FirstTrack;
      last_track  = cdrom_toc.LastTrack;
      if (last_track > first_track + MAX_TRACKS - 1)
        last_track = first_track + MAX_TRACKS - 1;
      total_tracks = last_track - first_track + 1;


      /* allocate space for the toc entries */
      toc = calloc (1, sizeof (cdrom_toc_t) + total_tracks * sizeof (cdrom_toc_entry_t));
      if (!toc) {
          perror("calloc");
          return NULL;
      }
      toc->first_track = first_track;
      toc->last_track  = last_track;
      toc->total_tracks = total_tracks;


      /* fetch each toc entry */
      /* Grab the leadout track too! (I think that this is correct?) */
      for (i = 0; i <= toc->total_tracks; i++) {

          toc->toc_entries[i].track_mode = (cdrom_toc.TrackData[i].Control & 0x04) ? 1 : 0;
          toc->toc_entries[i].first_frame_minute = cdrom_toc.TrackData[i].Address[1];
          toc->toc_entries[i].first_frame_second = cdrom_toc.TrackData[i].Address[2];
          toc->toc_entries[i].first_frame_frame = cdrom_toc.TrackData[i].Address[3];

          toc->toc_entries[i].first_frame =
              (toc->toc_entries[i].first_frame_minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
              (toc->toc_entries[i].first_frame_second * CD_FRAMES_PER_SECOND) +
              toc->toc_entries[i].first_frame_frame;
      }

  return toc;
}


static int read_cdrom_frames(cdda_input_plugin_t *this_gen, int frame, int num_frames,
  unsigned char *data) {

  DWORD dwBytesReturned;
  RAW_READ_INFO raw_read_info;

  if( this_gen->hASPI )
  {
	  /* This is for ASPI which obviously isn't supported! */
    lprintf("Windows ASPI support is not complete yet!\n");
    return -1;

  }
  else
    {
      memset(data, 0, CD_RAW_FRAME_SIZE * num_frames);

      while( num_frames ) {

#ifdef LOG
	/*printf("\t Raw read frame %d\n", frame);*/
#endif
	raw_read_info.DiskOffset.QuadPart = frame * CD_SECTOR_SIZE;
	raw_read_info.SectorCount = 1;
	raw_read_info.TrackMode = CDDA;

	/* read a frame */
	if( DeviceIoControl( this_gen->h_device_handle,
			     IOCTL_CDROM_RAW_READ,
			     &raw_read_info, sizeof(RAW_READ_INFO), data,
			     CD_RAW_FRAME_SIZE,
			     &dwBytesReturned, NULL ) == 0 )
	  {
#ifdef LOG
	    DWORD dw;
	    printf( "input_cdda: could not read frame\n" );
	    dw = GetLastError();
	    printf("GetLastError returned %u\n", dw);
#endif
	    return -1;
	  }

	data += CD_RAW_FRAME_SIZE;
	frame++;
	num_frames--;
      }
    }
  return 0;
}

#else



static cdrom_toc_t *read_cdrom_toc (int fd) {
  /* read_cdrom_toc is not supported on other platforms */
  return NULL;
}


static int read_cdrom_frames(cdda_input_plugin_t *this_gen, int frame, int num_frames,
  unsigned char *data) {
  return -1;
}

#endif


/**************************************************************************
 * network support functions. plays audio cd over the network.
 * see xine-lib/misc/cdda_server.c for the server application
 *************************************************************************/

#define _BUFSIZ 300


#ifndef WIN32
static int parse_url (char *urlbuf, char** host, int *port) {
  char   *start = NULL;
  char   *portcolon = NULL;

  if (host != NULL)
    *host = NULL;

  if (port != NULL)
    *port = 0;

  start = strstr(urlbuf, "://");
  if (start != NULL)
    start += 3;
  else
    start = urlbuf;

  while( *start == '/' )
    start++;

  portcolon = strchr(start, ':');

  if (host != NULL)
    *host = start;

  if (portcolon != NULL)
  {
    *portcolon = '\0';

    if (port != NULL)
        *port = atoi(portcolon + 1);
  }

  return 0;
}
#endif

static int XINE_FORMAT_PRINTF(4, 5)
network_command( xine_stream_t *stream, int socket, void *data_buf, const char *msg, ...)
{
  char     buf[_BUFSIZ];
  va_list  args;
  int      ret, n;

  va_start(args, msg);
  vsnprintf(buf, _BUFSIZ - 1, msg, args);
  va_end(args);

  /* Each line sent is '\n' terminated */
  if( buf[strlen(buf) - 1] != '\n' )
    strcat(buf, "\n");

  if( _x_io_tcp_write(stream, socket, buf, strlen(buf)) < (int)strlen(buf) )
  {
    if (stream)
      xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input_cdda: error writing to socket.\n");
    return -1;
  }

  if (_x_io_tcp_read_line(stream, socket, buf, _BUFSIZ) <= 0)
  {
    if (stream)
      xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input_cdda: error reading from socket.\n");
    return -1;
  }

  sscanf(buf, "%d %d", &ret, &n );

  if( n ) {
    if( !data_buf ) {
      if (stream)
        xprintf(stream->xine, XINE_VERBOSITY_DEBUG,
                "input_cdda: protocol error, data returned but no buffer provided.\n");
      return -1;
    }
      if ( _x_io_tcp_read(stream, socket, data_buf, n) < n )
      return -1;
  } else if ( data_buf ) {

    strcpy( data_buf, buf );
  }

  return ret;
}


#ifndef WIN32
static int network_connect(xine_stream_t *stream, const char *got_url )
{
  char *host;
  int port;
  int fd;

  char *url = strdup(got_url);
  parse_url(url, &host, &port);

  if( !host || !strlen(host) || !port )
  {
    free(url);
    return -1;
  }

  fd = _x_io_tcp_connect(stream, host, port);
  lprintf("TTTcosket=%d\n", fd);
  free(url);

  if( fd != -1 ) {
    if( network_command(stream, fd, NULL, "cdda_open") < 0 ) {
      xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input_cdda: error opening remote drive.\n");
      close(fd);
      return -1;
    }
  }
  return fd;
}

static cdrom_toc_t *network_read_cdrom_toc (xine_stream_t *stream, int fd) {

  char buf[_BUFSIZ];
  cdrom_toc_t *toc;
  int i, first_track, last_track, total_tracks;

  /* fetch the table of contents */
  if( network_command(stream, fd, buf, "cdda_tochdr" ) == -1) {
    if (stream)
      xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input_cdda: network CDROMREADTOCHDR error.\n");
    return NULL;
  }

  sscanf (buf,"%*s %*s %d %d", &first_track, &last_track);
  if (last_track > first_track + MAX_TRACKS - 1)
    last_track = first_track + MAX_TRACKS - 1;
  total_tracks = last_track - first_track + 1;

  /* allocate space for the toc entries */
  toc = calloc (1, sizeof (cdrom_toc_t) + total_tracks * sizeof (cdrom_toc_entry_t));
  if (!toc) {
    perror("calloc");
    return NULL;
  }
  toc->first_track  = first_track;
  toc->last_track   = last_track;
  toc->total_tracks = total_tracks;

  /* fetch each toc entry */
  for (i = 0; i < toc->total_tracks; i++) {

    /* fetch the table of contents */
    if (network_command (stream, fd, buf, "cdda_tocentry %d", toc->first_track + i) == -1) {
      if (stream)
        xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input_cdda: network CDROMREADTOCENTRY error.\n");
      free (toc);
      return NULL;
    }

    sscanf (buf, "%*s %*s %d %d %d %d",
      &toc->toc_entries[i].track_mode,
      &toc->toc_entries[i].first_frame_minute,
      &toc->toc_entries[i].first_frame_second,
      &toc->toc_entries[i].first_frame_frame);

    toc->toc_entries[i].first_frame =
      (toc->toc_entries[i].first_frame_minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (toc->toc_entries[i].first_frame_second * CD_FRAMES_PER_SECOND) +
       toc->toc_entries[i].first_frame_frame;
  }

  /* fetch the leadout as well */
  if( network_command( stream, fd, buf, "cdda_tocentry %d", CD_LEADOUT_TRACK ) == -1) {
    if (stream)
      xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input_cdda: network CDROMREADTOCENTRY error.\n");
    free (toc);
    return NULL;
  }

  sscanf (buf, "%*s %*s %d %d %d %d",
    &toc->toc_entries[i].track_mode,
    &toc->toc_entries[i].first_frame_minute,
    &toc->toc_entries[i].first_frame_second,
    &toc->toc_entries[i].first_frame_frame);

  toc->toc_entries[i].first_frame =
    (toc->toc_entries[i].first_frame_minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (toc->toc_entries[i].first_frame_second * CD_FRAMES_PER_SECOND) +
     toc->toc_entries[i].first_frame_frame;

  return toc;
}
#endif /* WIN32 */


static int network_read_cdrom_frames(xine_stream_t *stream, int fd, int first_frame, int num_frames,
  unsigned char data[CD_RAW_FRAME_SIZE]) {

  return network_command( stream, fd, data, "cdda_read %d %d", first_frame, num_frames );
}



/*
 * **************** CDDB *********************
 */
/*
 * Config callbacks
 */
static void cdda_device_cb (void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *)data;
  pthread_mutex_lock (&class->mutex);
  class->cdda_device = cfg->str_value;
  pthread_mutex_unlock (&class->mutex);
}

static void enable_cddb_changed_cb (void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *)data;
  pthread_mutex_lock (&class->mutex);
  if (class->cddb_enable != cfg->num_value) {
    class->cddb_enable = cfg->num_value;
    class->cddb_error = 0;
  }
  pthread_mutex_unlock (&class->mutex);
}

static void server_changed_cb (void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *)data;
  pthread_mutex_lock (&class->mutex);
  if (!class->cddb_server || (strcmp (class->cddb_server, cfg->str_value) != 0)) {
    class->cddb_server = cfg->str_value;
    class->cddb_error = 0;
  }
  pthread_mutex_unlock (&class->mutex);
}

static void port_changed_cb (void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *)data;
  pthread_mutex_lock (&class->mutex);
  if (class->cddb_port != cfg->num_value) {
    class->cddb_port = cfg->num_value;
    class->cddb_error = 0;
  }
  pthread_mutex_unlock (&class->mutex);
}

#ifdef CDROM_SELECT_SPEED
static void speed_changed_cb (void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *)data;
  pthread_mutex_lock (&class->mutex);
  class->speed = cfg->num_value;
  pthread_mutex_unlock (&class->mutex);
}
#endif

/*
 * Return 1 if CD has been changed, 0 of not, -1 on error.
 */
static int _cdda_is_cd_changed(cdda_input_plugin_t *this) {
#ifdef CDROM_MEDIA_CHANGED
  int err, cd_changed=0;

  if(this == NULL || this->fd < 0)
    return -1;

  if ((err = ioctl(this->fd, CDROM_MEDIA_CHANGED, cd_changed)) < 0) {
    int e = errno;
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "input_cdda: ioctl(CDROM_MEDIA_CHANGED) failed: %s.\n", strerror(e));
    return -1;
  }

  switch(err) {
  case 1:
    return 1;
    break;

  default:
    return 0;
    break;
  }

  return -1;
#else
  /*
   * At least on solaris, CDROM_MEDIA_CHANGED does not exist. Just return an
   * error for now
   */
  return -1;
#endif
}

/*
 * create a directory, in safe mode
 */
static void _cdda_mkdir_safe(xine_t *xine, char *path) {

  if(path == NULL)
    return;

#ifndef WIN32
  {
    struct stat  pstat;

    if((stat(path, &pstat)) < 0) {
      /* file or directory no exist, create it */
      if(mkdir(path, 0755) < 0) {
        int e = errno;
        xprintf (xine, XINE_VERBOSITY_DEBUG,
          "input_cdda: mkdir(%s) failed: %s.\n", path, strerror(e));
        return;
      }
    }
    else {
      /* Check of found file is a directory file */
      if(!S_ISDIR(pstat.st_mode)) {
	xprintf(xine, XINE_VERBOSITY_DEBUG, "input_cdda: %s is not a directory.\n", path);
      }
    }
  }
#else
  {
    HANDLE          hList;
    TCHAR           szDir[MAX_PATH+3];
    WIN32_FIND_DATA FileData;

    // Get the proper directory path
    snprintf_buf(szDir, "%s\\*", path);

    // Get the first file
    hList = FindFirstFile(szDir, &FileData);
    if (hList == INVALID_HANDLE_VALUE)
      {
	if(mkdir(path, 0) != 0) {
	  xprintf(xine, XINE_VERBOSITY_DEBUG, "input_cdda: mkdir(%s) failed.\n", path);
	  return;
	}
      }

    FindClose(hList);
  }
#endif /* WIN32 */
}

/*
 * Make recursive directory creation (given an absolute pathname)
 */
static void _cdda_mkdir_recursive_safe (xine_t *xine, char *path)
{
  if (!path)
    return;

  char buf[strlen (path) + 1];
  strcpy (buf, path);
  char *p = strchr (buf, '/');
  if (!p)
    p = buf;

  do
  {
    while (*p++ == '/') /**/;
    p = strchr (p, '/');
    if (p)
      *p = 0;
    _cdda_mkdir_safe (xine, buf);
    if (p)
      *p = '/';
  } while (p);
}

/*
 * Read from socket, fill char *s, return size length.
 */
static int _cdda_cddb_socket_read(cdda_input_plugin_t *this, char *str, int size) {
  int ret;
  ret = _x_io_tcp_read_line(this->stream, this->cddb.fd, str, size);

  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "<<< %s\n", str);

  return ret;
}

/*
 * Send a command to socket
 */
static int _cdda_cddb_send_command(cdda_input_plugin_t *this, char *cmd) {

  if((this == NULL) || (this->cddb.fd < 0) || (cmd == NULL))
    return -1;

  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, ">>> %s\n", cmd);

  return (int)_x_io_tcp_write(this->stream, this->cddb.fd, cmd, strlen(cmd));
}

/*
 * Handle return code od a command result.
 */
static int _cdda_cddb_handle_code(char *buf) {
  int  rcode, fdig, sdig, /*tdig,*/ err;

  err = -999;

  if (sscanf(buf, "%d", &rcode) == 1) {

    fdig = rcode / 100;
    sdig = (rcode - (fdig * 100)) / 10;
    /*tdig = (rcode - (fdig * 100) - (sdig * 10));*/

    /*
    printf(" %d--\n", fdig);
    printf(" -%d-\n", sdig);
    printf(" --%d\n", tdig);
    */

    err = rcode;

    switch (fdig) {
    case 1:
      /* printf("Informative message\n"); */
      break;
    case 2:
      /* printf("Command OK\n"); */
      break;
    case 3:
      /* printf("Command OK so far, continue\n"); */
      break;
    case 4:
      /* printf("Command OK, but cannot be performed for some specified reasons\n"); */
      err = 0 - rcode;
      break;
    case 5:
      /* printf("Command unimplemented, incorrect, or program error\n"); */
      err = 0 - rcode;
      break;
    default:
      /* printf("Unhandled case %d\n", fdig); */
      err = 0 - rcode;
      break;
    }

    switch (sdig) {
    case 0:
      /* printf("Ready for further commands\n"); */
      break;
    case 1:
      /* printf("More server-to-client output follows (until terminating marker)\n"); */
      break;
    case 2:
      /* printf("More client-to-server input follows (until terminating marker)\n"); */
      break;
    case 3:
      /* printf("Connection will close\n"); */
      err = 0 - rcode;
      break;
    default:
      /* printf("Unhandled case %d\n", sdig); */
      err = 0 - rcode;
      break;
    }
  }

  return err;
}

static inline char *_cdda_append (/*const*/ char *first, const char *second)
{
  if (!first)
    return strdup (second);

  char *result = (char *) realloc (first, strlen (first) + strlen (second) + 1);
  strcat (result, second);
  return result;
}

static void _cdda_parse_cddb_info (cdda_input_plugin_t *this, char *buffer, char **dtitle)
{
  /* buffer should be no more than 2048 bytes... */
  char buf[2048];
  int track_no;

  if (sscanf (buffer, "DTITLE=%s", &buf[0]) == 1) {
    char *pt = strchr (buffer, '=');
    if (pt) {
      ++pt;

      *dtitle = _cdda_append (*dtitle, pt);
      pt = strdup (*dtitle);

      char *title = strstr (pt, " / ");
      if (title)
      {
	*title = 0;
	title += 3;
	free (this->cddb.disc_artist);
	this->cddb.disc_artist = strdup (pt);
      }
      else
	title = pt;

      free (this->cddb.disc_title);
      this->cddb.disc_title = strdup (title);

      free (pt);
    }
  }
  else if (sscanf (buffer, "DYEAR=%s", &buf[0]) == 1) {
    char *pt = strchr (buffer, '=');
    if (pt && strlen (pt) == 5)
      this->cddb.disc_year = strdup (pt + 1);
  }
  else if(sscanf(buffer, "DGENRE=%s", &buf[0]) == 1) {
    char *pt = strchr(buffer, '=');
    if (pt)
      this->cddb.disc_category = strdup (pt + 1);
  }
  else if (sscanf (buffer, "TTITLE%d=%s", &track_no, &buf[0]) == 2) {
    if (track_no >= 0 && track_no < this->cddb.num_tracks) {
      char *pt = strchr(buffer, '=');
      this->cddb.track[track_no].title = _cdda_append (this->cddb.track[track_no].title, pt + 1);
    }
  }
  else if (!strncmp (buffer, "EXTD=", 5))
  {
    if (!this->cddb.disc_year)
    {
      int nyear;
      char *y = strstr (buffer, "YEAR:");
      if (y && sscanf (y + 5, "%4d", &nyear) == 1)
	this->cddb.disc_year = _x_asprintf ("%d", nyear);
    }
  }
}

/*
 * Try to load cached cddb infos
 */
static int _cdda_load_cached_cddb_infos(cdda_input_plugin_t *this) {
  DIR  *dir;

  const char *const xdg_cache_home = xdgCacheHome(&this->stream->xine->basedir_handle);
  const size_t cdir_size = strlen(xdg_cache_home) + sizeof("/"PACKAGE"/cddb") + 10 + 1;
  char *const cdir = alloca(cdir_size);
  sprintf(cdir, "%s/" PACKAGE "/cddb", xdg_cache_home);

  if((dir = opendir(cdir)) != NULL) {
    struct dirent *pdir;

    while((pdir = readdir(dir)) != NULL) {
      char discid[9];

      snprintf_buf(discid, "%08" PRIx32, this->cddb.disc_id);

      if(!strcasecmp(pdir->d_name, discid)) {
	FILE *fd;

	snprintf(cdir + cdir_size - 12, 10, "/%s", discid);
        if ((fd = fopen(cdir, "r")) == NULL) {
          int e = errno;
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
            "input_cdda: fopen(%s) failed: %s.\n", cdir, strerror (e));
          closedir (dir);
          return 0;
        }
	else {
	  char buffer[2048], *ln;
	  char *dtitle = NULL;

	  while ((ln = fgets(buffer, sizeof (buffer) - 1, fd)) != NULL) {

	    int length = strlen (buffer);
	    if (length && buffer[length - 1] == '\n')
	      buffer[length - 1] = '\0';

	    _cdda_parse_cddb_info (this, buffer, &dtitle);
	  }
	  fclose(fd);
	  free(dtitle);
	}

	closedir(dir);
	return 1;
      }
    }
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
      "input_cdda: cached entry for disc ID %08" PRIx32 " not found.\n", this->cddb.disc_id);
    closedir(dir);
  }

  return 0;
}

/*
 * Save cddb grabbed infos.
 */
static void _cdda_save_cached_cddb_infos(cdda_input_plugin_t *this, char *filecontent) {
  FILE  *fd;
  char *cfile;

  const char *const xdg_cache_home = xdgCacheHome(&this->stream->xine->basedir_handle);

  if (filecontent == NULL)
    return;

  /* the filename is always 8 characters */
  cfile = alloca(strlen(xdg_cache_home) + sizeof("/"PACKAGE"/cddb") + 9);
  strcpy(cfile, xdg_cache_home);
  strcat(cfile, "/"PACKAGE"/cddb");

  /* Ensure the cache directory exists */
  _cdda_mkdir_recursive_safe(this->stream->xine, cfile);

  sprintf(cfile + strlen(cfile), "/%08" PRIx32, this->cddb.disc_id);

  if ((fd = fopen(cfile, "w")) == NULL) {
    int e = errno;
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "input_cdda: fopen(%s) failed: %s.\n", cfile, strerror (e));
    return;
  }
  else {
    fprintf(fd, "%s", filecontent);
    fclose(fd);
  }

}

/*
 * Open a socket.
 */
static int _cdda_cddb_socket_open(cdda_input_plugin_t *this) {
  cdda_input_class_t *class = (cdda_input_class_t *)this->input_plugin.input_class;
  int sock;

#ifdef LOG
  printf("Conecting...");
  fflush(stdout);
#endif
  {
    char server[2048];
    int port;
    pthread_mutex_lock (&class->mutex);
    strlcpy (server, class->cddb_server, sizeof(server));
    port = class->cddb_port;
    pthread_mutex_unlock (&class->mutex);
    sock = _x_io_tcp_connect (this->stream, server, port);
    if (sock == -1 || _x_io_tcp_connect_finish (this->stream, sock, CDDB_TIMEOUT) != XIO_READY) {
      xine_log (this->stream->xine, XINE_LOG_MSG,
        _("%s: can't connect to %s:%d\n"), LOG_MODULE, server, port);
      lprintf ("failed\n");
      return -1;
    }
  }
  lprintf ("done, sock = %d\n", sock);
  return sock;
}

/*
 * Close the socket
 */
static void _cdda_cddb_socket_close(cdda_input_plugin_t *this) {

  if((this == NULL) || (this->cddb.fd < 0))
    return;

  _x_io_tcp_close(this->stream, this->cddb.fd);
  this->cddb.fd = -1;
}

/*
 * Try to talk with CDDB server (to retrieve disc/tracks titles).
 */
static int _cdda_cddb_retrieve(cdda_input_plugin_t *this) {
  cdda_input_class_t *class = (cdda_input_class_t *)this->input_plugin.input_class;
  char buffer[2048], buffercache[32768], *m, *p;
  char *dtitle = NULL;
  int err, i;

  if(_cdda_load_cached_cddb_infos(this)) {
    this->cddb.have_cddb_info = 1;
    return 1;
  }
  if (!class->cddb_enable || class->cddb_error) {
    this->cddb.have_cddb_info = 0;
    return 0;
  }
  else {
    class->cddb_error = 1;
    this->cddb.fd = _cdda_cddb_socket_open(this);
    if(this->cddb.fd >= 0) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
        _("input_cdda: successfully connected to cddb server '%s:%d'.\n"),
        class->cddb_server, class->cddb_port);
    }
    else {
      int e = errno;
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
        _("input_cdda: failed to connect to cddb server '%s:%d' (%s).\n"),
        class->cddb_server, class->cddb_port, strerror (e));
      this->cddb.have_cddb_info = 0;
      return 0;
    }

    /* Read welcome message */
    memset(&buffer, 0, sizeof(buffer));
    err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
    if (err < 0 || (err = _cdda_cddb_handle_code(buffer)) < 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: error while reading cddb welcome message.\n");
      _cdda_cddb_socket_close(this);
      return 0;
    }

    /* Send hello command */
    /* We don't send current user/host name to prevent spam.
     * Software that sends this is considered spyware
     * that most people don't like.
     */
    memset(&buffer, 0, sizeof(buffer));
    snprintf_buf(buffer, "cddb hello unknown localhost xine %s\n", VERSION);
    if ((err = _cdda_cddb_send_command(this, buffer)) <= 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: error while sending cddb hello command.\n");
      _cdda_cddb_socket_close(this);
      return 0;
    }

    memset(&buffer, 0, sizeof(buffer));
    err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
    if (err < 0 || (err = _cdda_cddb_handle_code(buffer)) < 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: cddb hello command returned error code '%03d'.\n", err);
      _cdda_cddb_socket_close(this);
      return 0;
    }
    /* Send server protocol number */
    /* For UTF-8 support - use protocol 6 */

    memset(&buffer, 0, sizeof(buffer));
    snprintf_buf(buffer, "proto %d\n", CDDB_PROTOCOL);
    if ((err = _cdda_cddb_send_command(this, buffer)) <= 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: error while sending cddb protocol command.\n");
      _cdda_cddb_socket_close(this);
      return 0;
    }

    memset(&buffer, 0, sizeof(buffer));
    err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
    if (err < 0 || (err = _cdda_cddb_handle_code(buffer)) < 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: cddb protocol command returned error code '%03d'.\n", err);
      _cdda_cddb_socket_close(this);
      return 0;
    }

    /* Send query command */
    memset(&buffer, 0, sizeof(buffer));
    size_t size = sprintf(buffer, "cddb query %08" PRIx32 " %d ", this->cddb.disc_id, this->cddb.num_tracks);
    for (i = 0; i < this->cddb.num_tracks; i++) {
      size += snprintf(buffer + size, sizeof(buffer) - size, "%d ", this->cddb.track[i].start);
    }
    snprintf(buffer + strlen(buffer), sizeof(buffer) - size, "%d\n", this->cddb.disc_length);
    if ((err = _cdda_cddb_send_command(this, buffer)) <= 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: error while sending cddb query command.\n");
      _cdda_cddb_socket_close(this);
      return 0;
    }

    memset(&buffer, 0, sizeof(buffer));
    err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
    if (err < 0 || (((err = _cdda_cddb_handle_code(buffer)) != 200) && (err != 210) && (err != 211))) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: cddb query command returned error code '%03d'.\n", err);
      _cdda_cddb_socket_close(this);
      return 0;
    }

    if (err == 200) {
      p = buffer;
      i = 0;
      while ((i <= 2) && ((m = xine_strsep(&p, " ")) != NULL)) {
        if (i == 1) {
          this->cddb.disc_category = strdup(m);
        }
        else if(i == 2) {
          this->cddb.cdiscid = strdup(m);
        }
        i++;
      }
    }

    if ((err == 210) || (err == 211)) {
      memset(&buffer, 0, sizeof(buffer));
      err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
      if (err < 0) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                "input_cdda: cddb query command returned error code '%03d'.\n", err);
        _cdda_cddb_socket_close(this);
        return 0;
      }
      p = buffer;
      i = 0;
      while ((i <= 1) && ((m = xine_strsep(&p, " ")) != NULL)) {
        if (i == 0) {
          this->cddb.disc_category = strdup(m);
        }
        else if(i == 1) {
          this->cddb.cdiscid = strdup(m);
        }
        i++;
      }
      while (strcmp(buffer, ".")) {
        memset(&buffer, 0, sizeof(buffer));
        err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
        if (err < 0) {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                  "input_cdda: cddb query command returned error code '%03d'.\n", err);
          _cdda_cddb_socket_close(this);
          return 0;
        }
      }
    }
    /* Send read command */
    memset(&buffer, 0, sizeof(buffer));
    snprintf_buf(buffer, "cddb read %s %s\n", this->cddb.disc_category, this->cddb.cdiscid);
    if ((err = _cdda_cddb_send_command(this, buffer)) <= 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: error while sending cddb read command.\n");
      _cdda_cddb_socket_close(this);
      return 0;
    }

    memset(&buffer, 0, sizeof(buffer));
    err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
    if (err < 0 || (err = _cdda_cddb_handle_code(buffer)) != 210) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: cddb read command returned error code '%03d'.\n", err);
      _cdda_cddb_socket_close(this);
      return 0;
    }

    this->cddb.have_cddb_info = 1;
    memset(&buffercache, 0, sizeof(buffercache));

    while (strcmp(buffer, ".")) {
      size_t bufsize = strlen(buffercache);

      memset(&buffer, 0, sizeof(buffer));
      _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
      snprintf(buffercache + bufsize, sizeof(buffercache) - bufsize, "%s\n", buffer);

      _cdda_parse_cddb_info (this, buffer, &dtitle);
    }
    free(dtitle);

    /* Save cddb info and close socket */
    _cdda_save_cached_cddb_infos(this, buffercache);
    _cdda_cddb_socket_close(this);
  }

  /* success */
  class->cddb_error = 0;
  return 1;
}

/*
 * Compute cddb disc compliant id
 */
static unsigned int _cdda_cddb_sum(int n) {
  unsigned int ret = 0;

  while(n > 0) {
    ret += (n % 10);
    n /= 10;
  }
  return ret;
}
static uint32_t _cdda_calc_cddb_id(cdda_input_plugin_t *this) {
  int i, tsum = 0;

  if(this == NULL || (this->cddb.num_tracks <= 0))
    return 0;

  for(i = 0; i < this->cddb.num_tracks; i++)
    tsum += _cdda_cddb_sum((this->cddb.track[i].start / CD_FRAMES_PER_SECOND));

  return ((tsum % 0xff) << 24
	  | (this->cddb.disc_length - (this->cddb.track[0].start / CD_FRAMES_PER_SECOND)) << 8
	  | this->cddb.num_tracks);
}

/*
 * Compute Musicbrainz CDIndex ID
 */
static void _cdda_cdindex(cdda_input_plugin_t *this, cdrom_toc_t *toc) {
  sha160_t sha;
  unsigned char temp[40], digest[24];
  int i;

  sha160_init (&sha);

  snprintf_buf (temp, "%02X%02X%08X",
    toc->first_track,
    toc->last_track - toc->ignore_last_track,
    toc->toc_entries[toc->total_tracks].first_frame); /* + 150 */
  sha160_update (&sha, temp, 12);

  for (i = toc->first_track; i <= toc->last_track - toc->ignore_last_track; i++) {
    snprintf_buf (temp, "%08X", toc->toc_entries[i - 1].first_frame);
    sha160_update (&sha, temp, 8);
  }

  for (i = toc->last_track - toc->ignore_last_track + 1; i < 100; i++) {
    sha160_update (&sha, temp, 8);
  }

  sha160_final (&sha, digest);

  xine_base64_encode (digest, temp, 20);

  _x_meta_info_set_utf8 (this->stream, XINE_META_INFO_CDINDEX_DISCID, temp);
}

/*
 * return cbbd disc id.
 */
static uint32_t _cdda_get_cddb_id(cdda_input_plugin_t *this) {

  if(this == NULL || (this->cddb.num_tracks <= 0))
    return 0;

  return _cdda_calc_cddb_id(this);
}

/*
 * Free allocated memory for CDDB informations
 */
static void _cdda_free_cddb_info(cdda_input_plugin_t *this) {

  if(this->cddb.track) {
    int t;

    for(t = 0; t < this->cddb.num_tracks; t++) {
      _x_freep(&this->cddb.track[t].title);
    }

    _x_freep(&this->cddb.track);
    _x_freep(&this->cddb.cdiscid);
    _x_freep(&this->cddb.disc_title);
    _x_freep(&this->cddb.disc_artist);
    _x_freep(&this->cddb.disc_category);
    _x_freep(&this->cddb.disc_year);
  }

  this->cddb.num_tracks = 0;
}
/*
 * ********** END OF CDDB ***************
 */

static int cdda_open (cdda_input_plugin_t *this_gen, const char *cdda_device, int *fdd) {
#ifndef WIN32
  int fd = -1;

  if ( !cdda_device ) return -1;

  *fdd = -1;

  this_gen->fd = -1;

  /* We use O_NONBLOCK for when /proc/sys/dev/cdrom/check_media is at 1 on
   * Linux systems */
  fd = xine_open_cloexec(cdda_device, O_RDONLY | O_NONBLOCK);
  if (fd == -1) {
    return -1;
  }

  this_gen->fd = fd;

#ifdef CDROM_SELECT_SPEED
  {
    cdda_input_class_t *class = (cdda_input_class_t *)this_gen->input_plugin.input_class;
    int speed = class->speed;
    if (speed && ioctl (fd, CDROM_SELECT_SPEED, speed) != 0)
      xprintf (class->xine, XINE_VERBOSITY_DEBUG,
        "input_cdda: setting drive speed to %d failed\n", speed);
  }
#endif

  *fdd = fd;
  return 0;

#else /* WIN32 */
  if ( !cdda_device ) return -1;

  *fdd = -1;

  this_gen->fd = -1;
  this_gen->h_device_handle = NULL;
  this_gen->i_sid = 0;
  this_gen->hASPI = NULL;
  this_gen->lpSendCommand = NULL;

  /* We are going to assume that we are opening a
   * device and not a file!
   */
  if( WIN_NT )
    {
      char psz_win32_drive[7];

      lprintf( "using winNT/2K/XP ioctl layer" );

      sprintf( psz_win32_drive, "\\\\.\\%c:", cdda_device[0] );

      this_gen->h_device_handle = CreateFile( psz_win32_drive, GENERIC_READ,
					      FILE_SHARE_READ | FILE_SHARE_WRITE,
					      NULL, OPEN_EXISTING,
					      FILE_FLAG_NO_BUFFERING |
					      FILE_FLAG_RANDOM_ACCESS, NULL );
      return (this_gen->h_device_handle == NULL) ? -1 : 0;
    }
  else
    {
      HMODULE hASPI = NULL;
      long (*lpGetSupport)( void ) = NULL;
      long (*lpSendCommand)( void* ) = NULL;
      DWORD dwSupportInfo;
      int i, j, i_hostadapters;
      char c_drive = cdda_device[0];

      hASPI = LoadLibrary( "wnaspi32.dll" );
      if( hASPI != NULL )
	{
          lpGetSupport = (long (*)(void))GetProcAddress( hASPI,
                                         "GetASPI32SupportInfo" );
          lpSendCommand = (long (*)(void*))GetProcAddress( hASPI,
                                         "SendASPI32Command" );
	}

      if( hASPI == NULL || lpGetSupport == NULL || lpSendCommand == NULL )
	{
	  lprintf( "unable to load aspi or get aspi function pointers" );

	  if( hASPI ) FreeLibrary( hASPI );
	  return -1;
	}

      /* ASPI support seems to be there */

      dwSupportInfo = lpGetSupport();

      if( HIBYTE( LOWORD ( dwSupportInfo ) ) == SS_NO_ADAPTERS )
	{
	  lprintf( "no host adapters found (aspi)" );
	  FreeLibrary( hASPI );
	  return -1;
	}

      if( HIBYTE( LOWORD ( dwSupportInfo ) ) != SS_COMP )
	{
	  lprintf( "unable to initalize aspi layer" );

	  FreeLibrary( hASPI );
	  return -1;
	}

      i_hostadapters = LOBYTE( LOWORD( dwSupportInfo ) );
      if( i_hostadapters == 0 )
	{
	  FreeLibrary( hASPI );
	  return -1;
	}

      c_drive = c_drive > 'Z' ? c_drive - 'a' : c_drive - 'A';

      for( i = 0; i < i_hostadapters; i++ )
	{
          for( j = 0; j < 15; j++ )
	    {
              struct SRB_GetDiskInfo srbDiskInfo;

              srbDiskInfo.SRB_Cmd         = SC_GET_DISK_INFO;
              srbDiskInfo.SRB_HaId        = i;
              srbDiskInfo.SRB_Flags       = 0;
              srbDiskInfo.SRB_Hdr_Rsvd    = 0;
              srbDiskInfo.SRB_Target      = j;
              srbDiskInfo.SRB_Lun         = 0;

              lpSendCommand( (void*) &srbDiskInfo );

              if( (srbDiskInfo.SRB_Status == SS_COMP) &&
                  (srbDiskInfo.SRB_Int13HDriveInfo == c_drive) )
		{
                  /* Make sure this is a cdrom device */
                  struct SRB_GDEVBlock   srbGDEVBlock;

                  memset( &srbGDEVBlock, 0, sizeof(struct SRB_GDEVBlock) );
                  srbGDEVBlock.SRB_Cmd    = SC_GET_DEV_TYPE;
                  srbGDEVBlock.SRB_HaId   = i;
                  srbGDEVBlock.SRB_Target = j;

                  lpSendCommand( (void*) &srbGDEVBlock );

                  if( ( srbGDEVBlock.SRB_Status == SS_COMP ) &&
                      ( srbGDEVBlock.SRB_DeviceType == DTYPE_CDROM ) )
		    {
                      this_gen->i_sid = MAKEWORD( i, j );
                      this_gen->hASPI = hASPI;
                      this_gen->lpSendCommand = lpSendCommand;

                      lprintf( "using aspi layer" );

                      return 0;
		    }
                  else
		    {
		      FreeLibrary( hASPI );
		      lprintf( "%c: is not a cdrom drive", cdda_device[0] );
		      return -1;
		    }
		}
	    }
	}

      FreeLibrary( hASPI );

      lprintf( "unable to get haid and target (aspi)" );
    }

#endif /* WIN32 */

    return -1;
}

static int cdda_close(cdda_input_plugin_t *this_gen) {

  if (!this_gen)
      return 0;

  if( this_gen->fd != -1 ) {
#ifdef CDROM_SELECT_SPEED
    cdda_input_class_t *class = (cdda_input_class_t *)this_gen->input_plugin.input_class;
    int speed = class->speed;
    if (speed && ioctl (this_gen->fd, CDROM_SELECT_SPEED, 0) != 0)
      xprintf (class->xine, XINE_VERBOSITY_DEBUG,
        "input_cdda: setting drive speed to normal failed\n");
#endif
    close(this_gen->fd);
  }
  this_gen->fd = -1;

  if (this_gen->net_fd != -1)
    close(this_gen->net_fd);
  this_gen->net_fd = -1;

#ifdef WIN32
  if( this_gen->h_device_handle )
     CloseHandle( this_gen->h_device_handle );
  this_gen->h_device_handle = NULL;
  if( this_gen->hASPI )
      FreeLibrary( this_gen->hASPI );
  this_gen->hASPI = NULL;
#endif /* WIN32 */

  return 0;
}


static uint32_t cdda_plugin_get_capabilities (input_plugin_t *this_gen) {

  (void)this_gen;
  return INPUT_CAP_SEEKABLE;
}


static off_t cdda_plugin_read (input_plugin_t *this_gen, void *buf, off_t len) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;
  uint32_t want, have;

  /* Allow reading full raw frames only. This will also short circuit _x_demux_read_header (). */
  if ((len < 0) || ((uint64_t)len > 0xffffffff))
    return 0;
  want = (uint64_t)len;
  want /= (uint32_t)CD_RAW_FRAME_SIZE;
  have = want * CD_RAW_FRAME_SIZE;
  if (have != (uint32_t)len)
    return 0;

  if (this->current_frame > this->last_frame)
    return 0;

  /* populate frame cache */
  if (this->cache_first == -1 ||
      this->current_frame < this->cache_first ||
      this->current_frame > this->cache_last) {
    int len, err = -1;

    if (this->tripple) {
      len = CACHED_FRAMES / 10;
      this->tripple--;
    } else {
      len = CACHED_FRAMES;
    }

    this->cache_first = this->current_frame;
    this->cache_last = this->current_frame + len - 1;
    if( this->cache_last > this->last_frame )
      this->cache_last = this->last_frame;

#ifndef WIN32
    if (this->fd != -1)
#else
    if (this->h_device_handle)
#endif /* WIN32 */
      err = read_cdrom_frames(this, this->cache_first,
                             this->cache_last - this->cache_first + 1,
                             this->cache[0]);
    else if (this->net_fd != -1)
      err = network_read_cdrom_frames(this->stream, this->net_fd, this->cache_first,
                                      this->cache_last - this->cache_first + 1,
                                      this->cache[0]);
    if (err < 0)
      return 0;

    this->last_read_time = time (NULL);
  }

  have = this->cache_last + 1 - this->current_frame;
  if (want > have)
    want = have;
  have = want * CD_RAW_FRAME_SIZE;
  memcpy (buf, this->cache [this->current_frame - this->cache_first], have);
  this->current_frame += want;
  return have;
}

static buf_element_t *cdda_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo,
  off_t nlen) {

  buf_element_t *buf;

  buf = fifo->buffer_pool_alloc(fifo);
  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  /* Standard bufs are 8kbyte and can hold 3 raw frames. */
  if (nlen > buf->max_size)
    nlen = buf->max_size;

  buf->size = cdda_plugin_read(this_gen, buf->content, nlen);
  if (buf->size == 0) {
    buf->free_buffer(buf);
    buf = NULL;
  }

  return buf;
}

static off_t cdda_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;
  int seek_to_frame;

  /* compute the proposed frame and check if it is within bounds */
  seek_to_frame = offset / CD_RAW_FRAME_SIZE;
  if (origin == SEEK_SET)
    seek_to_frame += this->first_frame;
  else if (origin == SEEK_CUR)
    seek_to_frame += this->current_frame;
  else
    seek_to_frame += this->last_frame + 1;

  if ((seek_to_frame >= this->first_frame) &&
      (seek_to_frame <= this->last_frame + 1)) {
    if ((seek_to_frame < this->cache_first) ||
        (seek_to_frame > this->cache_last + 1)) {
      time_t now = time (NULL);
      /* read in small steps first to give faster seek response. */
      if (now <= this->last_read_time + 5)
        this->tripple = 10;
    }
    this->current_frame = seek_to_frame;
  }

  return (this->current_frame - this->first_frame) * CD_RAW_FRAME_SIZE;
}

static off_t cdda_plugin_get_current_pos (input_plugin_t *this_gen){
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  return (this->current_frame - this->first_frame) * CD_RAW_FRAME_SIZE;
}

static off_t cdda_plugin_get_length (input_plugin_t *this_gen) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  return (this->last_frame - this->first_frame + 1) * CD_RAW_FRAME_SIZE;
}

static uint32_t cdda_plugin_get_blocksize (input_plugin_t *this_gen) {

  (void)this_gen;
  return 0;
}

static const char* cdda_plugin_get_mrl (input_plugin_t *this_gen) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  return this->mrl;
}

static int cdda_plugin_get_optional_data (input_plugin_t *this_gen,
                                          void *data, int data_type) {
  (void)this_gen;
  (void)data;
  (void)data_type;
  return 0;
}

static void cdda_plugin_dispose (input_plugin_t *this_gen ) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;
  cdda_input_class_t *class = (cdda_input_class_t *) this->input_plugin.input_class;

  class->last_read_time = this->last_read_time;

  _cdda_free_cddb_info(this);

  cdda_close(this);

  free(this);
}


static int cdda_plugin_open (input_plugin_t *this_gen ) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;
  cdda_input_class_t  *class = (cdda_input_class_t *) this_gen->input_class;
  cdrom_toc_t         *toc = NULL;
  int                  fd  = -1;
  char                 sbuf[2048];
  const char          *cdda_device;

  lprintf("cdda_plugin_open\n");

  /* get the CD TOC */

  cdda_device = this->device;
  if (!cdda_device) {
    pthread_mutex_lock (&class->mutex);
    strlcpy (sbuf, class->cdda_device, 2048);
    pthread_mutex_unlock (&class->mutex);
    cdda_device = sbuf;
  }

#ifndef WIN32
  if (strchr (cdda_device,':')) {
    fd = network_connect (this->stream, cdda_device);
    if (fd != -1) {
      this->net_fd = fd;
      toc = network_read_cdrom_toc (this->stream, this->net_fd);
    }
  }
#endif

  if (this->net_fd == -1) {
    if (cdda_open (this, cdda_device, &fd) == -1)
      return 0;
#ifndef WIN32
    toc = read_cdrom_toc (this->fd);
#else
    toc = read_cdrom_toc (this);
#endif
  }

  if (!toc) {
    cdda_close (this);
    return 0;
  }
  print_cdrom_toc (class->xine, toc);


  if (this->track >= 0) {
    if ((toc->first_track > (this->track + 1)) ||
        (toc->last_track < (this->track + 1))) {
      free_cdrom_toc (toc);
      cdda_close (this);
      return 0;
    }
    /* set up the frame boundaries for this particular track */
    this->first_frame   =
    this->current_frame = toc->toc_entries[this->track].first_frame;
    this->last_frame    = toc->toc_entries[this->track + 1].first_frame - 1;
  } else {
    /* no track given = all audio tracks */
    this->first_frame   =
    this->current_frame = toc->toc_entries[0].first_frame;
    this->last_frame    = toc->toc_entries[toc->last_track - toc->first_track + 1].first_frame - 1;
  }

  /* if the last read was just recently, assume the drive still up and spinning,
   * and use tripple mode for start as well. */
  {
    time_t now;
    this->last_read_time = class->last_read_time;
    now = time (NULL);
    if (now <= this->last_read_time + 5)
      this->tripple = 10;
  }

  /* invalidate cache */
  this->cache_first = this->cache_last = -1;

  /* get the Musicbrainz CDIndex */
  _cdda_cdindex (this, toc);

  /*
   * CDDB
   */
  _cdda_free_cddb_info(this);

  if (toc->total_tracks) {
    int t;

    this->cddb.track = (trackinfo_t *) calloc(toc->total_tracks, sizeof(trackinfo_t));
    if (this->cddb.track)
      this->cddb.num_tracks = toc->total_tracks;

    for(t = 0; t < this->cddb.num_tracks; t++) {
      int length = (toc->toc_entries[t].first_frame_minute * CD_SECONDS_PER_MINUTE +
		    toc->toc_entries[t].first_frame_second);

      this->cddb.track[t].start = (length * CD_FRAMES_PER_SECOND +
				   toc->toc_entries[t].first_frame_frame);
      this->cddb.track[t].title = NULL;
    }

  }

  this->cddb.disc_length = (toc->toc_entries[toc->total_tracks].first_frame_minute * CD_SECONDS_PER_MINUTE +
                            toc->toc_entries[toc->total_tracks].first_frame_second);
  this->cddb.disc_id     = _cdda_get_cddb_id(this);

  if((this->cddb.have_cddb_info == 0) || (_cdda_is_cd_changed(this) == 1))
    _cdda_cddb_retrieve(this);

  if(this->cddb.disc_title) {
    lprintf("Disc Title: %s\n", this->cddb.disc_title);

    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_ALBUM, this->cddb.disc_title);
  }

  if ((this->track >= 0) && (this->track < this->cddb.num_tracks) &&
      this->cddb.track[this->track].title) {
    /* Check for track 'titles' of the form <artist> / <title>. */
    char *pt;
    pt = strstr(this->cddb.track[this->track].title, " / ");
    if (pt != NULL) {
      char *track_artist;
      track_artist = strdup(this->cddb.track[this->track].title);
      track_artist[pt - this->cddb.track[this->track].title] = 0;
      lprintf("Track %d Artist: %s\n", this->track+1, track_artist);

      _x_meta_info_set_utf8(this->stream, XINE_META_INFO_ARTIST, track_artist);
      free(track_artist);
      pt += 3;
    }
    else {
      if(this->cddb.disc_artist) {
	lprintf("Disc Artist: %s\n", this->cddb.disc_artist);

	_x_meta_info_set_utf8(this->stream, XINE_META_INFO_ARTIST, this->cddb.disc_artist);
      }

      pt = this->cddb.track[this->track].title;
    }
    lprintf("Track %d Title: %s\n", this->track+1, pt);

    char tracknum[16];
    snprintf_buf(tracknum, "%d", this->track+1);
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_TRACK_NUMBER, tracknum);
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_TITLE, pt);
  }

  if(this->cddb.disc_category) {
    lprintf("Disc Category: %s\n", this->cddb.disc_category);

    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_GENRE, this->cddb.disc_category);
  }

  if(this->cddb.disc_year) {
    lprintf("Disc Year: %s\n", this->cddb.disc_year);

    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_YEAR, this->cddb.disc_year);
  }

  pthread_mutex_lock (&class->mutex);
  free_cdrom_toc (class->last_toc);
  class->last_toc = toc;
  pthread_mutex_unlock (&class->mutex);

  return 1;
}

static void free_autoplay_list(cdda_input_class_t *this)
{
  /* free old playlist */
  _x_freep (&this->autoplaylist);
}

static const char * const * cdda_class_get_autoplay_list (input_class_t *this_gen, int *num_files) {
  cdda_input_class_t  *this = (cdda_input_class_t *)this_gen;
  cdrom_toc_t         *toc = NULL;
  char                 dname[2048];

  lprintf("cdda_class_get_autoplay_list for >%s<\n", this->cdda_device);

  pthread_mutex_lock (&this->mutex);
  strlcpy (dname, this->cdda_device, 2048);
  pthread_mutex_unlock (&this->mutex);

  free_autoplay_list(this);

  /* get the CD TOC */
  /* we need an instance pointer to store all the details about the
   * device we are going to open; let's create a minimal temporary instance. */
  {
    int fd = -1;
    cdda_input_plugin_t *inst = calloc (1, sizeof (*inst));
    if (!inst)
      return NULL;
    inst->input_plugin.input_class = this_gen;
    inst->stream = NULL;
    inst->fd = -1;
    inst->net_fd = -1;
#ifndef WIN32
    if (strchr (dname,':')) {
      fd = network_connect (NULL, dname);
      if (fd != -1) {
        inst->net_fd = fd;
        toc = network_read_cdrom_toc (NULL, fd);
      }
    }
#endif
    if (fd == -1) {
      if (cdda_open (inst, dname, &fd) == -1) {
#ifdef LOG
        int e = errno;
        lprintf ("cdda_class_get_autoplay_list: opening >%s< failed %s\n", dname, strerror (e));
#endif
        free (inst);
        return NULL;
      }
#ifndef WIN32
      toc = read_cdrom_toc (fd);
#else
      toc = read_cdrom_toc (inst);
#endif /* WIN32 */
    }
    cdda_close (inst);
    free (inst);
  }

  if (!toc)
    return NULL;
  print_cdrom_toc (this->xine, toc);

  {
    int num_tracks = toc->last_track - toc->first_track + (toc->ignore_last_track ? 0 : 1);
    size_t size = (num_tracks + 1) * sizeof (char *) + num_tracks * 9;
    char **list = malloc (size);
    this->autoplaylist = list;
    if (list) {
      int n, t = toc->first_track;
      char *s = (char *)(list + num_tracks + 1);
      *num_files = num_tracks;
      n = 10 - t;
      if (n > 0) {
        if (n > num_tracks)
          n = num_tracks;
        num_tracks -= n;
        while ((n--) > 0) {
          *list++ = s;
          memcpy (s, "cdda:/", 6);
          s[6] = t + '0';
          s[7] = 0;
          s += 8;
          t++;
        }
      }
      n = num_tracks;
      while ((n--) > 0) {
        *list++ = s;
        memcpy (s, "cdda:/", 6);
        s[7] = (t % 10) + '0';
        s[6] = ((t / 10) & 15) + '0';
        s[8] = 0;
        s += 9;
        t++;
      }
      *list = NULL;
      pthread_mutex_lock (&this->mutex);
      free_cdrom_toc (this->last_toc);
      this->last_toc = toc;
      pthread_mutex_unlock (&this->mutex);
      return (const char * const *)this->autoplaylist;
    }
  }

  *num_files = 0;
  free_cdrom_toc (toc);
  return NULL;
}

static input_plugin_t *cdda_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream,
                                    const char *mrl) {

  cdda_input_plugin_t *this;
  size_t               mlen;
  const uint8_t       *p;
  unsigned int         d, t;

  lprintf ("cdda_class_get_instance: >%s<\n", mrl);

  if (strncasecmp (mrl, "cdda:/", 6))
    return NULL;

  /* parse "cdda:/[/foo/bar][/35]" */
  t = 0;
  d = 1;
  mlen = strlen (mrl + 5);
  p = (const uint8_t *)mrl + mlen + 5;
  while (1) {
    uint8_t z = *--p;
    if (z == '/')
      break;
    z ^= '0';
    if (z > 9)
      break;
    t += d * z;
    d *= 10u;
  }
  if (*p != '/') {
    p = (const uint8_t *)mrl + mlen + 5;
    t = 0;
  }
    
  this = calloc (1, sizeof (*this) + 2 * (mlen + 6));
  if (!this)
    return NULL;
#ifndef HAVE_ZERO_SAFE_MEM
  this->cddb.cdiscid        = NULL;
  this->cddb.disc_title     = NULL;
  this->cddb.disc_year      = NULL;
  this->cddb.disc_artist    = NULL;
  this->cddb.disc_category  = NULL;
  this->cddb.disc_length    = 0;
  this->cddb.track          = NULL;
  this->cddb.num_tracks     = 0;
  this->cddb.have_cddb_info = 0;
  this->device              = NULL;
  this->first_frame         = 0;
  this->current_frame       = 0;
  this->last_frame          = 0;
  this->cache_first         = 0;
  this->cache_last          = 0;
  this->tripple             = 0;
  this->last_read_time      = 0;
#  ifdef WIN32
  this->h_device_handle     = 0;
  this->hASPI               = 0;
  this->i_sid               = 0;
  this->lpSendCommand       = NULL;
#  endif
#endif

  /* CD tracks start from 1; internal data structure indexes from 0 */
  this->track = (int)t - 1;
  {
    char *q = (char *)this + sizeof (*this);
    this->mrl = q;
    memcpy (q, mrl, mlen + 6);
    q += mlen + 6;
    mlen = p - (const uint8_t *)mrl - 5;
    if (mlen > 1) {
      mlen--;
      this->device = q;
      memcpy (q, mrl + 6, mlen);
      q[mlen] = 0;
    }
  }

  this->stream      = stream;

  this->fd         = -1;
  this->net_fd     = -1;

  this->input_plugin.open               = cdda_plugin_open;
  this->input_plugin.get_capabilities   = cdda_plugin_get_capabilities;
  this->input_plugin.read               = cdda_plugin_read;
  this->input_plugin.read_block         = cdda_plugin_read_block;
  this->input_plugin.seek               = cdda_plugin_seek;
  this->input_plugin.get_current_pos    = cdda_plugin_get_current_pos;
  this->input_plugin.get_length         = cdda_plugin_get_length;
  this->input_plugin.get_blocksize      = cdda_plugin_get_blocksize;
  this->input_plugin.get_mrl            = cdda_plugin_get_mrl;
  this->input_plugin.get_optional_data  = cdda_plugin_get_optional_data;
  this->input_plugin.dispose            = cdda_plugin_dispose;
  this->input_plugin.input_class        = cls_gen;

  return &this->input_plugin;
}


static void cdda_class_dispose (input_class_t *this_gen) {
  cdda_input_class_t  *this = (cdda_input_class_t *) this_gen;
  config_values_t     *config = this->xine->config;

  config->unregister_callbacks (config, NULL, NULL, this, sizeof (*this));

  free_autoplay_list(this);
  free_cdrom_toc (this->last_toc);

  pthread_mutex_destroy (&this->mutex);
  free (this);
}

static int cdda_class_eject_media (input_class_t *this_gen) {
  cdda_input_class_t  *this = (cdda_input_class_t *) this_gen;
  int r;
  pthread_mutex_lock (&this->mutex);
  r = media_eject_media (this->xine, this->cdda_device);
  pthread_mutex_unlock (&this->mutex);
  return r;
}


static void *init_plugin (xine_t *xine, const void *data) {

  cdda_input_class_t  *this;
  config_values_t     *config;

  (void)data;
  this = calloc(1, sizeof (cdda_input_class_t));
  if (!this)
    return NULL;
#ifndef HAVE_ZERO_SAFE_MEM
  this->cddb_error          = 0;
  this->input_class.get_dir = NULL;
  this->last_read_time      = 0;
  this->last_toc            = NULL;
  this->autoplaylist        = NULL;
#endif
  
  this->xine   = xine;
  this->config = xine->config;
  config       = xine->config;

  this->input_class.get_instance       = cdda_class_get_instance;
  this->input_class.identifier         = "cdda";
  this->input_class.description        = N_("CD Digital Audio (aka. CDDA)");
  /* this->input_class.get_dir            = cdda_class_get_dir; */
  this->input_class.get_autoplay_list  = cdda_class_get_autoplay_list;
  this->input_class.dispose            = cdda_class_dispose;
  this->input_class.eject_media        = cdda_class_eject_media;

  this->cdda_device = config->register_filename (config,
    "media.audio_cd.device",
    DEFAULT_CDDA_DEVICE,
    XINE_CONFIG_STRING_IS_DEVICE_NAME,
    _("device used for CD audio"),
    _("The path to the device, usually a CD or DVD drive, which you intend to use "
      "for playing audio CDs."),
    10,
    cdda_device_cb,
    this);

  this->cddb_enable = config->register_bool (config,
    "media.audio_cd.use_cddb",
    1,
    _("query CDDB"),
    _("Enables CDDB queries, which will give you convenient title and track names "
      "for your audio CDs.\n"
      "Keep in mind that, unless you use your own private CDDB, this information is retrieved "
      "from an internet server which might collect a profile of your listening habits."),
    10,
    enable_cddb_changed_cb,
    this);

  this->cddb_server = config->register_string (config,
    "media.audio_cd.cddb_server",
    CDDB_SERVER,
    _("CDDB server name"),
    _("The CDDB server used to retrieve the title and track information from.\n"
      "This setting is security critical, because the sever will receive information "
      "about your listening habits and could answer the queries with malicious replies. "
      "Be sure to enter a server you can trust."),
    XINE_CONFIG_SECURITY,
    server_changed_cb,
    this);

  this->cddb_port = config->register_num (config,
    "media.audio_cd.cddb_port",
    CDDB_PORT,
    _("CDDB server port"),
    _("The server port used to retrieve the title and track information from."),
    XINE_CONFIG_SECURITY,
    port_changed_cb,
    this);

#ifdef CDROM_SELECT_SPEED
  this->speed = config->register_num (config,
    "media.audio_cd.drive_slowdown",
    4,
    _("slow down disc drive to this speed factor"),
    _("Since some CD or DVD drives make some really loud noises because of the "
      "fast disc rotation, xine will try to slow them down. With standard "
      "CD or DVD playback, the high datarates that require the fast rotation are "
      "not needed, so the slowdown should not affect playback performance.\n"
      "A value of zero here will disable the slowdown."),
    10,
    speed_changed_cb,
    this);
#endif

  pthread_mutex_init (&this->mutex, NULL);

  return this;
}

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, "CD", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
