include $(top_srcdir)/misc/Makefile.common


AM_CFLAGS = @ANSI_FLAGS@

noinst_LTLIBRARIES = libmpeg2.la libmpeg2arch.la

libmpeg2_la_SOURCES = alloc.c header.c decode.c slice.c motion_comp.c idct.c \
                      motion_comp_mlib.c idct_mlib.c
libmpeg2_la_LIBADD = libmpeg2arch.la

libmpeg2arch_la_SOURCES = motion_comp_mmx.c idct_mmx.c \
                          motion_comp_altivec.c idct_altivec.c \
                          motion_comp_alpha.c idct_alpha.c \
                          cpu_accel.c cpu_state.c

