dnl Extracted from automake-1.5 and sligtly modified for Xine usage.
dnl Daniel Caujolle-Bert <segfault@club-internet.fr>

# Figure out how to run the assembler.

# AM_PROG_AS
AC_DEFUN([AM_PROG_AS],
[# By default we simply use the C compiler to build assembly code.
AC_REQUIRE([AC_PROG_CC])
: ${AS='$(CC)'}
# Set ASFLAGS if not already set.
: ${ASFLAGS='$(CFLAGS)'}
# Set ASCOMPILE if not already set.
if test $AS = '$''('CC')'; then
: ${ASCOMPILE='$(AS) $(AM_ASFLAGS) $(ASFLAGS) -c'}
else
: ${ASCOMPILE='$(AS) $(AM_ASFLAGS) $(ASFLAGS)'}
fi
AC_SUBST(AS)
AC_SUBST(ASFLAGS)
AC_SUBST(ASCOMPILE)])
