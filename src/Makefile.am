# Order is important be careful changing SUBDIRS
#SUBDIRS = \
#	libdivx4 \
#	libxvid

SUBDIRS = \
	xine-utils \
	xine-engine \
	audio_out \
	video_out \
	dxr3 \
	input \
	demuxers \
	libffmpeg \
	libmpeg2 \
	liba52 \
	libspudec \
	libspucc \
	libsputext \
	libdts \
	libmad \
	libw32dll \
	liblpcm \
	libxinevdec \
	libxineadec \
	libvorbis \
	libreal \
	libfaad \
	post

debug:
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit; \
	done;

install-debug: debug
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit; \
	done;

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* 


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in


