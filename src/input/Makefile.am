##
## Process this file with automake to produce Makefile.in
##

EXTRA_DIST = input_dvd.c input_vcd.c input_gnome_vfs.c input_rtp.c

if HAVE_DVDNAV
SUBDIRS = dvb libreal librtsp
else
SUBDIRS = dvb libreal librtsp libdvdnav
endif

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic
XINE_LIB = $(top_builddir)/src/xine-engine/libxine.la

libdir = $(XINE_PLUGINDIR)

##
# IMPORTANT:
# ---------
# All of xine input plugins should be named like the scheme "xineplug_inp_"
#

if HAVE_CDROM_IOCTLS
in_dvd = xineplug_inp_dvd.la
in_vcd = xineplug_inp_vcd.la
#in_cda = xineplug_inp_cda.la
endif

if HAVE_V4L
in_v4l = xineplug_inp_v4l.la
in_pvr = xineplug_inp_pvr.la
endif

if HAVE_GNOME_VFS
in_gnome_vfs = xineplug_inp_gnome_vfs.la
endif

# For DVD
if HAVE_DVDNAV
DVD_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE $(DVDNAV_CFLAGS)
link_dvdnav = $(DVDNAV_LIBS)
else
DVD_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
  -I$(top_srcdir)/src/input/libdvdnav/
link_dvdnav = libdvdnav/libdvdnav.la
endif

AM_CFLAGS = $(GNOME_VFS_CFLAGS) $(DVD_CFLAGS) @ANSI_FLAGS@
DEBUG_CFLAGS = @DEBUG_CFLAGS@ $(DVD_CFLAGS) @ANSI_FLAGS@

lib_LTLIBRARIES = \
	xineplug_inp_file.la \
	xineplug_inp_http.la \
	$(in_dvd) \
	$(in_vcd) \
	$(in_v4l) \
	$(in_gnome_vfs) \
	xineplug_inp_mms.la \
	xineplug_inp_stdin_fifo.la \
	xineplug_inp_pnm.la \
	xineplug_inp_rtp.la \
	xineplug_inp_rtsp.la \
	xineplug_inp_net.la \
	$(in_pvr) \
	xineplug_inp_dvb.la \
	xineplug_inp_cdda.la


#lib_LTLIBRARIES = \
#	$(in_cda) \
#	$(in_dvd) \
#	$(in_vcd) \
#	xineplug_inp_file.la \
#	xineplug_inp_http.la \
#	xineplug_inp_mms.la \
#	xineplug_inp_net.la \
#	xineplug_inp_rtp.la \
#	xineplug_inp_stdin_fifo.la


xineplug_inp_file_la_SOURCES = input_file.c
xineplug_inp_file_la_LIBADD = $(XINE_LIB)
xineplug_inp_file_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_dvd_la_SOURCES = input_dvd.c media_helper.c
xineplug_inp_dvd_la_LIBADD = $(XINE_LIB) $(link_dvdnav)
xineplug_inp_dvd_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_net_la_SOURCES = input_net.c net_buf_ctrl.c
xineplug_inp_net_la_LIBADD = $(XINE_LIB)
xineplug_inp_net_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_mms_la_SOURCES = input_mms.c net_buf_ctrl.c mms.c mmsh.c
xineplug_inp_mms_la_LIBADD = $(XINE_LIB)
xineplug_inp_mms_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_vcd_la_SOURCES = input_vcd.c media_helper.c
xineplug_inp_vcd_la_LIBADD = $(XINE_LIB)
xineplug_inp_vcd_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_stdin_fifo_la_SOURCES = input_stdin_fifo.c net_buf_ctrl.c
xineplug_inp_stdin_fifo_la_LIBADD = $(XINE_LIB)
xineplug_inp_stdin_fifo_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_rtp_la_SOURCES = input_rtp.c
xineplug_inp_rtp_la_LIBADD = $(XINE_LIB)
xineplug_inp_rtp_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_http_la_SOURCES = input_http.c net_buf_ctrl.c
xineplug_inp_http_la_LIBADD = $(XINE_LIB)
xineplug_inp_http_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_pnm_la_SOURCES = input_pnm.c net_buf_ctrl.c pnm.c
xineplug_inp_pnm_la_LIBADD = $(XINE_LIB)
xineplug_inp_pnm_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_dvb_la_SOURCES = input_dvb.c net_buf_ctrl.c
xineplug_inp_dvb_la_LIBADD = $(XINE_LIB)
xineplug_inp_dvb_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_rtsp_la_SOURCES = input_rtsp.c net_buf_ctrl.c
xineplug_inp_rtsp_la_LIBADD = $(XINE_LIB) libreal/libreal.la librtsp/librtsp.la
xineplug_inp_rtsp_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_cdda_la_SOURCES = input_cdda.c media_helper.c
xineplug_inp_cdda_la_LIBADD = $(XINE_LIB)
xineplug_inp_cdda_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_v4l_la_SOURCES = input_v4l.c
xineplug_inp_v4l_la_LIBADD = $(XINE_LIB)
xineplug_inp_v4l_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_gnome_vfs_la_SOURCES = input_gnome_vfs.c
xineplug_inp_gnome_vfs_la_LIBADD = $(GNOME_VFS_LIBS) $(XINE_LIB)
xineplug_inp_gnome_vfs_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_inp_pvr_la_SOURCES = input_pvr.c
xineplug_inp_pvr_la_LIBADD = $(XINE_LIB)
xineplug_inp_pvr_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

include_HEADERS = input_plugin.h
noinst_HEADERS = net_buf_ctrl.h mms.h mmsh.h pnm.h media_helper.h

$(XINE_LIB):
	@cd $(top_builddir)/src/xine-engine && $(MAKE)

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
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit;\
	done;
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit;\
	done;
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
