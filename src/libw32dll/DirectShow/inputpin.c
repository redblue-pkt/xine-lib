#include "inputpin.h"
#include "../wine/winerror.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

IMPLEMENT_IUNKNOWN(CInputPin)

IMPLEMENT_IUNKNOWN(CRemotePin)

IMPLEMENT_IUNKNOWN(CRemotePin2)

IMPLEMENT_IUNKNOWN(CBaseFilter)

IMPLEMENT_IUNKNOWN(CBaseFilter2)

typedef struct _CEnumPins
{
    struct _IEnumPins_vt *vt;
    IPin* pin1;
    IPin* pin2;
    int counter;
    GUID interfaces[2];
    DECLARE_IUNKNOWN(CEnumPins)

} CEnumPins;

long STDCALL CEnumPins_Next(IEnumPins * This,
			     /* [in] */ unsigned long cMediaTypes,
			     /* [size_is][out] */ IPin **ppMediaTypes,
			     /* [out] */ unsigned long *pcFetched)
{
    int *lcounter=&((CEnumPins*)This)->counter;
    IPin* lpin1=((CEnumPins*)This)->pin1;
    IPin* lpin2=((CEnumPins*)This)->pin2;
    
    Debug printf("CEnumPins::Next() called\n");
    if (!ppMediaTypes)
	return E_INVALIDARG;
    if (!pcFetched && (cMediaTypes!=1))
	return E_INVALIDARG;
    if (cMediaTypes<=0)
	return 0;

    if (((*lcounter == 2) && lpin2) || ((*lcounter == 1) && !lpin2))
    {
	if (pcFetched)
	    *pcFetched=0;
	return 1;
    }

    if (pcFetched)
	*pcFetched=1;
    if (*lcounter==0)
    {
	*ppMediaTypes = lpin1;
	lpin1->vt->AddRef((IUnknown*)lpin1);
    }
    else
    {
	*ppMediaTypes = lpin2;
	lpin2->vt->AddRef((IUnknown*)lpin2);
    }
    (*lcounter)++;
    if (cMediaTypes == 1)
	return 0;
    return 1;
}

long STDCALL CEnumPins_Skip(IEnumPins * This,
			     /* [in] */ unsigned long cMediaTypes)
{
    Debug printf("CEnumPins::Skip() called\n");
    return E_NOTIMPL;
}

long STDCALL CEnumPins_Reset(IEnumPins * This)
{
    Debug printf("CEnumPins::Reset() called\n");
    ((CEnumPins*)This)->counter=0;
    return 0;
}

long STDCALL CEnumPins_Clone(IEnumPins * This,
			      /* [out] */ IEnumPins **ppEnum)
{
    Debug printf("CEnumPins::Clone() called\n");
    return E_NOTIMPL;
}

void CEnumPins_Destroy(CEnumPins *this)
{
    free(this);
}

IMPLEMENT_IUNKNOWN(CEnumPins)

CEnumPins * CEnumPins_Create(IPin* p, IPin* pp)
{
    CEnumPins *this;
    this = malloc(sizeof(CEnumPins));
    
    this->vt=malloc(sizeof(IEnumPins_vt));
    
    this->pin1 = malloc(sizeof(IPin));
    memcpy(this->pin1,p,sizeof(IPin));
    this->pin2 = malloc(sizeof(IPin));
    memcpy(this->pin2,pp,sizeof(IPin));
    this->counter = 0;
    this->refcount = 1;
    
    this->vt->QueryInterface = CEnumPins_QueryInterface;
    this->vt->AddRef = CEnumPins_AddRef;
    this->vt->Release = CEnumPins_Release;
    this->vt->Next = CEnumPins_Next;
    this->vt->Skip = CEnumPins_Skip;
    this->vt->Reset = CEnumPins_Reset;
    this->vt->Clone = CEnumPins_Clone;
    this->interfaces[0]=IID_IUnknown;
    this->interfaces[1]=IID_IEnumPins;
    return this;
}


long STDCALL CInputPin_Connect (
    IPin * This,
    /* [in] */ IPin *pReceivePin,
    /* [in] */ AM_MEDIA_TYPE *pmt)
{
    Debug printf("CInputPin::Connect() called\n");
    return E_NOTIMPL;
}

