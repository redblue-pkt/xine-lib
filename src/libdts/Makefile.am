include $(top_srcdir)/misc/Makefile.common

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_dts.la

xineplug_decode_dts_la_SOURCES = bitstream.c downmix.c parse.c xine_decoder.c
xineplug_decode_dts_la_LIBADD = $(XINE_LIB)
xineplug_decode_dts_la_LDFLAGS = -avoid-version -module $(XINE_PLUGIN_MIN_SYMS)

noinst_HEADERS = bitstream.h dts.h dts_internal.h tables.h tables_adpcm.h \
	tables_fir.h tables_huffman.h tables_quantization.h tables_vq.h
