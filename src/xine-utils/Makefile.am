include $(top_srcdir)/misc/Makefile.common

LIBTOOL = $(SHELL) $(top_builddir)/libtool
noinst_LTLIBRARIES = libxineutils.la

EXTRA_DIST = ppcasm_string.S ppc_asm.tmpl

if PPC_ARCH
pppc_files = ppcasm_string.S 
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

include_HEADERS = \
	attributes.h \
	compat.h \
	xine_buffer.h \
	xineutils.h \
	xmllexer.h \
	xmlparser.h

noinst_HEADERS = ppcasm_string.h xine_check.h

