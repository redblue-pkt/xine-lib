/*
 * Copyright (C) 2019 the xine project
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
 */

typedef struct {
  uint32_t video_width;
  uint32_t video_height;
  uint32_t bitrate;
  char     lang[4];
} multirate_pref_t;

static const char * const multirate_video_size_labels[] = {
  "Audio only", "Small", "SD", "HD", "Full HD", "4K", NULL
};

static void multirate_set_video_size (multirate_pref_t *pref, int n) {
  static const uint32_t w[] = { 0, 360, 720, 1280, 1920, 3840 };
  static const uint32_t h[] = { 0, 240, 576,  720, 1080, 2160 };
  if ((n >= 0) && (n < (int)(sizeof (w) / sizeof (w[0])))) {
    pref->video_width = w[n];
    pref->video_height = h[n];
  }
}

static void multirate_cb_video_size (void *pref_gen, xine_cfg_entry_t *entry) {
  multirate_pref_t *pref = (multirate_pref_t *)pref_gen;
  multirate_set_video_size (pref, entry->num_value);
}

static void multirate_set_lang (multirate_pref_t *pref, const char *lang) {
  if (lang)
    strlcpy (pref->lang, lang, sizeof (pref->lang));
}

static void multirate_cb_lang (void *pref_gen, xine_cfg_entry_t *entry) {
  multirate_pref_t *pref = (multirate_pref_t *)pref_gen;
  multirate_set_lang (pref, entry->str_value);
}

static void multirate_cb_bitrate (void *pref_gen, xine_cfg_entry_t *entry) {
  multirate_pref_t *pref = (multirate_pref_t *)pref_gen;
  pref->bitrate = entry->num_value;
}

static void multirate_pref_get (config_values_t *config, multirate_pref_t *pref) {
  multirate_set_video_size (pref, config->register_enum (config,
    "media.multirate.preferred_video_size", 3,
    (char **)multirate_video_size_labels,
    _("Preferred video size"),
    _("What size of video to play when there are multiple versions."),
    10,
    multirate_cb_video_size,
    pref));
  multirate_set_lang (pref, config->register_string (config,
    "media.multirate.preferred_language", "",
    _("Preferred language"),
    _("What language to play when there are multiple versions."),
    10,
    multirate_cb_lang,
    pref));
  pref->bitrate = config->register_num (config,
    "media.multirate.preferred_bitrate", 2000000,
    _("Preferred bitrate"),
    _("What bitrate to play when there are multiple versions of same size."),
    10,
    multirate_cb_bitrate,
    pref);
}

static int multirate_autoselect (multirate_pref_t *pref, multirate_pref_t *list, int list_size) {
  multirate_pref_t *item;
  int w, h, n, best_n, best_s, best_b;
  if (list_size <= 0)
    return -1;
  if (list_size == 1)
    return 0;
  w = pref->video_width > 0 ? pref->video_width : 1;
  h = pref->video_height > 0 ? pref->video_height : 1;
  best_n = 0;
  best_s = 0x7fffffff;
  best_b = 0x7fffffff;
  item = list;
  for (n = 0; n < list_size; n++) {
    int dw, dh, s, b;
    b = item->bitrate - pref->bitrate;
    b = b < 0 ? -b : b;
    dw = item->video_width - w;
    dh = item->video_height - h;
    dw = dw < 0 ? -dw : dw;
    dh = dh < 0 ? -dh : dh;
    s = (dw << 10) / w + (dh << 10) / h;
    if (s < best_s) {
      best_b = b;
      best_n = n;
      best_s = s;
    } else if (s == best_s) {
      if (b < best_b) {
        best_n = n;
        best_b = b;
      }
    }
    item++;
  }
  return best_n;
}
