CFLAGS = @GLOBAL_CFLAGS@ @X_CFLAGS@ $(LINUX_INCLUDE)

EXTRA_DIST = dxr3_decoder.c video_out_dxr3.c video_out_dxr3enc.c overlay.c

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if HAVE_DXR3
if HAVE_FAME
dxr3enc_module = xineplug_vo_out_dxr3enc.la 
endif
dxr3_modules = xineplug_decode_dxr3.la xineplug_vo_out_dxr3.la 
endif

lib_LTLIBRARIES = $(dxr3_modules) $(dxr3enc_module)

xineplug_decode_dxr3_la_SOURCES = dxr3_decoder.c
xineplug_decode_dxr3_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_dxr3_la_SOURCES = video_out_dxr3.c overlay.c
xineplug_vo_out_dxr3_la_LDFLAGS = -avoid-version -module $(X_LIBS)

xineplug_vo_out_dxr3enc_la_SOURCES = video_out_dxr3enc.c overlay.c
xineplug_vo_out_dxr3enc_la_LDFLAGS = \
		-avoid-version -module -lfame $(X_LIBS)

include_HEADERS = dxr3_overlay.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(LINUX_INCLUDE)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
