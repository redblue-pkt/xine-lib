##
## Process this file with automake to produce Makefile.in
##

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

noinst_LTLIBRARIES = libgsm610.la

libgsm610_la_SOURCES = \
	add.c \
	decode.c \
	gsm_create.c \
	gsm_decode.c \
	gsm_destroy.c \
	long_term.c \
	lpc.c \
	rpe.c \
	short_term.c \
	table.c

libgsm610_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = \
	gsm_config.h \
	gsm.h \
	private.h \
	proto.h \
	unproto.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(LIBFFMPEG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in

