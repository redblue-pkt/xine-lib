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
dnl   XINE_SUB += 1; continue with LT_* values below
dnl
XINE_MAJOR=1
XINE_MINOR=0
XINE_SUB=0


dnl The libtool version numbers (LT_*); Don't even think about faking this!
dnl 
dnl immediately before every release do:
dnl ===================================
dnl if (the interface is totally unchanged from previous release)
dnl    LT_REVISION ++;
dnl else { /* interfaces have been added, removed or changed */
dnl    LT_REVISION = 0;
dnl    LT_CURRENT ++;
dnl    if (any interfaces have been _added_ since last release)
dnl       AGE ++;
dnl    if (any interfaces have been _removed_ or _incompatibly changed_)
dnl       AGE = 0;
dnl }
dnl 
dnl If you want to know more about what you are doing, here are some details:
dnl  * LT_CURRENT is the current API version
dnl  * LT_REVISION is an internal revision number which is increased when the API
dnl    itself did not change
dnl  * LT_AGE is the number of previous API versions still supported by this library
dnl  * libtool has its own numbering scheme, because local library numbering schemes
dnl    are platform dependent
dnl  * in Linux, the library will be named
dnl    libname.so.(LT_CURRENT - LT_AGE).LT_AGE.LT_REVISION

LT_CURRENT=11
LT_REVISION=0
LT_AGE=10

dnl for a release tarball do "rm .cvsversion" before "make dist"
if test -f .cvsversion; then
  XINE_PRE="cvs"
else
  XINE_PRE="rc7"
fi

AC_SUBST(XINE_MAJOR)
AC_SUBST(XINE_MINOR)
AC_SUBST(XINE_SUB)
AC_SUBST(XINE_IFACE_AGE)
AC_SUBST(XINE_BIN_AGE)

AC_DEFINE_UNQUOTED(XINE_MAJOR, $XINE_MAJOR,[xine major version number])
AC_DEFINE_UNQUOTED(XINE_MINOR, $XINE_MINOR,[xine minor version number])
AC_DEFINE_UNQUOTED(XINE_SUB, $XINE_SUB, [xine sub version number])

AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

dnl TAR_NAME="xine-lib-"$XINE_MAJOR.$XINE_MINOR.$XINE_SUB$XINE_PRE
dnl SPEC_VERSION=$XINE_MAJOR.$XINE_MINOR.$XINE_SUB$XINE_PRE
TAR_NAME="xine-lib-"$XINE_MAJOR-$XINE_PRE
SPEC_VERSION=${XINE_MAJOR}_$XINE_PRE

AC_SUBST(TAR_NAME)
AC_SUBST(SPEC_VERSION)

LIBNAME=libxine$XINE_MAJOR
AC_SUBST(LIBNAME)

dnl
AC_CANONICAL_TARGET

dnl
dnl AM_INIT_AUTOMAKE("xine-lib", $XINE_MAJOR.$XINE_MINOR.$XINE_SUB$XINE_PRE)
AM_INIT_AUTOMAKE("xine-lib", $XINE_MAJOR-$XINE_PRE)


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
ALL_LINGUAS="fr pt_BR de sk es pl cs it"
AM_GNU_GETTEXT(use-libtool, need-ngettext)
AC_PROG_GMSGFMT_PLURAL
AC_CONFIG_LINKS($nls_cv_header_libgt, $nls_cv_header_intl)
AM_CONDITIONAL(INCLUDED_INTL, test x$USE_INCLUDED_LIBINTL = "xyes")
if test x"$USE_INCLUDED_LIBINTL" = x"yes"; then
   INTLDIR="-I\$(top_builddir)/intl -I\$(top_srcdir)/intl"
fi
AC_SUBST(INTLDIR)


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
dnl AC_CHECK_GENERATE_INTTYPES([lib])
AC_CHECK_TYPE(ssize_t, :, AC_DEFINE(ssize_t, __int64, [define ssize_t to __int64 if it's missing in default includes]))


dnl ---------------------------------------------
dnl threads and OS specific stuff
dnl ---------------------------------------------

case "$host" in
  *-*-freebsd*)
    THREAD_LIBS="-L/usr/local/lib -pthread"
    THREAD_CFLAGS="-I/usr/local/include -D_THREAD_SAFE"

    CFLAGS="$THREAD_CFLAGS $CFLAGS"

    ;;

  *-*-hpux11*)
    THREAD_LIBS=" -lpthread"
    THREAD_CFLAGS="-D_REENTRANT"
    CFLAGS="$THREAD_CFLAGS $CFLAGS"

    ;;

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
      THREAD_INCLUDES='-I$(top_srcdir)/win32/contrib/pthreads'
      THREAD_LIBS='$(top_builddir)/win32/contrib/pthreads/libpthread.la'
      THREAD_CFLAGS_CONFIG=" "
      THREAD_LIBS_CONFIG=" "
      WIN32_CPPFLAGS='-I$(top_srcdir)/win32/include -I$(top_srcdir)/win32/contrib/dirent'
      LIBS="-lwinmm -lwsock32 $LIBS"
      GOOM_LIBS="-liberty"

      need_internal_zlib=yes
    fi
    LDFLAGS="-no-undefined $LDFLAGS"
    ;;
    

  *)
    AC_CHECK_LIB(pthread, pthread_create,
  	     [THREAD_LIBS="-lpthread"],
 	     [AC_MSG_ERROR(pthread needed)])
    ;;
esac
dnl different thread cflags and libs for libxine and for external 
dnl application linking libxine
if test "x$THREAD_CFLAGS_CONFIG" = "x"; then
  THREAD_CFLAGS_CONFIG=$THREAD_CFLAGS
fi
if test "x$THREAD_LIBS_CONFIG" = "x"; then
  THREAD_LIBS_CONFIG=$THREAD_LIBS
fi
dnl used for building xine
AC_SUBST(THREAD_CFLAGS)
AC_SUBST(THREAD_INCLUDES)
AC_SUBST(THREAD_LIBS)
AC_SUBST(GOOM_LIBS)
dnl needed for xine-config
AC_SUBST(THREAD_CFLAGS_CONFIG)
AC_SUBST(THREAD_LIBS_CONFIG)
dnl WIN32 platform
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
 case `/usr/bin/file conftest.$ac_objext` in
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
AC_SUBST(LIBFFMPEG_CFLAGS)

