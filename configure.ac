dnl
dnl Configure.in for xine
dnl

AC_INIT
AC_CONFIG_SRCDIR([src/xine-engine/xine.c])

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
dnl    if (any interfaces have been _removed_ or incompatibly changed)
dnl       AGE = 0;
dnl }

LT_CURRENT=1
LT_REVISION=0
LT_AGE=0

dnl for a release tarball do "rm .cvsversion" before "make dist"
if test -f .cvsversion; then
  XINE_PRE="cvs"
else
  XINE_PRE="beta1"
fi

AC_SUBST(XINE_MAJOR)
AC_SUBST(XINE_MINOR)
AC_SUBST(XINE_SUB)
AC_SUBST(XINE_IFACE_AGE)
AC_SUBST(XINE_BIN_AGE)

AC_DEFINE_UNQUOTED(XINE_MAJOR, $XINE_MAJOR,[Xine major version number])
AC_DEFINE_UNQUOTED(XINE_MINOR, $XINE_MINOR,[Xine minor version number])
AC_DEFINE_UNQUOTED(XINE_SUB, $XINE_SUB, [Xine sub version number])

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
ALL_LINGUAS="fr pt_BR de sk es pl_PL cs"
AM_GNU_GETTEXT(use-libtool, need-ngettext)
AC_PROG_GMSGFMT_PLURAL
AC_CONFIG_LINKS($nls_cv_header_libgt, $nls_cv_header_intl)
AM_CONDITIONAL(INCLUDED_INTL, test x$USE_INCLUDED_LIBINTL = "xyes")
if test x"$USE_INCLUDED_LIBINTL" = x"yes"; then
   INTLDIR="-I\$(top_builddir)/intl -I\$(top_srcdir)/intl"
fi
dnl AC_SUBST(INTLDIR)



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
dnl AC_CHECK_TYPES([ptrdiff_t])

dnl ---------------------------------------------
dnl debug cflags
dnl ---------------------------------------------
AC_SUBST(DEBUG_CFLAGS)
DEBUG_CFLAGS="$CFLAGS -g -DDEBUG"

dnl ---------------------------------------------
dnl threads
dnl ---------------------------------------------
case "$host" in
  *-*-freebsd*)
    THREAD_LIBS="-L/usr/local/lib -pthread"
    THREAD_CFLAGS="-I/usr/local/include -D_THREAD_SAFE"

    CFLAGS="$CFLAGS $THREAD_CFLAGS"
    DEBUG_CFLAGS="$DEBUG_CFLAGS"

    ;;
  *)
    AC_CHECK_LIB(pthread, pthread_create,
  	     [THREAD_LIBS="-lpthread"],
 	     [AC_MSG_ERROR(pthread needed)])
    ;;
esac
AC_SUBST(THREAD_LIBS)
dnl needed for xine-config
AC_SUBST(THREAD_CFLAGS)


dnl ---------------------------------------------
dnl dynamic linker
dnl ---------------------------------------------
AC_CHECK_LIB(c, dlopen,
	     DYNAMIC_LD_LIBS="",
	     AC_CHECK_LIB(dl, dlopen,
             	          DYNAMIC_LD_LIBS="-ldl",
	                  AC_MSG_ERROR(dynamic linker needed)))
AC_SUBST(DYNAMIC_LD_LIBS)


dnl ---------------------------------------------
dnl mpeg2lib stuff
dnl ---------------------------------------------
AC_SUBST(LIBMPEG2_CONFIG_OBJS)
AC_SUBST(LIBMPEG2_CFLAGS)
AC_SUBST(LIBA52_CFLAGS)
AC_SUBST(LIBFFMPEG_CFLAGS)

LIBMPEG2_CFLAGS="" dnl default include path removed, no more needed.
LIBA52_CFLAGS=""
LIBFFMPEG_CFLAGS="-DSIMPLE_IDCT -DHAVE_AV_CONFIG_H -DRUNTIME_CPUDETECT"

AC_ARG_ENABLE(altivec,
	[  --disable-altivec       use assembly codes for Motorola 74xx CPUs],
		enable_altivec=no, 
		enable_altivec=yes)

AC_ARG_ENABLE(vis, 
	[  --disable-vis            use assembly codes for Sun UltraSPARC cpus],
         	enable_vis=no,
		enable_vis=yes)

if test x$enable_mlib = x; then
AC_ARG_ENABLE(mlib,
	[  --disable-mlib          make a version not using mediaLib],
		enable_mlib=no,
		enable_mlib=yes)
