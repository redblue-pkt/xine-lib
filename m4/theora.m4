# Configure paths for libtheora
# Andreas Heinchen <andreas.heinchen@gmx.de> 04-18-2003
# Shamelessly adapted from Jack Moffitt's version for libvorbis
# who had stolen it from Owen Taylor and Manish Singh

dnl AM_PATH_THEORA([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libtheora, and define THEORA_CFLAGS and THEORA_LIBS
dnl
AC_DEFUN([AM_PATH_THEORA],
[dnl 
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(theora-prefix,[  --with-theora-prefix=PFX   Prefix where libtheora is installed (optional)], theora_prefix="$withval", theora_prefix="")
AC_ARG_ENABLE(theoratest, [  --disable-theoratest       Do not try to compile and run a test Vorbis program],, enable_theoratest=yes)

  if test x$theora_prefix != x ; then
    theora_args="$theora_args --prefix=$theora_prefix"
    THEORA_CFLAGS="-I$theora_prefix/include"
    THEORA_LIBDIR="-L$theora_prefix/lib"
  fi

  THEORA_LIBS="$THEORA_LIBDIR -ltheora -lm"

  AC_MSG_CHECKING(for Theora)
  no_theora=""


  if test "x$enable_theoratest" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $THEORA_CFLAGS"
    echo "these are your cflags $CFLAGS"
    LIBS="$LIBS $THEORA_LIBS $OGG_LIBS"
dnl
dnl Now check if the installed Theora is sufficiently new.
dnl
      rm -f conf.theoratest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <theora/theora.h>

int main ()
{
  system("touch conf.theoratest");
  return 0;
}

],, no_theora=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_theora" = x ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])     
  else
     AC_MSG_RESULT(no)
     if test -f conf.theoratest ; then
       :
     else
       echo "*** Could not run Theora test program, checking why..."
       CFLAGS="$CFLAGS $THEORA_CFLAGS"
       LIBS="$LIBS $THEORA_LIBS $OGG_LIBS"
       AC_TRY_LINK([
#include <stdio.h>
#include <theora/theora.h>
],     [ return 0; ],
       [ echo "*** The test program compiled, but did not run. This usually means"
       echo "*** that the run-time linker is not finding Theora or finding the wrong"
       echo "*** version of Theora. If it is not finding Theora, you'll need to set your"
       echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
       echo "*** to the installed location  Also, make sure you have run ldconfig if that"
       echo "*** is required on your system"
       echo "***"
       echo "*** If you have an old version installed, it is best to remove it, although"
       echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
       [ echo "*** The test program failed to compile or link. See the file config.log for the"
       echo "*** exact error that occured. This usually means Theora was incorrectly installed"
       echo "*** or that you have moved Theora since it was installed." ])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
     THEORA_CFLAGS=""
     THEORA_LIBS=""
     THEORAFILE_LIBS=""
     THEORAENC_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(THEORA_CFLAGS)
  AC_SUBST(THEORA_LIBS)
  AC_SUBST(THEORAFILE_LIBS)
  AC_SUBST(THEORAENC_LIBS)
  rm -f conf.theoratest
])
