include $(top_srcdir)/misc/Makefile.common

libdir = $(XINE_PLUGINDIR)

if HAVE_GICONV
sputext_decoder = xineplug_decode_sputext.la xineplug_decode_spuogm.la xineplug_dmx_sputext.la
endif

lib_LTLIBRARIES = $(sputext_decoder)

xineplug_dmx_sputext_la_SOURCES = demux_sputext.c
## libgiconv is only needed with FreeBSD
xineplug_dmx_sputext_la_LIBADD  = $(XINE_LIB) $(GICONV_BSD_LIBS)
xineplug_dmx_sputext_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_sputext_la_SOURCES = xine_decoder.c 
xineplug_decode_sputext_la_LIBADD  = $(XINE_LIB)
xineplug_decode_sputext_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_spuogm_la_SOURCES = xine_decoder_ogm.c 
xineplug_decode_spuogm_la_LIBADD  = $(XINE_LIB)
xineplug_decode_spuogm_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
