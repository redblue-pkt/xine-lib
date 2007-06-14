dnl ---------------------------
dnl Decoder and Demuxer Plugins
dnl ---------------------------
AC_DEFUN([XINE_DECODER_PLUGINS], [
    dnl a52dec (optional; enabled by default; external version allowed)
    AC_ARG_ENABLE([a52dec],
                  [AS_HELP_STRING([--enable-a52dec], [Enable support for a52dec decoding library (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_a52dec="yes"])
    AC_ARG_WITH([external-a52dec],
                [AS_HELP_STRING([--with-external-a52dec], [Use external a52dec library (not recommended)])],
                [test x"$withval" != x"no" && with_external_a52dec="yes"], [with_external_a52dec="no"])
    if test x"$enable_a52dec" != x"no"; then
        if test x"$with_external_a52dec" != x"no"; then
            AC_CHECK_LIB([a52], [a52_init],
                         [AC_CHECK_HEADERS([a52dec/a52.h a52dec/a52_internal.h], [have_external_a52dec=yes], [have_external_a52dec=no],
                                           [#ifdef HAVE_SYS_TYPES_H
                                            # include <sys/types.h>
                                            #endif
                                            #ifdef HAVE_INTTYPES_H
                                            # include <inttypes.h>
                                            #endif
                                            #ifdef HAVE_STDINT_H
                                            # include <stdint.h>
                                            #endif
                                            #include <a52dec/a52.h>])], [have_external_a52dec=no], [-lm])
            if test x"$have_external_a52dec" = x"no"; then
                AC_MSG_RESULT([*** no usable version of a52dec found, using internal copy ***])
            fi
        else
            AC_MSG_RESULT([Using included a52dec support])
        fi
        if test x"$have_external_a52dec" = x"yes"; then
            A52DEC_CFLAGS=''
            A52DEC_LIBS='-la52'
            A52DEC_DEPS=''
	else
            A52DEC_CFLAGS='-I$(top_srcdir)/contrib/a52dec'
            A52DEC_LIBS='$(top_builddir)/contrib/a52dec/liba52.la'
            A52DEC_DEPS='$(top_builddir)/contrib/a52dec/liba52.la'
        fi
        AC_SUBST(A52DEC_CFLAGS)
        AC_SUBST(A52DEC_DEPS)
        AC_SUBST(A52DEC_LIBS)
    fi
    AM_CONDITIONAL([ENABLE_A52DEC], [test x"$enable_a52dec" != x"no"])
    AM_CONDITIONAL([WITH_EXTERNAL_A52DEC], [test x"$have_external_a52dec" = x"yes"])


    dnl ASF (optional; enabled by default)
    AC_ARG_ENABLE([asf],
                  [AS_HELP_STRING([--enable-asf], [Enable support for ASF demuxer (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_asf="yes"])
    AM_CONDITIONAL([ENABLE_ASF], [test x"$enable_asf" != x"no"])


    dnl FAAD (optional; enabled by default)
    AC_ARG_ENABLE([faad],
                  [AS_HELP_STRING([--enable-faad], [Enable support for FAAD decoder (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_faad="yes"])
    AM_CONDITIONAL([ENABLE_FAAD], [test x"$enable_faad" != x"no"])


    dnl ffmpeg (required; external version allowed)
    AC_ARG_WITH([external-ffmpeg],
                [AS_HELP_STRING([--with-external-ffmpeg], [use external ffmpeg library])],
                [], [with_external_ffmpeg="no"])
    AC_ARG_ENABLE([ffmpeg_uncommon_codecs],
                  [AS_HELP_STRING([--disable-ffmpeg-uncommon-codecs], [don't build uncommon ffmpeg codecs])],
                  [test x"$enableval" != x"no" && enable_ffmpeg_uncommon_codecs="yes"])
    AC_ARG_ENABLE([ffmpeg_popular_codecs],
                  [AS_HELP_STRING([--disable-ffmpeg-popular-codecs], [don't build popular ffmpeg codecs])],
                  [test x"$enableval" != x"no" && enable_ffmpeg_popular_codecs="yes"])
    case x"$with_external_ffmpeg" in
        x"no") with_external_ffmpeg=no ;;
        x"soft")
            PKG_CHECK_MODULES([FFMPEG], [libavcodec >= 51.20.0], [with_external_ffmpeg=yes], [with_external_ffmpeg=no])
            ;;
        x*)
            PKG_CHECK_MODULES([FFMPEG], [libavcodec >= 51.20.0], [with_external_ffmpeg=yes])
            ;;
    esac
    if test x"$with_external_ffmpeg" != x"no"; then
        PKG_CHECK_MODULES([FFMPEG_POSTPROC], [libpostproc])
        AC_DEFINE([HAVE_FFMPEG], 1, [Define this if you have ffmpeg library])
   
        AC_MSG_NOTICE([
*********************************************************************
xine-lib is configured with external ffmpeg.

This requires the same version of ffmpeg what is included in xine and
you should know what you do. If some problems occur, please try to
use internal ffmpeg.
*********************************************************************])
    else
        AC_MSG_RESULT([Using included ffmpeg])
    fi
    AM_CONDITIONAL([FFMPEG_DISABLE_UNCOMMON_CODECS], [test x"$enable_ffmpeg_uncommon_codecs" = x"no"])
    AM_CONDITIONAL([FFMPEG_DISABLE_POPULAR_CODECS], [test x"$enable_ffmpeg_popular_codecs" = x"no"])
    AM_CONDITIONAL([WITH_EXTERNAL_FFMPEG], [test x"$with_external_ffmpeg" != x"no"])


    dnl gdk-pixbuf (optional; enabled by default)
    AC_ARG_ENABLE([gdkpixbuf],
                  [AS_HELP_STRING([--enable-gdkpixbuf], [Enable GdkPixbuf support (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_gdkpixbuf="yes"])
    if test x"$enable_gdkpixbuf" != x"no"; then
        PKG_CHECK_MODULES([GDK_PIXBUF], [gdk-pixbuf-2.0], [have_gdkpixbuf=yes], [have_gdkpixbuf=no])
        if test x"$enable_gdkpixbuf" = x"yes" && test x"$have_gdkpixbuf" != x"yes"; then
            AC_MSG_ERROR([GdkPixbuf support requested, but GdkPixbuf not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_GDK_PIXBUF], [test x"$have_gdkpixbuf" = x"yes"])


    dnl ImageMagick (optional; enabled by default)
    AC_ARG_WITH([imagemagick],
                [AS_HELP_STRING([--with-imagemagick], [Enable ImageMagick image decoder support (default: enabled)])],
                [test x"$withval" != x"no" && with_imagemagick="yes"])
    if test x"$with_imagemagick" != x"no"; then
        PKG_CHECK_MODULES([WAND], [Wand], [have_imagemagick=yes], [have_imagemagick=no])
        if test x"$with_imagemagick" = x"yes" && test x"$have_imagemagick" = x"no"; then
            AC_MSG_ERROR([ImageMagick support requested, but ImageMagick not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_IMAGEMAGICK], [test x"$have_imagemagick" = x"yes"])


    dnl libdts (optional; enabled by default; external version allowed)
    AC_ARG_ENABLE([dts],
                  [AS_HELP_STRING([--enable-dts], [Enable support for DTS decoding library (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_dts="yes"])
    AC_ARG_WITH([external-libdts],
                [AS_HELP_STRING([--with-external-libdts], [Use external libdts/libdca library (not recommended)])],
                [test x"$withval" != x"no" && with_external_libdts="yes"], [with_external_libdts="no"])
    if test x"$enable_dts" != x"no"; then
        if test x"$with_external_libdts" != x"no"; then
            PKG_CHECK_MODULES([LIBDTS], [libdts], [have_external_dts=yes], [have_external_dts=no])
            if test x"$have_external_dts" != x"yes"; then
                AC_MSG_RESULT([*** no usable version of libdts found, using internal copy ***])
            fi
        else
            AC_MSG_RESULT([Using included libdts support])
        fi
        if test x"$have_external_libdts" != x"yes"; then
            LIBDTS_CFLAGS='-I$(top_srcdir)/contrib/libdca/include'
            LIBDTS_DEPS='$(top_builddir)/contrib/libdca/libdca.la'
            LIBDTS_LIBS='$(top_builddir)/contrib/libdca/libdca.la'
            AC_SUBST(LIBDTS_DEPS)
        fi
    fi
    AM_CONDITIONAL([ENABLE_DTS], [test x"$enable_dts" != x"no"])
    AM_CONDITIONAL([WITH_EXTERNAL_LIBDTS], [test x"$have_external_libdts" = x"yes"])


    dnl libFLAC (optional; disabled by default)
    AC_ARG_WITH([libflac],
                [AS_HELP_STRING([--with-libflac], [build libFLAC-based decoder and demuxer])],
                [test x"$withval" != x"no" && with_libflac="yes"], [with_libflac="no"])
    AC_ARG_WITH([libFLAC-prefix],
                [AS_HELP_STRING([--with-libFLAC-prefix=DIR], [prefix where libFLAC is installed (optional)])])
    AC_ARG_WITH([libFLAC-libraries],
                [AS_HELP_STRING([--with-libFLAC-libraries=DIR], [directory where libFLAC library is installed (optional)])])
    AC_ARG_WITH([libFLAC-includes],
                [AS_HELP_STRING([--with-libFLAC-includes=DIR], [directory where libFLAC header files are installed (optional)])])
    if test x"$with_libflac" != x"no"; then
        AC_MSG_CHECKING([libdir name])
        case "$host_or_hostalias" in
            *-*-linux*)
                # Test if the compiler is 64bit
                echo 'int i;' > conftest.$ac_ext
                xine_cv_cc_64bit_output=no
                if AC_TRY_EVAL(ac_compile); then
                    case `"$MAGIC_CMD" conftest.$ac_objext` in
                        *"ELF 64"*) xine_cv_cc_64bit_output=yes ;;
                    esac
                fi
                rm -rf conftest*
                ;;
        esac
        case "$host_cpu:$xine_cv_cc_64bit_output" in
            powerpc64:yes | s390x:yes | sparc64:yes | x86_64:yes)
                XINE_LIBDIRNAME="lib64" ;;
            *:*)
                XINE_LIBDIRNAME="lib" ;;
        esac
        AC_MSG_RESULT([$XINE_LIBDIRNAME])

        if test x"$with_libFLAC_includes" != x""; then
            LIBFLAC_CFLAGS="-I$with_libFLAC_includes"
        elif test x"$with_libFLAC_prefix" != x""; then
            LIBFLAC_CFLAGS="-I$with_libFLAC_prefix/include"
        elif test x"$prefix" != x"NONE"; then
            LIBFLAC_CFLAGS="-I$prefix/include"
        fi
        AC_SUBST(LIBFLAC_CFLAGS)

        if test x"$with_libFLAC_libraries" != x""; then
            LIBFLAC_LIBS="-L$with_libFLAC_libraries"
        elif test x"$with_libFLAC_prefix" != x""; then
            LIBFLAC_LIBS="-L$with_libFLAC_prefix/$XINE_LIBDIRNAME"
        elif test x"$prefix" != x"NONE"; then
            LIBFLAC_LIBS="-L$prefix/$XINE_LIBDIRNAME"
        fi
        AC_SUBST(LIBFLAC_LIBS)

        ac_save_CPPFLAGS="$CPPFLAGS" CPPFLAGS="$CPPFLAGS $LIBFLAC_CFLAGS"
        AC_CHECK_LIB([FLAC], [FLAC__stream_decoder_new],
                     [AC_CHECK_HEADERS([FLAC/stream_decoder.h],
                                       [have_libflac=yes LIBFLAC_LIBS="$LIBFLAC_LIBS -lFLAC -lm"],
                                       [have_libflac=no])],
                     [have_libflac=no], [-lm])
        CPPFLAGS="$ac_save_CPPFLAGS"

        if test x"$with_libflac" = x"yes" && test x"$have_libflac" != x"yes"; then
            AC_MSG_ERROR([libFLAC-based decoder support requested, but libFLAC not found])
        elif test x"$have_libflac" != x"yes"; then
            LIBFLAC_CFLAGS="" LIBFLAC_LIBS=""
        fi
    fi
    AM_CONDITIONAL([ENABLE_LIBFLAC], [test x"$have_libflac" = x"yes"])


    dnl libmad (optional; enabled by default; external version allowed)
    AC_ARG_ENABLE([mad],
                  [AS_HELP_STRING([--enable-mad], [Enable support for MAD decoding library (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_mad="yes"])
    AC_ARG_WITH([external-libmad],
                [AS_HELP_STRING([--with-external-libmad], [use external libmad library (not recommended)])],
                [test x"$withval" != x"no" && with_external_libmad="yes"], [with_external_libmad="no"])
    if test x"$enable_mad" != x"no"; then
        if test x"$with_external_libmad" != x"no"; then
            PKG_CHECK_MODULES([LIBMAD], [mad],
                              [AC_CHECK_HEADERS([mad.h], [have_external_libmad=yes], [have_external_libmad=no])],
                              [have_external_libmad=no])
            if test x"$have_external_libmad" != x"yes"; then
                AC_MSG_RESULT([*** no usable version of libmad found, using internal copy ***])
            fi
        else
            AC_MSG_RESULT([Using included libmad support])
        fi
        if test x"$have_external_libmad" != x"no"; then
            case "$host_or_hostalias" in
                i?86-* | k?-* | athlon-* | pentium*-)
                    AC_DEFINE([FPM_INTEL], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
                x86_64-*)
                    AC_DEFINE([FPM_64BIT], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
                ppc-* | powerpc-*) 
                    AC_DEFINE([FPM_PPC], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
                sparc*-*)
                    if test "$GCC" = yes; then
                        AC_DEFINE([FPM_SPARC], 1, [Define to select libmad fixed point arithmetic implementation])
                    else
                        AC_DEFINE([FPM_64BIT], 1, [Define to select libmad fixed point arithmetic implementation])
                    fi
                    ;;
                mips-*)
                    AC_DEFINE([FPM_MIPS], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
                alphaev56-* | alpha* | ia64-* | hppa*-linux-*)
                    AC_DEFINE([FPM_64BIT], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
                arm*-*)
                    AC_DEFINE([FPM_ARM], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
                universal-*)
                    ;;
                *)
                    AC_DEFINE([FPM_DEFAULT], 1, [Define to select libmad fixed point arithmetic implementation])
                    ;;
            esac
        fi
        if test x"$have_external_libmad" != x"yes"; then
            LIBMAD_CFLAGS='-I$(top_srcdir)/contrib/libmad'
            LIBMAD_LIBS='$(top_builddir)/contrib/libmad/libmad.la'
            LIBMAD_DEPS='$(top_builddir)/contrib/libmad/libmad.la'
        fi
        AC_SUBST(LIBMAD_CFLAGS)
        AC_SUBST(LIBMAD_DEPS)
        AC_SUBST(LIBMAD_LIBS)
    fi
    AM_CONDITIONAL([ENABLE_MAD], [test x"$enable_mad" != x"no"])
    AM_CONDITIONAL([WITH_EXTERNAL_MAD], [test x"$have_external_libmad" = x"yes"])


    dnl libmodplug (optional; enabled by default)
    AC_ARG_ENABLE([modplug],
                  [AS_HELP_STRING([--enable-modplug], [Enable MODPlug support (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_modplug="yes"])
    if test x"$enable_modplug" != x"no"; then
        PKG_CHECK_MODULES([LIBMODPLUG], [libmodplug >= 0.7], [have_modplug=yes], [have_modplug=no])
        if test x"$enable_modplug" = x"yes" && test x"$have_modplug" != x"yes"; then
            AC_MSG_ERROR([MODPlug support requested, but MODPlug not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_MODPLUG], [test x"$have_modplug" = x"yes"])


    dnl libmpcdec (optional; enabled by default; external version allowed)
    AC_ARG_ENABLE([musepack],
                  [AS_HELP_STRING([--enable-musepack], [Enable support for Musepack decoding (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_musepack="yes"])
    AC_ARG_WITH([external-libmpcdec],
                [AS_HELP_STRING([--with-external-libmpcdec], [Use external libmpc library])],
                [test x"$withval" != x"no" && with_external_libmpcdec="yes"], [with_external_libmpcdec="no"])
    if test x"$enable_musepack" != x"no"; then
        if test x"$with_external_libmpcdec" != x"no"; then
            AC_CHECK_LIB([mpcdec], [mpc_decoder_decode],
                         [AC_CHECK_HEADERS([mpcdec/mpcdec.h], [have_external_libmpcdec=yes], [have_external_libmpcdec=no])],
                         [have_external_libmpcdec=no])
            if test x"$have_external_libmpcdec" != x"yes"; then
                AC_MSG_RESULT([*** no usable version of libmpcdec found, using internal copy ***])
            else
                MPCDEC_CFLAGS=""
                MPCDEC_DEPS=""
                MPCDEC_LIBS="-lmpcdec"
            fi
        else
            AC_MSG_RESULT([Using included libmpcdec (Musepack)])
        fi
        if test x"$have_external_libmpcdec" != x"yes"; then
            MPCDEC_CFLAGS='-I$(top_srcdir)/contrib/libmpcdec'
            MPCDEC_LIBS='$(top_builddir)/contrib/libmpcdec/libmpcdec.la'
            MPCDEC_DEPS='$(top_builddir)/contrib/libmpcdec/libmpcdec.la'
        fi
        AC_SUBST(MPCDEC_CFLAGS)
        AC_SUBST(MPCDEC_DEPS)
        AC_SUBST(MPCDEC_LIBS)
    fi
    AM_CONDITIONAL([ENABLE_MUSEPACK], [test x"$enable_musepack" != x"no"])
    AM_CONDITIONAL([WITH_EXTERNAL_LIBMPCDEC], [test x"$have_external_libmpcdec" = x"yes"])


    dnl mlib
    AC_ARG_ENABLE([mlib],
	          [AS_HELP_STRING([--enable-mlib], [build Sun mediaLib support (default: disabled)])],
                  [test x"$enableval" != x"yes" && enable_mlib="no"])
    AC_ARG_ENABLE([mlib-lazyload],
                  [AS_HELP_STRING([--enable-mlib-lazyload], [check for Sun mediaLib at runtime])],
                  [test x"$enableval" != x"no" && enable_mlib_lazyload="yes"], [enable_mlib_lazyload="no"])
    if test x"$enable_mlib" = x"yes"; then
        mlibhome="$MLIBHOME" test x"$mlibhome" = x"" && mlibhome="/opt/SUNWmlib"
        AC_CHECK_LIB([mlib], [mlib_VideoAddBlock_U8_S16],
                     [saved_CPPFLAGS="$CPPFLAGS" CPPFLAGS="$CPPFLAGS -I$mlibhome/include"
                      AC_CHECK_HEADERS([mlib_video.h],
                          [if test x"$enable_mlib_lazyload" != x"no"; then
                               if test "$GCC" = yes; then
                                   MLIB_LIBS="-L$mlibhome/lib -Wl,-z,lazyload,-lmlib,-z,nolazyload"
                               else
                                   MLIB_LIBS="-L$mlibhome/lib -z lazyload -lmlib -z nolazyload"
                               fi
                               AC_DEFINE([MLIB_LAZYLOAD], 1, [Define this if you want to load mlib lazily])
                           else
                               MLIB_LIBS="-L$mlibhome/lib -lmlib"
                           fi
                           MLIB_CFLAGS="-I$mlibhome/include"
                           AC_SUBST(MLIB_LIBS)
                           AC_SUBST(MLIB_CFLAGS)
                           dnl TODO: src/video_out/yuv2rgb.c and src/xine-utils/cpu_accel.c should be changed to use LIBMPEG2_MLIB
                           dnl       and HAVE_MLIB should go away.
                           AC_DEFINE([HAVE_MLIB], 1, [Define this if you have mlib installed])
                           AC_DEFINE([LIBMPEG2_MLIB], 1, [Define this if you have mlib installed])
                           have_mlib=yes])
                      CPPFLAGS="$saved_CPPFLAGS"], [], ["-L$mlibhome/lib"])
    fi
    AM_CONDITIONAL([HAVE_MLIB], [test x"$have_mlib" = x"yes"])


    dnl mng (optional; enabled by default)
    AC_ARG_ENABLE([mng],
                  [AS_HELP_STRING([--enable-mng], [Enable MNG decoder support (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_mng="yes"])
    if test x"$with_mng" != x"no"; then
        AC_CHECK_LIB([mng], [mng_initialize],
                     [AC_CHECK_HEADERS([libmng.h], [have_mng=yes], [have_mng=no])], [have_mng=no])
        if test x"$with_mng" = x"yes" && test x"$have_mng" != x"yes"; then
            AC_MSG_ERROR([MNG support requested, but libmng not found])
        elif test x"$have_mng" = x"yes"; then
            MNG_LIBS="-lmng"
            AC_SUBST(MNG_LIBS)
        fi
    fi
    AM_CONDITIONAL([ENABLE_MNG], [test x"$have_mng" = x"yes"])


    dnl Ogg/Speex (optional; enabled by default; external)
    AC_ARG_WITH([speex],
                [AS_HELP_STRING([--with-speex], [Enable Speex audio decoder support (default: enabled)])],
                [test x"$withval" != x"no" && with_speex="yes"])
    if test x"$with_speex" != x"no"; then
        PKG_CHECK_MODULES([SPEEX], [ogg speex], [have_speex=yes], [have_speex=no])
        if test x"$with_speex" = x"yes" && test x"$have_speex" != x"yes"; then
            AC_MSG_ERROR([Speex support requested, but libspeex not found])
        elif test x"$have_speex" = x"yes"; then
            AC_DEFINE([HAVE_SPEEX], 1, [Define this if you have speex])
        fi
    fi
    AM_CONDITIONAL([ENABLE_SPEEX], [test x"$have_speex" = x"yes"])


    dnl Ogg/Theora (optional; enabled by default; external)
    AC_ARG_WITH([theora],
                [AS_HELP_STRING([--with-theora], [Enable Theora video decoder support (default: enabled)])],
                [test x"$withval" != x"no" && with_theora="yes"])
    if test x"$with_theora" != x"no"; then
        PKG_CHECK_MODULES([THEORA], [ogg theora], [have_theora=yes], [have_theora=no])
        if test x"$with_theora" = x"yes" && test x"$have_theora" = x"no"; then
            AC_MSG_ERROR([Theora support requested, but libtheora not found])
        elif test x"$have_theora" = x"yes"; then
            AC_DEFINE([HAVE_THEORA], 1, [Define this if you have theora])
        fi
    fi
    AM_CONDITIONAL([ENABLE_THEORA], [test x"$have_theora" = x"yes"])


    dnl Ogg/Vorbis (optional; enabled by default; external)
    AC_ARG_WITH([vorbis],
                [AS_HELP_STRING([--with-vorbis], [Enable Vorbis audio decoder support (default: enabled)])],
                [test x"$withval" != x"no" && with_vorbis="yes"])
    if test x"$with_vorbis" != x"no"; then
        PKG_CHECK_MODULES([VORBIS], [ogg vorbis], [have_vorbis=yes], [have_vorbis=no])
        if test x"$with_vorbis" = x"yes" && test x"$have_vorbis" = "xno"; then
            AC_MSG_ERROR([Vorbis support requested, but libvorbis not found])
        elif test x"$have_vorbis" = x"yes"; then
            AC_DEFINE([HAVE_VORBIS], 1, [Define this if you have vorbis])
        fi
    fi
    AM_CONDITIONAL([ENABLE_VORBIS], [test x"$have_vorbis" = x"yes"])


    dnl real (optional; enabled by default)
    dnl On some systems, we cannot enable Real codecs support to begin with.
    dnl This includes Darwin, because it uses Mach-O rather than ELF.
    AC_ARG_ENABLE([real-codecs],
                  [AS_HELP_STRING([--enable-real-codecs], [Enable Real binary codecs support (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_real_codecs="yes"])
    AC_ARG_WITH([real-codecs-path],
                [AS_HELP_STRING([--with-real-codecs-path=PATH], [Specify directory for Real binary codecs])])
    if test x"$enable_real_codecs" != x"no"; then
        case "$host_os" in
            darwin*) have_real_codecs=no ;;
            *)
                have_real_codecs=yes

                dnl For those that have a replacement, break at the first one found
                AC_CHECK_SYMBOLS([__environ _environ environ], [break], [need_weak_aliases=yes])
                AC_CHECK_SYMBOLS([stderr __stderrp], [break], [need_weak_aliases=yes])

                dnl For these there are no replacements
                AC_CHECK_SYMBOLS([___brk_addr __ctype_b])

                if test x"$need_weak_aliases" = x"yes"; then
                    CC_ATTRIBUTE_ALIAS([], [have_real_codecs=no])
                fi
                ;;
        esac
        if test x"$enable_real_codecs" = x"yes" && test x"$have_real_codecs" != x"yes"; then
            AC_MSG_ERROR([Binary Real codec support requested, but it is not available])
        elif test x"$have_real_codecs" = x"yes"; then
            if test "${with_real_codecs_path+set}" = "set"; then
                AC_DEFINE_UNQUOTED([REAL_CODEC_PATH], ["$with_real_codecs_path"], [Default path in which to find Real binary codecs])
            fi
        fi
    fi
    AM_CONDITIONAL([ENABLE_REAL], [test x"$have_real_codecs" = x"yes"])


    dnl w32dll (optional; x86 only; enabled if using GNU as; GNU as required)
    AC_ARG_ENABLE([w32dll],
                  [AS_HELP_STRING([--enable-w32dll], [Enable Win32 DLL support (default: enabled)])],
                  [test x"$enableval" != x"no" && enable_w32dll="yes"],
                  [test x"$with_gnu_as" != x"yes" && enable_w32dll="no"])
    AC_ARG_WITH([w32-path],
                [AS_HELP_STRING([--with-w32-path=PATH], [location of Win32 binary codecs])],
                [w32_path="$withval"], [w32_path="/usr/lib/codecs"])
    if test x"$enable_w32dll" != x"no"; then
        case "$host_or_hostalias" in
            *-mingw* | *-cygwin) have_w32dll=no ;;
            i?86-* | k?-* | athlon-* | pentium*-) have_w32dll="$with_gnu_as" ;;
            *) enable_w32dll=no ;;
        esac
        if test x"$enable_w32dll" = x"yes" && test x"$have_w32dll" != x"yes"; then
            AC_MSG_ERROR([Win32 DLL support requested, but Win32 DLL support is not available])
        fi
    fi
    AC_SUBST(w32_path)
    AM_CONDITIONAL([ENABLE_W32DLL], [test x"$have_w32dll" = x"yes"])


    dnl wavpack (optional; disabled by default)
    AC_ARG_WITH([wavpack],
                [AS_HELP_STRING([--with-wavpack], [Enable Wavpack decoder (requires libwavpack)])],
                [test x"$withval" != x"no" && with_wavpack="yes"], [with_wavpack="no"])
    if test x"$with_wavpack" != x"no"; then
        PKG_CHECK_MODULES([WAVPACK], [wavpack], [have_wavpack=yes], [have_wavpack=no])
        if test x"$with_wavpack" = x"yes" && test x"$have_wavpack" != x"yes"; then
            AC_MSG_ERROR([Wavpack decoder support requested, but libwavpack not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_WAVPACK], [test x"$have_wavpack" = x"yes"])


    dnl Only enable building dmx image if either gdk_pixbuf or ImageMagick are enabled
    AM_CONDITIONAL([BUILD_DMX_IMAGE], [test x"$have_imagemagick" = x"yes" -o x"$have_gdkpixbuf" = x"yes"])
])
