#include "xine_check.h"
#include <stdio.h>
#include "xineutils.h"
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>

#if defined(__linux__)
#include <linux/major.h>
#include <linux/hdreg.h>

typedef struct {
  FILE    *fd;
  char    *filename;
  char    *ln;
  char     buf[256];
} file_info_t;

int xine_health_check()
{
	int retval = 0;

#if 0
	if (xine_health_check_os() < 0)
	{
		retval = -1;
	}
#endif

	if (xine_health_check_kernel() < 0)
	{
		retval = -1;
	}

#if ARCH_X86
	if (xine_health_check_mtrr() < 0)
	{
		retval = -1;
	}
#endif /* ARCH_X86 */

	if (xine_health_check_cdrom() < 0)
	{
		retval = -1;
	}

	if (xine_health_check_dvdrom() < 0)
	{
		retval = -1;
	}

#if 0
	if (xine_health_check_dma() < 0)
	{
		retval = -1;
	}
#endif

	if (xine_health_check_x() < 0)
	{
		retval = -1;
	}

	if (xine_health_check_xv() < 0)
	{
		retval = -1;
	}

	return retval;
}

int xine_health_check_os(void)
{
	int retval = 0;
	fprintf(stdout, "xine health_check (OS): ");

	return retval;
}

int xine_health_check_kernel(void)
{
	struct utsname kernel;

	fprintf(stdout, "xine health_check (Kernel):\n");

	if (uname(&kernel) == 0)
	{
		fprintf(stdout,"  sysname: %s\n", kernel.sysname);
		fprintf(stdout,"  release: %s\n", kernel.release);
		fprintf(stdout,"  machine: %s\n", kernel.machine);
	}
	else
	{
		fprintf(stdout,"  FAILURE - Could not get kernel information.\n");
		return -1;
	}
	return 0;
}

int xine_health_check_mtrr(void)
{
	char *file = "/proc/mtrr";
	FILE *fd;

	fprintf(stdout, "xine health_check (MTRR):\n");

	fd = fopen(file, "r");
	if (fd < 0)
	{
		fprintf(stdout, "  FAILED: mtrr is not enabled.\n");
		return -1;
	}
	else {
		fprintf(stdout, "  SUCCESS: mtrr is enabled.\n");
		fclose(fd);
	}
	return 0;
}

int xine_health_check_cdrom(void)
{
	char* cdrom_name = "/dev/cdrom";
	struct stat cdrom_st;

	fprintf(stdout, "xine health_check (CDROM):\n");

	if (stat(cdrom_name,&cdrom_st) < 0)
	{
		fprintf(stdout, "  FAILED - could not cdrom: %s.\n", cdrom_name);
		return -1;
	}
	else
	{
		fprintf(stdout, "  SUCCESS - cdrom link %s is present.\n",cdrom_name);
	}

	if ((cdrom_st.st_mode & S_IFMT) != S_IFBLK)
	{
		fprintf(stdout, "  FAILED - %s is not a block device.\n", cdrom_name);
		return -1;
	}
	else 
	{
		fprintf(stdout, "  SUCCESS - %s is a block device.\n", cdrom_name);
	}

	if ((cdrom_st.st_mode & S_IFMT & S_IRWXU & S_IRWXG & S_IRWXO) != (S_IRUSR & S_IXUSR & S_IRGRP & S_IXGRP & S_IROTH & S_IXOTH))
	{
		fprintf(stdout, "  FAILED - %s permissions are not 'rwxrwxrx'.\n",cdrom_name);
	}
	else {
		fprintf(stdout, "  SUCCESS - %s does have proper permission.\n",cdrom_name);
	}
	
	return 0;
}

int xine_health_check_dvdrom(void)
{
	char* dvdrom_name = "/dev/dvd";
	struct stat dvdrom_st;

	fprintf(stdout, "xine health_check (DVDROM):\n");

	if (stat(dvdrom_name,&dvdrom_st) < 0)
	{
		fprintf(stdout, "  FAILED - could not dvdrom: %s.\n", dvdrom_name);
		return -1;
	}
	else
	{
		fprintf(stdout, "  SUCCESS - dvdrom link %s is present.\n",dvdrom_name);
	}

	if ((dvdrom_st.st_mode & S_IFMT) != S_IFBLK)
	{
		fprintf(stdout, "  FAILED - %s is not a block device.\n", dvdrom_name);
		return -1;
	}
	else 
	{
		fprintf(stdout, "  SUCCESS - %s is a block device.\n", dvdrom_name);
	}

	if ((dvdrom_st.st_mode & S_IFMT & S_IRWXU & S_IRWXG & S_IRWXO) != (S_IRUSR & S_IXUSR & S_IRGRP & S_IXGRP & S_IROTH & S_IXOTH))
	{
		fprintf(stdout, "  FAILED - %s permissions are not 'rwxrwxrx'.\n",dvdrom_name);
	}
	else {
		fprintf(stdout, "  SUCCESS - %s does have proper permission.\n",dvdrom_name);
	}
	
	return 0;
}

int xine_health_check_dma(void)
{
	int retval = 0;
	int is_scsi_dev = 0;
	int fd = 0;
	static long param = 0;
	char* name = "/dev/hdc";
	struct stat st;

	fprintf(stdout, "xine health_check (DMA):\n");

	/* If /dev/dvd points to /dev/scd0 but the drive is IDE (e.g. /dev/hdc) and not scsi 
	 * how do we detect the correct one */	
	if (stat(name, &st)){
		perror(name);
		exit(errno);
	}
	
	if (major(st.st_rdev) == LVM_BLK_MAJOR){
		is_scsi_dev = 1;
		fprintf(stdout, "  SKIPPED - Operation not supported on SCSI disks.\n");
	}

	/* At this time due to the way my system is setup user 'root' must be runnning xine */
	fd = open(name, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
	{
		perror(name);
		exit(errno);
	}

	if (!is_scsi_dev){
		if(ioctl(fd, HDIO_GET_DMA, &param))
		{
			fprintf(stdout, "  FAILED -  HDIO_GET_DMA failed. Ensure the permissions for %s are 0664.\n", name);
		}
		if (param != 1)
		{
			fprintf(stdout, "  FAILED - DMA not turned on for %s.\n", name);
		}
		else
		{
			fprintf(stdout, "  SUCCESS - DMA turned on for %s.\n", name);
			close(fd);
		}
	}	

	return retval;
}

int xine_health_check_x(void)
{
	char* env_display = getenv("DISPLAY");

	fprintf(stdout, "xine health_check (X):\n");
	if (strlen(env_display) == 0)
	{
		fprintf(stdout, "  FAILED - DISPLAY environment variable not set.\n");
		return -1;
	}
	else
	{
		fprintf(stdout, "  SUCCESS - DISPLAY environment variable is set.\n");
	}
	return 0;
}

int xine_health_check_xv(void)
{
	int retval = 0;

	fprintf(stdout, "xine health_check (XV):\n");

	return retval;
}
#else	/* !__linux__ */
int xine_health_check()
{
	return 0;
}
#endif	/* !__linux__ */
