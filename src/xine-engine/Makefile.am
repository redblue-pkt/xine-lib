include $(top_srcdir)/misc/Makefile.common

SUBDIRS = nvtv

AM_CFLAGS = $(THREAD_CFLAGS) $(X_CFLAGS) $(FT2_CFLAGS) @ANSI_FLAGS@

lib_LTLIBRARIES = libxine.la

XINEUTILS_LIB  = $(top_builddir)/src/xine-utils/libxineutils.la
NVTVCLIENT_LIB = $(top_builddir)/src/xine-engine/nvtv/libnvclient.la

libxine_la_SOURCES = xine.c metronom.c configfile.c buffer.c \
	load_plugins.c video_decoder.c buffer_types.c \
	audio_decoder.c video_out.c audio_out.c resample.c events.c lrb.c \
	video_overlay.c osd.c scratch.c locale.c demux.c vo_scale.c \
	xine_interface.c post.c tvmode.c broadcaster.c

if HAVE_NVTV
libxine_la_DEPENDENCIES = @INTLLIBS@ $(XINEUTILS_LIB) $(NVTVCLIENT_LIB)
libxine_la_LIBADD = $(THREAD_LIBS) $(DYNAMIC_LD_LIBS) @INTLLIBS@ $(ZLIB_LIBS) \
		-lm $(XINEUTILS_LIB) $(NVTVCLIENT_LIB) $(GICONV_BSD_LIBS) $(FT2_LIBS)
else
libxine_la_DEPENDENCIES = @INTLLIBS@ $(XINEUTILS_LIB)
libxine_la_LIBADD = $(THREAD_LIBS) $(DYNAMIC_LD_LIBS) @INTLLIBS@ $(ZLIB_LIBS) \
		-lm $(XINEUTILS_LIB) $(GICONV_BSD_LIBS) $(FT2_LIBS)
endif

libxine_la_LDFLAGS =  \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)


include_HEADERS  = buffer.h metronom.h configfile.h vo_scale.h \
        audio_out.h resample.h video_out.h xine_internal.h spu_decoder.h \
	lrb.h video_overlay.h osd.h scratch.h xine_plugin.h xineintl.h  \
	plugin_catalog.h audio_decoder.h video_decoder.h post.h broadcaster.h

noinst_HEADERS = bswap.h

@INCLUDED_INTL_TRUE@@INTLLIBS@:
@INCLUDED_INTL_TRUE@	@cd $(top_builddir)/intl && $(MAKE) libintl.la

$(XINEUTILS_LIB):
	cd $(top_srcdir)/src/xine-utils && $(MAKE) libxineutils.la

$(NVTVCLIENT_LIB):
	cd $(top_builddir)/src/xine-engine/nvtv && $(MAKE) libnvclient.la
