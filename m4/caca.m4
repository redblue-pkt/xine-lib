dnl Configure paths and dependencies for libcaca.
dnl
dnl Jeffrey S Smith <whydoubt@yahoo.com> 09-Dec-2003
dnl based on aa.m4 as found in xinelib
dnl  
dnl AM_PATH_CACA([MINIMUM-VERSION, [ACTION-IF-FOUND [,ACTION-IF-NOT-FOUND ]]])
dnl Test for CACA, and define CACA_CFLAGS, CACA_LIBS.
dnl
dnl ***********************
dnl 09-Dec-2003
dnl   * new m4 for libcaca
dnl
AC_DEFUN([AM_PATH_CACA],
[dnl 
dnl
AC_ARG_WITH(caca-prefix,
    [  --with-caca-prefix=PFX  Prefix where CACA is installed (optional)],
            caca_config_prefix="$withval", caca_config_prefix="")
AC_ARG_WITH(caca-exec-prefix,
    [  --with-caca-exec-prefix=PFX                                                                             Exec prefix where CACA is installed (optional)],
            caca_config_exec_prefix="$withval", caca_config_exec_prefix="")
AC_ARG_ENABLE(cacatest, 
    [  --disable-cacatest      Do not try to compile and run a test CACA program],, enable_cacatest=yes)

  if test x$caca_config_exec_prefix != x ; then
     caca_config_args="$caca_config_args --exec-prefix=$caca_config_exec_prefix"
     if test x${CACA_CONFIG+set} != xset ; then
        CACA_CONFIG=$caca_config_exec_prefix/bin/caca-config
     fi
  fi
  if test x$caca_config_prefix != x ; then
     caca_config_args="$caca_config_args --prefix=$caca_config_prefix"
     if test x${CACA_CONFIG+set} != xset ; then
        CACA_CONFIG=$caca_config_prefix/bin/caca-config
     fi
  fi

  min_caca_version=ifelse([$1], ,0.3,$1)

  if test x"$enable_cacatest" != "xyes"; then
    AC_MSG_CHECKING([for CACA version >= $min_caca_version])
  else
    if test ! -x "$CACA_CONFIG"; then
      CACA_CONFIG=""
    fi
    AC_PATH_PROG(CACA_CONFIG, caca-config, no)

    if test "$CACA_CONFIG" = "no" ; then
dnl
dnl caca-config is missing
dnl
      no_caca=yes
    else
      AC_MSG_CHECKING([for CACA version >= $min_caca_version])
      no_caca=""
      CACA_CFLAGS=`$CACA_CONFIG $caca_config_args --cflags`
      CACA_LIBS=`$CACA_CONFIG $caca_config_args --plugin-libs`
      caca_major_version=`$CACA_CONFIG $caca_config_args --version | \
             sed 's/\([[0-9]]*\).\([[0-9]]*\)/\1/'`
      caca_minor_version=`$CACA_CONFIG $caca_config_args --version | \
             sed 's/\([[0-9]]*\).\([[0-9]]*\)/\2/'`

      ac_save_CFLAGS="$CFLAGS"
      ac_save_LIBS="$LIBS"
      CFLAGS="$CFLAGS $CACA_CFLAGS"
      LIBS="$CACA_LIBS $LIBS"
dnl
dnl Now check if the installed CACA is sufficiently new. (Also sanity
dnl checks the results of caca-config to some extent)
dnl
      AC_LANG_SAVE()
      AC_LANG_C()
      rm -f conf.cacatest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <caca.h>

int main ()
{
  int major, minor;
  char *tmp_version;

  system("touch conf.cacatest");

  tmp_version = (char *) strdup("$min_caca_version");
  if (sscanf(tmp_version, "%d.%d", &major, &minor) != 2) {
     printf("%s, bad version string\n", "$min_caca_version");
     exit(1);
   }

   if (($caca_major_version > major) ||
      (($caca_major_version == major) && ($caca_minor_version >= minor)))
    {
      return 0;
    }
  else
    {
      printf("\n*** 'caca-config --version' returned %d.%d, but the minimum version\n", $caca_major_version, $caca_minor_version);
      printf("*** of CACA required is %d.%d. If caca-config is correct, then it is\n", major, minor);
      printf("*** best to upgrade to the required version.\n");
      printf("*** If caca-config was wrong, set the environment variable CACA_CONFIG\n");
      printf("*** to point to the correct copy of caca-config, and remove the file\n");
      printf("*** config.cache before re-running configure\n");
      return 1;
    }
}

],, no_caca=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])

      CFLAGS="$ac_save_CFLAGS"
      LIBS="$ac_save_LIBS"
    fi
  fi dnl CACA_CONFIG

  if test "x$no_caca" = x; then
    AC_MSG_RESULT(yes)
    ifelse([$2], , :, [$2])     
  else
    AC_MSG_RESULT(no)
    if test "$CACA_CONFIG" = "no"; then
      echo "*** The caca-config program installed by CACA could not be found"
      echo "*** If CACA was installed in PREFIX, make sure PREFIX/bin is in"
      echo "*** your path, or use --with-caca-prefix to set the prefix"
      echo "*** where CACA is installed."
    else
      if test -f conf.cacatest ; then
        :
      else
        echo "*** Could not run CACA test program, checking why..."
        CFLAGS="$CFLAGS $CACA_CFLAGS"
        LIBS="$LIBS $CACA_LIBS"
        AC_TRY_LINK([
#include <stdio.h>
#include <caca.h>
],      [ return 0; ],
        [ echo "*** The test program compiled, but did not run. This usually means"
          echo "*** that the run-time linker is not finding CACA or finding the wrong"
          echo "*** version of CACA. If it is not finding CACA, you'll need to set your"
          echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
          echo "*** to the installed location  Also, make sure you have run ldconfig if that"
          echo "*** is required on your system"
          echo "***"
          echo "*** If you have an old version installed, it is best to remove it, although"
          echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"
          echo "***"],
        [ echo "*** The test program failed to compile or link. See the file config.log for the"
          echo "*** exact error that occured. This usually means CACA was incorrectly installed"
          echo "*** or that you have moved CACA since it was installed." ])
          CFLAGS="$ac_save_CFLAGS"
          LIBS="$ac_save_LIBS"
      fi
    fi
    CACA_CFLAGS=""
    CACA_LIBS=""
    ifelse([$3], , :, [$3])
  fi
  AC_SUBST(CACA_CFLAGS)
  AC_SUBST(CACA_LIBS)
  AC_LANG_RESTORE()
  rm -f conf.cacatest
])
