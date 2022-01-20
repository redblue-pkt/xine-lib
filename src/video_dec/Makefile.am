AUTOMAKE_OPTIONS = subdir-objects

include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS  = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

EXTRA_DIST = foovideo.c

xineplug_LTLIBRARIES = 
if ENABLE_IMAGEMAGICK
xineplug_LTLIBRARIES += xineplug_decode_image.la
endif
if ENABLE_GDK_PIXBUF
xineplug_LTLIBRARIES += xineplug_decode_gdk_pixbuf.la
endif
if ENABLE_DAV1D
xineplug_LTLIBRARIES += xineplug_decode_dav1d.la
endif
if ENABLE_LIBAOM
xineplug_LTLIBRARIES += xineplug_decode_libaom.la
endif
if ENABLE_LIBJPEG
xineplug_LTLIBRARIES += xineplug_decode_libjpeg.la
endif
if ENABLE_LIBPNG
xineplug_LTLIBRARIES += xineplug_decode_libpng.la
endif
if ENABLE_OPENHEVC
xineplug_LTLIBRARIES += xineplug_decode_libopenhevc.la
endif
if ENABLE_VPX
xineplug_LTLIBRARIES += xineplug_decode_libvpx.la
endif
if ENABLE_MMAL
xineplug_LTLIBRARIES += xineplug_decode_libmmal.la
endif
if ENABLE_VDPAU
xineplug_LTLIBRARIES += xineplug_decode_vdpau.la
endif
if ENABLE_RAWVIDEO
xineplug_LTLIBRARIES += xineplug_decode_rawvideo.la
endif
if ENABLE_LIBMPEG2
xineplug_LTLIBRARIES += xineplug_decode_mpeg2.la
endif
if ENABLE_MPEG2NEW
xineplug_LTLIBRARIES += xineplug_decode_mpeg2new.la
endif


xineplug_decode_rawvideo_la_SOURCES = \
	group_raw.c \
	group_raw.h \
	bitplane.c \
	rgb.c \
	yuv.c
xineplug_decode_rawvideo_la_LIBADD = $(XINE_LIB) $(LTLIBINTL)

xineplug_decode_image_la_SOURCES = image.c
xineplug_decode_image_la_LIBADD = $(XINE_LIB) $(WAND_LIBS)
xineplug_decode_image_la_CFLAGS = $(AM_CFLAGS) $(WAND_CFLAGS)

xineplug_decode_gdk_pixbuf_la_SOURCES = gdkpixbuf.c
xineplug_decode_gdk_pixbuf_la_LIBADD = $(XINE_LIB) $(GDK_PIXBUF_LIBS)
xineplug_decode_gdk_pixbuf_la_CFLAGS = $(AM_CFLAGS) $(GDK_PIXBUF_CFLAGS)

xineplug_decode_libjpeg_la_SOURCES = libjpeg.c
xineplug_decode_libjpeg_la_LIBADD = $(XINE_LIB) $(JPEG_LIBS) $(LTLIBINTL)
xineplug_decode_libjpeg_la_CFLAGS = $(AM_CFLAGS) $(JPEG_CFLAGS)

xineplug_decode_libpng_la_SOURCES = libpng.c
xineplug_decode_libpng_la_LIBADD = $(XINE_LIB) $(LIBPNG_LIBS) $(LTLIBINTL)
xineplug_decode_libpng_la_CFLAGS = $(AM_CFLAGS) $(LIBPNG_CFLAGS)

xineplug_decode_libopenhevc_la_SOURCES = libopenhevc.c
xineplug_decode_libopenhevc_la_LIBADD = $(XINE_LIB) $(OPENHEVC_LIBS) -lm
xineplug_decode_libopenhevc_la_CFLAGS = $(AM_CFLAGS) $(OPENHEVC_CFLAGS)
xineplug_decode_libopenhevc_la_LDFLAGS = $(AM_LDFLAGS) -Wl,-Bsymbolic

xineplug_decode_libvpx_la_SOURCES = libvpx.c
xineplug_decode_libvpx_la_LIBADD = $(XINE_LIB) $(VPX_LIBS)
xineplug_decode_libvpx_la_CFLAGS = $(AM_CFLAGS) $(VPX_CFLAGS)

xineplug_decode_libaom_la_SOURCES = libaom.c
xineplug_decode_libaom_la_LIBADD = $(XINE_LIB) $(AOM_LIBS)
xineplug_decode_libaom_la_CFLAGS = $(AM_CFLAGS) $(AOM_CFLAGS)

xineplug_decode_dav1d_la_SOURCES = dav1d.c
xineplug_decode_dav1d_la_LIBADD = $(XINE_LIB) $(DAV1D_LIBS)
xineplug_decode_dav1d_la_CFLAGS = $(AM_CFLAGS) $(DAV1D_CFLAGS)

