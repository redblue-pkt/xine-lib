include $(top_srcdir)/misc/Makefile.common

RTSP_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -I$(srcdir)/../libreal

AM_CFLAGS = $(RTSP_CFLAGS) @ANSI_FLAGS@

noinst_LTLIBRARIES = librtsp.la

librtsp_la_SOURCES = \
  rtsp.c \
  rtsp_session.c

librtsp_la_LDFLAGS = $(THREAD_LIBS) -avoid-version -module

noinst_HEADERS = \
  rtsp.h \
  rtsp_session.h
