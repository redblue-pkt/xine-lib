##
## Process this file with automake to produce Makefile.in
##

CFLAGS = @GLOBAL_CFLAGS@ -DCONFIG_DECODERS -DHAVE_AV_CONFIG_H

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

noinst_LTLIBRARIES = libavcodec.la

if HAVE_FFMMX
mmx_modules = mpegvideo_mmx.c sad_mmx.s dsputil_mmx.c idct_mmx.c
#mmx_modules = mpegvideo_mmx.c sad_mmx.s
endif

libavcodec_la_SOURCES = dsputil.c   fdctref.c   jfdctfst.c  mpeg12.c   \
		        utils.c     rv10.c      h263.c      jrevdct.c  \
		        common.c    h263dec.c   msmpeg4.c   \
			mpegvideo.c mjpeg.c     motion_est.c \
			$(mmx_modules)


noinst_HEADERS = avcodec.h    dsputil.h   mpegvideo.h   dsputil_mmx_avg.h\
		 common.h     h263data.h  mpeg4data.h   msmpeg4data.h \
		 mpeg12data.h

debug:
	$(MAKE) CFLAGS="-Wall -DDEBUG -O -finline-functions -g -DXINE_COMPILE"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