long STDCALL CInputPin_ReceiveConnection(IPin * This,
					  /* [in] */ IPin *pConnector,
					  /* [in] */ const AM_MEDIA_TYPE *pmt)
{
    Debug printf("CInputPin::ReceiveConnection() called\n");
    return E_NOTIMPL;
}

long STDCALL CInputPin_Disconnect(IPin * This)
{
    Debug printf("CInputPin::Disconnect() called\n");
    return E_NOTIMPL;
}

long STDCALL CInputPin_ConnectedTo(IPin * This, /* [out] */ IPin **pPin)
{
    Debug printf("CInputPin::ConnectedTo() called\n");
    return E_NOTIMPL;
}

long STDCALL CInputPin_ConnectionMediaType(IPin * This,
					    /* [out] */ AM_MEDIA_TYPE *pmt)
{
    Debug printf("CInputPin::ConnectionMediaType() called\n");
    if(!pmt)return E_INVALIDARG;
    *pmt=((CInputPin*)This)->type;
    if(pmt->cbFormat>0)
    {
	pmt->pbFormat=(char *)CoTaskMemAlloc(pmt->cbFormat);
	memcpy(pmt->pbFormat, ((CInputPin*)This)->type.pbFormat, pmt->cbFormat);
    }
    return 0;
}

long STDCALL CInputPin_QueryPinInfo(IPin * This, /* [out] */ PIN_INFO *pInfo)
{
    CBaseFilter* lparent=((CInputPin*)This)->parent;
    Debug printf("CInputPin::QueryPinInfo() called\n");
    pInfo->dir=PINDIR_OUTPUT;
    pInfo->pFilter = (IBaseFilter *)lparent;
    lparent->vt->AddRef((IUnknown*)lparent);
    pInfo->achName[0]=0;
    return 0;
}

long STDCALL CInputPin_QueryDirection(IPin * This,
				       /* [out] */ PIN_DIRECTION *pPinDir)
{
    *pPinDir=PINDIR_OUTPUT;
    Debug printf("CInputPin::QueryDirection() called\n");
    return 0;
}

long STDCALL CInputPin_QueryId(IPin * This, /* [out] */ unsigned short* *Id)
{
    Debug printf("CInputPin::QueryId() called\n");
    return E_NOTIMPL;
}

long STDCALL CInputPin_QueryAccept(IPin * This,
				    /* [in] */ const AM_MEDIA_TYPE *pmt)
{
    Debug printf("CInputPin::QueryAccept() called\n");
    return E_NOTIMPL;
}


long STDCALL CInputPin_EnumMediaTypes (
    IPin * This,
    /* [out] */ IEnumMediaTypes **ppEnum)
{
    Debug printf("CInputPin::EnumMediaTypes() called\n");
    return E_NOTIMPL;
}


long STDCALL CInputPin_QueryInternalConnections(IPin * This,
						 /* [out] */ IPin **apPin,
						 /* [out][in] */ unsigned long *nPin)
{
    Debug printf("CInputPin::QueryInternalConnections() called\n");
    return E_NOTIMPL;
}

long STDCALL CInputPin_EndOfStream (IPin * This)
{
    Debug printf("CInputPin::EndOfStream() called\n");
    return E_NOTIMPL;
}


long STDCALL CInputPin_BeginFlush(IPin * This)
{
    Debug printf("CInputPin::BeginFlush() called\n");
    return E_NOTIMPL;
}


long STDCALL CInputPin_EndFlush(IPin * This)
{
    Debug printf("CInputPin::EndFlush() called\n");
    return E_NOTIMPL;
}

long STDCALL CInputPin_NewSegment(IPin * This,
				   /* [in] */ REFERENCE_TIME tStart,
				   /* [in] */ REFERENCE_TIME tStop,
				   /* [in] */ double dRate)
{
    Debug printf("CInputPin::NewSegment() called\n");
    return E_NOTIMPL;
}

