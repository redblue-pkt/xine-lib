
#ifndef _UTILS_H_
#define _UTILS_H_

extern BOOL CenterWindow( HWND hwnd );
extern BOOL AnchorWindow( HWND hwnd );

extern void SetTextNormal( HWND hwnd, char * newstatus );
extern void SetTextError( HWND hwnd, char * newstatus );

extern int Question( HWND hwnd, LPSTR szFmt, ... );
extern void Error( HWND hwnd, LPSTR szFmt, ... );

#endif