CFLAGS = @GLOBAL_CFLAGS@ 

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_ac3.la

xineplug_decode_ac3_la_SOURCES =  bitstream.c bit_allocate.c \
	decode.c coeff.c exponent.c parse.c crc.c rematrix.c \
	dither.c sanity_check.c srfft.c imdct.c downmix.c \
	xine_decoder.c
xineplug_decode_ac3_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = ac3.h ac3_internal.h bitstream.h \
	imdct.h coeff.h exponent.h bit_allocate.h parse.h \
	crc.h rematrix.h downmix.h dither.h \
	sanity_check.h srfft.h srfftp.h cmplx.h decode.h \
	bswap.h

##
## Install header files (default=$includedir/xine)
##
install-includeHEADERS: $(include_HEADERS)
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)$(includedir)/xine
	@list='$(include_HEADERS)'; for p in $$list; do \
	  if test -f "$$p"; then d= ; else d="$(srcdir)/"; fi; \
	  echo " $(INSTALL_DATA) $$d$$p $(DESTDIR)$(includedir)/xine/$$p"; \
	  $(INSTALL_DATA) $$d$$p $(DESTDIR)$(includedir)/xine/$$p; \
	done


##
## Remove them
##
uninstall-includeHEADERS:
	@$(NORMAL_UNINSTALL)
	list='$(include_HEADERS)'; for p in $$list; do \
	  rm -f $(DESTDIR)$(includedir)/xine/$$p; \
	done


debug:
	$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
