AM_CFLAGS = -DXINE_COMPILE $(ALSA_CFLAGS) $(ESD_CFLAGS) $(IRIXAL_CFLAGS) $(ARTS_CFLAGS)

EXTRA_DIST = audio_arts_out.c \
	audio_esd_out.c \
	audio_irixal_out.c \
	audio_sun_out.c 

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if HAVE_OSS
oss_module = xineplug_ao_out_oss.la
endif

if HAVE_ALSA
if HAVE_ALSA09
alsa_module = xineplug_ao_out_alsa.la
endif
endif

#if HAVE_ESD
#esd_module = xineplug_ao_out_esd.la
#endif

if HAVE_SUNAUDIO
sun_module = xineplug_ao_out_sun.la
endif

#if HAVE_IRIXAL
#irixal_module = xineplug_ao_out_irixal.la
#endif

#if HAVE_ARTS
#arts_module = xineplug_ao_out_arts.la
#endif

##
# IMPORTANT:
# ---------
# all xine audio out plugins should be named like the 
# scheme "xineplug_ao_out_"
#
lib_LTLIBRARIES = $(oss_module) \
	$(alsa_module) \
	$(sun_module)

#lib_LTLIBRARIES = \
#	$(alsa_module) \
#	$(arts_module) \
#	$(esd_module) \
#	$(irixal_module) \
#	$(oss_module) \
#	$(sun_module) 

xineplug_ao_out_oss_la_SOURCES = audio_oss_out.c
xineplug_ao_out_oss_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_ao_out_alsa_la_SOURCES = audio_alsa_out.c
xineplug_ao_out_alsa_la_LIBADD = $(ALSA_LIBS)
xineplug_ao_out_alsa_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

#xineplug_ao_out_esd_la_SOURCES = audio_esd_out.c 
#xineplug_ao_out_esd_la_LIBADD = $(ESD_LIBS)
#xineplug_ao_out_esd_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_ao_out_sun_la_SOURCES = audio_sun_out.c
xineplug_ao_out_sun_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

#xineplug_ao_out_irixal_la_SOURCES = audio_irixal_out.c 
#xineplug_ao_out_irixal_la_LIBADD = $(IRIXAL_LIBS)
#xineplug_ao_out_irixal_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

#xineplug_ao_out_arts_la_SOURCES = audio_arts_out.c
#xineplug_ao_out_arts_la_LIBADD = $(ARTS_LIBS)
#xineplug_ao_out_arts_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = \
	audio_alsa_out.h \
	audio_arts_out.h \
	audio_esd_out.h \
	audio_irixal_out.h \
	audio_oss_out.h \
	audio_sun_out.h

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

