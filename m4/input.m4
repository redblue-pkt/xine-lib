dnl -------------
dnl Input Plugins
dnl -------------
AC_DEFUN([XINE_INPUT_PLUGINS], [
    dnl Setup defaults for the target operating system.  For example, v4l is
    dnl only ever available on Linux, so don't bother checking for it unless
    dnl explicitly requested to do so on other operating systems.
    dnl Notes:
    dnl - dvb is Linux only
    dnl - v4l is Linux only

    default_enable_dvb=no
    default_enable_gnomevfs=yes
    default_enable_samba=yes
    default_enable_v4l=no
    default_enable_vcd=yes
    default_enable_vcdo=no
    default_enable_vdr=yes
    default_with_external_dvdnav=no

    case "$host_os" in
        cygwin* | mingw*)
            default_enable_gnomevfs=no
            default_enable_samba=no
            ;;
        darwin*)
            default_enable_gnomevfs=no
            default_enable_samba=no
            ;;
        freebsd*)
            default_enable_vcdo=yes
            ;;
        linux*)
            default_enable_dvb=yes
            default_enable_v4l=yes
            default_enable_vcdo=yes
            ;;
        solaris*)
            default_enable_vcdo=yes
            ;;
    esac

    dnl dvb
    XINE_ARG_ENABLE([dvb], [Enable support for the DVB plugin (Linux only)])
    if test x"$enable_dvb" != x"no"; then
        case "$host_os" in
            linux*) have_dvb=yes ;;
            *) have_dvb=no ;;
        esac
        if test x"$hard_enable_dvb" = x"yes" && test x"$have_dvb" != x"yes"; then
            AC_MSG_ERROR([DVB support requested, but DVB not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_DVB], [test x"$have_dvb" = x"yes"])


    dnl gnome-vfs
    XINE_ARG_ENABLE([gnomevfs], [Enable support for the Gnome-VFS plugin])
    if test x"$enable_gnomevfs" != x"no"; then
        PKG_CHECK_MODULES([GNOME_VFS], [gnome-vfs-2.0], [have_gnomevfs=yes], [have_gnome_vfs=no])
        if test x"$hard_enable_gnomevfs" = x"yes" && test x"$have_gnomevfs" != x"yes"; then
            AC_MSG_ERROR([Gnome-VFS support requested, but Gnome-VFS not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_GNOME_VFS], [test x"$have_gnomevfs" = x"yes"])


    dnl libsmbclient
    XINE_ARG_ENABLE([samba], [Enable support for the Samba plugin])
    if test x"$enable_samba" != x"no"; then
        AC_CHECK_LIB([smbclient], [smbc_init],
                     [AC_CHECK_HEADERS([libsmbclient.h], [have_samba=yes LIBSMBCLIENT_LIBS="-lsmbclient"])])
        AC_SUBST(LIBSMBCLIENT_LIBS)
        if test x"$hard_enable_samba" = x"yes" && test x"$have_samba" != x"yes"; then
            AC_MSG_ERROR([Samba support requested, but Samba not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_LIBSMBCLIENT], [test x"$have_samba" = x"yes"])


    dnl video-for-linux (v4l)
    XINE_ARG_ENABLE([v4l], [Enable Video4Linux support])
    if test x"$enable_v4l" != x"no"; then
        have_v4l=yes
        AC_CHECK_HEADERS([linux/videodev.h linux/videodev2.h], , [have_v4l=no])
        AC_CHECK_HEADERS([asm/types.h])
        if test x"$hard_enable_v4l" = x"yes" && test x"$have_v4l" != x"yes"; then
            AC_MSG_ERROR([Video4Linux support requested, but prerequisite headers not found.])
        fi
    fi
    AM_CONDITIONAL([ENABLE_V4L], [test x"$have_v4l" = x"yes"])


    dnl dvdnav
    dnl XXX: This could be cleaned up so that code does not have to ifdef so much
    XINE_ARG_WITH([external-dvdnav], [Use external dvdnav library (not recommended)])
    if test x"$with_external_dvdnav" != x"no"; then
        ACX_PACKAGE_CHECK([DVDNAV], [0.1.9], [dvdnav-config],
                          [AC_DEFINE([HAVE_DVDNAV], 1, [Define this if you have a suitable version of libdvdnav])],
                          [AC_MSG_RESULT([*** no usable version of libdvdnav found, using internal copy ***])])
    else
        AC_MSG_RESULT([Using included DVDNAV support])
    fi
    AM_CONDITIONAL([WITH_EXTERNAL_DVDNAV], [test x"$with_external_dvdnav" != x"no"])


    dnl Video CD
    dnl XXX: This could be cleaned up so that code does not have it ifdef so much
    XINE_ARG_ENABLE([vcd], [Enable VCD (VideoCD) support])
    if test x"$enable_vcd" != x"no"; then
        PKG_CHECK_MODULES([LIBCDIO], [libcdio >= 0.71])
        PKG_CHECK_MODULES([LIBVCDINFO], [libvcdinfo >= 0.7.23])
        AC_DEFINE([HAVE_VCDNAV], 1, [Define this if you use external libcdio/libvcdinfo])
    fi

    enable_vcdo=no
    test $default_enable_vcdo = no && test x"$enable_vcd" != x"no" && enable_vcdo=yes

    AC_DEFINE([LIBCDIO_CONFIG_H], 1, [Get of rid system libcdio build configuration])
    AC_DEFINE([EXTERNAL_LIBCDIO_CONFIG_H], 1, [Get of rid system libcdio build configuration])
    AC_SUBST(LIBCDIO_CFLAGS)
    AC_SUBST(LIBCDIO_LIBS)
    AC_SUBST(LIBVCD_CFLAGS)
    AC_SUBST(LIBVCD_LIBS)
    AM_CONDITIONAL([ENABLE_VCD], [test x"$enable_vcd" != x"no"])
    AM_CONDITIONAL([ENABLE_VCDO], [test x"$enable_vcdo" != x"no"])


    dnl vdr
    XINE_ARG_ENABLE([vdr], [Enable support for the VDR plugin (default: enabled)])
    AM_CONDITIONAL([ENABLE_VDR], [test x"$enable_vdr" != x"no"])
])
