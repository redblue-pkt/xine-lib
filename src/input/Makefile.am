##
## Process this file with automake to produce Makefile.in
##

SUBDIRS = libdvdread libdvdnav

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

##
# IMPORTANT:
# ---------
# All of xine input plugins should be named like the scheme "xineplug_inp_"
#

if HAVE_CDROM_IOCTLS
in_dvd = xineplug_inp_dvd.la
in_vcd = xineplug_inp_vcd.la
in_cda = xineplug_inp_cda.la
endif

# For DVD
DVD_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
  -Ilibdvdnav/ \
  -Ilibdvdread/

CFLAGS = @CFLAGS@ $(DVD_CFLAGS) 
DEBUG_CFLAGS = @DEBUG_CFLAGS@ $(DVD_CFLAGS) 

lib_LTLIBRARIES = xineplug_inp_file.la $(in_dvd) $(in_vcd) $(in_cda) \
	xineplug_inp_stdin_fifo.la xineplug_inp_net.la \
	xineplug_inp_rtp.la xineplug_inp_http.la xineplug_inp_mms.la

xineplug_inp_file_la_SOURCES = input_file.c
xineplug_inp_file_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_inp_file_la_LDFLAGS = -avoid-version -module

xineplug_inp_dvd_la_SOURCES = input_dvd.c 
xineplug_inp_dvd_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la \
  libdvdnav/libdvdnav.la libdvdread/libdvdread.la
xineplug_inp_dvd_la_LDFLAGS = -avoid-version -module

xineplug_inp_net_la_SOURCES = input_net.c net_buf_ctrl.c
xineplug_inp_net_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_inp_net_la_LDFLAGS = -avoid-version -module

xineplug_inp_mms_la_SOURCES = input_mms.c net_buf_ctrl.c mms.c 
xineplug_inp_mms_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_inp_mms_la_LDFLAGS = -avoid-version -module

xineplug_inp_vcd_la_SOURCES = input_vcd.c
xineplug_inp_vcd_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_inp_vcd_la_LDFLAGS = -avoid-version -module

xineplug_inp_stdin_fifo_la_SOURCES = input_stdin_fifo.c net_buf_ctrl.c
xineplug_inp_stdin_fifo_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_inp_stdin_fifo_la_LDFLAGS = -avoid-version -module

xineplug_inp_rtp_la_SOURCES = input_rtp.c
xineplug_inp_rtp_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_inp_rtp_la_LDFLAGS = -avoid-version -module

xineplug_inp_http_la_SOURCES = input_http.c net_buf_ctrl.c
xineplug_inp_http_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_inp_http_la_LDFLAGS = -avoid-version -module

xineplug_inp_cda_la_SOURCES = input_cda.c
xineplug_inp_cda_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_inp_cda_la_LDFLAGS = -avoid-version -module

include_HEADERS = input_plugin.h
noinst_HEADERS = dvd_udf.h read_cache.h net_buf_ctrl.h mms.h

EXTRA_DIST = input_dvd.c dvd_udf.c input_vcd.c read_cache.c

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
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