LIBMPEG2_CFLAGS=""
LIBFFMPEG_CFLAGS="-DSIMPLE_IDCT -DHAVE_AV_CONFIG_H -DRUNTIME_CPUDETECT -DUSE_FASTMEMCPY -DCONFIG_RISKY -DCONFIG_DECODERS -DXINE_MPEG_ENCODER -DCONFIG_ZLIB"

AC_CHECK_DECL(lrintf,[
	AC_DEFINE(HAVE_LRINTF,1,[Define this if the 'lrintf' function is declared in math.h])
	AC_DEFINE(_ISOC9X_SOURCE,1,[Define this if you are ISO C9X compliant])
      ],,[
#define _ISOC9X_SOURCE
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

dnl internal zlib permited only in MINGW-like platform
if test "x$need_internal_zlib" = "xyes"; then
  have_zlib=no
  ZLIB_INCLUDES='-I$(top_srcdir)/win32/contrib/zlib'
  ZLIB_LIBS='$(top_builddir)/win32/contrib/zlib/libzlib.la'
  ZLIB_LIBS_CONFIG=""
else
  AC_CHECK_LIB(z, gzsetparams,
    [AC_CHECK_HEADER(zlib.h,
       have_zlib=yes
       ZLIB_LIBS="-lz"
       ZLIB_LIBS_CONFIG="-lz",)], AC_MSG_ERROR(zlib needed))
fi
AM_CONDITIONAL(HAVE_ZLIB, [test x"$have_zlib" = "xyes"])
AC_SUBST(ZLIB_INCLUDES)
AC_SUBST(ZLIB_LIBS)
AC_SUBST(ZLIB_LIBS_CONFIG)

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
    have_syncfb="yes"
    ;;
  *)
    have_syncfb="no"
    ;;
esac
AM_CONDITIONAL(HAVE_SYNCFB, test x"$have_syncfb" = "xyes")


dnl ----------------------------------------------
dnl Check for usable video-for-linux (v4l) support
dnl ----------------------------------------------

AC_CHECK_HEADER(linux/videodev.h, have_v4l=yes,)
AM_CONDITIONAL(HAVE_V4L, [test x"$have_v4l" = "xyes"])


dnl ----------------------------------------------
dnl Check for Xv and XvMC support (iconv is also tested here?)
dnl ----------------------------------------------

dnl static linking is preferred!
dnl but only dynamic linking is possible when using libtool < 1.4.0

dnl ----------------------------------------------
dnl SECTION: Check for Xv and XvMC support (iconv is also tested here?)
dnl ----------------------------------------------
AC_ARG_WITH(xv-path, AC_HELP_STRING([--with-xv-path=path], [where libXv is installed]),
            xv_path="$withval",)

AC_ARG_ENABLE(shared-xv,
        AC_HELP_STRING([--enable-shared-xv],[Enable this to force linking against libXv.so]),
        xv_prefer_shared="yes",
        xv_prefer_shared="no")

if test x"$no_x" != "xyes"; then

  dnl -----------------------------------------------
  dnl   Finding install Xv library
  dnl -----------------------------------------------
  AC_FIND_LIBXV

fi


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
	if test "$xv_lib" = "libXv.a"; then
	  echo "warning: hppa linker - disabling static libXv"
	  xv_lib="libXv.so"
	fi
	;;

  i386-*-freebsd*)
	AC_CHECK_HEADER(/usr/local/include/iconv.h,
			CFLAGS="-I/usr/local/include $CFLAGS"
			LDFLAGS="-L/usr/local/lib $LDFLAGS"
	                LIBICONV="-liconv"
                        [ echo
                          echo "****************************************************************"
		          echo "* You need to install a recent version of the port 'libiconv'  *"
			  echo "* (in /usr/ports/converters/libiconv).                         *"
			  echo "* The library in this port is needed to successfully compile   *"
			  echo "* libsputext plugin.                                           *"
                          echo "****************************************************************"
	                  echo 
                        ],)
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
if test x$no_x = "x"; then

AC_ARG_WITH(xxmc-path, AC_HELP_STRING([--with-xxmc-path=path], [where libXvMC libraries for the
	xxmc plugin are  installed. Defalts to the default X library path.]),
	xxmc_path="$withval", xxmc_path="$x_libraries")
AC_ARG_WITH(xxmc-lib, AC_HELP_STRING([--with-xxmc-lib=XXXX], [The name of the XvMC library 
	libXXXX.so for the xxmc plugin.]),xxmc_stub="$withval", 
	xxmc_stub="XvMCW")
