include $(top_srcdir)/misc/Makefile.common

SUBDIRS = armv4l i386 mlib alpha ppc libpostproc

## we include some unused files
EXTRA_DIST = fdctref.c motion_est_template.c svq3.c wmv2.c \
	imgresample.c motion_est.c

AM_CFLAGS = $(LIBFFMPEG_CFLAGS)
ASFLAGS =

noinst_LTLIBRARIES = libavcodec.la

libavcodec_la_SOURCES = \
	4xm.c \
	adpcm.c \
	asv1.c \
	cabac.c \
	cinepak.c \
	common.c \
	cyuv.c \
	dpcm.c \
	dsputil.c \
	dv.c \
	error_resilience.c \
	eval.c \
	fft.c \
	golomb.c \
	h263.c \
	h263dec.c \
	h264.c \
	huffyuv.c \
	idcinvideo.c \
	imgconvert.c \
	indeo3.c \
	interplayvideo.c \
	jfdctfst.c \
	jfdctint.c \
	jrevdct.c \
	mdct.c \
	mace.c \
	mem.c \
	mjpeg.c \
	mpeg12.c \
	mpegaudiodec.c \
	mpegvideo.c \
	msmpeg4.c \
	msrle.c \
	msvideo1.c \
	opts.c \
	pcm.c \
	ra144.c \
	ra288.c \
	ratecontrol.c \
	rational.c \
	roqvideo.c \
	rpza.c \
	rv10.c \
	simple_idct.c \
	svq1.c \
	utils.c \
	vcr1.c \
	vp3.c \
	vqavideo.c \
	wmadec.c \
	xan.c

libavcodec_la_LDFLAGS = \
	$(top_builddir)/src/libffmpeg/libavcodec/armv4l/libavcodec_armv4l.la \
	$(top_builddir)/src/libffmpeg/libavcodec/i386/libavcodec_mmx.la \
	$(top_builddir)/src/libffmpeg/libavcodec/mlib/libavcodec_mlib.la \
	$(top_builddir)/src/libffmpeg/libavcodec/ppc/libavcodec_ppc.la \
	-avoid-version -module


noinst_HEADERS = \
	avcodec.h \
	cabac.h \
	common.h \
	dsputil.h \
	dvdata.h \
	fastmemcpy.h \
	golomb.h \
	imgconvert_template.h \
	indeo3data.h \
	h263data.h \
	h264data.h \
	mangle.h \
	mpeg4data.h \
	mpeg12data.h \
	mpegaudio.h \
	mpegaudiodectab.h \
	mpegaudiotab.h \
	mpegvideo.h \
	msmpeg4data.h \
	os_support.h \
	ra144.h \
	ra288.h \
	rational.h \
	simple_idct.h \
	sp5x.h \
	svq1_cb.h \
	svq1_vlc.h \
	vp3data.h \
	wmadata.h
