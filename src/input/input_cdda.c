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
 * Compact Disc Digital Audio (CDDA) Input Plugin 
 *   by Mike Melanson (melanson@pcisys.net)
 *
 * $Id: input_cdda.c,v 1.23 2003/05/20 01:23:56 tchamp Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#ifndef _MSC_VER 
#include <sys/ioctl.h>
#include <netdb.h>
#else
#include <timer.h> /* alarm() */
#endif /* _MSC_VER */

#include <signal.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "input_plugin.h"
#include "media_helper.h"

#ifdef WIN32
#include <winioctl.h>
#endif

/*
#define LOG 1
*/

#if defined(__sun)
#define	DEFAULT_CDDA_DEVICE	"/vol/dev/aliases/cdrom0"
#elif defined(WIN32)
#define DEFAULT_CDDA_DEVICE "d:\\"
#else
#define	DEFAULT_CDDA_DEVICE	"/dev/cdrom"
#endif

#define CDDB_SERVER             "freedb.freedb.org"
#define CDDB_PORT               8880

/* CD-relevant defines and data structures */
#define CD_SECONDS_PER_MINUTE   60
#define CD_FRAMES_PER_SECOND    75
#define CD_RAW_FRAME_SIZE       2352
#define CD_LEADOUT_TRACK        0xAA

typedef struct _cdrom_toc_entry {
  int   track_mode;
  int   first_frame;
  int   first_frame_minute;
  int   first_frame_second;
  int   first_frame_frame;
  int   total_frames;
} cdrom_toc_entry;

typedef struct _cdrom_toc {
  int   first_track;
  int   last_track;
  int   total_tracks;

  cdrom_toc_entry *toc_entries;
  cdrom_toc_entry leadout_track;  /* need to know where last track ends */
} cdrom_toc;

/**************************************************************************
 * xine interface functions
 *************************************************************************/

#define MAX_TRACKS     99
#define CACHED_FRAMES  100

typedef struct {
  int                  start;
  char                *title;
} trackinfo_t;