CInputPin * CInputPin_Create(CBaseFilter* p, const AM_MEDIA_TYPE *vh)
{
    CInputPin *this;
    this = malloc(sizeof(CInputPin));

    memcpy(&this->type,vh,sizeof(AM_MEDIA_TYPE));
    this->refcount = 1;
    this->parent = p;
    this->vt=malloc(sizeof(IPin_vt));
    this->vt->QueryInterface = CInputPin_QueryInterface;
    this->vt->AddRef = CInputPin_AddRef;
    this->vt->Release = CInputPin_Release;
    this->vt->Connect = CInputPin_Connect;
    this->vt->ReceiveConnection = CInputPin_ReceiveConnection;
    this->vt->Disconnect=CInputPin_Disconnect;
    this->vt->ConnectedTo = CInputPin_ConnectedTo;
    this->vt->ConnectionMediaType = CInputPin_ConnectionMediaType;
    this->vt->QueryPinInfo = CInputPin_QueryPinInfo;
    this->vt->QueryDirection = CInputPin_QueryDirection;
    this->vt->QueryId = CInputPin_QueryId;
    this->vt->QueryAccept = CInputPin_QueryAccept;
    this->vt->EnumMediaTypes = CInputPin_EnumMediaTypes;
    this->vt->QueryInternalConnections = CInputPin_QueryInternalConnections;
    this->vt->EndOfStream = CInputPin_EndOfStream;
    this->vt->BeginFlush = CInputPin_BeginFlush;
    this->vt->EndFlush = CInputPin_EndFlush;
    this->vt->NewSegment = CInputPin_NewSegment;

    this->interfaces[0]=IID_IUnknown;
    return this;
}

void CInputPin_Destroy(CInputPin * this)
{
  free(this->vt);
  free(this);
}

long STDCALL CBaseFilter_GetClassID(IBaseFilter * This,
				      /* [out] */ CLSID *pClassID)
{
    Debug printf("CBaseFilter::GetClassID() called\n");
    return E_NOTIMPL;
}

long STDCALL CBaseFilter_Stop(IBaseFilter * This)
{
    Debug printf("CBaseFilter::Stop() called\n");
    return E_NOTIMPL;
}

long STDCALL CBaseFilter_Pause(IBaseFilter * This)
{
    Debug printf("CBaseFilter::Pause() called\n");
    return E_NOTIMPL;
}

long STDCALL CBaseFilter_Run(IBaseFilter * This,
			      REFERENCE_TIME tStart)
{
    Debug printf("CBaseFilter::Run() called\n");
    return E_NOTIMPL;
}

long STDCALL CBaseFilter_GetState(IBaseFilter * This,
				   /* [in] */ unsigned long dwMilliSecsTimeout,
				   // /* [out] */ FILTER_STATE *State)
				   void* State)
{
    Debug printf("CBaseFilter::GetState() called\n");
    return E_NOTIMPL;
}

long STDCALL CBaseFilter_SetSyncSource(IBaseFilter * This,
					/* [in] */ IReferenceClock *pClock)
{
    Debug printf("CBaseFilter::SetSyncSource() called\n");
    return E_NOTIMPL;
}

long STDCALL CBaseFilter_GetSyncSource (
        IBaseFilter * This,
        /* [out] */ IReferenceClock **pClock)
{
    Debug printf("CBaseFilter::GetSyncSource() called\n");
    return E_NOTIMPL;
}


long STDCALL CBaseFilter_EnumPins (
        IBaseFilter * This,
        /* [out] */ IEnumPins **ppEnum)
{
    Debug printf("CBaseFilter::EnumPins() called\n");
    *ppEnum=(IEnumPins *)CEnumPins_Create(((CBaseFilter*)This)->pin, ((CBaseFilter*)This)->unused_pin);
    return 0;
}


long STDCALL CBaseFilter_FindPin (
        IBaseFilter * This,
        /* [string][in] */ const unsigned short* Id,
        /* [out] */ IPin **ppPin)
{
    Debug printf("CBaseFilter::FindPin() called\n");
    return E_NOTIMPL;
}


long STDCALL CBaseFilter_QueryFilterInfo (
        IBaseFilter * This,
//        /* [out] */ FILTER_INFO *pInfo)
	void* pInfo)
{
    Debug printf("CBaseFilter::QueryFilterInfo() called\n");
    return E_NOTIMPL;
}


