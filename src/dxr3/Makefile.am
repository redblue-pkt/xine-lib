CFLAGS = @CFLAGS@ $(X_CFLAGS) $(LINUX_INCLUDE)

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if HAVE_DXR3
dxr3_modules = xineplug_decode_dxr3.la xineplug_vo_out_dxr3.la 
endif
if HAVE_LIBFAME
link_fame = -lfame
endif
if HAVE_LIBRTE
link_rte = -lrte
endif

lib_LTLIBRARIES = $(dxr3_modules) 

xineplug_decode_dxr3_la_SOURCES = dxr3_decoder.c nav_read.c
xineplug_decode_dxr3_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_dxr3_la_SOURCES = dxr3_video_out.c \
		dxr3_vo_core.c mpeg_encoders.c alphablend.c
xineplug_vo_out_dxr3_la_LIBADD = $(X_LIBS) -lXext
xineplug_vo_out_dxr3_la_LDFLAGS = -avoid-version \
		-module $(link_fame) $(link_rte) $(X_LIBS)

noinst_HEADERS = nav_types.h nav_read.h alphablend.h bswap.h dxr3_video_out.h

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
