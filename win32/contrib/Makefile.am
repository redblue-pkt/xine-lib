include $(top_srcdir)/misc/Makefile.common

SUBDIRS = zlib pthreads

noinst_HEADERS = dirent/dirent.h timer/timer.h

if WIN32
if !HAVE_SETENV
libsetenv = libsetenv.la
endif
win32libs = libtimer.la libdirent.la
endif

noinst_LTLIBRARIES = $(libsetenv) $(win32libs)

libsetenv_la_SOURCES = setenv.c

libtimer_la_SOURCES = timer/timer.c

libdirent_la_SOURCES = dirent/dirent.c
