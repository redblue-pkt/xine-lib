/* Memory manager interface */
#include <stdio.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "libdha.h"
#include "kernelhelper/dhahelper.h"

static int libdha_fd=-1;

#define ALLOWED_VER 2
int bm_open( void )
{
  int retv;
  libdha_fd = open("/dev/dhahelper",O_RDWR);
  retv = libdha_fd > 0 ? 0 : ENXIO;
  if(!retv)
  {
    int ver;
    ioctl(libdha_fd,DHAHELPER_GET_VERSION,&ver);
    if(ver < ALLOWED_VER)
    {
	printf("libdha: You have wrong version (%i) of /dev/dhahelper\n"
	       "libdha: Please upgrade your driver up to ver=%i\n",ver,ALLOWED_VER);
	retv = EINVAL;
	close(libdha_fd);
    }
  }
  else printf("libdha: Can't open /dev/dhahelper\n");
  return retv;
}

void bm_close( void )
{
  close(libdha_fd);
}

int bm_virt_to_phys( void * virt_addr, unsigned long length, unsigned long * parray )
{
    dhahelper_vmi_t vmi;
    vmi.virtaddr = virt_addr;
    vmi.length = length;
    vmi.realaddr = parray;
    if(libdha_fd > 0) return ioctl(libdha_fd,DHAHELPER_VIRT_TO_PHYS,&vmi);
    return ENXIO;
}

int bm_virt_to_bus( void * virt_addr, unsigned long length, unsigned long * barray )
{
    dhahelper_vmi_t vmi;
    vmi.virtaddr = virt_addr;
    vmi.length = length;
    vmi.realaddr = barray;
    if(libdha_fd > 0) return ioctl(libdha_fd,DHAHELPER_VIRT_TO_BUS,&vmi);
    return ENXIO;
}
