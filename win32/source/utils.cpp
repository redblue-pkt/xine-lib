
#include "xineui.h"

int Question(  HWND hwnd, LPSTR szFmt, ... )
{

    char szBuff[256];

    *szBuff = 0;
    wvsprintf(	&szBuff[ strlen( szBuff ) ],
				szFmt,
				(CHAR *)(&szFmt+1) );

    return MessageBox( hwnd, szBuff, "Question", MB_ICONQUESTION | MB_YESNO | MB_APPLMODAL );
}

void Error(  HWND hwnd, LPSTR szFmt, ... )
{

    char szBuff[256];

    *szBuff = 0;
    wvsprintf(	&szBuff[ strlen( szBuff ) ],
				szFmt,
				(CHAR *)(&szFmt+1) );

    MessageBox( hwnd, szBuff, "Error", MB_ICONERROR | MB_OK | MB_APPLMODAL | MB_SYSTEMMODAL );
}

BOOL CenterWindow( HWND hwnd )
{
	RECT window_rect;
	GetWindowRect( hwnd, &window_rect );

	int screen_x = GetSystemMetrics( SM_CXFULLSCREEN );
	int screen_y = GetSystemMetrics( SM_CYFULLSCREEN );

	int window_x = screen_x / 2 - ( window_rect.right - window_rect.left ) / 2;
	int window_y = screen_y / 2 - ( window_rect.bottom - window_rect.top ) / 2;

	return SetWindowPos( hwnd, HWND_TOP, window_x, window_y, 0, 0, SWP_NOSIZE );
}

BOOL AnchorWindow( HWND hwnd )
{
	HWND phwnd = GetParent( hwnd );
	
	RECT parent_rect;
	GetWindowRect( phwnd, &parent_rect );

	RECT window_rect;
	GetWindowRect( hwnd, &window_rect );

	int center_x = parent_rect.left + ( parent_rect.right - parent_rect.left ) / 2;
	int center_y = parent_rect.top + ( parent_rect.bottom - parent_rect.top ) / 2;

	int window_x = center_x - ( window_rect.right - window_rect.left ) / 2;
	int window_y = center_y - ( window_rect.bottom - window_rect.top ) / 2;

	return SetWindowPos( hwnd, HWND_TOP, window_x, window_y, 0, 0, SWP_NOSIZE );
}
