##
## Process this file with automake to produce Makefile.in
##

#CFLAGS += @GLOBAL_CFLAGS@
CFLAGS = @CFLAGS@ -D_FILE_OFFSET_BITS=64 -Wall -DXINE_COMPILE

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

##
# IMPORTANT:
# ---------
# All of xine input plugins should be named like the scheme "xineplug_inp_"
#
lib_LTLIBRARIES = xineplug_inp_file.la xineplug_inp_dvd.la \
	xineplug_inp_stdin_fifo.la xineplug_inp_net.la \
	xineplug_inp_vcd.la xineplug_inp_rtp.la

xineplug_inp_file_la_SOURCES = input_file.c
xineplug_inp_file_la_LDFLAGS = -avoid-version -module

xineplug_inp_dvd_la_SOURCES = input_dvd.c dvd_udf.c
xineplug_inp_dvd_la_LDFLAGS = -avoid-version -module

xineplug_inp_net_la_SOURCES = input_net.c
xineplug_inp_net_la_LDFLAGS = -avoid-version -module

xineplug_inp_vcd_la_SOURCES = input_vcd.c
xineplug_inp_vcd_la_LDFLAGS = -avoid-version -module

xineplug_inp_stdin_fifo_la_SOURCES = input_stdin_fifo.c
xineplug_inp_stdin_fifo_la_LDFLAGS = -avoid-version -module

xineplug_inp_rtp_la_SOURCES = input_rtp.c
xineplug_inp_rtp_la_LDFLAGS = -avoid-version -module

include_HEADERS = input_plugin.h
noinst_HEADERS = dvd_udf.h


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
	$(MAKE) CFLAGS="-D_FILE_OFFSET_BITS=64 -Wall -DDEBUG -g"

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
