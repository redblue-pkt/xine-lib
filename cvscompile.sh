#!/bin/sh
# Run this to generate all the initial Makefiles, etc.

## extract automake version
automake_1_6x=no
AM="`automake --version | sed -n 1p | sed -e 's/[a-zA-Z\ \.\(\)\-]//g'`"
if test $AM -lt 100 ; then
  AM=`expr $AM \* 10`
fi
if [ `expr $AM` -ge 160 ]; then
    automake_1_6x=yes
fi
if [ -z "$NO_AUTOCONF_CHECK" ]; then
  if test x"$automake_1_6x" = x"no"; then
	  echo "To compile xine-lib from CVS requires automake >= 1.6"
	  exit
  fi
fi

## extract autoconf version
autoconf_2_53=no
AC="`autoconf --version | sed -n 1p | sed -e 's/[a-zA-Z\ \.\(\)\-]//g'`"
if test $AC -lt 100 ; then
  AC=`expr $AC \* 10`
fi
if [ `expr $AC` -ge 253 ]; then
    autoconf_2_53=yes
fi
if test x"$autoconf_2_53" = x"no"; then
	echo "To compile xine-lib from CVS requires autoconf >= 2.53"
	exit
fi

rm -f config.cache

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

(test -f $srcdir/configure.ac) || {
    echo -n "*** Error ***: Directory "\`$srcdir\'" does not look like the"
    echo " top-level directory"
    exit 1
}

. $srcdir/misc/autogen.sh

