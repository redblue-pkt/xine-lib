include $(top_srcdir)/misc/Makefile.common

SUBDIRS = zlib pthreads

noinst_HEADERS = dirent/dirent.h

if WIN32
win32libs = libdirent.la
endif

noinst_LTLIBRARIES = $(win32libs)

libdirent_la_SOURCES = dirent/dirent.c
