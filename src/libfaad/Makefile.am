##
## Process this file with automake to produce Makefile.in
##

SUBDIRS = codebook

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if BUILD_FAAD
faad_module = xineplug_decode_faad.la
endif

lib_LTLIBRARIES = $(faad_module)

VPATH = @srcdir@:@srcdir@/codebook:

xineplug_decode_faad_la_SOURCES = \
	bits.c \
	cfft.c \
	data.c \
	decoder.c \
	drc.c \
	error.c \
	filtbank.c \
	codebook/hcb_1.c \
	codebook/hcb_2.c \
	codebook/hcb_3.c \
	codebook/hcb_4.c \
	codebook/hcb_5.c \
	codebook/hcb_6.c \
	codebook/hcb_7.c \
	codebook/hcb_8.c \
	codebook/hcb_9.c \
	codebook/hcb_10.c \
	codebook/hcb_11.c \
	codebook/hcb_sf.c \
	ic_predict.c \
	is.c \
	lt_predict.c \
	mdct.c \
	mp4.c \
	ms.c \
	output.c \
	pns.c \
	pulse.c \
	reordered_spectral_data.c \
	rvlc_scale_factors.c \
	specrec.c \
	syntax.c \
	tns.c \
	xine_decoder.c

xineplug_decode_faad_la_LDFLAGS = -avoid-version -module
xineplug_decode_faad_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la

noinst_HEADERS = \
	analysis.h \
	bits.h \
	cfft.h \
	common.h \
	data.h \
	decoder.h \
	drc.h \
	error.h \
	faad.h  \
	filtbank.h \
	huffman.h \
	ic_predict.h \
	is.h \
	kbd_win.h \
	lt_predict.h \
	mdct.h \
	mp4.h \
	ms.h \
	output.h \
	pns.h \
	pulse.h \
	rvlc_scale_factors.h \
	sbr_dec.h \
	sbr_huff.h \
	sbr_qmf.h \
	sbr_syntax.h \
	specrec.h \
	syntax.h \
	tns.h 

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
