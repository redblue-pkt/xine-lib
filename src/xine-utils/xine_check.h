#ifndef XINE_CHECK_H
#define XINE_CHECK_H

/*
 * Start checking xine setup here
 *
 * cdrom_dev = Name of the device link for the cdrom drive (e.g. /dev/cdrom)
 * dvd_dev = Name of the device link for the dvd drive (e.g. /dev/dvd)
 */
int xine_health_check(char* cdrom_dev, char* dvd_dev);

/* Get OS information */
int xine_health_check_os(void);

/* Get Kernel information */
int xine_health_check_kernel(void);

#if ARCH_X86
/* health_check MTRR */
int xine_health_check_mtrr(void);
#endif /* ARCH_X86 */

/* health_check CDROM */
int xine_health_check_cdrom(char* cdrom_dev);

/* health_check DVDROM */
int xine_health_check_dvdrom(char* dvd_dev);

/* health_check DMA settings of DVD drive*/
int xine_health_check_dma(char* dvd_dev);

/* health_check X */
int xine_health_check_x(void);

/* health_check Xv extension */
int xine_health_check_xv(void);

#endif

