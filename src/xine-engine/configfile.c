/*
 * Copyright (C) 2000-2022 the xine project
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * config object (was: file) management - implementation
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <xine/configfile.h>
#include "bswap.h"

#define LOG_MODULE "configfile"
#define LOG_VERBOSE
/*
#define LOG
*/
/* #define DEBUG_CONFIG_FIND */

/* XXX: does this break some strange applictions?? */
#define SINGLE_CHUNK_ENUMS

#include <xine/xineutils.h>
#include <xine/sorted_array.h>
#include <xine/xine_internal.h>
#include "xine_private.h"

#define MAX_SORT_KEY 320

#if defined(WIN32)
#  define PSEP '\\'
#else
#  define PSEP '/'
#endif

/* FIXME: static data, no expiry ?! */
static const xine_config_entry_translation_t *config_entry_translation_user = NULL;

typedef struct {
  cfg_entry_t entry;
  int *magic;
  char *internal_key; /** << xine_fast_string_t * */
} fat_cfg_entry_t;

static void _config_set_fat_entry (fat_cfg_entry_t *entry) {
  entry->magic = &entry->entry.range_min;
}

static int _config_is_fat_entry (fat_cfg_entry_t *entry) {
  return entry->magic == &entry->entry.range_min;
}

typedef struct {
  fat_cfg_entry_t entry;
  char buf[MAX_SORT_KEY + 32];
} very_fat_cfg_entry_t;

typedef struct {
  config_values_t config;
  xine_sarray_t *key_index;
} fat_config_values_t;

typedef struct {
  xine_config_cb_t callback;
  void            *data;
} _cfg_cb_info_t;

typedef struct {
  uint32_t size;
  uint32_t used;
  _cfg_cb_info_t items[1];
} _cfg_cb_relay_t;

static void _cfg_relay (void *data, xine_cfg_entry_t *e) {
  _cfg_cb_info_t *i, *s;
  _cfg_cb_relay_t *relay = data;

  if (!relay)
    return;

  for (i = &relay->items[0], s = i + relay->used; i < s; i++)
    i->callback (i->data, e);
}

static int _cfg_cb_clear (cfg_entry_t *entry) {
  int n = 0;
  for (; entry; entry = entry->next) {
    if (entry->callback == _cfg_relay) {
      _cfg_cb_relay_t *relay = entry->callback_data;
      if (relay) {
        n += relay->used;
        free (relay);
      }
    } else {
      n += entry->callback ? 1 : 0;
    }
    entry->callback_data = NULL;
    entry->callback = NULL;
  }
  return n;
}

static int _cfg_cb_clear_report (xine_t *xine, cfg_entry_t *entry) {
  int n = 0;
  for (; entry; entry = entry->next) {
    int have = n;
    if (entry->callback == _cfg_relay) {
      _cfg_cb_relay_t *relay = entry->callback_data;
      if (relay) {
        n += relay->used;
        free (relay);
      }
    } else {
      n += entry->callback ? 1 : 0;
    }
    entry->callback_data = NULL;
    entry->callback = NULL;
    if ((n > have) && xine)
      xprintf (xine, XINE_VERBOSITY_DEBUG, "configfile: %d orphaned callbacks for %s.\n", n - have, entry->key);
  }
  return n;
}

static int _cfg_cb_d_rem (cfg_entry_t *entry, xine_config_cb_t callback, void *data, size_t data_size) {
  int n = 0;
  if (!data_size) data_size = 1;
  for (; entry; entry = entry->next) {
    if (entry->callback == _cfg_relay) {
      _cfg_cb_info_t *r, *e;
      _cfg_cb_relay_t *relay = entry->callback_data;
      if (!relay) {
        entry->callback = NULL;
        continue;
      }
      r = &relay->items[0];
      e = r + relay->used;
      while (r < e) {
        if ((callback == r->callback) && PTR_IN_RANGE (r->data, data, data_size)) *r = *(--e); else r++;
      }
      n += relay->used;
      relay->used = r - &relay->items[0];
      n -= relay->used;
      if (relay->used <= 1) {
        r->callback = NULL;
        r->data = NULL;
        entry->callback = relay->items[0].callback;
        entry->callback_data = relay->items[0].data;
        free (relay);
      }
    } else {
      if ((callback == entry->callback) && PTR_IN_RANGE (entry->callback_data, data, data_size)) {
        n++;
        entry->callback_data = NULL;
        entry->callback = NULL;
      }
    }
  }
  return n;
}

static int _cfg_cb_rem (cfg_entry_t *entry, xine_config_cb_t callback) {
  int n = 0;
  for (; entry; entry = entry->next) {
    if (entry->callback == _cfg_relay) {
      _cfg_cb_info_t *r, *e;
      _cfg_cb_relay_t *relay = entry->callback_data;
      if (!relay) {
        entry->callback = NULL;
        continue;
      }
      r = &relay->items[0];
      e = r + relay->used;
      while (r < e) {
        if (callback == r->callback) *r = *(--e); else r++;
      }
      n += relay->used;
      relay->used = r - &relay->items[0];
      n -= relay->used;
      if (relay->used <= 1) {
        r->callback = NULL;
        r->data = NULL;
        entry->callback = relay->items[0].callback;
        entry->callback_data = relay->items[0].data;
        free (relay);
      }
    } else {
      if (callback == entry->callback) {
        n++;
        entry->callback_data = NULL;
        entry->callback = NULL;
      }
    }
  }
  return n;
}

static int _cfg_d_rem (cfg_entry_t *entry, void *data, size_t data_size) {
  int n = 0;
  if (!data_size) data_size = 1;
  for (; entry; entry = entry->next) {
    if (entry->callback == _cfg_relay) {
      _cfg_cb_info_t *r, *e;
      _cfg_cb_relay_t *relay = entry->callback_data;
      if (!relay) {
        entry->callback = NULL;
        continue;
      }
      r = &relay->items[0];
      e = r + relay->used;
      while (r < e) {
        if (PTR_IN_RANGE (r->data, data, data_size)) *r = *(--e); else r++;
      }
      n += relay->used;
      relay->used = r - &relay->items[0];
      n -= relay->used;
      if (relay->used <= 1) {
        r->callback = NULL;
        r->data = NULL;
        entry->callback = relay->items[0].callback;
        entry->callback_data = relay->items[0].data;
        free (relay);
      }
    } else {
      if (PTR_IN_RANGE (entry->callback_data, data, data_size)) {
        n++;
        entry->callback_data = NULL;
        entry->callback = NULL;
      }
    }
  }
  return n;
}

static int _cfg_any_rem (cfg_entry_t *entry, xine_config_cb_t callback, void *data, size_t data_size) {
  if (callback) {
    if (data)
      return _cfg_cb_d_rem (entry, callback, data, data_size);
    else
      return _cfg_cb_rem (entry, callback);
  } else {
    if (data)
      return _cfg_d_rem (entry, data, data_size);
    else
      return _cfg_cb_clear (entry);
  }
}

static void _cfg_cb_add (cfg_entry_t *entry, xine_config_cb_t callback, void *data) {
  _cfg_cb_relay_t *relay;
  _cfg_cb_info_t *info;

  if (!callback)
    return;

  if (!entry->callback) {
    entry->callback = callback;
    entry->callback_data = data;
    return;
  }

  if (entry->callback == _cfg_relay) {
    relay = entry->callback_data;
    if (!relay)
      return;
  } else {
    relay = malloc (sizeof (*relay) + 8 * sizeof (relay->items[0]));
    if (!relay)
      return;
    relay->size = 8;
    relay->used = 1;
    relay->items[0].callback = entry->callback;
    relay->items[0].data = entry->callback_data;
    entry->callback = _cfg_relay;
    entry->callback_data = relay;
  }

  if (relay->used + 1 > relay->size) {
    uint32_t size = relay->size + 8;
    _cfg_cb_relay_t *r2 = realloc (relay, sizeof (*relay) + size * sizeof (relay->items[0]));
    if (!r2)
      return;
    r2->size = size;
    entry->callback_data = relay = r2;
  }

  info = &relay->items[0] + relay->used;
  info->callback = callback;
  info->data = data;
  relay->used++;
}

