CFLAGS = @BUILD_LIB_STATIC@ @GLOBAL_CFLAGS@ 

EXTRA_DIST = main.c

noinst_LTLIBRARIES = libmpg123.la

libmpg123_la_SOURCES = common.c decode_i386.c layer1.c layer3.c tabinit.c \
	dct64_i386.c interface.c layer2.c
libmpg123_la_LIBADD = -lm


noinst_HEADERS = huffman.h mpg123.h mpglib.h l2tables.h

debug:
	$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
