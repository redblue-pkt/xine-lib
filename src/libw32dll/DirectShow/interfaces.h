#ifndef DS_INTERFACES_H
#define DS_INTERFACES_H

/*

Definition of important DirectShow interfaces.
Created using freely-available DirectX 8.0 SDK
( http://msdn.microsoft.com )

*/

#include "../wine/com.h"
#include "guids.h"
#include "iunk.h"

#ifndef STDCALL
#define STDCALL __attribute__((__stdcall__))
#endif

/*typedef GUID& REFIID;*/
typedef GUID CLSID;
typedef GUID IID;

/*    Sh*t. MSVC++ and g++ use different methods of storing vtables.    */


/*typedef struct _IBaseFilter IBaseFilter;*/
typedef struct _IReferenceClock IReferenceClock;
typedef struct _IEnumPins IEnumPins;
typedef struct _IEnumMediaTypes IEnumMediaTypes;
typedef struct _IPin IPin;
typedef struct _IFilterGraph IFilterGraph;
typedef struct _IMemInputPin IMemInputPin;
typedef struct _IMemAllocator IMemAllocator;
typedef struct _IMediaSample IMediaSample;
typedef struct _IHidden IHidden;
typedef struct _IHidden2 IHidden2;
typedef struct _IDivxFilterInterface IDivxFilterInterface;

typedef struct _IBaseFilter_vt IBaseFilter_vt;
typedef struct _IReferenceClock_vt IReferenceClock_vt;
typedef struct _IEnumPins_vt IEnumPins_vt;
typedef struct _IEnumMediaTypes_vt IEnumMediaTypes_vt;
typedef struct _IPin_vt IPin_vt;
typedef struct _IFilterGraph_vt IFilterGraph_vt;
typedef struct _IMemInputPin_vt IMemInputPin_vt;
typedef struct _IMemAllocator_vt IMemAllocator_vt;
typedef struct _IMediaSample_vt IMediaSample_vt;
typedef struct _IHidden_vt IHidden_vt;
typedef struct _IHidden2_vt IHidden2_vt;
typedef struct _IDivxFilterInterface_vt IDivxFilterInterface_vt;


enum PIN_DIRECTION;

/*
class IClassFactory2
{
public:
    virtual long STDCALL QueryInterface(GUID* iid, void** ppv) =0;
    virtual long STDCALL AddRef(void) =0;
    virtual long STDCALL Release(void) =0;
    virtual long STDCALL CreateInstance(IUnknown* pUnkOuter, GUID* riid, void** ppvObject) =0;
};
*/

struct _IBaseFilter_vt
{
    INHERIT_IUNKNOWN();
   
    HRESULT STDCALL ( *GetClassID )(IBaseFilter * This,
				    /* [out] */ CLSID *pClassID);
    HRESULT STDCALL ( *Stop )(IBaseFilter * This);
    HRESULT STDCALL ( *Pause )(IBaseFilter * This);
    HRESULT STDCALL ( *Run )(IBaseFilter * This,
			     REFERENCE_TIME tStart);
    HRESULT STDCALL ( *GetState )(IBaseFilter * This,
				  /* [in] */ unsigned long dwMilliSecsTimeout,
				  ///* [out] */ FILTER_STATE *State);
				  void* State);
    HRESULT STDCALL ( *SetSyncSource )(IBaseFilter * This,
				       /* [in] */ IReferenceClock *pClock);
    HRESULT STDCALL ( *GetSyncSource )(IBaseFilter * This,
				       /* [out] */ IReferenceClock **pClock);
    HRESULT STDCALL ( *EnumPins )(IBaseFilter * This,
				  /* [out] */ IEnumPins **ppEnum);
    HRESULT STDCALL ( *FindPin )(IBaseFilter * This,
				 /* [string][in] */ const unsigned short* Id,
				 /* [out] */ IPin **ppPin);
    HRESULT STDCALL ( *QueryFilterInfo )(IBaseFilter * This,
					 // /* [out] */ FILTER_INFO *pInfo);
					 void* pInfo);
    HRESULT STDCALL ( *JoinFilterGraph )(IBaseFilter * This,
					 /* [in] */ IFilterGraph *pGraph,
					 /* [string][in] */ const unsigned short* pName);
    HRESULT STDCALL ( *QueryVendorInfo )(IBaseFilter * This,
					 /* [string][out] */ unsigned short* *pVendorInfo);
};

