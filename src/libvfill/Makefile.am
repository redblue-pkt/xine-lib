#
# the videofill plugin
#

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_vfill.la

xineplug_decode_vfill_la_SOURCES = xine_decoder.c
xineplug_decode_vfill_la_LDFLAGS = -avoid-version -module

# noinst_HEADERS = 

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
