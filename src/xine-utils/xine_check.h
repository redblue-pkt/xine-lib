#ifndef XINE_CHECK_H
#define XINE_CHECK_H
#include <stdio.h>

#ifdef XINE_COMPILE
#  include "xine.h"
#else
#  include <xine.h>
#endif

#define XINE_HEALTH_CHECK_OK            0
#define XINE_HEALTH_CHECK_FAIL          1
#define XINE_HEALTH_CHECK_UNSUPPORTED   2
#define XINE_HEALTH_CHECK_NO_SUCH_CHECK 3

#define CHECK_KERNEL    0
#define CHECK_MTRR      1
#define CHECK_CDROM     2
#define CHECK_DVDROM    3
#define CHECK_DMA       4
#define CHECK_X         5
#define CHECK_XV        6

/*
 * Start checking xine setup here
 *
 * cdrom_dev = Name of the device link for the cdrom drive (e.g. /dev/cdrom)
 * dvd_dev = Name of the device link for the dvd drive (e.g. /dev/dvd)
 */
xine_health_check_t* xine_health_check(xine_health_check_t*, int check_num);

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

