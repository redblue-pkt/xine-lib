#ifndef DS_ALLOCATOR_H
#define DS_ALLOCATOR_H

/*
#ifndef NOAVIFILE_HEADERS
#include "default.h" 
#else
#include "../wine/libwin32.h"
#endif
*/

#include "interfaces.h"
#include "cmediasample.h"
#include "iunk.h"

typedef struct _CMediaSample_vector
{   
    CMediaSample** m_Type;
    int m_uiSize;
    int m_uiAlloc;
} CMediaSample_vector;

typedef struct _MemAllocator
{
    IMemAllocator_vt *vt;
    
    ALLOCATOR_PROPERTIES props;

    CMediaSample_vector * used_list;
    CMediaSample_vector * free_list;
  
    char* new_pointer;
    CMediaSample* modified_sample;
    GUID interfaces[2];
    DECLARE_IUNKNOWN(MemAllocator);
    
    /*
    MemAllocator();
    ~MemAllocator();
    static long CreateAllocator(GUID* clsid, GUID* iid, void** ppv);
    */
} MemAllocator;

MemAllocator * MemAllocator_Create();
void MemAllocator_Destroy(MemAllocator *this);

long MemAllocator_CreateAllocator(GUID* clsid, GUID* iid, void** ppv);
void MemAllocator_SetPointer(MemAllocator*this, char* pointer);
void MemAllocator_ResetPointer(MemAllocator*this);

#endif /* DS_ALLOCATOR_H */
