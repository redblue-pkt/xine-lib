dnl
dnl Configure.ac for xine
dnl

AC_INIT
AC_CONFIG_SRCDIR([src/xine-engine/xine.c])
AC_CONFIG_LIBOBJ_DIR([lib])

dnl
dnl Require autoconf version 2.53
dnl
AC_PREREQ(2.53)

dnl Making releases:
dnl   XINE_SUB += 1; continue with XINE_LT_* values below
dnl
XINE_MAJOR=1
XINE_MINOR=1
XINE_SUB=2

#if test $XINE_SUB -eq 0 ; then
#  XINE_SUBPART="";
#else
  XINE_SUBPART=".$XINE_SUB"
#fi

dnl The libtool version numbers (XINE_LT_*); Don't even think about faking this!
dnl 
dnl immediately before every release do:
dnl ===================================
dnl if (the interface is totally unchanged from previous release)
dnl    XINE_LT_REVISION ++;
dnl else { /* interfaces have been added, removed or changed */
dnl    XINE_LT_REVISION = 0;
dnl    XINE_LT_CURRENT ++;
dnl    if (any interfaces have been _added_ since last release)
dnl       XINE_LT_AGE ++;
dnl    if (any interfaces have been _removed_ or _incompatibly changed_)
dnl       XINE_LT_AGE = 0;
dnl }
dnl 
dnl If you want to know more about what you are doing, here are some details:
dnl  * XINE_LT_CURRENT is the current API version
dnl  * XINE_LT_REVISION is an internal revision number which is increased when the API
dnl    itself did not change
dnl  * XINE_LT_AGE is the number of previous API versions still supported by this library
dnl  * libtool has its own numbering scheme, because local library numbering schemes
dnl    are platform dependent
dnl  * in Linux, the library will be named
dnl    libname.so.(XINE_LT_CURRENT - XINE_LT_AGE).XINE_LT_AGE.XINE_LT_REVISION

XINE_LT_CURRENT=15
XINE_LT_REVISION=0
XINE_LT_AGE=14

dnl for a release tarball do "rm .cvsversion" before "make dist"
if test -f .cvsversion; then
  XINE_PRE="cvs"
else
  XINE_PRE=""
fi

AC_SUBST(XINE_MAJOR)
AC_SUBST(XINE_MINOR)
AC_SUBST(XINE_SUB)
AC_SUBST(XINE_IFACE_AGE)
AC_SUBST(XINE_BIN_AGE)

AC_DEFINE_UNQUOTED(XINE_MAJOR, $XINE_MAJOR,[xine major version number])
AC_DEFINE_UNQUOTED(XINE_MINOR, $XINE_MINOR,[xine minor version number])
AC_DEFINE_UNQUOTED(XINE_SUB, $XINE_SUB, [xine sub version number])

AC_SUBST(XINE_LT_CURRENT)
AC_SUBST(XINE_LT_REVISION)
AC_SUBST(XINE_LT_AGE)

SPEC_VERSION=$XINE_MAJOR.$XINE_MINOR${XINE_SUBPART}$XINE_PRE
TAR_NAME="xine-lib-"$SPEC_VERSION
dnl TAR_NAME="xine-lib-"$XINE_MAJOR-$XINE_PRE
dnl SPEC_VERSION=${XINE_MAJOR}_$XINE_PRE

AC_SUBST(TAR_NAME)
AC_SUBST(SPEC_VERSION)

LIBNAME=libxine$XINE_MAJOR
AC_SUBST(LIBNAME)

AC_DEFINE_UNQUOTED(XINE_TEXTDOMAIN, "$LIBNAME", [catalog message text domain])

dnl
AC_CANONICAL_TARGET

dnl
AM_INIT_AUTOMAKE("xine-lib", $SPEC_VERSION)
dnl AM_INIT_AUTOMAKE("xine-lib", $XINE_MAJOR-$XINE_PRE)


dnl ---------------------------------------------
dnl Made possible to build for another arch.
dnl ---------------------------------------------

if test x$XINE_BUILD != "x"; then
  AC_MSG_RESULT([*** build forced to $XINE_BUILD ***])
  build=$XINE_BUILD
  host=$XINE_BUILD
else
  check_athlon=yes
fi

AM_CONFIG_HEADER(config.h)


dnl ---------------------------------------------
dnl Check for programs.
dnl ---------------------------------------------

dnl Save CFLAGS, AC_ISC_POSIX set some unwanted default CFLAGS
saved_CFLAGS="$CFLAGS"
AC_ISC_POSIX
AC_PATH_MAGIC
CFLAGS="$saved_CFLAGS"
AC_PROG_CC
AC_HEADER_STDC
AC_PROG_MAKE_SET
dnl AC_PROG_EGREP  needs autoconf 2.54, just hardcode for "egrep"
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_AWK
AC_C_INLINE


dnl ---------------------------------------------
dnl Check for assembler (ffmpeg need it), don't call this before LIBTOOL
dnl ---------------------------------------------

AM_PROG_AS_MOD


dnl ---------------------------------------------
dnl Libtool
dnl ---------------------------------------------

AC_LIBTOOL_DLOPEN
AC_DISABLE_STATIC
AC_LIBTOOL_WIN32_DLL
AC_PROG_LIBTOOL
AC_PROG_LIBTOOL_SANITYCHECK
AC_SUBST(LIBTOOL_DEPS)
if ${CONFIG_SHELL} ./libtool --features | grep "enable static" >/dev/null; then
  STATIC="-static"
else
  STATIC=
fi
AC_SUBST(STATIC)


dnl ---------------------------------------------
dnl NLS Support
dnl ---------------------------------------------

dnl ALL_LINGUAS="cs da de el en@quot en@boldquot es et fr gl id it ja ko nl nn no pl pt pt_BR ru sl sv tr zh"
AM_ICONV
AM_GNU_GETTEXT([external])
AC_PROG_GMSGFMT_PLURAL

AC_CHECK_FUNCS([nl_langinfo])

dnl ---------------------------------------------
dnl Checks for typedefs, structures, and compiler characteristics.
dnl ---------------------------------------------

AC_C_BIGENDIAN
dnl AC_C_BIGENDIAN triggers an AC_TRY_RUN warning; we can't cross compile
dnl xine (oh, well)
AC_C_CONST
AC_C_ALWAYS_INLINE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_CHECK_SIZEOF(long)
dnl AC_CHECK_TYPES([ptrdiff_t])
dnl For systems without inttypes.h would be needed extend generated replacement.
AC_CHECK_GENERATE_INTTYPES([lib])
AM_CONDITIONAL(GENERATED_INTTYPES_H, test x"$ac_cv_header_inttypes_h" != x"yes")
AC_CHECK_TYPE(ssize_t, :, AC_DEFINE(ssize_t, __int64, [define ssize_t to __int64 if it's missing in default includes]))


dnl ---------------------------------------------
dnl threads and OS specific stuff
dnl ---------------------------------------------

AC_ARG_WITH(pthread-prefix, 
  AC_HELP_STRING(
    [--with-pthread-prefix=PREFIX],
    [path to pthread library]),
  [pthread_prefix="$withval"],
  [pthread_prefix="no"])

case "$host" in
  *-*-freebsd*)
    if test x"$pthread_prefix" = "xno"; then
      pthread_prefix="/usr/local"
    fi
    THREAD_LIBS="-L$pthread_prefix/lib -pthread"
    THREAD_CPPFLAGS="-I$pthread_prefix/include"
    CPPFLAGS="$CPPFLAGS $THREAD_CPPFLAGS -D_THREAD_SAFE"
    have_pthread=yes

    if test x"$am_cv_func_iconv" != xyes; then
      AC_MSG_WARN([
****************************************************************
* You need to install a recent version of the port 'libiconv'  *
* (in /usr/ports/converters/libiconv) or to specify prefix     *
* by option --with-libiconv-prefix.                            *
* The library in this port is needed to successfully compile   *
* libsputext plugin.                                           *
****************************************************************
      ])
    fi
    ;;

  *-*-hpux11*)
    THREAD_LIBS="-lpthread"
    have_pthread=yes

    ;;

  *)
    if test x"$pthread_prefix" = "xno"; then
      THREAD_LIBS="-lpthread"
    else
      THREAD_LIBS="-L$pthread_prefix/lib -lpthread"
      THREAD_CPPFLAGS="-I$pthread_prefix/include"
    fi
    CPPFLAGS="$CPPFLAGS $THREAD_CPPFLAGS"

    ac_save_LIBS="$LIBS"
    LIBS="$LIBS $THREAD_LIBS"

    AC_CHECK_LIB(pthread, pthread_create, have_pthread=yes)

    LIBS="$ac_save_LIBS"
    ;;
esac

if test x"$have_pthread" != "xyes"; then
  AC_MSG_ERROR(pthread needed)
fi

AC_SUBST(THREAD_CPPFLAGS)
AC_SUBST(THREAD_LIBS)

dnl
AC_MSG_CHECKING(for recursive mutex support in pthread)
ac_save_LIBS="$LIBS"
LIBS="$LIBS $THREAD_LIBS"

AC_DEFINE(_GNU_SOURCE)
have_recursive_mutex=no
AC_COMPILE_IFELSE(AC_LANG_SOURCE([#include <pthread.h>

int main() {
  pthread_mutexattr_t attr;
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  return 0;
}
	]),[
	    have_recursive_mutex=yes
	],[
	    AC_MSG_ERROR(recursive mutex support is needed - please report)
	])
LIBS="$ac_save_LIBS"

AC_MSG_RESULT($have_recursive_mutex)


dnl ---------------------------------------------
dnl Windows ports checks
dnl ---------------------------------------------
case "$host" in
  *-*-mingw* | *-*-cygwin*)
    dnl check if we are using the cygwin, mingw or cygwin with mno-cygwin mode
    dnl in which case we are actually dealing with a mingw32 compiler
    case "$host" in
      *-*-mingw32*)
        SYS=mingw32
        ;;
      *-*-cygwin*)
        AC_EGREP_CPP(pattern, 
                     [#ifdef WIN32
                      yes
                      #endif],
                     SYS=mingw32, SYS=cygwin)
        ;;
    esac

    if test "$SYS" = "mingw32"; then
      WIN32_CPPFLAGS='-I$(top_srcdir)/win32/include'
      LIBS="-lwinmm -lwsock32 $LIBS"
      GOOM_LIBS="-liberty"
    fi
    LDFLAGS="-no-undefined $LDFLAGS"
    ;;
esac
AC_SUBST(GOOM_LIBS)
AC_SUBST(WIN32_CPPFLAGS)
AM_CONDITIONAL(WIN32, test x$SYS = "xmingw32")


dnl ---------------------------------------------
dnl dynamic linker
dnl ---------------------------------------------
AM_DL()


dnl ---------------------------------------------
dnl determine libdir name
dnl ---------------------------------------------

AC_MSG_CHECKING([libdir name])
case $host in
*-*-linux*)
 # Test if the compiler is 64bit
 echo 'int i;' > conftest.$ac_ext
 xine_cv_cc_64bit_output=no
 if AC_TRY_EVAL(ac_compile); then
 case `"$MAGIC_CMD" conftest.$ac_objext` in
 *"ELF 64"*)
   xine_cv_cc_64bit_output=yes
   ;;
 esac
 fi
 rm -rf conftest*
 ;;
esac

case $host_cpu:$xine_cv_cc_64bit_output in
powerpc64:yes | s390x:yes | sparc64:yes | x86_64:yes)
 XINE_LIBNAME="lib64"
 ;;
*:*)
 XINE_LIBNAME="lib"
 ;;
esac
AC_MSG_RESULT([$XINE_LIBNAME])


dnl ---------------------------------------------
dnl mpeg2lib and ffmpeg stuff
dnl ---------------------------------------------

AC_SUBST(LIBMPEG2_CFLAGS)
AC_SUBST(LIBFFMPEG_CPPFLAGS)

LIBMPEG2_CFLAGS=""
LIBFFMPEG_CPPFLAGS="-DSIMPLE_IDCT -DHAVE_AV_CONFIG_H -DRUNTIME_CPUDETECT -DUSE_FASTMEMCPY -DCONFIG_RISKY -DCONFIG_DECODERS -DXINE_MPEG_ENCODER -DCONFIG_ZLIB -DCONFIG_GPL"

AC_CHECK_DECL(lrintf,[
	AC_DEFINE(HAVE_LRINTF,1,[Define this if the 'lrintf' function is declared in math.h])
	AC_DEFINE(_ISOC9X_SOURCE,1,[Define this if you are ISO C9X compliant])
      ],,[
#define _ISOC9X_SOURCE
#include <math.h>
])
AC_CHECK_DECL(rintf,[
	AC_DEFINE(HAVE_RINTF,1,[Define this if the 'rintf' function is declared in math.h])
      ],,[
#include <math.h>
])

AC_CHECK_TYPES(int_fast8_t, [], [LIBFFMPEG_CFLAGS="$LIBFFMPEG_CFLAGS -DEMULATE_FAST_INT"])

AC_ARG_ENABLE(altivec,
	AC_HELP_STRING([--disable-altivec], [do not use assembly codes for Motorola 74xx CPUs]),
		enable_altivec=$enableval,
		enable_altivec=yes)

AC_ARG_ENABLE(vis,
	AC_HELP_STRING([--disable-vis], [do not use assembly codes for Sun UltraSPARC CPUs]),
         	enable_vis=$enableval,
		enable_vis=yes)

AC_ARG_ENABLE(mlib,
	AC_HELP_STRING([--disable-mlib], [do not build Sun mediaLib support]),
		enable_mlib=$enableval,
		enable_mlib=yes)

AC_ARG_ENABLE(mlib-lazyload,
	AC_HELP_STRING([--enable-mlib-lazyload], [check for Sun mediaLib at runtime]),
		enable_mlib_lazyload=$enableval,
		enable_mlib_lazyload=no)

