man_MANS = \
	ao_driver_t.3    config_values_s.3   status_group.3  visual_types.3 xine_init.3       \
	ao_functions_t.3 config_values_t.3   ui_callbacks.3  vo_driver_s.3  xine_management.3 \
	autoplay_group.3 demux_strategy.3    version_group.3 vo_driver_t.3  xine_t.3          \
	browse_group.3   event_group.3       video_cap.3     vo_frame_t.3   xine_version.3    \
	build_info.3     loadplugins_group.3 video_group.3   vo_overlay_t.3                   \
	cfg_data_t.3     mrl_t.3             video_prop.3    xine.h.3                         \
	config_group.3   mrl_types.3         video_ratio.3   xine_api.3

EXTRA_DIST = $(man_MANS)

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in xine-lib.3
