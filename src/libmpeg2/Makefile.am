CFLAGS = @CFLAGS@ $(LIBMPEG2_CFLAGS)
DEBUG_CFLAGS = @DEBUG_CFLAGS@ $(LIBMPEG2_CFLAGS)

EXTRA_DIST = idct_mlib.c idct_mlib.h motion_comp_mlib.c

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_mpeg2.la

#libmpeg2_la_SOURCES = slice.c header.c stats.c idct.c motion_comp.c\
#	decode.c  idct_mmx.c motion_comp_mmx.c

xineplug_decode_mpeg2_la_SOURCES = slice.c header.c stats.c idct.c \
	motion_comp.c decode.c idct_mmx.c motion_comp_mmx.c \
	idct_mlib.c motion_comp_mlib.c idct_altivec.c \
	motion_comp_altivec.c cpu_state.c xine_decoder.c
xineplug_decode_mpeg2_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_decode_mpeg2_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = vlc.h mpeg2.h mpeg2_internal.h 

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
