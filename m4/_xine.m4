dnl AC_C_ALWAYS_INLINE
dnl Define inline to something appropriate, including the new always_inline
dnl attribute from gcc 3.1
dnl Thanks to Michel LESPINASSE <walken@zoy.org>
AC_DEFUN([AC_C_ALWAYS_INLINE],
    [AC_C_INLINE
    if test x"$GCC" = x"yes" -a x"$ac_cv_c_inline" = x"inline"; then
        AC_MSG_CHECKING([for always_inline])
        SAVE_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS -Wall -Werror"
        AC_TRY_COMPILE([],[__attribute__ ((__always_inline__)) void f (void);],
            [ac_cv_always_inline=yes],[ac_cv_always_inline=no])
        CFLAGS="$SAVE_CFLAGS"
        AC_MSG_RESULT([$ac_cv_always_inline])
        if test x"$ac_cv_always_inline" = x"yes"; then
            AC_DEFINE_UNQUOTED([inline],[__attribute__ ((__always_inline__))])
        fi
    fi])

dnl
dnl Check for divx4.
dnl AC_CHECK_DIVX4([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND ]])
AC_DEFUN([AC_CHECK_DIVX4],
  [

    AC_CHECK_HEADER(decore.h,
                    [ no_divx4=no;
	              AC_CHECK_LIB(divxdecore, decore, 
				   [ no_divx4=no; DIVX4_LIBS="$DIVX4_LIBS -ldivxdecore" ], 
			           no_divx4=yes)
                    ], no_divx4=yes)

    AC_SUBST(DIVX4_LIBS)

    if test x"$no_divx4" = "xno" ; then
       ifelse([$1], , :, [$1])
    else
       ifelse([$2], , :, [$2])
    fi

])

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
AC_DEFUN([AC_LINUX_PATH],
  [AC_ARG_WITH(linux-path,
    [  --with-linux-path=PATH  Where the linux sources are located],
            linux_path="$withval", linux_path="$1")
  LINUX_INCLUDE="-I$linux_path/include"
])