fi
if test x$enable_mlib = xyes; then
    if test x"$MLIBHOME" = x; then
	mlibhome=/opt/SUNWmlib
    else
	mlibhome="$MLIBHOME"
    fi

    AC_CHECK_LIB(mlib, mlib_VideoAddBlock_U8_S16,
               [ LIBS="$LIBS -L$mlibhome/lib -lmlib" 
                 LIBMPEG2_CONFIG_OBJS="$LIBMPEG2_CONFIG_OBJS idct_mlib.lo motion_comp_mlib.lo" 
                 LIBMPEG2_CFLAGS="$LIBMPEG2_CFLAGS -I$mlibhome/include" 
                 LIBA52_CFLAGS="$LIBA52_CFLAGS -I$mlibhome/include" 
                 LIBFFMPEG_CFLAGS="$LIBFFMPEG_CFLAGS -I$mlibhome/include" 
		 AC_DEFINE(HAVE_MLIB,1,[Define this if you have mlib installed])
                 AC_DEFINE(LIBMPEG2_MLIB,1,[Define this if you have mlib installed])
		 AC_DEFINE(LIBA52_MLIB,1,[Define this if you have mlib installed])
		 ac_have_mlib=yes],
               , -L/opt/SUNWmlib/lib)
    AM_CONDITIONAL(HAVE_MLIB, test x$ac_have_mlib = "xyes")
fi


dnl ---------------------------------------------
dnl Checks for X11
dnl ---------------------------------------------
AC_PATH_XTRA
if test x"$no_x" != "xyes"; then
    AC_DEFINE(HAVE_X11,1,[Define this if you have X11R6 installed])
fi
AM_CONDITIONAL(HAVE_X11, [test x"$no_x" != "xyes"])


dnl ---------------------------------------------
dnl zlib
dnl ---------------------------------------------
dnl Test for libz
AC_CHECK_LIB(z, gzsetparams,
	[ AC_CHECK_HEADER(zlib.h,
	  have_zlib=yes
	  ZLIB_LIBS="-lz",)], AC_MSG_ERROR(zlib needed))

AM_CONDITIONAL(HAVE_ZLIB, [test x"$have_zlib" = "xyes"])
AC_SUBST(ZLIB_LIBS)


dnl ---------------------------------------------
dnl Checks for Xv extension
dnl ---------------------------------------------
dnl static linking with libXv is preferred!
dnl but only dynamic linking is possible when using libtool < 1.4.0

AC_PREREQ_LIBTOOL(1.4.0, xv_lib="libXv.a", xv_lib="libXv.so")


dnl ---------------------------------------------
dnl Check for OpenGL & [GLut | GLU]
dnl ---------------------------------------------
AC_CHECK_LIB(GL, glBegin,
	[AC_CHECK_HEADER(GL/gl.h,
             [ ac_have_opengl="yes"
               OPENGL_LIBS="-lGL" 
	       AC_CHECK_LIB(glut, glutInit, dnl check for glut
	                    [ ac_have_glut="yes"
                              GLUT_LIBS="-lglut"
	                      AC_DEFINE(HAVE_GLUT,1,[Define this if you have GLut support available])
			      AC_DEFINE(HAVE_OPENGL,1,[Define this if you have OpenGL support available]) ],
	                    [ ac_have_glut="no" dnl fallback, check for GLU
	                      AC_CHECK_LIB(GLU, gluPerspective,
		                           [ ac_have_glu="yes"
                                             GLU_LIBS="-lGLU -lm" 
		                             AC_DEFINE(HAVE_GLU,1,[Define this if you have GLU support available])
					     AC_DEFINE(HAVE_OPENGL,1,[Define this if you have OpenGL support available]) ],
	                                   [ ac_have_glu="no" ], 
                                           [$X_LIBS $X_PRE_LIBS $OPENGL_LIBS -lGLU -lm $X_EXTRA_LIBS])], 
	                    [$X_LIBS $X_PRE_LIBS -lglut $X_EXTRA_LIBS])
             ],
	[])],
   [],
   [$X_LIBS $X_PRE_LIBS -lGL $X_EXTRA_LIBS])