static const char *config_xlate_old (const char *s) {
  static const char * const tab[] = {
  /*"audio.a52_pass_through",			NULL,*/
    "audio.alsa_a52_device",			"audio.device.alsa_passthrough_device",
    "audio.alsa_default_device",		"audio.device.alsa_default_device",
    "audio.alsa_front_device",			"audio.device.alsa_front_device",
    "audio.alsa_mixer_name",			"audio.device.alsa_mixer_name",
    "audio.alsa_mmap_enable",			"audio.device.alsa_mmap_enable",
    "audio.alsa_surround40_device",		"audio.device.alsa_surround40_device",
    "audio.alsa_surround51_device",		"audio.device.alsa_surround51_device",
    "audio.av_sync_method",			"audio.synchronization.av_sync_method",
  /*"audio.directx_device",			NULL,*/
    "audio.esd_latency",			"audio.device.esd_latency",
  /*"audio.five_channel",			NULL,*/
  /*"audio.five_lfe_channel",			NULL,*/
    "audio.force_rate",				"audio.synchronization.force_rate",
  /*"audio.four_channel",			NULL,*/
  /*"audio.four_lfe_channel",			NULL,*/
    "audio.irixal_gap_tolerance",		"audio.device.irixal_gap_tolerance",
  /*"audio.mixer_name",				NULL,*/
    "audio.mixer_number",			"audio.device.oss_mixer_number",
    "audio.mixer_volume",			"audio.volume.mixer_volume",
    "audio.num_buffers",			"engine.buffers.audio_num_buffers",
    "audio.oss_device_name",			"audio.device.oss_device_name",
  /*"audio.oss_device_num",			NULL,*/
    "audio.oss_device_number",			"audio.device.oss_device_number",
  /*"audio.oss_pass_through_bug",		NULL,*/
    "audio.passthrough_offset",			"audio.synchronization.passthrough_offset",
    "audio.remember_volume",			"audio.volume.remember_volume",
    "audio.resample_mode",			"audio.synchronization.resample_mode",
    "audio.speaker_arrangement",		"audio.output.speaker_arrangement",
    "audio.sun_audio_device",			"audio.device.sun_audio_device",
    "codec.a52_dynrng",				"audio.a52.dynamic_range",
    "codec.a52_level",				"audio.a52.level",
    "codec.a52_surround_downmix",		"audio.a52.surround_downmix",
    "codec.ffmpeg_pp_quality",			"video.processing.ffmpeg_pp_quality",
    "codec.real_codecs_path",			"decoder.external.real_codecs_path",
    "codec.win32_path",				"decoder.external.win32_codecs_path",
    "dxr3.alt_play_mode",			"dxr3.playback.alt_play_mode",
    "dxr3.color_interval",			"dxr3.output.keycolor_interval",
    "dxr3.correct_durations",			"dxr3.playback.correct_durations",
  /*"dxr3.devicename",				NULL,*/
    "dxr3.enc_add_bars",			"dxr3.encoding.add_bars",
    "dxr3.enc_alt_play_mode",			"dxr3.encoding.alt_play_mode",
    "dxr3.enc_swap_fields",			"dxr3.encoding.swap_fields",
    "dxr3.encoder",				"dxr3.encoding.encoder",
    "dxr3.fame_quality",			"dxr3.encoding.fame_quality",
    "dxr3.keycolor",				"dxr3.output.keycolor",
    "dxr3.lavc_bitrate",			"dxr3.encoding.lavc_bitrate",
    "dxr3.lavc_qmax",				"dxr3.encoding.lavc_qmax",
    "dxr3.lavc_qmin",				"dxr3.encoding.lavc_qmin",
    "dxr3.lavc_quantizer",			"dxr3.encoding.lavc_quantizer",
    "dxr3.preferred_tvmode",			"dxr3.output.tvmode",
    "dxr3.rte_bitrate",				"dxr3.encoding.rte_bitrate",
    "dxr3.shrink_overlay_area",			"dxr3.output.shrink_overlay_area",
    "dxr3.sync_every_frame",			"dxr3.playback.sync_every_frame",
    "dxr3.videoout_mode",			"dxr3.output.mode",
    "input.cdda_cddb_cachedir",			"media.audio_cd.cddb_cachedir",
    "input.cdda_cddb_port",			"media.audio_cd.cddb_port",
    "input.cdda_cddb_server",			"media.audio_cd.cddb_server",
    "input.cdda_device",			"media.audio_cd.device",
    "input.cdda_use_cddb",			"media.audio_cd.use_cddb",
    "input.css_cache_path",			"media.dvd.css_cache_path",
    "input.css_decryption_method",		"media.dvd.css_decryption_method",
    "input.drive_slowdown",			"media.audio_cd.drive_slowdown",
    "input.dvb_last_channel_enable",		"media.dvb.remember_channel",
    "input.dvb_last_channel_watched",		"media.dvb.last_channel",
    "input.dvbdisplaychan",			"media.dvb.display_channel",
    "input.dvbzoom",				"media.dvb.zoom",
    "input.dvb_adapternum",			"media.dvb.adapter",
    "input.dvd_device",				"media.dvd.device",
    "input.dvd_language",			"media.dvd.language",
    "input.dvd_raw_device",			"media.dvd.raw_device",
    "input.dvd_region",				"media.dvd.region",
    "input.dvd_seek_behaviour",			"media.dvd.seek_behaviour",
    "input.dvd_skip_behaviour",			"media.dvd.skip_behaviour",
    "input.dvd_use_readahead",			"media.dvd.readahead",
    "input.file_hidden_files",			"media.files.show_hidden_files",
    "input.file_origin_path",			"media.files.origin_path",
    "input.http_no_proxy",			"media.network.http_no_proxy",
    "input.http_proxy_host",			"media.network.http_proxy_host",
    "input.http_proxy_password",		"media.network.http_proxy_password",
    "input.http_proxy_port",			"media.network.http_proxy_port",
    "input.http_proxy_user",			"media.network.http_proxy_user",
    "input.mms_network_bandwidth",		"media.network.bandwidth",
    "input.mms_protocol",			"media.network.mms_protocol",
    "input.pvr_device",				"media.wintv_pvr.device",
    "input.v4l_radio_device_path",		"media.video4linux.radio_device",
    "input.v4l_video_device_path",		"media.video4linux.video_device",
    "input.vcd_device",				"media.vcd.device",
    "misc.cc_center",				"subtitles.closedcaption.center",
    "misc.cc_enabled",				"subtitles.closedcaption.enabled",
    "misc.cc_font",				"subtitles.closedcaption.font",
    "misc.cc_font_size",			"subtitles.closedcaption.font_size",
    "misc.cc_italic_font",			"subtitles.closedcaption.italic_font",
    "misc.cc_scheme",				"subtitles.closedcaption.scheme",
    "misc.demux_strategy",			"engine.demux.strategy",
    "misc.memcpy_method",			"engine.performance.memcpy_method",
    "misc.osd_text_palette",			"ui.osd.text_palette",
    "misc.save_dir",				"media.capture.save_dir",
    "misc.spu_font",				"subtitles.separate.font",
    "misc.spu_src_encoding",			"subtitles.separate.src_encoding",
    "misc.spu_subtitle_size",			"subtitles.separate.subtitle_size",
    "misc.spu_use_unscaled_osd",		"subtitles.separate.use_unscaled_osd",
    "misc.spu_vertical_offset",			"subtitles.separate.vertical_offset",
    "misc.sub_timeout",				"subtitles.separate.timeout",
    "post.goom_csc_method",			"effects.goom.csc_method",
    "post.goom_fps",				"effects.goom.fps",
    "post.goom_height",				"effects.goom.height",
    "post.goom_width",				"effects.goom.width",
    "vcd.autoadvance",				"media.vcd.autoadvance",
    "vcd.autoplay",				"media.vcd.autoplay",
    "vcd.comment_format",			"media.vcd.comment_format",
    "vcd.debug",				"media.vcd.debug",
    "vcd.default_device",			"media.vcd.device",
    "vcd.length_reporting",			"media.vcd.length_reporting",
    "vcd.show_rejected",			"media.vcd.show_rejected",
    "vcd.title_format",				"media.vcd.title_format",
    "video.XV_DOUBLE_BUFFER",			"video.device.xv_double_buffer",
    "video.XV_FILTER",				"video.device.xv_filter",
    "video.deinterlace_method",			"video.output.xv_deinterlace_method",
    "video.disable_exact_osd_alpha_blending",	"video.output.disable_exact_alphablend",
    "video.disable_scaling",			"video.output.disable_scaling",
    "video.fb_device",				"video.device.fb_device",
    "video.fb_gamma",				"video.output.fb_gamma",
    "video.horizontal_position",		"video.output.horizontal_position",
    "video.num_buffers",			"engine.buffers.video_num_buffers",
    "video.opengl_double_buffer",		"video.device.opengl_double_buffer",
    "video.opengl_gamma",			"video.output.opengl_gamma",
    "video.opengl_min_fps",			"video.output.opengl_min_fps",
    "video.opengl_renderer",			"video.output.opengl_renderer",
    "video.pgx32_device",			"video.device.pgx32_device",
    "video.pgx64_brightness",			"video.output.pgx64_brightness",
    "video.pgx64_chromakey_en",			"video.device.pgx64_chromakey_en",
    "video.pgx64_colour_key",			"video.device.pgx64_colour_key",
  /*"video.pgx64_device",			NULL,*/
    "video.pgx64_multibuf_en",			"video.device.pgx64_multibuf_en",
  /*"video.pgx64_overlay_mode",			NULL,*/
    "video.pgx64_saturation",			"video.output.pgx64_saturation",
    "video.sdl_hw_accel",			"video.device.sdl_hw_accel",
    "video.unichrome_cpu_save",			"video.device.unichrome_cpu_save",
    "video.vertical_position",			"video.output.vertical_position",
    "video.vidix_blue_intensity",		"video.output.vidix_blue_intensity",
    "video.vidix_colour_key_blue",		"video.device.vidix_colour_key_blue",
    "video.vidix_colour_key_green",		"video.device.vidix_colour_key_green",
    "video.vidix_colour_key_red",		"video.device.vidix_colour_key_red",
    "video.vidix_green_intensity",		"video.output.vidix_green_intensity",
    "video.vidix_red_intensity",		"video.output.vidix_red_intensity",
    "video.vidix_use_double_buffer",		"video.device.vidix_double_buffer",
    "video.vidixfb_device",			"video.device.vidixfb_device",
    "video.warn_discarded_threshold",		"engine.performance.warn_discarded_threshold",
    "video.warn_skipped_threshold",		"engine.performance.warn_skipped_threshold",
    "video.xshm_gamma",				"video.output.xshm_gamma",
    "video.xv_autopaint_colorkey",		"video.device.xv_autopaint_colorkey",
    "video.xv_colorkey",			"video.device.xv_colorkey",
    "video.xv_pitch_alignment",			"video.device.xv_pitch_alignment",
    "video.xvmc_more_frames",			"video.device.xvmc_more_frames",
    "video.xvmc_nvidia_color_fix",		"video.device.xvmc_nvidia_color_fix"
  };
  int b = 0, e = sizeof (tab) / sizeof (tab[0]) / 2, m = e >> 1;
  do {
    int d = strcmp (s, tab[m << 1]);
    if (d == 0)
      return tab[(m << 1) + 1];
    if (d < 0)
      e = m;
    else
      b = m + 1;
    m = (b + e) >> 1;
  } while (b != e);
  return NULL;
}

