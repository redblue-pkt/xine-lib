##
## Process this file with automake to produce Makefile.in
##

CFLAGS = @GLOBAL_CFLAGS@

lib_LTLIBRARIES = libxine.la

##libxine_la_SOURCES = xine.c buffer.c metronom.c configfile.c \
##	monitor.c utils.c audio_decoder.c video_decoder.c load_plugins.c
libxine_la_SOURCES = metronom.c configfile.c monitor.c utils.c cpu_accel.c \
	load_plugins.c video_decoder.c audio_decoder.c
libxine_la_DEPENDENCIES = libsdeps
libxine_la_LIBADD = \
	$(top_srcdir)/src/libmpeg2/libmpeg2.la   \
	$(top_srcdir)/src/libac3/libac3.la       \
	$(top_srcdir)/src/libmpg123/libmpg123.la \
##	$(top_srcdir)/src/libspudec/libspudec.la \
	$(THREAD_LIBS)                           \
	$(DYNAMIC_LD_LIBS)
	-lm
libxine_la_LDFLAGS =  -version-info 5:0:5

#include_HEADERS  = buffer.h metronom.h configfile.h \
#	monitor.h cpu_accel.h attributes.h utils.h audio_decoder.h 

noinst_HEADERS = xine_internal.h buffer.h metronom.h configfile.h \
	monitor.h cpu_accel.h attributes.h utils.h

#cpu_accel.lo:
#	$(CC) -DHAVE_CONFIG_H $(INCLUDES) -pipe `echo "@DEBUG_CFLAGS@" | sed -e 's/\-DDEBUG//' -e 's/\-g//'` -fomit-frame-pointer -Wall -Wp,-MD,.deps/cpu_accel.P -c $(basename $@).c -o $@
#	echo timestamp > $(basename $@).o


debug:
	$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"


libsdeps:
	@cd $(top_builddir)/src/libmpeg2  &&  $(MAKE) libmpeg2.la
	@cd $(top_builddir)/src/libac3    && $(MAKE) libac3.la
	@cd $(top_builddir)/src/libmpg123 && $(MAKE) libmpg123.la
##	@cd $(top_builddir)/src/libspudec && $(MAKE) libspudec.la


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


mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* 


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
