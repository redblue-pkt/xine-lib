##
## Process this file with automake to produce Makefile.in
##

noinst_LTLIBRARIES = libxineutils.la

EXTRA_DIST = ppcasm_string.S ppc_asm.tmpl

if PPC_ARCH
pppc_files = ppcasm_string.S 
endif

AM_CFLAGS = $(THREAD_CFLAGS) @ANSI_FLAGS@

libxineutils_la_SOURCES = $(pppc_files) \
	cpu_accel.c \
	color.c \
	list.c \
	memcpy.c \
	monitor.c \
	utils.c \
	xine_check.c \
	xine_mutex.c \
	xmllexer.c \
	xmlparser.c 

libxineutils_la_LIBADD = $(THREAD_LIBS)


include_HEADERS = attributes.h \
	compat.h \
	ppcasm_string.h \
	xineutils.h \
	xine_check.h \
	xmllexer.h \
	xmlparser.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(THREAD_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am



###
# Install header files (default=$includedir/xine)
#
install-includeHEADERS: $(include_HEADERS)
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)$(includedir)/xine
	@list='$(include_HEADERS)'; for p in $$list; do \
	  if test -f "$$p"; then d= ; else d="$(srcdir)/"; fi; \
	  echo " $(INSTALL_DATA) $$d$$p $(DESTDIR)$(includedir)/xine/"; \
	  $(INSTALL_DATA) $$d$$p $(DESTDIR)$(includedir)/xine/; \
	done


###
# Remove them
#
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

