/*
 * dvdudf: parse and read the UDF volume information of a DVD Video
 * Copyright (C) 1999 Christian Wolff for convergence integrated media GmbH
 *	minor modifications by Thomas Mirlacher
 *      dir support and bugfixes by Guenter Bartsch for use in xine
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 * 
 * The author can be reached at scarabaeus@convergence.de, 
 * the project's page is at http://linuxtv.org/dvd/
 */
 

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include "dvd_udf.h"

static int _Unicodedecode (uint8_t *data, int len, char *target);

#define MAX_FILE_LEN 2048

struct Partition {
  int valid;
  uint8_t  VolumeDesc[128];
  uint16_t Flags;
  uint16_t Number;
  uint8_t  Contents[32];
  uint32_t AccessType;
  uint32_t Start;
  uint32_t Length;
} partition;

struct AD {
  uint32_t Location;
  uint32_t Length;
  uint8_t  Flags;
  uint16_t Partition;
};

/* for direct data access, LSB first */
#define GETN1(p) ((uint8_t)data[p])
#define GETN2(p) ((uint16_t)data[p]|((uint16_t)data[(p)+1]<<8))
#define GETN4(p) ((uint32_t)data[p]|((uint32_t)data[(p)+1]<<8)|((uint32_t)data[(p)+2]<<16)|((uint32_t)data[(p)+3]<<24))
#define GETN(p,n,target) memcpy(target,&data[p],n)


/*
 * reads absolute Logical Block of the disc
 * returns number of read bytes on success, 0 on error
 */

int UDFReadLB (int fd, off_t lb_number, size_t block_count, uint8_t *data)
{
  if (fd < 0)
    return 0;

  if (lseek (fd, lb_number * (off_t) DVD_VIDEO_LB_LEN, SEEK_SET) < 0)
    return 0; /* position not found */

  return read (fd, data, block_count*DVD_VIDEO_LB_LEN);
}


static int _Unicodedecode (uint8_t *data, int len, char *target)
{
  int p=1,i=0;

  if (!(data[0] & 0x18)) {
    target[0] ='\0';
    return 0;
  }

  if (data[0] & 0x10) {		/* ignore MSB of unicode16 */
    p++;

    while (p<len)
      target[i++]=data[p+=2];
  } else {
    while (p<len)
      target[i++]=data[p++];
  }
	
  target[i]='\0';

  return 0;
}


int UDFEntity (uint8_t *data, uint8_t *Flags, char *Identifier)
{
  Flags[0] = data[0];
  strncpy (Identifier, &data[1], 5);

  return 0;
}


int UDFDescriptor (uint8_t *data, uint16_t *TagID)
{
  TagID[0] = GETN2(0);
  /* TODO: check CRC n stuff */

  return 0;
}


int UDFExtentAD (uint8_t *data, uint32_t *Length, uint32_t *Location)
{
  Length[0]  =GETN4(0);
  Location[0]=GETN4(4);

  return 0;
}

#define UDFADshort	1
#define UDFADlong	2
#define UDFADext	4

int UDFAD (uint8_t *data, struct AD *ad, uint8_t type)
{
  ad->Length	= GETN4(0);
  ad->Flags	= ad->Length>>30;
  ad->Length	&= 0x3FFFFFFF;

  switch (type) {
  case UDFADshort:
    ad->Location	= GETN4(4);
    ad->Partition	= partition.Number;  /* use number of current partition */
    break;
  case UDFADlong:
    ad->Location	= GETN4(4);
    ad->Partition	= GETN2(8);
    break;
  case UDFADext:
    ad->Location	= GETN4(12);
    ad->Partition	= GETN2(16);
    break;
  }

  return 0;
}

int UDFICB (uint8_t *data, uint8_t *FileType, uint16_t *Flags)
{
  FileType[0]=GETN1(11);
  Flags[0]=GETN2(18);

  return 0;
}

int UDFPartition (uint8_t *data, uint16_t *Flags, uint16_t *Number, char *Contents, 
		  uint32_t *Start, uint32_t *Length)
{
  Flags[0]	= GETN2(20);
  Number[0]	= GETN2(22);
  GETN(24,32,Contents);
  Start[0]	= GETN4(188);
  Length[0]	= GETN4(192);

  return 0;
}


/*
 * reads the volume descriptor and checks the parameters
 * returns 0 on OK, 1 on error
 */

int UDFLogVolume (uint8_t *data, char *VolumeDescriptor)
{
  uint32_t lbsize,MT_L,N_PM;

  _Unicodedecode (&data[84],128,VolumeDescriptor);
  lbsize	= GETN4(212);		/* should be 2048 */
  MT_L	        = GETN4(264);		/* should be 6 */
  N_PM	        = GETN4(268);		/* should be 1 */

  if (lbsize!=DVD_VIDEO_LB_LEN)
    return 1;

  return 0;
}


