dnl
dnl Check for minimum version of libtool
dnl AC_PREREQ_LIBTOOL([MINIMUM VERSION],[ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND ]])
AC_DEFUN([AC_PREREQ_LIBTOOL],
  [
    lt_min_full=ifelse([$1], ,1.3.5,$1)
    lt_min=`echo $lt_min_full | sed -e 's/\.//g'`
    AC_MSG_CHECKING(for libtool >= $lt_min_full)
    lt_version="`grep '^VERSION' $srcdir/ltmain.sh | sed -e 's/VERSION\=//g;s/[[-.a-zA-Z]]//g'`"

    if test $lt_version -lt 100 ; then
      lt_version=`expr $lt_version \* 10`
    fi

    if test $lt_version -lt $lt_min ; then
      AC_MSG_RESULT(no)
      ifelse([$3], , :, [$3])
    fi
    AC_MSG_RESULT(yes)
    ifelse([$2], , :, [$2])
  ])

dnl
AC_DEFUN([AC_CHECK_LIRC],
  [AC_ARG_ENABLE(lirc,
     [  --disable-lirc          Turn off LIRC support.],
     , enable_lirc=yes)

  if test x"$enable_lirc" = xyes; then
     have_lirc=yes
     AC_REQUIRE_CPP
     AC_CHECK_LIB(lirc_client,lirc_init,
           AC_CHECK_HEADER(lirc/lirc_client.h, true, have_lirc=no), have_lirc=no)
     if test "$have_lirc" = "yes"; then

        if test x"$LIRC_PREFIX" != "x"; then
           lirc_libprefix="$LIRC_PREFIX/lib"
  	   LIRC_INCLUDE="-I$LIRC_PREFIX/include"
        fi
        for llirc in $lirc_libprefix /lib /usr/lib /usr/local/lib; do
          AC_CHECK_FILE("$llirc/liblirc_client.a",
             LIRC_LIBS="$llirc/liblirc_client.a"
             AC_DEFINE(HAVE_LIRC),,)
        done
     else
         AC_MSG_RESULT([*** LIRC client support not available, LIRC support will be disabled ***]);
     fi
  fi

     AC_SUBST(LIRC_LIBS)
     AC_SUBST(LIRC_INCLUDE)
])


dnl AC_LINUX_PATH(DEFAULT PATH)
AC_DEFUN(AC_LINUX_PATH,
  [AC_ARG_WITH(linux-path,
    [  --with-linux-path=PATH  Where the linux sources are located],
            linux_path="$withval", linux_path="$1")
  LINUX_INCLUDE="-I$linux_path/include"
])

dnl AC_CHECK_DXR3()
AC_DEFUN(AC_CHECK_DXR3,
[
  AC_ARG_ENABLE(dxr3,
    [  --disable-dxr3          Do not build the DXR3/HW+ plugins],,
    enable_dxr3=yes)
  if test x"$enable_dxr3" = xyes; then
    AC_CHECK_HEADER($linux_path/include/linux/em8300.h, 
         have_dxr3=yes,
         have_dxr3=no
         AC_MSG_RESULT(*** DXR3 support disabled due to missing em8300.h ***))
  else
    AC_MSG_RESULT(DXR3 plugins will not be built.)
    have_dxr3=no
  fi
])


dnl AC_C_ATTRIBUTE_ALIGNED
dnl define ATTRIBUTE_ALIGNED_MAX to the maximum alignment if this is supported
AC_DEFUN([AC_C_ATTRIBUTE_ALIGNED],
    [AC_CACHE_CHECK([__attribute__ ((aligned ())) support],
        [ac_cv_c_attribute_aligned],
        [ac_cv_c_attribute_aligned=0
        for ac_cv_c_attr_align_try in 2 4 8 16 32 64; do
            AC_TRY_COMPILE([],
                [static char c __attribute__ ((aligned($ac_cv_c_attr_align_try))) = 0
; return c;],
                [ac_cv_c_attribute_aligned=$ac_cv_c_attr_align_try])
        done])
    if test x"$ac_cv_c_attribute_aligned" != x"0"; then
        AC_DEFINE_UNQUOTED([ATTRIBUTE_ALIGNED_MAX],
            [$ac_cv_c_attribute_aligned],[maximum supported data alignment])
    fi])

