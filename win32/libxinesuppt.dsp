# Microsoft Developer Studio Project File - Name="libxinesuppt" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=libxinesuppt - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libxinesuppt.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libxinesuppt.mak" CFG="libxinesuppt - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libxinesuppt - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libxinesuppt - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libxinesuppt - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release/libxinesuppt"
# PROP Intermediate_Dir "Release/libxinesuppt"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBXINESUPPT_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "contrib/dirent" /I "contrib/pthreads" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBXINESUPPT_EXPORTS" /D "__CLEANUP_C" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 winmm.lib /nologo /dll /machine:I386 /out:"Release/bin/libxinesuppt.dll"
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Desc=Moving Contrib Includes
PostBuild_Cmds=scripts\move_contrib_deps.bat
# End Special Build Tool

!ELSEIF  "$(CFG)" == "libxinesuppt - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug/libxinesuppt"
# PROP Intermediate_Dir "Debug/libxinesuppt"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBXINESUPPT_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "contrib/dirent" /I "contrib/pthreads" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBXINESUPPT_EXPORTS" /D "__CLEANUP_C" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 winmm.lib /nologo /dll /debug /machine:I386 /out:"Debug/bin/libxinesuppt.dll" /pdbtype:sept
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Desc=Moving Contrib Dependencies
PostBuild_Cmds=scripts\move_contrib_deps.bat
# End Special Build Tool

!ENDIF 

# Begin Target

# Name "libxinesuppt - Win32 Release"
# Name "libxinesuppt - Win32 Debug"
# Begin Group "Source Files ( dirent )"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\contrib\dirent\dirent.c
# End Source File
# End Group
# Begin Group "Source Files ( pthreads )"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\contrib\pthreads\attr.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\barrier.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\cancel.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\cleanup.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\condvar.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\create.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\dll.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\errno.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\exit.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\fork.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\global.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\misc.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\mutex.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\nonportable.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\private.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\rwlock.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\sched.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\sched.h
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\semaphore.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\signal.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\spin.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\sync.c
# End Source File
# Begin Source File

SOURCE=.\contrib\pthreads\tsd.c
# End Source File
# End Group
# Begin Group "Source Files ( zlib )"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\contrib\zlib\adler32.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\compress.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\crc32.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\deflate.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\gzio.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\infblock.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\infcodes.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\inffast.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\inflate.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\inftrees.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\infutil.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\trees.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\uncompr.c
# End Source File
# Begin Source File

SOURCE=.\contrib\zlib\zutil.c
# End Source File
# End Group
# Begin Group "Source Files ( timer )"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\contrib\timer\timer.c
# End Source File
# End Group
# Begin Group "DLL Defs"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\libxinesupport.def
# End Source File
# End Group
# End Target
# End Project
