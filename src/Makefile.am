# Order is important be careful changing SUBDIRS
#SUBDIRS = \
#	xine-utils \
#	xine-engine \
#	audio_out \
#	video_out \
#	dxr3 \
#	input \
#	libmpeg2 \
#	libspudec \
#	demuxers \
#	liba52 \
#	libffmpeg \
#	liblpcm \
#	libw32dll \
#	libmad 
#	libdts \
#	libvorbis \
#	libdivx4 \
#	libsputext \
#	libspucc \
#	libxvid \
#	libxinevdec \
#	libxineadec \
#	libfaad

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
	libmad \
	libw32dll \
	liblpcm \
	libxinevdec \
	libxineadec 

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


