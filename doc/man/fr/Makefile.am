mansubdir=/fr/man3

man_MANS = xine-lib.3

EXTRA_DIST = $(man_MANS) xine-lib.3.in

install-man3:
	$(mkinstalldirs) $(DESTDIR)$(mandir)$(mansubdir)
	@list='$(man3_MANS)'; \
	l2='$(man_MANS)'; for i in $$l2; do \
	  case "$$i" in \
	    *.3*) list="$$list $$i" ;; \
	  esac; \
	done; \
	for i in $$list; do \
	  if test -f $(srcdir)/$$i; then file=$(srcdir)/$$i; \
	  else file=$$i; fi; \
	  ext=`echo $$i | sed -e 's/^.*\\.//'`; \
	  inst=`echo $$i | sed -e 's/\\.[0-9a-z]*$$//'`; \
	  inst=`echo $$inst | sed '$(transform)'`.$$ext; \
	  echo " $(INSTALL_DATA) $$file $(DESTDIR)$(mandir)$(mansubdir)/$$inst"; \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(mandir)$(mansubdir)/$$inst; \
	done

uninstall-man3:
	@list='$(man3_MANS)'; \
	l2='$(man_MANS)'; for i in $$l2; do \
	  case "$$i" in \
	    *.3*) list="$$list $$i" ;; \
	  esac; \
	done; \
	for i in $$list; do \
	  ext=`echo $$i | sed -e 's/^.*\\.//'`; \
	  inst=`echo $$i | sed -e 's/\\.[0-9a-z]*$$//'`; \
	  inst=`echo $$inst | sed '$(transform)'`.$$ext; \
	  echo " rm -f $(DESTDIR)$(mandir)$(mansubdir)/$$inst"; \
	  rm -f $(DESTDIR)$(mandir)$(mansubdir)/$$inst; \
	done

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in xine-lib.3
