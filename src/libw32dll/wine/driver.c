#include "config.h"

#include "driver.h"
#include "pe_image.h"
#include "winreg.h"
#include "vfw.h"
#include "registry.h"
#include "ldt_keeper.h"
#include "ext.h"
#include "win32.h"
#include "driver.h"
#include "debugtools.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

// #define DETAILED_OUT

char* win32_codec_name=NULL;  // must be set before calling DrvOpen() !!!
char* win32_def_path  =NULL;  // must be set before calling DrvOpen() !!!

#if 1

/*
 * STORE_ALL/REST_ALL seems like an attempt to workaround problems due to
 * WINAPI/no-WINAPI bustage.
 *
 * There should be no need for the STORE_ALL/REST_ALL hack once all
 * function definitions agree with their prototypes (WINAPI-wise) and
 * we make sure, that we do not call these functions without a proper
 * prototype in scope.
 */

#define STORE_ALL
#define REST_ALL
#else
// this asm code doesn't work - why ???
// kabi@i.am
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

static NPDRVR DrvAlloc(HDRVR* lpDriver, LPUINT lpDrvResult)
{
    /* allocate and lock handle */
    *lpDrvResult = MMSYSERR_INVALPARAM;
    if (lpDriver)
    {
	if ((*lpDriver = (HDRVR) malloc(sizeof(DRVR))) != 0 )
	{
	    memset((void*)*lpDriver, 0, sizeof(DRVR));
	    *lpDrvResult = MMSYSERR_NOERROR;
	    return (NPDRVR) *lpDriver;
	}
	*lpDrvResult = MMSYSERR_NOMEM;
    }
    return (NPDRVR) 0;
}

static int needs_free=0;
void SetCodecPath(const char* path)
{
    if(needs_free)free(win32_def_path);
    if(path==0)
    {
	win32_def_path=WIN32_PATH;
	needs_free=0;
	return;
    }
    win32_def_path=malloc(strlen(path)+1);
    strcpy(win32_def_path, path);
    needs_free=1;
}

static void DrvFree(HDRVR hDriver)
{
    int i;

    Setup_FS_Segment();

    if (hDriver)
    {
	if (((DRVR*)hDriver)->hDriverModule)
	{
	    if (((DRVR*)hDriver)->DriverProc)
	    {
		(((DRVR*)hDriver)->DriverProc)(((DRVR*)hDriver)->dwDriverID, hDriver, DRV_CLOSE, 0, 0);
		(((DRVR*)hDriver)->DriverProc)(0, hDriver, DRV_FREE, 0, 0);
	    }
	    FreeLibrary(((DRVR*)hDriver)->hDriverModule);
	}
	free((NPDRVR)hDriver);
    }
    return;
}

void DrvClose(HDRVR hdrvr)
{
    DrvFree(hdrvr);
    CodecRelease();
}

//DrvOpen(LPCSTR lpszDriverName, LPCSTR lpszSectionName, LPARAM lParam2)
HDRVR DrvOpen(LPARAM lParam2)
{
    UINT uDrvResult;
    HDRVR hDriver;
    NPDRVR npDriver;
    char unknown[0x124];
    const char* filename = win32_codec_name;

    npDriver = DrvAlloc(&hDriver, &uDrvResult);
    if (!npDriver)
	return (HDRVR) 0;

    if (uDrvResult)
    {
	DrvFree(hDriver);
	return (HDRVR) 0;
    }

    npDriver->hDriverModule = LoadLibraryA(filename);

    if (!npDriver->hDriverModule)
    {
	printf("Can't open library %s\n", filename);
	DrvFree(hDriver);
	return ((HDRVR) 0);
    }

    npDriver->DriverProc = (DRIVERPROC) GetProcAddress(npDriver->hDriverModule,
						       "DriverProc");
    if (!npDriver->DriverProc)
    {
	printf("Library %s is not a valid VfW/ACM codec\n", filename);
	DrvFree(hDriver);
	return ((HDRVR) 0);
    }

    TRACE("DriverProc == %X\n", npDriver->DriverProc);
    npDriver->dwDriverID = ++dwDrvID;

    printf("Loaded DLL driver %s\n", filename);

    Setup_FS_Segment();

    STORE_ALL;
    (npDriver->DriverProc)(0, hDriver, DRV_LOAD, 0, 0);
    REST_ALL;
    TRACE("DRV_LOAD Ok!\n");
    STORE_ALL;
    (npDriver->DriverProc)(0, hDriver, DRV_ENABLE, 0, 0);
    REST_ALL;
    TRACE("DRV_ENABLE Ok!\n");

    // open driver
    STORE_ALL;
    npDriver->dwDriverID=(npDriver->DriverProc)(npDriver->dwDriverID, hDriver,
						DRV_OPEN, (LPARAM) (LPSTR) unknown,
						lParam2);
    REST_ALL;

    TRACE("DRV_OPEN Ok!(%X)\n", npDriver->dwDriverID);

    CodecAlloc();
    return hDriver;
}
