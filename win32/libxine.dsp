# Microsoft Developer Studio Project File - Name="libxine" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=libxine - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libxine.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libxine.mak" CFG="libxine - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libxine - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libxine - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libxine - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release/libxine"
# PROP Intermediate_Dir "Release/libxine"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
LIB32=link.exe
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBXINE_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "include" /I "../src" /I "../src/xine-engine" /I "../src/xine-utils" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBXINE_EXPORTS" /D "XINE_COMPILE" /D "HAVE_CONFIG_H" /D "__WINE_WINDEF_H" /D "__WINE_WINGDI_H" /D "__WINE_VFW_H" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 /nologo /dll /machine:I386 /out:"Release/bin/libxine.dll"
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Desc=Moving Xine Includes
PostBuild_Cmds=scripts\move_xine_deps.bat Release
# End Special Build Tool

!ELSEIF  "$(CFG)" == "libxine - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug/libxine"
# PROP Intermediate_Dir "Debug/libxine"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
LIB32=link.exe
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBXINE_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "include" /I "../include" /I ".." /I "../intl" /I "../src" /I "../src/xine-engine" /I "../src/xine-utils" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBXINE_EXPORTS" /D "XINE_COMPILE" /D "HAVE_CONFIG_H" /D "__WINE_WINDEF_H" /D "__WINE_WINGDI_H" /D "__WINE_VFW_H" /FR /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib uuid.lib wsock32.lib comctl32.lib /nologo /dll /debug /machine:I386 /out:"Debug/bin/libxine.dll" /pdbtype:sept
# SUBTRACT LINK32 /pdb:none

!ENDIF 

# Begin Target

# Name "libxine - Win32 Release"
# Name "libxine - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE="..\src\xine-engine\audio_decoder.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\audio_out.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\buffer.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\buffer_types.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\configfile.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\demux.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\events.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\load_plugins.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\locale.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\lrb.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\metronom.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\osd.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\post.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\resample.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\scratch.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\tvmode.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\video_decoder.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\video_out.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\video_overlay.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\vo_scale.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\xine.c"
# End Source File
# Begin Source File

SOURCE="..\src\xine-engine\xine_interface.c"
# End Source File
# End Group
# Begin Group "DLL Defs"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\libxine.def
# End Source File
# End Group
# End Target
# End Project
