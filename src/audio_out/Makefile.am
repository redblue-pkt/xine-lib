CFLAGS = @GLOBAL_CFLAGS@ -DXINE_COMPILE

EXTRA_DIST = audio_alsa_out.c audio_esd_out.c

if HAVE_ALSA
alsa_module = xineplug_ao_out_alsa.la
endif

if HAVE_ESD
esd_module = xineplug_ao_out_esd.la
endif

##
# IMPORTANT:
# ---------
# All of xine audio out plugins should be named like the 
# scheme "xineplug_ao_out_"
#
##lib_LTLIBRARIES = xineplug_ao_out_oss.la $(alsa_module) $(esd_module)
lib_LTLIBRARIES =

##xineplug_ao_out_oss_la_SOURCES = audio_oss_out.c resample.c
##xineplug_ao_out_oss_la_LDFLAGS = -avoid-version -module

##xineplug_ao_out_alsa_la_SOURCES = audio_alsa_out.c resample.c
##xineplug_ao_out_alsa_la_LDFLAGS = -avoid-version -module

##xineplug_ao_out_esd_la_SOURCES = audio_esd_out.c resample.c
##xineplug_ao_out_esd_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = audio_oss_out.h audio_alsa_out.h audio_esd_out.h resample.h


debug:
	$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"


mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
