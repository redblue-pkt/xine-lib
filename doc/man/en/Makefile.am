#SUBDIRS = man3

#man_MANS = xine-lib.3 xine-config.1
man_MANS = xine-config.1

EXTRA_DIST = $(man_MANS)

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
