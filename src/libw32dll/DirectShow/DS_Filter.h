#ifndef DS_FILTER_H
#define DS_FILTER_H

#include "inputpin.h"
#include "outputpin.h"

/**
   User will allocate and fill format structures, call Create(),
   and then set up m_pAll.
 **/
typedef struct _DS_Filter DS_Filter;

struct _DS_Filter
{
    int m_iHandle;
    IBaseFilter* m_pFilter;
    IPin* m_pInputPin;
    IPin* m_pOutputPin;

    CBaseFilter* m_pSrcFilter;
    CBaseFilter2* m_pParentFilter;
    IPin* m_pOurInput;
    COutputPin* m_pOurOutput;

    AM_MEDIA_TYPE *m_pOurType;
    AM_MEDIA_TYPE *m_pDestType;
    IMemAllocator* m_pAll;
    IMemInputPin* m_pImp;
    int m_iState;
};

void DS_Filter_Destroy(DS_Filter * this);

DS_Filter * DS_Filter_Create(const char* dllname, const GUID* id,
		       AM_MEDIA_TYPE* in_fmt,
		       AM_MEDIA_TYPE* out_fmt);

void DS_Filter_Start(DS_Filter *this);

void DS_Filter_Stop(DS_Filter *this);

#endif /* DS_FILTER_H */
