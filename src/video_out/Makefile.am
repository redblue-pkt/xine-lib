CFLAGS = @GLOBAL_CFLAGS@ @X_CFLAGS@ @LIBMPEG2_CFLAGS@ -DXINE_COMPILE @SDL_CFLAGS@
DEBUG_CFLAGS = @DEBUG_CFLAGS@ @X_CFLAGS@ @LIBMPEG2_CFLAGS@ -DXINE_COMPILE @SDL_CFLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if HAVE_X11
xshm_module = xineplug_vo_out_xshm.la
syncfb_module = xineplug_vo_out_syncfb.la
if HAVE_XV
xv_module = xineplug_vo_out_xv.la
endif
if HAVE_OPENGL
opengl_module = xineplug_vo_out_opengl.la
endif
endif

if HAVE_AA
aa_module = xineplug_vo_out_aa.la
endif

if HAVE_FB
fb_module = xineplug_vo_out_fb.la
endif

if HAVE_DIRECTFB
directfb_module = xineplug_vo_out_directfb.la
endif

if HAVE_SDL
sdl_module = xineplug_vo_out_sdl.la
endif


##
# IMPORTANT:
# ---------
# All of xine video out plugins should be named like the 
# scheme "xineplug_vo_out_"
#
lib_LTLIBRARIES = $(xv_module) $(syncfb_module) $(xshm_module) $(aa_module) \
                  $(fb_module) $(sdl_module) $(opengl_module) $(directfb_module)

xineplug_vo_out_xv_la_SOURCES = deinterlace.c alphablend.c video_out_xv.c
xineplug_vo_out_xv_la_LIBADD =  $(XV_LIB) $(X_LIBS) -lXext
xineplug_vo_out_xv_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_xshm_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c yuv2rgb_mlib.c \
	alphablend.c video_out_xshm.c
xineplug_vo_out_xshm_la_LIBADD =  $(X_LIBS) -lXext
xineplug_vo_out_xshm_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_opengl_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c yuv2rgb_mlib.c \
	alphablend.c video_out_opengl.c
xineplug_vo_out_opengl_la_LIBADD =  $(OPENGL_LIBS) $(GLUT_LIBS) $(GLU_LIBS) $(X_LIBS)
xineplug_vo_out_opengl_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_syncfb_la_SOURCES = alphablend.c video_out_syncfb.c 
xineplug_vo_out_syncfb_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_aa_la_SOURCES =  video_out_aa.c
xineplug_vo_out_aa_la_LIBADD = $(AALIB_LIBS)
xineplug_vo_out_aa_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_fb_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c yuv2rgb_mlib.c \
	alphablend.c video_out_fb.c
xineplug_vo_out_fb_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_directfb_la_SOURCES = alphablend.c video_out_directfb.c
xineplug_vo_out_directfb_la_LIBADD = $(DIRECTFB_LIBS)
xineplug_vo_out_directfb_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_sdl_la_SOURCES = alphablend.c video_out_sdl.c
xineplug_vo_out_sdl_la_LIBADD = $(SDL_LIBS)
xineplug_vo_out_sdl_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = yuv2rgb.h video_out_syncfb.h alphablend.h deinterlace.h

include_HEADERS = video_out_x11.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) -DXINE_COMPILE"

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
	@list='$(include_HEADERS)'; for p in $$list; do \
	  rm -f $(DESTDIR)$(includedir)/xine/$$p; \
	done

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
