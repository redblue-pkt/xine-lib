#ifndef XINE_CHECK_H
#define XINE_CHECK_H

/* Start checking xine setup here */
int xine_health_check(void);

/* Get OS information */
int xine_health_check_os(void);

/* Get Kernel information */
int xine_health_check_kernel(void);

#if ARCH_X86
/* health_check MTRR */
int xine_health_check_mtrr(void);
#endif /* ARCH_X86 */

/* health_check CDROM */
int xine_health_check_cdrom(void);

/* health_check DVDROM */
int xine_health_check_dvdrom(void);

/* health_check DMA settings */
int xine_health_check_dma(void);

/* health_check X */
int xine_health_check_x(void);

/* health_check Xv extension */
int xine_health_check_xv(void);

#endif

