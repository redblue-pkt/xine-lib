#include "allocator.h"
#include "cmediasample.h"
#include "../wine/com.h"
#include "../wine/winerror.h"
#include <stdio.h>
#include <stdlib.h>

//#undef Debug
//#define Debug

/*
class AllocatorKeeper
{
public:
    AllocatorKeeper()
    {
	RegisterComClass(&CLSID_MemoryAllocator, MemAllocator::CreateAllocator);
    }
    ~AllocatorKeeper()
    {
	UnregisterComClass(&CLSID_MemoryAllocator, MemAllocator::CreateAllocator);
    }
};
static AllocatorKeeper keeper;
*/
static int Allocator_Used;
 
void CMediaSample_vector_copy(CMediaSample_vector *this, CMediaSample** in, int sz, int alloc)
{
    int i;
    this->m_Type = malloc(alloc*sizeof(CMediaSample *));
    this->m_uiSize = sz;
    this->m_uiAlloc = alloc;
    for (i = 0; i < sz; i++)
	this->m_Type[i] = in[i];
}
  
CMediaSample** CMediaSample_vector_begin(CMediaSample_vector *this)
{ return this->m_Type; }
    
CMediaSample** CMediaSample_vector_end(CMediaSample_vector *this)
{ return this->m_Type + this->m_uiSize; }

void CMediaSample_vector_pop_back(CMediaSample_vector *this)
{
	this->m_uiSize--;
	if ((this->m_uiAlloc >= 8) && (this->m_uiSize < this->m_uiAlloc / 4))
	{
            CMediaSample** t = this->m_Type;
	    CMediaSample_vector_copy(this, this->m_Type, this->m_uiSize, this->m_uiAlloc / 2);
            free(t);
	}
}

void CMediaSample_vector_erase(CMediaSample_vector *this, CMediaSample** pos)
{
	if (this->m_uiSize > 0)
	{
	    while (pos < CMediaSample_vector_end(this) - 1)
	    {
		pos[0] = pos[1];
		pos++;
	    }
	    CMediaSample_vector_pop_back(this);
	}
}
    
void CMediaSample_vector_push_back(CMediaSample_vector *this, CMediaSample *m)
{
	if (this->m_uiSize + 1 >= this->m_uiAlloc)
	{
            CMediaSample** t = this->m_Type;
	    CMediaSample_vector_copy(this, this->m_Type, this->m_uiSize, this->m_uiAlloc * 2);
            free(t);
	}
	this->m_Type[this->m_uiSize++] = m;
}


int CMediaSample_vector_size(CMediaSample_vector *this)
{ return this->m_uiSize; }

void CMediaSample_vector_clear(CMediaSample_vector *this)
{
	if (this->m_uiAlloc > 4)
	{
	    free( this->m_Type );
	    this->m_uiAlloc = 4;
	    this->m_Type = malloc(this->m_uiAlloc*sizeof(CMediaSample *));
	}
	this->m_uiSize = 0;
}

CMediaSample_vector * CMediaSample_vector_create()
{
    CMediaSample_vector *this;
    this = malloc( sizeof( CMediaSample_vector ) );
    this->m_uiAlloc = 4;
    this->m_Type = malloc(sizeof(CMediaSample *) * this->m_uiAlloc);
    this->m_uiSize = 0;
    return this;
}


IMPLEMENT_IUNKNOWN(MemAllocator)

long MemAllocator_CreateAllocator(GUID* clsid, GUID* iid, void** ppv)
{
    MemAllocator* p;
    int result;
    
    if (!ppv) return -1;
    *ppv = 0;
    if (memcmp(clsid, &CLSID_MemoryAllocator, sizeof(GUID)))
	return -1;

    p = MemAllocator_Create();
    result=p->vt->QueryInterface((IUnknown*)p, iid, ppv);
    p->vt->Release((IUnknown*)p);
    return result;
}

void MemAllocator_SetPointer(MemAllocator*this, char* pointer) 
{ this->new_pointer=pointer; }
    
void MemAllocator_ResetPointer(MemAllocator*this) 
{ 
	if (this->modified_sample)
	{
	    this->modified_sample->ResetPointer(this->modified_sample);
	    this->modified_sample=0;
	}
}


void AllocatorKeeper_Create() {
RegisterComClass(&CLSID_MemoryAllocator, MemAllocator_CreateAllocator);
}

void AllocatorKeeper_Destroy() {
UnregisterComClass(&CLSID_MemoryAllocator, MemAllocator_CreateAllocator);
}


static HRESULT STDCALL MemAllocator_SetProperties(IMemAllocator * This,
						  /* [in] */ ALLOCATOR_PROPERTIES *pRequest,
						  /* [out] */ ALLOCATOR_PROPERTIES *pActual)
{
    MemAllocator* me = (MemAllocator*)This;
        
    Debug printf("MemAllocator_SetProperties() called\n");
    if (!pRequest || !pActual)
	return E_INVALIDARG;
    if (pRequest->cBuffers<=0 || pRequest->cbBuffer<=0)
	return E_FAIL;

    if (CMediaSample_vector_size(me->used_list) || CMediaSample_vector_size(me->free_list))
	return E_FAIL;
    me->props = *pRequest;
    *pActual = *pRequest;
    return 0;
}

static HRESULT STDCALL MemAllocator_GetProperties(IMemAllocator * This,
						  /* [out] */ ALLOCATOR_PROPERTIES *pProps)
{
    Debug printf("MemAllocator_GetProperties(%p) called\n", This);
    if (!pProps)
	return E_INVALIDARG;
    if (((MemAllocator*)This)->props.cbBuffer<0)
	return E_FAIL;
    *pProps=((MemAllocator*)This)->props;
    return 0;
}