if test x$enable_mlib = xyes; then
    if test x"$MLIBHOME" = x; then
	mlibhome=/opt/SUNWmlib
    else
	mlibhome="$MLIBHOME"
    fi

    AC_CHECK_LIB(mlib, mlib_VideoAddBlock_U8_S16,
	[ saved_CPPFLAGS="$CPPFLAGS"
	  CPPFLAGS="$CPPFLAGS -I$mlibhome/include"
	  AC_CHECK_HEADER(mlib_video.h,
	       [ if test x$enable_mlib_lazyload = xyes; then
		     if test "$GCC" = yes; then
			 MLIB_LIBS="-L$mlibhome/lib -Wl,-z,lazyload,-lmlib,-z,nolazyload"
		     else
			 MLIB_LIBS="-L$mlibhome/lib -z lazyload -lmlib -z nolazyload"
		     fi
		     AC_DEFINE(MLIB_LAZYLOAD,1,[Define this if you want to load mlib lazily])
		 else
		     MLIB_LIBS="-L$mlibhome/lib -lmlib"
		 fi
		 MLIB_CFLAGS="-I$mlibhome/include"
		 LIBMPEG2_CFLAGS="$LIBMPEG2_CFLAGS $MLIB_CFLAGS" 
		 LIBFFMPEG_CFLAGS="$LIBFFMPEG_CFLAGS $MLIB_CFLAGS"
		 AC_DEFINE(HAVE_MLIB,1,[Define this if you have mlib installed])
		 AC_DEFINE(LIBMPEG2_MLIB,1,[Define this if you have mlib installed])
		 ac_have_mlib=yes
	       ],)
	  CPPFLAGS="$saved_CPPFLAGS"
	], , -L$mlibhome/lib)
fi
AM_CONDITIONAL(HAVE_MLIB, test x$ac_have_mlib = "xyes")
AC_SUBST(MLIB_LIBS)
AC_SUBST(MLIB_CFLAGS)

AM_PATH_FFMPEG(AC_DEFINE(HAVE_FFMPEG, 1, [Define this if you have ffmpeg library]))
AM_CONDITIONAL(HAVE_FFMPEG, test x"$external_ffmpeg_found" = "xyes")


dnl ---------------------------------------------
dnl Checks for X11
dnl ---------------------------------------------

AC_PATH_XTRA
if test x"$no_x" != "xyes"; then
    AC_DEFINE(HAVE_X11,1,[Define this if you have X11R6 installed])
fi
AM_CONDITIONAL(HAVE_X11, [test x"$no_x" != "xyes"])


dnl ---------------------------------------------
dnl Locate libraries needed for X health check
dnl ---------------------------------------------

x_lib_location="`ls -1 "${x_libraries:-/usr/local/lib}/libX11.so"* "${x_libraries:-/usr/lib}/libX11.so"* 2>/dev/null | sed -e '/[[0-9]]$/! d; s%^.*/%%; t q; b; :q q'`"
AC_DEFINE_UNQUOTED([LIBX11_SO], "${x_lib_location:-libX11.so}", [The soname of libX11, needed for dlopen()])
x_lib_location="`ls -1 "${x_libraries:-/usr/local/lib}/libXv.so"*  "${x_libraries:-/usr/lib}/libXv.so"*  2>/dev/null | sed -e '/[[0-9]]$/! d; s%^.*/%%; t q; b; :q q'`"
AC_DEFINE_UNQUOTED([LIBXV_SO],  "${x_lib_location:-libXv.so}",  [The soname of libXv, needed for dlopen()])


dnl ---------------------------------------------
dnl socket library
dnl ---------------------------------------------

dnl Test for socket and network support library
NET_LIBS=""
AC_CHECK_LIB(socket, socket, NET_LIBS="-lsocket $NET_LIBS",)
AC_CHECK_LIB(nsl, gethostbyname, NET_LIBS="-lnsl $NET_LIBS",)
AC_SUBST(NET_LIBS)


dnl ---------------------------------------------
dnl socklen_t
dnl ---------------------------------------------
AC_CHECK_SOCKLEN_T


dnl ---------------------------------------------
dnl IPv6
dnl ---------------------------------------------

AC_ARG_ENABLE(ipv6,
	AC_HELP_STRING([--enable-ipv6], [enable use of IPv6]),
		enable_ipv6=$enableval,
		enable_ipv6=no)

echo -n "IPv6 is "
if test x$enable_ipv6 = xyes; then
    CFLAGS="-DENABLE_IPV6 $CFLAGS"
    echo "enabled"
else
    echo "disabled"
fi


dnl ---------------------------------------------
dnl zlib
dnl ---------------------------------------------

AC_ARG_WITH(zlib-prefix, 
  AC_HELP_STRING(
    [--with-zlib-prefix=PREFIX],
    [path to zlib compression library]),
  [zlib_prefix="$withval"],
  [zlib_prefix="no"])

if test x"$zlib_prefix" = "xno"; then
  ZLIB_LIBS="-lz"
else
  ZLIB_CPPFLAGS="-I$zlib_prefix/include"
  ZLIB_LIBS="-L$zlib_prefix/lib -lz"
fi

ac_save_CPPFLAGS="$CPPFLAGS"
ac_save_LIBS="$LIBS"
CPPFLAGS="$CPPFLAGS $ZLIB_CPPFLAGS"
LIBS="$LIBS $ZLIB_LIBS"

AC_CHECK_LIB(z, gzsetparams,
  [AC_CHECK_HEADER(zlib.h,
     have_zlib=yes)])

CPPFLAGS="$ac_save_CPPFLAGS"
LIBS="$ac_save_LIBS"

if test "x$have_zlib" != "xyes"; then
  AC_MSG_ERROR(zlib needed)
fi
AC_SUBST(ZLIB_CPPFLAGS)
AC_SUBST(ZLIB_LIBS)


dnl ---------------------------------------------
dnl check posix timers
dnl ---------------------------------------------

AC_CHECK_LIB(rt, clock_getres,
             [RT_LIBS="-lrt"
              AC_DEFINE(HAVE_POSIX_TIMERS,1,
                        [Define this if you have POSIX timers.])], 
             AC_MSG_RESULT([*** no POSIX timers available.]))
AC_SUBST(RT_LIBS)

dnl ---------------------------------------------
dnl Check for OpenGL & [GLut | GLU]
dnl ---------------------------------------------

AM_PATH_OPENGL()


dnl ---------------------------------------------
dnl Check for platform which supports syncfb
dnl ---------------------------------------------

case "$host_or_hostalias" in
  *linux* )
    AC_ARG_ENABLE([fb],
      AC_HELP_STRING([--disable-fb], [do not build syncfb plugins]),
      have_syncfb=$enableval, have_syncfb="yes")
    ;;
  *)
    have_syncfb="no"
    ;;
esac
AM_CONDITIONAL(HAVE_SYNCFB, test x"$have_syncfb" = "xyes")


dnl ----------------------------------------------
dnl Check for usable video-for-linux (v4l) support
dnl ----------------------------------------------
AC_ARG_ENABLE([v4l],
  AC_HELP_STRING([--disable-v4l], [do not build Video4Linux support]),
  [with_v4l=${enableval}], [with_v4l=yes])

if test "x$with_v4l" == "xyes"; then
  AC_CHECK_HEADER(linux/videodev.h, have_v4l=yes,)
else
  have_v4l=no
fi
AM_CONDITIONAL(HAVE_V4L, [test x"$have_v4l" = "xyes"])


dnl ----------------------------------------------
dnl Check for Xv and XvMC support
dnl ----------------------------------------------

dnl With recent XFree86 or Xorg, dynamic linking is preferred!
dnl Only dynamic linking is possible when using libtool < 1.4.0

AC_ARG_WITH(xv-path, AC_HELP_STRING([--with-xv-path=path], [where libXv is installed]),
            xv_path="$withval",)

AC_ARG_ENABLE([static-xv],
        AC_HELP_STRING([--enable-static-xv],[Enable this to force linking against libXv.a]))

if test "x$enable_static_xv" = "xyes"; then
  xv_prefer_shared="no"
else
  xv_prefer_shared="yes"
fi

if test x"$no_x" != "xyes"; then
  AC_FIND_LIBXV
fi
AM_CONDITIONAL(HAVE_XV, test x$ac_have_xv = "xyes")


host_or_hostalias="$host"
if test "$host_or_hostalias" = ""; then
    dnl user has called ./configure with a host parameter unknown to
    dnl config.sub; the canonical "$host" is empty
    dnl
    dnl Try the following switch with user's original host_alias 
    dnl input instead.
    dnl
    host_or_hostalias="$host_alias"
fi

case "$host_or_hostalias" in
  hppa*)
	if test x$ac_have_xv_static = "xyes"; then
	  echo "warning: hppa linker - disabling static libXv"
	  XV_LIB="libXv.so"
	fi
	;;

  ppc-*-linux* | powerpc-*)
        ppc_arch="yes"
        ;;

  *)
        ;;
esac
AM_CONDITIONAL(PPC_ARCH, test x$ppc_arch = "xyes")

dnl
dnl Check if we can enable the xxmc plugin.
dnl

AC_ARG_ENABLE([xvmc],
  AS_HELP_STRING([--disable-xvmc], [Disable XxMC and XvMC outplut plugins]) )

if test x$no_x = "x" && test "x$enable_xvmc" != "xno"; then

AC_ARG_WITH(xxmc-path, AC_HELP_STRING([--with-xxmc-path=path], [where libXvMC libraries for the
	xxmc plugin are  installed. Defalts to the default X library path.]),
	xxmc_path="$withval", xxmc_path="$x_libraries")
AC_ARG_WITH(xxmc-lib, AC_HELP_STRING([--with-xxmc-lib=XXXX], [The name of the XvMC library 
	libXXXX.so for the xxmc plugin.]),xxmc_stub="$withval", 
	xxmc_stub="XvMCW")

saved_libs="$LIBS"
saved_CPPFLAGS="$CPPFLAGS"
if test x$x_includes != "x"; then
	CPPFLAGS="$CPPFLAGS -I$x_includes"
fi

XXMC_LIB="-L$xxmc_path -l$xxmc_stub"
AC_MSG_CHECKING(whether to enable the xxmc plugin with vld extensions)
AC_MSG_RESULT()
dnl Check if vld "extended" XvMC is available
if test x$xxmc_stub == "xXvMCW" && test x$ac_have_xv == "xyes"; then
	AC_CHECK_LIB($xxmc_stub, XvMCPutSlice,
             	     ac_have_xxmc="yes",
	             [ac_have_xxmc="no"
              	        AC_MSG_RESULT([*** Could not link with -l$xxmc_stub for vld extensions.])],
                     [-L$xxmc_path $X_LIBS $X_PRE_LIBS $XV_LIB -lXext $X_EXTRA_LIBS])
else
    if test x$ac_have_xv = "xyes"; then 
      AC_CHECK_LIB($xxmc_stub, XvMCPutSlice,
	           [ac_have_xxmc="yes"
	             XXMC_LIB="$XXMC_LIB -lXvMC"],
                   [ac_have_xxmc="no"
		     AC_MSG_RESULT([*** Could not link with -l$xxmc_stub -lXvMC for vld extensions.])],
                   [-L$xxmc_path -lXvMC $X_LIBS $X_PRE_LIBS $XV_LIB -lXext $X_EXTRA_LIBS])
    else 
      ac_have_xxmc="no",
    fi  
fi

if test x$ac_have_xxmc = "xyes"; then
  AC_CHECK_HEADERS(X11/extensions/vldXvMC.h,
     [ac_have_vldxvmc_h="yes"],
     ac_have_vldxvmc="no",)
  if test "x$ac_have_vldxvmc_h" = "xyes"; then
     AC_DEFINE([HAVE_VLDXVMC], [1], 
               [Define 1 if you have vldXvMC.h])
  fi
fi
dnl Try fallback to standard XvMC if vld failed
if test x$ac_have_xxmc = "xno"; then
  if test x$xxmc_stub == "xXvMCW"; then
	  AC_CHECK_LIB($xxmc_stub, XvMCCreateContext,
               ac_have_xxmc="yes",
	       [ac_have_xxmc="no"
                AC_MSG_RESULT([*** Could not link with -l$xxmc_stub for standard XvMC.])],
               [-L$xxmc_path $X_LIBS $X_PRE_LIBS $XV_LIB -lXext $X_EXTRA_LIBS])
  else
      if test x$ac_have_xv = "xyes"; then 
        AC_CHECK_LIB($xxmc_stub, XvMCCreateContext,
	             [ac_have_xxmc="yes"
		      XXMC_LIB="$XXMC_LIB -lXvMC"],
                     [ac_have_xxmc="no"
		      AC_MSG_RESULT([*** Could not link with -lXvMC for standard XvMC.])],
                     [-L$xxmc_path -lXvMC $X_LIBS $X_PRE_LIBS $XV_LIB -lXext $X_EXTRA_LIBS])
      else 
        ac_have_xxmc="no",
      fi  
  fi
fi
if test x$ac_have_xxmc = "xyes"; then
  AC_CHECK_HEADERS(X11/extensions/XvMC.h,,
     ac_have_xxmc="no",)
fi
if test x$ac_have_xxmc = "xyes"; then
  AC_DEFINE(HAVE_XXMC,1,[Define this to compile the xxmc plugin.])
  if test "x$ac_have_vldxvmc_h" = "xyes"; then
    AC_MSG_RESULT([*** Enabling xxmc plugin with vld extensions.])
  else
    AC_MSG_RESULT([*** Enabling xxmc plugin for standard XvMC *only*.])
  fi
else
  AC_MSG_RESULT([*** Disabling xxmc plugin due to above errors.])
fi
LIBS="$saved_libs"
fi
AM_CONDITIONAL(HAVE_VLDXVMC, test x$ac_have_vldxvmc_h = "xyes")
AM_CONDITIONAL(HAVE_XXMC, test x$ac_have_xxmc = "xyes")
AC_SUBST(XXMC_LIB)
	   
dnl
dnl Check if we can enable the xvmc plugin.
dnl
if test x$no_x = "x" && test "x$enable_xvmc" != "xno"; then

AC_ARG_WITH(xvmc-path, AC_HELP_STRING([--with-xvmc-path=path], [where libXvMC libraries for the
	xvmc plugin are  installed. Defalts to the default X library path.]),
	xvmc_path="$withval", xvmc_path="$x_libraries")
AC_ARG_WITH(xvmc-lib, AC_HELP_STRING([--with-xvmc-lib=XXXX], [The name of the XvMC library 
	libXXXX.so for the xvmc plugin.]),xvmc_stub="$withval", 
	xvmc_stub="XvMCW")
saved_libs="$LIBS"
XVMC_LIB="-L$xvmc_path -l$xvmc_stub"
AC_MSG_CHECKING(whether to enable the xvmc plugin)
AC_MSG_RESULT()
if test x$xvmc_stub == "xXvMCW"; then
	AC_CHECK_LIB($xvmc_stub, XvMCCreateContext,
             ac_have_xvmc="yes",
	     [ac_have_xvmc="no"
              AC_MSG_RESULT([*** Could not link with -l$xvmc_stub.])],
             [-L$xvmc_path $X_LIBS $X_PRE_LIBS $XV_LIB -lXext $X_EXTRA_LIBS])
