include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = genfb_vid.c nvidia_vid.c

libdir = $(XINE_PLUGINDIR)/vidix

if HAVE_VIDIX
vidix_drivers = \
	mach64_vid.la \
	mga_crtc2_vid.la \
	mga_vid.la \
	pm2_vid.la \
	pm3_vid.la \
	radeon_vid.la \
	rage128_vid.la \
	cyberblade_vid.la
endif

lib_LTLIBRARIES = $(vidix_drivers)

radeon_vid_la_SOURCES = radeon_vid.c
radeon_vid_la_LIBADD = -lm
radeon_vid_la_LDFLAGS = -avoid-version -module

rage128_vid.c:
	@echo "$(LN_S) -f $(top_srcdir)/src/video_out/vidix/drivers/radeon_vid.c \
	  $(top_srcdir)/src/video_out/vidix/drivers/$*.c"; \
	$(LN_S) -f $(top_srcdir)/src/video_out/vidix/drivers/radeon_vid.c \
	  $(top_srcdir)/src/video_out/vidix/drivers/$*.c

rage128_vid.lo: $(rage128_vid_la_SOURCES)
	source='$*.c' object='$@' libtool=yes \
	depfile='$(DEPDIR)/$*.Plo' tmpdepfile='$(DEPDIR)/$*.TPlo' \
	$(CCDEPMODE) $(depcomp) \
	$(LTCOMPILE) -DRAGE128 -c -o $@ `test -f $*.c || echo '$(srcdir)/'`$*.c

rage128_vid_la_SOURCES = rage128_vid.c
rage128_vid_la_LDFLAGS = -avoid-version -module

pm2_vid_la_SOURCES = pm2_vid.c
pm2_vid_la_LDFLAGS = -avoid-version -module

pm3_vid_la_SOURCES = pm3_vid.c
pm3_vid_la_LDFLAGS = -avoid-version -module

mach64_vid.lo:
	source='$*.c' object='$@' libtool=yes \
	depfile='$(DEPDIR)/$*.Plo' tmpdepfile='$(DEPDIR)/$*.TPlo' \
	$(CCDEPMODE) $(depcomp) \
	$(LTCOMPILE) -c -o $@ `test -f $*.c || echo '$(srcdir)/'`$*.c

mach64_vid_la_SOURCES = mach64_vid.c
mach64_vid_la_LDFLAGS = -avoid-version -module

mga_vid_la_SOURCES = mga_vid.c
mga_vid_la_LIBADD = -lm
mga_vid_la_LDFLAGS = -avoid-version -module

mga_crtc2_vid.c:
	@echo "$(LN_S) -f $(top_srcdir)/src/video_out/vidix/drivers/mga_vid.c \
	  $(top_srcdir)/src/video_out/vidix/drivers/$*.c"; \
	$(LN_S) -f $(top_srcdir)/src/video_out/vidix/drivers/mga_vid.c \
	  $(top_srcdir)/src/video_out/vidix/drivers/$*.c

mga_crtc2_vid.lo: $(mga_crtc2_vid_la_SOURCES)
	source='$*.c' object='$@' libtool=yes \
	depfile='$(DEPDIR)/$*.Plo' tmpdepfile='$(DEPDIR)/$*.TPlo' \
	$(CCDEPMODE) $(depcomp) \
	$(LTCOMPILE) -DCRTC2 -c -o $@ `test -f $*.c || echo '$(srcdir)/'`$*.c

mga_crtc2_vid_la_SOURCES = mga_crtc2_vid.c
mga_crtc2_vid_la_LIBADD = -lm
mga_crtc2_vid_la_LDFLAGS = -avoid-version -module

cyberblade_vid_la_SOURCES = cyberblade_vid.c
cyberblade_vid_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = mach64.h nvidia.h glint_regs.h pm3_regs.h radeon.h \
	cyberblade_regs.h

AM_CPPFLAGS = -I$(top_srcdir)/src/video_out/vidix \
	-I$(top_srcdir)/src/video_out/libdha