int UDFFileEntry (uint8_t *data, uint8_t *FileType, struct AD *ad)
{
  uint8_t filetype;
  uint16_t flags;
  uint32_t L_EA,L_AD;
  int p;

  UDFICB(&data[16],&filetype,&flags);
  FileType[0]=filetype;
  L_EA=GETN4(168);
  L_AD=GETN4(172);
  p=176+L_EA;

  while (p<176+L_EA+L_AD) {
    switch (flags&0x07) {
    case 0:
      UDFAD (&data[p], ad, UDFADshort);
      p += 0x08;
      break;
    case 1:
      UDFAD (&data[p], ad, UDFADlong);
      p += 0x10;
      break;
    case 2: UDFAD (&data[p], ad, UDFADext);
      p += 0x14;
      break;
    case 3:
      switch (L_AD) {
      case 0x08:
	UDFAD (&data[p], ad, UDFADshort);
	break;
      case 0x10:
	UDFAD (&data[p], ad, UDFADlong);
	break;
      case 0x14:
	UDFAD (&data[p], ad, UDFADext);
	break;
      }
    default:
      p += L_AD;
      break;
    }
  }

  return 0;
}


int UDFFileIdentifier (uint8_t *data, uint8_t *FileCharacteristics, char *FileName, struct AD *FileICB)
{
  uint8_t L_FI;
  uint16_t L_IU;
  
  FileCharacteristics[0]=GETN1(18);
  L_FI=GETN1(19);
  UDFAD(&data[20],FileICB,UDFADlong);
  L_IU=GETN2(36);

  if (L_FI)
    _Unicodedecode (&data[38+L_IU],L_FI,FileName);
  else
    FileName[0]='\0';

  return 4*((38+L_FI+L_IU+3)/4);
}


/*
 * Maps ICB to FileAD
 * ICB: Location of ICB of directory to scan
 * FileType: Type of the file
 * File: Location of file the ICB is pointing to
 * return 1 on success, 0 on error;
 */

int UDFMapICB (int fd, struct AD ICB, uint8_t *FileType, struct AD *File)
{
  uint8_t *LogBlock;
  uint32_t lbnum;
  uint16_t TagID;

  if ((LogBlock = (uint8_t*)malloc(DVD_VIDEO_LB_LEN)) == NULL) {
    fprintf(stderr, "%s: malloc failed\n", __FUNCTION__);
    return 0;
  }

  lbnum=partition.Start+ICB.Location;

  do {
    if (!UDFReadLB(fd, lbnum++,1,LogBlock)) TagID=0;
    else UDFDescriptor(LogBlock,&TagID);

    if (TagID==261) {
      UDFFileEntry(LogBlock,FileType,File);
      free(LogBlock);
      return 1;
    };
  } while ((lbnum<=partition.Start+ICB.Location+(ICB.Length-1)/DVD_VIDEO_LB_LEN) && (TagID!=261));

  free(LogBlock);
  return 0;
}

/*  
 * Dir: Location of directory to scan
 * FileName: Name of file to look for
 * FileICB: Location of ICB of the found file
 * return 1 on success, 0 on error;
 */

int UDFScanDir (int fd, struct AD Dir, char *FileName, struct AD *FileICB)
{
  uint8_t   *LogBlock;
  uint32_t   lbnum, lb_dir_end, offset;
  uint16_t   TagID;
  uint8_t    filechar;
  char      *filename;
  int        p, retval = 0;
  
  LogBlock = (uint8_t*)malloc(DVD_VIDEO_LB_LEN * 30);
  filename = (char*)malloc(MAX_FILE_LEN);
  if ((LogBlock == NULL) || (filename == NULL)) {
    fprintf(stderr, "%s: malloc failed\n", __FUNCTION__);
    goto bail;
  }

  /*
   * read complete directory
   */
  
  lbnum        = partition.Start+Dir.Location;
  lb_dir_end   = partition.Start+Dir.Location+(Dir.Length-1)/DVD_VIDEO_LB_LEN;
  offset       = 0;
  
  while (lbnum<=lb_dir_end) {
    
    if (!UDFReadLB(fd, lbnum++,1,&LogBlock[offset])) 
      break;
    
    offset += DVD_VIDEO_LB_LEN;
  }

  /* Scan dir for ICB of file */

  p=0;
  while (p<offset) {
    UDFDescriptor (&LogBlock[p],&TagID);

    if (TagID==257) {
      p += UDFFileIdentifier(&LogBlock[p],&filechar,filename,FileICB);
      if (!strcasecmp (FileName,filename)) {
        retval = 1;
	goto bail;
      }
    } else
	  p=offset;
  }

  retval = 0;

bail:
  free(LogBlock);
  free(filename);
  return retval;
}