saved_libs="$LIBS"
XXMC_LIB="-L$xxmc_path -l$xxmc_stub"
AC_MSG_CHECKING(whether to enable the xxmc plugin with vld extensions)
AC_MSG_RESULT()
dnl Check if vld "extended" XvMC is available
if test x$xxmc_stub == "xXvMCW"; then
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
  AC_CHECK_HEADERS($x_includes/X11/extensions/vldXvMC.h,
     [ac_have_vldxvmc_h="yes"],
     ac_have_xxmc="no",)
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
  AC_CHECK_HEADERS($x_includes/X11/extensions/XvMC.h,,
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
if test x$no_x = "x"; then

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
  AC_CHECK_HEADERS($x_includes/X11/extensions/XvMC.h,,
     ac_have_xvmc="no",)
fi
if test x$ac_have_xvmc = "xyes"; then
  AC_DEFINE(HAVE_XVMC,1,[Define this if you have an XvMC library and XvMC.h installed.])
  AC_MSG_RESULT([*** Enabling old xvmc plugin.])
else
  AC_MSG_RESULT([*** Disabling old xvmc plugin due to above errors.])
fi

LIBS="$saved_libs"
fi
AM_CONDITIONAL(HAVE_XVMC, test x$ac_have_xvmc = "xyes")
AC_SUBST(XVMC_LIB)


dnl ---------------------------------------------
dnl Checks for Xinerama extension
dnl ---------------------------------------------

AC_CHECK_LIB(Xinerama, XineramaQueryExtension, 
             [X_LIBS="$X_LIBS -lXinerama"
              AC_DEFINE(HAVE_XINERAMA,1,[Define this if you have libXinerama installed])
              ac_have_xinerama="yes"],,
             [$X_LIBS $X_PRE_LIBS -lXext $X_EXTRA_LIBS])
dnl AM_CONDITIONAL(HAVE_XINERAMA, test x$ac_have_xinerama = "xyes")

 
dnl ---------------------------------------------
dnl Checks for Ascii-Art library
dnl ---------------------------------------------

AM_PATH_AALIB(1.4,, AC_MSG_RESULT([*** All of AALIB dependent parts will be disabled ***]))
AM_CONDITIONAL(HAVE_AA, test x$no_aalib != "xyes")


dnl ---------------------------------------------
dnl Checks for Color AsCii Art library
dnl ---------------------------------------------

AM_PATH_CACA(0.3,, AC_MSG_RESULT([*** All CACA-dependent parts will be disabled ***]))
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

AC_PATH_PROG(PKG_CONFIG, pkg-config, no)
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

  DIRECTFB_REQUIRED_VERSION=0.9.9
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

AM_PATH_SDL(1.1.5, 
	AC_DEFINE(HAVE_SDL,1,[Define this if you have SDL library installed]),
	[])
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

PKG_CHECK_MODULES(SPEEX, speex, no_speex="no", no_speex="yes")

if test "x$no_speex" = "xyes" ; then
  AM_PATH_OGG([ AM_PATH_SPEEX(no_speex="no")])
fi

if test "x$no_speex" = "xyes" ; then
  AC_MSG_RESULT([*** All OGG/SPEEX dependent parts will be disabled ***])
else
  AC_DEFINE(HAVE_SPEEX,1,[Define this if you have speex])

  dnl Test whether Speex headers are eg. <speex.h> or <speex/speex.h>
  dnl Speex headers were moved in mid-2004; 1.0.x has backwards compatible headers, 1.1.x does not
  AC_CHECK_HEADER([speex/speex.h],
                  AC_DEFINE(HAVE_SPEEX_SUBDIR, [1], [Define to 1 if speex headers are eg. <speex/speex.h>])
  )

  AC_SUBST(SPEEX_CFLAGS)
  AC_SUBST(SPEEX_LIBS)
fi
AM_CONDITIONAL(HAVE_SPEEX, [test x"$no_ogg" != "xyes" -a x"$no_speex" != "xyes"])


dnl ---------------------------------------------
dnl check for libFLAC
dnl ---------------------------------------------

AM_PATH_LIBFLAC([],
	AC_MSG_RESULT([*** All FLAC dependent parts will be disabled ***]))
AM_CONDITIONAL(HAVE_FLAC, [test x"$no_libFLAC" != "xyes"])


dnl ---------------------------------------------
dnl MNG libs.
dnl ---------------------------------------------

AC_CHECK_LIB(mng, mng_initialize,
	[ AC_CHECK_HEADER(libmng.h,
		[ have_libmng=yes
		  MNG_LIBS="-lmng" ], 
		AC_MSG_RESULT([*** All libmng dependent parts will be disabled ***]))],
	AC_MSG_RESULT([*** All libmng dependent parts will be disabled ***]))
AM_CONDITIONAL(HAVE_LIBMNG, test x"$have_libmng" = "xyes")
AC_SUBST(MNG_LIBS)


dnl ---------------------------------------------
dnl PNG lib.
dnl ---------------------------------------------

AC_PATH_PROG(LIBPNG_CONFIG, libpng-config, no)
if test "$LIBPNG_CONFIG" = "no" ; then
  AC_CHECK_LIB(png, png_create_read_struct,
		  have_libpng=yes
		  PNG_LIBS="-lpng",
  AC_MSG_RESULT([*** All libpng dependent parts will be disabled ***]))
else
  PNG_CFLAGS=`$LIBPNG_CONFIG --cflags`
  PNG_LIBS=`$LIBPNG_CONFIG --libs`
  have_libpng="yes"
  AC_DEFINE(HAVE_LIBPNG,1,[Define this if you have png library])
fi

AM_CONDITIONAL(HAVE_LIBPNG, test x"$have_libpng" = "xyes" )
AC_SUBST(PNG_CFLAGS)
AC_SUBST(PNG_LIBS)


dnl ---------------------------------------------
dnl freetype2 lib.
dnl ---------------------------------------------

AM_PATH_FREETYPE2()


dnl ---------------------------------------------
dnl OSS style audio interface
dnl ---------------------------------------------

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
AM_CONDITIONAL(HAVE_OSS, test x"$have_ossaudio" = "xyes")


dnl ---------------------------------------------
dnl Alsa support
dnl ---------------------------------------------

AM_PATH_ALSA(0.9.0rc4, 
        [ AC_DEFINE(HAVE_ALSA,1,[Define this if you have Alsa (libasound) installed])
          if test x"$have_alsa09" = "xyes"; then
            AC_DEFINE(HAVE_ALSA09,1,[Define this if you have alsa 0.9.x and more installed])
          fi
        ],
	AC_MSG_RESULT(*** All of ALSA dependent parts will be disabled ***))
AM_CONDITIONAL(HAVE_ALSA, test x"$no_alsa" != "xyes")
AM_CONDITIONAL(HAVE_ALSA09, test x"$have_alsa09" = "xyes")


dnl ---------------------------------------------
dnl ESD support
dnl ---------------------------------------------

AM_PATH_ESD(0.2.8,
        AC_DEFINE(HAVE_ESD,1,[Define this if you have ESD (libesd) installed]),
        AC_MSG_RESULT(*** All of ESD dependent parts will be disabled ***))
AM_CONDITIONAL(HAVE_ESD, test x"$no_esd" != "xyes")


dnl ---------------------------------------------
dnl ARTS support
dnl ---------------------------------------------

AM_PATH_ARTS(0.9.5,
        AC_DEFINE(HAVE_ARTS,1,[Define this if you have ARTS (libartsc) installed]),
        AC_MSG_RESULT(*** All of ARTS dependent parts will be disabled ***))
AM_CONDITIONAL(HAVE_ARTS, test x"$no_arts" != "xyes")


dnl ---------------------------------------------
dnl gnome-vfs support
dnl ---------------------------------------------

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
AM_CONDITIONAL(HAVE_GNOME_VFS, test x"$no_gnome_vfs" != "xyes")


dnl ---------------------------------------------
dnl libsmbclient support
dnl ---------------------------------------------

AC_CHECK_LIB(smbclient, smbc_init,
	[ AC_CHECK_HEADER(libsmbclient.h,
		[ have_libsmbclient=yes
		  LIBSMBCLIENT_LIBS="-lsmbclient" ], 
		AC_MSG_RESULT([*** All libsmbclient dependent parts will be disabled ***]))],
	AC_MSG_RESULT([*** All libsmbclient dependent parts will be disabled ***]))
AM_CONDITIONAL(HAVE_LIBSMBCLIENT, test x"$have_libsmbclient" = "xyes")
AC_SUBST(LIBSMBCLIENT_LIBS)


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
    if $PKG_CONFIG --atleast-version 0.64 libcdio; then
      AC_MSG_RESULT(found)
      LIBCDIO_CFLAGS=`$PKG_CONFIG --cflags libcdio`
      LIBCDIO_LIBS=`$PKG_CONFIG --libs libcdio`

      AC_MSG_CHECKING(for libvcdinfo)
      if $PKG_CONFIG --atleast-version 0.7.19-cdio libvcdinfo; then
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

    dnl bitfield order
    AC_MSG_CHECKING(bitfield ordering in structs)
    AC_TRY_RUN([
int
 main() {
  struct { char bit_0:1, bit_12:2, bit_345:3, bit_67:2; }
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
  __attribute__((packed))
#endif
  bf = { 1,1,1,1 };
  if (sizeof (bf) != 1) return 1;
  return *((unsigned char*) &bf) != 0x4b; }
], bf_lsbf=1, AC_TRY_RUN([ 
int
main() {
  struct { char bit_0:1, bit_12:2, bit_345:3, bit_67:2; }
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
  __attribute__((packed))
#endif
  bf = { 1,1,1,1 };
  if (sizeof (bf) != 1) return 1;
  return *((unsigned char*) &bf) != 0xa5; }
], bf_lsbf=0, AC_MSG_ERROR([unsupported bitfield ordering])))
    if test "x$bf_lsbf" = "x1"; then
     AC_MSG_RESULT(LSBF)
     AC_DEFINE(BITFIELD_LSBF, [], [compiler does lsbf in struct bitfields])
    else
     AC_MSG_RESULT(MSBF)
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
dnl libmodplug support 
dnl ---------------------------------------------
if test x$PKG_CONFIG = xno ; then
  echo "*** pkg-config not found. See http://pkgconfig.sourceforge.net"
  echo "*** All of MODPLUG dependent parts will be disabled"
else
    LIBMODPLUG_REQUIRED_VERSION=0.7
    if $PKG_CONFIG --atleast-version $LIBMODPLUG_REQUIRED_VERSION libmodplug ; then
        LIBMODPLUG_CFLAGS=`$PKG_CONFIG --cflags libmodplug`
        LIBMODPLUG_LIBS=`$PKG_CONFIG --libs libmodplug`
        AC_DEFINE(HAVE_MODPLUG,1,[define this if you have libmodplug installed])
        have_modplug="yes"
    else
        AC_MSG_RESULT([*** All of MODPLUG dependent parts will be disabled ***])
    fi
fi
AC_SUBST(LIBMODPLUG_CFLAGS)
AC_SUBST(LIBMODPLUG_LIBS)
dnl AM_CONDITIONAL(HAVE_MODPLUG, [test x"$have_modplug" = x"yes"])


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
AC_CHECK_FUNCS([vsscanf sigaction sigset getpwuid_r nanosleep lstat memset strchr])
AC_FUNC_FSEEKO
AC_DEFINE(_GNU_SOURCE)
AC_CHECK_HEADERS(byteswap.h malloc.h execinfo.h ucontext.h sys/mman.h sys/mixer.h libgen.h netdb.h sys/times.h sys/ioctl.h)
AC_REPLACE_FUNCS(basename gettimeofday hstrerror setenv strndup strpbrk strsep strtok_r)
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
 

if test "$GCC" = yes; then
    dnl
    dnl check cflags not supported by all gcc versions
    dnl eg: -mpreferred-stack-boundary=2 and 2.91.66,
    dnl and gcc-2.7.2.3 support a bit less options
    dnl
    AC_TRY_CFLAGS("-mpreferred-stack-boundary=2", 
	m_psb="-mpreferred-stack-boundary=2", m_psb="")
    AC_TRY_CFLAGS("-fno-strict-aliasing", f_nsa="-fno-strict-aliasing", f_nsa="")
    AC_TRY_CFLAGS("-fschedule-insns2", f_si="-fschedule-insns2", f_si="")
    AC_TRY_CFLAGS("-mwide-multiply", m_wm="-mwide-multiply", m_wm="")
    dnl
    dnl gcc 3.1 uses the -f version
    dnl
    AC_TRY_CFLAGS("-falign-functions=4", f_af="-falign-functions=4",
        f_af="-malign-functions=4")
    AC_TRY_CFLAGS("-falign-loops=4", f_al="-falign-loops=4",
        f_al="-malign-loops=4")
    AC_TRY_CFLAGS("-falign-jumps=4", f_aj="-falign-jumps=4",
        f_aj="-malign-jumps=4")
    dnl
    dnl Check for some optimization disabling
    dnl needed for win32 code
    dnl
    AC_TRY_CFLAGS("-fno-omit-frame-pointer", W32_NO_OPTIMIZE="$W32_NO_OPTIMIZE -fno-omit-frame-pointer",)
    AC_TRY_CFLAGS("-fno-inline-functions", W32_NO_OPTIMIZE="$W32_NO_OPTIMIZE -fno-inline-functions",)
    AC_TRY_CFLAGS("-fno-rename-registers", W32_NO_OPTIMIZE="$W32_NO_OPTIMIZE -fno-rename-registers",)
    AC_SUBST(W32_NO_OPTIMIZE)
    dnl
    dnl Multipass compilation
    dnl
    AC_TRY_CFLAGS("-fprofile-arcs", PASS1_CFLAGS="-fprofile_arcs $PASS1_CFLAGS",)
    AC_TRY_CFLAGS("-fbranch-probabilities", PASS2_CFLAGS="-fbranch-probabilities $PASS2_CFLAGS",)
    AC_SUBST(PASS1_CFLAGS)
    AC_SUBST(PASS2_CFLAGS)
    dnl
    dnl Warnings
    dnl
    CFLAGS="-Wchar-subscripts -Wmissing-declarations -Wmissing-prototypes $CFLAGS"
    CFLAGS="-Wnested-externs -Wcast-align $CFLAGS"
    dnl some combinations of gcc+glibc produce useless warnings on memset
    dnl when compiling with -Wpointer-arith, so we check for this first
    AC_MSG_CHECKING(for sane -Wpointer-arith)
    SAVE_CFLAGS="$CFLAGS"
    CFLAGS="-O2 -Wpointer-arith -Werror $CFLAGS"
    AC_TRY_COMPILE([#include <string.h>],[int a; memset(&a, 0, sizeof(int));],
	[AC_MSG_RESULT(yes); CFLAGS="-Wpointer-arith $SAVE_CFLAGS"],
	[AC_MSG_RESULT(no);  CFLAGS="$SAVE_CFLAGS"]);
fi

dnl Flags not supported by all *cc* variants
AC_TRY_CFLAGS("-Wall", wall="-Wall", wall="")

dnl check for __attribute__ ((aligned ()))
dnl WARNING! Do not move this check behind the $(MULTIPASS_CFLAGS)!
dnl (the alignment test macro does not like variables in the CFLAGS)
AC_C_ATTRIBUTE_ALIGNED

dnl Common cflags for all platforms
CFLAGS="$wall -DNDEBUG -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DXINE_COMPILE \$(MULTIPASS_CFLAGS) $CFLAGS"
DEBUG_CFLAGS="$wall -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DXINE_COMPILE $DEBUG_CFLAGS"

enable_w32dll="no"
enable_ffmmx="no"
enable_armv4l="no"

case "$host_or_hostalias" in
  i386-*-freebsd*)
    CFLAGS="-pipe -fomit-frame-pointer $f_af $f_al $f_aj $m_wm $m_psb -fexpensive-optimizations $f_si $f_nsa -ffast-math -funroll-loops -finline-functions $CFLAGS"
    DEBUG_CFLAGS="$X_CFLAGS $DEBUG_CFLAGS"
    AC_DEFINE(__i386__,1,[Define this if you're running x86 architecture])
    AC_DEFINE_UNQUOTED(ARCH_X86,,[Define this if you're running x86 architecture])
    AC_DEFINE_UNQUOTED(FPM_INTEL,,[Define to select libmad fixed point arithmetic implementation])

    enable_w32dll="yes"
    enable_ffmmx="yes"
    no_fpic="yes"
    ;;



  i?86-*-linux* | i386-*-solaris* | i?86-* | k?-* | athlon-* | pentium*)
    if test "$GCC" = yes -o "${CC##*/}x" = "iccx" ; then

      if test "$GCC" = yes ; then
	dnl Check for gcc cpu optimization support
	AC_TRY_CFLAGS("-mtune=i386", 
	  sarchopt="-mtune",
  	  AC_TRY_CFLAGS("-mcpu=i386", 
	    sarchopt="-mcpu",
            AC_TRY_CFLAGS("-march=i386", 
	      sarchopt="-march",
	      [ AC_MSG_RESULT(** no cpu optimization supports **)
	        sarchopt=no 
	      ]
            )
          )
        )

	dnl special check for k7 cpu CC support
        AC_TRY_CFLAGS("$sarchopt=athlon", k7cpu="athlon", k7cpu="i686")

	dnl add x86 specific gcc CFLAGS
	CFLAGS="-O3 -pipe -fomit-frame-pointer $f_af $f_al $f_aj $m_wm $m_psb -fexpensive-optimizations $f_si $f_nsa -ffast-math -funroll-loops -finline-functions $CFLAGS"

	DEBUG_CFLAGS="-O $DEBUG_CFLAGS"

	if test x"$sarchopt" != "xno"; then
	    [
	    archopt_val=

	    case "$host_or_hostalias" in
	    i386-*) # *BSD return this even on a P III #-))
		archopt_val="i386" ;;
	    i486-*) # oh dear!
		archopt_val="i486" ;;
	    i586-*)
		archopt_val="pentium" 
                ;;
	    pentium-mmx-*)
		archopt_val="pentium-mmx" 
		;;
	    pentiumpro-* | pentium2-* | i686-*)
		archopt_val="pentiumpro"
		if test x"$check_athlon" = "xyes"; then
		    if test -f /proc/cpuinfo; then
			modelname=`cat /proc/cpuinfo | grep "model\ name\	:" | sed -e 's/ //g' | cut -d':' -f2`
			case "$modelname" in
			*Athlon* | *Duron* | *K7*)
			    archopt_val="$k7cpu"
			    ;;
			VIAEzra)
			    archopt_val="c3"
			    ;;
			esac
		    fi 
		fi
		;;
	    k6-2-* | k6-3-*)
		archopt_val="k6-2"
		;;
	    k6-*)
		archopt_val="k6"
		;;
	    pentium3-*)
		archopt_val="pentium3"
		;;
	    pentium4-*)
		archopt_val="pentium4"
		;;
	    athlon-4-* | athlon-xp-* | athlon-mp-*)
		archopt_val="athlon-4"
		;;
	    k7-* | athlon-tbird-* | athlon-*)
		archopt_val="athlon"
		;;
	    
	    esac
	    if test x"$archopt_val" != x; then
		CFLAGS="$sarchopt=$archopt_val $CFLAGS"
		DEBUG_CFLAGS="$sarchopt=$archopt_val $DEBUG_CFLAGS"
	    fi
	    ]
	fi
      else
	dnl we have the Intel compiler
	CFLAGS="-unroll -ipo -ipo_obj -O3 $CFLAGS"
	PASS1_CFLAGS="-prof_genx -prof_dir \$(PWD)/\$(top_builddir)/ $PASS1_CFLAGS"
	PASS2_CFLAGS="-prof_use -prof_dir \$(PWD)/\$(top_builddir)/ $PASS2_CFLAGS"
	AC_SUBST(PASS1_CFLAGS)
	AC_SUBST(PASS2_CFLAGS)
      fi
	
    	dnl enable x86 specific parts of the code
	dnl
	dnl all of this stuff needs gcc/gas; it uses gnu c compiler extensions
	dnl like the extended asm() or __attribute(__cdecl__), or other direct
	dnl mmx/sse/3dnow assembler instructions. 
	dnl
    	AC_DEFINE_UNQUOTED(ARCH_X86,,[Define this if you're running x86 architecture])
	AC_DEFINE(FPM_INTEL,1,[Define to select libmad fixed point arithmetic implementation])
	enable_ffmmx="yes"
	enable_w32dll="yes"

        case "$host_or_hostalias" in
	*-*-mingw*)
	  enable_w32dll="no" 
	  ;;
	*-*-cygwin) 
          enable_w32dll="no" 
          LIBS="$LIBS @INTLLIBS@ -lkernel32"
	  ;;
	esac
    else
	dnl add x86 specific cc CFLAGS
	CFLAGS="-O $CFLAGS"
	DEBUG_CFLAGS="-O $DEBUG_CFLAGS"
    	AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed point arithmetic implementation])
    fi

    no_fpic="yes"
    AC_DEFINE(__i386__,1,[Define this if you're running x86 architecture])
    ;;

  alphaev56-*)
    CFLAGS="-O3 -mcpu=ev56 -mieee $CFLAGS"
    DEBUG_CFLAGS="-O3 -mcpu=ev56 -mieee $DEBUG_CFLAGS"
    AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed point arithmetic implementation])
    ;;

  alpha*)
    CFLAGS="-O3 -mieee $CFLAGS"
    DEBUG_CFLAGS="-O3 -mieee $DEBUG_CFLAGS"
    AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed point arithmetic implementation])
    ;;
  
  *darwin*)
    HOST_OS_DARWIN=1
    AC_DEFINE_UNQUOTED(HOST_OS_DARWIN, 1, [Define this if built on Mac OS X/Darwin])
    CFLAGS="-O3 -pipe -fomit-frame-pointer $m_wm $m_psb -fexpensive-optimizations $f_si $f_nsa -ffast-math -funroll-loops -finline-functions -no-cpp-precomp -D_INTL_REDIRECT_MACROS $CFLAGS"
    DEBUG_CFLAGS="-O3 $DEBUG_CFLAGS"
    
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
    CFLAGS="-O3 -pipe -fomit-frame-pointer $m_wm $m_psb -fexpensive-optimizations $f_si $f_nsa -ffast-math -funroll-loops -finline-functions $CFLAGS"
    DEBUG_CFLAGS="-O3 $DEBUG_CFLAGS"
    dnl avoid ppc compilation crash
    AS="$CC"
    AC_DEFINE_UNQUOTED(FPM_PPC,,[Define to select libmad fixed point arithmetic implementation])
    AC_DEFINE_UNQUOTED(ARCH_PPC,,[Define this if you're running PowerPC architecture])
    
    if test x$enable_altivec = xyes; then
        AC_DEFINE_UNQUOTED(ENABLE_ALTIVEC,,[Define this if you have a Motorola 74xx CPU])
        CFLAGS="-Wa,-m7400 $CFLAGS"
	DEBUG_CFLAGS="-Wa,-m7400 $DEBUG_CFLAGS"
    fi
    ;;

  sparc*-*-linux*)
    case $host_alias in
    sparc-*)    cpu_cflags="-mcpu=supersparc -mtune=supersparc" ;;
    sparc64-*)
      cpu_cflags="-mcpu=ultrasparc -mtune=ultrasparc"
      if test x$enable_vis = xyes; then
        AC_DEFINE_UNQUOTED(ENABLE_VIS,,[Define this if you have Sun UltraSPARC CPU])
      fi
    ;;
    esac

    CFLAGS="-O3 $cpu_cflags -funroll-loops -finline-functions $CFLAGS"
    DEBUG_CFLAGS="-O $cpu_cflags -funroll-loops -finline-functions $DEBUG_CFLAGS"

    AC_DEFINE_UNQUOTED(FPM_SPARC,,[Define to select libmad fixed point arithmetic implementation])
    AC_DEFINE_UNQUOTED(ARCH_SPARC,,[Define this if you're running SPARC architecture])
    ;;

  sparc-*-solaris*)
    if test "$GCC" = yes; then
	case `uname -m` in
	sun4c) cpu_cflags="-mcpu=v7 -mtune=supersparc" ;;
	sun4m) cpu_cflags="-mcpu=v8 -mtune=supersparc" ;;
	sun4u)
	  case `$CC --version 2>/dev/null` in
	  1.*|2.*)
	    # -mcpu=ultrasparc triggers a GCC 2.95.x compiler bug when
	    # compiling video_out.c:
	    #   gcc: Internal compiler error: program cc1 got fatal signal 11
	    # avoid -mcpu=ultrasparc with gcc 2.*
            cpu_cflags="-mcpu=v8 -mtune=ultrasparc"
	    ;;
          *)
	    # GCC 3 or newer should have no problem with -mcpu=ultrasparc 
            cpu_cflags="-mcpu=ultrasparc -mtune=ultrasparc"
            if test x$enable_vis = xyes; then
	      AC_DEFINE_UNQUOTED(ENABLE_VIS,,[Define this if you have Sun UltraSPARC CPU])
            fi
	    ;;
	  esac
          ;;
	*)     cpu_cflags= ;;
	esac

	cc_optimize_cflags="-O3 $cpu_cflags -funroll-loops -finline-functions"
	cc_debug_cflags="-O $cpu_cflags -funroll-loops -finline-functions"

        AC_DEFINE_UNQUOTED(FPM_SPARC,,[Define to select libmad fixed point arithmetic implementation])		dnl uses gnu c asm extensions
        AC_DEFINE_UNQUOTED(ARCH_SPARC,,[Define this if you're running SPARC architecture])
    else
	case `uname -m` in
	sun4c) cpu_cflags="-xarch=v7" ;;
	sun4m) cpu_cflags="-xarch=v8" ;;
	sun4u) cpu_cflags="-xarch=v8plusa" ;;
	esac

	cc_optimize_cflags="-fast $cpu_cflags -xCC"
	cc_debug_cflags="-O"
    	AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed point arithmetic implementation])		dnl use portable version with non-gcc
    fi
    CFLAGS="$cc_optimize_cflags $CFLAGS"
    DEBUG_CFLAGS="$cc_debug_cflags $DEBUG_CFLAGS"
    ;;

  mips-*)
    CFLAGS="-O3 $CFLAGS"
    DEBUG_CFLAGS="-O $DEBUG_CFLAGS"
    AC_DEFINE_UNQUOTED(FPM_MIPS,,[Define to select libmad fixed point arithmetic implementation])
    ;;

  m68k-*)
    # used to be -O2, but that makes gcc 2.95.2 segfault
    # see http://bugs.debian.org/146006 for more info
    CFLAGS="-O $CFLAGS"
    DEBUG_CFLAGS="-O $DEBUG_CFLAGS"
    AC_DEFINE_UNQUOTED(FPM_DEFAULT,,[Define to select libmad fixed point arithmetic implementation])
    ;;

  ia64-*)
    CFLAGS="-O3 $CFLAGS"
    DEBUG_CFLAGS="-O3 $DEBUG_CFLAGS"
    AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed pointarithmetic implementation])
  ;;

  hppa-*)
    dnl -O3 seems to trigger gcc internal compiler errors, at least up to gcc 3.2.1
    CFLAGS="-O $CFLAGS"
    DEBUG_CFLAGS="-O $DEBUG_CFLAGS"
    AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed pointarithmetic implementation])
    ;;

  hppa*-hp-*)
    dnl -O3 seems to trigger gcc internal compiler errors, at least up to gcc 3.2.1
    CFLAGS="-O2 $CFLAGS"
    DEBUG_CFLAGS="-O2 $DEBUG_CFLAGS"
    AC_DEFINE_UNQUOTED(FPM_DEFAULT,,[Define to select libmad fixed pointarithmetic implementation])
    ;;

  x86_64-*)
    CFLAGS="-O3 -fomit-frame-pointer $m_wm $m_psb -fexpensive-optimizations $f_si $f_nsa -ffast-math -funroll-loops -finline-functions $CFLAGS"
    DEBUG_CFLAGS="-O0 -g $DEBUG_CFLAGS"
    AC_DEFINE_UNQUOTED(ARCH_X86_64,,[Define this if you're running x86 architecture])
    AC_DEFINE(FPM_64BIT,1,[Define to select libmad fixed point arithmetic implementation])
    ;;

  s390-*)
    CFLAGS="-O3 $CFLAGS"
    DEBUG_CFLAGS="-O3 $DEBUG_CFLAGS"
    AC_DEFINE_UNQUOTED(FPM_DEFAULT,,[Define to select libmad fixed point arithmetic implementation])
    ;;

  armv4l-*-linux*)
    CFLAGS="-O2 -fsigned-char -ffast-math -mcpu=strongarm1100 -fomit-frame-pointer -fthread-jumps -fregmove $CFLAGS"
    dnl    CFLAGS="-O1 -fforce-mem -fforce-addr -fthread-jumps -fcse-follow-jumps -fcse-skip-blocks -fexpensive-optimizations -fregmove -fschedule-insns2 -finline-functions -fsigned-char -fomit-frame-pointer -march=armv4 -mtune=strongarm $CFLAGS"
    DEBUG_CFLAGS="-O2 $DEBUG_CFLAGS"
    AC_DEFINE_UNQUOTED(FPM_ARM,,[Define to select libmad fixed point arithmetic implementation])
    enable_armv4l="yes"
    ;;
  
  *)
    echo
    echo "****************************** WARNING ******************************"
    echo
    echo "Host type '$host' ($host_alias) is currently not supported by xine"
    echo
    echo "Assuming that -O3 makes the compiler produce useful code."
    echo
    echo "You might experience problems with this, so please report your"
    echo "architecture and, if possible, known good optimization flags for"
    echo "your compiler to <xine-devel@lists.sf.net>"\!
    echo
    echo "*********************************************************************"
    echo "(sleeping one minute so you can read this...)"
    sleep 60

    CFLAGS="-O3 $CFLAGS"
    DEBUG_CFLAGS="-O $DEBUG_CFLAGS"
    AC_DEFINE_UNQUOTED(FPM_DEFAULT,,[Define to select libmad fixed point arithmetic implementation])
    ;;
