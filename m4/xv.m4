# AC_FIND_LIBXV_IMPL (LIB)
# -------------------------
#
AC_DEFUN([AC_PATH_LIBXV_IMPL],
[
  AC_MSG_CHECKING([for $1])
  if test -f "$xv_path/$1"; then
    AC_MSG_RESULT([found $1 in $xv_path])
    XV_LIB="$1"
  else
    AC_MSG_RESULT([$1 not found in $xv_path])
  fi
])

AC_DEFUN([AC_TEST_LIBXV],
[
  dnl -----------------------------------------------
  dnl   Testing installed Xv library
  dnl -----------------------------------------------
  AC_CHECK_LIB(Xv, XvShmCreateImage,
  [
     AC_DEFINE(HAVE_XV,
        1,
        [Define this if you have libXv installed])

     ac_have_xv="yes"
     case x$XV_LIB in
      x*.a)
        AC_DEFINE(HAVE_XV_STATIC,
                1,
                [Define this if you have libXv.a])
        ac_have_xv_static="yes"
        XV_LIB="$xv_path/$XV_LIB"
        ;;
      x*.so)
        XV_LIB=`echo $XV_LIB | sed 's/^lib/-l/; s/\.so$//'`
        ;;
      *)
        AC_MSG_ERROR([sorry, I don't know about $XV_LIB])
        ;;
     esac
    ],
     ,
  [$X_LIBS $X_PRE_LIBS -lXext $X_EXTRA_LIBS])

  AM_CONDITIONAL(HAVE_XV, test x$ac_have_xv = "xyes")

  dnl -----------------------------------------------
  dnl xine_check use Xv functions API.
  dnl -----------------------------------------------
  if test x$ac_have_xv = "xyes"; then
    EXTRA_X_LIBS="-L$xv_path $XV_LIB -lXext"
    EXTRA_X_CFLAGS=""
  fi
  AC_SUBST(XV_LIB)
  AC_SUBST(EXTRA_X_LIBS)
  AC_SUBST(EXTRA_X_CFLAGS)
])

# AC_PATH_LIBXV
# -------------------------
#
AC_DEFUN([AC_FIND_LIBXV],
[
  # Ensure that AC_PATH_XTRA is executed before this
  AC_REQUIRE([AC_PATH_XTRA])

  # Set xv_path if its not done already
  if test -z $xv_path; then
    xv_path=`echo $X_LIBS | sed -e 's/\-L\(.*\)/\1/'`
  fi

  if test "x$xv_prefer_shared" = "xyes"; then  
    AC_PATH_LIBXV_IMPL([libXv.so])
  else
    AC_PATH_LIBXV_IMPL([libXv.a])
  fi
  
  # Try the other lib if prefered failed
  if test -z $XV_LIB; then
    if ! test "x$xv_prefer_shared" = "xyes"; then  
      AC_PATH_LIBXV_IMPL([libXv.so])
    else
      AC_PATH_LIBXV_IMPL([libXv.a])
    fi
  fi

  if ! test -z $XV_LIB; then
    AC_TEST_LIBXV
  fi
])
