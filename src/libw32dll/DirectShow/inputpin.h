#ifndef DS_INPUTPIN_H
#define DS_INPUTPIN_H

#include "interfaces.h"

//class CBaseFilter2;

typedef struct _CBaseFilter
{
    struct _IBaseFilter_vt *vt;
    
    IPin* pin;
    IPin* unused_pin;
    GUID interfaces[2];
    DECLARE_IUNKNOWN(CBaseFilter)
} CBaseFilter;

typedef struct _CInputPin
{
    IPin_vt *vt;
    
    AM_MEDIA_TYPE type;
    CBaseFilter* parent;
    GUID interfaces[1];
    DECLARE_IUNKNOWN(CInputPin)

} CInputPin;

typedef struct _CBaseFilter2
{
    struct _IBaseFilter_vt *vt;
    
    IPin* pin;
    GUID interfaces[5];
    DECLARE_IUNKNOWN(CBaseFilter2)
    
}CBaseFilter2;


typedef struct _CRemotePin
{
    IPin_vt *vt;
    CBaseFilter* parent;
    IPin* remote_pin;
    GUID interfaces[1];
    DECLARE_IUNKNOWN(CRemotePin)
}CRemotePin;

typedef struct _CRemotePin2
{
    IPin_vt *vt;
    CBaseFilter2* parent;
    GUID interfaces[1];
    DECLARE_IUNKNOWN(CRemotePin2)
}CRemotePin2;


long STDCALL CInputPin_Connect (
    IPin * This,
    /* [in] */ IPin *pReceivePin,
    /* [in] */ AM_MEDIA_TYPE *pmt);

long STDCALL CInputPin_ReceiveConnection(IPin * This,
					  /* [in] */ IPin *pConnector,
					  /* [in] */ const AM_MEDIA_TYPE *pmt);

long STDCALL CInputPin_Disconnect(IPin * This);
long STDCALL CInputPin_ConnectedTo(IPin * This, /* [out] */ IPin **pPin);

long STDCALL CInputPin_ConnectionMediaType(IPin * This,
					    /* [out] */ AM_MEDIA_TYPE *pmt);

long STDCALL CInputPin_QueryPinInfo(IPin * This, /* [out] */ PIN_INFO *pInfo);
long STDCALL CInputPin_QueryDirection(IPin * This,
				       /* [out] */ PIN_DIRECTION *pPinDir);
long STDCALL CInputPin_QueryId(IPin * This, /* [out] */ unsigned short* *Id);

long STDCALL CInputPin_QueryAccept(IPin * This,
				    /* [in] */ const AM_MEDIA_TYPE *pmt);


long STDCALL CInputPin_EnumMediaTypes (
    IPin * This,
    /* [out] */ IEnumMediaTypes **ppEnum);

long STDCALL CInputPin_QueryInternalConnections(IPin * This,
						 /* [out] */ IPin **apPin,
						 /* [out][in] */ unsigned long *nPin);

long STDCALL CInputPin_EndOfStream (IPin * This);
long STDCALL CInputPin_BeginFlush(IPin * This);

long STDCALL CInputPin_EndFlush(IPin * This);

long STDCALL CInputPin_NewSegment(IPin * This,
				   /* [in] */ REFERENCE_TIME tStart,
				   /* [in] */ REFERENCE_TIME tStop,
				   /* [in] */ double dRate);

CInputPin * CInputPin_Create(CBaseFilter* p, const AM_MEDIA_TYPE *vh);
void CInputPin_Destroy(CInputPin * this);

long STDCALL CBaseFilter_GetClassID(IBaseFilter * This,
				      /* [out] */ CLSID *pClassID);
long STDCALL CBaseFilter_Stop(IBaseFilter * This);

long STDCALL CBaseFilter_Pause(IBaseFilter * This);

long STDCALL CBaseFilter_Run(IBaseFilter * This,
			      REFERENCE_TIME tStart);