/*
 * looks for partition on the disc
 *   partnum: number of the partition, starting at 0
 *   part: structure to fill with the partition information
 *   return 1 if partition found, 0 on error;
 */

int UDFFindPartition (int fd, int partnum, struct Partition *part)
{
  uint8_t *LogBlock,*Anchor;
  uint32_t lbnum,MVDS_location,MVDS_length;
  uint16_t TagID;
  uint32_t lastsector;
  int i,terminate,volvalid,retval = 0;

  LogBlock = (uint8_t*)malloc(DVD_VIDEO_LB_LEN);
  Anchor = (uint8_t*)malloc(DVD_VIDEO_LB_LEN);
  if ((LogBlock == NULL) || (Anchor == NULL)) {
    fprintf(stderr, "%s: malloc failed\n", __FUNCTION__);
    goto bail;
  }

  /* find anchor */
  lastsector=0;
  lbnum=256;   /* try #1, prime anchor */
  terminate=0;

  while (1) {  /* loop da loop */
    if (UDFReadLB(fd, lbnum,1,Anchor)) {
      UDFDescriptor(Anchor,&TagID);
    } else
      TagID=0;
    
    if (TagID!=2) { /* not an anchor? */
      if (terminate) goto bail;	/* final try failed */
      if (lastsector) {		/* we already found the last sector    */
	lbnum=lastsector;	/* try #3, alternative backup anchor   */
	terminate=1;		/* but thats just about enough, then!  */
      } else {
	/* TODO: find last sector of the disc (this is optional)       */
	if (lastsector) lbnum=lastsector-256; /* try #2, backup anchor */
	else goto bail;          /* unable to find last sector          */
      }
    } else break;               /* it is an anchor! continue...        */
  }

  UDFExtentAD(&Anchor[16],&MVDS_length,&MVDS_location);  /* main volume descriptor */
  
  part->valid=0;
  volvalid=0;
  part->VolumeDesc[0]='\0';
  
  i=1;
  do {
    /* Find Volume Descriptor */
    lbnum=MVDS_location;
    do {
      if (!UDFReadLB (fd, lbnum++, 1, LogBlock))
	TagID=0;
      else
	UDFDescriptor (LogBlock, &TagID);
      if ((TagID==5) && (!part->valid)) {  /* Partition Descriptor */
	UDFPartition (LogBlock,&part->Flags,&part->Number,part->Contents,
		      &part->Start,&part->Length);
	part->valid=(partnum==part->Number);
      } else if ((TagID==6) && (!volvalid)) {  /* Logical Volume Descriptor */
	if (UDFLogVolume(LogBlock,part->VolumeDesc)) {  
          /* TODO: sector size wrong! */
	} else volvalid=1;
      }
    } while ((lbnum<=MVDS_location+(MVDS_length-1)/DVD_VIDEO_LB_LEN) && (TagID!=8) && ((!part->valid) || (!volvalid)));
    if ((!part->valid) || (!volvalid)) UDFExtentAD(&Anchor[24],&MVDS_length,&MVDS_location);  /* backup volume descriptor */
  } while (i-- && ((!part->valid) || (!volvalid)));
  
  retval = part->valid;  /* we only care for the partition, not the volume */

bail:
  free(LogBlock);
  free(Anchor);
  return retval;
}


/*
 * looks for a file on the UDF disc/imagefile and seeks to it's location
 * filename has to be the absolute pathname on the UDF filesystem,
 * starting with '/'
 * returns absolute LB number, or 0 on error
 */

uint32_t UDFFindFile (int fd, char *filename, off_t *size)
{
  uint8_t *LogBlock;
  uint8_t filetype;
  uint32_t lbnum, retval = 0;
  uint16_t TagID;
  struct AD RootICB,File,ICB;
  char *tokenline;
  char *token;
  off_t lb_number;
  int Partition=0;  /* this is the standard location for DVD Video */

  LogBlock = (uint8_t*)malloc(DVD_VIDEO_LB_LEN);
  tokenline = (char*)malloc(MAX_FILE_LEN);
  if ((LogBlock == NULL) || (tokenline == NULL)) {
    fprintf(stderr, "%s: malloc failed\n", __FUNCTION__);
    goto bail;
  }
  memset(tokenline, 0, MAX_FILE_LEN);
  
  strncat (tokenline,filename,MAX_FILE_LEN);
  
  /* Find partition */
  if (!UDFFindPartition(fd, Partition,&partition))
    goto bail;
  
  /* Find root dir ICB */
  lbnum=partition.Start;
  
  do {
    if (!UDFReadLB(fd, lbnum++,1,LogBlock))
      TagID=0;
    else
      UDFDescriptor(LogBlock,&TagID);
    
    if (TagID==256)		/* File Set Descriptor */
      UDFAD(&LogBlock[400],&RootICB,UDFADlong);
  } while ((lbnum<partition.Start+partition.Length) && (TagID!=8) && (TagID!=256));
  if (TagID!=256)
    goto bail;
  if (RootICB.Partition!=Partition)
    goto bail;
  
  /* Find root dir */
  if (!UDFMapICB(fd, RootICB,&filetype,&File))
    goto bail;
  if (filetype!=4)		/* root dir should be dir */
    goto bail;
  
  /* Tokenize filepath */
  token=strtok(tokenline,"/");
  while (token) {
    if (!UDFScanDir(fd, File,token,&ICB))
      goto bail;
    if (!UDFMapICB(fd, ICB,&filetype,&File))
      goto bail;
    token=strtok(NULL,"/");
  }
  
  *size = File.Length;

  lb_number = partition.Start+File.Location ;

  printf ("lb_number : %ld\n", (long int)lb_number);

  retval = lb_number;

bail:
  free(LogBlock);
  free(tokenline);
  return retval;
}


