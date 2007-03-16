include $(top_srcdir)/misc/Makefile.common

xineplug_LTLIBRARIES = xineplug_decode_real.la xineplug_decode_real_audio.la

xineplug_decode_real_la_SOURCES = xine_decoder.c real_common.c
xineplug_decode_real_la_LIBADD = $(XINE_LIB) $(DYNAMIC_LD_LIBS)
xineplug_decode_real_la_CFLAGS = $(VISIBILITY_FLAG)
xineplug_decode_real_la_LDFLAGS = $(xineplug_ldflags)

xineplug_decode_real_audio_la_SOURCES = audio_decoder.c real_common.c
xineplug_decode_real_audio_la_LIBADD = $(XINE_LIB) $(DYNAMIC_LD_LIBS)
xineplug_decode_real_audio_la_CFLAGS = $(VISIBILITY_FLAG)
xineplug_decode_real_audio_la_LDFLAGS = $(xineplug_ldflags)
