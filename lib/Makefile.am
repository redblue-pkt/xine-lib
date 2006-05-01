include $(top_srcdir)/misc/Makefile.common

if GENERATED_INTTYPES_H
inttypes_h = inttypes.h
endif

EXTRA_DIST = Makefile.common $(inttypes_h)

noinst_LTLIBRARIES = libxineposix.la
noinst_HEADERS = os_internal.h
include_HEADERS = os_types.h

libxineposix_la_SOURCES = 
libxineposix_la_LIBADD = @LTLIBOBJS@
