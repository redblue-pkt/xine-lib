#!/bin/sh
# Run this to generate all the initial Makefiles, etc.

## extract automake version
automake_1_5x=no
AM="`automake --version | sed -n 1p | sed -e 's/[a-zA-Z\ \.\(\)\-]//g'`"
if test $AM -lt 100 ; then
  AM=`expr $AM \* 10`
fi
if [ `expr $AM` -ge 150 ]; then
    automake_1_5x=yes
fi

rm -f config.cache

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

m4_files="_xine.m4 arts.m4 esd.m4 iconv.m4 lcmessage.m4 vorbis.m4 aa.m4 gettext.m4 irixal.m4 ogg.m4 alsa.m4 codeset.m4 glibc21.m4 isc-posix.m4 progtest.m4 sdl.m4 xvid.m4 libfame.m4 dvdnav.m4"
if test -d $srcdir/m4; then
    rm -f acinclude.m4
    for m4f in $m4_files; do
	cat $srcdir/m4/$m4f >> acinclude.m4
    done
    ## automake 1.5x implement AM_PROG_AS, not older ones, so add it.
    if test x"$automake_1_5x" = x"no"; then
	cat $srcdir/m4/as.m4 >> acinclude.m4
    fi
else
    echo "Directory 'm4' is missing."
    exit 1
fi

(test -f $srcdir/configure.in) || {
    echo -n "*** Error ***: Directory "\`$srcdir\'" does not look like the"
    echo " top-level directory"
    exit 1
}

. $srcdir/misc/autogen.sh

