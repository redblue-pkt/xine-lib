
LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)/vidix

if HAVE_VIDIX
vidix_drivers = radeon_vid.la rage128_vid.la pm3_vid.la mach64_vid.la nvidia_vid.la \
	genfb_vid.la mga_vid.la mga_crtc2_vid.la
endif

lib_LTLIBRARIES = $(vidix_drivers)

radeon_vid_la_SOURCES = radeon_vid.c
radeon_vid_la_LIBADD = $(top_builddir)/src/video_out/libdha/libdha.la -lm
radeon_vid_la_LDFLAGS = -avoid-version -module

rage128_vid_la_SOURCES = radeon_vid.c
rage128_vid_la_LIBADD = $(top_builddir)/src/video_out/libdha/libdha.la
rage128_vid_la_LDFLAGS = -avoid-version -module -DRAGE128

pm3_vid_la_SOURCES = pm3_vid.c
pm3_vid_la_LIBADD = $(top_builddir)/src/video_out/libdha/libdha.la
pm3_vid_la_LDFLAGS = -avoid-version -module

mach64_vid_la_SOURCES = mach64_vid.c
mach64_vid_la_LIBADD = $(top_builddir)/src/video_out/libdha/libdha.la
mach64_vid_la_LDFLAGS = -avoid-version -module -DRAGE128

nvidia_vid_la_SOURCES = nvidia_vid.c
nvidia_vid_la_LIBADD = $(top_builddir)/src/video_out/libdha/libdha.la -lm
nvidia_vid_la_LDFLAGS = -avoid-version -module

genfb_vid_la_SOURCES = genfb_vid.c
genfb_vid_la_LIBADD = $(top_builddir)/src/video_out/libdha/libdha.la -lm
genfb_vid_la_LDFLAGS = -avoid-version -module

mga_vid_la_SOURCES = mga_vid.c
mga_vid_la_LIBADD = $(top_builddir)/src/video_out/libdha/libdha.la -lm
mga_vid_la_LDFLAGS = -avoid-version -module

mga_crtc2_vid_la_SOURCES = mga_vid.c
mga_crtc2_vid_la_LIBADD = $(top_builddir)/src/video_out/libdha/libdha.la -lm
mga_crtc2_vid_la_LDFLAGS = -avoid-version -module -DCRTC2

noinst_HEADERS = mach64.h  nvidia.h  pm3_regs.h	radeon.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) -DXINE_COMPILE"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
