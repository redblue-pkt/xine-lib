#!/bin/sh

# Making releases:
#   1. For normal releases
#        Increment XINE_VERSION_SUB
#        Clear XINE_VERSION_PATCH
#      For patch releases
#        Increment XINE_VERSION_PATCH (use ".1", ".2" etc.)
#   2. Remove .cvsversion before running make dist
#   3. Adjust the values of XINE_LT_CURRENT, XINE_LT_REVISION, and XINE_LT_AGE
#      according to the following rules:
#
#      1. Increment XINE_LT_REVISION.
#      2. If any interfaces have been added, removed, or changed, set
#         XINE_LT_REVISION to 0 and increment XINE_LT_CURRENT.
#      3. If any interfaces have been added, increment XINE_LT_AGE.
#      4. If any interfaces have been removed or incompatibly changed,
#         set XINE_LT_AGE to 0, and rename po/libxine*.pot accordingly
#         (use "hg rename").
#
# The most important thing to keep in mind is that the libtool version
# numbers DO NOT MATCH the xine-lib version numbers, and you should NEVER
# try to make them match.
#
# See the libtool documentation for more information.
#
#   XINE_LT_CURRENT     the current API version
#   XINE_LT_REVISION    the current revision of the current API
#   XINE_LT_AGE         the number of previous API versions still supported

XINE_VERSION_MAJOR=1
XINE_VERSION_MINOR=2
XINE_VERSION_SUB=9
XINE_VERSION_PATCH=
# Release series number (usually $XINE_MAJOR.$XINE_MINOR)
XINE_VERSION_SERIES=1.2

XINE_LT_CURRENT=9
XINE_LT_REVISION=0
XINE_LT_AGE=7

if [ -f "`dirname $0`/.cvsversion" ]; then
    HG_REV="`hg summary | sed -e '1s/^parent: \([0-9]*\):.*$/\1/;1q'`"
    HG_DATE_UNIX="`hg export -r ${HG_REV} | sed -n '3s/^# Date \([0-9]*\) .*$/\1/p;3q'`"
    HG_DATE="`date -u +%Y%m%d --date=@${HG_DATE_UNIX}`"
    HG_DIRTY=`hg out 2>/dev/null | grep -q changeset || hg sum | grep -q 'commit:.*modified' && echo "+dirty"`
    XINE_VERSION_SUFFIX="-${HG_DATE}hg${HG_REV}${HG_DIRTY}"
fi
XINE_VERSION_SPEC="${XINE_VERSION_MAJOR}.${XINE_VERSION_MINOR}.${XINE_VERSION_SUB}${XINE_VERSION_PATCH}${XINE_VERSION_SUFFIX}"

####
####    You should not need to touch anything beyond this point
####

echo "m4_define([XINE_VERSION_MAJOR],  [${XINE_VERSION_MAJOR}])dnl"
echo "m4_define([XINE_VERSION_MINOR],  [${XINE_VERSION_MINOR}])dnl"
echo "m4_define([XINE_VERSION_SUB],    [${XINE_VERSION_SUB}])dnl"
echo "m4_define([XINE_VERSION_PATCH],  [${XINE_VERSION_PATCH}])dnl"
echo "m4_define([XINE_VERSION_SUFFIX], [${XINE_VERSION_SUFFIX}])dnl"
echo "m4_define([XINE_VERSION_SPEC],   [${XINE_VERSION_SPEC}])dnl"
echo "m4_define([XINE_VERSION_SERIES], [${XINE_VERSION_SERIES}])dnl"
echo "m4_define([__XINE_LT_CURRENT],   [${XINE_LT_CURRENT}])dnl"
echo "m4_define([__XINE_LT_REVISION],  [${XINE_LT_REVISION}])dnl"
echo "m4_define([__XINE_LT_AGE],       [${XINE_LT_AGE}])dnl"