dnl AC_CHECK_DXR3()
AC_DEFUN([AC_CHECK_DXR3],
[
  AC_ARG_ENABLE(dxr3,
    [  --disable-dxr3          Do not build the DXR3/HW+ plugins],,
    enable_dxr3=yes)
  if test x"$enable_dxr3" = xyes; then
    have_dxr3=yes
    AC_MSG_RESULT([*** checking for a supported mpeg encoder])
    have_encoder=no
    have_libfame=yes
    AC_CHECK_LIB(fame, fame_open, 
      [AC_CHECK_HEADER(fame.h, true, have_libfame=no)], have_libfame=no)
    if test "$have_libfame" = "yes"; then
      AC_DEFINE(HAVE_LIBFAME)
      have_encoder=yes
    fi
    have_librte=yes
    AC_CHECK_LIB(rte, rte_init, 
      [AC_CHECK_HEADER(rte.h, true, have_librte=no)], have_librte=no)
    if test "$have_librte" = "yes"; then
      AC_DEFINE(HAVE_LIBRTE)
      AC_MSG_WARN([this will probably only work with rte version 0.4!])
      have_encoder=yes
    fi
    if test "$have_encoder" = "yes"; then
      AC_MSG_RESULT([*** found one or more mpeg encoders, will compile in on-the-fly encoding]);
    else
      AC_MSG_RESULT([*** no mpeg encoder found, will support mpeg playing only]);
    fi
  else
    AC_MSG_RESULT([DXR3 plugins will not be built.])
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
         [AC_CACHE_CHECK([for ip_mreqn], [ac_cv_have_ip_mreqn],
		[AC_EGREP_HEADER([ip_mreqn],
			[netinet/in.h],
			[ac_cv_have_ip_mreqn=yes],
			[ac_cv_have_ip_mreqn=no])])
	  if test $ac_cv_have_ip_mreqn = yes; then
             AC_DEFINE([HAVE_IP_MREQN],1,[Define this if you have ip_mreqn in netinet/in.h])
	  fi
])


# AC_PROG_GMSGFMT_PLURAL
# ----------------------
# Validate the GMSGFMT program found by gettext.m4; reject old versions
# of GNU msgfmt that do not support the "msgid_plural" extension.
AC_DEFUN([AC_PROG_GMSGFMT_PLURAL],
 [dnl AC_REQUIRE(AM_GNU_GETTEXT)
  
  if test "$GMSGFMT" != ":"; then
    AC_MSG_CHECKING([for plural forms in GNU msgfmt])

    changequote(,)dnl We use [ and ] in in .po test input

    dnl If the GNU msgfmt does not accept msgid_plural we define it
    dnl as : so that the Makefiles still can work.
    cat >conftest.po <<_ACEOF
msgid "channel"
msgid_plural "channels"
msgstr[0] "canal"
msgstr[1] "canal"

_ACEOF
    changequote([,])dnl

    if $GMSGFMT -o /dev/null conftest.po >/dev/null 2>&1; then
      AC_MSG_RESULT(yes)
    else
      AC_MSG_RESULT(no)
      AC_MSG_RESULT(
	[found GNU msgfmt program is too old, it does not support plural forms; ignore it])
      GMSGFMT=":"
    fi
    rm -f conftest.po
  fi
])# AC_PROG_GMSGFMT_PLURAL


# AC_PROG_LIBTOOL_SANITYCHECK
# ----------------------
# Default configuration of libtool on solaris produces non-working
# plugin modules, when gcc is used as compiler, and gcc does not
# use gnu-ld
AC_DEFUN([AC_PROG_LIBTOOL_SANITYCHECK],
 [dnl AC_REQUIRE(AC_PROG_CC)
  dnl AC_REQUIRE(AC_PROG_LD)
  dnl AC_REQUIRE(AC_PROG_LIBTOOL)

  case $host in
  *-*-solaris*)
    if test "$GCC" = yes && test "$with_gnu_ld" != yes; then
      AC_MSG_CHECKING([if libtool can build working modules])
      cat > conftest1.c <<_ACEOF
#undef NDEBUG
#include <assert.h>
int shlib_func(long long a, long long b) {
  assert(b);
  switch (a&3) {
  case 0: return a/b;
  case 1: return a%b;
  case 2: return (unsigned long long)a/b;
  case 3: return (unsigned long long)a%b;
  }
}
_ACEOF

      cat > conftest2.c <<_ACEOF
#include <dlfcn.h>
int main(){
  void *dl = dlopen(".libs/libconftest.so", RTLD_NOW);
  if (!dl) printf("%s\n", dlerror());
  exit(dl ? 0 : 1);
}
_ACEOF

      if ./libtool $CC -c conftest1.c >/dev/null 2>&1 && \
         ./libtool $CC -o libconftest.la conftest1.lo \
		 -module -avoid-version -rpath /tmp  >/dev/null 2>&1 && \
         ./libtool $CC -o conftest2 conftest2.c -ldl >/dev/null 2>&1
      then
        if ./conftest2 >/dev/null 2>&1; then
          AC_MSG_RESULT(yes)
        else
	  dnl typical problem: dlopen'ed module not self contained, because
	  dnl it wasn't linked with -lgcc
	  AC_MSG_RESULT(no)
	  if grep '^archive_cmds=.*$LD -G' libtool >/dev/null; then
            AC_MSG_CHECKING([if libtool can be fixed])

	    dnl first try to update gcc2's spec file to add the
	    dnl gcc3 -mimpure-text flag

	    libtool_specs=""

	    if $CC -dumpspecs | grep -- '-G -dy -z text' >/dev/null; then
	      $CC -dumpspecs | \
		  sed 's/-G -dy -z text/-G -dy %{!mimpure-text:-z text}/g' \
		  > gcc-libtool-specs
	      libtool_specs=" -specs=`pwd`/gcc-libtool-specs"
	    fi

	    sed -e "s,\$LD -G,\$CC${libtool_specs} -shared -mimpure-text,g" \
		-e 's/ -M / -Wl,-M,/' libtool >libtool-fixed
	    chmod +x libtool-fixed
            if ./libtool-fixed $CC -o libconftest.la conftest1.lo \
		    -module -avoid-version -rpath /tmp  >/dev/null 2>&1 && \
	       ./conftest2 >/dev/null 2>&1; then

	      dnl the fixed libtool works
	      AC_MSG_RESULT(yes)
	      mv -f libtool-fixed libtool

            else
	      AC_MSG_RESULT(no)
            fi
	  fi
        fi 
      else
        AC_MSG_RESULT(no)
      fi
      rm -f conftest1.c conftest1.lo conftest1.o conftest2.c \
		libconftest.la conftest libtool-fixed
      rm -rf .libs
    fi ;;
  esac
])# AC_PROG_LIBTOOL_SANITYCHECK