dnl AC_TRY_CFLAGS (CFLAGS, [ACTION-IF-WORKS], [ACTION-IF-FAILS])
dnl check if $CC supports a given set of cflags
AC_DEFUN([AC_TRY_CFLAGS],
    [AC_MSG_CHECKING([if $CC supports $1 flags])
    SAVE_CFLAGS="$CFLAGS"
    CFLAGS="$1"
    AC_TRY_COMPILE([],[],[ac_cv_try_cflags_ok=yes],[ac_cv_try_cflags_ok=no])
    CFLAGS="$SAVE_CFLAGS"
    AC_MSG_RESULT([$ac_cv_try_cflags_ok])
    if test x"$ac_cv_try_cflags_ok" = x"yes"; then
        ifelse([$2],[],[:],[$2])
    else
        ifelse([$3],[],[:],[$3])
    fi])


dnl AC_CHECK_GENERATE_INTTYPES_H (INCLUDE-DIRECTORY)
dnl generate a default inttypes.h if the header file does not exist already
AC_DEFUN([AC_CHECK_GENERATE_INTTYPES],
    [AC_CHECK_HEADER([inttypes.h],[],
        [AC_COMPILE_CHECK_SIZEOF([char],[1])
        AC_COMPILE_CHECK_SIZEOF([short],[2])
        AC_COMPILE_CHECK_SIZEOF([int],[4])
        AC_COMPILE_CHECK_SIZEOF([long long],[8])
        cat >$1/inttypes.h << EOF
#ifndef _INTTYPES_H
#define _INTTYPES_H
/* default inttypes.h for people who do not have it on their system */
#if (!defined __int8_t_defined) && (!defined __BIT_TYPES_DEFINED__)
#define __int8_t_defined
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
#ifdef ARCH_X86
typedef signed long long int64_t;
#endif
#endif
#if (!defined _LINUX_TYPES_H)
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
#ifdef ARCH_X86
typedef unsigned long long uint64_t;
#endif
#endif
#endif
EOF
        ])])


dnl AC_COMPILE_CHECK_SIZEOF (TYPE SUPPOSED-SIZE)
dnl abort if the given type does not have the supposed size
AC_DEFUN([AC_COMPILE_CHECK_SIZEOF],
    [AC_MSG_CHECKING(that size of $1 is $2)
    AC_TRY_COMPILE([],[switch (0) case 0: case (sizeof ($1) == $2):;],[],
        [AC_MSG_ERROR([can not build a default inttypes.h])])
    AC_MSG_RESULT([yes])])


dnl AM_CHECK_CDROM_IOCTLS ([ACTION-IF-YES], [ACTION-IF-NO])
dnl check for CDROM_DRIVE_STATUS in ioctl.h
AC_DEFUN([AM_CHECK_CDROM_IOCTLS],
         [AC_CACHE_CHECK([if cdrom ioctls are available],
	     [am_cv_have_cdrom_ioctls],
             [AC_EGREP_HEADER([CDROM_DRIVE_STATUS],[sys/ioctl.h],
	        am_cv_have_cdrom_ioctls=yes,
                [AC_EGREP_HEADER([CDIOCALLOW],[sys/ioctl.h],
		   am_cv_have_cdrom_ioctls=yes,
                   [AC_EGREP_CPP(we_have_cdrom_ioctls,[
#include <sys/ioctl.h>
#ifdef HAVE_SYS_CDIO_H
#  include <sys/cdio.h>
#endif
#ifdef HAVE_LINUX_CDROM_H
#  include <linux/cdrom.h>
#endif
#if defined(CDROM_DRIVE_STATUS) || defined(CDIOCALLOW) || defined(CDROMCDXA)
  we_have_cdrom_ioctls
#endif
],
                   am_cv_have_cdrom_ioctls=yes,
		   am_cv_have_cdrom_ioctls=no
          )])])])
          have_cdrom_ioctls=$am_cv_have_cdrom_ioctls
          if test "x$have_cdrom_ioctls" = xyes ; then
            ifelse([$1], , :, [$1])
          else
            ifelse([$2], , :, [$2])
          fi
])


dnl AC_CHECK_IP_MREQN
dnl check for struct ip_mreqn in netinet/in.h
AC_DEFUN([AC_CHECK_IP_MREQN],
         [AC_CACHE_CHECK([for ip_mreqn],[ac_cv_have_ip_mreqn],
            [AC_EGREP_HEADER([ip_mreqn],[netinet/in.h],
	        ac_cv_have_ip_mreqn=yes,ac_cv_have_ip_mreqn=no)
          ])
          have_ip_mreqn=$ac_cv_have_ip_mreqn
          if test "x$have_ip_mreqn" = xyes ; then
             AC_DEFINE(HAVE_IP_MREQN)
          fi
])

