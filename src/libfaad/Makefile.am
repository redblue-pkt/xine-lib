include $(top_srcdir)/misc/Makefile.common

SUBDIRS = codebook

libdir = $(XINE_PLUGINDIR)

if BUILD_FAAD
faad_module = xineplug_decode_faad.la
endif

lib_LTLIBRARIES = $(faad_module)

VPATH = @srcdir@:@srcdir@/codebook:

xineplug_decode_faad_la_SOURCES = \
	bits.c \
	cfft.c \
	common.c \
	data.c \
	decoder.c \
	dither.c \
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
	hcr.c \
	ic_predict.c \
	is.c \
	lt_predict.c \
	mdct.c \
	mp4.c \
	ms.c \
	output.c \
	pns.c \
	pulse.c \
	rvlc.c \
	specrec.c \
	ssr.c \
	ssr_fb.c \
	ssr_ipqf.c \
	syntax.c \
	tns.c \
	xine_decoder.c

xineplug_decode_faad_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
xineplug_decode_faad_la_LIBADD = $(XINE_LIB)

noinst_HEADERS = \
	analysis.h \
	bits.h \
	cfft.h \
	cfft_tab.h \
	common.h \
	data.h \
	decoder.h \
	dither.h \
	drc.h \
	error.h \
	faad.h  \
	filtbank.h \
	fixed.h \
	huffman.h \
	ic_predict.h \
	is.h \
	iq_table.h \
	kbd_win.h \
	lt_predict.h \
	mdct.h \
	mp4.h \
	ms.h \
	output.h \
	pns.h \
	pulse.h \
	rvlc.h \
	sine_win.h \
	specrec.h \
	ssr.h \
	ssr_fb.h \
	ssr_ipqf.h \
	ssr_win.h \
	structs.h \
	syntax.h \
	tns.h 
