# Configure paths for libxvidcore
# Tomas Kovar <tomask@mac.com> 03-31-2002
# Stolen from ogg.m4

dnl AM_PATH_XVID([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libxvidcore, and define XVID_CFLAGS and XVID_LIBS
dnl
AC_DEFUN([AM_PATH_XVID],
[dnl 
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(xvid-prefix,[  --with-xvid-prefix=PFX   Prefix where XviD is installed (optional)], xvid_prefix="$withval", xvid_prefix="")
AC_ARG_ENABLE(xvidtest, [  --disable-xvidtest       Do not try to compile and run a test XviD program],, enable_xvidtest=yes)

  if test x$xvid_prefix != x ; then
    xvid_args="$xvid_args --prefix=$xvid_prefix"
    XVID_CFLAGS="-I$xvid_prefix/include"
    XVID_LIBS="-L$xvid_prefix/lib"
  fi

  XVID_LIBS="$XVID_LIBS -lxvidcore"

  AC_MSG_CHECKING(for XviD)
  no_xvid=""


  if test "x$enable_xvidtest" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $XVID_CFLAGS"
    LIBS="$LIBS $XVID_LIBS"
dnl
dnl Now check if the installed XviD is sufficiently new.
dnl
      rm -f conf.xvidtest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xvid.h>

int main ()
{
  XVID_INIT_PARAM xinit;
  system("touch conf.xvidtest");

  xinit.cpu_flags = 0;
  xvid_init(NULL, 0, &xinit, NULL);

  if (xinit.api_version == API_VERSION) {
    return 0;
  } else {
    printf ("Header file and library are out of sync. Header file supports\n"
	    "version %d.%d API and shared library supports version %d.%d API.\n",
	    API_VERSION >> 16, API_VERSION & 0xFFFF,
	    xinit.api_version >> 16, xinit.api_version & 0xFFFF);
    return 1;
  }
}

],, no_xvid=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_xvid" = x ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])     
  else
     AC_MSG_RESULT(no)
     if test -f conf.xvidtest ; then
       :
     else
       echo "*** Could not run XviD test program, checking why..."
       CFLAGS="$CFLAGS $XVID_CFLAGS"
       LIBS="$LIBS $XVID_LIBS"
       AC_TRY_LINK([
#include <stdio.h>
#include <xvid.h>
],     [ return 0; ],
       [ echo "*** The test program compiled, but did not run. This usually means"
       echo "*** that the run-time linker is not finding XviD or finding the wrong"
       echo "*** version of XviD. If it is not finding XviD, you'll need to set your"
       echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
       echo "*** to the installed location  Also, make sure you have run ldconfig if that"
       echo "*** is required on your system"
       echo "***"
       echo "*** If you have an old version installed, it is best to remove it, although"
       echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
       [ echo "*** The test program failed to compile or link. See the file config.log for the"
       echo "*** exact error that occured. This usually means XviD was incorrectly installed." ])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
     XVID_CFLAGS=""
     XVID_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(XVID_CFLAGS)
  AC_SUBST(XVID_LIBS)
  rm -f conf.xvidtest
])
