# CFLAGS = @BUILD_LIB_STATIC@ -I wine -D__WINE__ -Ddbg_printf=__vprintf -DTRACE=__vprintf -fno-omit-frame-pointer
# CFLAGS = -I wine -D__WINE__ -Ddbg_printf=__vprintf -DTRACE=__vprintf -fno-omit-frame-pointer @X_CFLAGS@

CFLAGS = @BUILD_LIB_STATIC@ -I wine -fno-omit-frame-pointer \
	-DWIN32_PATH=\"@w32_path@\" -pipe

SUBDIRS = wine

if HAVE_W32DLL
w32dll_lib = libw32dll.la
endif

noinst_LTLIBRARIES = $(w32dll_lib)

libw32dll_la_SOURCES = afl.c       elfdll.c    module.c    pe_resource.c  \
                       resource.c  win32.c     driver.c    ext.c          \
                       pe_image.c  registry.c  vfl.c       ##w32codec.c     

libw32dll_la_LIBADD = stubs.lo

noinst_HEADERS = loader.h  registry.h  win32.h  wineacm.h w32codec.h

EXTRA_DIST = stubs.s

stubs.lo: stubs.s
	$(CC) -c $(top_srcdir)/src/libw32dll/stubs.s -o stubs.lo


debug:
	$(MAKE) CFLAGS="$(CFLAGS) -g -DSTATIC_WIN32_PATH"

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
