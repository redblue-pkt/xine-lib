/* 
 * Copyright (C) 2000-2002 the xine project, 
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
 */

/* Standard includes */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

#if defined(__NetBSD__) || defined(__OpenBSD__) || defined(__FreeBSD__)
#include <sys/cdio.h> /* CDIOCALLOW etc... */
#elif defined(HAVE_LINUX_CDROM_H)
#include <linux/cdrom.h>
#elif defined(HAVE_SYS_CDIO_H)
#include <sys/cdio.h>
#else
#warning "This might not compile due to missing cdrom ioctls"
#endif


#define LOG_MEDIA_EJECT

static int media_umount_media(char *device)
{
  char *argv[10];
  int i;
  pid_t pid;
  int status;

  argv[0]="umount";
  argv[1]=device;
  argv[2]=0;
  pid=fork();
  if (pid == 0) {
    i= execv("/bin/umount", argv);
    exit(127);
  }
  do {
    if(waitpid(pid, &status, 0) == -1) {
      if (errno != EINTR)
	return -1;
    } 
    else {
      return WEXITSTATUS(status);
    }
  } while(1);
  
  return -1;
} 

int media_eject_media (char *device) {
  int   ret, status;
  int   fd;

  /* printf("input_dvd: Eject Device %s current device %s opened=%d handle=%p trying...\n",device, this->current_dvd_device, this->opened, this->dvdnav); */
  media_umount_media(device);
  /**********
        printf("ipnut_dvd: umount result: %s\n", 
                  strerror(errno));  
   ***********/
  if ((fd = open (device, O_RDONLY|O_NONBLOCK)) > -1) {

#if defined (__linux__)
    if((status = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT)) > 0) {
      switch(status) {
      case CDS_TRAY_OPEN:
        if((ret = ioctl(fd, CDROMCLOSETRAY)) != 0) {
#ifdef LOG_MEDIA_EJECT
          printf("input_dvd: CDROMCLOSETRAY failed: %s\n", 
                  strerror(errno));  
#endif
        }
        break;
      case CDS_DISC_OK:
        if((ret = ioctl(fd, CDROMEJECT)) != 0) {
#ifdef LOG_MEDIA_EJECT
          printf("input_dvd: CDROMEJECT failed: %s\n", strerror(errno));  
#endif
        }
        break;
      }
    }
    else {
#ifdef LOG_MEDIA_EJECT
      printf("input_dvd: CDROM_DRIVE_STATUS failed: %s\n", 
              strerror(errno));
#endif
      close(fd);
      return 0;
    }
#elif defined (__NetBSD__) || defined (__OpenBSD__) || defined (__FreeBSD__)

    if (ioctl(fd, CDIOCALLOW) == -1) {
      perror("ioctl(cdromallow)");
    } else {
      if (ioctl(fd, CDIOCEJECT) == -1) {
        perror("ioctl(cdromeject)");
      }
    }

#endif

    close(fd);
  } else {
    printf("input_dvd: Device %s failed to open during eject calls\n",device);
  }
  return 1;
}

