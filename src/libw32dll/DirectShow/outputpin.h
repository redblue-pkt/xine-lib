#ifndef DS_OUTPUTPIN_H
#define DS_OUTPUTPIN_H

/* "output pin" - the one that connects to output of filter. */

#include "allocator.h"

typedef struct _COutputPin COutputPin;

typedef struct _COutputMemPin COutputMemPin;
struct _COutputMemPin
{
    IMemInputPin_vt* vt;
    DECLARE_IUNKNOWN();
    char** frame_pointer;
    long* frame_size_pointer;
    MemAllocator* pAllocator;
    COutputPin* parent;
};

struct _COutputPin
{
    IPin_vt* vt;
    DECLARE_IUNKNOWN();
    COutputMemPin* mempin;
    AM_MEDIA_TYPE type;
    IPin* remote;
    void ( *SetFramePointer )(COutputPin*, char** z);
    void ( *SetPointer2 )(COutputPin*, char* p);
    void ( *SetFrameSizePointer )(COutputPin*, long* z);
    void ( *SetNewFormat )(COutputPin*, const AM_MEDIA_TYPE* a);
};

COutputPin* COutputPinCreate(const AM_MEDIA_TYPE* vhdr);

typedef struct _CEnumMediaTypes  CEnumMediaTypes;

struct _CEnumMediaTypes
{
    IEnumMediaTypes_vt* vt;
    DECLARE_IUNKNOWN();
    AM_MEDIA_TYPE type;
    GUID interfaces[2];
} ;


void CEnumMediaTypes_Destroy(CEnumMediaTypes* This);
CEnumMediaTypes* CEnumMediaTypesCreate(const AM_MEDIA_TYPE* amt);

#endif /* DS_OUTPUTPIN_H */
