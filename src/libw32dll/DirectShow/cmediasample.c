#include "cmediasample.h"
#include "../wine/winerror.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static long STDCALL CMediaSample_QueryInterface(IUnknown * This,
						/* [in] */ IID* iid,
						/* [iid_is][out] */ void **ppv)
{
    Debug printf("CMediaSample_QueryInterface() called\n");
    if (!ppv)
	return E_INVALIDARG;
    if (!memcmp(iid, &IID_IUnknown, 16))
    {
	*ppv=(void*)This;
	((IMediaSample *)This)->vt->AddRef(This);
	return 0;
    }
    if (!memcmp(iid, &IID_IMediaSample, 16))
    {
	*ppv=(void*)This;
	((IMediaSample *)This)->vt->AddRef(This);
	return 0;
    }
    return E_NOINTERFACE;
}

static long STDCALL CMediaSample_AddRef(IUnknown* This)
{
    Debug printf("CMediaSample_AddRef() called\n");
    ((CMediaSample*)This)->refcount++;
    return 0;
}

static long STDCALL CMediaSample_Release(IUnknown* This)
{
    CMediaSample* parent=(CMediaSample*)This;
    Debug printf("%p: CMediaSample_Release() called, new refcount %d\n",
		 This, ((CMediaSample*)This)->refcount-1);
    if (--((CMediaSample*)This)->refcount==0)
	parent->all->vt->ReleaseBuffer((IMemAllocator*)(parent->all),
				       (IMediaSample*)This);
    return 0;
}

static HRESULT STDCALL CMediaSample_GetPointer(IMediaSample * This,
					       /* [out] */ BYTE **ppBuffer)
{
    Debug printf("%p: CMediaSample_GetPointer() called\n", This);
    if (!ppBuffer)
	return E_INVALIDARG;
    *ppBuffer=(BYTE *)((CMediaSample*)This)->block;
    return 0;
}

static long STDCALL CMediaSample_GetSize(IMediaSample * This)
{
    Debug printf("%p: CMediaSample_GetSize() called -> %d\n",
		 This, ((CMediaSample*)This)->size);
    return ((CMediaSample*)This)->size;
}

static HRESULT STDCALL CMediaSample_GetTime(IMediaSample * This,
					    /* [out] */ REFERENCE_TIME *pTimeStart,
					    /* [out] */ REFERENCE_TIME *pTimeEnd)
{
    Debug printf("%p: CMediaSample_GetTime() called\n", This);
    return E_NOTIMPL;
}

static HRESULT STDCALL CMediaSample_SetTime(IMediaSample * This,
					    /* [in] */ REFERENCE_TIME *pTimeStart,
					    /* [in] */ REFERENCE_TIME *pTimeEnd)
{
    Debug printf("%p: CMediaSample_SetTime() called\n", This);
    return E_NOTIMPL;
}

static HRESULT STDCALL CMediaSample_IsSyncPoint(IMediaSample * This)
{
    Debug printf("%p: CMediaSample_IsSyncPoint() called\n", This);
    if (((CMediaSample*)This)->isSyncPoint)
	return 0;
    return 1;
}

static HRESULT STDCALL CMediaSample_SetSyncPoint(IMediaSample * This,
						 long bIsSyncPoint)
{
    Debug printf("%p: CMediaSample_SetSyncPoint() called\n", This);
    ((CMediaSample*)This)->isSyncPoint=bIsSyncPoint;
    return 0;
}

static HRESULT STDCALL CMediaSample_IsPreroll(IMediaSample * This)
{
    Debug printf("%p: CMediaSample_IsPreroll() called\n", This);

    if (((CMediaSample*)This)->isPreroll)
	return 0;//S_OK

    return 1;//S_FALSE
}

static HRESULT STDCALL CMediaSample_SetPreroll(IMediaSample * This,
					       long bIsPreroll)
{
    Debug printf("%p: CMediaSample_SetPreroll() called\n", This);
    ((CMediaSample*)This)->isPreroll=bIsPreroll;
    return 0;
}

static long STDCALL CMediaSample_GetActualDataLength(IMediaSample * This)
{
    Debug printf("%p: CMediaSample_GetActualDataLength() called -> %d\n", This, ((CMediaSample*)This)->actual_size);
    return ((CMediaSample*)This)->actual_size;
}

static HRESULT STDCALL CMediaSample_SetActualDataLength(IMediaSample * This,
							long __MIDL_0010)
{
    Debug printf("%p: CMediaSample_SetActualDataLength(%ld) called\n", This, __MIDL_0010);
    if (__MIDL_0010 > ((CMediaSample*)This)->size)
    {
	printf("%p: ERROR: CMediaSample buffer overflow\n", This);
    }
    ((CMediaSample*)This)->actual_size=__MIDL_0010;
    return 0;
}

