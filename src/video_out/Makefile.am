CFLAGS = @GLOBAL_CFLAGS@ @X_CFLAGS@ -DXINE_COMPILE

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if HAVE_X11
## xshm and syncfb should be fixed first ;-)
## xshm_module = xineplug_vo_out_xshm.la
## syncfb_module = xineplug_vo_out_syncfb.la
if HAVE_XV
xv_module = xineplug_vo_out_xv.la
endif
endif

##
# IMPORTANT:
# ---------
# All of xine video out plugins should be named like the 
# scheme "xineplug_vo_out_"
#
lib_LTLIBRARIES = $(xv_module) ## $(xshm_module) $(syncfb_module)

xineplug_vo_out_xv_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c video_out_xv.c
xineplug_vo_out_xv_la_LDFLAGS = -avoid-version -module

##xineplug_vo_out_xshm_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c video_out_xshm.c
##xineplug_vo_out_xshm_la_LDFLAGS = -avoid-version -module

##xineplug_vo_out_syncfb_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c video_out_syncfb.c
##xineplug_vo_out_syncfb_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = yuv2rgb.h video_out_syncfb.h


debug:
	$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