/*
 * lists contents of given directory 
 */

void UDFListDir(int fd, char *dirname, int nMaxFiles, char **file_list, int *nFiles) {
  uint8_t   *LogBlock;
  uint32_t   lbnum;
  uint16_t   TagID;
  struct AD  RootICB,Dir,ICB;
  char      *tokenline;
  char      *token, *ntoken;
  uint8_t    filetype;
  char      *filename;
  int        p;
  uint8_t    filechar;
  char      *dest;
  int        Partition=0;  /* this is the standard location for DVD Video */
  
  LogBlock = (uint8_t*)malloc(DVD_VIDEO_LB_LEN * 30);
  tokenline = (char*)malloc(MAX_FILE_LEN);
  filename = (char*)malloc(MAX_FILE_LEN);
  if ((LogBlock == NULL) || (tokenline == NULL) || (filename == NULL)) {
    fprintf(stderr, "%s: malloc failed\n", __FUNCTION__);
    goto bail;
  }
 
  *nFiles = 0;
  tokenline[0]='\0';
  strncat(tokenline,dirname,MAX_FILE_LEN);

  /* Find partition */
  if (!UDFFindPartition(fd, Partition,&partition)) 
    goto bail; /* no partition found (no disc ??) */
  
  /* Find root dir ICB */
  lbnum=partition.Start;
  do {
    if (!UDFReadLB(fd, lbnum++,1,LogBlock)) 
      TagID=0;
    else 
      UDFDescriptor(LogBlock,&TagID);

    if (TagID==256) {  // File Set Descriptor
      UDFAD(&LogBlock[400],&RootICB,UDFADlong);
    }
  } while ((lbnum<partition.Start+partition.Length) 
	   && (TagID!=8) && (TagID!=256));

  if (TagID!=256) 
    goto bail;
  if (RootICB.Partition!=Partition) 
    goto bail;
  
  /* Find root dir */
  if (!UDFMapICB(fd, RootICB,&filetype,&Dir)) 
    goto bail;
  if (filetype!=4) 
    goto bail;  /* root dir should be dir */

  

  /* Tokenize filepath */
  token=strtok(tokenline,"/");
  ntoken=strtok(NULL,"/");
  while (token != NULL) {

    if (!UDFScanDir(fd, Dir,token,&ICB)) 
      goto bail;
    if (!UDFMapICB(fd, ICB,&filetype,&Dir)) 
      goto bail;

    if (ntoken == NULL) {
      uint32_t lb_dir_end, offset;
      
      /*
       * read complete directory
       */

      lbnum        = partition.Start+Dir.Location;
      lb_dir_end   = partition.Start+Dir.Location+(Dir.Length-1)/DVD_VIDEO_LB_LEN;
      offset       = 0;

      while (lbnum<=lb_dir_end) {

	if (!UDFReadLB(fd, lbnum++,1,&LogBlock[offset])) 
	  break;

	offset += DVD_VIDEO_LB_LEN;
      }


      p=0;
      while (p<offset) {
	UDFDescriptor(&LogBlock[p],&TagID);
	/* printf ("tagid : %d\n",TagID);  */
	if (TagID==257) {
	  p+=UDFFileIdentifier(&LogBlock[p],&filechar,filename,&ICB);

	  /* printf ("file : >%s< %d (p: %d)\n", filename, *nFiles,p);  */

	  if (strcmp (filename,"")) {

	    dest = file_list[*nFiles];
	    strncpy (dest,filename,256);
	    (*nFiles)++;
	    
	    if ((*nFiles)>=nMaxFiles)
	      goto bail;
	    
	  }

	} else {
	  p=offset;
	}
      }

    } 

    token=ntoken;
    ntoken=strtok(NULL,"/");
  }

bail:
  free(LogBlock);
  free(tokenline);
  free(filename);
  return;
}