AC_SUBST(OPENGL_LIBS)
AC_SUBST(GLUT_LIBS)
AC_SUBST(GLU_LIBS)
AM_CONDITIONAL(HAVE_OPENGL, [ test x$ac_have_opengl = "xyes" -a x$ac_have_glut="xyes" -o x$ac_have_opengl = "xyes" -a x$ac_have_glu="xyes" ] )



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
			CFLAGS="$CFLAGS -I/usr/local/include"
			LDFLAGS="$LDFLAGS -L/usr/local/lib"
	                GICONV_BSD_LIBS="-lgiconv"
		        no_BSDiconv="no", 
                        [ no_BSDiconv="yes"
	                  echo
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
        no_BSDiconv="no"
        ;;
esac
AC_SUBST(GICONV_BSD_LIBS)
AM_CONDITIONAL(HAVE_GICONV, test x$no_BSDiconv != "xyes")
AM_CONDITIONAL(PPC_ARCH, test x$ppc_arch = "xyes")


AC_ARG_WITH(xv-path,[  --with-xv-path=path     Where $xv_lib is installed],
            xv_path="$withval", xv_path="/usr/X11R6/lib")

AC_CHECK_LIB(Xv, XvShmCreateImage, 
             [ AC_MSG_CHECKING(for $xv_lib location)
               if test -f "$xv_path/$xv_lib"; then
                 AC_MSG_RESULT(found in $xv_path)
                 XV_LIB="-lXv"
	         AC_DEFINE(HAVE_XV,1,[Define this if you have libXv installed])
	         ac_have_xv="yes"
               else
                 AC_MSG_RESULT(not found in $xv_path)
	         echo
                 echo "****************************************************************"
		 echo "* if you don't have a libXv.so on your system, use:            *"
		 echo "* ld --whole-archive -shared -o libXv.so.1 libXv.a             *"
		 echo "* then: ln -s libXv.so.1 libXv.so                              *"
	         echo "* to create it or try to use --with-xv-path to set the         *"
                 echo "* location of libXv.so                                         *"
                 echo "****************************************************************"
	         echo
               fi ],, [$X_LIBS $X_PRE_LIBS -lXext $X_EXTRA_LIBS])
AM_CONDITIONAL(HAVE_XV, test x$ac_have_xv = "xyes")
dnl
dnl xine_check use Xv functions API.
dnl
if test x$ac_have_xv = "xyes"; then
  EXTRA_X_LIBS="-L$xv_path $XV_LIBS -lXext"
  EXTRA_X_CFLAGS=""
fi
AC_SUBST(XV_LIB)
AC_SUBST(EXTRA_X_LIBS)
AC_SUBST(EXTRA_X_CFLAGS)

dnl ---------------------------------------------
dnl Checks for Xinerama extension
dnl ---------------------------------------------

AC_CHECK_LIB(Xinerama, XineramaQueryExtension, 
             [X_LIBS="$X_LIBS -lXinerama"
              AC_DEFINE(HAVE_XINERAMA,1,[Define this if you have libXinerama installed])
              ac_have_xinerama="yes"],,
             [$X_LIBS $X_PRE_LIBS -lXext $X_EXTRA_LIBS])
AM_CONDITIONAL(HAVE_XINERAMA, test x$ac_have_xinerama = "xyes")

 
dnl ---------------------------------------------
dnl Checks for Ascii-Art library
dnl ---------------------------------------------
AM_PATH_AALIB(1.4,, AC_MSG_RESULT([*** All of AALIB dependent parts will be disabled ***]))
AM_CONDITIONAL(HAVE_AA, test x$no_aalib != "xyes")

dnl ---------------------------------------------
dnl Check solaris framebuffer device support
dnl ---------------------------------------------
AC_CHECK_HEADER(sys/fbio.h, ac_have_sunfb=yes,)
AM_CONDITIONAL(HAVE_SUNFB, [test x"$ac_have_sunfb" = "xyes"])

dnl ---------------------------------------------
dnl Check linux framebuffer device support
dnl ---------------------------------------------
AC_CHECK_HEADER(linux/fb.h, have_fb=yes,)
AM_CONDITIONAL(HAVE_FB, [test x"$have_fb" = "xyes"])

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
  DIRECTFB_REQUIRED_VERSION=0.9.9

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
dnl dxr3 / hollywood plus card
dnl ---------------------------------------------
AC_CHECK_DXR3()
if test x"$have_libfame" = "xyes" ; then
  AC_DEFINE_UNQUOTED(HAVE_LIBFAME,1,[Define this if you have libfame mpeg encoder installed (fame.sf.net)])
  AM_PATH_LIBFAME(0.8.10, 
    AC_DEFINE(HAVE_NEW_LIBFAME,1,[Define this if you have libfame 0.8.10 or above]))
fi
if test x"$have_librte" = "xyes" ; then
  AC_DEFINE_UNQUOTED(HAVE_LIBRTE,1,[Define this if you have librte mpeg encoder installed (zapping.sf.net)])
fi
AM_CONDITIONAL(HAVE_DXR3, test x"$have_dxr3" = "xyes")
AM_CONDITIONAL(HAVE_LIBFAME, test x"$have_libfame" = "xyes")
AM_CONDITIONAL(HAVE_LIBRTE, test x"$have_librte" = "xyes")
AM_CONDITIONAL(HAVE_ENCODER, test x"$have_encoder" = "xyes")


dnl ---------------------------------------------
dnl Vidix/libdha
dnl ---------------------------------------------
AC_LINUX_PATH(/usr/src/linux)
AC_SUBST([LINUX_INCLUDE])

AC_ARG_ENABLE(vidix, [  --disable-vidix         Do not build vidix support],
	      check_vidix=no, check_vidix=yes)
AC_ARG_ENABLE(dha-kmod, [  --enable-dha-kmod       Build DHA kernel module],
	      ,,enable_dha_kmod="yes")

enable_vidix="no"

AC_MSG_CHECKING(for vidix support)
if test x"$check_vidix" = "xyes" -a x"$ac_cv_prog_AWK" != "xno"; then
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
AC_CHECK_PROG(DEPMOD, depmod, depmod, no)

dnl ---------------------------------------------
dnl Check for divx4
dnl ---------------------------------------------
dnl AC_CHECK_DIVX4(, AC_MSG_RESULT(*** All DIVX4 dependent parts will be disabled ***))
dnl AM_CONDITIONAL(HAVE_DIVX4, test x"$no_divx4" != "xyes")


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
dnl XviD libs.
dnl ---------------------------------------------
AM_PATH_XVID([], AC_MSG_RESULT([*** All XviD dependent parts will be disabled ***]))
AM_CONDITIONAL(HAVE_XVID, [test x"$no_xvid" != "xyes"])	


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
AM_PATH_ALSA(0.9.0, 
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
	[AC_MSG_RESULT([*** DVD and (S)VCD support will be disabled ***])])
AM_CONDITIONAL(HAVE_CDROM_IOCTLS, [test x"$have_cdrom_ioctls" = "xyes"])

dnl ---------------------------------------------
dnl check for a usable version of libdvdnav
dnl ---------------------------------------------
AM_PATH_DVDNAV(0.1.4,
	AC_DEFINE(HAVE_DVDNAV,1,[Define this if you have a suitable version of libdvdnav]),
	[AC_MSG_RESULT([*** no usable version of libdvdnav found, using internal copy ***])])
AM_CONDITIONAL(HAVE_DVDNAV, [test x"$no_dvdnav" != "xyes"])


dnl ---------------------------------------------
dnl ASF build can be optional
dnl ---------------------------------------------
AC_ARG_ENABLE(asf, [  --disable-asf           Do not build ASF demuxer],
	      enable_asf=no, enable_asf=yes)
AM_CONDITIONAL(BUILD_ASF, test x"$enable_asf" = "xyes")

dnl ---------------------------------------------
dnl FAAD build can be optional
dnl ---------------------------------------------
AC_ARG_ENABLE(faad, [  --disable-faad          Do not build FAAD decoder],
	      enable_faad=no, enable_faad=yes)
AM_CONDITIONAL(BUILD_FAAD, test x"$enable_faad" = "xyes")


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
AC_ARG_ENABLE(fpic,
   [  --disable-fpic          Disable -fPIC on shared libs (default on x86)],
   no_fpic=yes, no_fpic=no)


dnl ---------------------------------------------
dnl Build xine plugins with only the "xine_plugin_info" symbol exported
dnl (let the user disable this feature, in case libtool does not offer this
dnl feature on the target platform)
dnl ---------------------------------------------
AC_ARG_ENABLE(min-symtab,
   [  --disable-min-symtab    Export all global symbols from xine plugins ],
   XINE_PLUGIN_MIN_SYMS="",
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
dnl Some extra checks.
dnl ---------------------------------------------
AC_CHECK_LIB(posix4, sched_get_priority_min)
AC_CHECK_FUNCS([vsscanf sigaction sigset getpwuid_r nanosleep strsep strpbrk setenv])
AC_CHECK_HEADERS(byteswap.h malloc.h sys/mman.h sys/mixer.h)


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
    ANSI_FLAGS="-std=gnu89"
    AC_SUBST(ANSI_FLAGS)
fi

dnl Flags not supported by all *cc* variants
AC_TRY_CFLAGS("-Wall", wall="-Wall", wall="")


dnl Common cflags for all platforms
CFLAGS="$CFLAGS $wall -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DXINE_COMPILE"
DEBUG_CFLAGS="$DEBUG_CFLAGS $wall -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DXINE_COMPILE"

enable_w32dll="no"
enable_ffmmx="no"
enable_armv4l="no"

case "$host_or_hostalias" in
  i386-*-freebsd*)
    CFLAGS="$CFLAGS -pipe -fomit-frame-pointer $f_af $f_al $f_aj $m_wm $m_psb -fexpensive-optimizations $f_si $f_nsa -ffast-math -funroll-loops -finline-functions"
    DEBUG_CFLAGS="$X_CFLAGS $DEBUG_CFLAGS"
    AC_DEFINE(__i386__,1,[Define this if you're running x86 architecture])
    AC_DEFINE_UNQUOTED(ARCH_X86,,[Define this if you're running x86 architecture])
    AC_DEFINE_UNQUOTED(FPM_INTEL,,[Define to select libmad fixed point arithmetic implementation])

    enable_w32dll="yes"
    enable_ffmmx="yes"
    no_fpic="yes"
    ;;



  i?86-*-linux* | i386-*-solaris* | i?86-* | k?-* | athlon-* | pentium*)
    if test "$GCC" = yes; then
	dnl Check for gcc cpu optimization support
	AC_TRY_CFLAGS("-mcpu=i386", 
		sarchopt="-mcpu",
		AC_TRY_CFLAGS("-march=i386", 
			sarchopt="-march",
			[ AC_MSG_RESULT(** no cpu optimization supports **)
			  sarchopt=no ]))

	dnl special check for k7 cpu CC support
        AC_TRY_CFLAGS("$sarchopt=athlon", k7cpu="athlon", k7cpu="i686")

	dnl add x86 specific gcc CFLAGS
	CFLAGS="$CFLAGS -O3 -pipe -fomit-frame-pointer $f_af $f_al $f_aj $m_wm $m_psb -fexpensive-optimizations $f_si $f_nsa -ffast-math -funroll-loops -finline-functions"

	DEBUG_CFLAGS="$DEBUG_CFLAGS -O"

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
		CFLAGS="$CFLAGS $sarchopt=$archopt_val"
		DEBUG_CFLAGS="$DEBUG_CFLAGS $sarchopt=$archopt_val"
	    fi
	    ]
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
    else
	dnl add x86 specific cc CFLAGS
	CFLAGS="$CFLAGS -O"
	DEBUG_CFLAGS="$DEBUG_CFLAGS -O"
    	AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed point arithmetic implementation])
    fi

    no_fpic="yes"
    AC_DEFINE(__i386__,1,[Define this if you're running x86 architecture])
    ;;

  alphaev56-*)
    CFLAGS="$CFLAGS -O3 -mcpu=ev56 -mieee"
    DEBUG_CFLAGS="$DEBUG_CFLAGS -O3 -mcpu=ev56 -mieee"
    AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed point arithmetic implementation])
    ;;

  alpha*)
    CFLAGS="$CFLAGS -O3 -mieee"
    DEBUG_CFLAGS="$DEBUG_CFLAGS -O3 -mieee"
    AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed point arithmetic implementation])
    ;;

  ppc-*-linux* | powerpc-*) 
    CFLAGS="$CFLAGS -O3 -pipe -fomit-frame-pointer $m_wm $m_psb -fexpensive-optimizations $f_si $f_nsa -ffast-math -funroll-loops -funroll-all-loops -finline-functions"
    DEBUG_CFLAGS="$DEBUG_CFLAGS -O3"
    dnl avoid ppc compilation crash
    AS="$CC"
    AC_DEFINE_UNQUOTED(FPM_PPC,,[Define to select libmad fixed point arithmetic implementation])
    AC_DEFINE_UNQUOTED(ARCH_PPC,,[Define this if you're running PowerPC architecture])
    
    if test x$enable_altivec = xyes; then
        AC_DEFINE_UNQUOTED(ENABLE_ALTIVEC,,[Define this if you have a Motorola 74xx CPU])
        CFLAGS="$CFLAGS -Wa,-m7400"
	DEBUG_CFLAGS="$DEBUG_CFLAGS -Wa,-m7400"
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

    CFLAGS="$CFLAGS -O3 $cpu_cflags -funroll-loops -funroll-all-loops -finline-functions"
    DEBUG_CFLAGS="$DEBUG_CFLAGS -O $cpu_cflags -funroll-loops -funroll-all-loops -finline-functions"

    AC_DEFINE_UNQUOTED(FPM_SPARC,,[Define to select libmad fixed point arithmetic implementation])
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

	cc_optimize_cflags="-O3 $cpu_cflags -funroll-loops -funroll-all-loops -finline-functions"
	cc_debug_cflags="-O $cpu_cflags -funroll-loops -funroll-all-loops -finline-functions"
        AC_DEFINE_UNQUOTED(FPM_SPARC,,[Define to select libmad fixed point arithmetic implementation])		dnl uses gnu c asm extensions
    else
	case `uname -m` in
	sun4c) cpu_cflags="-xarch=v7 -xchip=ultra2" ;;
	sun4m) cpu_cflags="-xarch=v8 -xchip=ultra2" ;;
	sun4u) cpu_cflags="-xarch=v8plusa -xchip=ultra2" ;;
	esac

	cc_optimize_cflags="-xO5 $cpu_cflags -xCC"
	cc_debug_cflags="-O"
    	AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed point arithmetic implementation])		dnl use portable version with non-gcc
    fi
    CFLAGS="$CFLAGS $cc_optimize_cflags"
    DEBUG_CFLAGS="$DEBUG_CFLAGS $cc_debug_cflags"
    ;;

  mips-*)
    CFLAGS="$CFLAGS -O3"
    DEBUG_CFLAGS="$DEBUG_CFLAGS -O"
    AC_DEFINE_UNQUOTED(FPM_MIPS,,[Define to select libmad fixed point arithmetic implementation])
    ;;

  m68k-*)
    # used to be -O2, but that makes gcc 2.95.2 segfault
    # see http://bugs.debian.org/146006 for more info
    CFLAGS="$CFLAGS -O"
    DEBUG_CFLAGS="$DEBUG_CFLAGS -O"
    AC_DEFINE_UNQUOTED(FPM_DEFAULT,,[Define to select libmad fixed point arithmetic implementation])
    ;;

  ia64-*)
    CFLAGS="$CFLAGS -O3"
    DEBUG_CFLAGS="$DEBUG_CFLAGS -O3"
    AC_DEFINE_UNQUOTED(FPM_64BIT,,[Define to select libmad fixed point arithmetic implementation])
    ;;

  s390-*)
    CFLAGS="$CFLAGS -O3"
    DEBUG_CFLAGS="$DEBUG_CFLAGS -O3"
    AC_DEFINE_UNQUOTED(FPM_DEFAULT,,[Define to select libmad fixed point arithmetic implementation])
    ;;

  armv4l-*-linux*)
    CFLAGS="$CFLAGS -O2  -fsigned-char  -ffast-math -mcpu=strongarm1100 -fomit-frame-pointer -fthread-jumps  -fregmove"
    dnl    CFLAGS="$CFLAGS -O1 -fforce-mem -fforce-addr -fthread-jumps -fcse-follow-jumps -fcse-skip-blocks -fexpensive-optimizations -fregmove -fschedule-insns2 -finline-functions -fsigned-char -fomit-frame-pointer -march=armv4 -mtune=strongarm"
    DEBUG_CFLAGS="$DEBUG_CFLAGS -O2"
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

    CFLAGS="$CFLAGS -O3"
    DEBUG_CFLAGS="$DEBUG_CFLAGS -O"
    AC_DEFINE_UNQUOTED(FPM_DEFAULT,,[Define to select libmad fixed point arithmetic implementation])
    ;;
