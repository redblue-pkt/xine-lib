include $(top_srcdir)/misc/Makefile.common

libdir = $(XINE_PLUGINDIR)

if DTS
dts_module = xineplug_decode_dts.la
endif

lib_LTLIBRARIES = $(dts_module)

if EXTERNAL_LIBDTS
internal_sources =
else
internal_sources = bitstream.c downmix.c parse.c 
endif

xineplug_decode_dts_la_SOURCES = xine_decoder.c $(internal_sources)
xineplug_decode_dts_la_LDFLAGS = -avoid-version -module $(XINE_PLUGIN_MIN_SYMS)

if EXTERNAL_LIBDTS
xineplug_decode_dts_la_LIBADD = $(XINE_LIB) $(LIBDTS_LIBS)
xineplug_decode_dts_la_CFLAGS = $(LIBDTS_CFLAGS)
else
xineplug_decode_dts_la_LIBADD = $(XINE_LIB)
endif

noinst_HEADERS = bitstream.h dts.h dts_internal.h tables.h tables_adpcm.h \
	tables_fir.h tables_huffman.h tables_quantization.h tables_vq.h
