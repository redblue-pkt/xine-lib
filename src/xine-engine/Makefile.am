include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(THREAD_CFLAGS) $(X_CFLAGS) $(FT2_CFLAGS)

LIBTOOL = $(SHELL) $(top_builddir)/libtool
lib_LTLIBRARIES = libxine.la

XINEUTILS_LIB  = $(top_builddir)/src/xine-utils/libxineutils.la
INTERNAL_ZLIB_LIB = $(top_builddir)/win32/contrib/zlib/libzlib.la
DIRENT_LIB = $(top_builddir)/win32/contrib/libdirent.la
INTERNAL_PTHREAD_LIB = $(top_builddir)/win32/contrib/pthreads/libpthread.la
TIMER_LIB = $(top_builddir)/win32/contrib/libtimer.la
DEF_FILE = libxine-$(XINE_MAJOR).def
if !HAVE_ZLIB
zlib_dep = $(INTERNAL_ZLIB_LIB)
endif
if WIN32
dirent_lib = $(DIRENT_LIB)
pthread_dep = $(INTERNAL_PTHREAD_LIB)
timer_lib = $(TIMER_LIB)
def_ldflags="-Wl,--output-def,$(DEF_FILE)"
endif

libxine_la_SOURCES = xine.c metronom.c configfile.c buffer.c \
	load_plugins.c video_decoder.c buffer_types.c \
	audio_decoder.c video_out.c audio_out.c resample.c events.c \
	video_overlay.c osd.c scratch.c demux.c vo_scale.c \
	xine_interface.c post.c tvmode.c broadcaster.c io_helper.c \
	input_rip.c info_helper.c $(top_srcdir)/src/xine-utils/xmlparser.c

# FIXME: these are currently unused:
EXTRA_DIST = lrb.c lrb.h

libxine_la_DEPENDENCIES = @INTLLIBS@ $(XINEUTILS_LIB) $(zlib_dep) \
                          $(dirent_lib) $(pthread_dep) $(timer_lib)
libxine_la_LIBADD = $(THREAD_LIBS) $(DYNAMIC_LD_LIBS) @INTLLIBS@ $(ZLIB_LIBS) \
		    -lm $(XINEUTILS_LIB) $(LIBICONV) $(FT2_LIBS) \
                    $(dirent_lib) $(timer_lib)

libxine_la_LDFLAGS =  \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) $(def_ldflags)


include_HEADERS  = buffer.h metronom.h configfile.h vo_scale.h \
        audio_out.h resample.h video_out.h xine_internal.h spu_decoder.h \
	video_overlay.h osd.h scratch.h xine_plugin.h xineintl.h  \
	plugin_catalog.h audio_decoder.h video_decoder.h post.h \
	io_helper.h broadcaster.h info_helper.h

noinst_HEADERS = bswap.h

if WIN32
install-exec-local:
	cp -p $(DEF_FILE) $(DESTDIR)$(libdir)

uninstall-local:
	rm -f $(DEF_FILE) $(DESTDIR)$(libdir)
endif

@INCLUDED_INTL_TRUE@@INTLLIBS@:
@INCLUDED_INTL_TRUE@	@cd $(top_builddir)/intl && $(MAKE) libintl.la

$(XINEUTILS_LIB):
	$(MAKE) -C $(top_builddir)/src/xine-utils libxineutils.la

$(INTERNAL_PTHREAD_LIB):
	$(MAKE) -C $(top_builddir)/win32/contrib/pthreads libpthread.la

$(INTERNAL_ZLIB_LIB):
	$(MAKE) -C $(top_builddir)/win32/contrib/zlib libzlib.la

$(DIRENT_LIB):
	$(MAKE) -C $(top_builddir)/win32/contrib libdirent.la

$(TIMER_LIB):
	$(MAKE) -C $(top_builddir)/win32/contrib libtimer.la
