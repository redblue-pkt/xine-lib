CFLAGS = @CFLAGS@ $(X_CFLAGS)

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if HAVE_DXR3
dxr3_modules = xineplug_decode_dxr3_video.la xineplug_decode_dxr3_spu.la xineplug_vo_out_dxr3.la 
endif
if HAVE_X11
link_x_libs = $(X_LIBS) -lXext
endif
if HAVE_LIBFAME
link_fame = $(LIBFAME_LIBS)
endif
if HAVE_LIBRTE
link_rte = -lrte
endif

lib_LTLIBRARIES = $(dxr3_modules) 

xineplug_decode_dxr3_video_la_SOURCES = dxr3_decode_video.c dxr3_scr.c
xineplug_decode_dxr3_video_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_decode_dxr3_video_la_LDFLAGS = -avoid-version -module

xineplug_decode_dxr3_spu_la_SOURCES = dxr3_decode_spu.c nav_read.c
xineplug_decode_dxr3_spu_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_decode_dxr3_spu_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_dxr3_la_SOURCES = video_out_dxr3.c dxr3_mpeg_encoders.c alphablend.c dxr3_spu_encoder.c
xineplug_vo_out_dxr3_la_LIBADD = $(link_fame) $(link_rte) $(link_x_libs) \
	$(top_builddir)/src/xine-utils/libxineutils.la
xineplug_vo_out_dxr3_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = dxr3.h dxr3_scr.h video_out_dxr3.h nav_types.h nav_read.h alphablend.h bswap.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

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
