#!/bin/sh
#
# shlibdeps.sh - script to calculate depends/recommends/suggests for shlibs
#
# usage: debian/shlibdeps.sh <packagename>
#
# (C) 2001 Siggi Langauf <siggi@debian.org>

installdir=debian/$1

OPTIONAL="$installdir/usr/lib/xine/plugins/xineplug_ao_out_alsa.so
	  $installdir/usr/lib/xine/plugins/xineplug_ao_out_arts.so
	  $installdir/usr/lib/xine/plugins/xineplug_ao_out_esd.so
	  $installdir/usr/lib/xine/plugins/xineplug_ao_out_oss.so
	  $installdir/usr/lib/xine/plugins/xineplug_vo_out_aa.so
	  $installdir/usr/lib/xine/plugins/xineplug_vo_out_syncfb.so
	  $installdir/usr/lib/xine/plugins/xineplug_vo_out_xv.so"

RECOMMENDED="$installdir/usr/lib/xine/plugins/xineplug_vo_out_aa.so"

#start with all executables and shared objects
REQUIRED=`find $installdir -type f \( -name \*.so -o -perm +111 \)`


#remove all OPTIONAL or RECOMMENDED stuff
for file in `echo $OPTIONAL $RECOMMENDED`; do
    REQUIRED=`echo "$REQUIRED" | grep -v $file`
done

dpkg-shlibdeps -Tdebian/$1.substvars \
               $REQUIRED -dRecommends $RECOMMENDED -dSuggests $OPTIONAL
