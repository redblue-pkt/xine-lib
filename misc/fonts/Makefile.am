datadir = $(XINE_FONTDIR)
data_DATA = mono-16.xinefont.gz  sans-16.xinefont.gz  serif-16.xinefont.gz \
	mono-20.xinefont.gz  sans-20.xinefont.gz  serif-20.xinefont.gz     \
	mono-24.xinefont.gz  sans-24.xinefont.gz  serif-24.xinefont.gz     \
	mono-32.xinefont.gz  sans-32.xinefont.gz  serif-32.xinefont.gz     \
	cetus-16.xinefont.gz cetus-20.xinefont.gz cetus-24.xinefont.gz     \
	cetus-32.xinefont.gz cc-16.xinefont.gz    cc-20.xinefont.gz        \
	cc-24.xinefont.gz    cc-32.xinefont.gz    cci-16.xinefont.gz       \
	cci-20.xinefont.gz   cci-24.xinefont.gz   cci-32.xinefont.gz	   \
	sanshu-16.xinefont.gz sanshu-20.xinefont.gz sanshu-24.xinefont.gz  \
	sanshu-32.xinefont.gz

#
# font used for osd
#
EXTRA_DIST = cetus.ttf README.cetus $(data_DATA)

debug:

install-debug: install

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* 


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