else
    if test x$ac_have_xv = "xyes"; then 
      AC_CHECK_LIB($xvmc_stub, XvMCCreateContext,
	           [ac_have_xvmc="yes"
		    XVMC_LIB="$XVMC_LIB -lXvMC"],
                   [ac_have_xvmc="no"
		    AC_MSG_RESULT([*** Could not link with -lXvMC.])],
                   [-L$xvmc_path -lXvMC $X_LIBS $X_PRE_LIBS $XV_LIB -lXext $X_EXTRA_LIBS])
    else 
      ac_have_xvmc="no",
    fi  
fi
if test x$ac_have_xvmc = "xyes"; then
  AC_CHECK_HEADERS(X11/extensions/XvMC.h,,
     ac_have_xvmc="no",)
fi
if test x$ac_have_xvmc = "xyes"; then
  AC_DEFINE(HAVE_XVMC,1,[Define this if you have an XvMC library and XvMC.h installed.])
  AC_MSG_RESULT([*** Enabling old xvmc plugin.])
else
  AC_MSG_RESULT([*** Disabling old xvmc plugin due to above errors.])
fi
CPPFLAGS="$saved_CPPFLAGS"
LIBS="$saved_libs"
fi
AM_CONDITIONAL(HAVE_XVMC, test x$ac_have_xvmc = "xyes")
AC_SUBST(XVMC_LIB)


dnl ---------------------------------------------
dnl Checks for Xinerama extension
dnl ---------------------------------------------

AC_ARG_ENABLE([xinerama],
  AC_HELP_STRING([--disable-xinerama], [do not build Xinerama support]),
  [with_xinerama=$enableval], [with_xinerama=yes])

if test "x$with_xinerama" = "xyes"; then
  AC_CHECK_LIB(Xinerama, XineramaQueryExtension, 
             [X_LIBS="$X_LIBS -lXinerama"
              AC_DEFINE(HAVE_XINERAMA,1,[Define this if you have libXinerama installed])
              ac_have_xinerama="yes"],,
             [$X_LIBS $X_PRE_LIBS -lXext $X_EXTRA_LIBS])
else
  ac_have_xinerama=no
fi
dnl AM_CONDITIONAL(HAVE_XINERAMA, test x$ac_have_xinerama = "xyes")

 
dnl ---------------------------------------------
dnl Checks for Ascii-Art library
dnl ---------------------------------------------

AC_ARG_ENABLE([aalib],
  AC_HELP_STRING([--disable-aalib], [do not build AALIB support]),
  [with_aalib=$enableval], [with_aalib=yes])

if test "x$with_aalib" = "xyes"; then
  AM_PATH_AALIB(1.4,, AC_MSG_RESULT([*** All of AALIB dependent parts will be disabled ***]))
else
  no_aalib=yes
fi

AM_CONDITIONAL(HAVE_AA, test x$no_aalib != "xyes")

dnl ---------------------------------------------
dnl Checks for Color AsCii Art library
dnl ---------------------------------------------

AC_ARG_ENABLE([caca],
  AC_HELP_STRING([--disable-caca], [do not build CACA support]),
  [with_caca=$enableval], [with_caca=yes])

if test "x$with_caca" = "xyes"; then
  AM_PATH_CACA(0.3,, AC_MSG_RESULT([*** All CACA-dependent parts will be disabled ***]))
else
  no_caca=yes
fi
AM_CONDITIONAL(HAVE_CACA, test x$no_caca != "xyes")

dnl ---------------------------------------------
dnl Check solaris framebuffer device support
dnl ---------------------------------------------

AC_CHECK_HEADER(sys/fbio.h, ac_have_sunfb=yes,)
AM_CONDITIONAL(HAVE_SUNFB, [test x"$ac_have_sunfb" = "xyes"])


dnl ---------------------------------------------
dnl Check for Sun DGA
dnl ---------------------------------------------

saved_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS -L/usr/openwin/lib"
saved_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS -I/usr/openwin/include"
AC_CHECK_LIB(dga, XDgaGrabDrawable, [
	AC_CHECK_HEADER(dga/dga.h, [
	        SUNDGA_CFLAGS="-I/usr/openwin/include"
		SUNDGA_LIBS="-L/usr/openwin/lib -R/usr/openwin/lib -ldga"
		ac_have_sundga=yes
	])
])
LDFLAGS="$saved_LDFLAGS"
CPPFLAGS="$saved_CPPFLAGS"
AM_CONDITIONAL(HAVE_SUNDGA, [test x"$ac_have_sundga" = "xyes"])
AC_SUBST(SUNDGA_CFLAGS)
AC_SUBST(SUNDGA_LIBS)


dnl ---------------------------------------------
dnl Check linux framebuffer device support
dnl ---------------------------------------------

AC_CHECK_HEADER(linux/fb.h,
                [AC_DEFINE(HAVE_FB,1,[Define this if you have linux framebuffer support])
                 have_fb=yes],)
AC_ARG_ENABLE(fb, AC_HELP_STRING([--disable-fb], [do not build linux framebuffer support]),
	      have_fb=$enableval)
AM_CONDITIONAL(HAVE_FB, [test x"$have_fb" = "xyes"])


dnl ---------------------------------------------
dnl Check whether to build Mac OS X video output driver
dnl ---------------------------------------------

AC_ARG_ENABLE(macosx_video, AC_HELP_STRING([--enable-macosx-video], [enable support for Mac OS X OpenGL video output]),
              have_macosx_video=$enableval)
AM_CONDITIONAL(HAVE_MACOSX_VIDEO, [test x"$have_macosx_video" = "xyes"])


dnl ---------------------------------------------
dnl Check whether to build Mac OS X audio output driver
dnl ---------------------------------------------

AC_ARG_ENABLE(coreaudio, AC_HELP_STRING([--enable-coreaudio], [enable support for Mac OS X Coreaudio output]),
              have_coreaudio=$enableval)
AM_CONDITIONAL(HAVE_COREAUDIO, [test x"$have_coreaudio" = "xyes"])


dnl ---------------------------------------------
dnl Find pkg-config
dnl ---------------------------------------------

AC_PATH_TOOL(PKG_CONFIG, pkg-config, no)
if test x$PKG_CONFIG = xno ; then
  echo "*** pkg-config not found. See http://pkgconfig.sourceforge.net"
  echo "*** All of DIRECTFB dependent parts will be disabled"
else


dnl ---------------------------------------------
dnl Check for DirectFB
dnl ---------------------------------------------
AC_ARG_ENABLE(directfb,
	AC_HELP_STRING([--enable-directfb], [enable use of DirectFB]),
		enable_directfb=$enableval,
		enable_directfb=no)

  DIRECTFB_REQUIRED_VERSION=0.9.22
if test x$enable_directfb = "xyes"; then
  AC_MSG_CHECKING(for DirectFB)
  if $PKG_CONFIG --atleast-version $DIRECTFB_REQUIRED_VERSION directfb ; then
        AC_MSG_RESULT(found)
        DIRECTFB_CFLAGS=`$PKG_CONFIG --cflags directfb`
        DIRECTFB_LIBS=`$PKG_CONFIG --libs directfb`
        have_directfb="yes"
  else
    AC_MSG_RESULT([*** All of DIRECTFB dependent parts will be disabled ***])
  fi
fi
fi

AC_SUBST(DIRECTFB_CFLAGS)
AC_SUBST(DIRECTFB_LIBS)
AM_CONDITIONAL(HAVE_DIRECTFB, test x$have_directfb = "xyes" )


dnl ---------------------------------------------
dnl check for SDL
dnl ---------------------------------------------

AC_ARG_ENABLE([sdl],
  AC_HELP_STRING([--disable-sdl], [do not build SDL support]),
  [with_sdl=$enableval], [with_sdl=yes])

if test "x$with_sdl" = "xyes"; then
  AM_PATH_SDL(1.1.5, 
    AC_DEFINE(HAVE_SDL,1,[Define this if you have SDL library installed]),
    [])
else
  no_sdl=yes
fi
AM_CONDITIONAL(HAVE_SDL, [test x"$no_sdl" != x"yes"])


dnl ---------------------------------------------
dnl check for Libstk
dnl ---------------------------------------------

AC_MSG_CHECKING(for Libstk)
dnl do some actual testing here
if test x$PKG_CONFIG = xno ; then
  AC_MSG_RESULT(no)
  echo "*** pkg-config not found. See http://pkgconfig.sourceforge.net"
  echo "*** All of LIBSTK dependent parts will be disabled"
else
    LIBSTK_REQUIRED_VERSION=0.2.0
    if $PKG_CONFIG --atleast-version $LIBSTK_REQUIRED_VERSION libstk ; then
        LIBSTK_CFLAGS=`$PKG_CONFIG --cflags libstk`
        LIBSTK_LIBS=`$PKG_CONFIG --libs libstk`
        have_stk="yes"
        AC_MSG_RESULT(yes)
    else
        AC_MSG_RESULT(no)
        AC_MSG_RESULT([*** All of Libstk dependent parts will be disabled ***])
    fi
fi
AC_SUBST(LIBSTK_CFLAGS)
AC_SUBST(LIBSTK_LIBS)
AM_CONDITIONAL(HAVE_STK, [test x"$have_stk" = x"yes"])

dnl ---------------------------------------------
dnl check for polypaudio
dnl ---------------------------------------------

AC_ARG_ENABLE([polypaudio],
  AC_HELP_STRING([--disable-polypaudio], [do not build Polypaudio support]),
  [with_polypaudio=$enableval], [with_polypaudio=yes])

if test "x$with_polypaudio" = "xyes"; then
  AC_MSG_CHECKING(for polypaudio)
  dnl do some actual testing here
  if test x$PKG_CONFIG = xno ; then
    AC_MSG_RESULT(no)
    echo "*** pkg-config not found. See http://pkgconfig.sourceforge.net"
    echo "*** All of polypaudio dependent parts will be disabled"
  else
      POLYPAUDIO_REQUIRED_VERSION=0.6
      if $PKG_CONFIG --atleast-version $POLYPAUDIO_REQUIRED_VERSION polyplib ; then
          POLYPAUDIO_CFLAGS=`$PKG_CONFIG --cflags polyplib`
          POLYPAUDIO_LIBS=`$PKG_CONFIG --libs polyplib`
          have_polypaudio="yes"
          AC_MSG_RESULT(yes)
      else
          AC_MSG_RESULT(no)
          AC_MSG_RESULT([*** All of polypaudio dependent parts will be disabled ***])
      fi
  fi
  AC_SUBST(POLYPAUDIO_CFLAGS)
  AC_SUBST(POLYPAUDIO_LIBS)
else
  have_polypaudio=no
fi
AM_CONDITIONAL(HAVE_POLYPAUDIO, [test x"$have_polypaudio" = x"yes"])

dnl ---------------------------------------------
dnl check for DirectX
dnl ---------------------------------------------

AM_PATH_DIRECTX()


dnl ---------------------------------------------
dnl dxr3 / hollywood plus card
dnl ---------------------------------------------

case "$host_or_hostalias" in
  *-linux*)
    AC_CHECK_DXR3()
    if test x"$have_libfame" = "xyes" ; then
      AC_DEFINE_UNQUOTED(HAVE_LIBFAME,1,[Define this if you have libfame mpeg encoder installed (fame.sf.net)])
      AM_PATH_LIBFAME(0.8.10, 
        AC_DEFINE(HAVE_NEW_LIBFAME,1,[Define this if you have libfame 0.8.10 or above]))
    fi
    if test x"$have_librte" = "xyes" ; then
      AC_DEFINE_UNQUOTED(HAVE_LIBRTE,1,[Define this if you have librte mpeg encoder installed (zapping.sf.net)])
    fi
    ;;
  *)
    have_dxr3="no"
    have_libfame="no"
    have_librte="no"
    have_encoder="no"
    ;;
esac
AM_CONDITIONAL(HAVE_DXR3, test x"$have_dxr3" = "xyes")
AM_CONDITIONAL(HAVE_LIBFAME, test x"$have_libfame" = "xyes")
AM_CONDITIONAL(HAVE_LIBRTE, test x"$have_librte" = "xyes")


dnl ---------------------------------------------
dnl Vidix/libdha
dnl ---------------------------------------------

AC_LINUX_PATH(/usr/src/linux)
AC_SUBST([LINUX_INCLUDE])

AC_ARG_ENABLE(vidix, AC_HELP_STRING([--disable-vidix], [do not build vidix support]),
	      check_vidix=$enableval, check_vidix=yes)
AC_ARG_ENABLE(dha-kmod, AC_HELP_STRING([--enable-dha-kmod], [build DHA kernel module]),
	      enable_dha_kmod=$enableval,enable_dha_kmod=no)

enable_vidix="no"

AC_MSG_CHECKING(for vidix support)
if test x"$check_vidix" = "xyes" -a x"$ac_cv_prog_AWK" != "xno"; then
  if test x"$no_x" != "xyes" -o x"$have_fb" = "xyes"; then
    case "$host_or_hostalias" in
      i?86-*-linux* | k?-*-linux* | athlon-*-linux*)
        enable_vidix="yes"
        enable_linux="yes"
        ;;
      i386-*-freebsd*)
        enable_vidix="yes"
        enable_dha_kmod="no"
        ;;
      *)
        enable_dha_kmod="no"
        enable_vidix="no"
        ;;
    esac
  fi
fi
AC_MSG_RESULT($enable_vidix)

AC_MSG_CHECKING(for DHA linux kernel module build)
if test x"$enable_dha_kmod" = "xyes"; then
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi

AM_CONDITIONAL(HAVE_VIDIX, test x"$enable_vidix" = "xyes")
AM_CONDITIONAL(HAVE_LINUX, test x"$enable_linux" = "xyes")
AM_CONDITIONAL(BUILD_DHA_KMOD, test x"$enable_dha_kmod" = "xyes")
AC_CHECK_PROG(MKNOD, mknod, mknod, no)
AC_CHECK_PROG(DEPMOD, depmod, depmod, no, $PATH:/sbin)


dnl ---------------------------------------------
dnl Ogg/Vorbis libs.
dnl ---------------------------------------------

AM_PATH_OGG(
           [ AM_PATH_VORBIS([], 
	                    AC_MSG_RESULT([*** All OGG/VORBIS dependent parts will be disabled ***])) 
	   ], 
           AC_MSG_RESULT([*** All of OGG/VORBIS dependent parts will be disabled ***]))
AM_CONDITIONAL(HAVE_VORBIS, [test x"$no_ogg" != "xyes" -a x"$no_vorbis" != "xyes"])


