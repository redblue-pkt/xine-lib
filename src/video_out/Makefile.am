CFLAGS = @GLOBAL_CFLAGS@ @X_CFLAGS@ -DXINE_COMPILE

# LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if HAVE_X11
xshm_module = xineplug_vo_out_xshm.la
syncfb_module = xineplug_vo_out_syncfb.la
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
lib_LTLIBRARIES = $(xv_module) $(syncfb_module) $(xshm_module) 

xineplug_vo_out_xv_la_SOURCES = video_out_xv.c
xineplug_vo_out_xv_la_LIBADD = $(X_LIBS)
xineplug_vo_out_xv_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_xshm_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c video_out_xshm.c
xineplug_vo_out_xshm_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_syncfb_la_SOURCES =  video_out_syncfb.c
xineplug_vo_out_syncfb_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = yuv2rgb.h video_out_syncfb.h

include_HEADERS = video_out_x11.h

debug:
	$(MAKE) CFLAGS="$(DEBUG_CFLAGS) -DXINE_COMPILE"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

###
# Install header files (default=$includedir/xine)
#
install-includeHEADERS: $(include_HEADERS)
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)$(includedir)/xine
	@list='$(include_HEADERS)'; for p in $$list; do \
	  if test -f "$$p"; then d= ; else d="$(srcdir)/"; fi; \
	  echo " $(INSTALL_DATA) $$d$$p $(DESTDIR)$(includedir)/xine/$$p"; \
	  $(INSTALL_DATA) $$d$$p $(DESTDIR)$(includedir)/xine/$$p; \
	done


###
# Remove them
#
uninstall-includeHEADERS:
	@$(NORMAL_UNINSTALL)
	list='$(include_HEADERS)'; for p in $$list; do \
	  rm -f $(DESTDIR)$(includedir)/xine/$$p; \
	done

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
