CFLAGS = @GLOBAL_CFLAGS@ @X_CFLAGS@ $(LINUX_INCLUDE)

EXTRA_DIST = dxr3_decoder.c dxr3_vo_core.c dxr3_vo_encoder.c dxr3_vo_standard.c 

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

# replace -lfame by -lrte below and define USE_MP1E 1 in dxr3_vo_decoder.h to 
# use librte! Works with API of librte-0.4. 

if HAVE_DXR3
if HAVE_FAME
dxr3enc_module = xineplug_vo_out_dxr3enc.la 
endif
dxr3_modules = xineplug_decode_dxr3.la xineplug_vo_out_dxr3.la 
endif

lib_LTLIBRARIES = $(dxr3_modules) $(dxr3enc_module)

xineplug_decode_dxr3_la_SOURCES = dxr3_decoder.c
xineplug_decode_dxr3_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_dxr3_la_SOURCES = dxr3_vo_standard.c dxr3_vo_core.c 
xineplug_vo_out_dxr3_la_LDFLAGS = -avoid-version -module $(X_LIBS)

xineplug_vo_out_dxr3enc_la_SOURCES = dxr3_vo_encoder.c dxr3_vo_core.c 
xineplug_vo_out_dxr3enc_la_LDFLAGS = \
		-avoid-version -module -lfame $(X_LIBS)

include_HEADERS = dxr3_video_out.h dxr3_vo_encoder.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(LINUX_INCLUDE)"

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
