include $(top_srcdir)/misc/Makefile.common

SUBDIRS = cdio MSWindows image

INCLUDES = $(LIBCDIO_CFLAGS) -I$(top_srcdir)/include -I$(top_srcdir)/lib

libcdio_SRCS = \
	image/bincue.c \
	image/nrg.c \
	MSWindows/aspi32.c \
	MSWindows/aspi32.h \
	MSWindows/ioctl.c \
	MSWindows/win32.c \
	MSWindows/win32.h \
	_cdio_bsdi.c \
	_cdio_freebsd.c \
	_cdio_generic.c \
	_cdio_linux.c \
	_cdio_osx.c \
	_cdio_stdio.c \
	_cdio_stream.c \
	_cdio_sunos.c \
	cdio.c \
	cd_types.c \
	ds.c \
	logging.c \
	sector.c \
	util.c
libiso9660_SRCS = \
	iso9660.c \
	iso9660_fs.c \
	xa.c

EXTRA_DIST = $(libcdio_SRCS) $(libiso9660_SRCS)

if ENABLE_VCD
if HAVE_VCDNAV
else
noinst_LTLIBRARIES = libcdio.la libiso9660.la
libcdio_la_SOURCES = $(libcdio_SRCS)
libcdio_la_LDFLAGS = -avoid-version -module
libiso9660_la_SOURCES = $(libiso9660_SRCS)
libiso9660_la_LDFLAGS = -avoid-version -module
endif
endif

noinst_HEADERS = \
	bytesex_asm.h \
	cdio_assert.h \
	_cdio_stdio.h \
	ds.h \
	scsi_mmc.h \
	bytesex.h \
	cdio_private.h \
	_cdio_stream.h \
	iso9660_private.h
