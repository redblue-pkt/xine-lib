EXTRA_DIST = asm.h \
	dsputil_alpha.c \
	mpegvideo_alpha.c \
	motion_est_alpha.c \
	motion_est_mvi_asm.S \
	regdef.h \
	dsputil_alpha_asm.S \
	simple_idct_alpha.c

all:

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(LIBFFMPEG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
