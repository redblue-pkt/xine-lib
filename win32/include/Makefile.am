include $(top_srcdir)/misc/Makefile.common

msvc_headers = \
	msvc/inttypes.h \
	msvc/stdint.h \
	msvc/sys/time.h

if WIN32
  install_h = $(msvc_headers)
else
  dist_h = $(msvc_headers)
endif

EXTRA_DIST = $(dist_h) \
	sys/socket.h \
	msvc/config.h \
	netinet/in.h \
	sys/socket.h \
	dlfcn.h \
	netdb.h \
	pwd.h \
	unistd.h

nobase_include_HEADERS = $(install_h)
