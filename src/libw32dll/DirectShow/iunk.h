#ifndef DS_IUNK_H
#define DS_IUNK_H

#include "guids.h"

#define DECLARE_IUNKNOWN(CLASSNAME) \
    long STDCALL (*QueryInterface)(IUnknown * This, GUID* riid, void **ppvObject); \
    long STDCALL (*AddRef) (IUnknown * This); \
    long STDCALL (*Release) (IUnknown * This); \
    int refcount; 

#define INHERIT_IUNKNOWN() \
    long STDCALL (*QueryInterface)(IUnknown * This, GUID* riid, void **ppvObject); \
    long STDCALL (*AddRef) (IUnknown * This); \
    long STDCALL (*Release) (IUnknown * This); 
        
#define IMPLEMENT_IUNKNOWN(CLASSNAME) 		\
long STDCALL CLASSNAME ## _QueryInterface(IUnknown * This, GUID* riid, void **ppvObject); \
long STDCALL CLASSNAME ## _AddRef ( 		\
    IUnknown * This); 				\
long STDCALL CLASSNAME ## _Release ( 		\
    IUnknown * This); 				\
long STDCALL CLASSNAME ## _QueryInterface(IUnknown * This, GUID* riid, void **ppvObject) \
{ \
    CLASSNAME * me = (CLASSNAME *)This;		\
    GUID* r; unsigned int i = 0;				\
    Debug printf(#CLASSNAME "_QueryInterface() called\n");\
    if (!ppvObject) return 0x80004003; 		\
    for(r=me->interfaces; i<sizeof(me->interfaces)/sizeof(me->interfaces[0]); r++, i++) \
	if(!memcmp(r, riid, 16)) 		\
	{ 					\
	    me->vt->AddRef((IUnknown*)This); 	\
	    *ppvObject=This; 			\
	    return 0; 				\
	} 					\
    Debug printf("Failed\n");			\
    return E_NOINTERFACE;			\
} 						\
						\
long STDCALL CLASSNAME ## _AddRef ( 		\
    IUnknown * This) 				\
{						\
    CLASSNAME * me=( CLASSNAME *)This;		\
    Debug printf(#CLASSNAME "_AddRef() called\n"); \
    return ++(me->refcount); 			\
}     						\
						\
long STDCALL CLASSNAME ## _Release ( 		\
    IUnknown * This) 				\
{ 						\
    CLASSNAME* me=( CLASSNAME *)This;	 	\
    Debug printf(#CLASSNAME "_Release() called\n"); \
    if(--(me->refcount) ==0)			\
		CLASSNAME ## _Destroy(me); 	\
    return 0; 					\
}


#endif /* DS_IUNK_H */