dnl ---------------------------------------------
dnl Ogg/Theora libs.
dnl ---------------------------------------------

AM_PATH_OGG(
           [ AM_PATH_THEORA(AC_DEFINE(HAVE_THEORA,1,[Define this if you have theora]),
                           AC_MSG_RESULT([*** All OGG/THEORA dependent parts will be disabled ***]))
          ],
           AC_MSG_RESULT([*** All of OGG/Theora dependent parts will be disabled ***]))
AM_CONDITIONAL(HAVE_THEORA, [test x"$no_ogg" != "xyes" -a x"$no_theora" != "xyes"])


dnl ---------------------------------------------
dnl Ogg/Speex libs.
dnl ---------------------------------------------
AC_ARG_ENABLE([speex],
  AC_HELP_STRING([--disable-speex], [do not build speex support]),
  [with_speex=$enableval], [with_speex=yes])

no_speex=yes

if test "x$with_speex" = "xyes"; then
  PKG_CHECK_MODULES(SPEEX, speex, [
    no_speex=no

    AC_DEFINE(HAVE_SPEEX,1,[Define this if you have speex])

    dnl Test whether Speex headers are eg. <speex.h> or <speex/speex.h>
    dnl Speex headers were moved in mid-2004; 1.0.x has backwards compatible headers, 1.1.x does not
    AC_CHECK_HEADER([speex/speex.h], [
       AC_DEFINE(HAVE_SPEEX_SUBDIR, [1], [Define to 1 if speex headers are eg. <speex/speex.h>])
    ])
  ], no_speex="yes")
fi

if test "x$no_speex" = "xyes" ; then
  AC_MSG_RESULT([*** All OGG/SPEEX dependent parts will be disabled ***])
fi

AC_SUBST(SPEEX_CFLAGS)
AC_SUBST(SPEEX_LIBS)
AM_CONDITIONAL(HAVE_SPEEX, [test x"$no_ogg" != "xyes" -a x"$no_speex" != "xyes"])


dnl ---------------------------------------------
dnl check for libFLAC
dnl ---------------------------------------------

AC_ARG_ENABLE([flac],
  AC_HELP_STRING([--disable-flac], [do not build flac support]),
  [with_flac=$enableval], [with_flac=yes])

if test "x$with_flac" = "xyes"; then
  AM_PATH_LIBFLAC([],
    AC_MSG_RESULT([*** All FLAC dependent parts will be disabled ***]))
else
  no_libFLAC=yes
fi
AM_CONDITIONAL(HAVE_FLAC, [test x"$no_libFLAC" != "xyes"])

dnl ---------------------------------------------
dnl External version of a52dec
dnl ---------------------------------------------

AC_ARG_ENABLE(a52dec, AC_HELP_STRING([--disable-a52dec], [Disable support for a52dec decoding library (default: enabled)]),
              [enable_a52dec="$enableval"], [enable_a52dec="yes"])
AC_ARG_WITH(external-a52dec, AC_HELP_STRING([--with-external-a52dec], [use external a52dec library (not recommended)]),
            [external_a52dec="$withval"], [external_a52dec="no"])

have_a52="no"

if test x"$enable_a52dec" = "xno"; then
  AC_MSG_RESULT([a52dec support disabled])
elif test x"$external_a52dec" = "xyes"; then
  have_a52="yes"
  AC_CHECK_HEADERS([a52dec/a52.h a52dec/a52_internal.h],, have_a52="no",
[
  #ifdef HAVE_SYS_TYPES_H
  # include <sys/types.h>
  #endif
  #ifdef HAVE_INTTYPES_H
  # include <inttypes.h>
  #endif
  #ifdef HAVE_STDINT_H
  # include <stdint.h>
  #endif

  #include <a52dec/a52.h>
])
  SAVE_LIBS="$LIBS"
  AC_CHECK_LIB([a52], [a52_init],, have_a52="no", [-lm])
  LIBS="$SAVE_LIBS"

  if test "x$have_a52" = "xno"; then
    AC_MSG_RESULT([*** no usable version of a52dec found, using internal copy ***])
  fi
else
  AC_MSG_RESULT([Use included a52dec support])
fi

AM_CONDITIONAL(A52, test x"$enable_a52dec" = "xyes")
AM_CONDITIONAL(EXTERNAL_A52DEC, test x"$have_a52" = "xyes")

dnl ---------------------------------------------
dnl External version of libmad
dnl ---------------------------------------------

AC_ARG_ENABLE(mad, AC_HELP_STRING([--disable-mad], [Disable support for MAD decoding library (default: enabled)]),
              [enable_libmad="$enableval"], [enable_libmad="yes"])
AC_ARG_WITH(external-libmad, AC_HELP_STRING([--with-external-libmad], [use external libmad library (not recommended)]),
            [external_libmad="$withval"], [external_libmad="no"])

have_mad="no"

if test "x$enable_libmad" = "xno"; then
  AC_MSG_RESULT([libmad support disabled])
elif test x"$external_libmad" = "xyes"; then
  PKG_CHECK_MODULES(LIBMAD, [mad], have_mad=yes, have_mad=no)
  AC_CHECK_HEADERS([mad.h])
  AC_SUBST(LIBMAD_LIBS)
  AC_SUBST(LIBMAD_CFLAGS)
  if test "x$have_mad" = "xno"; then
    AC_MSG_RESULT([*** no usable version of libmad found, using internal copy ***])
  fi
else
  AC_MSG_RESULT([Use included libmad support])
fi

AM_CONDITIONAL(MAD, test "x$enable_libmad" = "xyes")
AM_CONDITIONAL(EXTERNAL_LIBMAD, test x"$have_mad" = "xyes")

dnl ---------------------------------------------
dnl MNG libs.
dnl ---------------------------------------------

AC_ARG_ENABLE([mng],
  AC_HELP_STRING([--disable-mng], [do not build mng support]),
  [with_mng=$enableval], [with_mng=yes])

if test "x$with_mng" = "xyes"; then
  AC_CHECK_LIB(mng, mng_initialize,
	[ AC_CHECK_HEADER(libmng.h,
		[ have_libmng=yes
		  MNG_LIBS="-lmng" ], 
		AC_MSG_RESULT([*** All libmng dependent parts will be disabled ***]))],
	AC_MSG_RESULT([*** All libmng dependent parts will be disabled ***]))
  AC_SUBST(MNG_LIBS)
else
  have_libmng=no
fi
AM_CONDITIONAL(HAVE_LIBMNG, test x"$have_libmng" = "xyes")

dnl ---------------------------------------------
dnl MagickWand API of Imagemagick.
dnl ---------------------------------------------

AC_ARG_ENABLE([imagemagick],
  AC_HELP_STRING([--disable-imagemagick], [do not build ImageMagick support]),
  [with_imagemagick=$enableval], [with_imagemagick=yes])

if test "x$with_imagemagick" = "xyes"; then
  AC_PATH_TOOL(WAND_CONFIG, Wand-config, no)
  if test "$WAND_CONFIG" = "no" ; then
    AC_MSG_RESULT([*** All Imagemagick dependent parts will be disabled, Wand-config not found. ***])
    have_imagemagick="no"
  else
    WAND_CFLAGS=`$WAND_CONFIG --cflags`
    WAND_CPPFLAGS=`$WAND_CONFIG --cppflags`
    WAND_LDFLAGS=`$WAND_CONFIG --ldflags`
    WAND_LIBS=`$WAND_CONFIG --libs`
    have_imagemagick="yes"
    AC_DEFINE(HAVE_WAND,1,[Define this if you have Imagemagick])
  fi
else
  have_imagemagick="no"
fi

AM_CONDITIONAL(HAVE_WAND, test x"$have_imagemagick" = "xyes" )
AC_SUBST(WAND_CFLAGS)
AC_SUBST(WAND_CPPFLAGS)
AC_SUBST(WAND_LDFLAGS)
AC_SUBST(WAND_LIBS)

dnl ---------------------------------------------
dnl freetype2 lib.
dnl ---------------------------------------------

AM_PATH_FREETYPE2()


dnl ---------------------------------------------
dnl OSS style audio interface
dnl ---------------------------------------------
AC_ARG_ENABLE([oss],
  AC_HELP_STRING([--disable-oss], [do not build OSS support]),
  [with_oss=$enableval], [with_oss=yes])

if test "x$with_oss" = "xyes"; then
  AC_MSG_CHECKING(for OSS audio support)
  have_ossaudio=no
  AC_TRY_COMPILE([
	    #ifdef __NetBSD__
	    #include <soundcard.h>
	    #else
	    #include <sys/soundcard.h>
	    #endif
	],[
	    int arg = SNDCTL_DSP_SETFRAGMENT;
	],[
	    have_ossaudio=yes
	])
  AC_MSG_RESULT($have_ossaudio)
else
  have_ossaudio=no
fi
AM_CONDITIONAL(HAVE_OSS, test x"$have_ossaudio" = "xyes")


dnl ---------------------------------------------
dnl Alsa support
dnl ---------------------------------------------

AM_PATH_ALSA(0.9.0rc4, 
        [ AC_DEFINE(HAVE_ALSA,1,[Define this if you have Alsa (libasound) installed])
          if test x"$have_alsa09" = "xyes"; then
            AC_DEFINE(HAVE_ALSA09,1,[Define this if you have ALSA 0.9.x or later installed])
          fi
        ],
	AC_MSG_RESULT(*** All of ALSA dependent parts will be disabled ***))
AM_CONDITIONAL(HAVE_ALSA, test x"$no_alsa" != "xyes")
AM_CONDITIONAL(HAVE_ALSA09, test x"$have_alsa09" = "xyes")

dnl ---------------------------------------------
dnl ESD support
dnl ---------------------------------------------

AC_ARG_ENABLE([esd],
  AC_HELP_STRING([--disable-esd], [do not build esd support]),
  [with_esd=$enableval], [with_esd=yes])

if test "x$with_esd" = "xyes"; then
  AM_PATH_ESD(0.2.8,
        AC_DEFINE(HAVE_ESD,1,[Define this if you have ESD (libesd) installed]),
        AC_MSG_RESULT(*** All of ESD dependent parts will be disabled ***))
else
  no_esd=yes
fi
AM_CONDITIONAL(HAVE_ESD, test x"$no_esd" != "xyes")


dnl ---------------------------------------------
dnl ARTS support
dnl ---------------------------------------------

AC_ARG_ENABLE([arts],
  AC_HELP_STRING([--disable-arts], [do not build arts support]),
  [with_arts=$enableval], [with_arts=yes])

if test "x$with_arts" = "xyes"; then
  AM_PATH_ARTS(0.9.5,
        AC_DEFINE(HAVE_ARTS,1,[Define this if you have ARTS (libartsc) installed]),
        AC_MSG_RESULT(*** All of ARTS dependent parts will be disabled ***))
else
  no_arts=yes
fi
AM_CONDITIONAL(HAVE_ARTS, test x"$no_arts" != "xyes")


dnl ---------------------------------------------
dnl FusionSound support
dnl ---------------------------------------------

AC_ARG_ENABLE([fusionsound],
  AC_HELP_STRING([--enable-fusionsound], [build with fusionsound support]),
  [with_fusionsound=$enableval], [with_fusionsound=no])

if test "x$with_fusionsound" = "xyes"; then
  PKG_CHECK_MODULES(FUSIONSOUND, fusionsound >= 0.9.23,
      AC_DEFINE(HAVE_FUSIONSOUND,1,[Define to 1 if you have FusionSound.]),
      AC_MSG_RESULT(*** All of FusionSound dependent parts will be disabled ***))
  AC_SUBST(FUSIONSOUND_CFLAGS)
  AC_SUBST(FUSIONSOUND_LIBS)
else
  no_fusionsound=yes
fi
AM_CONDITIONAL(HAVE_FUSIONSOUND, test x"$no_fusionsound" != "xyes")


dnl ---------------------------------------------
dnl gnome-vfs support
dnl ---------------------------------------------

AC_ARG_ENABLE([gnomevfs],
  AC_HELP_STRING([--disable-gnomevfs], [do not build gnome-vfs support]),
  [with_gnome_vfs=$enableval], [with_gnome_vfs=yes])

if test "x$with_gnome_vfs" = "xyes"; then
  PKG_CHECK_MODULES(GNOME_VFS, gnome-vfs-2.0,
                no_gnome_vfs=no,
		no_gnome_vfs=yes)
  AC_SUBST(GNOME_VFS_CFLAGS)
  AC_SUBST(GNOME_VFS_LIBS)
  if test x"$no_gnome_vfs" != "xyes"; then
    AC_DEFINE(HAVE_GNOME_VFS,1,[Define this if you have gnome-vfs installed])
  else
    AC_MSG_RESULT(*** All of the gnome-vfs dependent parts will be disabled ***)
  fi
else
  no_gnome_vfs=yes
fi
AM_CONDITIONAL(HAVE_GNOME_VFS, test x"$no_gnome_vfs" != "xyes")

dnl ---------------------------------------------
dnl gdk-pixbuf support
dnl ---------------------------------------------

AC_ARG_ENABLE([gdkpixbuf],
  AC_HELP_STRING([--disable-gdkpixbuf], [do not build gdk-pixbuf support]),
  [with_gdkpixbuf=$enableval], [with_gdkpixbuf=yes])

if test "x$with_gdkpixbuf" = "xyes"; then
  PKG_CHECK_MODULES(GDK_PIXBUF, gdk-pixbuf-2.0,
                no_gdkpixbuf=no,
		no_gdkpixbuf=yes)
  AC_SUBST(GDK_PIXBUF_CFLAGS)
  AC_SUBST(GDK_PIXBUF_LIBS)
  if test x"$no_gdkpixbuf" != "xyes"; then
    AC_DEFINE(HAVE_GDK_PIXBUF,1,[Define this if you have gdk-pixbuf installed])
  else
    AC_MSG_RESULT(*** All of the gdk-pixbuf dependent parts will be disabled ***)
  fi
else
  no_gdkpixbuf=yes
fi
AM_CONDITIONAL(HAVE_GDK_PIXBUF, test x"$no_gdkpixbuf" != "xyes")

dnl ---------------------------------------------
dnl libsmbclient support
dnl ---------------------------------------------

AC_ARG_ENABLE([samba],
  AC_HELP_STRING([--disable-samba], [do not build Samba support]),
  [with_samba=$enableval], [with_samba=yes])

