#ifndef DS_CMEDIASAMPLE_H
#define DS_CMEDIASAMPLE_H

#include "interfaces.h"
#include "guids.h"

typedef struct _CMediaSample
{
    IMediaSample_vt *vt;
        
    IMemAllocator* all;
    int size;
    int actual_size;
    char* block;
    char* own_block;
    int refcount;
    int isPreroll;
    int isSyncPoint;
    AM_MEDIA_TYPE media_type;
    int type_valid;
    
    /*
    CMediaSample(IMemAllocator* allocator, long _size);
    ~CMediaSample();
    */ 
    
    void (*SetPointer)(struct _CMediaSample *this, char* pointer);
    void (*ResetPointer)(struct _CMediaSample *this);
} CMediaSample;

CMediaSample * CMediaSample_Create(IMemAllocator* allocator, long _size);
void CMediaSample_Destroy(CMediaSample *this);

#endif /* DS_CMEDIASAMPLE_H */
