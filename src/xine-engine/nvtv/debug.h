/* NVTV debug -- Dirk Thierbach <dthierbach@gmx.de>
 *
 * This is open software protected by the GPL. See GPL.txt for details.
 *
 * Debug definitions.
 *
 */

#ifndef _DEBUG_H
#define _DEBUG_H

#include <stdio.h>

/* -------- Config defines -------- */

/* Chrontel slave sync modes (normally not necessary) */
/* #define CONFIG_SLAVE_CH */

/* Brooktree async modes (test) */
/* #define CONFIG_ASYNC_BT */

/* Setup BlankEnd for Voodoo cards */
#define CONFIG_TDFX_SETUP_BLANK

/* Allow GeForce2Go */
#define CONFIG_GEFORCEGO_OK

/* Allow overlay modes */
/* #define CONFIG_OVERLAY_NV */

/* -------- Debugging defines -------- */

/* Temporary define for philips 7104 test modes */
/* #define TEST_PHILIPS */

/* Enable DPRINTF messages */
/* #define NVTV_DEBUG */

/* Disable timeout procs */
/* #define DISABLE_TIMEOUT */

/* Enable probe/debug routines */
#define DEBUG_PROBE 

/* Scan all unknown chips of no known tv chip is found (nv_tv.c) */
#define PROBE_ALL_UNKNOWN

/* The EEPROM on the XBox bus might cause problems when probed. */
#ifdef XBOX_SUPPORT
#undef PROBE_ALL_UNKOWN
#endif

/* -------- Meta */

/* Fake XBox (Meta) */
/* #define FAKE_XBOX */

/* Fake GeForce3 (Meta) */
/* #define FAKE_GEFORCE3 */

/* Fake Voodoo3 (Meta) */
/* #define FAKE_VOODOO */

/* Fake Intel i810 (Meta) */
/* #define FAKE_I810 */

/* Fake Brooktree chip (Meta) */
/* #define FAKE_BROOKTREE */

/* Fake Conexant chip (Meta) */
/* #define FAKE_CONEXANT */

/* Fake Chrontel chip (Meta) */
/* #define FAKE_CHRONTEL */

/* Fake Philips chip (Meta) */
/* #define FAKE_PHILIPS */

/* Fake TV mode (Meta) */
/* #define FAKE_TV */

/* -------- Assertions */

/* Check MMIO when faking it */
/* #define CHECK_MMIO */

/* Base and range for check */
/* #define CHECK_MMIO_BASE 0x601 */
/* #define CHECK_MMIO_MIN 0x2000 */
/* #define CHECK_MMIO_MAX 0x4000 */
/* #define CHECK_MMIO_ABORT      */

/* -------- */

/* Fake successful probing of all tv chips (nv_tv.c) */
/* #define FAKE_PROBE_ALL */

/* Fake successful probing of this addr on all busses (nv_tv.c) */
/* #define FAKE_PROBE_ADDR 0xEA */

/* Fake Brooktree chip identification */
/* #define FAKE_PROBE_BROOKTREE */

/* Fake Conexant chip identification */
/* #define FAKE_PROBE_CONEXANT */

/* Fake Chrontel chip identification */
/* #define FAKE_PROBE_CHRONTEL */

/* Fake Philips chip identification */
/* #define FAKE_PROBE_PHILIPS */

/* Fake id to return on identification */
/* #define FAKE_PROBE_ID TV_PHILIPS_7104 */

/* Fake I2C Bus reads and writes (nv_tv.c) */
/* #define FAKE_I2C */

/* Fake CRTC register writes (nv_tv.c) */
/* #define FAKE_CRTC */

/* Fake mmapped register writes (tv_nv.c) */
/* #define FAKE_MMIO */

/* Fake pci card in root backend */
/* #define FAKE_CARD */

/* Fake memory mapping */
/* #define FAKE_CARD_MMAP */

/* Fake vendor id of pci card.  */
/* #define FAKE_CARD_VENDOR PCI_VENDOR_3DFX */

/* Fake device id of pci card */
/* #define FAKE_CARD_DEVICE PCI_CHIP_VOODOO3 */

/* -------- */

#ifdef FAKE_XBOX
#define FAKE_CARD
#define FAKE_CARD_MMAP
#define FAKE_MMIO
#define FAKE_CRTC
#define FAKE_I2C
#define FAKE_CARD_VENDOR PCI_VENDOR_NVIDIA
#define FAKE_CARD_DEVICE PCI_CHIP_GEFORCE3_MCPX
#endif

#ifdef FAKE_GEFORCE3
#define FAKE_CARD
#define FAKE_CARD_MMAP
#define FAKE_MMIO
#define FAKE_CARD_VENDOR PCI_VENDOR_NVIDIA
#define FAKE_CARD_DEVICE PCI_CHIP_GEFORCE3
#endif

#ifdef FAKE_VOODOO
#define FAKE_CARD
#define FAKE_CARD_MMAP
#define FAKE_CARD_VENDOR PCI_VENDOR_3DFX
#define FAKE_CARD_DEVICE PCI_CHIP_VOODOO3
#endif

#ifdef FAKE_I810
#define FAKE_CARD
#define FAKE_CARD_MMAP
#define FAKE_CARD_VENDOR PCI_VENDOR_INTEL
#define FAKE_CARD_DEVICE PCI_CHIP_I810
#endif

#ifdef FAKE_BROOKTREE
#define FAKE_MMIO
#define FAKE_CRTC
#define FAKE_I2C
#define FAKE_PROBE_ADDR 0x8A
#define FAKE_PROBE_BROOKTREE
#endif

#ifdef FAKE_CONEXANT
#define FAKE_MMIO
#define FAKE_CRTC
#define FAKE_I2C
#define FAKE_PROBE_ADDR 0x8A
#define FAKE_PROBE_CONEXANT
#endif

#ifdef FAKE_CHRONTEL
#define FAKE_MMIO
#define FAKE_CRTC
#define FAKE_I2C
#define FAKE_PROBE_ADDR 0xEA
#define FAKE_PROBE_CHRONTEL
#endif

#ifdef FAKE_PHILIPS
#define FAKE_MMIO
#define FAKE_CRTC
#define FAKE_I2C
#define FAKE_PROBE_ADDR 0x88
#define FAKE_PROBE_PHILIPS
#endif

#ifdef FAKE_TV
#define FAKE_MMIO
#define FAKE_CRTC
#endif

#define ERROR(X...) fprintf(stderr, X)

/* Fake output */
#define FPRINTF(X...) fprintf(stderr, X)

#ifdef NVTV_DEBUG
#define DPRINTF(X...) fprintf(stderr, X)
#define NO_TIMEOUT
#else
#define DPRINTF(X...) /* */
#endif

#endif
