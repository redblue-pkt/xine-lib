#ifndef XINE_CHECK_H
#define XINE_CHECK_H
#include <stdio.h>


#define XINE_HEALTH_CHECK_OK            0
#define XINE_HEALTH_CHECK_FAIL          1
#define XINE_HEALTH_CHECK_UNSUPPORTED   2

struct xine_health_check_s {
  int status;
  const char* cdrom_dev;
  const char* dvd_dev;
  char* msg;
};

typedef struct xine_health_check_s xine_health_check_t;

typedef struct {
  FILE    *fd;
  char    *filename;
  char    *ln;
  char     buf[256];
} file_info_t;


/*
 * Start checking xine setup here
 *
 * cdrom_dev = Name of the device link for the cdrom drive (e.g. /dev/cdrom)
 * dvd_dev = Name of the device link for the dvd drive (e.g. /dev/dvd)
 */
xine_health_check_t* xine_health_check(xine_health_check_t*);

#if 0
/* Get OS information */
xine_health_check_t* xine_health_check_os(void);
#endif

/* Get Kernel information */
xine_health_check_t* xine_health_check_kernel(xine_health_check_t*);

/* health_check MTRR */
xine_health_check_t* xine_health_check_mtrr(xine_health_check_t*);

/* health_check CDROM */
xine_health_check_t* xine_health_check_cdrom(xine_health_check_t*);

/* health_check DVDROM */
xine_health_check_t* xine_health_check_dvdrom(xine_health_check_t*);

/* health_check DMA settings of DVD drive*/
xine_health_check_t* xine_health_check_dma(xine_health_check_t*);

/* health_check X */
xine_health_check_t* xine_health_check_x(xine_health_check_t*);

/* health_check Xv extension */
xine_health_check_t* xine_health_check_xv(xine_health_check_t*);

#endif

