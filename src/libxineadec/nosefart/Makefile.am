##
## Process this file with automake to produce Makefile.in
##

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

AM_CFLAGS = -DNSF_PLAYER

noinst_LTLIBRARIES = libnosefart.la

libnosefart_la_SOURCES = \
	fds_snd.c \
	log.c \
	mmc5_snd.c \
	nes_apu.c \
	vrc7_snd.c \
	dis6502.c \
	fmopl.c \
	memguard.c \
	nes6502.c \
	nsf.c \
	vrcvisnd.c	

libnosefart_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = \
	dis6502.h \
	fmopl.h \
	memguard.h \
	nes6502.h \
	nsf.h \
	types.h \
	vrc7_snd.h \
	fds_snd.h \
	log.h \
	mmc5_snd.h \
	nes_apu.h \
	osd.h \
	version.h \
	vrcvisnd.h

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

