include $(top_srcdir)/misc/Makefile.common

AM_CPPFLAGS = -D_LARGEFILE64_SOURCE -I$(srcdir)/../librtsp

noinst_LTLIBRARIES = libreal.la

libreal_la_SOURCES = \
  real.c \
  asmrp.c \
  rmff.c \
  sdpplin.c

libreal_la_LDFLAGS = $(THREAD_LIBS) -avoid-version -module

noinst_HEADERS = \
  real.h \
  asmrp.h \
  rmff.h \
  sdpplin.h
