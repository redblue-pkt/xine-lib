#include "config.h"
#include <stdio.h>

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#else
#include <stdlib.h>
#endif

#include "driver.h"
#include "pe_image.h"
#include "winreg.h"
#include "vfw.h"
#include "win32.h"
#include "registry.h"

#ifdef __FreeBSD__
#include <sys/time.h>
#endif

#if 1
#define STORE_ALL /**/
#define REST_ALL  /**/
#else
#define STORE_ALL \
    __asm__ ( \
    "push %%ebx\n\t" \
    "push %%ecx\n\t" \
    "push %%edx\n\t" \
    "push %%esi\n\t" \
    "push %%edi\n\t"::)

#define REST_ALL \
    __asm__ ( \
    "pop %%edi\n\t" \
    "pop %%esi\n\t" \
    "pop %%edx\n\t" \
    "pop %%ecx\n\t" \
    "pop %%ebx\n\t"::)
#endif



static DWORD dwDrvID = 0;


LRESULT WINAPI SendDriverMessage( HDRVR hDriver, UINT message,
                                    LPARAM lParam1, LPARAM lParam2 )
{
    DRVR* module=(DRVR*)hDriver;
    int result;
#ifdef DETAILED_OUT    
    printf("SendDriverMessage: driver %X, message %X, arg1 %X, arg2 %X\n", hDriver, message, lParam1, lParam2);
#endif
    if(module==0)return -1;
    if(module->hDriverModule==0)return -1;
    if(module->DriverProc==0)return -1;
    STORE_ALL;
    result=module->DriverProc(module->dwDriverID,1,message,lParam1,lParam2);
    REST_ALL;
#ifdef DETAILED_OUT    
    printf("\t\tResult: %X\n", result);
#endif    
    return result;
}				    

static NPDRVR DrvAlloc(HDRVR*lpDriver, LPUINT lpDrvResult)
{
    NPDRVR npDriver;
    /* allocate and lock handle */
    if (lpDriver)
    {
      if ( (*lpDriver = (HDRVR) malloc(sizeof(DRVR))) )
        {
            if ((npDriver = (NPDRVR) *lpDriver))
            {
                    *lpDrvResult = MMSYSERR_NOERROR;
                    return (npDriver);
            }
            free((NPDRVR)*lpDriver);
        }
        return (*lpDrvResult = MMSYSERR_NOMEM, (NPDRVR) 0);
    }
    return (*lpDrvResult = MMSYSERR_INVALPARAM, (NPDRVR) 0);
}

                                                                                                                    
static void DrvFree(HDRVR hDriver)
{
    int i;
    if(hDriver)
    	if(((DRVR*)hDriver)->hDriverModule)
    	if(((DRVR*)hDriver)->DriverProc)
	(((DRVR*)hDriver)->DriverProc)(((DRVR*)hDriver)->dwDriverID, hDriver, DRV_CLOSE, 0, 0);
    if(hDriver)	{
            if(((DRVR*)hDriver)->hDriverModule)
    		if(((DRVR*)hDriver)->DriverProc)
			(((DRVR*)hDriver)->DriverProc)(0, hDriver, DRV_FREE, 0, 0);
		FreeLibrary(((DRVR*)hDriver)->hDriverModule);
        	free((NPDRVR)hDriver);
		return;	
    }
}

void DrvClose(HDRVR hdrvr)
{
    DrvFree(hdrvr);
}


#ifdef WIN32_PATH
char* def_path=WIN32_PATH;	    // path to codecs
#else
char* def_path="/usr/lib/win32";    // path to codecs
#endif
char* win32_codec_name=NULL;  // must be set before calling DrvOpen() !!!

HDRVR
DrvOpen(LPARAM lParam2)
{
    ICOPEN *icopen=(ICOPEN*)lParam2;
    UINT uDrvResult;
    HDRVR hDriver;
    NPDRVR npDriver;
    char unknown[0x24];
//    char* codec_name=icopen->fccHandler;

    if (!(npDriver = DrvAlloc(&hDriver, &uDrvResult)))
	return ((HDRVR) 0);

    if (!(npDriver->hDriverModule = expLoadLibraryA(win32_codec_name))) {
     	printf("Can't open library %s\n", win32_codec_name);
        DrvFree(hDriver);
        return ((HDRVR) 0);
    }
   
    if (!(npDriver->DriverProc = (DRIVERPROC)
             GetProcAddress(npDriver->hDriverModule, "DriverProc"))) {
         printf("Library %s is not a valid codec\n", win32_codec_name);
         FreeLibrary(npDriver->hDriverModule);
         DrvFree(hDriver);
         return ((HDRVR) 0);
    }

    //TRACE("DriverProc == %X\n", npDriver->DriverProc);
     npDriver->dwDriverID = ++dwDrvID;

	STORE_ALL;
        (npDriver->DriverProc)(0, hDriver, DRV_LOAD, 0, 0);
	REST_ALL;
	//TRACE("DRV_LOAD Ok!\n");
	STORE_ALL;
	(npDriver->DriverProc)(0, hDriver, DRV_ENABLE, 0, 0);
	REST_ALL;
	//TRACE("DRV_ENABLE Ok!\n");

     // open driver 
    STORE_ALL;
     npDriver->dwDriverID=(npDriver->DriverProc)(npDriver->dwDriverID, hDriver, DRV_OPEN,
         (LPARAM) (LPSTR) unknown, lParam2);
    REST_ALL;

    //TRACE("DRV_OPEN Ok!(%X)\n", npDriver->dwDriverID);

    if (uDrvResult)
    {
         DrvFree(hDriver);
         hDriver = (HDRVR) 0;
     }
     
     printf("Successfully loaded codec %s\n",win32_codec_name);
     
     return (hDriver);
}
  
