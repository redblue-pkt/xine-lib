##
## Process this file with automake to produce Makefile.in
##

SUBDIRS = fftw codebook

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)


CFLAGS = @CFLAGS@ -I$(srcdir)/fftw

DEBUG_CFLAGS = @DEBUG_CFLAGS@ -I$(srcdir)/fftw

#if HAVE_FAAD
faad_module = xineplug_decode_faad.la
#endif

lib_LTLIBRARIES = $(faad_module)

VPATH = @srcdir@:@srcdir@/codebook:

xineplug_decode_faad_la_SOURCES = xine_decoder.c \
	bits.c data.c decoder.c drc.c error.c filtbank.c \
	ic_predict.c is.c lt_predict.c mdct.c mp4.c ms.c output.c pns.c \
	pulse.c specrec.c syntax.c tns.c reordered_spectral_data.c \
	hcb_1.c hcb_2.c hcb_3.c hcb_4.c hcb_5.c hcb_6.c hcb_7.c hcb_8.c \
	hcb_9.c hcb_10.c hcb_11.c hcb_sf.c
xineplug_decode_faad_la_LDFLAGS = -avoid-version -module
xineplug_decode_faad_la_LIBADD = $(top_builddir)/src/libfaad/fftw/libfftw.la \
	$(top_builddir)/src/xine-utils/libxineutils.la

noinst_HEADERS = analysis.h bits.h common.h data.h decoder.h drc.h error.h \
		 filtbank.h huffman.h ic_predict.h is.h kbd_win.h \
		 lt_predict.h mdct.h mp4.h ms.h output.h pns.h pulse.h \
		 sbr_dec.h sbr_huff.h sbr_qmf.h sbr_syntax.h \
		 specrec.h syntax.h tns.h codebook/hcb.h faad.h


debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