struct _IBaseFilter
{
    struct _IBaseFilter_vt *vt;
};


struct _IEnumPins_vt
{
    INHERIT_IUNKNOWN();
    
    HRESULT STDCALL ( *Next )(IEnumPins * This,
			      /* [in] */ unsigned long cPins,
			      /* [size_is][out] */ IPin **ppPins,
			      /* [out] */ unsigned long *pcFetched);
    HRESULT STDCALL ( *Skip )(IEnumPins * This,
			      /* [in] */ unsigned long cPins);
    HRESULT STDCALL ( *Reset )(IEnumPins * This);
    HRESULT STDCALL ( *Clone )(IEnumPins * This,
			       /* [out] */ IEnumPins **ppEnum);
};

struct _IEnumPins
{
    struct _IEnumPins_vt *vt;
};


struct _IPin_vt
{
    INHERIT_IUNKNOWN();

    HRESULT STDCALL ( *Connect )(IPin * This,
				 /* [in] */ IPin *pReceivePin,
				 /* [in] */ /*const*/ AM_MEDIA_TYPE *pmt);
    HRESULT STDCALL ( *ReceiveConnection )(IPin * This,
					   /* [in] */ IPin *pConnector,
					   /* [in] */ const AM_MEDIA_TYPE *pmt);
    HRESULT STDCALL ( *Disconnect )(IPin * This);
    HRESULT STDCALL ( *ConnectedTo )(IPin * This, /* [out] */ IPin **pPin);
    HRESULT STDCALL ( *ConnectionMediaType )(IPin * This,
					     /* [out] */ AM_MEDIA_TYPE *pmt);
    HRESULT STDCALL ( *QueryPinInfo )(IPin * This, /* [out] */ PIN_INFO *pInfo);
    HRESULT STDCALL ( *QueryDirection )(IPin * This,
					/* [out] */ PIN_DIRECTION *pPinDir);
    HRESULT STDCALL ( *QueryId )(IPin * This, /* [out] */ unsigned short* *Id);
    HRESULT STDCALL ( *QueryAccept )(IPin * This,
				     /* [in] */ const AM_MEDIA_TYPE *pmt);
    HRESULT STDCALL ( *EnumMediaTypes )(IPin * This,
					/* [out] */ IEnumMediaTypes **ppEnum);
    HRESULT STDCALL ( *QueryInternalConnections )(IPin * This,
						  /* [out] */ IPin **apPin,
						  /* [out][in] */ unsigned long *nPin);
    HRESULT STDCALL ( *EndOfStream )(IPin * This);
    HRESULT STDCALL ( *BeginFlush )(IPin * This);
    HRESULT STDCALL ( *EndFlush )(IPin * This);
    HRESULT STDCALL ( *NewSegment )(IPin * This,
				    /* [in] */ REFERENCE_TIME tStart,
				    /* [in] */ REFERENCE_TIME tStop,
				    /* [in] */ double dRate);
};

struct _IPin
{
    IPin_vt *vt;
};


struct _IEnumMediaTypes_vt
{
    INHERIT_IUNKNOWN();
    
    HRESULT STDCALL ( *Next )(IEnumMediaTypes * This,
			      /* [in] */ unsigned long cMediaTypes,
			      /* [size_is][out] */ AM_MEDIA_TYPE **ppMediaTypes,
			      /* [out] */ unsigned long *pcFetched);
    HRESULT STDCALL ( *Skip )(IEnumMediaTypes * This,
			      /* [in] */ unsigned long cMediaTypes);
    HRESULT STDCALL ( *Reset )(IEnumMediaTypes * This);
    HRESULT STDCALL ( *Clone )(IEnumMediaTypes * This,
			       /* [out] */ IEnumMediaTypes **ppEnum);
};

struct _IEnumMediaTypes
{
    IEnumMediaTypes_vt *vt;
};