if test "x$with_samba" = "xyes"; then
  AC_CHECK_LIB(smbclient, smbc_init,
	[ AC_CHECK_HEADER(libsmbclient.h,
		[ have_libsmbclient=yes
		  LIBSMBCLIENT_LIBS="-lsmbclient" ], 
		AC_MSG_RESULT([*** All libsmbclient dependent parts will be disabled ***]))],
	AC_MSG_RESULT([*** All libsmbclient dependent parts will be disabled ***]))
  AC_SUBST(LIBSMBCLIENT_LIBS)
fi
AM_CONDITIONAL(HAVE_LIBSMBCLIENT, test x"$have_libsmbclient" = "xyes")


dnl ---------------------------------------------
dnl SUN style audio interface
dnl ---------------------------------------------

AC_MSG_CHECKING(for Sun audio support)
have_sunaudio=no
AC_TRY_COMPILE([
	    #include <sys/types.h>
	    #include <sys/audioio.h>
	],[
	    audio_info_t audio_info;
	    AUDIO_INITINFO(&audio_info);
	],[
	    have_sunaudio=yes
	])
AC_MSG_RESULT($have_sunaudio)
AM_CONDITIONAL(HAVE_SUNAUDIO, test x"$have_sunaudio" = "xyes")


dnl ---------------------------------------------
dnl IRIX style audio interface
dnl ---------------------------------------------

AM_CHECK_IRIXAL([AC_DEFINE(HAVE_IRIXAL,1,
			[Define this if you have a usable IRIX al interface available])],
	[AC_MSG_RESULT([*** All of IRIX AL dependent parts will be disabled ***])])
AM_CONDITIONAL(HAVE_IRIXAL, [test "x$am_cv_have_irixal" = xyes])


dnl ---------------------------------------------
dnl Solaris kstat kernel statistics
dnl ---------------------------------------------

AC_CHECK_LIB(kstat, kstat_open,
	[KSTAT_LIBS=-lkstat
	 AC_DEFINE(HAVE_KSTAT,1,[Define this if you have kernel statistics available via kstat interface])])
AC_SUBST(KSTAT_LIBS)


dnl ---------------------------------------------
dnl cdrom ioctls
dnl ---------------------------------------------

AC_CHECK_HEADERS(sys/cdio.h linux/cdrom.h)
AM_CHECK_CDROM_IOCTLS(
	[AC_DEFINE(HAVE_CDROM_IOCTLS,1,[Define this if you have CDROM ioctls])],
	[AC_MSG_RESULT([*** (S)VCD support will be disabled ***])])
AM_CONDITIONAL(HAVE_CDROM_IOCTLS, [test x"$have_cdrom_ioctls" = "xyes"])


dnl ---------------------------------------------
dnl check for a usable version of libdvdnav
dnl ---------------------------------------------

AC_ARG_WITH(external-dvdnav, AC_HELP_STRING([--with-external-dvdnav], [use external dvdnav library (not recommended)]),
            [external_dvdnav="$withval"], [no_dvdnav="yes"; external_dvdnav="no"])

if test x"$external_dvdnav" = "xyes"; then
  AM_PATH_DVDNAV(0.1.9,
	  AC_DEFINE(HAVE_DVDNAV,1,[Define this if you have a suitable version of libdvdnav]),
	  [AC_MSG_RESULT([*** no usable version of libdvdnav found, using internal copy ***])])
else
  AC_MSG_RESULT([Use included DVDNAV support])
fi

AM_CONDITIONAL(HAVE_DVDNAV, [test x"$no_dvdnav" != "xyes"])


dnl ---------------------------------------------
dnl Video CD
dnl ---------------------------------------------

AC_ARG_ENABLE(vcd, AC_HELP_STRING([--disable-vcd], [do not compile VCD plugin]),
              enable_vcd=$enableval, enable_vcd=yes)

dnl Force build of both vcd plugins, for now.
dnl AC_ARG_ENABLE(vcdo, AC_HELP_STRING([--disable-vcdo], [do not compile old VCD plugin]),
dnl               enable_vcdo=$enableval, enable_vcdo=yes)
dnl
enable_vcdo="yes"

AC_ARG_WITH(internal-vcdlibs, AC_HELP_STRING([--with-internal-vcdlibs], [force using internal libcdio/libvcd/libvcdinfo]),
            [internal_vcdnav="$withval"], [internal_vcdnav="no"])

