#!/bin/sh
# Run this to generate all the initial Makefiles, etc.

m4_files="_xine.m4 alsa.m4 esd.m4"
if test -d m4; then
    rm -f acinclude.m4
    for m4f in $m4_files; do
	cat m4/$m4f >> acinclude.m4
    done
else
    echo "Directory 'm4' is missing."
    exit 1
fi

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

(test -f $srcdir/configure.in) || {
    echo -n "*** Error ***: Directory "\`$srcdir\'" does not look like the"
    echo " top-level directory"
    exit 1
}

. $srcdir/misc/autogen.sh