static int config_section_enum (const uint8_t *s, uint32_t l) {
  int d;

  switch (l) {
    case 2:
      if (!memcmp (s, "ui", 2))
        return 2;
      break;
    case 3:
      if (!memcmp (s, "gui", 3))
        return 1;
      break;
    case 4:
      d = memcmp (s, "misc", 4);
      if (d == 0)
        return 14;
      if (d < 0) {
        if (!memcmp (s, "dxr3", 4))
          return 5;
      } else {
        if (!memcmp (s, "post", 4))
          return 11;
      }
      break;
    case 5:
      d = memcmp (s, "input", 5);
      if (d == 0)
        return 6;
      if (d < 0) {
        if (!memcmp (s, "audio", 5))
          return 3;
        if (!memcmp (s, "codec", 5))
          return 8;
      } else {
        if (!memcmp (s, "media", 5))
          return 7;
        if (!memcmp (s, "video", 5))
          return 4;
      }
      break;
    case 6:
      if (!memcmp (s, "engine", 6))
        return 13;
      break;
    case 7:
      if (!memcmp (s, "decoder", 7))
        return 9;
      if (!memcmp (s, "effects", 7))
        return 12;
      break;
    case 9:
      if (!memcmp (s, "subtitles", 9))
        return 10;
      break;
    default: ;
  }
  return 0;
}

static size_t config_make_sort_key (char *dest, const char *key, int exp_level) {
  uint8_t *q = (uint8_t *)dest, *e = q + MAX_SORT_KEY - 7;
  const uint8_t *p = (const uint8_t *)key, *b;
  int n;
  uint32_t l;

  /* section name */
  b = p;
  while (1) {
    while (*p > '.')
      p++;
    if (!*p || (*p == '.'))
      break;
    p++;
  }
  l = p - b;
  n = config_section_enum (b, l);
  if (n > 0) {
    *q++ = n;
  } else {
    if (l > (uint32_t)(e - q))
      l = e - q;
    memcpy (q, b, l);
    q += l;
  }
  if (*p)
    p++;
  *q++ = 0x1f;

  /* subsection name */
  b = p;
  while (1) {
    while (*p > '.')
      p++;
    if (!*p || (*p == '.'))
      break;
    p++;
  }
  l = p - b;
  if (l > (uint32_t)(e - q))
    l = e - q;
  memcpy (q, b, l);
  q += l;
  if (*p)
    p++;
  *q++ = 0x1f;

  /* TJ. original code did sort by section/subsection/exp/name.
   * We can do that here as well but that means inefficient
   * adding (2 passes) and finding (linear scanning).
   * 2 entries with same key but different exp level never
   * were supported anyway, and frontends group entries by
   * section only. So lets leave level out, and benefit from
   * binary searches instead.
   */
#if 0
  /* experience level */
  n = exp_level;
  q[2] = (n % 10) + '0';
  n /= 10;
  q[1] = (n % 10) + '0';
  n /= 10;
  q[0] = (n % 10) + '0';
  q += 3;
  *q++ = 0x1f;
#else
  (void)exp_level;
#endif

  /* entry name */
  b = p;
  l = strlen ((const char *)b);
  if (l > (uint32_t)(e - q))
    l = e - q;
  memcpy (q, b, l);
  q += l;
  *q = 0;
  return q - (uint8_t *)dest;
}

#if 0
/* Ugly: rebuild index every time. Maybe we could cache it somewhere? */
static cfg_entry_t **config_array (config_values_t *this, cfg_entry_t **tab, int *n) {
  cfg_entry_t *e;
  int m = *n, i;
  for (i = 0, e = this->first; e; e = e->next) {
    tab[i++] = e;
    if (i >= m) {
      cfg_entry_t **t2;
      if (m == *n) {
        m += 512;
        t2 = malloc (m * sizeof (*tab));
        if (!t2)
          break;
        memcpy (t2, tab, (m - 512) * sizeof (*tab));
      } else {
        m += 512;
        t2 = realloc (tab, m * sizeof (*tab));
        if (!t2)
          break;
      }
      tab = t2;
    }
  }
  *n = i;
  return tab;
}
#endif

static int config_validate (config_values_t *this_gen) {
  fat_config_values_t *this = (fat_config_values_t *)this_gen;
  fat_cfg_entry_t *entry;
  int num_new = 0;
  /* Paranoia:
   * 1. Find manually added entries. */
  for (entry = (fat_cfg_entry_t *)this->config.first; entry; entry = (fat_cfg_entry_t *)entry->entry.next) {
    if (!_config_is_fat_entry (entry)) {
      if (xine_sarray_add (this->key_index, entry) >= 0) {
        num_new++;
        if (this->config.xine) {
          xprintf (this->config.xine, XINE_VERBOSITY_DEBUG,
            LOG_MODULE ": WARNING: found manually added entry \"%s\".\n", entry->entry.key);
        }
      }
    }
  }
  if (num_new) {
    int index, num_entries = xine_sarray_size (this->key_index);
    cfg_entry_t **prev;

    for (prev = &this->config.first, index = 0; index < num_entries; prev = &entry->entry.next, index++) {
      entry = xine_sarray_get (this->key_index, index);

      *prev = &entry->entry;
    }
    this->config.last = &entry->entry;
  }
  return num_new;
}

#define FIND_ONLY 0x7fffffff
static cfg_entry_t *config_insert (config_values_t *this_gen, const char *key, int exp_level) {
  fat_config_values_t *this = (fat_config_values_t *)this_gen;
  very_fat_cfg_entry_t dummy_entry;
  size_t internal_key_len;
  fat_cfg_entry_t *entry;
  int index, num_entries;

  _config_set_fat_entry (&dummy_entry.entry);
  dummy_entry.entry.internal_key = xine_fast_string_init (dummy_entry.buf, sizeof (dummy_entry.buf));
  internal_key_len = config_make_sort_key (dummy_entry.entry.internal_key, key, exp_level);
  xine_fast_string_set (dummy_entry.entry.internal_key, NULL, internal_key_len);
  num_entries = xine_sarray_size (this->key_index);

  if (exp_level == FIND_ONLY) {
    index = xine_sarray_binary_search (this->key_index, &dummy_entry);
    if (index < 0) {
      /* scan manually added entries */
      if (config_validate (&this->config))
        index = xine_sarray_binary_search (this->key_index, &dummy_entry);
      if (index < 0)
        return NULL;
    }
    entry = xine_sarray_get (this->key_index, index);
  } else {
    index = xine_sarray_add (this->key_index, &dummy_entry);
    if (index >= 0) {
      cfg_entry_t *e1, *e2;
      char *buf;
      /* new */
      entry = malloc (sizeof (*entry) + internal_key_len + 32);
      xine_sarray_move_location (this->key_index, entry, index);
      if (!entry)
        return NULL;
#ifdef HAVE_ZERO_SAFE_MEM
      memset (&entry->entry, 0, sizeof (entry->entry));
#else
      entry->entry.num_value     = 0;
      entry->entry.num_default   = 0;
      entry->entry.range_min     = 0;
      entry->entry.range_max     = 0;
      entry->entry.next          = NULL;
      entry->entry.enum_values   = NULL;
      entry->entry.unknown_value = NULL;
      entry->entry.str_value     = NULL;
      entry->entry.str_default   = NULL;
      entry->entry.help          = NULL;
      entry->entry.description   = NULL;
      entry->entry.callback      = NULL;
      entry->entry.callback_data = NULL;
#endif
      entry->entry.config        = &this->config;
      entry->entry.key           = strdup (key);
      entry->entry.type          = XINE_CONFIG_TYPE_UNKNOWN;
      entry->entry.exp_level     = exp_level;
      _config_set_fat_entry (entry);
      buf = (char *)entry + sizeof (*entry);
      entry->internal_key = xine_fast_string_init (buf, internal_key_len + 32);
      xine_fast_string_set (entry->internal_key, dummy_entry.entry.internal_key, internal_key_len);
      /* sigh. make public links. */
      num_entries++;
      e1 = index > 0 ? xine_sarray_get (this->key_index, index - 1) : NULL;
      e2 = index < num_entries - 1 ? xine_sarray_get (this->key_index, index + 1) : NULL;
      if (e1) {
        if (e2) {
          if (e1->next == e2) {
            e1->next = &entry->entry;
            entry->entry.next = e2;
          }
        } else {
          e1->next = &entry->entry;
          this->config.last = &entry->entry;
        }
      } else {
        if (e2) {
          this->config.first = &entry->entry;
          entry->entry.next = e2;
        } else {
          this->config.first = this->config.last = &entry->entry;
        }
      }
    } else {
      /* found */
      index = ~index;
      entry = xine_sarray_get (this->key_index, index);
    }
  }
  return &entry->entry;
}

static const char *config_xlate_internal (const char *key, const xine_config_entry_translation_t *trans)
{
  --trans;
  while ((++trans)->old_name)
    if (trans->new_name[0] && strcmp(key, trans->old_name) == 0)
      return trans->new_name;
  return NULL;
}

#define _MAX_CFG_KEY 320
static const char *config_translate_key (const char *key, char *tmp, size_t klen) {
  /* Returns translated key or, if no translation found, NULL.
   * Translated key may be written to tmp. */
  const char *newkey;

  /* first, special-case the decoder entries (so that new ones can be added
   * without requiring modification of the translation table). */
  if (!strncmp (key, "decoder.", 8)) {
    if ((klen > 8 + 9) && !memcmp (key + klen - 9, "_priority", 9) && (klen < _MAX_CFG_KEY + 8 + 9 - 26 - 1)) {
      memcpy (tmp, "engine.decoder_priorities.", 26);
      memcpy (tmp + 26, key + 8, klen - 8 - 9);
      tmp[26 + klen - 8 - 9] = 0;
      return tmp;
    }
  }

  /* search the translation table... */
  newkey = config_xlate_old (key);
  if (!newkey && config_entry_translation_user)
    newkey = config_xlate_internal (key, config_entry_translation_user);

  return newkey;
}