esac


CFLAGS="$CFLAGS $ALSA_CFLAGS $ESD_CFLAGS $ARTS_CFLAGS $IRIXAL_CFLAGS"
DEBUG_CFLAGS="$DEBUG_CFLAGS $ALSA_CFLAGS $ESD_CFLAGS $ARTS_CFLAGS $IRIXAL_CFLAGS"

AM_CONDITIONAL(HAVE_W32DLL, test x"$enable_w32dll" = "xyes")
if test x"$enable_w32dll" = "xyes"; then
   W32DLL_DEP=""
else 
   W32DLL_DEP="#"
fi
AC_SUBST(W32DLL_DEP)

AM_CONDITIONAL(HAVE_FFMMX, test x"$enable_ffmmx" = "xyes")

dnl ---------------------------------------------
dnl HAVE_ARMV4L is currently used in libavcodec makefile.am
dnl ---------------------------------------------
AM_CONDITIONAL(HAVE_ARMV4L, test x"$enable_armv4l" = "xyes")


dnl ---------------------------------------------
dnl gcc __attribute__ ((aligned ()))
dnl ---------------------------------------------
AC_C_ATTRIBUTE_ALIGNED



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
XINE_PLUGINDIR="$libdir/xine/plugins/$XINE_MAJOR.$XINE_MINOR.$XINE_SUB"
eval XINE_PLUGINPATH=`eval echo "$XINE_PLUGINDIR"`
AC_DEFINE_UNQUOTED(XINE_PLUGINDIR,"$XINE_PLUGINPATH",[Define this to plugins directory location])
AC_SUBST(XINE_PLUGINPATH)