struct _IMemInputPin_vt
{
    INHERIT_IUNKNOWN();
    
    HRESULT STDCALL ( *GetAllocator )(IMemInputPin * This,
				      /* [out] */ IMemAllocator **ppAllocator);
    HRESULT STDCALL ( *NotifyAllocator )(IMemInputPin * This,
					 /* [in] */ IMemAllocator *pAllocator,
					 /* [in] */ int bReadOnly);
    HRESULT STDCALL ( *GetAllocatorRequirements )(IMemInputPin * This,
						  /* [out] */ ALLOCATOR_PROPERTIES *pProps);
    HRESULT STDCALL ( *Receive )(IMemInputPin * This,
				 /* [in] */ IMediaSample *pSample);
    HRESULT STDCALL ( *ReceiveMultiple )(IMemInputPin * This,
					 /* [size_is][in] */ IMediaSample **pSamples,
					 /* [in] */ long nSamples,
					 /* [out] */ long *nSamplesProcessed);
    HRESULT STDCALL ( *ReceiveCanBlock )(IMemInputPin * This);
};

struct _IMemInputPin
{
    IMemInputPin_vt *vt;
};


struct _IMemAllocator_vt
{
    INHERIT_IUNKNOWN();

    HRESULT STDCALL ( *SetProperties )(IMemAllocator * This,
				       /* [in] */ ALLOCATOR_PROPERTIES *pRequest,
				       /* [out] */ ALLOCATOR_PROPERTIES *pActual);
    HRESULT STDCALL ( *GetProperties )(IMemAllocator * This,
				       /* [out] */ ALLOCATOR_PROPERTIES *pProps);
    HRESULT STDCALL ( *Commit )(IMemAllocator * This);
    HRESULT STDCALL ( *Decommit )(IMemAllocator * This);
    HRESULT STDCALL ( *GetBuffer )(IMemAllocator * This,
				   /* [out] */ IMediaSample **ppBuffer,
				   /* [in] */ REFERENCE_TIME *pStartTime,
				   /* [in] */ REFERENCE_TIME *pEndTime,
				   /* [in] */ unsigned long dwFlags);
    HRESULT STDCALL ( *ReleaseBuffer )(IMemAllocator * This,
				       /* [in] */ IMediaSample *pBuffer);
};

struct _IMemAllocator
{
    IMemAllocator_vt *vt;
};


struct _IMediaSample_vt
{
    INHERIT_IUNKNOWN();

    HRESULT STDCALL ( *GetPointer )(IMediaSample * This,
				    /* [out] */ unsigned char **ppBuffer);
    LONG    STDCALL ( *GetSize )(IMediaSample * This);
    HRESULT STDCALL ( *GetTime )(IMediaSample * This,
				 /* [out] */ REFERENCE_TIME *pTimeStart,
				 /* [out] */ REFERENCE_TIME *pTimeEnd);
    HRESULT STDCALL ( *SetTime )(IMediaSample * This,
				 /* [in] */ REFERENCE_TIME *pTimeStart,
				 /* [in] */ REFERENCE_TIME *pTimeEnd);
    HRESULT STDCALL ( *IsSyncPoint )(IMediaSample * This);
    HRESULT STDCALL ( *SetSyncPoint )(IMediaSample * This,
				      long bIsSyncPoint);
    HRESULT STDCALL ( *IsPreroll )(IMediaSample * This);
    HRESULT STDCALL ( *SetPreroll )(IMediaSample * This,
				    long bIsPreroll);
    LONG    STDCALL ( *GetActualDataLength )(IMediaSample * This);
    HRESULT STDCALL ( *SetActualDataLength )(IMediaSample * This,
					     long __MIDL_0010);
    HRESULT STDCALL ( *GetMediaType )(IMediaSample * This,
				      AM_MEDIA_TYPE **ppMediaType);
    HRESULT STDCALL ( *SetMediaType )(IMediaSample * This,
				      AM_MEDIA_TYPE *pMediaType);
    HRESULT STDCALL ( *IsDiscontinuity )(IMediaSample * This);
    HRESULT STDCALL ( *SetDiscontinuity )(IMediaSample * This,
					  long bDiscontinuity);
    HRESULT STDCALL ( *GetMediaTime )(IMediaSample * This,
				      /* [out] */ long long *pTimeStart,
				      /* [out] */ long long *pTimeEnd);
    HRESULT STDCALL ( *SetMediaTime )(IMediaSample * This,
				      /* [in] */ long long *pTimeStart,
				      /* [in] */ long long *pTimeEnd);
};