typedef struct {
  input_plugin_t       input_plugin;

  xine_stream_t       *stream;

  struct  {
    int                enabled;
    char              *server;
    int                port;
    char              *cache_dir; 
    
    char              *cdiscid;
    char              *disc_title;
    char              *disc_year;
    char              *disc_artist;
    char              *disc_category;

    int                fd;
    unsigned long      disc_id;

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

  char                *cdda_device;

  unsigned char        cache[CACHED_FRAMES][CD_RAW_FRAME_SIZE];
  int                  cache_first;
  int                  cache_last;

#ifdef WIN32
    HANDLE h_device_handle;                         /* vcd device descriptor */
  long  hASPI;
  short i_sid;
  long  (*lpSendCommand)( void* );
#endif

} cdda_input_plugin_t;

typedef struct {

  input_class_t        input_class;

  xine_t              *xine;
  config_values_t     *config;

  char                *cdda_device;
  
  cdda_input_plugin_t *ip;

  int                  show_hidden_files;
  char                *origin_path;

  int                  mrls_allocated_entries;
  xine_mrl_t         **mrls;
  
  char                *autoplaylist[MAX_TRACKS];

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


static void print_cdrom_toc(cdrom_toc *toc) {

	int i;
	int time1;
	int time2;
	int timediff;

	printf("\ntoc:\n");
	printf("\tfirst track  = %d\n", toc->first_track);
	printf("\tlast track   = %d\n", toc->last_track);
	printf("\ttotal tracks = %d\n", toc->total_tracks);
	printf("\ntoc entries:\n");

	
	printf("leadout track: Control: %d MSF: %02d:%02d:%04d, first frame = %d\n",
		toc->leadout_track.track_mode,
		toc->leadout_track.first_frame_minute,
		toc->leadout_track.first_frame_second,
		toc->leadout_track.first_frame_frame,
		toc->leadout_track.first_frame);

	/* fetch each toc entry */
	if (toc->first_track > 0) {		
		for (i = toc->first_track; i <= toc->last_track; i++) {			
			printf("\ttrack mode = %d", toc->toc_entries[i-1].track_mode);
			printf("\ttrack %d, audio, MSF: %02d:%02d:%02d, first frame = %d\n",  
				i, 
				toc->toc_entries[i-1].first_frame_minute,
				toc->toc_entries[i-1].first_frame_second,
				toc->toc_entries[i-1].first_frame_frame,
				toc->toc_entries[i-1].first_frame);

			time1 = ((toc->toc_entries[i-1].first_frame_minute * 60) + 
                      toc->toc_entries[i-1].first_frame_second);

        	if (i == toc->last_track) {
			  time2 = ((toc->leadout_track.first_frame_minute * 60) +
				  toc->leadout_track.first_frame_second);
			}
			else {
			  time2 = ((toc->toc_entries[i].first_frame_minute * 60) + 
                  toc->toc_entries[i].first_frame_second);
			}

            timediff = time2 - time1;

			printf("\t time: %02d:%02d\n", timediff/60, timediff%60);
		}
	}	
}

void init_cdrom_toc(cdrom_toc *toc) {

  toc->first_track = toc->last_track = toc->total_tracks = 0;
  toc->toc_entries = NULL;
}

void free_cdrom_toc(cdrom_toc *toc) {

  if(toc && toc->toc_entries)
    free(toc->toc_entries);
}

#if defined (__linux__)

#include <linux/cdrom.h>

static int read_cdrom_toc(int fd, cdrom_toc *toc) {

  struct cdrom_tochdr tochdr;
  struct cdrom_tocentry tocentry;
  int i;

  /* fetch the table of contents */
  if (ioctl(fd, CDROMREADTOCHDR, &tochdr) == -1) {
    perror("CDROMREADTOCHDR");
    return -1;
  }

  toc->first_track = tochdr.cdth_trk0;
  toc->last_track = tochdr.cdth_trk1;
  toc->total_tracks = toc->last_track - toc->first_track + 1;

  /* allocate space for the toc entries */
  toc->toc_entries =
    (cdrom_toc_entry *)malloc(toc->total_tracks * sizeof(cdrom_toc_entry));
  if (!toc->toc_entries) {
    perror("malloc");
    return -1;
  }

  /* fetch each toc entry */
  for (i = toc->first_track; i <= toc->last_track; i++) {

    memset(&tocentry, 0, sizeof(tocentry));

    tocentry.cdte_track = i;
    tocentry.cdte_format = CDROM_MSF;
    if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
      perror("CDROMREADTOCENTRY");
      return -1;
    }

    toc->toc_entries[i-1].track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
    toc->toc_entries[i-1].first_frame_minute = tocentry.cdte_addr.msf.minute;
    toc->toc_entries[i-1].first_frame_second = tocentry.cdte_addr.msf.second;
    toc->toc_entries[i-1].first_frame_frame = tocentry.cdte_addr.msf.frame;
    toc->toc_entries[i-1].first_frame =
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
    return -1;
  }

  toc->leadout_track.track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
  toc->leadout_track.first_frame_minute = tocentry.cdte_addr.msf.minute;
  toc->leadout_track.first_frame_second = tocentry.cdte_addr.msf.second;
  toc->leadout_track.first_frame_frame = tocentry.cdte_addr.msf.frame;
  toc->leadout_track.first_frame =
    (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
     tocentry.cdte_addr.msf.frame;

  return 0;
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

static int read_cdrom_toc(int fd, cdrom_toc *toc) {

  struct cdrom_tochdr tochdr;
  struct cdrom_tocentry tocentry;
  int i;

  /* fetch the table of contents */
  if (ioctl(fd, CDROMREADTOCHDR, &tochdr) == -1) {
    perror("CDROMREADTOCHDR");
    return -1;
  }

  toc->first_track = tochdr.cdth_trk0;
  toc->last_track = tochdr.cdth_trk1;
  toc->total_tracks = toc->last_track - toc->first_track + 1;

  /* allocate space for the toc entries */
  toc->toc_entries =
    (cdrom_toc_entry *)malloc(toc->total_tracks * sizeof(cdrom_toc_entry));
  if (!toc->toc_entries) {
    perror("malloc");
    return -1;
  }

  /* fetch each toc entry */
  for (i = toc->first_track; i <= toc->last_track; i++) {

    memset(&tocentry, 0, sizeof(tocentry));

    tocentry.cdte_track = i;
    tocentry.cdte_format = CDROM_MSF;
    if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
      perror("CDROMREADTOCENTRY");
      return -1;
    }

    toc->toc_entries[i-1].track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
    toc->toc_entries[i-1].first_frame_minute = tocentry.cdte_addr.msf.minute;
    toc->toc_entries[i-1].first_frame_second = tocentry.cdte_addr.msf.second;
    toc->toc_entries[i-1].first_frame_frame = tocentry.cdte_addr.msf.frame;
    toc->toc_entries[i-1].first_frame =
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
    return -1;
  }

  toc->leadout_track.track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
  toc->leadout_track.first_frame_minute = tocentry.cdte_addr.msf.minute;
  toc->leadout_track.first_frame_second = tocentry.cdte_addr.msf.second;
  toc->leadout_track.first_frame_frame = tocentry.cdte_addr.msf.frame;
  toc->leadout_track.first_frame =
    (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
     tocentry.cdte_addr.msf.frame;

  return 0;
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

#elif defined(__FreeBSD__)

#include <sys/cdio.h>

static int read_cdrom_toc(int fd, cdrom_toc *toc) {

  struct ioc_toc_header tochdr;
  struct ioc_read_toc_single_entry tocentry;
  int i;

  /* fetch the table of contents */
  if (ioctl(fd, CDIOREADTOCHEADER, &tochdr) == -1) {
    perror("CDIOREADTOCHEADER");
    return -1;
  }

  toc->first_track = tochdr.starting_track;
  toc->last_track = tochdr.ending_track;
  toc->total_tracks = toc->last_track - toc->first_track + 1;

  /* allocate space for the toc entries */
  toc->toc_entries =
    (cdrom_toc_entry *)malloc(toc->total_tracks * sizeof(cdrom_toc_entry));
  if (!toc->toc_entries) {
    perror("malloc");
    return -1;
  }

  /* fetch each toc entry */
  for (i = toc->first_track; i <= toc->last_track; i++) {

    memset(&tocentry, 0, sizeof(tocentry));

    tocentry.track = i;
    tocentry.address_format = CD_MSF_FORMAT;
    if (ioctl(fd, CDIOREADTOCENTRY, &tocentry) == -1) {
      perror("CDIOREADTOCENTRY");
      return -1;
    }

    toc->toc_entries[i-1].track_mode = (tocentry.entry.control & 0x04) ? 1 : 0;
    toc->toc_entries[i-1].first_frame_minute = tocentry.entry.addr.msf.minute;
    toc->toc_entries[i-1].first_frame_second = tocentry.entry.addr.msf.second;
    toc->toc_entries[i-1].first_frame_frame = tocentry.entry.addr.msf.frame;
    toc->toc_entries[i-1].first_frame =
      (tocentry.entry.addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.entry.addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.entry.addr.msf.frame;
  }

  /* fetch the leadout as well */
  memset(&tocentry, 0, sizeof(tocentry));

  tocentry.track = CD_LEADOUT_TRACK;
  tocentry.address_format = CD_MSF_FORMAT;
  if (ioctl(fd, CDIOREADTOCENTRY, &tocentry) == -1) {
    perror("CDIOREADTOCENTRY");
    return -1;
  }

  toc->leadout_track.track_mode = (tocentry.entry.control & 0x04) ? 1 : 0;
  toc->leadout_track.first_frame_minute = tocentry.entry.addr.msf.minute;
  toc->leadout_track.first_frame_second = tocentry.entry.addr.msf.second;
  toc->leadout_track.first_frame_frame = tocentry.entry.addr.msf.frame;
  toc->leadout_track.first_frame =
    (tocentry.entry.addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (tocentry.entry.addr.msf.second * CD_FRAMES_PER_SECOND) +
     tocentry.entry.addr.msf.frame;

  return 0;
}

static int read_cdrom_frames(cdda_input_plugin_t *this_gen, int frame, int num_frames,
  unsigned char *data) {

  int fd = this_gen->fd;
  struct ioc_read_audio cdda;

  while( num_frames ) {
    cdda.address_format = CD_MSF_FORMAT;
    cdda.address.msf.minute = frame / CD_SECONDS_PER_MINUTE / CD_FRAMES_PER_SECOND;
    cdda.address.msf.second = (frame / CD_FRAMES_PER_SECOND) % CD_SECONDS_PER_MINUTE;
    cdda.address.msf.frame = frame % CD_FRAMES_PER_SECOND;
    cdda.nframes = 1;
    cdda.buffer = data;

    /* read a frame */
    if(ioctl(fd, CDIOCREADAUDIO, &cdda) < 0) {
      perror("CDIOCREADAUDIO");
      return -1;
    }
    
    data += CD_RAW_FRAME_SIZE;
    frame++;
    num_frames--;
  }
  return 0;
}

#elif defined(WIN32)

static int read_cdrom_toc(cdda_input_plugin_t *this_gen, cdrom_toc *toc) {

  if( this_gen->hASPI )
  {
	  /* This is for ASPI which obviously isn't supported! */
#ifdef LOG 
	  printf("Windows ASPI support is not complete yet!\n");
#endif
	  return -1;
      
  }
  else
  {
	  DWORD dwBytesReturned;
      DWORD dw; 
	  CDROM_TOC cdrom_toc;
	  int i;

	  if( DeviceIoControl( this_gen->h_device_handle,
		  IOCTL_CDROM_READ_TOC,
		  NULL, 0, &cdrom_toc, sizeof(CDROM_TOC),
		  &dwBytesReturned, NULL ) == 0 )
	  {
#ifdef LOG
		  printf( "xineplug_inp_cdda : could not read TOCHDR\n" );
          dw = GetLastError();
          printf("GetLastError returned %u\n", dw); 

#endif
		  return -1;
	  }

      toc->first_track = cdrom_toc.FirstTrack;
      toc->last_track = cdrom_toc.LastTrack;
      toc->total_tracks = toc->last_track - toc->first_track + 1;

     
      /* allocate space for the toc entries */
      toc->toc_entries =
          (cdrom_toc_entry *)malloc(toc->total_tracks * sizeof(cdrom_toc_entry));
      if (!toc->toc_entries) {
          perror("malloc");
          return -1;
      }
  

      /* fetch each toc entry */
      for (i = toc->first_track; i <= toc->last_track; i++) {
          
          toc->toc_entries[i-1].track_mode = (cdrom_toc.TrackData[i-1].Control & 0x04) ? 1 : 0;
          toc->toc_entries[i-1].first_frame_minute = cdrom_toc.TrackData[i-1].Address[1];
          toc->toc_entries[i-1].first_frame_second = cdrom_toc.TrackData[i-1].Address[2];
          toc->toc_entries[i-1].first_frame_frame = cdrom_toc.TrackData[i-1].Address[3];

          toc->toc_entries[i-1].first_frame =
              (toc->toc_entries[i-1].first_frame_minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
              (toc->toc_entries[i-1].first_frame_second * CD_FRAMES_PER_SECOND) +
              toc->toc_entries[i-1].first_frame_frame;
      }

	  /* Grab the leadout track too! (I think that this is correct?) */
	  i = toc->total_tracks;
      toc->leadout_track.track_mode = (cdrom_toc.TrackData[i].Control & 0x04) ? 1 : 0;
      toc->leadout_track.first_frame_minute = cdrom_toc.TrackData[i].Address[1];
      toc->leadout_track.first_frame_second = cdrom_toc.TrackData[i].Address[2];
      toc->leadout_track.first_frame_frame = cdrom_toc.TrackData[i].Address[3];
      toc->leadout_track.first_frame =
        (toc->leadout_track.first_frame_minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
        (toc->leadout_track.first_frame_second * CD_FRAMES_PER_SECOND) +
         toc->leadout_track.first_frame_frame;
  }		

  return 0;
}


static int read_cdrom_frames(cdda_input_plugin_t *this_gen, int frame, int num_frames,
  unsigned char *data) {

  DWORD dw; 
  DWORD dwBytesReturned;
  RAW_READ_INFO raw_read_info;

  if( this_gen->hASPI )
  {
	  /* This is for ASPI which obviously isn't supported! */
#ifdef LOG 
	  printf("Windows ASPI support is not complete yet!\n");
#endif
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
			  printf( "xineplug_inp_cdda : could not read frame\n" );
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



static int read_cdrom_toc(int fd, cdrom_toc *toc) {
  return -1;
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

static int host_connect_attempt (struct in_addr ia, int port)
{
  int                s;
  struct sockaddr_in sin;

  s=socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (s==-1) {
    printf("input_cdda: failed to open socket\n");
    return -1;
  }

  sin.sin_family = AF_INET;
  sin.sin_addr   = ia;
  sin.sin_port   = htons(port);

  if (connect(s, (struct sockaddr *)&sin, sizeof(sin))==-1 && errno != EINPROGRESS) {
    printf("input_cdda: cannot connect to host\n");
    close(s);
    return -1;
  }

  return s;
}

static int host_connect (const char *host, int port)
{
  struct hostent *h;
  int i;
  int s;

  h=gethostbyname(host);
  if (h==NULL) {
        printf("input_cdda: unable to resolve >%s<\n", host);
        return -1;
  }

  for(i=0; h->h_addr_list[i]; i++) {
    struct in_addr ia;
    memcpy(&ia, h->h_addr_list[i], 4);
    s=host_connect_attempt(ia, port);
    if(s != -1) {
      signal( SIGPIPE, SIG_IGN );
      return s;
    }
  }

  printf("input_cdda: unable to connect to >%s<\n", host);
  return -1;
}


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

static int sock_check_opened(int socket) {
  fd_set   readfds, writefds, exceptfds;
  int      retval;
  struct   timeval timeout;

  for(;;) {
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    FD_SET(socket, &exceptfds);

    timeout.tv_sec  = 0;
    timeout.tv_usec = 0;

    retval = select(socket + 1, &readfds, &writefds, &exceptfds, &timeout);

    if(retval == -1 && (errno != EAGAIN && errno != EINTR))
      return 0;

    if (retval != -1)
      return 1;
  }

  return 0;
}

/*
 * read binary data from socket
 */
static int sock_data_read (int socket, char *buf, int nlen) {
  int n, num_bytes;

  if((socket < 0) || (buf == NULL))
    return -1;

  if(!sock_check_opened(socket))
    return -1;

  num_bytes = 0;

  while (num_bytes < nlen) {

    n = read (socket, &buf[num_bytes], nlen - num_bytes);

    /* read errors */
    if (n < 0) {
      if(errno == EAGAIN) {
        fd_set rset;
        struct timeval timeout;

        FD_ZERO (&rset);
        FD_SET  (socket, &rset);

        timeout.tv_sec  = 30;
        timeout.tv_usec = 0;

        if (select (socket+1, &rset, NULL, NULL, &timeout) <= 0) {
          printf ("input_cdda: timeout on read\n");
          return 0;
        }
        continue;
      }
      printf ("input_cdda: read error %d\n", errno);
      return 0;
    }

    num_bytes += n;

    /* end of stream */
    if (!n) break;
  }

  return num_bytes;
}

/*
 * read a line (\n-terminated) from socket
 */
static int sock_string_read(int socket, char *buf, int len) {
  char    *pbuf;
  int      r, rr;
  void    *nl;

  if((socket < 0) || (buf == NULL))
    return -1;

  if(!sock_check_opened(socket))
    return -1;

  if (--len < 1)
    return(-1);

  pbuf = buf;

  do {

    if((r = recv(socket, pbuf, len, MSG_PEEK)) <= 0)
      return -1;

    if((nl = memchr(pbuf, '\n', r)) != NULL)
      r = ((char *) nl) - pbuf + 1;

    if((rr = read(socket, pbuf, r)) < 0)
      return -1;

    pbuf += rr;
    len -= rr;

  } while((nl == NULL) && len);

  if (pbuf > buf && *(pbuf-1) == '\n'){
    *(pbuf-1) = '\0';
  }
  *pbuf = '\0';
  return (pbuf - buf);
}

/*
 * Write to socket.
 */
static int sock_data_write(int socket, char *buf, int len) {
  ssize_t  size;
  int      wlen = 0;

  if((socket < 0) || (buf == NULL))
    return -1;

  if(!sock_check_opened(socket))
    return -1;

  while(len) {
    size = write(socket, buf, len);

    if(size <= 0)
      return -1;

    len -= size;
    wlen += size;
    buf += size;
  }

  return wlen;
}

static int network_command( int socket, char *data_buf, char *msg, ...)
{
  char     buf[_BUFSIZ];
  va_list  args;
  int      ret, n;

  va_start(args, msg);
  vsnprintf(buf, _BUFSIZ, msg, args);
  va_end(args);

  /* Each line sent is '\n' terminated */
  if((buf[strlen(buf)] == '\0') && (buf[strlen(buf) - 1] != '\n'))
    sprintf(buf, "%s%c", buf, '\n');

  if( sock_data_write(socket, buf, strlen(buf)) < (int)strlen(buf) )
  {
    printf("input_cdda: error writing to socket\n");
    return -1;
  }

  if( sock_string_read(socket, buf, _BUFSIZ) <= 0 )
  {
    printf("input_cdda: error reading from socket\n");
    return -1;
  }

  sscanf(buf, "%d %d", &ret, &n );

  if( n ) {
    if( !data_buf ) {
      printf("input_cdda: protocol error, data returned but no buffer provided.\n");
      return -1;
    }
    if( sock_data_read(socket, data_buf, n) < n )
      return -1;
  } else if ( data_buf ) {

    strcpy( data_buf, buf );
  }

  return ret;
}


static int network_connect( char *url )
{
  char *host;
  int port;
  int fd;

  url = strdup(url);
  parse_url(url, &host, &port);

  if( !host || !strlen(host) || !port )
  {
    free(url);
    return -1;
  }

  fd = host_connect( host, port );
  free(url);

  if( fd != -1 ) {
    if( network_command(fd, NULL, "cdda_open") < 0 ) {
      printf("input_cdda: error opening remote drive\n");
      close(fd);
      return -1;
    }
  }
  return fd;
}
                   
static int network_read_cdrom_toc(int fd, cdrom_toc *toc) {

  char buf[_BUFSIZ];
  int i;

  /* fetch the table of contents */
  if( network_command( fd, buf, "cdda_tochdr" ) == -1) {
    printf("input_cdda: network CDROMREADTOCHDR error\n");
    return -1;
  }

  sscanf(buf,"%*s %*s %d %d", &toc->first_track, &toc->last_track);
  toc->total_tracks = toc->last_track - toc->first_track + 1;

  /* allocate space for the toc entries */
  toc->toc_entries =
    (cdrom_toc_entry *)malloc(toc->total_tracks * sizeof(cdrom_toc_entry));
  if (!toc->toc_entries) {
    perror("malloc");
    return -1;
  }

  /* fetch each toc entry */
  for (i = toc->first_track; i <= toc->last_track; i++) {

    /* fetch the table of contents */
    if( network_command( fd, buf, "cdda_tocentry %d", i ) == -1) {
      printf("input_cdda: network CDROMREADTOCENTRY error\n");
      return -1;
    }

    sscanf(buf,"%*s %*s %d %d %d %d", &toc->toc_entries[i-1].track_mode,
                                      &toc->toc_entries[i-1].first_frame_minute,
                                      &toc->toc_entries[i-1].first_frame_second,
                                      &toc->toc_entries[i-1].first_frame_frame);

    toc->toc_entries[i-1].first_frame =
      (toc->toc_entries[i-1].first_frame_minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (toc->toc_entries[i-1].first_frame_second * CD_FRAMES_PER_SECOND) +
       toc->toc_entries[i-1].first_frame_frame;
  }

  /* fetch the leadout as well */
  if( network_command( fd, buf, "cdda_tocentry %d", CD_LEADOUT_TRACK ) == -1) {
    printf("input_cdda: network CDROMREADTOCENTRY error\n");
    return -1;
  }

  sscanf(buf,"%*s %*s %d %d %d %d", &toc->leadout_track.track_mode,
                                    &toc->leadout_track.first_frame_minute,
                                    &toc->leadout_track.first_frame_second,
                                    &toc->leadout_track.first_frame_frame);
  toc->leadout_track.first_frame =
    (toc->leadout_track.first_frame_minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (toc->leadout_track.first_frame_second * CD_FRAMES_PER_SECOND) +
     toc->leadout_track.first_frame_frame;

  return 0;
}

static int network_read_cdrom_frames(int fd, int first_frame, int num_frames,
  unsigned char data[CD_RAW_FRAME_SIZE]) {

  return network_command( fd, data, "cdda_read %d %d", first_frame, num_frames );
}



/*
 * **************** CDDB *********************
 */
/*
 * Config callbacks
 */
static void cdda_device_cb(void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *) data;
  
  class->cdda_device = cfg->str_value;
}
static void enable_cddb_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *) data;
  
  if(class->ip) {
    cdda_input_plugin_t *this = class->ip;

    this->cddb.enabled = cfg->num_value;
  }
}
static void server_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *) data;
  
  if(class->ip) {
    cdda_input_plugin_t *this = class->ip;

    this->cddb.server = cfg->str_value;
  }
}
static void port_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *) data;
  
  if(class->ip) {
    cdda_input_plugin_t *this = class->ip;
    this->cddb.port = cfg->num_value;
  }
}
static void cachedir_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *) data;
  
  if(class->ip) {
    cdda_input_plugin_t *this = class->ip;

    this->cddb.cache_dir = cfg->str_value;
  }
}

/*
 * Return 1 if CD has been changed, 0 of not, -1 on error.
 */
static int _cdda_is_cd_changed(cdda_input_plugin_t *this) {
#ifdef CDROM_MEDIA_CHANGED
  int err, cd_changed=0;
  
  if(this == NULL || this->fd < 0)
    return -1;
  
  if((err = ioctl(this->fd, CDROM_MEDIA_CHANGED, cd_changed)) < 0) {
    printf("input_cdda: ioctl(CDROM_MEDIA_CHANGED) failed: %s.\n", strerror(errno));
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
static void _cdda_mkdir_safe(char *path) {
  
  if(path == NULL)
    return;

#ifndef WIN32
  {
	  struct stat  pstat;
	  
	  if((lstat(path, &pstat)) < 0) {
		  /* file or directory no exist, create it */
		  if(mkdir(path, 0755) < 0) {
			  fprintf(stderr, "input_cdda: mkdir(%s) failed: %s\n", path, strerror(errno));
			  return;
		  }
	  }
	  else {
		  /* Check of found file is a directory file */
		  if(!S_ISDIR(pstat.st_mode)) {
			  fprintf(stderr, "input_cdda: %s is not a directory.\n", path);
		  }
	  }
  }
#else
  {
	  HANDLE          hList;
	  TCHAR           szDir[MAX_PATH+1];
	  WIN32_FIND_DATA FileData;
	  
	  // Get the proper directory path
	  sprintf(szDir, "%s\\*", path);
	  
	  // Get the first file
	  hList = FindFirstFile(szDir, &FileData);
	  if (hList == INVALID_HANDLE_VALUE)
	  {
		  if(_mkdir(path) != 0) {
			  fprintf(stderr, "input_cdda: mkdir(%s) failed\n", path);
			  return;
		  }		  
	  }
	  
      FindClose(hList);
  }
#endif /* WIN32 */
}

/*
 * Make recursive directory creation
 */
static void _cdda_mkdir_recursive_safe(char *path) {
  char *p, *pp;
  char buf[XINE_PATH_MAX + XINE_NAME_MAX + 1];
  char buf2[XINE_PATH_MAX + XINE_NAME_MAX + 1];

  if(path == NULL)
    return;

  memset(&buf, 0, sizeof(buf));
  memset(&buf2, 0, sizeof(buf2));

  sprintf(buf, "%s", path);
  pp = buf;
  while((p = xine_strsep(&pp, "/")) != NULL) {
    if(p && strlen(p)) {

#ifdef WIN32
		if (*buf2 != '\0') {
#endif

      sprintf(buf2, "%s/%s", buf2, p);

#ifdef WIN32
		}
		else {
          sprintf(buf2, "%s", p);
		}

#endif /* WIN32 */

      _cdda_mkdir_safe(buf2);
    }
  }
}

/*
 * Where, by default, cddb cache files will be saved
 */
static char *_cdda_cddb_get_default_location(void) {
  static char buf[XINE_PATH_MAX + XINE_NAME_MAX + 1];
  
  memset(&buf, 0, sizeof(buf));
  sprintf(buf, "%s/.xine/cddbcache", (xine_get_homedir()));
  
  return buf;
}

/*
 * Small sighandler ;-)
 */
static void die(int signal) {
  abort();
}

/*
 * Read from socket, fill char *s, return size length.
 */
static int _cdda_cddb_socket_read(char *s, int size, int socket) {
  int i = 0, r;
  char c;
  
  alarm(20);
  signal(SIGALRM, die);
  
  while((r=recv(socket, &c, 1, 0)) != 0) {
    if(c == '\r' || c == '\n')
      break;
    if(i > size)
      break;
    s[i] = c;
    i++;
  }
  s[i] = '\n';
  s[i+1] = 0;
  recv(socket, &c, 1, 0);
  
  alarm(0);
  signal(SIGALRM, SIG_DFL);
  
  s[i] = 0;

  return r;
}

/*
 * Send a command to socket
 */
static int _cdda_cddb_send_command(cdda_input_plugin_t *this, char *cmd) {
  
  if((this == NULL) || (this->cddb.fd < 0) || (cmd == NULL))
    return -1;

  return (send(this->cddb.fd, cmd, strlen(cmd), 0));
}

/*
 * Handle return code od a command result.
 */
static int _cdda_cddb_handle_code(char *buf) {
  int  rcode, fdig, sdig, tdig;
  int  err = -1;

  if(sscanf(buf, "%d", &rcode) == 1) {

    fdig = rcode / 100;
    sdig = (rcode - (fdig * 100)) / 10;
    tdig = (rcode - (fdig * 100) - (sdig * 10));

    /*
    printf(" %d--\n", fdig);
    printf(" -%d-\n", sdig);
    printf(" --%d\n", tdig);
    */
    switch(fdig) {
    case 1:
      /* printf("Informative message\n"); */
      err = 0;
      break;
    case 2:
      /* printf("Command OK\n"); */
      err = 0;
      break;
    case 3:
      /* printf("Command OK so far, continue\n"); */
      err = 0;
      break;
    case 4:
      /* printf("Command OK, but cannot be performed for some specified reasons\n"); */
      err = -1;
      break;
    case 5:
      /* printf("Command unimplemented, incorrect, or program error\n"); */
      err = -1;
      break;
    default:
      /* printf("Unhandled case %d\n", fdig); */
      err = -1;
      break;
    }

    switch(sdig) {
    case 0:
      /* printf("Ready for further commands\n"); */
      err = 0;
      break;
    case 1:
      /* printf("More server-to-client output follows (until terminating marker)\n"); */
      err = 0;
      break;
    case 2:
      /* printf("More client-to-server input follows (until terminating marker)\n"); */
      err = 0;
      break;
    case 3:
      /* printf("Connection will close\n"); */
      err = -1;
      break;
    default:
      /* printf("Unhandled case %d\n", sdig); */
      err = -1;
      break;
    }

    if(err >= 0)
      err = rcode;
  }
  
  return err;
}

/*
 * Try to load cached cddb infos
 */
static int _cdda_load_cached_cddb_infos(cdda_input_plugin_t *this) {
  char  cdir[XINE_PATH_MAX + XINE_NAME_MAX + 1];
  DIR  *dir;

  if(this == NULL)
    return 0;
  
  memset(&cdir, 0, sizeof(cdir));
  sprintf(cdir, "%s", this->cddb.cache_dir);
  
  if((dir = opendir(cdir)) != NULL) {
    struct dirent *pdir;
    
    while((pdir = readdir(dir)) != NULL) {
      char discid[9];
      
      memset(&discid, 0, sizeof(discid));
      sprintf(discid, "%08lx", this->cddb.disc_id);
     
      if(!strcasecmp(pdir->d_name, discid)) {
	FILE *fd;
	
	sprintf(cdir, "%s/%s", cdir, discid);
	if((fd = fopen(cdir, "r")) == NULL) {
	  printf("input_cdda: fopen(%s) failed: %s\n", cdir, strerror(errno));
	  closedir(dir);
	  return 0;
	}
	else {
	  char buffer[256], *ln, *pt;
	  char buf[256];
	  int  tnum;
	  
	  while((ln = fgets(buffer, 255, fd)) != NULL) {

	    buffer[strlen(buffer) - 1] = '\0';
	    
	    if(sscanf(buffer, "DTITLE=%s", &buf[0]) == 1) {
	      char *artist, *title;

	      pt = strrchr(buffer, '=');
	      if(pt)
		pt++;

	      artist = pt;
	      title = strchr(pt, '/');
	      if(title) {
		*title++ = 0;
	      }
	      else {
		title = artist;
		artist = NULL;
	      }

	      if(artist)
		this->cddb.disc_artist = strdup(artist);

	      this->cddb.disc_title = strdup(title);
	    }
	    else if(sscanf(buffer, "TTITLE%d=%s", &tnum, &buf[0]) == 2) {
	      pt = strrchr(buffer, '=');
	      if(pt)
		pt++;
	      this->cddb.track[tnum].title = strdup(pt);
	    }
	    else {
	      if(!strncmp(buffer, "EXTD=", 5)) {
		char *y;
		int   nyear;
		
		y = strstr(buffer, "YEAR:");
		if(y) {
		  if(sscanf(y+5, "%4d", &nyear) == 1) {
		    char year[5];

		    snprintf(year, 5, "%d", nyear);
		    this->cddb.disc_year = strdup(year);
		  }
		}
	      }
	    }
	  }
	  fclose(fd);
	}
	
	closedir(dir);
	return 1;
      }
    }
    closedir(dir);
  }
  
  return 0;
}

/*
 * Save cddb grabbed infos.
 */
static void _cdda_save_cached_cddb_infos(cdda_input_plugin_t *this, char *filecontent) {
  char   cfile[XINE_PATH_MAX + XINE_NAME_MAX + 1];
  FILE  *fd;
  
  if((this == NULL) || (filecontent == NULL))
    return;
  
  memset(&cfile, 0, sizeof(cfile));

  /* Ensure "~/.xine/cddbcache" exist */
  sprintf(cfile, "%s", this->cddb.cache_dir);
  
  _cdda_mkdir_recursive_safe(cfile);
  
  sprintf(cfile, "%s/%08lx", this->cddb.cache_dir, this->cddb.disc_id);
  
  if((fd = fopen(cfile, "w")) == NULL) {
    printf("input_cdda: fopen(%s) failed: %s\n", cfile, strerror(errno));
    return;
  }
  else {
    fprintf(fd, filecontent);
    fclose(fd);
  }
  
}

/*
 * Open a socket.
 */
static int _cdda_cddb_socket_open(cdda_input_plugin_t *this) {
  int                 sockfd;
  struct hostent     *he;
  struct sockaddr_in  their_addr;

  if(this == NULL)
    return -1;
  
  alarm(15);
  signal(SIGALRM, die);
  if((he=gethostbyname(this->cddb.server)) == NULL) {
    alarm(0);
    signal(SIGALRM, SIG_DFL);
    return -1;
  }
  
  if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    alarm(0);
    signal(SIGALRM, SIG_DFL);
    return -1;
  }
  
  their_addr.sin_family = AF_INET;
  their_addr.sin_port   = htons(this->cddb.port);
  their_addr.sin_addr   = *((struct in_addr *)he->h_addr);
  memset(&(their_addr.sin_zero), 0, 8);
  
  if(connect(sockfd, (struct sockaddr *)&their_addr, sizeof(struct sockaddr)) == -1) {
    alarm(0);
    signal(SIGALRM, SIG_DFL);
    return -1;
  }
  alarm(0);
  signal(SIGALRM, SIG_DFL);

  return sockfd;
}

/*
 * Close the socket
 */
static void _cdda_cddb_socket_close(cdda_input_plugin_t *this) {
  
  if((this == NULL) || (this->cddb.fd < 0))
    return;

  close(this->cddb.fd);
  this->cddb.fd = -1;
}

/*
 * Try to talk with CDDB server (to retrieve disc/tracks titles).
 */
static void _cdda_cddb_retrieve(cdda_input_plugin_t *this) {
  char  buffer[2048];
  int   err, i;

  if(this == NULL)
    return;
  
  if(_cdda_load_cached_cddb_infos(this)) {
    this->cddb.have_cddb_info = 1;
    return;
  }
  else {
    
    this->cddb.fd = _cdda_cddb_socket_open(this);
    if(this->cddb.fd >= 0) {
      printf("input_cdda: server '%s:%d' successfuly connected.\n", 
	     this->cddb.server, this->cddb.port);
      
    }
    else {
      printf("input_cdda: opening server '%s:%d' failed: %s\n", 
	     this->cddb.server, this->cddb.port, strerror(errno));
      this->cddb.have_cddb_info = 0;
      return;
    }
    
    memset(&buffer, 0, sizeof(buffer));
    
    /* Get welcome message */
    if(_cdda_cddb_socket_read(&buffer[0], 2047, this->cddb.fd)) {
      if((err = _cdda_cddb_handle_code(buffer)) >= 0) {
	/* send hello */
	memset(&buffer, 0, sizeof(buffer));
        /* We don't send current user/host name to prevent spam.
         * Software that sends this is considered spyware
         * that most people don't like.
         */
	sprintf(buffer, "cddb hello unknown localhost xine %s\n", VERSION);
	if((err = _cdda_cddb_send_command(this, buffer)) > 0) {
	  /* Get answer from hello */
	  memset(&buffer, 0, sizeof(buffer));
	  if(_cdda_cddb_socket_read(&buffer[0], 2047, this->cddb.fd)) {
	    /* Parse returned code */
	    if((err = _cdda_cddb_handle_code(buffer)) >= 0) {
	      /* We are logged, query disc */
	      memset(&buffer, 0, sizeof(buffer));
	      sprintf(buffer, "cddb query %08lx %d ", this->cddb.disc_id, this->cddb.num_tracks);
	      for(i = 0; i < this->cddb.num_tracks; i++) {
		sprintf(buffer, "%s%d ", buffer, this->cddb.track[i].start);
	      }
	      sprintf(buffer, "%s%d\n", buffer, this->cddb.disc_length);
	      if((err = _cdda_cddb_send_command(this, buffer)) > 0) {
		memset(&buffer, 0, sizeof(buffer));
		if(_cdda_cddb_socket_read(&buffer[0], 2047, this->cddb.fd)) {
		  /* Parse returned code */
		  if((err = _cdda_cddb_handle_code(buffer)) == 200) {
		    /* Disc entry exist */
		    char *m = NULL, *p = buffer;
		    int   f = 0;
		    
		    while((f <= 2) && ((m = xine_strsep(&p, " ")) != NULL)) {
		      if(f == 1)
			this->cddb.disc_category = strdup(m);
		      else if(f == 2)
			this->cddb.cdiscid = strdup(m);
		      f++;
		    }
		  }
		  
		  /* Now, grab track titles */
		  memset(&buffer, 0, sizeof(buffer));
		  sprintf(buffer, "cddb read %s %s\n", 
			  this->cddb.disc_category, this->cddb.cdiscid);

		  if((err = _cdda_cddb_send_command(this, buffer)) > 0) {
		    /* Get answer from read */
		    memset(&buffer, 0, sizeof(buffer));
		    if(_cdda_cddb_socket_read(&buffer[0], 2047, this->cddb.fd)) {
		      /* Great, now we will have track titles */
		      if((err = _cdda_cddb_handle_code(buffer)) == 210) {
			char           buf[2048];
			unsigned char *pt;
			int            tnum;
			char           buffercache[32768];
			
			this->cddb.have_cddb_info = 1;
			memset(&buffercache, 0, sizeof(buffercache));
						
			while(strcmp(buffer, ".")) {
			  memset(&buffer, 0, sizeof(buffer));
			  _cdda_cddb_socket_read(&buffer[0], 2047, this->cddb.fd);

			  sprintf(buffercache, "%s%s\n", buffercache, buffer);
			  
			  if(sscanf(buffer, "DTITLE=%s", &buf[0]) == 1) {
			    char *artist, *title;

			    pt = strrchr(buffer, '=');
			    if(pt)
			      pt++;
			    
			    artist = pt;
			    title = strchr(pt, '/');
			    if(title) {
			      *title++ = 0;
			    }
			    else {
			      title = artist;
			      artist = NULL;
			    }
			    
			    if(artist)
			      this->cddb.disc_artist = strdup(artist);
			    
			    this->cddb.disc_title = strdup(title);
			  }
			  else if(sscanf(buffer, "TTITLE%d=%s", &tnum, &buf[0]) == 2) {
			    pt = strrchr(buffer, '=');
			    if(pt) pt++;
			    this->cddb.track[tnum].title = strdup(pt);
			  }
			  else {
			    if(!strncmp(buffer, "EXTD=", 5)) {
			      char *y;
			      int   nyear;
			      
			      y = strstr(buffer, "YEAR:");
			      if(y) {
				if(sscanf(y+5, "%4d", &nyear) == 1) {
				  char year[5];
				  
				  snprintf(year, 5, "%d", nyear);
				  this->cddb.disc_year = strdup(year);
				}
			      }
			    }
			  }
			}
			/* Save grabbed info */
			_cdda_save_cached_cddb_infos(this, buffercache);
		      }
		    }
		  }
		}
	      }
	    }	  
	  }
	}
      }
    }
    _cdda_cddb_socket_close(this);
  }
 
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
static unsigned long _cdda_calc_cddb_id(cdda_input_plugin_t *this) {
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
 * return cbbd disc id.
 */
static unsigned long _cdda_get_cddb_id(cdda_input_plugin_t *this) {

  if(this == NULL || (this->cddb.num_tracks <= 0))
    return 0;

  return _cdda_calc_cddb_id(this);
}

/*
 * grab (try) titles from cddb server.
 */
static void _cdda_cddb_grab_infos(cdda_input_plugin_t *this) {

  if(this == NULL)
    return;

  _cdda_cddb_retrieve(this);

}

/*
 * Free allocated memory for CDDB informations
 */
static void _cdda_free_cddb_info(cdda_input_plugin_t *this) {

  if(this->cddb.track) {
    int t;

    for(t = 0; t < this->cddb.num_tracks; t++) {
      if(this->cddb.track[t].title)
	free(this->cddb.track[t].title);
    }

    free(this->cddb.track);
    
    if(this->cddb.cdiscid)
      free(this->cddb.cdiscid);
    
    if(this->cddb.disc_title)
      free(this->cddb.disc_title);
    
    if(this->cddb.disc_artist)
      free(this->cddb.disc_artist);

    if(this->cddb.disc_category)
      free(this->cddb.disc_category);
    
    if(this->cddb.disc_year)
      free(this->cddb.disc_year);
    
  }
}
/*
 * ********** END OF CDDB ***************
 */

static int cdda_open(cdda_input_plugin_t *this_gen,
					 char *cdda_device, cdrom_toc *toc) {

  int fd = -1;

  if ( !cdda_device ) return -1;

#ifndef WIN32
 
  this_gen->fd = -1;

  fd = open (cdda_device, O_RDONLY);
  if (fd == -1) {
    return -1;
  }
  this_gen>fd = fd;

#else /* WIN32 */

  this_gen->fd = -1;
  this_gen->h_device_handle = NULL;
  this_gen->i_sid = 0;
  this_gen->hASPI = 0;
  this_gen->lpSendCommand = 0;


  /* We are going to assume that we are opening a 
   * device and not a file!
   */
  if( WIN_NT )
  {
	  char psz_win32_drive[7];
	  
#ifdef LOG
	  printf( "xineplug_inp_cdda : using winNT/2K/XP ioctl layer" );
#endif
	  
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
		  (FARPROC) lpGetSupport = GetProcAddress( hASPI,
			  "GetASPI32SupportInfo" );
		  (FARPROC) lpSendCommand = GetProcAddress( hASPI,
			  "SendASPI32Command" );
	  }
	  
	  if( hASPI == NULL || lpGetSupport == NULL || lpSendCommand == NULL )
	  {
#ifdef LOG
		  printf( "xineplug_inp_cdda : unable to load aspi or get aspi function pointers" );
#endif

		  if( hASPI ) FreeLibrary( hASPI );
		  return -1;
	  }
	  
	  /* ASPI support seems to be there */
	  
	  dwSupportInfo = lpGetSupport();
	  
	  if( HIBYTE( LOWORD ( dwSupportInfo ) ) == SS_NO_ADAPTERS )
	  {
#ifdef LOG
		  printf( "xineplug_inp_cdda : no host adapters found (aspi)" );
#endif
		  FreeLibrary( hASPI );
		  return -1;
	  }
	  
	  if( HIBYTE( LOWORD ( dwSupportInfo ) ) != SS_COMP )
	  {
#ifdef LOG
		  printf( "xineplug_inp_cdda : unable to initalize aspi layer" );
#endif
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
                      this_gen->hASPI = (long)hASPI;
                      this_gen->lpSendCommand = lpSendCommand;

#ifdef LOG
                      printf( "xineplug_inp_cdda : using aspi layer" );
#endif
					  
                      return 0;
                  }
                  else
                  {
                      FreeLibrary( hASPI );
#ifdef LOG
                      printf( "xineplug_inp_cdda : %s: is not a cdrom drive",
						  cdda_device[0] );
#endif
                      return -1;
                  }
              }
          }
	  }
	  
	  FreeLibrary( hASPI );

#ifdef LOG
	  printf( "xineplug_inp_cdda : unable to get haid and target (aspi)" );
#endif
	  
    }
	
    return -1;



#endif /* WIN32 */

}

static int cdda_close(cdda_input_plugin_t *this_gen) {

  if( this_gen->fd != -1 )
      close(this_gen->fd);
  this_gen->fd = -1;

  if (this_gen->net_fd != -1)
    close(this_gen->net_fd);
  this_gen->net_fd = -1;

#ifdef WIN32
  if( this_gen->h_device_handle )
     CloseHandle( this_gen->h_device_handle );
  this_gen->h_device_handle = NULL;
  if( this_gen->hASPI )
      FreeLibrary( (HMODULE)this_gen->hASPI );
  this_gen->hASPI = NULL;
#endif /* WIN32 */

  return 0;
}


static uint32_t cdda_plugin_get_capabilities (input_plugin_t *this_gen) {

  return INPUT_CAP_SEEKABLE | INPUT_CAP_BLOCK;
}


static off_t cdda_plugin_read (input_plugin_t *this_gen, char *buf, off_t len) {

  /* only allow reading in block-sized chunks */

  return 0;
}

static buf_element_t *cdda_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, 
  off_t nlen) {

  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;
  buf_element_t *buf;
  unsigned char frame_data[CD_RAW_FRAME_SIZE];
  int err = 0;

  if (nlen != CD_RAW_FRAME_SIZE)
    return NULL;

  if (this->current_frame >= this->last_frame)
    return NULL;

  /* populate frame cache */
  if( this->cache_first == -1 ||
      this->current_frame < this->cache_first ||
      this->current_frame > this->cache_last ) {

    this->cache_first = this->current_frame;
    this->cache_last = this->current_frame + CACHED_FRAMES - 1;
    if( this->cache_last > this->last_frame )
      this->cache_last = this->last_frame;
    
#ifndef WIN32		
    if ( this->fd != -1 )  
#else
	if ( this->h_device_handle )
#endif /* WIN32 */

      err = read_cdrom_frames(this, this->cache_first,
                             this->cache_last - this->cache_first + 1,
                             this->cache[0]);
    else if ( this->net_fd != -1 )
      err = network_read_cdrom_frames(this->net_fd, this->cache_first,
                                      this->cache_last - this->cache_first + 1,
                                      this->cache[0]);
  }

  if( err < 0 )
    return NULL;
    
  memcpy(frame_data, this->cache[this->current_frame-this->cache_first], CD_RAW_FRAME_SIZE);
  this->current_frame++;

  buf = fifo->buffer_pool_alloc(fifo);
  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;
  buf->size = CD_RAW_FRAME_SIZE;
  memcpy(buf->mem, frame_data, CD_RAW_FRAME_SIZE);

  return buf;
}

static off_t cdda_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;
  int seek_to_frame;

  /* compute the proposed frame and check if it is within bounds */
  if (origin == SEEK_SET)
    seek_to_frame = offset / CD_RAW_FRAME_SIZE + this->first_frame;
  else if (origin == SEEK_CUR)
    seek_to_frame = offset / CD_RAW_FRAME_SIZE + this->current_frame;
  else
    seek_to_frame = offset / CD_RAW_FRAME_SIZE + this->last_frame;

  if ((seek_to_frame >= this->first_frame) &&
      (seek_to_frame <= this->last_frame))
    this->current_frame = seek_to_frame;

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

  return CD_RAW_FRAME_SIZE;
}

static char* cdda_plugin_get_mrl (input_plugin_t *this_gen) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  return this->mrl;
}

