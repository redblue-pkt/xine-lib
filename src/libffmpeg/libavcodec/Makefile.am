##
## Process this file with automake to produce Makefile.in
##

SUBDIRS = armv4l i386 mlib

#CFLAGS = -D_FILE_OFFSET_BITS=64 @GLOBAL_CFLAGS@ -DCONFIG_DECODERS -DHAVE_AV_CONFIG_H

CFLAGS = @GLOBAL_CFLAGS@ @LIBFFMPEG_CFLAGS@ -DCONFIG_DECODERS -DHAVE_AV_CONFIG_H
ASFLAGS =

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

noinst_LTLIBRARIES = libavcodec.la

libavcodec_la_SOURCES = common.c utils.c mpegvideo.c h263.c jrevdct.c jfdctfst.c \
			mjpeg.c dsputil.c \
			motion_est.c imgconvert.c msmpeg4.c \
			mpeg12.c h263dec.c rv10.c simple_idct.c
#imgresample.c
 
libavcodec_la_LDFLAGS = \
		$(top_builddir)/src/libffmpeg/libavcodec/armv4l/libavcodec_armv4l.la \
		$(top_builddir)/src/libffmpeg/libavcodec/i386/libavcodec_mmx.la \
		$(top_builddir)/src/libffmpeg/libavcodec/mlib/libavcodec_mlib.la \
		-avoid-version -module


noinst_HEADERS = avcodec.h    dsputil.h   mpegvideo.h \
		 common.h     h263data.h  mpeg4data.h   msmpeg4data.h \
		 mpeg12data.h simple_idct.h

.s.lo:
	$(ASCOMPILE) -o $@ `test -f $< || echo '$(srcdir)/'`$<

debug:
	@$(MAKE) CFLAGS="@DEBUG_CFLAGS@ @LIBFFMPEG_CFLAGS@ -DCONFIG_DECODERS -DHAVE_AV_CONFIG_H"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
