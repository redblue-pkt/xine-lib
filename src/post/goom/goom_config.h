#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>

/* #define VERSION "1.9dev5" */
/* #define _DEBUG */

#define COLOR_BGRA
/* #define COLOR_ARGB */

#ifdef COLOR_BGRA
/** position des composantes **/
    #define ROUGE 2
    #define BLEU 0
    #define VERT 1
    #define ALPHA 3
#else
    #define ROUGE 1
    #define BLEU 3
    #define VERT 2
    #define ALPHA 0
#endif
		

/*  target */
#define XMMS_PLUGIN
/* #define STANDALONE */

/*  for pc users with mmx processors. */
#ifdef ARCH_X86
#define MMX
#endif

#ifdef ARCH_PPC
#define POWERPC
#endif

/* #define VERBOSE */

#ifndef guint32
#define guint8  uint8_t
#define guin16  uint16_t
#define guint32 uint32_t
#define gint8   int8_t
#define gint16  int16_t
#define gint32  int32_t
#endif
