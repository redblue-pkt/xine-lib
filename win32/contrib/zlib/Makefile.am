include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = inffixed.h ChangeLog FAQ INDEX README example.c maketree.c \
             minigzip.c zlib.3 algorithm.txt build.tar.gz nt/Makefile.emx \
             nt/Makefile.gcc nt/Makefile.nt nt/zlib.dnt

if !HAVE_ZLIB
link_zlib = libzlib.la
endif

noinst_LTLIBRARIES = $(link_zlib)

libzlib_la_SOURCES = adler32.c compress.c crc32.c deflate.c gzio.c infblock.c \
                  infcodes.c inffast.c inflate.c inftrees.c infutil.c \
                  trees.c uncompr.c zutil.c

noinst_HEADERS = deflate.h infblock.h infcodes.h inffast.h inftrees.h \
                 infutil.h trees.h zconf.h zlib.h zutil.h
