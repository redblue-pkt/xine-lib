SUBDIR = libvcd

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic
INCLUDES = $(LIBCDIO_CFLAGS)

libvcd_SRCS = \
	vcd.c \
	data_structures.c \
	directory.c \
	files.c \
	image.c \
	image_bincue.c \
	image_cdrdao.c \
	image_nrg.c \
	logging.c \
	mpeg.c \
	mpeg_stream.c \
	pbc.c \
	salloc.c \
	sector.c \
	stream.c \
	stream_stdio.c \
	util.c 

libvcdinfo_SRCS = \
	info.c \
	inf.c \
	info_private.c \
	vcd_read.c

if ENABLE_VCDX
if HAVE_VCDNAV
EXTRA_DIST = $(libvcd_SRCS) $(libvcdinfo_SRCS)
else
noinst_LTLIBRARIES = libvcd.la libvcdinfo.la
libvcd_la_SOURCES = $(libvcd_SRCS)
libvcd_la_LDFLAGS = -avoid-version -module
libvcd_la_LIBADD = $(LIBCDIO_LIBS) $(LIBISO9660_LIBS)
libvcdinfo_la_SOURCES = $(libvcdinfo_SRCS)
libvcdinfo_la_LDFLAGS = -avoid-version -module
endif
endif

noinst_HEADERS = \
	assert.h \
	data_structures.h \
	info_private.h \
	pbc.h \
	stream_stdio.h \
	bitvec.h \
	dict.h \
	mpeg.h \
	salloc.h \
	util.h \
	bytesex_asm.h \
	directory.h \
	mpeg_stream.h \
	sector_private.h \
	vcd.h \
	bytesex.h \
	image_sink.h \
	obj.h \
	stream.h \
	vcd_read.h

debug:
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit;\
	done;
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit;\
	done;
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* 

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