static HRESULT STDCALL CMediaSample_GetMediaType(IMediaSample * This,
						 AM_MEDIA_TYPE **ppMediaType)
{
    AM_MEDIA_TYPE *t=&((CMediaSample*)This)->media_type;
    
    Debug printf("%p: CMediaSample_GetMediaType() called\n", This);
    if(!ppMediaType)
	return E_INVALIDARG;
    if(!((CMediaSample*)This)->type_valid)
    {
	*ppMediaType=0;
	return 1;
    }
//    if(t.pbFormat)CoTaskMemFree(t.pbFormat);
    (*ppMediaType)=(AM_MEDIA_TYPE*)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
    memcpy(*ppMediaType, t, sizeof(AM_MEDIA_TYPE));
    (*ppMediaType)->pbFormat=(char*)CoTaskMemAlloc(t->cbFormat);
    memcpy((*ppMediaType)->pbFormat, t->pbFormat, t->cbFormat);
//    *ppMediaType=0; //media type was not changed
    return 0;
}

static HRESULT STDCALL CMediaSample_SetMediaType(IMediaSample * This,
						 AM_MEDIA_TYPE *pMediaType)
{
    AM_MEDIA_TYPE *t = &((CMediaSample*)This)->media_type;
    
    Debug printf("%p: CMediaSample_SetMediaType() called\n", This);
    if (!pMediaType)
	return E_INVALIDARG;
    if (t->pbFormat)
	CoTaskMemFree(t->pbFormat);
    t = pMediaType;
    t->pbFormat = (char*)CoTaskMemAlloc(t->cbFormat);
    memcpy(t->pbFormat, pMediaType->pbFormat, t->cbFormat);
    ((CMediaSample*)This)->type_valid=1;

    return 0;
}

static HRESULT STDCALL CMediaSample_IsDiscontinuity(IMediaSample * This)
{
    Debug printf("%p: CMediaSample_IsDiscontinuity() called\n", This);
    return 1;
}

static HRESULT STDCALL CMediaSample_SetDiscontinuity(IMediaSample * This,
						     long bDiscontinuity)
{
    Debug printf("%p: CMediaSample_SetDiscontinuity() called\n", This);
    return E_NOTIMPL;
}

static HRESULT STDCALL CMediaSample_GetMediaTime(IMediaSample * This,
						 /* [out] */ LONGLONG *pTimeStart,
						 /* [out] */ LONGLONG *pTimeEnd)
{
    Debug printf("%p: CMediaSample_GetMediaTime() called\n", This);
    return E_NOTIMPL;
}

static HRESULT STDCALL CMediaSample_SetMediaTime(IMediaSample * This,
						 /* [in] */ LONGLONG *pTimeStart,
						 /* [in] */ LONGLONG *pTimeEnd)
{
    Debug printf("%p: CMediaSample_SetMediaTime() called\n", This);
    return E_NOTIMPL;
}

void CMediaSample_SetPointer(CMediaSample *this, char* pointer)
{ this->block = pointer; }

void CMediaSample_ResetPointer(CMediaSample *this)
{ this->block = this->own_block; }
    
CMediaSample * CMediaSample_Create(IMemAllocator* allocator, long _size)
{
    CMediaSample *this;
    
    this = malloc( sizeof( CMediaSample ) );
    this->vt = malloc( sizeof( IMediaSample_vt ) );

    this->vt->QueryInterface = CMediaSample_QueryInterface;
    this->vt->AddRef = CMediaSample_AddRef;
    this->vt->Release = CMediaSample_Release;
    
    this->vt->GetPointer = CMediaSample_GetPointer;
    this->vt->GetSize = CMediaSample_GetSize;
    this->vt->GetTime = CMediaSample_GetTime;
    this->vt->SetTime = CMediaSample_SetTime;
    this->vt->IsSyncPoint = CMediaSample_IsSyncPoint;
    this->vt->SetSyncPoint = CMediaSample_SetSyncPoint;
    this->vt->IsPreroll = CMediaSample_IsPreroll;
    this->vt->SetPreroll = CMediaSample_SetPreroll;
    this->vt->GetActualDataLength = CMediaSample_GetActualDataLength;
    this->vt->SetActualDataLength = CMediaSample_SetActualDataLength;
    this->vt->GetMediaType = CMediaSample_GetMediaType;
    this->vt->SetMediaType = CMediaSample_SetMediaType;
    this->vt->IsDiscontinuity = CMediaSample_IsDiscontinuity;
    this->vt->SetDiscontinuity = CMediaSample_SetDiscontinuity;
    this->vt->GetMediaTime = CMediaSample_GetMediaTime;
    this->vt->SetMediaTime = CMediaSample_SetMediaTime;

    this->all = allocator;
    this->size = _size;
    this->refcount = 0;
    this->actual_size = 0;
    this->media_type.pbFormat = 0;
    this->isPreroll = 0;
    this->type_valid = 0;
    this->own_block = malloc(this->size);
    this->block = this->own_block;
    
    this->SetPointer = CMediaSample_SetPointer;
    this->ResetPointer = CMediaSample_ResetPointer;
    
    Debug printf("%p: Creating media sample with size %ld, buffer %p\n",
		 this, _size, this->block);
    return this;
}

void CMediaSample_Destroy(CMediaSample *this)
{
    Debug printf("%p: CMediaSample::~CMediaSample() called\n", this);
    if (!this->vt)
        printf("Second delete of CMediaSample()!!\n");
    free( this->vt );
    free( this->own_block );
    if (this->media_type.pbFormat)
	CoTaskMemFree(this->media_type.pbFormat);
    free( this );
}
