include $(top_srcdir)/misc/Makefile.common

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