XINE_FONTDIR="${datadir}/xine/libxine$XINE_MAJOR/fonts"
eval XINE_FONTPATH="$XINE_FONTDIR"
AC_DEFINE_UNQUOTED(XINE_FONTDIR,"$XINE_FONTPATH",[Define this to osd fonts dir location])
AC_SUBST(XINE_FONTPATH)

XINE_LOCALEDIR="${datadir}/locale"
eval XINE_LOCALEPATH="$XINE_LOCALEDIR"
AC_DEFINE_UNQUOTED(XINE_LOCALEDIR, "$XINE_LOCALEPATH",[Path where catalog files will be.])
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
AC_ARG_WITH(w32-path,[  --with-w32-path=path    Location of WIN32 libraries],
            w32_path="$withval", w32_path="/usr/lib/win32")
AC_SUBST(w32_path)


dnl ---------------------------------------------
dnl some include paths ( !!! DO NOT REMOVE !!! )
dnl
INCLUDES='-I$(top_srcdir) -I$(top_builddir)/include -I$(top_srcdir)/include -I$(top_srcdir)/src -I$(top_builddir)/src/xine-engine -I$(top_srcdir)/src/xine-engine -I$(top_srcdir)/src/xine-utils $(INTLDIR)'
AC_SUBST(INCLUDES)


dnl ---------------------------------------------
dnl Get where .m4 should be installed.
dnl ---------------------------------------------
if test x"${ACLOCAL_DIR+set}" != xset; then
  case "`id`" in
    uid=0\(* )
      AC_MSG_CHECKING(for aclocal directory)
      if (aclocal --version) < /dev/null > /dev/null 2>&1; then
        ACLOCAL_DIR="`eval $ACLOCAL --print-ac-dir`"
        AC_MSG_RESULT($ACLOCAL_DIR)
      else
        ACLOCAL_DIR="${prefix}/share/aclocal"
        AC_MSG_RESULT(none - will be installed in $ACLOCAL_DIR)
      fi
      escapedprefix="`echo $prefix | sed -e 's/\\//\\\\\//g'`"
      ACLOCAL_DIR="`echo $ACLOCAL_DIR|sed -e 's/^'$escapedprefix/'\${prefix}'/`"
      ;;
  esac
fi
AC_SUBST(ACLOCAL_DIR)
AM_CONDITIONAL(INSTALL_M4, test x"$ACLOCAL_DIR" != "x")


dnl ---------------------------------------------
dnl It seems automake 1.5 don't take care about this script
dnl ---------------------------------------------
if test ! -z "$am_depcomp"; then
	DEPCOMP="depcomp"