esac

AM_CONDITIONAL(HOST_OS_DARWIN, test x"$HOST_OS_DARWIN" = "x1")

AM_CONDITIONAL(HAVE_W32DLL, test x"$enable_w32dll" = "xyes")
if test x"$enable_w32dll" = "xyes"; then
   W32DLL_DEP=""
else 
   W32DLL_DEP="#"
fi
AC_SUBST(W32DLL_DEP)

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
if test x"$SYS" = "xmingw32" -o x"$SYS" = "xcygwin"; then
  dnl installation directories
  eval XINE_PLUGINDIR="${bindir}/plugins"
  eval XINE_FONTDIR="${bindir}/fonts"
  eval XINE_LOCALEDIR="${bindir}/locale"
  dnl directories from xine-config
  XINE_PLUGINPATH="\`dirname \$0\`/plugins"
  XINE_FONTPATH="\`dirname \$0\`/fonts"
  XINE_LOCALEPATH="\`dirname \$0\`/locale"
  dnl runtime directories
  AC_DEFINE(XINE_PLUGINDIR,[exec_path_append_subdir("plugins")],[Define this to plugins directory location])
  AC_DEFINE(XINE_FONTDIR,[exec_path_append_subdir("fonts")],[Define this to osd fonts dir location])
  AC_DEFINE(XINE_LOCALEDIR,[exec_path_append_subdir("locale")],[Path where catalog files will be.])
  dnl prefix in xine-config
  XINE_CONFIG_PREFIX="\`dirname \$0\`/.."
