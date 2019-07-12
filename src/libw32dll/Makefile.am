AUTOMAKE_OPTIONS = subdir-objects

include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS   = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS  = $(xineplug_ldflags)

noinst_HEADERS =
noinst_LTLIBRARIES =
xineplug_LTLIBRARIES =
EXTRA_DIST =

if ENABLE_W32DLL
noinst_LTLIBRARIES += libdmo_filter.la libds_filter.la libwine.la
xineplug_LTLIBRARIES += xineplug_decode_w32dll.la
endif

#
# dmo
#

noinst_HEADERS += dmo/DMO_AudioDecoder.h dmo/dmo_guids.h dmo/dmo_interfaces.h dmo/DMO_Filter.h dmo/dmo.h dmo/DMO_VideoDecoder.h
libdmo_filter_la_SOURCES = dmo/buffer.c dmo/DMO_AudioDecoder.c dmo/dmo.c dmo/dmo_guids.c dmo/DMO_VideoDecoder.c
libdmo_filter_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir) -I$(srcdir)/dmo -I$(srcdir)/wine -DWIN32_PATH=\"$(w32_path)\" -DNOAVIFILE_HEADERS
libdmo_filter_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing $(X_CFLAGS) -fno-omit-frame-pointer

#
# DirectShow
#

noinst_HEADERS += DirectShow/allocator.h DirectShow/cmediasample.h DirectShow/guids.h DirectShow/inputpin.h \
	DirectShow/interfaces.h DirectShow/iunk.h DirectShow/outputpin.h DirectShow/DS_AudioDecoder.h \
	DirectShow/DS_Filter.h DirectShow/DS_VideoDecoder.h
libds_filter_la_SOURCES = DirectShow/allocator.c DirectShow/cmediasample.c DirectShow/guids.c DirectShow/inputpin.c \
	DirectShow/outputpin.c DirectShow/DS_Filter.c DirectShow/DS_AudioDecoder.c DirectShow/DS_VideoDecoder.c
libds_filter_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir) -I$(srcdir)/DirectShow -I$(srcdir)/wine \
	-DWIN32_PATH=\"$(w32_path)\" -DNOAVIFILE_HEADERS
libds_filter_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing $(X_CFLAGS) -fno-omit-frame-pointer

#
# wine
#

noinst_HEADERS += wine/avifmt.h wine/basetsd.h wine/com.h wine/debugtools.h wine/driver.h wine/elfdll.h wine/ext.h \
	wine/heap.h wine/ldt.h wine/ldt_keeper.h wine/loader.h wine/mmreg.h wine/module.h wine/msacm.h \
	wine/msacmdrv.h wine/ntdef.h wine/pe_image.h wine/poppack.h wine/pshpack1.h wine/pshpack2.h \
	wine/pshpack4.h wine/pshpack8.h wine/registry.h wine/resource.h wine/vfw.h wine/win32.h wine/wineacm.h \
	wine/winbase.h wine/windef.h wine/windows.h wine/winerror.h wine/winestring.h wine/winnt.h \
	wine/winreg.h wine/winuser.h wine/wrapper.h
libwine_la_SOURCES = wine/afl.c wine/driver.c wine/elfdll.c wine/ext.c wine/ldt_keeper.c wine/module.c \
	wine/pe_image.c wine/pe_resource.c wine/resource.c wine/registry.c wine/vfl.c wine/win32.c \
	wine/stubs.s wine/wrapper.S
libwine_la_CPPFLAGS = $(AM_CPPFLAGS) $(XDG_BASEDIR_CPPFLAGS) -DWIN32_PATH=\"$(w32_path)\" -I$(srcdir) -I$(srcdir)/wine \
	-D__WINE__ -Ddbg_printf=__vprintf -DTRACE=__vprintf -I$(top_srcdir)/src/xine-utils
# disable -fomit-frame-pointer, -finline-functions, and -frename-registers
# because they cause bad behavior of wine
libwine_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing $(DEFAULT_OCFLAGS) $(X_CFLAGS) @W32_NO_OPTIMIZE@ \
	-fno-omit-frame-pointer -fno-inline-functions -fno-rename-registers
libwine_la_LIBADD = ${WINE_LIBS} -lm

#
# qtx
#

noinst_HEADERS += qtx/qtxsdk/components.h  qtx/qtxsdk/select.h

#
# plugins
#

EXTRA_DIST += common.c
noinst_HEADERS += group_w32.h libwin32.h w32codec.h

xineplug_decode_w32dll_la_SOURCES = group_w32.c w32codec.c qt_decoder.c
xineplug_decode_w32dll_la_DEPS = $(XDG_BASEDIR_DEPS)
xineplug_decode_w32dll_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL) -lm $(KSTAT_LIBS) $(XDG_BASEDIR_LIBS) \
	libwine.la libds_filter.la libdmo_filter.la
xineplug_decode_w32dll_la_CPPFLAGS = $(AM_CPPFLAGS) $(XDG_BASEDIR_CPPFLAGS) -I$(srcdir)/wine -DWIN32_PATH=\"$(w32_path)\"
xineplug_decode_w32dll_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing

