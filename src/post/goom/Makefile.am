##
## Process this file with automake to produce Makefile.in
##

DEBUG_CFLAGS = @DEBUG_CFLAGS@ @ANSI_FLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

# FIXME
# -fomit-frame-pointer segfaults here
#CFLAGS = -DHAVE_CONFIG_H -I. -I. -I../../.. -I../../.. -I../../../include -I../../../include -I../../../src -I../../../src/xine-engine -I../../../src/xine-engine -I../../../src/xine-utils -Wall -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DXINE_COMPILE -O3 -pipe -falign-functions=4 -falign-loops=4 -falign-jumps=4 -mpreferred-stack-boundary=2 -fexpensive-optimizations -fschedule-insns2 -fno-strict-aliasing -ffast-math -funroll-loops -finline-functions -mcpu=pentiumpro

libdir = $(XINE_PLUGINDIR)/post

lib_LTLIBRARIES = xineplug_post_goom.la

if !PPC_ARCH
  xineplug_post_goom_la_SOURCES = \
        xine_goom.c \
        goom_core.c goom_core.h \
        goom_tools.h goom_config.h \
        filters.c filters.h\
        lines.c lines.h \
        ifs_display.c ifs.c ifs.h \
        graphic.c graphic.h \
        drawmethods.h \
        zoom_filter_mmx.c

else
  xineplug_post_goom_la_SOURCES = \
        xine_goom.c \
        goom_core.c goom_core.h \
        goom_tools.h goom_config.h \
        filters.c filters.h \
        lines.c lines.h \
        ifs_display.c ifs.c ifs.h \
        graphic.c graphic.h \
        drawmethods.h
#        ppc_zoom_ultimate.s
endif

xineplug_post_goom_la_LIBADD = $(XINE_LIB)
xineplug_post_goom_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

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