fi
AC_SUBST(DEPCOMP)

dnl ---------------------------------------------
dnl Output configuration files
dnl ---------------------------------------------
AC_CONFIG_FILES([
Makefile
doc/Makefile
doc/man/Makefile
doc/man/en/Makefile
doc/man/en/man3/Makefile
doc/hackersguide/Makefile
include/Makefile
include/xine.h
intl/Makefile
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
src/input/libdvdread/Makefile
src/input/ost/Makefile
src/input/librtsp/Makefile
src/input/libreal/Makefile
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
src/liblpcm/Makefile
src/libmad/Makefile
src/libmpeg2/Makefile
src/libspudec/Makefile
src/libspucc/Makefile
src/libsputext/Makefile
src/libvorbis/Makefile
src/libw32dll/Makefile
src/libw32dll/wine/Makefile
src/libw32dll/DirectShow/Makefile
src/libw32dll/dmo/Makefile
src/libw32dll/qtx/Makefile
src/libw32dll/qtx/qtxsdk/Makefile
src/libxinevdec/Makefile
src/libxineadec/Makefile
src/libxineadec/gsm610/Makefile
src/libreal/Makefile
src/post/Makefile
src/post/planar/Makefile
src/post/goom/Makefile
src/video_out/Makefile
src/video_out/libdha/Makefile
src/video_out/libdha/bin/Makefile
src/video_out/libdha/kernelhelper/Makefile
src/video_out/libdha/oth/Makefile
src/video_out/libdha/sysdep/Makefile
src/video_out/vidix/Makefile
src/video_out/vidix/drivers/Makefile
src/xine-utils/Makefile
src/xine-engine/Makefile
src/xine-engine/xineintl.h])
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
if test x$no_fpic = "xyes"; then
  cat libtool | sed -e 's/^pic_flag=/#pic_flag=/' > libtool-nofpic
