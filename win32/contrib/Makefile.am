include $(top_srcdir)/misc/Makefile.common

SUBDIRS = zlib pthreads

noinst_HEADERS = dirent/dirent.h timer/timer.h

if !HAVE_SETENV
libsetenv = libsetenv.la
endif

if WIN32
noinst_LTLIBRARIES = $(libsetenv) libtimer.la libdirent.la
endif

libsetenv_la_SOURCES = setenv.c

libtimer_la_SOURCES = timer/timer.c

libdirent_la_SOURCES = dirent/dirent.c

EXTRA_DIST = \
	pthreads/FAQ \
	pthreads/NEWS \
	pthreads/TODO \
	pthreads/barrier.c \
	pthreads/build.tar.gz \
	pthreads/cleanup.c \
	pthreads/pthread.h \
	pthreads/private.c \
	pthreads/CONTRIBUTORS \
	pthreads/dll.c \
	pthreads/README.CV \
	pthreads/tsd.c \
	pthreads/COPYING.LIB \
	pthreads/signal.c \
	pthreads/README \
	pthreads/condvar.c \
	pthreads/sched.c \
	pthreads/sched.h \
	pthreads/WinCE-PORT \
	pthreads/semaphore.c \
	pthreads/semaphore.h \
	pthreads/README.NONPORTABLE \
	pthreads/PROGRESS \
	pthreads/errno.c \
	pthreads/need_errno.h \
	pthreads/global.c \
	pthreads/cancel.c \
	pthreads/attr.c \
	pthreads/config.h \
	pthreads/nonportable.c \
	pthreads/exit.c \
	pthreads/fork.c \
	pthreads/README.WinCE \
	pthreads/config.h.in \
	pthreads/mutex.c \
	pthreads/misc.c \
	pthreads/pthread.def \
	pthreads/pthread.dsp \
	pthreads/pthread.dsw \
	pthreads/pthread.ncb \
	pthreads/pthread.opt \
	pthreads/spin.c \
	pthreads/sync.c \
	pthreads/rwlock.c \
	pthreads/ANNOUNCE \
	pthreads/MAINTAINERS \
	pthreads/create.c \
	pthreads/ChangeLog \
	pthreads/implement.h \
	pthreads/acconfig.h