static int cdda_plugin_get_optional_data (input_plugin_t *this_gen,
                                          void *data, int data_type) {
  return 0;
}

static void cdda_plugin_dispose (input_plugin_t *this_gen ) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  _cdda_free_cddb_info(this);

  cdda_close(this);

  free(this->mrl);

  if (this->cdda_device)
    free(this->cdda_device);
  
  free(this);
}


static int cdda_plugin_open (input_plugin_t *this_gen ) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;
  cdda_input_class_t  *class = (cdda_input_class_t *) this_gen->input_class;
  cdrom_toc            toc;
  int                  fd  = -1;
  char                *cdda_device;
  int                  err = -1;
    
#ifdef LOG
  printf("cdda_plugin_open\n");
#endif

  /* get the CD TOC */
  init_cdrom_toc(&toc);

  if( this->cdda_device )
    cdda_device = this->cdda_device;
  else
    cdda_device = class->cdda_device;

#ifndef WIN32  
  if( strchr(cdda_device,':') ) {
    fd = network_connect(cdda_device);
    if( fd != -1 ) {
      this->net_fd = fd;

      err = network_read_cdrom_toc(this->net_fd, &toc);
    }
  }
#endif

  if( this->net_fd == -1 ) {

    if (cdda_open(this, cdda_device, &toc) == -1) {
      free_cdrom_toc(&toc);
      return 0;
	}

#ifndef WIN32    
    err = read_cdrom_toc(this->fd, &toc);
#else
    err = read_cdrom_toc(this, &toc);
#endif

#ifdef LOG
	print_cdrom_toc(&toc);
#endif

  }

  
  if ( (err < 0) || (toc.first_track > (this->track + 1)) || 
      (toc.last_track < (this->track + 1))) {

	cdda_close(this);
      
    free_cdrom_toc(&toc);
    return 0;
  }

  /* set up the frame boundaries for this particular track */
  this->first_frame = this->current_frame = 
    toc.toc_entries[this->track].first_frame;
  if (this->track + 1 == toc.last_track)
    this->last_frame = toc.leadout_track.first_frame - 1;
  else
    this->last_frame = toc.toc_entries[this->track + 1].first_frame - 1;

  /* invalidate cache */
  this->cache_first = this->cache_last = -1;
  
    
  /*
   * CDDB
   */
  _cdda_free_cddb_info(this);

  this->cddb.num_tracks = toc.total_tracks;

  if(this->cddb.num_tracks) {
    int t;

    this->cddb.track = (trackinfo_t *) xine_xmalloc(sizeof(trackinfo_t) * this->cddb.num_tracks);

    for(t = 0; t < this->cddb.num_tracks; t++) {
      int length = (toc.toc_entries[t].first_frame_minute * CD_SECONDS_PER_MINUTE + 
		    toc.toc_entries[t].first_frame_second);
      
      this->cddb.track[t].start = (length * CD_FRAMES_PER_SECOND + 
				   toc.toc_entries[t].first_frame_frame);
      this->cddb.track[t].title = NULL;
    }
    
  }

  this->cddb.disc_length = (toc.leadout_track.first_frame_minute * CD_SECONDS_PER_MINUTE + 
			    toc.leadout_track.first_frame_second);
  this->cddb.disc_id     = _cdda_get_cddb_id(this);

  if(this->cddb.enabled && ((this->cddb.have_cddb_info == 0) || (_cdda_is_cd_changed(this) == 1)))
    _cdda_cddb_grab_infos(this);
  
  if(this->cddb.disc_title) {
#ifdef LOG
	  printf("Disc Title: %s\n", this->cddb.disc_title);
#endif

    if(this->stream->meta_info[XINE_META_INFO_ALBUM])
      free(this->stream->meta_info[XINE_META_INFO_ALBUM]);
    this->stream->meta_info[XINE_META_INFO_ALBUM] = strdup(this->cddb.disc_title);
  }

  if(this->cddb.track[this->track].title) {
#ifdef LOG
	  printf("Track %d Title: %s\n", this->track+1, this->cddb.track[this->track].title);
#endif

    if(this->stream->meta_info[XINE_META_INFO_TITLE])
      free(this->stream->meta_info[XINE_META_INFO_TITLE]);
    this->stream->meta_info[XINE_META_INFO_TITLE] = strdup(this->cddb.track[this->track].title);
  }
  
  if(this->cddb.disc_artist) {
#ifdef LOG
	  printf("Disc Artist: %s\n", this->cddb.disc_artist);
#endif

    if(this->stream->meta_info[XINE_META_INFO_ARTIST])
      free(this->stream->meta_info[XINE_META_INFO_ARTIST]);
    this->stream->meta_info[XINE_META_INFO_ARTIST] = strdup(this->cddb.disc_artist);
  }
  
  if(this->cddb.disc_category) {
#ifdef LOG
	  printf("Disc Category: %s\n", this->cddb.disc_category);
#endif

    if(this->stream->meta_info[XINE_META_INFO_GENRE])
      free(this->stream->meta_info[XINE_META_INFO_GENRE]);
    this->stream->meta_info[XINE_META_INFO_GENRE] = strdup(this->cddb.disc_category);
  }

  if(this->cddb.disc_year) {
#ifdef LOG
	  printf("Disc Year: %s\n", this->cddb.disc_year);
#endif

    if(this->stream->meta_info[XINE_META_INFO_YEAR])
      free(this->stream->meta_info[XINE_META_INFO_YEAR]);
    this->stream->meta_info[XINE_META_INFO_YEAR] = strdup(this->cddb.disc_year);
  }

  free_cdrom_toc(&toc);
  return 1;
}

