#ifndef __FASTMEMCPY_H__
#define __FASTMEMCPY_H__

#include "xineutils.h"

#define memcpy(a,b,c) xine_fast_memcpy(a,b,c)

#endif
