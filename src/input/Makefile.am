##
## Process this file with automake to produce Makefile.in
##

#CFLAGS += @GLOBAL_CFLAGS@
CFLAGS += -D_FILE_OFFSET_BITS=64 -Wall -DXINE_COMPILE

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

#lib_LTLIBRARIES = input_file.la input_net.la input_dvd.la input_vcd.la \
#	input_stdin_fifo.la
lib_LTLIBRARIES = input_file.la

#input_dvd_la_SOURCES = input_dvd.c dvd_udf.c
#input_dvd_la_LDFLAGS = -avoid-version -module -Wl,-soname,input_dvd.so
#input_dvd_la_LIBADD =

input_file_la_SOURCES = input_file.c
input_file_la_LDFLAGS = -avoid-version -module -Wl,-soname,input_file.so
input_file_la_LIBADD =

#input_net_la_SOURCES = input_net.c
#input_net_la_LDFLAGS = -avoid-version -module -Wl,-soname,input_net.so
#input_net_la_LIBADD =

#input_vcd_la_SOURCES = input_vcd.c
#input_vcd_la_LDFLAGS = -avoid-version -module -Wl,-soname,input_vcd.so
#input_vcd_la_LIBADD =

#input_stdin_fifo_la_SOURCES = input_stdin_fifo.c
#input_stdin_fifo_la_LDFLAGS = -avoid-version -module -Wl,-soname,input_stdin_fifo.so


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