else
  dnl installation directories
  XINE_PLUGINDIR="$libdir/xine/plugins/$XINE_MAJOR.$XINE_MINOR.$XINE_SUB"
  XINE_FONTDIR="${datadir}/xine/libxine$XINE_MAJOR/fonts"
  XINE_LOCALEDIR="${datadir}/locale"
  dnl directories from xine-config and runtime directories
  eval XINE_PLUGINPATH=`eval echo "$XINE_PLUGINDIR"`
  eval XINE_FONTPATH="$XINE_FONTDIR"
  eval XINE_LOCALEPATH="$XINE_LOCALEDIR"
  dnl defining runtime directories
  AC_DEFINE_UNQUOTED(XINE_PLUGINDIR,"$XINE_PLUGINPATH",[Define this to plugins directory location])
  AC_DEFINE_UNQUOTED(XINE_FONTDIR,"$XINE_FONTPATH",[Define this to osd fonts dir location])
  AC_DEFINE_UNQUOTED(XINE_LOCALEDIR, "$XINE_LOCALEPATH",[Path where catalog files will be.])
  dnl prefix in xine-config
  XINE_CONFIG_PREFIX="${prefix}"
fi
AC_SUBST(XINE_CONFIG_PREFIX)
AC_SUBST(XINE_PLUGINPATH)
AC_SUBST(XINE_FONTPATH)
AC_SUBST(XINE_LOCALEPATH)
AC_SUBST(XINE_PLUGINDIR)
AC_SUBST(XINE_FONTDIR)
AC_SUBST(XINE_LOCALEDIR)