static char ** cdda_class_get_autoplay_list (input_class_t *this_gen, 
					    int *num_files) {

  cdda_input_class_t *this = (cdda_input_class_t *) this_gen;
  cdda_input_plugin_t *ip = this->ip;
  cdrom_toc toc;
  char trackmrl[20];
  int fd, i, err = -1;

  /* free old playlist */
  for( i = 0; this->autoplaylist[i]; i++ ) {
    free( this->autoplaylist[i] );
    this->autoplaylist[i] = NULL; 
  }  
  
  /* get the CD TOC */
  init_cdrom_toc(&toc);

  fd = -1;

#ifndef WIN32
  if( strchr(this->cdda_device,':') ) {
    fd = network_connect(this->cdda_device);
    if( fd != -1 ) {
      err = network_read_cdrom_toc(fd, &toc);
    }
  }
#endif

  if (fd == -1) {
    if (cdda_open(ip, ip->cdda_device, &toc) == -1) {
      return NULL;
	}
  }


#ifndef WIN32
    err = read_cdrom_toc(fd, &toc);
#else
    err = read_cdrom_toc(ip, &toc);
#endif /* WIN32 */

#ifdef LOG
	print_cdrom_toc(&toc);
#endif

  cdda_close(ip);  
  
  if ( err < 0 )
    return NULL;
  
  for ( i = 0; i <= toc.last_track - toc.first_track; i++ ) {
    sprintf(trackmrl,"cdda:/%d",i+toc.first_track);
    this->autoplaylist[i] = strdup(trackmrl);    
  }

  *num_files = toc.last_track - toc.first_track + 1;

  free_cdrom_toc(&toc);
  return this->autoplaylist;
}