xineplug_decode_libmmal_la_SOURCES = mmal.c
xineplug_decode_libmmal_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS) $(MMAL_LIBS)
xineplug_decode_libmmal_la_CFLAGS = $(AM_CFLAGS) $(MMAL_CFLAGS)
xineplug_decode_libmmal_la_LDFLAGS = $(AM_LDFLAGS) $(MMAL_LDFLAGS)

xineplug_decode_vdpau_la_SOURCES = \
	libvdpau/group_vdpau.c \
	libvdpau/group_vdpau.h \
	libvdpau/vdec_hw_bits_reader.h \
	libvdpau/vdec_hw_h264.h \
	libvdpau/vdec_hw_h264.c \
	libvdpau/alterh264_decode.c \
	libvdpau/bits_reader.h \
	libvdpau/vdpau_h264.c \
	libvdpau/vdpau_mpeg12.c \
	libvdpau/vdpau_vc1.c \
	libvdpau/vdpau_mpeg4.c
xineplug_decode_vdpau_la_CFLAGS = $(AM_CFLAGS) $(VDPAU_CFLAGS) -D_ISOC99_SOURCE
xineplug_decode_vdpau_la_LIBADD = $(XINE_LIB) -lm

xineplug_decode_mpeg2_la_SOURCES = \
	libmpeg2/cpu_state.c \
	libmpeg2/decode.c \
	libmpeg2/header.c \
	libmpeg2/idct.c \
	libmpeg2/idct_altivec.c \
	libmpeg2/idct_mlib.h \
	libmpeg2/idct_mlib.c \
	libmpeg2/idct_mmx.c \
	libmpeg2/motion_comp.c \
	libmpeg2/motion_comp_altivec.c \
	libmpeg2/motion_comp_mmx.c \
	libmpeg2/motion_comp_mlib.c \
	libmpeg2/motion_comp_vis.c \
	libmpeg2/mpeg2.h \
	libmpeg2/mpeg2_internal.h \
	libmpeg2/slice.c \
	libmpeg2/slice_xvmc.c \
	libmpeg2/slice_xvmc_vld.c \
	libmpeg2/stats.c \
	libmpeg2/vis.h \
	libmpeg2/vlc.h \
	libmpeg2/xine_mpeg2_decoder.c \
	libmpeg2/xvmc.h \
	libmpeg2/xvmc_vld.h \
	libmpeg2/libmpeg2_accel.h \
	libmpeg2/libmpeg2_accel.c

xineplug_decode_mpeg2_la_LIBADD = $(XINE_LIB) $(MLIB_LIBS) $(LTLIBINTL) -lm
xineplug_decode_mpeg2_la_CFLAGS = $(AM_CFLAGS) $(MLIB_CFLAGS)

xineplug_decode_mpeg2new_la_SOURCES = \
	libmpeg2new/xine_mpeg2new_decoder.c \
	\
	libmpeg2new/include/attributes.h \
	libmpeg2new/include/video_out.h \
	libmpeg2new/include/mpeg2.h \
	libmpeg2new/include/mpeg2convert.h \
	libmpeg2new/libmpeg2/alloc.c \
	libmpeg2new/libmpeg2/decode.c \
	libmpeg2new/libmpeg2/header.c \
	libmpeg2new/libmpeg2/idct.c \
	libmpeg2new/libmpeg2/motion_comp.c \
	libmpeg2new/libmpeg2/mpeg2_internal.h \
	libmpeg2new/libmpeg2/slice.c \
	libmpeg2new/libmpeg2/vlc.h \
	\
	libmpeg2new/include/alpha_asm.h \
	libmpeg2new/include/mmx.h \
	libmpeg2new/include/tendra.h \
	libmpeg2new/include/vis.h \
	libmpeg2new/libmpeg2/motion_comp_mmx.c \
	libmpeg2new/libmpeg2/idct_mmx.c \
	libmpeg2new/libmpeg2/motion_comp_altivec.c \
	libmpeg2new/libmpeg2/idct_altivec.c \
	libmpeg2new/libmpeg2/motion_comp_alpha.c \
	libmpeg2new/libmpeg2/idct_alpha.c \
	libmpeg2new/libmpeg2/motion_comp_vis.c \
	libmpeg2new/libmpeg2/cpu_accel.c \
	libmpeg2new/libmpeg2/cpu_state.c

xineplug_decode_mpeg2new_la_LIBADD = $(XINE_LIB)
xineplug_decode_mpeg2new_la_CFLAGS = $(AM_CFLAGS) $(MLIB_CFLAGS)

