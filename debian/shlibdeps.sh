#!/bin/sh
#
# shlibdeps.sh - script to calculate depends/recommends/suggests for shlibs
#
# usage: debian/shlibdeps.sh <packagename>
#
# (C) 2001 Siggi Langauf <siggi@debian.org>

installdir=debian/$1

ver=`(cd $installdir/usr/lib/xine/plugins; ls [0-9]*)`
OPTIONAL="$installdir/usr/lib/xine/plugins/$ver/xineplug_ao_out_alsa.so
	  $installdir/usr/lib/xine/plugins/$ver/xineplug_ao_out_arts.so
	  $installdir/usr/lib/xine/plugins/$ver/xineplug_ao_out_esd.so
	  $installdir/usr/lib/xine/plugins/$ver/xineplug_vo_out_aa.so
	  $installdir/usr/lib/xine/plugins/$ver/xineplug_vo_out_syncfb.so
"

RECOMMENDED="$installdir/usr/lib/xine/plugins/$ver/xineplug_decode_vorbis.so
             $installdir/usr/lib/xine/plugins/$ver/xineplug_ao_out_oss.so
	     $installdir/usr/lib/xine/plugins/$ver/xineplug_vo_out_xv.so
	     $installdir/usr/lib/xine/plugins/$ver/xineplug_dmx_ogg.so"

#start with all executables and shared objects
REQUIRED=`find $installdir -type f \( -name \*.so -o -perm +111 \)`


#remove all OPTIONAL or RECOMMENDED stuff
for file in `echo $OPTIONAL $RECOMMENDED`; do
    REQUIRED=`echo "$REQUIRED" | grep -v $file`
done


# remove nonexisting files, warn in that case
for file in $RECOMMENDED; do
    if test ! -f "$file"; then
	echo "WARNING: non-existing file \"$file\" in RECOMMENDED list"
	RECOMMENDED=`echo "$var" | grep -v $file`
    fi
done
for file in $OPTIONAL; do
    if test ! -f "$file"; then
	echo "WARNING: non-existing file \"$file\" in OPTIONAL list"
	OPTIONAL=`echo "$var" | grep -v $file`
    fi
done



dpkg-shlibdeps -Tdebian/$1.substvars \
               $REQUIRED -dRecommends $RECOMMENDED -dSuggests $OPTIONAL