static input_plugin_t *cdda_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream,
                                    const char *mrl) {

  cdda_input_plugin_t *this;
  cdda_input_class_t  *class = (cdda_input_class_t *) cls_gen;
  int                  track;
  xine_cfg_entry_t     enable_entry, server_entry, port_entry, cachedir_entry;
  char                *cdda_device = NULL;

#ifdef LOG
  printf("cdda_class_get_instance\n");
#endif

  /* fetch the CD track to play */
  if (!strncasecmp (mrl, "cdda:/", 6)) {

    if ( strlen(mrl) > 8 && strchr(&mrl[8],'/') ) {
      int i;

      cdda_device = strdup(&mrl[6]);

      i = strlen(cdda_device)-1;
      while( i && cdda_device[i] != '/' )
        i--;

      if( i ) {
        cdda_device[i] = '\0';
        track = atoi(&cdda_device[i+1]);
      } else
        track = -1;        

    } else {
      track = atoi(&mrl[6]);
    }
    
    /* CD tracks start at 1, reject illegal tracks */
    if (track <= 0)
      return NULL;
  } else
    return NULL;

  this = (cdda_input_plugin_t *) xine_xmalloc (sizeof (cdda_input_plugin_t));
  
  class->ip = this;
  this->stream      = stream;
  this->mrl         = strdup(mrl);
  this->cdda_device = cdda_device;
  
  /* CD tracks start from 1; internal data structure indexes from 0 */
  this->track      = track - 1;
  this->cddb.track = NULL;
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
  
  /*
   * Lookup config entries.
   */
  class->ip = this;
  if(xine_config_lookup_entry(this->stream->xine, "input.cdda_use_cddb", 
			      &enable_entry)) 
    enable_cddb_changed_cb(class, &enable_entry);

  if(xine_config_lookup_entry(this->stream->xine, "input.cdda_cddb_server", 
			      &server_entry)) 
    server_changed_cb(class, &server_entry);
  
  if(xine_config_lookup_entry(this->stream->xine, "input.cdda_cddb_port", 
			      &port_entry)) 
    port_changed_cb(class, &port_entry);

  if(xine_config_lookup_entry(this->stream->xine, "input.cdda_cddb_cachedir", 
			      &cachedir_entry)) 
    cachedir_changed_cb(class, &cachedir_entry);

  return &this->input_plugin;
}


