#ifndef DS_OUTPUTPIN_H
#define DS_OUTPUTPIN_H

/* "output pin" - the one that connects to output of filter. */

#include "allocator.h"

typedef struct _COutputPin COutputPin; 

typedef struct _COutputMemPin
{  
    IMemInputPin_vt *vt;
    char** frame_pointer;
    long* frame_size_pointer;
    MemAllocator* pAllocator;
    COutputPin* parent;
}COutputMemPin;

struct _COutputPin
{
    IPin_vt *vt;
    COutputMemPin* mempin;
    int refcount;
    AM_MEDIA_TYPE type;
    IPin* remote;
};


COutputPin * COutputPin_Create(const AM_MEDIA_TYPE * vh);

void COutputPin_Destroy(COutputPin *this);

void COutputPin_SetFramePointer(COutputPin *this,char** z);

void COutputPin_SetPointer2(COutputPin *this,char* p); 
    
void COutputPin_SetFrameSizePointer(COutputPin *this,long* z);

void COutputPin_SetNewFormat(COutputPin *this, AM_MEDIA_TYPE * a); 

#endif /* DS_OUTPUTPIN_H */