long STDCALL CBaseFilter_JoinFilterGraph (
        IBaseFilter * This,
        /* [in] */ IFilterGraph *pGraph,
        /* [string][in] */ const unsigned short* pName)
{
    Debug printf("CBaseFilter::JoinFilterGraph() called\n");
    return E_NOTIMPL;
}


long STDCALL CBaseFilter_QueryVendorInfo (
        IBaseFilter * This,
        /* [string][out] */ unsigned short* *pVendorInfo)
{
    Debug printf("CBaseFilter::QueryVendorInfo() called\n");
    return E_NOTIMPL;
}

CBaseFilter * CBaseFilter_Create(const AM_MEDIA_TYPE *type, CBaseFilter2* parent)
{
    CBaseFilter *this;
    
    this = malloc(sizeof(CBaseFilter));
    this->refcount = 1;
    this->pin=(IPin *)CInputPin_Create(this, type);
    this->unused_pin=(IPin *)CRemotePin_Create(this, CBaseFilter2_GetPin(parent));
    this->vt=malloc(sizeof(IBaseFilter_vt));
    this->vt->QueryInterface = CBaseFilter_QueryInterface;
    this->vt->AddRef = CBaseFilter_AddRef;
    this->vt->Release = CBaseFilter_Release;
    this->vt->GetClassID = CBaseFilter_GetClassID;
    this->vt->Stop = CBaseFilter_Stop;
    this->vt->Pause = CBaseFilter_Pause;
    this->vt->Run = CBaseFilter_Run;
    this->vt->GetState = CBaseFilter_GetState;
    this->vt->SetSyncSource = CBaseFilter_SetSyncSource;
    this->vt->GetSyncSource = CBaseFilter_GetSyncSource;
    this->vt->EnumPins = CBaseFilter_EnumPins;
    this->vt->FindPin = CBaseFilter_FindPin;
    this->vt->QueryFilterInfo = CBaseFilter_QueryFilterInfo;
    this->vt->JoinFilterGraph = CBaseFilter_JoinFilterGraph;
    this->vt->QueryVendorInfo = CBaseFilter_QueryVendorInfo;
    this->interfaces[0]=IID_IUnknown;
    this->interfaces[1]=IID_IBaseFilter;
    return this;
}


void CBaseFilter_Destroy(CBaseFilter *this)
{
  free(this->vt);
  this->pin->vt->Release((IUnknown*)this->pin);
  this->unused_pin->vt->Release((IUnknown*)this->unused_pin);
  free(this);
}
    
IPin* CBaseFilter_GetPin(CBaseFilter *this) 
{return this->pin;}

IPin* CBaseFilter_GetUnusedPin(CBaseFilter *this)
{return this->unused_pin;}



long STDCALL CBaseFilter2_GetClassID (
        IBaseFilter * This,
        /* [out] */ CLSID *pClassID)
{
    Debug printf("CBaseFilter2::GetClassID() called\n");
    return E_NOTIMPL;
}

long STDCALL CBaseFilter2_Stop (
        IBaseFilter * This)
{
    Debug printf("CBaseFilter2::Stop() called\n");
    return E_NOTIMPL;
}


long STDCALL CBaseFilter2_Pause (IBaseFilter * This)
{
    Debug printf("CBaseFilter2::Pause() called\n");
    return E_NOTIMPL;
}

long STDCALL CBaseFilter2_Run (IBaseFilter * This, REFERENCE_TIME tStart)
{
    Debug printf("CBaseFilter2::Run() called\n");
    return E_NOTIMPL;
}


long STDCALL CBaseFilter2_GetState (
        IBaseFilter * This,
        /* [in] */ unsigned long dwMilliSecsTimeout,
//        /* [out] */ FILTER_STATE *State)
    	void* State)
{
    Debug printf("CBaseFilter2::GetState() called\n");
    return E_NOTIMPL;
}


long STDCALL CBaseFilter2_SetSyncSource (
        IBaseFilter * This,
        /* [in] */ IReferenceClock *pClock)
{
    Debug printf("CBaseFilter2::SetSyncSource() called\n");
    return E_NOTIMPL;
}