static HRESULT STDCALL MemAllocator_Commit(IMemAllocator * This)
{
    int i;
    MemAllocator* me = (MemAllocator*)This;
    
    Debug printf("MemAllocator_Commit(%p) called\n", This);
    if (((MemAllocator*)This)->props.cbBuffer < 0)
	return E_FAIL;
    if (CMediaSample_vector_size(me->used_list) || CMediaSample_vector_size(me->free_list))
	return E_INVALIDARG;
    for(i = 0; i<me->props.cBuffers; i++)
	CMediaSample_vector_push_back(me->free_list,CMediaSample_Create(This, me->props.cbBuffer));

    //printf("Added mem %p: %d  %d  size: %d\n", me, me->free_list.size(), me->props.cBuffers, me->props.cbBuffer);
    return 0;
}

static HRESULT STDCALL MemAllocator_Decommit(IMemAllocator * This)
{
    MemAllocator* me=(MemAllocator*)This;
    CMediaSample **it;
    Debug printf("MemAllocator_Decommit(%p) called\n", This);
    
    //printf("Deleted mem %p: %d  %d\n", me, me->free_list.size(), me->used_list.size());
    for(it=CMediaSample_vector_begin(me->free_list); it!=CMediaSample_vector_end(me->free_list); it++)
	CMediaSample_Destroy(*it);
    for(it=CMediaSample_vector_begin(me->used_list); it!=CMediaSample_vector_end(me->used_list); it++)
	CMediaSample_Destroy(*it);
    
    CMediaSample_vector_clear(me->free_list);
    CMediaSample_vector_clear(me->used_list);
    return 0;
}

static HRESULT STDCALL MemAllocator_GetBuffer(IMemAllocator * This,
					      /* [out] */ IMediaSample **ppBuffer,
					      /* [in] */ REFERENCE_TIME *pStartTime,
					      /* [in] */ REFERENCE_TIME *pEndTime,
					      /* [in] */ DWORD dwFlags)
{
    MemAllocator* me = (MemAllocator*)This;
    CMediaSample **it;
     
    it = CMediaSample_vector_begin(me->free_list);
    
    Debug printf("MemAllocator_GetBuffer(%p) called\n", This);
    if (CMediaSample_vector_size(me->free_list) == 0)
    {
	Debug printf("No samples available\n");
	return E_FAIL;//should block here if no samples are available
    }
    CMediaSample_vector_push_back(me->used_list,*it);
    *ppBuffer = (IMediaSample *)*it;
    (*ppBuffer)->vt->AddRef((IUnknown*)*ppBuffer);
    if (me->new_pointer)
    {
	if(me->modified_sample)
	    me->modified_sample->ResetPointer(me->modified_sample);
	(*it)->SetPointer(*it,me->new_pointer);
	me->modified_sample = *it;
	me->new_pointer = 0;
    }
    CMediaSample_vector_erase(me->free_list,it);
    return 0;
}

static HRESULT STDCALL MemAllocator_ReleaseBuffer(IMemAllocator * This,
						  /* [in] */ IMediaSample *pBuffer)
{
    MemAllocator* me = (MemAllocator*)This;
    CMediaSample **it;
        
    Debug printf("MemAllocator_ReleaseBuffer(%p) called\n", This);
    
    for (it = CMediaSample_vector_begin(me->used_list); it != CMediaSample_vector_end(me->used_list); it++)
	if ( *it == (CMediaSample*)pBuffer)
	{
	    CMediaSample_vector_erase(me->used_list,it);
	    CMediaSample_vector_push_back(me->free_list,(CMediaSample*)pBuffer);
	    return 0;
	}
    Debug printf("Releasing unknown buffer\n");
    return E_FAIL;
}

MemAllocator * MemAllocator_Create()
{
    MemAllocator *this;
    
    this = malloc(sizeof(MemAllocator));
    
    Debug printf("MemAllocator::MemAllocator() called\n");
    this->vt = malloc(sizeof(IMemAllocator_vt));
    
    this->vt->QueryInterface = MemAllocator_QueryInterface;
    this->vt->AddRef = MemAllocator_AddRef;
    this->vt->Release = MemAllocator_Release;
    this->vt->SetProperties = MemAllocator_SetProperties;
    this->vt->GetProperties = MemAllocator_GetProperties;
    this->vt->Commit = MemAllocator_Commit;
    this->vt->Decommit = MemAllocator_Decommit;
    this->vt->GetBuffer = MemAllocator_GetBuffer;
    this->vt->ReleaseBuffer = MemAllocator_ReleaseBuffer;

    this->refcount = 1;
    this->props.cBuffers = 1;
    this->props.cbBuffer = 65536; /* :/ */
    this->props.cbAlign = this->props.cbPrefix = 0;

    this->new_pointer=0;
    this->modified_sample=0;

    this->interfaces[0]=IID_IUnknown;
    this->interfaces[1]=IID_IMemAllocator;
    
    this->used_list = CMediaSample_vector_create();
    this->free_list = CMediaSample_vector_create();
    
    if( Allocator_Used++ == 0)
      RegisterComClass(&CLSID_MemoryAllocator, MemAllocator_CreateAllocator);
        
    return this;
}

void MemAllocator_Destroy(MemAllocator *this)
{
    if( --Allocator_Used == 0)
      UnregisterComClass(&CLSID_MemoryAllocator, MemAllocator_CreateAllocator);
    
    Debug printf("MemAllocator::~MemAllocator() called\n");
    free( this->vt );
}