else
  cat libtool > libtool-nofpic
fi

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
if test x"$have_cdrom_ioctls" = "xyes"; then
  echo "   - dvd           - vcd"
  echo "   - cda"
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
echo "   - cda           - snd/au"
echo "   - yuv4mpeg2     - real/realaudio"
echo "   - ea wve        - raw dv"
echo "   - interplay mve "
if test x"$enable_asf" = "xyes"; then
  echo "   - asf"
fi
if test x"$no_oggvorbis" = "xno"; then
  echo "   - ogg"
fi
if test x"$have_libmng" = "xyes"; then
  echo "   - mng"
fi
echo ""

dnl video decoders
echo " * video decoder plugins:"
echo "   - mpeg2           - ffmpeg"
echo "   - cyuv            - divx4"
echo "   - cinepak         - msvc"
echo "   - fli             - msrle"
echo "   - rgb             - yuv"
echo "   - roqvideo        - svq1"
echo "   - idcin           - smc"
echo "   - rpza            - wc3video"
echo "   - qtrle           - interplayvideo"
if test x"$no_xvid" = "xno"; then
  echo "   - xvid"
fi
if test x"$have_dxr3" = "xyes"; then
  echo "   - dxr3_video"
fi
if test x"$enable_w32dll" = "xyes"; then
  echo "   - w32dll"
fi
echo ""

dnl audio decoders
echo " * audio decoder plugins:"
echo "   - a52/ra-dnet     - dts"
echo "   - mad             - adpcm"
echo "   - linear pcm      - roqaudio"
echo "   - log pcm         - gsm610"
echo "   - ffmpeg          - interplayaudio"
if test x"$no_oggvorbis" = "xno"; then
  echo "   - vorbis"
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
echo "   - sputext"
if test x"$have_dxr3" = "xyes"; then
  echo "   - dxr3_spu"
fi
echo ""

dnl post plugins
echo " * post effect plugins:"
echo "  * planar video effects:"
echo "   - invert"
echo "  * SFX:"
echo "   - goom"
echo ""

dnl Video plugins
echo " * video driver plugins:"
if test x"$no_x" != "xyes"; then
  echo "   - XShm (X11 shared memory)"
  echo "   - SyncFB (for Matrox G200/G400 cards)"
  dnl Xv
  if test x$ac_have_xv = "xyes"; then
    if test x$xv_lib="xlibXv.a"; then
      echo "   - Xv (XVideo *static*)"
    else
      echo "   - Xv (XVideo *shared*)"
    fi
  fi
  if test x$ac_have_opengl = "xyes" -a x$ac_have_glut="xyes" -o \
	  x$ac_have_opengl = "xyes" -a x$ac_have_glu="xyes"; then	
    echo "   - OpenGL"
  fi
  if test x$ac_have_sunfb = "xyes"; then
    echo "   - PGX64 (for Sun PGX64/PGX24 cards)"
  fi
fi
if test x$no_aalib != "xyes"; then
  echo "   - aa (Ascii ART)"
fi
if test x$have_fb = "xyes"; then
  echo "   - fb (Linux framebuffer device)"
fi
if test x"$no_sdl" != x"yes"; then
  echo "   - sdl (Simple DirectMedia Layer)"
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
  if test x"$enable_dha_kmod" = "xyes"; then
    echo "   - vidix (dhahelper)"
  else
    echo "   - vidix"
  fi
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
echo "---"

dnl
dnl do some checking if necessary paths are set
dnl

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