long STDCALL CBaseFilter2_GetSyncSource (
        IBaseFilter * This,
        /* [out] */ IReferenceClock **pClock)
{
    Debug printf("CBaseFilter2::GetSyncSource() called\n");
    return E_NOTIMPL;
}


long STDCALL CBaseFilter2_EnumPins (
        IBaseFilter * This,
        /* [out] */ IEnumPins **ppEnum)
{
    Debug printf("CBaseFilter2::EnumPins() called\n");
    *ppEnum=(IEnumPins *)CEnumPins_Create(((CBaseFilter2*)This)->pin,0);
    return 0;
}


long STDCALL CBaseFilter2_FindPin (
        IBaseFilter * This,
        /* [string][in] */ const unsigned short* Id,
        /* [out] */ IPin **ppPin)
{
    Debug printf("CBaseFilter2::FindPin() called\n");
    return E_NOTIMPL;
}


long STDCALL CBaseFilter2_QueryFilterInfo (
        IBaseFilter * This,
//        /* [out] */ FILTER_INFO *pInfo)
	void* pInfo)
{
    Debug printf("CBaseFilter2::QueryFilterInfo() called\n");
    return E_NOTIMPL;
}


long STDCALL CBaseFilter2_JoinFilterGraph(IBaseFilter * This,
					   /* [in] */ IFilterGraph *pGraph,
					   /* [string][in] */
					   const unsigned short* pName)
{
    Debug printf("CBaseFilter2::JoinFilterGraph() called\n");
    return E_NOTIMPL;
}

long STDCALL CBaseFilter2_QueryVendorInfo(IBaseFilter * This,
					   /* [string][out] */
					   unsigned short* *pVendorInfo)
{
    Debug printf("CBaseFilter2::QueryVendorInfo() called\n");
    return E_NOTIMPL;
}

GUID CBaseFilter2_interf1={0x76c61a30, 0xebe1, 0x11cf, {0x89, 0xf9, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xcb}};
GUID CBaseFilter2_interf2={0xaae7e4e2, 0x6388, 0x11d1, {0x8d, 0x93, 0x00, 0x60, 0x97, 0xc9, 0xa2, 0xb2}};
GUID CBaseFilter2_interf3={0x02ef04dd, 0x7580, 0x11d1, {0xbe, 0xce, 0x00, 0xc0, 0x4f, 0xb6, 0xe9, 0x37}};

CBaseFilter2 * CBaseFilter2_Create()
{
    CBaseFilter2 *this;
    
    this = malloc(sizeof(CBaseFilter2));
    this->refcount = 1;
    this->pin=(IPin *)CRemotePin2_Create(this);
    this->vt=malloc(sizeof(IBaseFilter_vt));
    memset(this->vt, 0, sizeof (IBaseFilter_vt));
    this->vt->QueryInterface = CBaseFilter2_QueryInterface;
    this->vt->AddRef = CBaseFilter2_AddRef;
    this->vt->Release = CBaseFilter2_Release;
    this->vt->GetClassID = CBaseFilter2_GetClassID;
    this->vt->Stop = CBaseFilter2_Stop;
    this->vt->Pause = CBaseFilter2_Pause;
    this->vt->Run = CBaseFilter2_Run;
    this->vt->GetState = CBaseFilter2_GetState;
    this->vt->SetSyncSource = CBaseFilter2_SetSyncSource;
    this->vt->GetSyncSource = CBaseFilter2_GetSyncSource;
    this->vt->EnumPins = CBaseFilter2_EnumPins;
    this->vt->FindPin = CBaseFilter2_FindPin;
    this->vt->QueryFilterInfo = CBaseFilter2_QueryFilterInfo;
    this->vt->JoinFilterGraph = CBaseFilter2_JoinFilterGraph;
    this->vt->QueryVendorInfo = CBaseFilter2_QueryVendorInfo;
    this->interfaces[0]=IID_IUnknown;
    this->interfaces[1]=IID_IBaseFilter;
    this->interfaces[2]=CBaseFilter2_interf1;
    this->interfaces[3]=CBaseFilter2_interf2;
    this->interfaces[4]=CBaseFilter2_interf3;
    
    return this;
}


    
void CBaseFilter2_Destroy(CBaseFilter2 *this)
{
free(this->vt);
this->pin->vt->Release((IUnknown*)this->pin);
free(this);
}