if test x"$enable_vcd" = "xyes"; then
  dnl empty_array_size
  AC_MSG_CHECKING([how to create empty arrays])

  empty_array_size="xxx"
  AC_TRY_COMPILE([],[struct { int foo; int bar[]; } doo;], empty_array_size="")

  if test "x$empty_array_size" = "xxxx";then
   AC_TRY_COMPILE([],[struct { int foo; int bar[0]; } doo;], empty_array_size="0")
  fi

  if test "x$empty_array_size" = "xxxx"
  then
    AC_MSG_ERROR([compiler is unable to creaty empty arrays])
  else
    AC_DEFINE_UNQUOTED(EMPTY_ARRAY_SIZE, $empty_array_size, 
	   [what to put between the brackets for empty arrays])
    changequote(`,')
    msg="[${empty_array_size}]"
    changequote([,])
  AC_MSG_RESULT($msg)
  fi
  dnl empty_array_size

  if test x"$internal_vcdnav" = "xno" && test x"$PKG_CONFIG" != "xno" ; then
    AC_MSG_CHECKING(for libcdio)
    if $PKG_CONFIG --atleast-version 0.71 libcdio; then
      AC_MSG_RESULT(found)
      LIBCDIO_CFLAGS=`$PKG_CONFIG --cflags libcdio`
      LIBCDIO_LIBS=`$PKG_CONFIG --libs libcdio`

      AC_MSG_CHECKING(for libvcdinfo)
      if $PKG_CONFIG --atleast-version 0.7.21 libvcdinfo; then
        AC_MSG_RESULT(found)
        LIBVCDINFO_CFLAGS=`$PKG_CONFIG --cflags libvcdinfo`
        LIBVCDINFO_LIBS=`$PKG_CONFIG --libs libvcdinfo`
	  AC_DEFINE(HAVE_VCDNAV,1,[Define this if you have a suitable version of libcdio/libvcd])
      else
        AC_MSG_RESULT([Use included libcdio/libvcdinfo support])
	internal_vcdnav="yes"
      fi

    else
      AC_MSG_RESULT([Use included libcdio/libvcdinfo support])
      internal_vcdnav="yes"
    fi
  else
    AC_MSG_RESULT([Use included libcdio/libvcdinfo support])
    internal_vcdnav="yes"
  fi

  dnl check twice, fallback is internal copy
  if test x"$internal_vcdnav" = "xyes"; then
    AC_DEFINE_UNQUOTED(HOST_ARCH, "$host_os/$host_cpu", [host os/cpu identifier])
    AC_CHECK_FUNCS(snprintf vsnprintf, , [AC_MSG_ERROR(required function not found)])

    AC_DEFINE(_DEVELOPMENT_, [], enable warnings about being development release)
    AC_DEFINE(_GNU_SOURCE, [], enable GNU libc extension)
    AC_STDC_HEADERS
    AC_CHECK_HEADERS(sys/stat.h stdint.h glob.h inttypes.h stdbool.h)

    if test "x$ac_cv_header_stdint_h" != "xyes" 
     then
       AC_CHECK_SIZEOF(int, 4)
       AC_CHECK_SIZEOF(long, 4)
       AC_CHECK_SIZEOF(long long, 8)
    fi

    dnl ISOC99_PRAGMA
    AC_MSG_CHECKING([whether $CC supports ISOC99 _Pragma()])
    AC_TRY_COMPILE([],[_Pragma("pack(1)")], [
      ISOC99_PRAGMA=yes 
      AC_DEFINE(HAVE_ISOC99_PRAGMA, [], [Supports ISO _Pragma() macro])
    ],ISOC99_PRAGMA=no)
    AC_MSG_RESULT($ISOC99_PRAGMA)

    dnl
    dnl bitfield order
    dnl
    AC_MSG_CHECKING([bitfield ordering in structs])

    dnl basic compile test for all platforms
    AC_COMPILE_IFELSE([
int main() {
  struct { char bit_0:1, bit_12:2, bit_345:3, bit_67:2; }
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
  __attribute__((packed))
#endif
  bf = { 1,1,1,1 };
  switch (0) case 0: case sizeof(bf) == 1:;
  return 0;
}
], [], AC_MSG_ERROR([compiler doesn't support bitfield structs]))


    dnl run test
    AC_RUN_IFELSE([
int main() {
  struct { char bit_0:1, bit_12:2, bit_345:3, bit_67:2; }
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
  __attribute__((packed))
#endif
  bf = { 1,1,1,1 };
  if (sizeof (bf) != 1) return 1;
  return *((unsigned char*) &bf) != 0x4b; }
], bf_lsbf=1, [
  AC_RUN_IFELSE([
int main() {
  struct { char bit_0:1, bit_12:2, bit_345:3, bit_67:2; }
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
  __attribute__((packed))
#endif
 bf = { 1,1,1,1 };
 if (sizeof (bf) != 1) return 1;
 return *((unsigned char*) &bf) != 0xa5; }
], bf_lsbf=0, AC_MSG_ERROR([unsupported bitfield ordering]))
  ],
  [case "$host" in
     *-*-mingw32* | *-*-cygwin*)
       bf_lsbf=1
     ;;
     *)
       AC_MSG_RESULT([unknown])
       AC_MSG_ERROR([value of bitfield test isn't known for $host
*********************************************************************
Value of bitfield test can't be found out for cross-compiling and we
don't know its value for host "$host".

Because it's needed for VCD plugin, disable VCD by configure option
--disable-vcd or use external VCD library.
*********************************************************************])
  esac]
)

    if test "x$cross_compiling" = "xyes"; then
      TEXT=" (guessed)"
    else
      TEXT=""
    fi
    if test "x$bf_lsbf" = "x1"; then
     AC_MSG_RESULT(LSBF${TEXT})
     AC_DEFINE(BITFIELD_LSBF, [], [compiler does lsbf in struct bitfields])
    else
     AC_MSG_RESULT(MSBF${TEXT})
    fi

    AC_HAVE_HEADERS( errno.h fcntl.h \
		     stdbool.h  stdlib.h stdint.h stdio.h string.h \
		     strings.h linux/version.h sys/cdio.h sys/stat.h \
		     sys/types.h )

    LIBCDIO_CFLAGS='-I$(top_srcdir)/src/input/vcd/libcdio'
    LIBCDIO_LIBS='$(top_builddir)/src/input/vcd/libcdio/libcdio.la'
    LIBISO9660_LIBS='$(top_builddir)/src/input/vcd/libcdio/libiso9660.la'
    LIBVCD_CFLAGS='-I$(top_srcdir)/src/input/vcd/libvcd'
    LIBVCD_LIBS='$(top_builddir)/src/input/vcd/libvcd/libvcd.la'
    LIBVCDINFO_LIBS='$(top_builddir)/src/input/vcd/libvcd/libvcdinfo.la'

    case $host_os in
         darwin*)
           AC_CHECK_HEADERS(IOKit/IOKitLib.h CoreFoundation/CFBase.h, 
                            [have_iokit_h="yes"])
           if test "x$have_iokit_h" = "xyes" ; then 
             AC_DEFINE([HAVE_DARWIN_CDROM], [1],
	          [Define 1 if you have Darwin OS X-type CD-ROM support])
           fi
         ;;
         linux*)
            AC_CHECK_HEADERS(linux/version.h)
            AC_CHECK_HEADERS(linux/cdrom.h, [have_linux_cdrom_h="yes"])
	    if test "x$have_linux_cdrom_h" = "xyes" ; then
	       AC_TRY_COMPILE(,[
#include <linux/cdrom.h>
struct cdrom_generic_command test;
int has_timeout=sizeof(test.timeout);],
                [AC_DEFINE([HAVE_LINUX_CDROM_TIMEOUT], [1], 
                       [Define 1 if timeout is in cdrom_generic_command struct])])
	       AC_DEFINE([HAVE_LINUX_CDROM], [1], 
                       [Define 1 if you have Linux-type CD-ROM support])
	    fi
	 ;;
         bsdi*)
            AC_CHECK_HEADERS(dvd.h, [have_bsdi_dvd_h="yes"])
	    if test "x$have_bsdi_dvd_h" = "xyes" ; then
     	      AC_DEFINE([HAVE_BSDI_CDROM], [1], 
                        [Define 1 if you have BSDI-type CD-ROM support])
	    fi
	 ;;
         sunos*|sun*|solaris*)
            AC_CHECK_HEADERS(sys/cdio.h)
     	    AC_DEFINE([HAVE_SOLARIS_CDROM], [1], 
                      [Define 1 if you have Solaris CD-ROM support])
	 ;;
         cygwin*)
	    AC_DEFINE([CYGWIN], [1], 
                      [Define 1 if you are compiling using cygwin])
     	    AC_DEFINE([HAVE_WIN32_CDROM], [1], 
                      [Define 1 if you have MinGW CD-ROM support])
	    LIBCDIO_LIBS="$LIBCDIO_LIBS -lwinmm"
	    LIBVCD_LIBS="$LIBVCD_LIBS -lwinmm"
	 ;;
         mingw*)
	    AC_DEFINE([MINGW32], [1], 
                      [Define 1 if you are compiling using MinGW])
     	    AC_DEFINE([HAVE_WIN32_CDROM], [1], 
                      [Define 1 if you have MinGW CD-ROM support])
	 ;;
         freebsd4.*)
     	    AC_DEFINE([HAVE_FREEBSD_CDROM], [1], 
                      [Define 1 if you have FreeBSD CD-ROM support])
	 ;;
         *)
	    AC_MSG_WARN(Don't have OS CD-reading support for ${host_os}...)
	    AC_MSG_WARN(Will use generic support.)
	 ;;
    esac
    AC_SUBST(LINUX_CDROM_TIMEOUT)
    AC_SUBST(HAVE_BSDI_CDROM)
    AC_SUBST(HAVE_DARWIN_CDROM)
    AC_SUBST(HAVE_FREEBSD_CDROM)
    AC_SUBST(HAVE_LINUX_CDROM)
    AC_SUBST(HAVE_SOLARIS_CDROM)
    AC_SUBST(HAVE_WIN32_CDROM)
    AC_SUBST(LINUX_CDROM_TIMEOUT)
    AC_SUBST(LIBVCD_SYSDEP)

    AC_CHECK_FUNCS( bzero memcpy )

    AC_CHECK_MEMBER([struct tm.tm_gmtoff],
                    [AC_DEFINE(HAVE_TM_GMTOFF, 1,
                           [Define if struct tm has the tm_gmtoff member.])],
                     ,
                     [#include <time.h>])
  fi
fi

AC_DEFINE([LIBCDIO_CONFIG_H], 1, [This defined to get of rid system libcdio build configuration])
AC_SUBST(LIBCDIO_CFLAGS)
AC_SUBST(LIBCDIO_LIBS)
AC_SUBST(LIBISO9660_LIBS)
AC_SUBST(LIBVCD_CFLAGS)
AC_SUBST(LIBVCD_LIBS)
AC_SUBST(LIBVCDINFO_LIBS)
AM_CONDITIONAL(HAVE_VCDNAV, [test x"$internal_vcdnav" = "xno"])
AM_CONDITIONAL(ENABLE_VCD, [test x"$enable_vcd" = "xyes"])


dnl ---------------------------------------------
dnl ASF build can be optional
dnl ---------------------------------------------

AC_ARG_ENABLE(asf, AC_HELP_STRING([--disable-asf], [do not build ASF demuxer]),
	      enable_asf=$enableval, enable_asf=yes)
AM_CONDITIONAL(BUILD_ASF, test x"$enable_asf" = "xyes")


dnl ---------------------------------------------
dnl FAAD build can be optional
dnl ---------------------------------------------

AC_ARG_ENABLE(faad, AC_HELP_STRING([--disable-faad], [do not build FAAD decoder]),
	      enable_faad=$enableval, enable_faad=yes)
AM_CONDITIONAL(BUILD_FAAD, test x"$enable_faad" = "xyes")

dnl ---------------------------------------------
dnl Optional and external libdts
dnl ---------------------------------------------

AC_ARG_ENABLE(dts, AC_HELP_STRING([--disable-dts], [Disable support for DTS decoding library (default: enabled)]),
              [enable_libdts="$enableval"], [enable_libdts="yes"])
AC_ARG_WITH(external-libdts, AC_HELP_STRING([--with-external-libdts], [use external libdts library (not recommended)]),
            [external_libdts="$withval"], [external_libdts="no"])

have_dts="no"

if test "x$enable_libdts" = "xno"; then
  AC_MSG_RESULT([libdts support disabled])
elif test x"$external_libdts" = "xyes"; then
  PKG_CHECK_MODULES(LIBDTS, [libdts], have_dts=yes, have_dts=no)
  AC_CHECK_HEADERS([dts.h])
  AC_SUBST(LIBDTS_LIBS)
  AC_SUBST(LIBDTS_CFLAGS)
  if test "x$have_dts" = "xno"; then
    AC_MSG_RESULT([*** no usable version of libdts found, using internal copy ***])
  fi
else
  AC_MSG_RESULT([Use included libdts support])
fi

AM_CONDITIONAL(DTS, test "x$enable_libdts" = "xyes")
AM_CONDITIONAL(EXTERNAL_LIBDTS, test x"$have_dts" = "xyes")

dnl ---------------------------------------------
dnl libmodplug support 
dnl ---------------------------------------------
AC_ARG_ENABLE([modplug],
  AS_HELP_STRING([--enable-modplug], [Enable modplub support]) )

if test "x$enable_modplug" != "xno"; then
  PKG_CHECK_MODULES([LIBMODPLUG], [libmodplug >= 0.7],
    AC_DEFINE([HAVE_MODPLUG], 1, [define this if you have libmodplug installed]),
    [enable_modplug=no])
fi

AC_SUBST(LIBMODPLUG_CFLAGS)
AC_SUBST(LIBMODPLUG_LIBS)
dnl AM_CONDITIONAL(HAVE_MODPLUG, [test x"$have_modplug" = x"yes"])


dnl ---------------------------------------------
dnl Win32 DLL codecs
dnl ---------------------------------------------
AC_ARG_ENABLE([w32dll],
  AC_HELP_STRING([--disable-w32dll], [Disable Win32 DLL support]),
  enable_w32dll=$enableval, [
    AC_MSG_CHECKING([whether to enable Win32 DLL support])
    case "$host_or_hostalias" in
      *-mingw* | *-cygwin)
        enable_w32dll="no"
        ;;
      i?86-* | k?-* | athlon-* | pentium*-)
        enable_w32dll="yes"
        ;;
      *)
        enable_w32dll="no"
    esac
    AC_MSG_RESULT([$enable_w32dll])])
AM_CONDITIONAL(HAVE_W32DLL, test x"$enable_w32dll" = "xyes")
if test x"$enable_w32dll" = "xyes"; then
   W32DLL_DEP=""
else 
   W32DLL_DEP="#"
fi
AC_SUBST(W32DLL_DEP)


dnl ---------------------------------------------
dnl ip_mreqn
dnl ---------------------------------------------

AC_CHECK_IP_MREQN
if test x"$have_ip_mreqn" = "xyes"; then
  AC_DEFINE(HAVE_IP_MREQN,1,[Define this if you have ip_mreqn in netinet/in.h])
fi


dnl ---------------------------------------------
dnl Using or not using -fPIC (override default behavior - system dependent)
dnl ---------------------------------------------

AC_ARG_ENABLE(fpic, AC_HELP_STRING([--disable-fpic], [disable -fPIC on shared libs (default on x86)]),
   enable_fpic=$enableval)


dnl ---------------------------------------------
dnl Some extra checks.
dnl ---------------------------------------------

AC_SYS_LARGEFILE
AC_CHECK_LIB(posix4, sched_get_priority_min)
AC_CHECK_FUNCS([_vsnprintf _snprintf _stricmp _strnicmp vsscanf sigaction sigset getpwuid_r nanosleep lstat memset readlink strchr strcasecmp strncasecmp])
AC_FUNC_FSEEKO
AC_DEFINE(_GNU_SOURCE)
AC_CHECK_HEADERS(assert.h byteswap.h malloc.h execinfo.h ucontext.h sys/mman.h sys/mixer.h libgen.h netdb.h dirent.h sys/times.h sys/ioctl.h sys/param.h)
AC_REPLACE_FUNCS(basename gettimeofday setenv strndup strpbrk strsep strtok_r timegm unsetenv)

AC_LIBSOURCE(hstrerror.c)
AC_LINK_IFELSE([#include <netdb.h>
int main(void) {
  hstrerror(0);
}], ac_cv_function_system_hstrerror="yes")
AC_CHECK_LIB([resolv], [hstrerror], [ac_cv_function_system_hstrerror="yes"])
if test x"$ac_cv_function_system_hstrerror" = "xyes"; then
  AC_DEFINE(HAVE_HSTRERROR, 1, [Define to 1 if you have 'hstrerror' in <netdb.h>])
else
  AC_LIBOBJ(hstrerror)
fi

AC_LIBSOURCE(dirent_msvc.c)
AC_CHECK_FUNC(opendir,
  [AC_DEFINE(HAVE_OPENDIR, 1, [Define to 1 if you have 'opendir' function])],
  [if test x"$SYS" = "xmingw32"; then
    AC_LIBOBJ(dirent_msvc)
  else
    AC_MSG_ERROR([dirent is needed (opendir, readdir, ...)])
  fi])

AH_BOTTOM([/* include internal system specific header */
#include "os_internal.h"])


dnl ---------------------------------------------
dnl cflags and debug cflags
dnl ---------------------------------------------
 
AC_SUBST(DEBUG_CFLAGS)
DEBUG_CFLAGS="-g -DDEBUG $CFLAGS"

dnl dummy
ASFLAGS="$ASFLAGS"
AC_SUBST(ASFLAGS)

dnl check for __attribute__ ((aligned ()))
dnl WARNING! Do not move this check behind the $(MULTIPASS_CFLAGS)!
dnl (the alignment test macro does not like variables in the CFLAGS)
AC_C_ATTRIBUTE_ALIGNED

dnl Common cflags for all platforms
CFLAGS="-DNDEBUG -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DXINE_COMPILE \$(MULTIPASS_CFLAGS) $CFLAGS"
DEBUG_CFLAGS="-D_REENTRANT -D_FILE_OFFSET_BITS=64 -DXINE_COMPILE $DEBUG_CFLAGS"

AC_OPTIMIZATIONS

enable_ffmmx="no"
enable_armv4l="no"

case "$host_or_hostalias" in
  i?86-* | k?-* | athlon-* | pentium*-)
    dnl enable x86 specific parts of the code
    dnl
    dnl all of this stuff needs gcc/gas; it uses gnu c compiler extensions
    dnl like the extended asm() or __attribute(__cdecl__), or other direct
    dnl mmx/sse/3dnow assembler instructions. 
    dnl
    AC_DEFINE_UNQUOTED(ARCH_X86,,[Define this if you're running x86 architecture])
    AC_DEFINE(FPM_INTEL,1,[Define to select libmad fixed point arithmetic implementation])
    enable_ffmmx="yes"

    case "$host_or_hostalias" in
      *-*-cygwin) 
        LIBS="$LIBS @INTLLIBS@ -lkernel32"
        ;;
    esac
    ;;
  x86_64-*)
    AC_DEFINE_UNQUOTED(ARCH_X86_64,,[Define this if you're running x86 architecture])
    AC_DEFINE(FPM_64BIT,1,[Define to select libmad fixed point arithmetic implementation])
    enable_ffmmx="yes"
    ;;
  *darwin*)
    HOST_OS_DARWIN=1
    AC_DEFINE_UNQUOTED(HOST_OS_DARWIN, 1, [Define this if built on Mac OS X/Darwin])
    
    dnl avoid ppc compilation crash
    AS="$CC"
    AC_DEFINE_UNQUOTED(FPM_PPC,,[Define to select libmad fixed point arithmetic implementation])
    AC_DEFINE_UNQUOTED(ARCH_PPC,,[Define this if you're running PowerPC architecture])

    OBJC=${CC:-gcc}
    AC_SUBST(OBJC)
    OBJCFLAGS="-D_INTL_REDIRECT_MACROS $CFLAGS $OBJCFLAGS"
    AC_SUBST(OBJCFLAGS)
    OBJCDEPMODE="depmode=gcc3"
    dnl Do not use AC_SUBST(OBJCDEPMODE): we don't need it as long we use
    dnl _AM_DEPENDENCIES (below), and doing the AC_SUBST elicits a warning
    dnl from automake 1.6.
    
    if test x$enable_altivec = xyes; then
        AC_DEFINE_UNQUOTED(ENABLE_ALTIVEC,,[Define this if you want to use altivec on PowerPC CPUs])
        CFLAGS="$CFLAGS -faltivec"
        LIBMPEG2_CFLAGS="$LIBMPEG2_CFLAGS -force_cpusubtype_ALL -faltivec"
    fi
    ;;
  ppc-*-linux* | powerpc-*) 
    dnl avoid ppc compilation crash
    AS="$CC"
    AC_DEFINE_UNQUOTED(FPM_PPC,,[Define to select libmad fixed point arithmetic implementation])
    AC_DEFINE_UNQUOTED(ARCH_PPC,,[Define this if you're running PowerPC architecture])
    
    if test x$enable_altivec = xyes; then
        AC_DEFINE_UNQUOTED(ENABLE_ALTIVEC,,[Define this if you have a Motorola 74xx CPU])
    fi
    ;;
  sparc*-*-linux*)
    if test x$enable_vis = xyes; then
      AC_DEFINE_UNQUOTED(ENABLE_VIS,,[Define this if you have Sun UltraSPARC CPU])
    fi

    AC_DEFINE_UNQUOTED(FPM_SPARC,,[Define to select libmad fixed point arithmetic implementation])
    AC_DEFINE_UNQUOTED(ARCH_SPARC,,[Define this if you're running SPARC architecture])
    ;;
  sparc-*-solaris*)
    if test "$GCC" = yes; then
      case `$CC --version 2>/dev/null` in
	1.*|2.*) ;;
        *)
          if test x$enable_vis = xyes; then
            AC_DEFINE_UNQUOTED(ENABLE_VIS,,[Define this if you have Sun UltraSPARC CPU])
          fi
	  ;;
      esac
  
      AC_DEFINE_UNQUOTED(FPM_SPARC,,[Define to select libmad fixed point arithmetic implementation])
      AC_DEFINE_UNQUOTED(ARCH_SPARC,,[Define this if you're running SPARC architecture])
    else
      AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed point arithmetic implementation])
    fi
    ;;
  mips-*)
    AC_DEFINE_UNQUOTED(FPM_MIPS,,[Define to select libmad fixed point arithmetic implementation])
    ;;
  alphaev56-* | alpha* | ia64-* | hppa*-linux-*)
    AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed point arithmetic implementation])
    ;;
  armv4l-*-linux*)
    AC_DEFINE_UNQUOTED(FPM_ARM,,[Define to select libmad fixed point arithmetic implementation])
    enable_armv4l="yes"
    ;;
  *)
    AC_DEFINE_UNQUOTED(FPM_DEFAULT,,[Define to select libmad fixed point arithmetic implementation])
    ;;
esac

AM_CONDITIONAL(HOST_OS_DARWIN, test x"$HOST_OS_DARWIN" = "x1")

AM_CONDITIONAL(HAVE_FFMMX, test x"$enable_ffmmx" = "xyes")

dnl ---------------------------------------------
dnl Build xine plugins with only the "xine_plugin_info" symbol exported
dnl (let the user disable this feature, in case libtool does not offer this
dnl feature on the target platform)
dnl ---------------------------------------------

AC_ARG_ENABLE(min-symtab,
   AC_HELP_STRING([--disable-min-symtab], [export all global symbols from xine plugins]),
   [
     if test "x$enableval" = "xno" ; then
       XINE_PLUGIN_MIN_SYMS=""
     else
       XINE_PLUGIN_MIN_SYMS="-export-symbols-regex xine_plugin_info"
     fi
   ],
   XINE_PLUGIN_MIN_SYMS="-export-symbols-regex xine_plugin_info")

if test "$with_gnu_ld" = yes; then
   dnl
   dnl -export-symbols-regex in libtool 1.4.X is broken, when GNU ld is
   dnl used for linking.  libtool / gnu ld does not touch the dynamic
   dnl symbol table, but removes the unwanted symbols from the
   dnl symbol table => The debug versions of the the xine-lib plugins
   dnl cannot be debugged.
   dnl
   dnl Don't use libtool's -export-symbols-regex on such a platform for now
   dnl
   XINE_PLUGIN_MIN_SYMS=""
fi

AC_SUBST(XINE_PLUGIN_MIN_SYMS)



dnl ---------------------------------------------
dnl HAVE_ARMV4L is currently used in libavcodec makefile.am
dnl ---------------------------------------------

AM_CONDITIONAL(HAVE_ARMV4L, test x"$enable_armv4l" = "xyes")


dnl ---------------------------------------------
dnl XINE_ROOTDIR does not work if architecture independent files are 
dnl installed to another place than architecture dependent files !!!
dnl ---------------------------------------------

if test "x$prefix" = xNONE; then
   prefix="${ac_default_prefix}"
fi
if test "x$exec_prefix" = xNONE; then
   exec_prefix='${prefix}'
fi

dnl
dnl installation directories and directories relative to prefix
dnl
dnl Note:
dnl   use AC_DEFINE for runtime
dnl   use AC_SUBST for installation
dnl

makeexpand () {
  local i
  local j
  i="$1"
  while test "$i" != "$j"; do j="$i"; eval i="$j"; done
  echo "$i"
}

XINE_PLUGINDIR="$libdir/xine/plugins/$XINE_MAJOR.$XINE_MINOR.$XINE_SUB"
XINE_FONTDIR="${datadir}/xine/libxine$XINE_MAJOR/fonts"
XINE_LOCALEDIR="${datadir}/locale"
XINE_REL_PLUGINDIR="`makeexpand "$XINE_PLUGINDIR"`"
XINE_REL_PLUGINDIR="`makeexpand "$XINE_REL_PLUGINDIR" | sed -e "s,^${prefix}/,,"`"
XINE_REL_FONTDIR="`makeexpand "$XINE_FONTDIR" | sed -e "s,^${prefix}/,,"`"
XINE_REL_LOCALEDIR="`makeexpand "$XINE_LOCALEDIR" | sed -e "s,^${prefix}/,,"`"
if test x"$SYS" = "xmingw32" -o x"$SYS" = "xcygwin"; then
  dnl polish paths (MinGW runtime accepts both \ and / anyway)
  XINE_REL_PLUGINDIR="`echo "$XINE_REL_PLUGINDIR" | sed -e 's/\\//\\\\\\\\/g'`"
  XINE_REL_FONTDIR="`echo "$XINE_REL_FONTDIR" | sed -e 's/\\//\\\\\\\\/g'`"
  XINE_REL_LOCALEDIR="`echo "$XINE_REL_LOCALEDIR" | sed -e 's/\\//\\\\\\\\/g'`"
  dnl prefix in xine-config
  XINE_CONFIG_PREFIX="\`dirname \$0\`/.."
  dnl installation directories (in xine-config)
  XINE_PLUGINPATH="$XINE_CONFIG_PREFIX/$XINE_REL_PLUGINDIR"
  XINE_FONTPATH="$XINE_CONFIG_PREFIX/$XINE_REL_FONTDIR"
  XINE_LOCALEPATH="$XINE_CONFIG_PREFIX/$XINE_REL_LOCALEDIR"
  dnl runtime directories
  AC_DEFINE(XINE_PLUGINDIR,[xine_get_plugindir()],[Define this to plugins directory location])
  AC_DEFINE(XINE_FONTDIR,[xine_get_fontdir()],[Define this to osd fonts dir location])
  AC_DEFINE(XINE_LOCALEDIR,[xine_get_localedir()],[Path where catalog files will be.])
else
  dnl prefix in xine-config
  XINE_CONFIG_PREFIX="`makeexpand "${prefix}"`"
  dnl directories from xine-config and runtime directories
  XINE_PLUGINPATH="`makeexpand "$XINE_PLUGINDIR"`"
  XINE_FONTPATH="`makeexpand "$XINE_FONTDIR"`"
  XINE_LOCALEPATH="`makeexpand "$XINE_LOCALEDIR"`"
  dnl defining runtime directories
  AC_DEFINE_UNQUOTED(XINE_PLUGINDIR,"$XINE_PLUGINPATH",[Define this to plugins directory location])
  AC_DEFINE_UNQUOTED(XINE_FONTDIR,"$XINE_FONTPATH",[Define this to osd fonts dir location])
  AC_DEFINE_UNQUOTED(XINE_LOCALEDIR, "$XINE_LOCALEPATH",[Path where catalog files will be.])
fi
AC_DEFINE_UNQUOTED(XINE_REL_PLUGINDIR,"$XINE_REL_PLUGINDIR",[Define this to plugin directory relative to execution prefix])
AC_DEFINE_UNQUOTED(XINE_REL_FONTDIR,"$XINE_REL_FONTDIR",[Define this to font directory relative to prefix])
AC_DEFINE_UNQUOTED(XINE_REL_LOCALEDIR,"$XINE_REL_LOCALEDIR",[Define this to font directory relative to prefix])
AC_SUBST(XINE_CONFIG_PREFIX)
AC_SUBST(XINE_PLUGINPATH)
AC_SUBST(XINE_FONTPATH)
AC_SUBST(XINE_LOCALEPATH)
AC_SUBST(XINE_PLUGINDIR)
AC_SUBST(XINE_FONTDIR)
AC_SUBST(XINE_LOCALEDIR)

dnl Where aclocal m4 files should be installed
XINE_ACFLAGS="-I `makeexpand "${datarootdir:-${datadir}}/aclocal"`"
AC_DEFINE_UNQUOTED(XINE_ACFLAGS, "$XINE_ACFLAGS", [Path where aclocal m4 files will be.])
AC_SUBST(XINE_ACFLAGS)

dnl Where architecture independent data (e.g. logo) will/should be installed
XINE_DATADIR="`makeexpand "${datarootdir:-${datadir}}/xine"`"
AC_SUBST(XINE_DATADIR)

dnl Where scripts will/should be installed.
eval XINE_SCRIPTPATH="$XINE_DATADIR/xine/scripts"
AC_SUBST(XINE_SCRIPTPATH)


dnl ---------------------------------------------
dnl Some informations about xine-lib compilation
dnl ---------------------------------------------

XINE_BUILD_CC="`$CC -v 2>&1 | tail -n 1`"
XINE_BUILD_OS="`uname -s -r -m`"
XINE_BUILD_DATE="`date \"+%a %d %b %Y %T\"`"
AC_SUBST(XINE_BUILD_CC)
AC_SUBST(XINE_BUILD_OS)
AC_SUBST(XINE_BUILD_DATE)


dnl ---------------------------------------------
dnl For win32 libraries location, needed by libw32dll.
dnl ---------------------------------------------

AC_ARG_WITH(w32-path, AC_HELP_STRING([--with-w32-path=path], [location of WIN32 libraries]),
            w32_path="$withval", w32_path="/usr/lib/codecs")
AC_SUBST(w32_path)


dnl ---------------------------------------------
dnl some include paths ( !!! DO NOT REMOVE !!! )
dnl ---------------------------------------------

INCLUDES='-I$(top_srcdir) -I$(top_builddir)/include -I$(top_srcdir)/include -I$(top_srcdir)/src -I$(top_builddir)/src/xine-engine -I$(top_srcdir)/src/xine-engine -I$(top_srcdir)/src/xine-utils $(INTLDIR) -I$(top_builddir)/src/input -I$(top_srcdir)/src/input $(WIN32_CPPFLAGS) -I$(top_builddir)/lib -I$(top_srcdir)/lib'
AC_SUBST(INCLUDES)


dnl ---------------------------------------------
dnl Get where .m4 should be installed.
dnl ---------------------------------------------

dnl if test x"${ACLOCAL_DIR+set}" != xset; then
dnl   case "`id`" in
dnl     uid=0\(* )
dnl       AC_MSG_CHECKING(for aclocal directory)
dnl       if (aclocal --version) < /dev/null > /dev/null 2>&1; then
dnl         ACLOCAL_DIR="`eval $ACLOCAL --print-ac-dir`"
dnl         AC_MSG_RESULT($ACLOCAL_DIR)
dnl       else
dnl         ACLOCAL_DIR="${prefix}/share/aclocal"
dnl         AC_MSG_RESULT(none - will be installed in $ACLOCAL_DIR)
dnl       fi
dnl       escapedprefix="`echo $prefix | sed -e 's/\\//\\\\\//g'`"
dnl       ACLOCAL_DIR="`echo $ACLOCAL_DIR|sed -e 's/^'$escapedprefix/'\${prefix}'/`"
dnl       ;;
dnl   esac
dnl fi
AC_SUBST(ACLOCAL_DIR)
AM_CONDITIONAL([INSTALL_M4],[test x"$ACLOCAL_DIR" != "x"])


dnl ---------------------------------------------
dnl It seems automake 1.5 don't take care about this script
dnl ---------------------------------------------

if test ! -z "$am_depcomp"; then
	DEPCOMP="depcomp"
fi
AC_SUBST(DEPCOMP)


dnl ---------------------------------------------
dnl Check for documentation formatting tool
dnl ---------------------------------------------

AC_CHECK_PROG(SGMLTOOLS, sgmltools, sgmltools, no)
AM_CONDITIONAL([HAVE_SGMLTOOLS], [test "$SGMLTOOLS" != "no"])

AC_CHECK_PROG(FIG2DEV, fig2dev, fig2dev, no)
AM_CONDITIONAL([HAVE_FIG2DEV], [test "$FIG2DEV" != "no"])

dnl ---------------------------------------------
dnl Newest automake workaround
dnl ---------------------------------------------

AC_SUBST(mkdir_p)
if test -n "$ac_aux_dir"; then
  case "$ac_aux_dir" in
    /*) MKINSTALLDIRS="$ac_aux_dir/install-sh -d" ;;
    *) MKINSTALLDIRS="\$(top_builddir)/$ac_aux_dir/install-sh -d" ;;
  esac
else
  MKINSTALLDIRS="\$(top_srcdir)/install-sh -d"
fi
AC_SUBST(MKINSTALLDIRS)

dnl this is an internal function we should not use, but
dnl as long as neither autoconf nor automake offer an A[CM]_PROG_OBJC
dnl check we will have to call it
_AM_DEPENDENCIES([OBJC])

AM_CONDITIONAL([BUILD_DMX_IMAGE], [test x"$have_imagemagick" = "xyes" -o x"$no_gdkpixbuf" != "xyes"])


dnl ---------------------------------------------
dnl Output configuration files
dnl ---------------------------------------------

AC_CONFIG_FILES([
Makefile
doc/Makefile
doc/man/Makefile
doc/man/en/Makefile
doc/hackersguide/Makefile
doc/faq/Makefile
include/Makefile
include/xine.h
lib/Makefile
m4/Makefile
misc/Makefile
misc/SlackBuild
misc/build_rpms.sh
misc/fonts/Makefile
misc/libxine.pc
misc/relchk.sh
misc/xine-config
misc/xine-lib.spec
po/Makefile.in
src/Makefile
src/audio_out/Makefile
src/demuxers/Makefile
src/dxr3/Makefile
src/input/Makefile
src/input/libdvdnav/Makefile
src/input/dvb/Makefile
src/input/librtsp/Makefile
src/input/libreal/Makefile
src/input/vcd/Makefile
src/input/vcd/libcdio/Makefile
src/input/vcd/libcdio/cdio/Makefile
src/input/vcd/libcdio/MSWindows/Makefile
src/input/vcd/libcdio/image/Makefile
src/input/vcd/libvcd/Makefile
src/input/vcd/libvcd/libvcd/Makefile
src/liba52/Makefile
src/libdts/Makefile
src/libfaad/Makefile
src/libfaad/codebook/Makefile
src/libffmpeg/Makefile
src/libffmpeg/libavcodec/Makefile
src/libffmpeg/libavcodec/armv4l/Makefile
src/libffmpeg/libavcodec/i386/Makefile
src/libffmpeg/libavcodec/mlib/Makefile
src/libffmpeg/libavcodec/alpha/Makefile
src/libffmpeg/libavcodec/ppc/Makefile
src/libffmpeg/libavcodec/sparc/Makefile
src/libffmpeg/libavcodec/libpostproc/Makefile
src/libffmpeg/libavutil/Makefile
src/libflac/Makefile
src/liblpcm/Makefile
src/libmad/Makefile
src/libmpeg2/Makefile
src/libmusepack/Makefile
src/libmusepack/musepack/Makefile
src/libspudec/Makefile
src/libspucc/Makefile
src/libspucmml/Makefile
src/libspudvb/Makefile
src/libsputext/Makefile
src/libvorbis/Makefile
src/libtheora/Makefile
src/libspeex/Makefile
src/libw32dll/Makefile
src/libw32dll/wine/Makefile
src/libw32dll/DirectShow/Makefile
src/libw32dll/dmo/Makefile
src/libw32dll/qtx/Makefile
src/libw32dll/qtx/qtxsdk/Makefile
src/libxinevdec/Makefile
src/libxineadec/Makefile
src/libxineadec/gsm610/Makefile
src/libxineadec/nosefart/Makefile
src/libreal/Makefile
src/post/Makefile
src/post/planar/Makefile
src/post/goom/Makefile
src/post/mosaico/Makefile
src/post/visualizations/Makefile
src/post/audio/Makefile
src/post/deinterlace/Makefile
src/post/deinterlace/plugins/Makefile
src/video_out/Makefile
src/video_out/libdha/Makefile
src/video_out/libdha/bin/Makefile
src/video_out/libdha/kernelhelper/Makefile
src/video_out/libdha/oth/Makefile
src/video_out/libdha/sysdep/Makefile
src/video_out/macosx/Makefile
src/video_out/vidix/Makefile
src/video_out/vidix/drivers/Makefile
src/xine-utils/Makefile
src/xine-engine/Makefile
win32/Makefile
win32/include/Makefile])
AC_CONFIG_COMMANDS([default],[[chmod +x ./misc/SlackBuild ./misc/build_rpms.sh ./misc/relchk.sh]],[[]])
AC_OUTPUT


dnl ---------------------------------------------
dnl Hack the libtool script (if required).
dnl
dnl If user intentionnaly overrided detection, use wish.
dnl ---------------------------------------------

if test x"$enable_fpic" != x; then
   case "${enable_fpic}" in
     yes)
       no_fpic="no"
       ;;
     *)
       no_fpic="yes"
       ;;
   esac
fi

dnl made libtool define BUILD_BASENAME
dnl cat libtool | sed -e 's/s/command\=\"\$base_compile\ \$srcfile/command\=\"\$base_compile\ \$srcfile\ \-DBUILD_BASENAME\=\`echo\ \$srcfile\ \|\ sed\ \-e\ '\''s\/\\.\[csS\]\/\/g'\''\`/g' > libtool.tmp
dnl cat libtool.tmp > libtool
dnl chmod +x libtool

dnl make libtool-nofpic silent and not installing plugins's .la files
cat libtool | sed -e 's/^.*echo \"---------.*$/exit 0/;s/# Install the pseudo-library.*/continue/' > libtool.tmp

dnl Remove fPIC flag, if necessary
if test x$no_fpic = "xyes"; then
  cat libtool.tmp | sed -e 's/^pic_flag=/#pic_flag=/;s/^ *pic_mode=.*$/pic_mode=no/' > libtool-nofpic
else
  cat libtool.tmp > libtool-nofpic
fi

rm -f libtool.tmp
chmod +x libtool-nofpic


dnl ---------------------------------------------
dnl Some infos:
dnl ---------------------------------------------

echo "xine-lib summary:"
echo "----------------"

dnl Input
echo " * input plugins:"
echo "   - file          - net"
echo "   - stdin_fifo    - rtp"
echo "   - http          - mms"
echo "   - pnm           - rtsp"
echo "   - dvb"
if test x"$external_dvdnav" = "xyes"; then
  echo "   - dvd (external libs)"
else
  echo "   - dvd (internal libs)"
fi
if test x"$have_cdrom_ioctls" = "xyes"; then
  if test x"$enable_vcd" = "xyes"; then
    if test x"$internal_vcdnav" = "xno"; then
      echo "   - vcd (external libs)"
    else
      echo "   - vcd (internal libs)"
    fi
  fi
  echo "   - vcdo"
  echo "   - cdda"
fi
if test x"$no_gnome_vfs" = "xno"; then
  echo "   - gnome-vfs"
fi
if test x"$have_v4l" = "xyes"; then
  echo "   - v4l"
fi
if test x"$have_libsmbclient" = "xyes"; then
  echo "   - smbclient"
fi
echo ""

dnl Demuxers
echo " * demultiplexer plugins:"
echo "   - avi           - mpeg"
echo "   - mpeg_block    - mpeg_audio"
echo "   - mpeg_elem     - mpeg_pes"
echo "   - mpeg_ts       - qt/mpeg-4"
echo "   - film          - roq"
echo "   - fli           - smjpeg"
echo "   - idcin         - wav"
echo "   - wc3 mve       - voc"
echo "   - vqa           - aiff"
echo "   - cdda          - snd/au"
echo "   - yuv4mpeg2     - real/realaudio"
echo "   - ea wve        - raw dv"
echo "   - interplay mve - psx str"
echo "   - ws aud        - pva"
echo "   - vox           - nsf"
echo "   - nsv           - 4xm"
echo "                   - aac"
echo "   - iff           - matroska"
echo "   - vmd           - flv"
if test x"$enable_asf" = "xyes"; then
  echo "   - asf"
fi
if test x"$no_vorbis" != "xyes"; then
  echo "   - ogg"
fi
if test x"$have_libmng" = "xyes"; then
  echo "   - mng"
fi
if test x"$enable_modplug" != x"no"; then
  echo "   - mod"
fi
if test x"$enable_a52dec" = "xyes"; then
  if test x"$have_a52" = "xyes"; then
    echo "   - ac3 (external library)"
  else
    echo "   - ac3 (internal library)"
  fi
fi
echo ""

dnl video decoders
echo " * video decoder plugins:"
echo "   - MPEG 1,2         - Amiga Bitplane"
echo "   - Raw RGB          - Raw YUV"
if test x"$external_ffmpeg_found" = "xyes"; then
  echo "   - ffmpeg (external library):"
else
  echo "   - ffmpeg (internal library):"
fi
echo "     - MPEG-4 (ISO, Microsoft, DivX*, XviD)"
echo "     - Creative YUV    - Motion JPEG"
echo "     - Cinepak         - MS Video-1"
echo "     - FLI/FLC         - MS RLE"
echo "     - Id RoQ          - Id Cin"
echo "     - Apple Graphics  - Apple Video"
echo "     - Apple Animation - Interplay Video"
echo "     - Westwood VQA    - Origin Xan"
echo "     - H.263           - Intel Indeo 3"
echo "     - SVQ1            - SVQ3"
echo "     - Real Video 1.0  - Real Video 2.0"
echo "     - 4X Video        - Sierra Video"
echo "     - Asus v1/v2      - HuffYUV"
echo "     - On2 VP3         - DV"
echo "     - 8BPS            - Duck TrueMotion v1"
echo "     - ATI VCR1        - Flash Video"
echo "     - ZLIB            - MSZH"
if test x"$have_dxr3" = "xyes"; then
  echo "   - dxr3_video"
fi
if test x"$enable_w32dll" = "xyes"; then
  echo "   - w32dll"
fi
if test x"$have_imagemagick" = "xyes"; then
  echo "   - image"
fi
if test x"no_gdkpixbuf" != "xyes"; then
  echo "   - gdk-pixbuf"
fi
if test x"$no_theora" != "xyes"; then
  echo "   - theora"
fi
echo ""

dnl audio decoders
echo " * audio decoder plugins:"
echo "   - GSM 06.10"
echo "   - linear PCM      - Nosefart (NSF)"
if test x"$external_ffmpeg_found" = "xyes"; then
  echo "   - ffmpeg (external library):"
else
  echo "   - ffmpeg (internal library):"
fi
echo "     - Windows Media Audio v1/v2"
echo "     - DV            - logarithmic PCM"
echo "     - 14k4          - 28k8"
echo "     - MS ADPCM      - IMA ADPCM"
echo "     - XA ADPCM      - Game DPCM/ADPCM"
echo "     - Mace 3:13     - Mace 6:1"
if test x"no_libFLAC" != "xyes"; then
  echo "     - FLAC"
fi
if test x"$no_vorbis" != "xyes"; then
  echo "   - vorbis"
fi
if test x"$no_speex" != "xyes"; then
  echo "   - speex"
fi
if test x"$enable_w32dll" = "xyes"; then
  echo "   - w32dll"
fi
if test x"$enable_faad" = "xyes"; then
  echo "   - faad"
fi
if test x"$enable_libmad" = "xyes"; then
  if test x"$have_mad" = "xyes"; then
    echo "   - MAD (MPG 1/2/3) (external library)"
  else
    echo "   - MAD (MPG 1/2/3) (internal library)"
  fi
fi
if test x"$enable_libdts" = "xyes"; then
  if test x"$have_dts" = "xyes"; then
    echo "   - DTS (external library)"
  else
    echo "   - DTS (internal library)"
  fi
fi
if test x"$enable_a52dec" = "xyes"; then
  if test x"$have_a52" = "xyes"; then
    echo "   - A52/ra-dnet (external library)"
  else
    echo "   - A52/ra-dnet (internal library)"
  fi
fi
echo ""

dnl spu decoders
echo " * subtitle decoder plugins:"
echo "   - spu             - spucc"
echo "   - spucmml         - sputext"
echo "   - spudvb"
if test x"$have_dxr3" = "xyes"; then
  echo "   - dxr3_spu"
fi
echo ""

dnl post plugins
echo " * post effect plugins:"
echo "  * planar video effects:"
echo "   - invert          - expand"
echo "   - eq              - eq2"
echo "   - boxblur         - denoise3d"
echo "   - unsharp         - tvtime"
echo "  * SFX:"
echo "   - goom            - oscope"
echo "   - fftscope        - mosaico"
echo ""

dnl Video plugins
echo " * video driver plugins:"
if test x"$no_x" != "xyes"; then
  echo "   - XShm (X11 shared memory)"
  dnl synfb
  if test x$ac_have_syncfb = "xyes"; then
    echo "   - SyncFB (for Matrox G200/G400 cards)"
  fi
  dnl Xv
  if test x$ac_have_xv = "xyes"; then
    if test x$ac_have_xv_static = "xyes"; then
      echo "   - Xv (XVideo *static*)"
    else
      echo "   - Xv (XVideo *shared*)"
    fi
  fi
  dnl XxMC
  if test x$ac_have_xxmc = "xyes"; then
    if test "x$ac_have_vldxvmc_h" = "xyes"; then
      echo "   - XxMC (XVideo extended motion compensation)"
    else
      echo "   - XxMC (XVideo motion compensation - vld extensions DISABLED)"
    fi
  fi
  dnl XvMC
  if test x$ac_have_xvmc = "xyes"; then
    echo "   - XvMC (XVideo motion compensation)"
  fi
  if test x$ac_have_opengl = "xyes" -a x$ac_have_glut="xyes" -o \
	  x$ac_have_opengl = "xyes" -a x$ac_have_glu="xyes"; then	
    echo "   - OpenGL"
  fi
  if test x$ac_have_sunfb = "xyes"; then
    if test x$ac_have_sundga = "xyes"; then
      echo "   - PGX64 (for Sun XVR100/PGX64/PGX24 cards)"
      echo "   - PGX32 (for Sun PGX32 cards)"
    fi
  fi
fi
if test x$no_aalib != "xyes"; then
  echo "   - aa (Ascii ART)"
fi
if test x$no_caca != "xyes"; then
  echo "   - caca (Color AsCii Art)"
fi
if test x$have_fb = "xyes"; then
  echo "   - fb (Linux framebuffer device)"
fi
if test x"$no_sdl" != x"yes"; then
  echo "   - sdl (Simple DirectMedia Layer)"
fi
if test x"$have_stk" = "xyes"; then
  echo "   - stk (Libstk Set-top Toolkit)"
fi
if test x"$have_directfb" = "xyes"; then
  echo "   - directfb (DirectFB driver)"
fi
if test x"$have_dxr3" = "xyes"; then
  if test x"$have_encoder" = "xyes"; then
    echo "   - dxr3 (Hollywood+ and Creative dxr3, both mpeg and non-mpeg video)"
  else
    echo "   - dxr3 (Hollywood+ and Creative dxr3, mpeg video only)"
  fi
fi
if test x"$enable_vidix" = "xyes"; then
  echo $ECHO_N "   - vidix ("
  
  if test x"$no_x" != "xyes"; then
    echo $ECHO_N "X11"
    if test x"$have_fb" = "xyes"; then
      echo $ECHO_N " and "
    fi
  fi
    
  if test x"$have_fb" = "xyes"; then
      echo $ECHO_N "framebuffer"
  fi
  
  echo $ECHO_N " support"

  if test x"$enable_dha_kmod" = "xyes"; then
    echo " with dhahelper)"
  else
    echo ")"
  fi
fi
if test x"$have_directx" = "xyes"; then
  echo "   - directx (DirectX video driver)"
fi
if test x"$have_macosx_video" = "xyes"; then
  echo "   - Mac OS X OpenGL"
fi

echo ""

dnl Audio plugins
echo " * audio driver plugins:"
if test x"$have_ossaudio" = "xyes"; then
  echo "   - oss (Open Sound System)"
fi
if test x"$no_alsa" != "xyes"; then
  if test x"$have_alsa_1_0_9" = "xyes"; then
    echo "   - alsa (ALSA >= 1.0.9)"
  elif test x"$have_alsa09" = "xyes"; then
    echo "   - alsa (ALSA >= 0.9, < 1.0.9)"
  fi
fi
if test x"$no_esd" != "xyes"; then
  echo "   - esd (Enlightened Sound Daemon)"
fi
if test x"$no_arts" != "xyes"; then
  echo "   - arts (aRts - KDE soundserver)"
fi
if test x"$no_fusionsound" != "xyes"; then
  echo "   - fusionsound (FusionSound driver)"
fi
if test x"$have_sunaudio" = "xyes"; then
  echo "   - sun ()"
fi
if test "x$am_cv_have_irixal" = xyes; then
  echo "   - irixal (Irix audio library)"
fi
if test x"$have_directx" = "xyes"; then
  echo "   - directx (DirectX audio driver)"
fi
if test x"$have_coreaudio" = "xyes"; then
  echo "   - CoreAudio (Mac OS X audio driver)"
fi  
if test x"$have_polypaudio" = "xyes"; then
  echo "   - polypaudio sound server"
fi
echo "---"


dnl ---------------------------------------------
dnl some user warnings
dnl ---------------------------------------------

dnl some levels of variable expansion to get final install paths
final_libdir="`eval eval eval eval echo $libdir`"
final_bindir="`eval eval eval eval echo $bindir`"

if test -r /etc/ld.so.conf && ! grep -x "$final_libdir" /etc/ld.so.conf >/dev/null ; then
  if test "$final_libdir" != "/lib" -a "$final_libdir" != "/usr/lib" ; then
    if ! echo $LD_LIBRARY_PATH | egrep "(:|^)$final_libdir(/?:|/?$)" >/dev/null ; then
      echo
      echo "****************************************************************"
      echo "xine-lib will be installed to $final_libdir"
      echo
      echo "This path is not mentioned among the linker search paths in your"
      echo "/etc/ld.so.conf. This means it is possible that xine-lib will"
      echo "not be found when you try to compile or run a program using it."
      echo "If this happens, you should add $final_libdir to"
      echo "the environment variable LD_LIBRARY_PATH like that:"
      echo
      echo "export LD_LIBRARY_PATH=$final_libdir:\$LD_LIBRARY_PATH"
      echo
      echo "Alternatively you can add a line \"$final_libdir\""
      echo "to your /etc/ld.so.conf."
      echo "****************************************************************"
      echo
    fi
  fi
fi

if ! echo $PATH | egrep "(:|^)$final_bindir(/?:|/?$)" >/dev/null ; then
  echo
  echo "****************************************************************"
  echo "xine-config will be installed to $final_bindir"
  echo
  echo "This path is not in your search path. This means it is possible"
  echo "that xine-config will not be found when you try to compile a"
  echo "program using xine-lib. This will result in build failures."
  echo "If this happens, you should add $final_bindir to"
  echo "the environment variable PATH like that:"
  echo
  echo "export PATH=$final_bindir:\$PATH"
  echo
  echo "Note that this is only needed for compilation. It is not needed"
  echo "to have xine-config in your search path at runtime. (Although"
  echo "it will not cause any harm either.)"
  echo "****************************************************************"
  echo
fi

dnl warn if no X11 plugins will be built
if test x"$no_x" = "xyes" \
        -a x$SYS != "xmingw32" -a x$SYS != "xcygwin" ; then
  echo
  echo "****************************************************************"
  echo "WARNING! No X11 output plugins will be built."
  echo
  echo "For some reason, the requirements for building the X11 video"
  echo "output plugins are not met. That means, that you will NOT be"
  echo "able to use the resulting xine-lib to watch videos in a window"
  echo "on any X11-based display (e.g. your desktop)."
  echo
  echo "If this is not what you want, provide the necessary X11 build"
  echo "dependencies (usually done by installing a package called"
  echo "XFree86-devel or similar) and run configure again."
  echo "****************************************************************"
  echo
fi
