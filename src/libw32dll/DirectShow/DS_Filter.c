#include "DS_Filter.h"
#include "../wine/driver.h"

#ifndef NOAVIFILE_HEADERS
#include "except.h"
#else
#include "../libwin32.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define __MODULE__ "DirectShow generic filter"

typedef long STDCALL (*GETCLASS) (const GUID*, const GUID*, void**);

//extern "C" int STDCALL LoadLibraryA(const char*);
//extern "C" STDCALL void* GetProcAddress(int, const char*); // STDCALL has to be first NetBSD
//extern "C" int STDCALL FreeLibrary(int);

void DS_Filter_Destroy(DS_Filter * this)
{   
    DS_Filter_Stop(this);
    
    if (this->m_iState == 0)
	return;
    this->m_iState = 0;

    if (this->m_pOurInput)
	this->m_pOurInput->vt->Release((IUnknown*)this->m_pOurInput);
    if (this->m_pInputPin)
	this->m_pInputPin->vt->Disconnect(this->m_pInputPin);
    if (this->m_pOutputPin)
	this->m_pOutputPin->vt->Disconnect(this->m_pOutputPin);
    if (this->m_pFilter)
	this->m_pFilter->vt->Release((IUnknown*)this->m_pFilter);
    if (this->m_pOutputPin)
	this->m_pOutputPin->vt->Release((IUnknown*)this->m_pOutputPin);
    if (this->m_pInputPin)
	this->m_pInputPin->vt->Release((IUnknown*)this->m_pInputPin);
    if (this->m_pImp)
	this->m_pImp->vt->Release((IUnknown*)this->m_pImp);

    COutputPin_Destroy(this->m_pOurOutput);
    CBaseFilter2_Destroy(this->m_pParentFilter);
    CBaseFilter_Destroy(this->m_pSrcFilter);

    // FIXME - we are still leaving few things allocated!
    if (this->m_iHandle)
	FreeLibrary(this->m_iHandle);
    
    CodecRelease();
}