dnl Where aclocal m4 files should be installed
XINE_ACFLAGS="-I ${datadir}/aclocal"
AC_DEFINE_UNQUOTED(XINE_ACFLAGS, "$XINE_ACFLAGS", [Path where aclocal m4 files will be.])
AC_SUBST(XINE_ACFLAGS)

dnl Where architecture independent data (e.g. logo) will/should be installed
eval XINE_DATADIR="${datadir}/xine"
AC_SUBST(XINE_DATADIR)

dnl Where scripts will/should be installed.
eval XINE_SCRIPTPATH="${datadir}/xine/scripts"
AC_SUBST(XINE_SCRIPTPATH)


dnl ---------------------------------------------
dnl Some informations about xine-lib compilation
dnl ---------------------------------------------

XINE_BUILD_CC="`$CC -v 2>&1 | tail -1`"
XINE_BUILD_OS="`uname -s -r -m`"
XINE_BUILD_DATE="`date \"+%a %d %b %Y %T\"`"
AC_SUBST(XINE_BUILD_CC)
AC_SUBST(XINE_BUILD_OS)
AC_SUBST(XINE_BUILD_DATE)


dnl ---------------------------------------------
dnl For win32 libraries location, needed by libw32dll.
dnl ---------------------------------------------

AC_ARG_WITH(w32-path, AC_HELP_STRING([--with-w32-path=path], [location of WIN32 libraries]),
            w32_path="$withval", w32_path="/usr/lib/win32")
