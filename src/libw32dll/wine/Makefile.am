EXTRA_DIST = stubs.s

CFLAGS = @BUILD_LIB_STATIC@ -fno-omit-frame-pointer @X_CFLAGS@ \
	 -DWIN32_PATH=\"@w32_path@\" -pipe


noinst_HEADERS = avifmt.h      elfdll.h    msacm.h     pshpack1.h   winbase.h \
                 winnt.h       basetsd.h   heap.h      msacmdrv.h   pshpack2.h\
                 windef.h      winreg.h    config.h    ldt.h        ntdef.h   \
                 pshpack4.h    windows.h   winuser.h   debugtools.h mmreg.h   \
                 pe_image.h    pshpack8.h  winerror.h  driver.h     module.h  \
                 poppack.h     vfw.h       winestring.h \
		 loader.h  registry.h  win32.h  wineacm.h 


if HAVE_W32DLL
wine_lib = libwine.la
endif

noinst_LTLIBRARIES = $(wine_lib)

libwine_la_SOURCES = afl.c       elfdll.c    module.c    pe_resource.c  \
		     resource.c  win32.c     driver.c    ext.c          \
		     pe_image.c  registry.c  vfl.c        

libwine_la_LIBADD = stubs.lo

stubs.lo: stubs.s
	$(CC) -c $(top_srcdir)/src/libw32dll/wine/stubs.s -o stubs.lo

debug:

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in

