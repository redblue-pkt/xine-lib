dnl Configure paths/version for IRIX AL
dnl
AC_DEFUN(AM_PATH_IRIXAL,
 [
dnl replace by test
  AC_ARG_ENABLE(irixal, [  --enable-irixal         Turn on IRIX AL audio support.], enable_irixal=yes, enable_irixal=no)

  AC_ARG_WITH(irixal-prefix,[  --irixal-prefix=pfx     Prefix where al is installed (optional)],
              irixal_prefix="$withval", irixal_prefix="")

  AC_MSG_CHECKING([for IRIX AL support])
  if test "x$enable_irixal" = xyes ; then

    if test x$irixal_prefix != x ; then
      IRIXAL_LIBS="-L$al_prefix/lib"
      IRIXAL_STATIC_LIB="$al_prefix"
      IRIXAL_CFLAGS="-I$al_prefix/include"
    fi

    IRIXAL_LIBS="-laudio $IRIXAL_LIBS"
    if test x$IRIXAL_STATIC_LIB != x; then
      IRIXAL_STATIC_LIB="$IRIXAL_STATIC_LIB/lib/libaudio.a"
    else
      IRIXAL_STATIC_LIB="/usr/lib/libaudio.a"
    fi
  fi

  AC_MSG_RESULT($enable_irixal)
  if test "x$enable_irixal" = xyes ; then
    ifelse([$2], , :, [$2])
  else
    ifelse([$3], , :, [$3])
  fi

  AC_SUBST(IRIXAL_CFLAGS)
  AC_SUBST(IRIXAL_STATIC_LIB)
  AC_SUBST(IRIXAL_LIBS)

])

