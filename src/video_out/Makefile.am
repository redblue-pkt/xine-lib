SUBDIRS = libdha vidix

VIDIX_CFLAGS = -I$(top_builddir)/src/video_out/vidix -I$(top_srcdir)/src/video_out/vidix

CFLAGS = @CFLAGS@ $(X_CFLAGS) $(LIBMPEG2_CFLAGS) -DXINE_COMPILE $(SDL_CFLAGS) $(VIDIX_CFLAGS) $(DIRECTFB_CFLAGS)
DEBUG_CFLAGS = @DEBUG_CFLAGS@ $(X_CFLAGS) $(LIBMPEG2_CFLAGS) -DXINE_COMPILE $(SDL_CFLAGS) $(VIDIX_CFLAGS) $(DIRECTFB_CFLAGS)

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if HAVE_X11
xshm_module = xineplug_vo_out_xshm.la
syncfb_module = xineplug_vo_out_syncfb.la
if HAVE_VIDIX
vidix_module = xineplug_vo_out_vidix.la
endif
if HAVE_XV
xv_module = xineplug_vo_out_xv.la
endif
if HAVE_OPENGL
opengl_module = xineplug_vo_out_opengl.la
endif
if HAVE_SUNFB
pgx64_module = xineplug_vo_out_pgx64.la
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



# important:
# 
# all xine video out plugins should be named  
# "xineplug_vo_out_*"

lib_LTLIBRARIES = $(xshm_module) $(xv_module) $(directfb_module) $(aa_module) \
                  $(syncfb_module) $(fb_module) $(opengl_module) \
                  $(sdl_module) $(vidix_module) $(pgx64_module)

xineplug_vo_out_xv_la_SOURCES = deinterlace.c alphablend.c video_out_xv.c
xineplug_vo_out_xv_la_LIBADD =  $(XV_LIB) $(X_LIBS) -lXext \
	$(top_builddir)/src/xine-utils/libxineutils.la \
	$(top_builddir)/src/xine-engine/libxine.la
xineplug_vo_out_xv_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_xshm_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c yuv2rgb_mlib.c \
	alphablend.c video_out_xshm.c
xineplug_vo_out_xshm_la_LIBADD =  $(X_LIBS) -lXext \
	$(top_builddir)/src/xine-utils/libxineutils.la \
	$(top_builddir)/src/xine-engine/libxine.la
xineplug_vo_out_xshm_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_opengl_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c yuv2rgb_mlib.c \
	alphablend.c video_out_opengl.c
xineplug_vo_out_opengl_la_LIBADD =  $(OPENGL_LIBS) $(GLUT_LIBS) $(GLU_LIBS) $(X_LIBS) \
	$(top_builddir)/src/xine-utils/libxineutils.la
xineplug_vo_out_opengl_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_syncfb_la_SOURCES = alphablend.c video_out_syncfb.c 
xineplug_vo_out_syncfb_la_LIBADD = $(X_LIBS) -lXext \
	$(top_builddir)/src/xine-utils/libxineutils.la \
	$(top_builddir)/src/xine-engine/libxine.la
xineplug_vo_out_syncfb_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_aa_la_SOURCES =  video_out_aa.c
xineplug_vo_out_aa_la_LIBADD = $(AALIB_LIBS)
xineplug_vo_out_aa_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_fb_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c yuv2rgb_mlib.c \
	alphablend.c video_out_fb.c
xineplug_vo_out_fb_la_LIBADD = $(top_builddir)/src/xine-utils/libxineutils.la \
	$(top_builddir)/src/xine-engine/libxine.la
xineplug_vo_out_fb_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_directfb_la_SOURCES = alphablend.c video_out_directfb.c
xineplug_vo_out_directfb_la_LIBADD = $(DIRECTFB_LIBS)
xineplug_vo_out_directfb_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_sdl_la_SOURCES = alphablend.c video_out_sdl.c
xineplug_vo_out_sdl_la_LIBADD = $(SDL_LIBS) \
	$(top_builddir)/src/xine-engine/libxine.la
xineplug_vo_out_sdl_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_vidix_la_SOURCES = alphablend.c video_out_vidix.c
xineplug_vo_out_vidix_la_LIBADD =  $(X_LIBS) $(top_builddir)/src/video_out/vidix/libvidix.la \
	$(top_builddir)/src/xine-engine/libxine.la
xineplug_vo_out_vidix_la_LDFLAGS = -avoid-version -module

xineplug_vo_out_pgx64_la_SOURCES = video_out_pgx64.c
xineplug_vo_out_pgx64_la_LIBADD = $(X_LIBS)
xineplug_vo_out_pgx64_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = yuv2rgb.h video_out_syncfb.h alphablend.h deinterlace.h

include_HEADERS = video_out_x11.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) -DXINE_COMPILE"

install-debug: debug
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit;\
	done;
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