DS_Filter * DS_Filter_Create(const char* dllname, const GUID* id,
		       AM_MEDIA_TYPE* in_fmt,
		       AM_MEDIA_TYPE* out_fmt)
{
    DS_Filter *this;
    this = malloc(sizeof(DS_Filter));

    this->m_iHandle = 0;
    this->m_pFilter = 0;
    this->m_pInputPin = 0;
    this->m_pOutputPin = 0;
    this->m_pSrcFilter = 0;
    this->m_pParentFilter = 0;
    this->m_pOurInput = 0;
    this->m_pOurOutput = 0;
    this->m_pAll = 0;
    this->m_pImp = 0;
    this->m_iState = 0;
    CodecAlloc();
 
    /*try*/
    {
	GETCLASS func;
	HRESULT result;
	struct IClassFactory* factory = 0;
	struct IUnknown* object = 0;
	IEnumPins* enum_pins = 0;
	IPin* array[256];
	ULONG fetched;
	unsigned int i;
	
 	this->m_iHandle = LoadLibraryA(dllname);
	if (!this->m_iHandle) {
	    printf("Could not open DirectShow DLL: %.200s\n", dllname);
            return NULL;
	}
        
	func = (GETCLASS)GetProcAddress(this->m_iHandle, "DllGetClassObject");
	if (!func) {
	    printf("Illegal or corrupt DirectShow DLL: %.200s\n", dllname);
            return NULL;
	}
        
	result = func(id, &IID_IClassFactory, (void**)&factory);
	if (result || !factory) {
	    printf("No such class object\n");
            return NULL;
        }
        
	result = factory->vt->CreateInstance(factory, 0, &IID_IUnknown, (void**)&object);
	factory->vt->Release((IUnknown*)factory);
	if (result || !object) {
	    printf("Class factory failure\n");
            return NULL;
        }
        
	result = object->vt->QueryInterface(object, &IID_IBaseFilter, (void**)&this->m_pFilter);
	object->vt->Release((IUnknown*)object);
	if (result || !this->m_pFilter) {
	    printf("Object does not have IBaseFilter interface\n");
            return NULL;
        }
        
	// enumerate pins
	result = this->m_pFilter->vt->EnumPins(this->m_pFilter, &enum_pins);
	if (result || !enum_pins) {
	    printf("Could not enumerate pins\n");
            return NULL;
	}
        
	enum_pins->vt->Reset(enum_pins);
	result = enum_pins->vt->Next(enum_pins, (ULONG)256, (IPin**)array, &fetched);
	Debug printf("Pins enumeration returned %ld pins, error is %x\n", fetched, (int)result);

	for (i = 0; i < fetched; i++)
	{
	    int direction = -1;
	    array[i]->vt->QueryDirection(array[i], (PIN_DIRECTION*)&direction);
	    if (!this->m_pInputPin && direction == 0)
	    {
		this->m_pInputPin = array[i];
		this->m_pInputPin->vt->AddRef((IUnknown*)this->m_pInputPin);
	    }
	    if (!this->m_pOutputPin && direction == 1)
	    {
		this->m_pOutputPin = array[i];
		this->m_pOutputPin->vt->AddRef((IUnknown*)this->m_pOutputPin);
	    }
	    array[i]->vt->Release((IUnknown*)(array[i]));
	}
	if (!this->m_pInputPin) {
	    printf("Input pin not found\n");
            return NULL;
        }
        
	if (!this->m_pOutputPin) {
	    printf("Output pin not found\n");
            return NULL;
	}

	result = this->m_pInputPin->vt->QueryInterface((IUnknown*)this->m_pInputPin,
						 &IID_IMemInputPin,
						 (void**)&this->m_pImp);
        if (result) {
	    printf("Error getting IMemInputPin interface\n");
            return NULL;
        }
        
	this->m_pOurType = in_fmt;
	this->m_pDestType = out_fmt;
        result = this->m_pInputPin->vt->QueryAccept(this->m_pInputPin, this->m_pOurType);
	if (result) {
	    printf("Source format is not accepted\n");
            return NULL;
	}
        
	this->m_pParentFilter = CBaseFilter2_Create();
        this->m_pSrcFilter = CBaseFilter_Create(this->m_pOurType, this->m_pParentFilter);
	this->m_pOurInput = CBaseFilter_GetPin(this->m_pSrcFilter);
	this->m_pOurInput->vt->AddRef((IUnknown*)this->m_pOurInput);

	result = this->m_pInputPin->vt->ReceiveConnection(this->m_pInputPin,
						    this->m_pOurInput,
						    this->m_pOurType);
	if (result) {
	    printf("Error connecting to input pin\n");
            return NULL;
	}
	this->m_pOurOutput = COutputPin_Create(this->m_pDestType);

	//extern void trapbug();
	//trapbug();
	result = this->m_pOutputPin->vt->ReceiveConnection(this->m_pOutputPin,
						     (IPin*)this->m_pOurOutput,
						     this->m_pDestType);
	if (result)
	{
	    //printf("Tracking ACELP %d  0%x\n", result);
	    printf("Error connecting to output pin\n");
            return NULL;
	}

	printf("Using DirectShow codec: %s\n", dllname);
	this->m_iState = 1;
    }
    /*
    catch (printfError& e)
    {
	//e.PrintAll();
	destroy();
	throw;
    }
    */
    return this;
}

void DS_Filter_Start(DS_Filter *this)
{
    HRESULT hr;
    if (this->m_iState != 1)
	return;

    Debug printf("DS_Filter::Start() %p\n", this->m_pFilter);

    this->m_pFilter->vt->Pause(this->m_pFilter);
    
    hr=this->m_pFilter->vt->Run(this->m_pFilter, 0);
    if (hr != 0)
    {
	Debug printf("WARNING: m_Filter->Run() failed, error code %x\n", (int)hr);
    }
    hr = this->m_pImp->vt->GetAllocator(this->m_pImp, &this->m_pAll);
    if (hr)
    {
	Debug printf("WARNING: error getting IMemAllocator interface %x\n", (int)hr);
        this->m_pImp->vt->Release((IUnknown*)this->m_pImp);
        return;
    }
    this->m_pImp->vt->NotifyAllocator(this->m_pImp, this->m_pAll,	0);
    this->m_iState = 2;
}

void DS_Filter_Stop(DS_Filter *this)
{
    if (this->m_iState == 2)
    {
	this->m_iState = 1;
	Debug	printf("DS_Filter::Stop() %p\n", this->m_pFilter);
	if (this->m_pFilter)
	{
	    //printf("vt: %p\n", this->m_pFilter->vt);
	    //printf("vtstop %p\n", this->m_pFilter->vt->Stop);
	    this->m_pFilter->vt->Stop(this->m_pFilter); // causes weird crash ??? FIXME
	}
	else
	    printf("WARNING: DS_Filter::Stop() m_pFilter is NULL!\n");
	this->m_pAll->vt->Release((IUnknown*)this->m_pAll);
	this->m_pAll = 0;
    }
}