static char *cdda_class_get_identifier (input_class_t *this_gen) {
  return "cdda";
}

static char *cdda_class_get_description (input_class_t *this_gen) {
  return _("CD Digital Audio (aka. CDDA)");
}

static xine_mrl_t **cdda_class_get_dir (input_class_t *this_gen,
                                        const char *filename, int *nFiles) {

  cdda_input_class_t   *this = (cdda_input_class_t *) this_gen;

  *nFiles = 0; /* Unsupported */
  return this->mrls;
}

static void cdda_class_dispose (input_class_t *this_gen) {
  cdda_input_class_t  *this = (cdda_input_class_t *) this_gen;

  free (this->mrls);
  free (this);
}

static int cdda_class_eject_media (input_class_t *this_gen) {
  cdda_input_class_t  *this = (cdda_input_class_t *) this_gen;

  return media_eject_media (this->cdda_device);
}


static void *init_plugin (xine_t *xine, void *data) {

  cdda_input_class_t  *this;
  config_values_t     *config;

  this = (cdda_input_class_t *) xine_xmalloc (sizeof (cdda_input_class_t));

  this->xine   = xine;
  this->config = xine->config;
  config       = xine->config;

  this->input_class.get_instance       = cdda_class_get_instance;
  this->input_class.get_identifier     = cdda_class_get_identifier;
  this->input_class.get_description    = cdda_class_get_description;
  /* this->input_class.get_dir            = cdda_class_get_dir; */
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = cdda_class_get_autoplay_list;
  this->input_class.dispose            = cdda_class_dispose;
  this->input_class.eject_media        = cdda_class_eject_media;

  this->mrls = (xine_mrl_t **) xine_xmalloc(sizeof(xine_mrl_t*));
  this->mrls_allocated_entries = 0;
  
  this->cdda_device = config->register_string(config, "input.cdda_device", 
					      DEFAULT_CDDA_DEVICE,
					      _("device used for cdda drive"), NULL, 20, 
					      cdda_device_cb, (void *) this);
  
  config->register_bool(config, "input.cdda_use_cddb", 1,
			_("use cddb feature"), NULL, 10,
			enable_cddb_changed_cb, (void *) this);
  
  config->register_string(config, "input.cdda_cddb_server", CDDB_SERVER,
			  _("cddbp server name"), NULL, 10,
			  server_changed_cb, (void *) this);
  
  config->register_num(config, "input.cdda_cddb_port", CDDB_PORT,
		       _("cddbp server port"), NULL, 10,
		       port_changed_cb, (void *) this);
  
  config->register_string(config, "input.cdda_cddb_cachedir", 
			  (_cdda_cddb_get_default_location()),
			  _("cddbp cache directory"), NULL, 20, 
			  cachedir_changed_cb, (void *) this);

  return this;
}

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT, 13, "CD", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

