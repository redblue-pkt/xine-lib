include $(top_srcdir)/misc/Makefile.common

SUBDIRS = zlib pthreads

noinst_HEADERS = dirent/dirent.h timer/timer.h

if WIN32
win32libs = libtimer.la libdirent.la
endif

noinst_LTLIBRARIES = $(win32libs)

libtimer_la_SOURCES = timer/timer.c

libdirent_la_SOURCES = dirent/dirent.c
