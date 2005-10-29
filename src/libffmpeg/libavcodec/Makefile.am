include $(top_srcdir)/misc/Makefile.common

SUBDIRS = armv4l i386 mlib alpha ppc sparc libpostproc

# some of ffmpeg's decoders are not used by xine yet
EXTRA_DIST = motion_est_template.c imgresample.c \
	adx.c cljr.c fdctref.c ffv1.c g726.c mdec.c raw.c svq3.c wmv2.c

# we need to compile everything in debug mode, including the encoders,
# otherwise we get unresolved symbols, because some unsatisfied function calls
# are not optimized away with debug optimization
AM_CFLAGS = $(LIBFFMPEG_CFLAGS) `test "$(CFLAGS)" = "$(DEBUG_CFLAGS)" && echo -DCONFIG_ENCODERS` -I../libavutil
AM_CPPFLAGS = $(ZLIB_CPPFLAGS) -I../libavutil
ASFLAGS =

noinst_LTLIBRARIES = libavcodec.la

libavcodec_la_SOURCES = \
	4xm.c \
	8bps.c \
	aasc.c \
	adpcm.c \
	alac.c \
	asv1.c \
	bitstream.c \
	cabac.c \
	cinepak.c \
	cyuv.c \
	dpcm.c \
	dsputil.c \
	dv.c \
	error_resilience.c \
	eval.c \
	faandct.c \
	flac.c \
	flicvideo.c \
	fraps.c \
	fft.c \
	golomb.c \
	h261.c \
	h263.c \
	h263dec.c \
	h264.c \
	h264idct.c \
	huffyuv.c \
	idcinvideo.c \
	imgconvert.c \
	indeo2.c \
	indeo3.c \
	interplayvideo.c \
	jfdctfst.c \
	jfdctint.c \
	jrevdct.c \
	lcl.c \
	loco.c \
	mdct.c \
	mace.c \
	mem.c \
	mjpeg.c \
	motion_est.c \
	mpeg12.c \
	mpegaudiodec.c \
	mpegvideo.c \
	msmpeg4.c \
	msrle.c \
	msvideo1.c \
	parser.c \
	pcm.c \
	qdm2.c \
	qdrw.c \
	qpeg.c \
	qtrle.c \
	ra144.c \
	ra288.c \
	rangecoder.c \
	ratecontrol.c \
	roqvideo.c \
	rpza.c \
	rv10.c \
	shorten.c \
	simple_idct.c \
	smc.c \
	snow.c \
	svq1.c \
	tscc.c \
	truemotion1.c \
	truemotion2.c \
	ulti.c \
	utils.c \
	vcr1.c \
	vmdav.c \
	vorbis.c \
	vp3.c \
	vp3dsp.c \
	vqavideo.c \
	wmadec.c \
	wnv1.c \
	xan.c \
	xl.c

libavcodec_la_LDFLAGS = \
	$(top_builddir)/src/libffmpeg/libavcodec/armv4l/libavcodec_armv4l.la \
	$(top_builddir)/src/libffmpeg/libavcodec/i386/libavcodec_mmx.la \
	$(top_builddir)/src/libffmpeg/libavcodec/mlib/libavcodec_mlib.la \
	$(top_builddir)/src/libffmpeg/libavcodec/ppc/libavcodec_ppc.la \
	$(top_builddir)/src/libffmpeg/libavcodec/sparc/libavcodec_sparc.la \
	-avoid-version -module


noinst_HEADERS = \
	avcodec.h \
	bitstream.h \
	cabac.h \
	dsputil.h \
	dvdata.h \
	faandct.h \
	fastmemcpy.h \
	golomb.h \
	imgconvert_template.h \
	indeo2data.h \
	indeo3data.h \
	h261data.h \
	h263data.h \
	h264data.h \
	mpeg4data.h \
	mpeg12data.h \
	mpegaudio.h \
	mpegaudiodectab.h \
	mpegaudiotab.h \
	mpegvideo.h \
	msmpeg4data.h \
	opt.h \
	qdm2data.h \
	ra144.h \
	ra288.h \
	rangecoder.h \
	simple_idct.h \
	sp5x.h \
	svq1_cb.h \
	svq1_vlc.h \
	truemotion1data.h \
	ulti_cb.h \
	vorbis.h \
	vp3data.h \
	wmadata.h