long STDCALL CBaseFilter_GetState(IBaseFilter * This,
				   /* [in] */ unsigned long dwMilliSecsTimeout,
				   // /* [out] */ FILTER_STATE *State)
				   void* State);

long STDCALL CBaseFilter_SetSyncSource(IBaseFilter * This,
					/* [in] */ IReferenceClock *pClock);

long STDCALL CBaseFilter_GetSyncSource (
        IBaseFilter * This,
        /* [out] */ IReferenceClock **pClock);


long STDCALL CBaseFilter_EnumPins (
        IBaseFilter * This,
        /* [out] */ IEnumPins **ppEnum);

long STDCALL CBaseFilter_FindPin (
        IBaseFilter * This,
        /* [string][in] */ const unsigned short* Id,
        /* [out] */ IPin **ppPin);


long STDCALL CBaseFilter_QueryFilterInfo (
        IBaseFilter * This,
//        /* [out] */ FILTER_INFO *pInfo)
	void* pInfo);

long STDCALL CBaseFilter_JoinFilterGraph (
        IBaseFilter * This,
        /* [in] */ IFilterGraph *pGraph,
        /* [string][in] */ const unsigned short* pName);


long STDCALL CBaseFilter_QueryVendorInfo (
        IBaseFilter * This,
        /* [string][out] */ unsigned short* *pVendorInfo);

CBaseFilter * CBaseFilter_Create(const AM_MEDIA_TYPE *type, CBaseFilter2* parent);


void CBaseFilter_Destroy(CBaseFilter *this);
IPin* CBaseFilter_GetPin(CBaseFilter *this);
IPin* CBaseFilter_GetUnusedPin(CBaseFilter *this);
long STDCALL CBaseFilter2_GetClassID (
        IBaseFilter * This,
        /* [out] */ CLSID *pClassID);

long STDCALL CBaseFilter2_Stop (
        IBaseFilter * This);
long STDCALL CBaseFilter2_Pause (IBaseFilter * This);

long STDCALL CBaseFilter2_Run (IBaseFilter * This, REFERENCE_TIME tStart);

long STDCALL CBaseFilter2_GetState (
        IBaseFilter * This,
        /* [in] */ unsigned long dwMilliSecsTimeout,
//        /* [out] */ FILTER_STATE *State)
    	void* State);

long STDCALL CBaseFilter2_SetSyncSource (
        IBaseFilter * This,
        /* [in] */ IReferenceClock *pClock);
long STDCALL CBaseFilter2_GetSyncSource (
        IBaseFilter * This,
        /* [out] */ IReferenceClock **pClock);

long STDCALL CBaseFilter2_EnumPins (
        IBaseFilter * This,
        /* [out] */ IEnumPins **ppEnum);
long STDCALL CBaseFilter2_FindPin (
        IBaseFilter * This,
        /* [string][in] */ const unsigned short* Id,
        /* [out] */ IPin **ppPin);

long STDCALL CBaseFilter2_QueryFilterInfo (
        IBaseFilter * This,
//        /* [out] */ FILTER_INFO *pInfo)
	void* pInfo);

long STDCALL CBaseFilter2_JoinFilterGraph(IBaseFilter * This,
					   /* [in] */ IFilterGraph *pGraph,
					   /* [string][in] */
					   const unsigned short* pName);

long STDCALL CBaseFilter2_QueryVendorInfo(IBaseFilter * This,
					   /* [string][out] */
					   unsigned short* *pVendorInfo);
CBaseFilter2 * CBaseFilter2_Create();
void CBaseFilter2_Destroy(CBaseFilter2 *this);

IPin* CBaseFilter2_GetPin(CBaseFilter2 *this);
				       
CRemotePin * CRemotePin_Create(CBaseFilter* pt, IPin* rpin);
void CRemotePin_Destroy(CRemotePin * this);
CRemotePin2 * CRemotePin2_Create(CBaseFilter2* p);
void CRemotePin2_Destroy(CRemotePin2 * this);


#endif /* DS_INPUTPIN_H */
