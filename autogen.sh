#!/bin/sh
# run this to generate all the initial makefiles, etc.

PROG=gnome-xine

# Check how echo works in this /bin/sh
case `echo -n` in
-n)     _echo_n=   _echo_c='\c';;
*)      _echo_n=-n _echo_c=;;
esac

detect_configure_ac() {

  srcdir=`dirname $0`
  test -z "$srcdir" && srcdir=.

  (test -f $srcdir/configure.ac) || {
    echo $_echo_n "*** Error ***: Directory "\`$srcdir\`" does not look like the"
    echo " top-level directory"
    exit 1
  }
}


#--------------------
# AUTOCONF
#-------------------
detect_autoconf() {
  (autoconf --version) < /dev/null > /dev/null 2>&1 || {
    echo
    echo "**Error**: You must have \`autoconf' installed to compile gxine."
    echo "Download the appropriate package for your distribution,"
    echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
    exit 1
  }
}

run_autoheader () {
  echo $_echo_n " + Running autoheader: $_echo_c";
    autoheader;
  echo "done."
}

run_autoconf () {
  echo $_echo_n " + Running autoconf: $_echo_c";
    autoconf;
  echo "done."
}

#--------------------
# LIBTOOL
#-------------------
detect_libtool() {
  (libtool --version) < /dev/null > /dev/null 2>&1 || {
    echo
    echo "**Error**: You must have \`libtool' installed to compile gxine."
    echo "Get ftp://ftp.gnu.org/pub/gnu/libtool-1.4.tar.gz"
    echo "(or a newer version if it is available)"
    exit 1
  }
}

run_libtoolize() {
  echo $_echo_n " + Running libtoolize: $_echo_c";
    libtoolize --force --copy >/dev/null 2>&1;
  echo "done."
}

#--------------------
# AUTOMAKE
#--------------------
detect_automake() {
  if [ -f `which automake-1.6` ]; then
    automake_1_6x=yes
  else
    if [ -f `which automake` ]; then
      AM="`automake --version | sed -n 1p | sed -e 's/[a-zA-Z\ \.\(\)\-]//g'`"
      if test $AM -lt 100 ; then
        AM=`expr $AM \* 10`
      fi
      if [ `expr $AM` -ge 160 ]; then
        automake_1_6x=yes
      fi
    else
      echo
      echo "You must have automake installed to compile $PROG."
      echo "Get ftp://ftp.gnu.org/pub/gnu/automake-1.6.tar.gz"
      echo "(or a newer version if it is available)"
      exit 1
    fi
  fi
}

run_automake () {
  if test x"$automake_1_6x" = x"no"; then
    echo "Warning: automake < 1.6. Some warning message might occur from automake"
    echo
  fi

  echo $_echo_n " + Running automake: $_echo_c";

  if test x"$automake_1_6x" = "xyes"; then
    automake-1.6 --gnu --add-missing --copy;
  else
    automake --gnu --add-missing --copy;
  fi
  echo "done."
}

#--------------------
# ACLOCAL
#-------------------
detect_aclocal() {

  # if no automake, don't bother testing for aclocal
  if [ -f `which aclocal-1.6` ]; then
    aclocal_1_6x=yes
  else
    if [ -f `which aclocal` ]; then
      AC="`aclocal --version | sed -n 1p | sed -e 's/[a-zA-Z\ \.\(\)\-]//g'`"
      if test $AC -lt 100 ; then
        AC=`expr $AC \* 10`
      fi
      if [ `expr $AC` -ge 160 ]; then
        aclocal_1_6x=yes
      fi
    else
      echo
      echo "**Error**: Missing \`aclocal'.  The version of \`automake'"
      echo "installed doesn't appear recent enough."
      echo "Get ftp://ftp.gnu.org/pub/gnu/automake-1.3.tar.gz"
      echo "(or a newer version if it is available)"
      exit 1
    fi
  fi
}

run_aclocal () {
  echo $_echo_n " + Running aclocal: $_echo_c"

  if test x"$aclocal_1_6x" = x"yes"; then
    aclocal-1.6 $aclocalinclude -I m4
  else
    aclocal $aclocalinclude -I m4
  fi
  echo "done." 
}

#--------------------
# CONFIGURE
#-------------------
run_configure () {
  rm -f config.cache
  echo " + Running 'configure $@':"
  if [ -z "$*" ]; then
    echo "   ** If you wish to pass arguments to ./configure, please"
    echo "   ** specify them on the command line."
  fi
  ./configure "$@" 
}


#---------------
# MAIN
#---------------
detect_configure_ac
detect_autoconf
detect_libtool
detect_automake
detect_aclocal


#   help: print out usage message
#   *) run aclocal, autoheader, automake, autoconf, configure
case "$1" in
  aclocal)
    run_aclocal
    ;;
  autoheader)
    run_autoheader
    ;;
  automake)
    run_automake
    ;;
  autoconf)
    run_aclocal
    run_autoconf
    ;;
  libtoolize)
    run_libtoolize
    ;;
  noconfig)
    run_aclocal
    run_libtoolize
    run_autoheader
    run_automake
    run_autoconf
    ;;
  *)
    run_aclocal
    run_libtoolize
    run_autoheader
    run_automake
    run_autoconf
    run_configure $@
    ;;
esac