struct _IMediaSample
{
    struct _IMediaSample_vt *vt;
};

struct _IHidden_vt
{
    INHERIT_IUNKNOWN();
    
    HRESULT STDCALL ( *GetSmth )(IHidden * This, int* pv);
    HRESULT STDCALL ( *SetSmth )(IHidden * This, int v1, int v2);
    HRESULT STDCALL ( *GetSmth2 )(IHidden * This, int* pv);
    HRESULT STDCALL ( *SetSmth2 )(IHidden * This, int v1, int v2);
    HRESULT STDCALL ( *GetSmth3 )(IHidden * This, int* pv);
    HRESULT STDCALL ( *SetSmth3 )(IHidden * This, int v1, int v2);
    HRESULT STDCALL ( *GetSmth4 )(IHidden * This, int* pv);
    HRESULT STDCALL ( *SetSmth4 )(IHidden * This, int v1, int v2);
    HRESULT STDCALL ( *GetSmth5 )(IHidden * This, int* pv);
    HRESULT STDCALL ( *SetSmth5 )(IHidden * This, int v1, int v2);
    HRESULT STDCALL ( *GetSmth6 )(IHidden * This, int* pv);
};

struct _IHidden
{
    struct _IHidden_vt *vt;
};

struct _IHidden2_vt
{
    INHERIT_IUNKNOWN();
    
    HRESULT STDCALL ( *unk1 )(void);
    HRESULT STDCALL ( *unk2 )(void);
    HRESULT STDCALL ( *unk3 )(void);
    HRESULT STDCALL ( *DecodeGet )(IHidden2* This, int* region);
    HRESULT STDCALL ( *unk5 )(void);
    HRESULT STDCALL ( *DecodeSet )(IHidden2* This, int* region);
    HRESULT STDCALL ( *unk7 )(void);
    HRESULT STDCALL ( *unk8 )(void);
};

struct _IHidden2
{
    struct _IHidden2_vt *vt;
};

struct _IDivxFilterInterface
{
    struct _IDivxFilterInterface_vt* vt;
};

struct _IDivxFilterInterface_vt
{
    INHERIT_IUNKNOWN();
    
    HRESULT STDCALL ( *get_PPLevel )(IDivxFilterInterface* This, int* PPLevel); // current postprocessing level
    HRESULT STDCALL ( *put_PPLevel )(IDivxFilterInterface* This, int PPLevel); // new postprocessing level
    HRESULT STDCALL ( *put_DefaultPPLevel )(IDivxFilterInterface* This);
    HRESULT STDCALL ( *put_MaxDelayAllowed )(IDivxFilterInterface* This, int maxdelayallowed);
    HRESULT STDCALL ( *put_Brightness )(IDivxFilterInterface* This,  int brightness);
    HRESULT STDCALL ( *put_Contrast )(IDivxFilterInterface* This,  int contrast);
    HRESULT STDCALL ( *put_Saturation )(IDivxFilterInterface* This, int saturation);
    HRESULT STDCALL ( *get_MaxDelayAllowed )(IDivxFilterInterface* This,  int* maxdelayallowed);
    HRESULT STDCALL ( *get_Brightness)(IDivxFilterInterface* This, int* brightness);
    HRESULT STDCALL ( *get_Contrast)(IDivxFilterInterface* This, int* contrast);
    HRESULT STDCALL ( *get_Saturation )(IDivxFilterInterface* This, int* saturation);
    HRESULT STDCALL ( *put_AspectRatio )(IDivxFilterInterface* This, int x, IDivxFilterInterface* This2, int y);
    HRESULT STDCALL ( *get_AspectRatio )(IDivxFilterInterface* This, int* x, IDivxFilterInterface* This2, int* y);
};
#endif  /* DS_INTERFACES_H */
