## Process this file with automake to produce Makefile.in

AM_CFLAGS = $(X_CFLAGS)

noinst_LTLIBRARIES= libnvclient.la

libnvclient_sources = back_client.c pipe.c error.c

libnvclient_dummy = libnvclient_dummy.c

EXTRA_DIST = $(libnvclient_sources) $(libnvclient_dummy)

if HAVE_NVTV
nvtv_modules = $(libnvclient_sources)
endif

libnvclient_la_SOURCES = $(nvtv_modules) $(libnvclient_dummy)


noinst_HEADERS = backend.h local.h tv_chip.h xf86i2c.h back_client.h debug.h \
	         miscstruct.h pipe.h tv_common.h xfree.h error.h

libnvclient_la_LIBADD = $(X_LIBS) 

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(X_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