static cfg_entry_t *config_lookup_entry_int (config_values_t *this, const char *key) {
  cfg_entry_t *entry;
  char tmp[_MAX_CFG_KEY];

  /* try twice at most (second time with translation from old key name) */
  entry = config_insert (this, key, FIND_ONLY);
  if (entry)
    return entry;
  /* we did not find a match, maybe this is an old config entry name
   * trying to translate */
  key = config_translate_key (key, tmp, strlen (key));
  if (!key)
    return NULL;
  entry = config_insert (this, key, FIND_ONLY);
  return entry;
}

static char **str_array_dup (const char **from, uint32_t *n) {
#ifdef SINGLE_CHUNK_ENUMS
  uint32_t sizes[257], *sitem, all;
  const char **fitem;
  char **to, **titem, *q;

  *n = 0;
  if (!from)
    return NULL;

  fitem = from;
  sitem = sizes;
  all = sizeof (char *);
  while (*fitem && (sitem < sizes + 256))
    all += (*sitem++ = strlen (*fitem++) + 1) + sizeof (char *);
  *sitem = 0;
  to = malloc (all);
  if (!to)
    return NULL;

  *n = sitem - sizes;
  q = (char *)to + (*n + 1) * sizeof (char *);
  fitem = from;
  sitem = sizes;
  titem = to;
  while (*sitem) {
    *titem++ = q;
    xine_small_memcpy (q, *fitem++, *sitem);
    q += *sitem++;
  }
  *titem = NULL;
  return to;
#else
  const char **fitem;
  char **to, **titem;

  *n = 0;
  if (!from)
    return NULL;

  for (fitem = from; *fitem; fitem++) ;
  to = malloc ((fitem - from + 1) * sizeof (char *));
  if (!to)
    return NULL;

  *n = fitem - from;
  fitem = from;
  titem = to;
  while (*fitem)
    *titem++ = strdup (*fitem++);
  *titem = NULL;
  return to;
#endif
}

static void str_array_free (char **a) {
  char **i;
  if (!a)
    return;
  for (i = a; *i; i++) ;
  if (a[0] != (char *)(i + 1)) {
    for (i = a; *i; i++)
      free (*i);
  }
  free (a);
}

/*
 * external interface
 */

static cfg_entry_t *config_lookup_entry(config_values_t *this, const char *key) {
  cfg_entry_t *entry;

  pthread_mutex_lock(&this->config_lock);
  entry = config_lookup_entry_int (this, key);
  pthread_mutex_unlock(&this->config_lock);

  return entry;
}

/* 1 private */
static cfg_entry_t *config_lookup_entry_safe (config_values_t *this, const char *key) {
  pthread_mutex_lock (&this->config_lock);
  if (this->lookup_entry == config_lookup_entry)
    return config_lookup_entry_int (this, key);
  return this->lookup_entry (this, key);
}

static char *config_lookup_string(config_values_t *this, const char *key) {
  cfg_entry_t *entry;
  char *str_value = NULL;

  entry = config_lookup_entry_safe(this, key);
  if (!entry) {
    if (this->xine)
      xprintf (this->xine, XINE_VERBOSITY_LOG, "configfile: WARNING: entry %s not found\n", key);
    goto out;
  }

  if (entry->type != XINE_CONFIG_TYPE_STRING) {
    if (this->xine)
      xprintf (this->xine, XINE_VERBOSITY_LOG, "configfile: WARNING: %s is not string entry\n", key);
    goto out;
  }

  if (entry->str_value) {
    str_value = strdup(entry->str_value);
  }

out:
  pthread_mutex_unlock(&this->config_lock);
  return str_value;
}

static void config_free_string(config_values_t *this, char **str)
{
  (void)this;
  _x_freep(str);
}

static int config_lookup_num(config_values_t *this, const char *key, int def_value) {
  cfg_entry_t *entry;
  int value = def_value;

  entry = config_lookup_entry_safe(this, key);
  if (!entry) {
    if (this->xine)
      xprintf (this->xine, XINE_VERBOSITY_LOG, "configfile: WARNING: entry %s not found\n", key);
    goto out;
  }

  switch (entry->type) {
    case XINE_CONFIG_TYPE_RANGE:
    case XINE_CONFIG_TYPE_ENUM:
    case XINE_CONFIG_TYPE_NUM:
    case XINE_CONFIG_TYPE_BOOL:
      value = entry->num_value;
      break;
    default:
      if (this->xine)
        xprintf (this->xine, XINE_VERBOSITY_LOG, "configfile: WARNING: %s is not numeric entry\n", key);
      break;
  }

out:
  pthread_mutex_unlock(&this->config_lock);
  return value;
}

static void config_reset_value(cfg_entry_t *entry) {
  /* NULL is a frequent case. */
  if (entry->str_value)   {free (entry->str_value);   entry->str_value = NULL;}
  if (entry->str_default) {free (entry->str_default); entry->str_default = NULL;}
  if (entry->description) {free (entry->description); entry->description = NULL;}
  if (entry->help)        {free (entry->help);        entry->help = NULL;}

  str_array_free (entry->enum_values);
  entry->enum_values = NULL;
  entry->num_value = 0;
}

static void config_shallow_copy (xine_cfg_entry_t *dest, const cfg_entry_t *src) {
  dest->key           = src->key;
  dest->type          = src->type;
  dest->exp_level     = src->exp_level;
  dest->unknown_value = src->unknown_value;
  dest->str_value     = src->str_value;
  dest->str_default   = src->str_default;
  dest->num_value     = src->num_value;
  dest->num_default   = src->num_default;
  dest->range_min     = src->range_min;
  dest->range_max     = src->range_max;
  dest->enum_values   = src->enum_values;
  dest->description   = src->description;
  dest->help          = src->help;
  dest->callback      = src->callback;
  dest->callback_data = src->callback_data;
}

static cfg_entry_t *config_register_key (config_values_t *this,
  const char *key, int exp_level, xine_config_cb_t changed_cb, void *cb_data,
  const char *description, const char *help) {
  cfg_entry_t *entry;

  lprintf ("registering %s\n", key);

  pthread_mutex_lock (&this->config_lock);
  entry = config_insert (this, key, exp_level);
  if (!entry) {
    pthread_mutex_unlock (&this->config_lock);
    return NULL;
  }

  /* new entry */
  entry->exp_level = exp_level != FIND_ONLY ? exp_level : 0;

  /* add callback */
  _cfg_cb_add (entry, changed_cb, cb_data);

  /* we created a new entry, call the callback */
  if (this->new_entry_cb) {
    xine_cfg_entry_t cb_entry;
    /* thread safe extension, private to _x_scan_plugins ()
     * (.cur is otherwise unused). */
    this->cur = entry;
    config_shallow_copy(&cb_entry, entry);
    this->new_entry_cb(this->new_entry_cbdata, &cb_entry);
    this->cur = NULL;
  }

  if (entry->type != XINE_CONFIG_TYPE_UNKNOWN) {
    lprintf ("config entry already registered: %s\n", key);
  } else {
    config_reset_value (entry);
    entry->description = description ? strdup (description) : NULL;
    entry->help        = help ? strdup (help) : NULL;
  }

  return entry;
}

static char *config_register_string (config_values_t *this,
  const char *key, const char *def_value, const char *description, const char *help,
  int exp_level, xine_config_cb_t changed_cb, void *cb_data) {

  cfg_entry_t *entry;

  if (!key || !def_value) {
    if (this->xine)
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "config_register_string: error: config=%p, key=%s, def_value=%s.\n",
               (void *)this, key ? key : "NULL", def_value ? def_value : "NULL");
    return NULL;
  }

  entry = config_register_key (this, key, exp_level, changed_cb, cb_data, description, help);
  if (!entry)
    return NULL;

  if (entry->type == XINE_CONFIG_TYPE_UNKNOWN) {
    /* set string */
    entry->type = XINE_CONFIG_TYPE_STRING;
    entry->num_value = 0;
    entry->str_default = strdup (def_value);
    if (entry->unknown_value) {
      entry->str_value = entry->unknown_value;
      entry->unknown_value = NULL;
    } else {
      entry->str_value = strdup (def_value);
    }
  }

  pthread_mutex_unlock (&this->config_lock);
  return entry->str_value;
}

static char *config_register_filename (config_values_t *this,
  const char *key, const char *def_value, int req_type,
  const char *description, const char *help, int exp_level,
  xine_config_cb_t changed_cb, void *cb_data) {

  cfg_entry_t *entry;

  if (!key || !def_value) {
    if (this->xine)
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "config_register_filename: error: config=%p, key=%s, def_value=%s.\n",
               (void *)this, key ? key : "NULL", def_value ? def_value : "NULL");
    return NULL;
  }

  entry = config_register_key (this, key, exp_level, changed_cb, cb_data, description, help);
  if (!entry)
    return NULL;

  if (entry->type == XINE_CONFIG_TYPE_UNKNOWN) {
    /* set string */
    entry->type = XINE_CONFIG_TYPE_STRING;
    entry->num_value = req_type;
    entry->str_default = strdup (def_value);
    if (entry->unknown_value) {
      entry->str_value = entry->unknown_value;
      entry->unknown_value = NULL;
    } else {
      entry->str_value = strdup (def_value);
    }
  }

  pthread_mutex_unlock (&this->config_lock);
  return entry->str_value;
}

