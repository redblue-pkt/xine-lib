CFLAGS = @BUILD_LIB_STATIC@ @LIBMPEG2_CFLAGS@ @GLOBAL_CFLAGS@

EXTRA_DIST = idct_mlib.c idct_mlib.h motion_comp_mlib.c

noinst_LTLIBRARIES = libmpeg2.la

#libmpeg2_la_SOURCES = slice.c header.c stats.c idct.c motion_comp.c\
#	decode.c  idct_mmx.c motion_comp_mmx.c
libmpeg2_la_SOURCES = slice.c header.c stats.c idct.c motion_comp.c\
	decode.c idct_mmx.c motion_comp_mmx.c

noinst_HEADERS = vlc.h mpeg2.h mpeg2_internal.h 

debug:
	$(MAKE) CFLAGS="$(DEBUG_CFLAGS) @BUILD_LIB_STATIC@"

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
