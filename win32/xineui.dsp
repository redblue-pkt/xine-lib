# Microsoft Developer Studio Project File - Name="xineui" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=xineui - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "xineui.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "xineui.mak" CFG="xineui - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "xineui - Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE "xineui - Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "xineui - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release/xineui"
# PROP Intermediate_Dir "Release/xineui"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "source" /I "include" /I "../src/video_out" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /FR /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib uuid.lib comctl32.lib ddraw.lib /nologo /subsystem:windows /machine:I386 /out:"Release/bin/xineui.exe"

!ELSEIF  "$(CFG)" == "xineui - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug/xineui"
# PROP Intermediate_Dir "Debug/xineui"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "source" /I "include" /I "contrib/pthreads" /I "contrib/timer" /I "../include" /I ".." /I "../src/video_out" /I "../src/xine-utils" /I "../src/xine-engine" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "XINE_COMPILE" /FR /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib uuid.lib comctl32.lib /nologo /subsystem:windows /debug /machine:I386 /out:"Debug/bin/xineui.exe" /pdbtype:sept

!ENDIF 

# Begin Target

# Name "xineui - Win32 Release"
# Name "xineui - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\source\main.cpp
# End Source File
# Begin Source File

SOURCE=.\source\wnd.ctrl.cpp
# End Source File
# Begin Source File

SOURCE=.\source\wnd.panel.cpp
# End Source File
# Begin Source File

SOURCE=.\source\wnd.playlist.cpp
# End Source File
# Begin Source File

SOURCE=.\source\wnd.video.cpp
# End Source File
# Begin Source File

SOURCE=.\source\xineui.cpp
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=.\source\bitmap1.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp00001.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp00002.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp00003.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp00004.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp00005.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp00006.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp00007.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp00008.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp00009.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp00010.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp00011.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp_arro.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp_conf.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp_ffor.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp_full.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp_next.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp_play.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp_prev.bmp
# End Source File
# Begin Source File

SOURCE=.\source\bmp_volu.bmp
# End Source File
# Begin Source File

SOURCE=.\source\icon1.ico
# End Source File
# Begin Source File

SOURCE=.\source\maskbmp.bmp
# End Source File
# Begin Source File

SOURCE=.\source\resource.rc
# End Source File
# Begin Source File

SOURCE=.\source\xine_logo.bmp
# End Source File
# End Group
# End Target
# End Project
