include $(top_srcdir)/misc/Makefile.common

noinst_LTLIBRARIES = libxineutils.la

EXTRA_DIST = ppcasm_string.S ppc_asm.tmpl

if PPC_ARCH
pppc_files = ppcasm_string.S 
endif

if HAVE_XV_STATIC
xv_libs = $(XV_LIB)
endif

AM_CFLAGS = $(THREAD_CFLAGS) $(X_CFLAGS) @ANSI_FLAGS@

libxineutils_la_SOURCES = $(pppc_files) \
	cpu_accel.c \
	color.c \
	list.c \
	memcpy.c \
	monitor.c \
	utils.c \
	xine_check.c \
	xine_mutex.c \
	xmllexer.c \
	xmlparser.c \
	xine_buffer.c 

libxineutils_la_LIBADD = $(THREAD_LIBS) $(xv_libs)

include_HEADERS = attributes.h \
	compat.h \
	ppcasm_string.h \
	xineutils.h \
	xine_check.h \
	xmllexer.h \
	xmlparser.h \
	xine_buffer.h