static int config_register_num (config_values_t *this,
				const char *key,
				int def_value,
				const char *description,
				const char *help,
				int exp_level,
				xine_config_cb_t changed_cb,
				void *cb_data) {

  cfg_entry_t *entry;

  if (!key) {
    if (this->xine)
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "config_register_num: error: config=%p, key=%s.\n",
               (void *)this, key ? key : "NULL");
    return 0;
  }

  entry = config_register_key (this, key, exp_level, changed_cb, cb_data, description, help);
  if (!entry)
    return 0;

  if (entry->type == XINE_CONFIG_TYPE_UNKNOWN) {
    const char *val;
    entry->type = XINE_CONFIG_TYPE_NUM;
    entry->num_default = def_value;
    val = entry->unknown_value;
    entry->num_value = val ? xine_str2int32 (&val) : def_value;
  }

  pthread_mutex_unlock (&this->config_lock);
  return entry->num_value;
}

static int config_register_bool (config_values_t *this,
				 const char *key,
				 int def_value,
				 const char *description,
				 const char *help,
				 int exp_level,
				 xine_config_cb_t changed_cb,
				 void *cb_data) {

  cfg_entry_t *entry;

  if (!key) {
    if (this->xine)
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "config_register_bool: error: config=%p, key=%s.\n",
               (void *)this, key ? key : "NULL");
    return 0;
  }

  entry = config_register_key (this, key, exp_level, changed_cb, cb_data, description, help);
  if (!entry)
    return 0;

  if (entry->type == XINE_CONFIG_TYPE_UNKNOWN) {
    const char *val;
    entry->type = XINE_CONFIG_TYPE_BOOL;
    entry->num_default = def_value;
    val = entry->unknown_value;
    entry->num_value = val ? xine_str2int32 (&val) : def_value;
  }

  pthread_mutex_unlock (&this->config_lock);
  return entry->num_value;
}

static int config_register_range (config_values_t *this,
				  const char *key,
				  int def_value,
				  int min, int max,
				  const char *description,
				  const char *help,
				  int exp_level,
				  xine_config_cb_t changed_cb,
				  void *cb_data) {

  cfg_entry_t *entry;

  if (!key) {
    if (this->xine)
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "config_register_range: error: config=%p, key=%s.\n",
               (void *)this, key ? key : "NULL");
    return 0;
  }

  entry = config_register_key (this, key, exp_level, changed_cb, cb_data, description, help);
  if (!entry)
    return 0;

  if (entry->type == XINE_CONFIG_TYPE_UNKNOWN) {
    const char *val;
    entry->type        = XINE_CONFIG_TYPE_RANGE;
    entry->num_default = def_value;
    entry->range_min   = min;
    entry->range_max   = max;
    val = entry->unknown_value;
    entry->num_value = val ? xine_str2int32 (&val) : def_value;
    /* validate value (config files can be edited ...) */
    /* Allow default to be out of range. xine-ui uses this for brightness etc. */
    if (entry->num_value != def_value) {
      if (entry->num_value > max) {
        if (this->xine)
          xprintf (this->xine, XINE_VERBOSITY_DEBUG,
            "configfile: WARNING: value %d for %s is larger than max (%d)\n", entry->num_value, key, max);
        entry->num_value = max;
      }
      if (entry->num_value < min) {
        if (this->xine)
          xprintf (this->xine, XINE_VERBOSITY_DEBUG,
            "configfile: WARNING: value %d for %s is smaller than min (%d)\n", entry->num_value, key, min);
        entry->num_value = min;
      }
    }
  }

  pthread_mutex_unlock (&this->config_lock);
  return entry->num_value;
}

static int config_parse_enum (const char *str, const char **values) {

  const char **value;
  int    i;


  value = values;
  i = 0;

  while (*value) {

    lprintf ("parse enum, >%s< ?= >%s<\n", *value, str);

    if (!strcmp (*value, str))
      return i;

    value++;
    i++;
  }

  lprintf ("warning, >%s< is not a valid enum here, using 0\n", str);

  return 0;
}

static int config_register_enum (config_values_t *this,
				 const char *key,
				 int def_value,
				 char **values,
				 const char *description,
				 const char *help,
				 int exp_level,
				 xine_config_cb_t changed_cb,
				 void *cb_data) {

  cfg_entry_t *entry;

  if (!key || !values) {
    if (this->xine)
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "config_register_enum: error: config=%p, key=%s, values=%p.\n",
               (void *)this, key ? key : "NULL", (void *)values);
    return 0;
  }

  entry = config_register_key (this, key, exp_level, changed_cb, cb_data, description, help);
  if (!entry)
    return 0;

  if (entry->type == XINE_CONFIG_TYPE_UNKNOWN) {
    uint32_t value_count;
    entry->type = XINE_CONFIG_TYPE_ENUM;
    entry->num_default = def_value;
    if (entry->unknown_value)
      entry->num_value = config_parse_enum (entry->unknown_value, (const char **)values);
    else
      entry->num_value = def_value;
    /* allocate and copy the enum values */
    entry->enum_values = str_array_dup ((const char **)values, &value_count);
    entry->range_min = 0;
    entry->range_max = value_count;
    if (entry->num_value < 0)
      entry->num_value = 0;
    if (entry->num_value >= (int)value_count)
      entry->num_value = value_count;
  } else if (entry->type == XINE_CONFIG_TYPE_ENUM) {
    /* xine-ui does this to update the list. */
    if (entry->enum_values && values) {
      const char **old, **new;
      for (old = (const char **)entry->enum_values, new = (const char **)values; *old && *new; old++, new++) {
        if (strcmp (*old, *new))
          break;
      }
      if (*old || *new) {
        uint32_t value_count;
        char **nv = str_array_dup ((const char **)values, &value_count);
        if (nv) {
          int found = -1;
          if ((entry->num_value >=0) && (entry->num_value < entry->range_max)) {
            for (new = (const char **)values; *new; new++) {
              if (!strcmp (entry->enum_values[entry->num_value], *new)) {
                found = new - (const char **)values;
                break;
              }
            }
          }
          str_array_free (entry->enum_values);
          entry->enum_values = nv;
          entry->num_default = (def_value >= 0) && (def_value < (int)value_count) ? def_value : 0;
          entry->num_value = found < 0 ? 0 : found;
          entry->range_min = 0;
          entry->range_max = value_count;
        }
      }
    }
  }

  pthread_mutex_unlock (&this->config_lock);
  return entry->num_value;
}

static void config_update_num_e (cfg_entry_t *entry, int value) {

  switch (entry->type) {
    case XINE_CONFIG_TYPE_RANGE:
      if (value != entry->num_default) {
        if (value < entry->range_min)
          value = entry->range_min;
        else if (value > entry->range_max)
          value = entry->range_max;
      }
      entry->num_value = value;
      break;
    case XINE_CONFIG_TYPE_BOOL:
      entry->num_value = value ? 1 : 0;
      break;
    case XINE_CONFIG_TYPE_ENUM:
      if (value != entry->num_default) {
        if (value < 0)
          value = 0;
        else if (value >= entry->range_max)
          value = entry->range_max - 1;
      }
      /* fall through */
    case XINE_CONFIG_TYPE_NUM:
      entry->num_value = value;
      break;
    default: ;
  }

  if (entry->callback) {
    xine_cfg_entry_t cb_entry;
    config_shallow_copy (&cb_entry, entry);
    /* it is safe to enter the callback from within a locked context
     * because we use a recursive mutex.
     */
    entry->callback (entry->callback_data, &cb_entry);
  }
}

static void config_update_num (config_values_t *this, const char *key, int value) {
  cfg_entry_t *entry;
  lprintf ("updating %s to %d\n", key, value);
  entry = config_lookup_entry_safe (this, key);
  if (!entry) {
    lprintf ("WARNING! tried to update unknown key %s (to %d)\n", key, value);
    pthread_mutex_unlock (&this->config_lock);
    return;
  }
  if ((entry->type == XINE_CONFIG_TYPE_UNKNOWN)
      || (entry->type == XINE_CONFIG_TYPE_STRING)) {
    if (this->xine)
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "configfile: error - tried to update non-num type %d (key %s, value %d)\n",
               entry->type, entry->key, value);
    pthread_mutex_unlock (&this->config_lock);
    return;
  }
  config_update_num_e (entry, value);
  pthread_mutex_unlock (&this->config_lock);
}

/* Anything can be updated with a string. Config file load uses this. */
static void config_update_string_e (cfg_entry_t *entry, const char *value) {
  char *str_free = NULL;
  int min, max;

  switch (entry->type) {
    case XINE_CONFIG_TYPE_RANGE:
      min = entry->range_min;
      max = entry->range_max;
      goto set_snum;
    case XINE_CONFIG_TYPE_NUM:
      min = -2147483647 - 1;
      max = 2147483647;
      goto set_snum;
    case XINE_CONFIG_TYPE_BOOL:
      min = 0;
      max = 1;
    set_snum: {
      const char *val = value;
      int v = xine_str2int32 (&val);
      if (v != entry->num_default) {
        if (v < min)
          v = min;
        else if (v > max)
          v = max;
      }
      entry->num_value = v;
      break;
    }
    case XINE_CONFIG_TYPE_ENUM:
      entry->num_value = config_parse_enum (value, (const char **)entry->enum_values);
      break;
    case XINE_CONFIG_TYPE_STRING:
      /* Client may be using the string directly, not a private duplicate of it.
       * Idea #1: Let both the old and new strings be valid during the change callback
       * as sort of a safe switch protocol there. However, to be really safe, client
       * needs to lock the config object during any use of the string as well.
       * Even worse: no callback...
       * Idea #2: keep a full backlog of outdated strings.
       * Might be misused for flooding the heap.
       * Idea #3: with callback, park previous string at entry.unknown_value.
       * without, park initial string there. */
      if (value != entry->str_value) {
        if (entry->str_value) {
          if (entry->callback || !entry->unknown_value) {
            str_free = entry->unknown_value;
            entry->unknown_value = entry->str_value;
          } else {
            str_free = entry->str_value;
          }
        }
        entry->str_value = strdup (value);
      }
      break;
    default:
      if (value != entry->unknown_value) {
        str_free = entry->unknown_value;
        entry->unknown_value = strdup (value);
      }
  }

  if (entry->callback) {
    xine_cfg_entry_t cb_entry;
    config_shallow_copy (&cb_entry, entry);
    /* it is safe to enter the callback from within a locked context
     * because we use a recursive mutex.
     */
    entry->callback (entry->callback_data, &cb_entry);
  }
  free (str_free);
}

