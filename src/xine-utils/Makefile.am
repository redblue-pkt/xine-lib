include $(top_srcdir)/misc/Makefile.common

LIBTOOL = $(SHELL) $(top_builddir)/libtool
noinst_LTLIBRARIES = libxineutils.la

EXTRA_DIST = ppcasm_string.S ppc_asm.tmpl

if PPC_ARCH
pppc_files = ppcasm_string.S 
endif

if WIN32
TIMER_LIB = $(top_builddir)/win32/contrib/libtimer.la
else
#lib_thr = $(THREAD_LIBS)
endif

AM_CFLAGS = $(X_CFLAGS)

libxineutils_la_SOURCES = $(pppc_files) \
	cpu_accel.c \
	color.c \
	copy.c \
	list.c \
	memcpy.c \
	monitor.c \
	utils.c \
	xine_check.c \
	xine_mutex.c \
	xmllexer.c \
	xine_buffer.c 

libxineutils_la_DEPENDENCIES = $(TIMER_LIB)
libxineutils_la_LIBADD = $(lib_thr) $(TIMER_LIB)

include_HEADERS = \
	attributes.h \
	compat.h \
	xine_buffer.h \
	xineutils.h \
	xmllexer.h \
	xmlparser.h

noinst_HEADERS = ppcasm_string.h xine_check.h

$(TIMER_LIB):
	$(MAKE) -C $(top_builddir)/win32/contrib libtimer.la
