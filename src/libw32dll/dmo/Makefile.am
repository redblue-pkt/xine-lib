
AM_CFLAGS = $(X_CFLAGS) -fno-omit-frame-pointer \
	-Wmissing-prototypes -Wimplicit-function-declaration \
	-DWIN32_PATH=\"@w32_path@\" -DNOAVIFILE_HEADERS \
	-I$(srcdir)/.. -I$(srcdir)/../wine

#DEBUG_CFLAGS = @DEBUG_CFLAGS@ $(X_CFLAGS) -fno-omit-frame-pointer \
#	-Wmissing-prototypes -Wimplicit-function-declaration \
#	-DWIN32_PATH=\\\"@w32_path@\\\" -DNOAVIFILE_HEADERS \
#	-I$(srcdir)/.. -I$(srcdir)/../wine

if HAVE_W32DLL
dmo_filter_lib = libdmo_filter.la
endif

noinst_LTLIBRARIES = $(dmo_filter_lib)

libdmo_filter_la_SOURCES = \
	buffer.c \
	DMO_AudioDecoder.c \
	dmo.c \
	dmo_guids.c \
	DMO_VideoDecoder.c
 
noinst_HEADERS = \
	DMO_AudioDecoder.h \
	dmo_guids.h \
	dmo_interfaces.h \
	DMO_Filter.h \
	dmo.h \
	DMO_VideoDecoder.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
