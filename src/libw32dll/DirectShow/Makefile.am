
CFLAGS = @CFLAGS@ $(X_CFLAGS) -fno-omit-frame-pointer \
	-Wmissing-prototypes -Wimplicit-function-declaration \
	-DWIN32_PATH=\"@w32_path@\" -DNOAVIFILE_HEADERS \
	-I$(srcdir)/.. -I$(srcdir)/../wine

DEBUG_CFLAGS = @DEBUG_CFLAGS@ $(X_CFLAGS) -fno-omit-frame-pointer \
	-Wmissing-prototypes -Wimplicit-function-declaration \
	-DWIN32_PATH=\\\"@w32_path@\\\" -DNOAVIFILE_HEADERS \
	-I$(srcdir)/.. -I$(srcdir)/../wine

if HAVE_W32DLL
ds_filter_lib = libds_filter.la
endif

noinst_LTLIBRARIES = $(ds_filter_lib)

libds_filter_la_SOURCES = guids.c inputpin.c outputpin.c allocator.c \
	cmediasample.c DS_Filter.c DS_AudioDecoder.c DS_VideoDecoder.c

noinst_HEADERS = DS_Filter.h allocator.h cmediasample.h \
	guids.h inputpin.h interfaces.h iunk.h outputpin.h \
	DS_AudioDecoder.h DS_VideoDecoder.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