static void config_update_string (config_values_t *this, const char *key, const char *value) {
  cfg_entry_t *entry;
  lprintf ("updating %s to %s\n", key, value);
  entry = config_lookup_entry_safe (this, key);
  if (!entry) {
    if (this->xine)
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "configfile: error - tried to update unknown key %s (to %s)\n", key, value);
    pthread_mutex_unlock (&this->config_lock);
    return;
  }
  config_update_string_e (entry, value);
  pthread_mutex_unlock (&this->config_lock);
}

/*
 * front end config translation handling
 */
void xine_config_set_translation_user (const xine_config_entry_translation_t *xlate)
{
  config_entry_translation_user = xlate;
}

/*
 * load/save config data from/to afile (e.g. $HOME/.xine/config)
 */
void xine_config_load (xine_t *xine, const char *filename) {
  config_values_t *this = xine->config;
  xine_fast_text_t *xft;

  this->xine = xine;

  lprintf ("reading from file '%s'\n", filename);

  /* TJ. I got far less than 32k, so > 2M is probably insane. */
  xft = xine_fast_text_load (filename, 2 << 20);
  if (xft) {
    int version;

    pthread_mutex_lock (&this->config_lock);
    version = this->current_version;
    pthread_mutex_unlock (&this->config_lock);

    while (1) {
      size_t lsize;
      char *line = xine_fast_text_line (xft, &lsize);
      char *value;

      if (!line)
        break;

      /* skip comments */
      if (line[0] == '#')
        continue;

      if (line[0] == '.') {
        if ((lsize > 9) && !memcmp (line + 1, "version:", 8)) {
          const char *val = line + 9;
          version = xine_str2int32 (&val);
          if (version > CONFIG_FILE_VERSION) {
            xine_log (xine, XINE_LOG_MSG,
              _("The current config file has been modified by a newer version of xine."));
          }
          pthread_mutex_lock (&this->config_lock);
          this->current_version = version;
          pthread_mutex_unlock (&this->config_lock);
          continue;
        }
      }
      
      value = strchr (line, ':');
      if (value) {
        cfg_entry_t *entry;
        size_t klen = value - line;

        *value++ = 0;

        pthread_mutex_lock (&this->config_lock);

        if (version < CONFIG_FILE_VERSION) {
          /* old config file -> let's see if we have to rename this one */
          entry = config_insert (this, line, FIND_ONLY);
          if (!entry) {
            char tmp[_MAX_CFG_KEY];
            const char *key = config_translate_key (line, tmp, klen);
            if (!key)
              key = line; /* no translation? fall back on untranslated key */
            entry = config_insert (this, key, 50);
          }
        } else {
          entry = config_insert (this, line, 50);
        }

        if (entry)
          config_update_string_e (entry, value);

        pthread_mutex_unlock (&this->config_lock);
      }
    }
    xine_fast_text_unload (&xft);
    xine_log (xine, XINE_LOG_MSG,
      _("Loaded configuration from file '%s'\n"), filename);
    return;
  }

  if (errno != ENOENT)
    xine_log (xine, XINE_LOG_MSG,
      _("Failed to load configuration from file '%s': %s\n"), filename, strerror (errno));
}

static size_t xine_realpath (char *buf, const char *filename, size_t bsize, int *num_links) {
  char tbuf[1536];
  struct stat sbuf;
  size_t used;
  int try;

  if (!buf || !bsize)
    return 0;
  if (!filename) {
    buf[0] = 0;
    return 0;
  }

  used = strlcpy (buf, filename, bsize);
  if (used >= bsize)
    used = bsize - 1;

  try = 8;
#if defined(HAVE_LSTAT) && defined(HAVE_READLINK)
  for (; try; try--) {
    ssize_t r;

    if (lstat (buf, &sbuf))
      break;
    if (!S_ISLNK (sbuf.st_mode))
      break;
    r = readlink (buf, tbuf, sizeof (tbuf) - 1);
    if (r <= 0)
      break;
    tbuf[r] = 0;
    if (tbuf[0] == PSEP) {
      /* absolute link */
      used = (size_t)r < bsize ? (size_t)r : bsize - 1;
      memcpy (buf, tbuf, used);
      buf[used] = 0;
    } else {
      size_t left;
      char *p;
      /* relative link */
      for (p = buf + used; (p > buf) && (p[-1] != PSEP); p--) ;
      left = bsize - (p - buf);
      used = (size_t)r < left ? (size_t)r : left - 1;
      memcpy (p, tbuf, used);
      p[used] = 0;
      used += p - buf;
    }
  }
  if (!try)
    return 0;
#endif
  *num_links = 8 - try;

  return used;
}