IPin* CBaseFilter2_GetPin(CBaseFilter2 *this)
{return this->pin;}

static long STDCALL CRemotePin_ConnectedTo(IPin * This, /* [out] */ IPin **pPin)
{
    Debug printf("CRemotePin::ConnectedTo called\n");
    if (!pPin)
	return E_INVALIDARG;
    *pPin=((CRemotePin*)This)->remote_pin;
    (*pPin)->vt->AddRef((IUnknown*)(*pPin));
    return 0;
}

static long STDCALL CRemotePin_QueryDirection(IPin * This,
					      /* [out] */ PIN_DIRECTION *pPinDir)
{
    Debug printf("CRemotePin::QueryDirection called\n");
    if (!pPinDir)
	return E_INVALIDARG;
    *pPinDir=PINDIR_INPUT;
    return 0;
}

static long STDCALL CRemotePin_ConnectionMediaType(IPin* This, /* [out] */ AM_MEDIA_TYPE* pmt)
{
    Debug printf("CRemotePin::ConnectionMediaType() called\n");
    return E_NOTIMPL;
}

static long STDCALL CRemotePin_QueryPinInfo(IPin* This, /* [out] */ PIN_INFO* pInfo)
{
    CBaseFilter* lparent = ((CRemotePin*)This)->parent;
    Debug printf("CRemotePin::QueryPinInfo() called\n");
    pInfo->dir=PINDIR_INPUT;
    pInfo->pFilter = (IBaseFilter *)lparent;
    lparent->vt->AddRef((IUnknown*)lparent);
    pInfo->achName[0]=0;
    return 0;
}


static long STDCALL CRemotePin2_QueryPinInfo(IPin * This,
				       /* [out] */ PIN_INFO *pInfo)
{
    CBaseFilter2* lparent=((CRemotePin2*)This)->parent;
    Debug printf("CRemotePin2::QueryPinInfo called\n");
    pInfo->pFilter=(IBaseFilter*)lparent;
    lparent->vt->AddRef((IUnknown*)lparent);
    pInfo->dir=PINDIR_OUTPUT;
    pInfo->achName[0]=0;
    return 0;
}

CRemotePin * CRemotePin_Create(CBaseFilter* pt, IPin* rpin)
{
    CRemotePin *this;
    
    this = malloc(sizeof(CRemotePin));
    this->parent = pt;
    this->remote_pin = rpin;
    this->refcount = 1;
    this->vt = malloc(sizeof(IPin_vt));
    memset(this->vt, 0, sizeof(IPin_vt));
    this->vt->QueryInterface = CRemotePin_QueryInterface;
    this->vt->AddRef = CRemotePin_AddRef;
    this->vt->Release = CRemotePin_Release;
    this->vt->QueryDirection = CRemotePin_QueryDirection;
    this->vt->ConnectedTo = CRemotePin_ConnectedTo;
    this->vt->ConnectionMediaType = CRemotePin_ConnectionMediaType;
    this->vt->QueryPinInfo = CRemotePin_QueryPinInfo;
    this->interfaces[0]=IID_IUnknown;
    return this;
}

void CRemotePin_Destroy(CRemotePin * this)
{
  free(this->vt);
  free(this);
}

CRemotePin2 * CRemotePin2_Create(CBaseFilter2* p)
{
    CRemotePin2 *this;
    this = malloc(sizeof(CRemotePin2));
    this->parent = p,
    this->refcount = 1;
    this->vt = malloc(sizeof(IPin_vt));
    memset(this->vt, 0, sizeof(IPin_vt));
    this->vt->QueryInterface = CRemotePin2_QueryInterface;
    this->vt->AddRef = CRemotePin2_AddRef;
    this->vt->Release = CRemotePin2_Release;
    this->vt->QueryPinInfo = CRemotePin2_QueryPinInfo;
    this->interfaces[0]=IID_IUnknown;
    
    return this;
}

void CRemotePin2_Destroy(CRemotePin2 * this)
{
  free(this->vt);
  free(this);
}
