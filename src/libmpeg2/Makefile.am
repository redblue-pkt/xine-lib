include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(LIBMPEG2_CFLAGS)

EXTRA_DIST = idct_mlib.c idct_mlib.h motion_comp_mlib.c

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_mpeg2.la

xineplug_decode_mpeg2_la_SOURCES = \
	cpu_state.c \
	decode.c \
	header.c \
	idct.c \
	idct_altivec.c \
	idct_mlib.c \
	idct_mmx.c \
	motion_comp.c \
	motion_comp_altivec.c \
	motion_comp_mmx.c \
	motion_comp_mlib.c \
	slice.c \
	stats.c \
	xine_decoder.c

xineplug_decode_mpeg2_la_LIBADD = $(XINE_LIB)
xineplug_decode_mpeg2_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = vlc.h mpeg2.h mpeg2_internal.h 
