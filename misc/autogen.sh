#!/bin/sh
# Run this to generate all the initial makefiles, etc.
# This was lifted from the Gimp, and adapted slightly by
# Raph Levien, slightly hacked for xine by Daniel Caujolle-Bert.

DIE=0

PROG=xine-lib

(autoconf --version) < /dev/null > /dev/null 2>&1 || {
        echo
        echo "You must have autoconf installed to compile $PROG."
        echo "Download the appropriate package for your distribution,"
        echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
        DIE=1
}

(libtool --version) < /dev/null > /dev/null 2>&1 || {
        echo
        echo "You must have libtool installed to compile $PROG."
        echo "Get ftp://ftp.gnu.org/pub/gnu/libtool-1.2.tar.gz"
        echo "(or a newer version if it is available)"
        DIE=1
}

(automake --version) < /dev/null > /dev/null 2>&1 || {
        echo
        echo "You must have automake installed to compile $PROG."
        echo "Get ftp://ftp.gnu.org/pub/gnu/automake-1.3.tar.gz"
        echo "(or a newer version if it is available)"
        DIE=1
}

(aclocal --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "**Error**: Missing aclocal. The version of automake"
	echo "installed doesn't appear recent enough."
	echo "Get ftp://ftp.gnu.org/pub/gnu/automake-1.3.tar.gz"
	echo "(or a newer version if it is available)"
	DIE=1
}

if [ "$DIE" -eq 1 ]; then
        exit 1
fi

aclocalinclude="$ACLOCAL_FLAGS"; \
(echo -n " + Running aclocal: "; \
    aclocal $aclocalinclude; \
 echo "done.") && \
(echo -n " + Running libtoolize: "; \
    libtoolize --force; \
 echo "done.") && \
(echo -n " + Running autoheader: "; \
    autoheader; \
 echo "done.") && \
(echo -n " + Running automake: "; \
    automake --gnu --add-missing; \
 echo "done.") && \
(echo -n " + Running autoconf: "; \
    autoconf; \
 echo "done.")

rm -f config.cache

if [ x"$NO_CONFIGURE" = "x" ]; then
    echo " + Running 'configure $@':"
    if [ -z "$*" ]; then
	echo "   ** If you wish to pass arguments to ./configure, please"
        echo "   ** specify them on the command line."
    fi
    ./configure "$@" && \
    echo "Now type 'make' to compile $PKG_NAME" || exit 1
fi