AC_SUBST(w32_path)


dnl ---------------------------------------------
dnl some include paths ( !!! DO NOT REMOVE !!! )
dnl ---------------------------------------------

INCLUDES='-I$(top_srcdir) -I$(top_builddir)/include -I$(top_srcdir)/include -I$(top_srcdir)/src -I$(top_builddir)/src/xine-engine -I$(top_srcdir)/src/xine-engine -I$(top_srcdir)/src/xine-utils $(INTLDIR) -I$(top_builddir)/src/input -I$(top_srcdir)/src/input $(WIN32_CPPFLAGS) $(THREAD_INCLUDES) $(ZLIB_INCLUDES) -I$(top_srcdir)/lib'
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
  MKINSTALLDIRS="$ac_aux_dir/install-sh -d"
else
  MKINSTALLDIRS="\$(top_srcdir)/install-sh -d"
fi
AC_SUBST(MKINSTALLDIRS)

dnl this is an internal function we should not use, but
dnl as long as neither autoconf nor automake offer an A[CM]_PROG_OBJC
dnl check we will have to call it
_AM_DEPENDENCIES([OBJC])


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
intl/Makefile
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
src/libflac/Makefile
src/liblpcm/Makefile
src/libmad/Makefile
src/libmpeg2/Makefile
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
src/xine-engine/xineintl.h
win32/Makefile
win32/contrib/Makefile
win32/contrib/pthreads/Makefile
win32/contrib/zlib/Makefile
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
echo "   - ac3           - aac"
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
if test x"$have_modplug" = x"yes"; then
  echo "   - mod"
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
if test x"$have_libpng" = "xyes"; then
  echo "   - png"
fi
if test x"$no_theora" != "xyes"; then
  echo "   - theora"
fi
echo ""

dnl audio decoders
echo " * audio decoder plugins:"
echo "   - A52/ra-dnet     - DTS"
echo "   - MAD (MPG 1/2/3) - GSM 06.10"
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
echo "     - FLAC"
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
    if test x$xv_lib="xlibXv.a"; then
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
  if test x"$have_alsa09" = "xyes"; then
    echo "   - alsa (Alsa 0.9.x)"
  fi
fi
if test x"$no_esd" != "xyes"; then
  echo "   - esd (Enlightened Sound Daemon)"
fi
if test x"$no_arts" != "xyes"; then
  echo "   - arts (aRts - KDE soundserver)"
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