void xine_config_save (xine_t *xine, const char *filename) {
  config_values_t *this;
  char fname[1536], bname[1536], tname[1536];
  struct stat sbuf;
  size_t blen;
  FILE *f;
#define XCF_HAVE_BACKUP 1
#define XCF_HAVE_FILE   2
#define XCF_HAVE_TFILE  4
#define XCF_HAVE_ITEMS  8
  uint32_t flags = 0;
  int num_links = 0;

  if (!xine || !filename)
    return;

  /* yes this _is_ relevant. */
  blen = xine_realpath (fname, filename, sizeof (fname) - 48, &num_links);
  if (!blen)
    return;
  if (num_links) {
    xprintf (xine, XINE_VERBOSITY_DEBUG,
      LOG_MODULE ": %s -> %s.\n", filename, fname);
  }

  /* When X server shuts down while multiple xine instances run,
   * a) concurrent writes may trash the file, and/or
   * b) write may be interrupted. */

  this = xine->config;
  xine_fast_memcpy (bname, fname, blen);
  memcpy (bname + blen, "~", 2);
  xine_fast_memcpy (tname, fname, blen);
  {
    char *p = tname + blen;

    *p++ = '.';
    xine_uint32_2str (&p, (uintptr_t)getpid ());
    *p++ = '.';
    xine_uint32_2str (&p, (uintptr_t)this);
    *p = 0;
  }

/*  xprintf(xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: backing up configfile to %s failed\n"), bname); */
  xprintf (xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": writing %s.\n", tname);

  f = fopen (tname, "wb");
  if (!f) {
    int e = errno;

    xprintf (xine, XINE_VERBOSITY_LOG,
      LOG_MODULE ": %s: %s (%d).\n", tname, strerror (e), e);
    xprintf(xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: your configuration will not be saved\n"));
    return;
  } else {
#define XCS_BUF_SIZE 4096
    char buf[XCS_BUF_SIZE], *q, *e = buf + XCS_BUF_SIZE - 28 - 4 * XINE_MAX_INT32_STR;
    cfg_entry_t *entry;
    size_t flen = 0;

    flags |= XCF_HAVE_TFILE;
    q = buf;
    memcpy (q, "#\n# xine config file\n#\n.version:", 32); q += 32;
    xine_uint32_2str (&q, CONFIG_FILE_VERSION);
    memcpy (q,
      "\n"
      "\n"
      "# Entries which are still set to their default values are commented out.\n"
      "# Remove the \'#\' at the beginning of the line, if you want to change them.\n"
      "\n",
      151); q += 151;
    flen += fwrite (buf, 1, q - buf, f);

    pthread_mutex_lock(&this->config_lock);

    for (entry = this->first; entry; entry = entry->next) {

      if (!entry->key[0]) /* deleted key */
        continue;

      lprintf ("saving key '%s'\n", entry->key);

      q = buf;
      if (entry->description) {
        memcpy (q, "# ", 2); q += 2;
        q += strlcpy (q, entry->description, e - q);
        if (q >= e)
          q = e - 1;
        *q++ = '\n';
      }

      switch (entry->type) {

      case XINE_CONFIG_TYPE_UNKNOWN: {
#if 0
        /* discard unclaimed values.
         * better dont do that because
         * a) multiple frontends may share the same config file, and
         * b) we dont always load all plugins for performance. */
#else
        q += strlcpy (q, entry->key, e - q);
        if (q >= e)
          q = e - 1;
        *q++ = ':';
        q += strlcpy (q, entry->unknown_value, e - q);
        if (q >= e)
          q = e - 1;
        memcpy (q, "\n\n", 2); q += 2;
#endif
	break;
      }

      case XINE_CONFIG_TYPE_RANGE:
        memcpy (q, "# [ ", 4); q += 3;
        xine_int32_2str (&q, entry->range_min);
        memcpy (q, "..", 2); q += 2;
        xine_int32_2str (&q, entry->range_max);
        *q++ = ']';
      tail_num:
        memcpy (q, ", default:  ", 12); q += 11;
        xine_int32_2str (&q, entry->num_default);
        *q++ = '\n';
        if (entry->num_value == entry->num_default)
          *q++ = '#';
        q += strlcpy (q, entry->key, e - q);
        if (q >= e)
          q = e - 1;
        *q++ = ':';
        xine_int32_2str (&q, entry->num_value);
        memcpy (q, "\n\n", 2); q += 2;
	break;

      case XINE_CONFIG_TYPE_STRING:
        memcpy (q, "# string, default:  ", 20); q += 19;
        q += strlcpy (q, entry->str_default, e - q);
        if (q >= e)
          q = e - 1;
        *q++ = '\n';
        if (!strcmp (entry->str_value, entry->str_default))
          *q++ = '#';
        q += strlcpy (q, entry->key, e - q);
        if (q >= e)
          q = e - 1;
        *q++ = ':';
        q += strlcpy (q, entry->str_value, e - q);
        if (q >= e)
          q = e - 1;
        memcpy (q, "\n\n", 2); q += 2;
	break;

      case XINE_CONFIG_TYPE_ENUM:
        do {
          char **value;
          memcpy (q, "# { ", 4); q += 3;
          value = entry->enum_values;
          while (*value) {
            *q++ = ' ';
            q += strlcpy (q, *value, e - q);
            if (q >= e) {
              q = e - 1;
              break;
            }
            *q++ = ' ';
            value++;
          }
          memcpy (q, "}, default: ", 12); q += 12;
          xine_uint32_2str (&q, entry->num_default);
          *q++ = '\n';
          if ((entry->num_value >= 0) &&
              (entry->num_value < entry->range_max) &&
              entry->enum_values[entry->num_value]) {
            if (entry->num_value == entry->num_default)
              *q++ = '#';
            q += strlcpy (q, entry->key, e - q);
            if (q >= e)
              q = e - 1;
            *q++ = ':';
            q += strlcpy (q, entry->enum_values[entry->num_value], e - q);
            if (q >= e)
              q = e - 1;
            *q++ = '\n';
          }
        } while (0);
        *q++ = '\n';
	break;

      case XINE_CONFIG_TYPE_NUM:
        memcpy (q, "# numeric", 9); q += 9;
        goto tail_num;

      case XINE_CONFIG_TYPE_BOOL:
        memcpy (q, "# bool", 6); q += 6;
        goto tail_num;
      }
      flen += fwrite (buf, 1, q - buf, f);
      flags |= XCF_HAVE_ITEMS;
    }
    pthread_mutex_unlock(&this->config_lock);

    /* paranoia ... */
    if (fclose (f))
      flags &= ~XCF_HAVE_TFILE;
    if (!stat (tname, &sbuf)) {
      if ((off_t)flen != sbuf.st_size)
        flags &= ~XCF_HAVE_TFILE;
    } else {
      flags &= ~XCF_HAVE_TFILE;
    }

    if ((flags & (XCF_HAVE_TFILE | XCF_HAVE_ITEMS)) != (XCF_HAVE_TFILE | XCF_HAVE_ITEMS)) {
      xprintf (xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: writing configuration to %s failed\n"), fname);
      xprintf (xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: removing possibly broken config file %s\n"), tname);
      xprintf (xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: you should check the backup file %s\n"), bname);
      /* writing config failed -> remove file, it might be broken. */
      unlink (tname);
      return;
    }

    if (!stat (bname, &sbuf) && S_ISREG (sbuf.st_mode))
      flags |= XCF_HAVE_BACKUP;
    if (!stat (fname, &sbuf) && S_ISREG (sbuf.st_mode))
      flags |= XCF_HAVE_FILE;

    if (flags & XCF_HAVE_FILE) {
      if (flags & XCF_HAVE_BACKUP)
        unlink (bname);
      rename (fname, bname);
    }
    rename (tname, fname);
  }
}

static void config_dispose (config_values_t *this_gen) {
  fat_config_values_t *this = (fat_config_values_t *)this_gen;
  cfg_entry_t *entry, *last;
  int n;

  pthread_mutex_lock (&this->config.config_lock);
  entry = this->config.first;

  lprintf ("dispose\n");

  n = 0;
  while (entry) {
    last = entry;
    entry = entry->next;

    last->next = NULL;
    n += _cfg_cb_clear_report (this->config.xine, last);
    _x_freep (&last->key);
    _x_freep (&last->unknown_value);

    config_reset_value(last);

    free (last);
  }

  xine_sarray_delete (this->key_index);
  this->key_index = NULL;

  pthread_mutex_unlock (&this->config.config_lock);

  if (n && this->config.xine) {
    xprintf (this->config.xine, XINE_VERBOSITY_DEBUG, "configfile: unregistered %d orphaned change callbacks.\n", n);
  }

  pthread_mutex_destroy (&this->config.config_lock);
  free (this);
}


static void config_unregister_cb (config_values_t *this, const char *key) {

  cfg_entry_t *entry;

  _x_assert(key);
  _x_assert(this);

  entry = config_lookup_entry_safe (this, key);
  if (entry) {
    cfg_entry_t *next = entry->next;
    entry->next = NULL;
    _cfg_cb_clear (entry);
    entry->next = next;
  }
  pthread_mutex_unlock (&this->config_lock);
}

static int config_unregister_callbacks (config_values_t *this,
  const char *key, xine_config_cb_t changed_cb, void *cb_data, size_t cb_data_size) {
  int n;
  cfg_entry_t *entry, *next;
  if (!this)
    return 0;
  next = NULL;
  if (key) {
    entry = config_lookup_entry_safe (this, key);
    if (entry) {
      next = entry->next;
      entry->next = NULL;
    }
  } else {
    pthread_mutex_lock (&this->config_lock);
    entry = this->first;
  }
  n = _cfg_any_rem (entry, changed_cb, cb_data, cb_data_size);
  if (next)
    entry->next = next;
  pthread_mutex_unlock (&this->config_lock);
  return n;
}

void _x_config_unregister_cb_class_d (config_values_t *this, void *callback_data) {

  _x_assert(this);
  _x_assert(callback_data);

  pthread_mutex_lock(&this->config_lock);
  _cfg_d_rem (this->first, callback_data, 0);
  pthread_mutex_unlock(&this->config_lock);
}

void _x_config_unregister_cb_class_p (config_values_t *this, xine_config_cb_t callback) {

  _x_assert(this);
  _x_assert(callback);

  pthread_mutex_lock (&this->config_lock);
  _cfg_cb_rem (this->first, callback);
  pthread_mutex_unlock (&this->config_lock);
}

static void config_set_new_entry_callback (config_values_t *this, xine_config_cb_t new_entry_cb, void* cbdata) {
  pthread_mutex_lock(&this->config_lock);
  this->new_entry_cb = new_entry_cb;
  this->new_entry_cbdata = cbdata;
  pthread_mutex_unlock(&this->config_lock);
}

static void config_unset_new_entry_callback (config_values_t *this) {
  pthread_mutex_lock(&this->config_lock);
  this->new_entry_cb = NULL;
  this->new_entry_cbdata = NULL;
  pthread_mutex_unlock(&this->config_lock);
}

static void put_int (uint8_t **dest, int value) {
  int32_t value_int32 = (int32_t)value;
#if (defined(__GNUC__) && (__GNUC__ >= 4)) || defined(__clang__)
#  ifdef WORDS_BIGENDIAN
  value_int32 = __builtin_bswap32 (value_int32);
#  endif
  __builtin_memcpy (*dest, &value_int32, 4);
#else
  (*dest)[0] = value_int32 & 0xFF;
  (*dest)[1] = (value_int32 >> 8) & 0xFF;
  (*dest)[2] = (value_int32 >> 16) & 0xFF;
  (*dest)[3] = (value_int32 >> 24) & 0xFF;
#endif
  *dest += 4;
}

static void put_string (uint8_t **dest, const char *value, uint32_t value_len) {
  put_int (dest, value_len);
  if (value_len > 0)
    memcpy (*dest, value, value_len);
  *dest += value_len;
}

static char* config_get_serialized_entry (config_values_t *this, const char *key) {
  cfg_entry_t *entry;

  /* thread safe extension, private to _x_scan_plugins ()
   * (.cur is otherwise unused). */
  pthread_mutex_lock (&this->config_lock);
  if (!key) {
    entry = this->cur;
    this->cur = NULL;
  } else if (this->lookup_entry == config_lookup_entry) {
    entry = config_lookup_entry_int (this, key);
  } else {
    entry = this->lookup_entry (this, key);
  }
  if (!entry) {
    pthread_mutex_unlock (&this->config_lock);
    return NULL;
  }

  {
    /* now serialize this stuff
      fields to serialize :
          int              type;
          int              range_min;
          int              range_max;
          int              exp_level;
          int              num_default;
          int              num_value;
          char            *key;
          char            *str_default;
          char            *description;
          char            *help;
          char           **enum_values;
    */

    uint8_t *buf1, *buf2, *q;

    uint32_t total_len, buf_len;
    uint32_t key_len         = entry->key         ? strlen (entry->key) : 0;
    uint32_t str_default_len = entry->str_default ? strlen (entry->str_default) : 0;
    uint32_t description_len = entry->description ? strlen (entry->description) : 0;
    uint32_t help_len        = entry->help        ? strlen (entry->help) : 0;
    uint32_t value_len[256];
    uint32_t value_count, i;
    char   **cur_value;

    /* 6 integers: value (4 bytes)
     * 4 strings:  len (4 bytes) + string (len bytes)
     *   enums:    count (4 bytes) + count * (len (4 bytes) + string (len bytes)) */
    total_len = 11 * 4 + key_len + str_default_len + description_len + help_len;

    value_count = 0;
    cur_value = entry->enum_values;
    if (cur_value) {
      while (*cur_value && (value_count < (sizeof (value_len) / sizeof (value_len[0])))) {
        value_len[value_count] = strlen (*cur_value);
        total_len += 4 + value_len[value_count];
        value_count++;
        cur_value++;
      }
    }

    /* Now we have the length needed to serialize the entry and the length of each string */
    buf_len = (total_len * 4 + 2) / 3 + 8;
    buf2 = malloc (buf_len);
    if (!buf2) {
      pthread_mutex_unlock(&this->config_lock);
      return NULL;
    }
    buf1 = buf2 + (buf_len - total_len - 4);

    /* Let's go */
    q = buf1;

    /* the integers */
    put_int (&q, entry->type);
    put_int (&q, entry->range_min);
    put_int (&q, entry->range_max);
    put_int (&q, entry->exp_level);
    put_int (&q, entry->num_default);
    put_int (&q, entry->num_value);

    /* the strings */
    put_string (&q, entry->key, key_len);
    put_string (&q, entry->str_default, str_default_len);
    put_string (&q, entry->description, description_len);
    put_string (&q, entry->help, help_len);

    /* the enum stuff */
    put_int (&q, value_count);
    cur_value = entry->enum_values;
    for (i = 0; i < value_count; i++) {
      put_string (&q, *cur_value, value_len[i]);
      cur_value++;
    }
    pthread_mutex_unlock (&this->config_lock);

    /* and now the output encoding */
    /* We're going to encode total_len bytes in base64 */
    xine_base64_encode (buf1, (char *)buf2, total_len);
    return (char *)buf2;
  }
}

static char* config_register_serialized_entry (config_values_t *this, const char *value) {
  /*
      fields serialized :
          int              type;
          int              range_min;
          int              range_max;
          int              exp_level;
          int              num_default;
          int              num_value;
          char            *key;
          char            *str_default;
          char            *description;
          char            *help;
          char           **enum_values;
  */

  uint8_t *output;

  if (!value)
    return NULL;
  output = malloc ((strlen (value) * 3 + 3) / 4 + 1);

  if (output) do {
    char    *enum_values[257];
    int      type, range_min, range_max, exp_level, num_default, num_value;
    char    *key, *str_default, *description, *help;
    uint8_t *p;
    uint32_t left, value_count, i;

    left = xine_base64_decode (value, output);
    /* we need at least 7 ints and 4 string lengths */
    if (left < 11 * 4)
      break;
    left -= 11 * 4;

    p = output;
    type        = (int32_t)_X_LE_32 (p); p += 4;
    range_min   = (int32_t)_X_LE_32 (p); p += 4;
    range_max   = (int32_t)_X_LE_32 (p); p += 4;
    exp_level   = (int32_t)_X_LE_32 (p); p += 4;
    num_default = (int32_t)_X_LE_32 (p); p += 4;
    num_value   = (int32_t)_X_LE_32 (p); p += 4;

#define get_string(s) { \
  uint32_t len = _X_LE_32 (p); p[0] = 0; p += 4; \
  if (len > left) \
    break; \
  left -= len; \
  s = (char *)p; p += len; \
}

    get_string (key);
    get_string (str_default);
    get_string (description);
    get_string (help);

    value_count = _X_LE_32 (p); p[0] = 0; p += 4;
    if (value_count > 256)
      break;
    if (left < value_count * 4)
      break;
    left -= value_count * 4;
    for (i = 0; i < value_count; i++) {
      get_string (enum_values[i]);
    }
    if (i < value_count)
      break;
    /* yes we have that byte. */
    p[0] = 0;
    enum_values[value_count] = NULL;

#undef get_string

    if (exp_level == FIND_ONLY)
      exp_level = 0;
#ifdef LOG
    printf ("config entry deserialization:\n");
    printf ("  key        : %s\n", key);
    printf ("  type       : %d\n", type);
    printf ("  exp_level  : %d\n", exp_level);
    printf ("  num_default: %d\n", num_default);
    printf ("  num_value  : %d\n", num_value);
    printf ("  str_default: %s\n", str_default);
    printf ("  range_min  : %d\n", range_min);
    printf ("  range_max  : %d\n", range_max);
    printf ("  description: %s\n", description);
    printf ("  help       : %s\n", help);
    printf ("  enum       : %d values\n", value_count);
    for (i = 0; i < value_count; i++)
      printf ("    enum[%2d]: %s\n", i, enum_values[i]);
    printf ("\n");
#endif

    switch (type) {
      case XINE_CONFIG_TYPE_STRING:
        switch (num_value) {
          case 0:
            this->register_string (this, key, str_default, description, help, exp_level, NULL, NULL);
            break;
          default:
            this->register_filename (this, key, str_default, num_value, description, help, exp_level, NULL, NULL);
            break;
        }
        break;
      case XINE_CONFIG_TYPE_RANGE:
        this->register_range (this, key, num_default, range_min, range_max, description, help, exp_level, NULL, NULL);
        break;
      case XINE_CONFIG_TYPE_ENUM:
        this->register_enum (this, key, num_default, enum_values, description, help, exp_level, NULL, NULL);
        break;
      case XINE_CONFIG_TYPE_NUM:
        this->register_num (this, key, num_default, description, help, exp_level, NULL, NULL);
        break;
      case XINE_CONFIG_TYPE_BOOL:
        this->register_bool (this, key, num_default, description, help, exp_level, NULL, NULL);
        break;
      default: ;
    }

    key = strdup (key);
    free (output);
    return key;
  } while (0);

  /* cleanup */
  free (output);
  return NULL;
}

static int _config_fat_entry_cmp (void *a, void *b) {
  char buf1[MAX_SORT_KEY + 32], buf2[MAX_SORT_KEY + 32], *fs1, *fs2;
  fat_cfg_entry_t *d = (fat_cfg_entry_t *)a;
  fat_cfg_entry_t *e = (fat_cfg_entry_t *)b;

  if (_config_is_fat_entry (d)) {
    fs1 = d->internal_key;
  } else {
    fs1 = xine_fast_string_init (buf1, sizeof (buf1));
    xine_fast_string_set (fs1, NULL, config_make_sort_key (fs1, d->entry.key, d->entry.exp_level));
  }
  if (_config_is_fat_entry (e)) {
    fs2 = e->internal_key;
  } else {
    fs2 = xine_fast_string_init (buf2, sizeof (buf2));
    xine_fast_string_set (fs2, NULL, config_make_sort_key (fs2, e->entry.key, e->entry.exp_level));
  }
  return xine_fast_string_cmp (fs1, fs2);
}

config_values_t *_x_config_init (void) {

#ifdef HAVE_IRIXAL
  volatile /* is this a (old, 2.91.66) irix gcc bug?!? */
#endif
  fat_config_values_t *this;
  pthread_mutexattr_t attr;

  this = calloc (1, sizeof (*this));
  if (!this) {
    fprintf (stderr, "configfile: could not allocate config object\n");
    return NULL;
  }

#ifndef HAVE_ZERO_SAFE_MEM
  this->config.first           = NULL;
  this->config.last            = NULL;
  this->config.current_version = 0;
  this->config.xine            = NULL;
#endif

  /* warning: config_lock is a recursive mutex. it must NOT be
   * used with neither pthread_cond_wait() or pthread_cond_timedwait()
   */
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init (&this->config.config_lock, &attr);
  pthread_mutexattr_destroy(&attr);

  this->config.register_string           = config_register_string;
  this->config.register_filename         = config_register_filename;
  this->config.register_range            = config_register_range;
  this->config.register_enum             = config_register_enum;
  this->config.register_num              = config_register_num;
  this->config.register_bool             = config_register_bool;
  this->config.register_serialized_entry = config_register_serialized_entry;
  this->config.update_num                = config_update_num;
  this->config.update_string             = config_update_string;
  this->config.parse_enum                = config_parse_enum;
  this->config.lookup_entry              = config_lookup_entry;
  this->config.unregister_callback       = config_unregister_cb;
  this->config.dispose                   = config_dispose;
  this->config.set_new_entry_callback    = config_set_new_entry_callback;
  this->config.unset_new_entry_callback  = config_unset_new_entry_callback;
  this->config.get_serialized_entry      = config_get_serialized_entry;
  this->config.unregister_callbacks      = config_unregister_callbacks;
  this->config.lookup_num                = config_lookup_num;
  this->config.lookup_string             = config_lookup_string;
  this->config.free_string               = config_free_string;

  this->key_index = xine_sarray_new (1024, _config_fat_entry_cmp);
  xine_sarray_set_mode (this->key_index, XINE_SARRAY_MODE_UNIQUE);

  return &this->config;
}

int _x_config_change_opt(config_values_t *config, const char *opt) {
  cfg_entry_t *entry;
  int          handled = 0;
  char        *key, *value;

  /* If the configuration is missing, return now rather than trying
   * to dereference it and then check it. */
  if ( ! config || ! opt ) return -1;

  if ((entry = config->lookup_entry(config, "misc.implicit_config")) &&
      entry->type == XINE_CONFIG_TYPE_BOOL) {
    if (!entry->num_value)
      /* changing config entries implicitly is denied */
      return -1;
  } else
    /* someone messed with the config entry */
    return -1;

  if ( *opt == '\0' ) return 0;

  key = strdup(opt);
  if ( !key ) return 0;

  value = strrchr(key, ':');
  if ( !value || *value == '\0' ) {
    free(key);
    return 0;
  }

  *value++ = '\0';

  handled = -1;
  entry = config_lookup_entry_safe (config, key);
  if (entry) {
    if (entry->exp_level >= XINE_CONFIG_SECURITY) {
      if (config->xine)
        xprintf (config->xine, XINE_VERBOSITY_LOG, _("configfile: entry '%s' mustn't be modified from MRL\n"), key);
    } else {
      switch (entry->type) {
        case XINE_CONFIG_TYPE_STRING:
        case XINE_CONFIG_TYPE_RANGE:
        case XINE_CONFIG_TYPE_ENUM:
        case XINE_CONFIG_TYPE_NUM:
        case XINE_CONFIG_TYPE_BOOL:
        case XINE_CONFIG_TYPE_UNKNOWN:
          config_update_string_e (entry, value);
          handled = 1;
          break;
        default: ;
      }
    }
  }
  pthread_mutex_unlock (&config->config_lock);
  free(key);
  return handled;
}
