dnl
dnl Search the freetype library.
dnl
dnl AM_PATH_FREETYPE2()
dnl

AC_DEFUN([AM_PATH_FREETYPE2], [

  AC_ARG_ENABLE(freetype,
    AC_HELP_STRING([--disable-freetype], [disable freetype2 support]),
    [enable_freetype=$enableval],
    [enable_freetype=yes]
  )

  if test x"$enable_freetype" = "xyes"; then
    AC_PATH_TOOL(FREETYPE_CONFIG, freetype-config, no)
    if test "$FREETYPE_CONFIG" = "no" ; then
      AC_MSG_RESULT([*** freetype-config not found, freetype2 support disabled **
  ])
    else
      FT2_CFLAGS=`$FREETYPE_CONFIG --cflags`
      FT2_LIBS=`$FREETYPE_CONFIG --libs`
      have_ft2="yes"
      AC_DEFINE(HAVE_FT2,1,[Define this if you have freetype2 library])
    fi
  else
    AC_MSG_RESULT([*** freetype2 support disabled ***])
  fi

  dnl AM_CONDITIONAL(HAVE_FT2, test x"$have_ft2" = "xyes" )
  AC_SUBST(FT2_CFLAGS)
  AC_SUBST(FT2_LIBS)

])
