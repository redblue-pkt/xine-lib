LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

AM_CPPFLAGS = -D_WIN32_WINNT=0x400 -DHAVE_CONFIG_H -DPTW32_BUILD -D__CLEANUP_C

EXTRA_DIST = acconfig.h ANNOUNCE CONTRIBUTORS COPYING.LIB ChangeLog FAQ MAINTAINERS NEWS PROGRESS README README.CV README.NONPORTABLE README.WinCE TODO WinCE-PORT build.tar.gz config.h.in pthread.def pthread.dsp pthread.dsw

if WIN32
noinst_LTLIBRARIES = libpthread.la
include_HEADERS = pthread.h sched.h
noinst_HEADERS = config.h implement.h need_errno.h semaphore.h
else
noinst_HEADERS = config.h implement.h need_errno.h semaphore.h pthread.h sched.h
endif

libpthread_la_SOURCES = attr.c barrier.c cancel.c cleanup.c condvar.c create.c dll.c errno.c exit.c fork.c global.c misc.c mutex.c nonportable.c private.c rwlock.c sched.c semaphore.c signal.c spin.c sync.c tsd.c

pass1:
	@$(MAKE) MULTIPASS_CFLAGS="$(PASS1_CFLAGS)"

pass2:
	@$(MAKE) MULTIPASS_CFLAGS="$(PASS2_CFLAGS)"

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-data-hook:

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit; \
	done;
	$(MAKE) $(AM_MAKEFLAGS) install-data-hook

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
