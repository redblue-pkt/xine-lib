include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(LIBFFMPEG_CFLAGS)
ASFLAGS =

noinst_LTLIBRARIES = libpostprocess.la

EXTRA_DIST = postprocess_template.c

libpostprocess_la_SOURCES = postprocess.c

noinst_HEADERS = mangle.h postprocess.h postprocess_internal.h

.s.lo:
	$(ASCOMPILE) -o $@ `test -f $< || echo '$(srcdir)/'`$<
