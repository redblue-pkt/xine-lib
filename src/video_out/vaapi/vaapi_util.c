/*
 * Copyright (C) 2012 Edgar Hucek <gimli|@dark-green.com>
 * Copyright (C) 2012-2022 xine developers
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
 * vaapi_util.c, VAAPI video extension interface for xine
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vaapi_util.h"

#include <stdlib.h>
#include <pthread.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>

#include <va/va.h>

#include "xine_va_display.h"

#if defined(LOG) || defined(DEBUG)
static const char *_x_va_string_of_VAImageFormat(VAImageFormat *imgfmt)
{
  static char str[5];
  str[0] = imgfmt->fourcc;
  str[1] = imgfmt->fourcc >> 8;
  str[2] = imgfmt->fourcc >> 16;
  str[3] = imgfmt->fourcc >> 24;
  str[4] = '\0';
  return str;
}
#endif

const char *_x_va_profile_to_string(VAProfile profile)
{
  switch(profile) {
#define PROFILE(profile) \
    case VAProfile##profile: return "VAProfile" #profile
      PROFILE(MPEG2Simple);
      PROFILE(MPEG2Main);
      PROFILE(MPEG4Simple);
      PROFILE(MPEG4AdvancedSimple);
      PROFILE(MPEG4Main);
      PROFILE(H264Main);
      PROFILE(H264High);
      PROFILE(VC1Simple);
      PROFILE(VC1Main);
      PROFILE(VC1Advanced);
#if VA_CHECK_VERSION(0, 37, 0)
      PROFILE(HEVCMain);
      PROFILE(HEVCMain10);
#endif
#undef PROFILE
    default: break;
  }
  return "<unknown>";
}

const char *_x_va_entrypoint_to_string(VAEntrypoint entrypoint)
{
  switch(entrypoint)
  {
#define ENTRYPOINT(entrypoint) \
    case VAEntrypoint##entrypoint: return "VAEntrypoint" #entrypoint
      ENTRYPOINT(VLD);
      ENTRYPOINT(IZZ);
      ENTRYPOINT(IDCT);
      ENTRYPOINT(MoComp);
      ENTRYPOINT(Deblocking);
#undef ENTRYPOINT
    default: break;
  }
  return "<unknown>";
}

int _x_va_check_status(vaapi_context_impl_t *this, VAStatus vaStatus, const char *msg)
{
  if (vaStatus != VA_STATUS_SUCCESS) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " Error : %s: %s\n", msg, vaErrorStr(vaStatus));
    return 0;
  }
  return 1;
}

void _x_va_reset_va_context(ff_vaapi_context_t *va_context)
{
  int i;

  va_context->va_config_id              = VA_INVALID_ID;
  va_context->va_context_id             = VA_INVALID_ID;
  va_context->valid_context             = 0;

  for(i = 0; i < RENDER_SURFACES; i++) {
    ff_vaapi_surface_t *va_surface      = &va_context->va_render_surfaces[i];

    va_surface->index                   = i;
    va_surface->status                  = SURFACE_FREE;
    va_surface->va_surface_id           = VA_INVALID_SURFACE;

    va_context->va_surface_ids[i]       = VA_INVALID_SURFACE;
  }
}

VAStatus _x_va_terminate(ff_vaapi_context_t *va_context)
{
  VAStatus vaStatus = VA_STATUS_SUCCESS;

  _x_freep(&va_context->va_image_formats);
  va_context->va_num_image_formats  = 0;

  if (va_context->va_display) {
    vaStatus = vaTerminate(va_context->va_display);
    va_context->va_display = NULL;
  }

  return vaStatus;
}

void _x_va_free(vaapi_context_impl_t **p_va_context)
{
  if (*p_va_context) {
    vaapi_context_impl_t *va_context = *p_va_context;
    VAStatus vaStatus;

    if (va_context->va_display_plugin)
      va_context->va_display_plugin->dispose(&va_context->va_display_plugin);
    va_context->c.va_display = NULL;

    vaStatus = _x_va_terminate(&va_context->c);
    _x_va_check_status(va_context, vaStatus, "vaTerminate()");

    pthread_mutex_destroy(&va_context->surfaces_lock);
    pthread_mutex_destroy(&va_context->ctx_lock);

    _x_freep(p_va_context);
  }
}

VAStatus _x_va_initialize(ff_vaapi_context_t *va_context)
{
  VAStatus vaStatus;
  int      maj, min;
  int      fmt_count = 0;

  if (!va_context->va_display) {
    return VA_STATUS_ERROR_INVALID_DISPLAY;
  }

  vaStatus = vaInitialize(va_context->va_display, &maj, &min);
  if (vaStatus != VA_STATUS_SUCCESS) {
    goto fail;
  }

  lprintf("libva: %d.%d\n", maj, min);

  fmt_count = vaMaxNumImageFormats(va_context->va_display);
  va_context->va_image_formats = calloc(fmt_count, sizeof(*va_context->va_image_formats));
  if (!va_context->va_image_formats) {
    goto fail;
  }

  vaStatus = vaQueryImageFormats(va_context->va_display, va_context->va_image_formats, &va_context->va_num_image_formats);
  if (vaStatus != VA_STATUS_SUCCESS) {
    goto fail;
  }

  return vaStatus;

fail:
  _x_va_terminate(va_context);
  return vaStatus;
}

vaapi_context_impl_t *_x_va_new(xine_t *xine, int visual_type, const void *visual, int opengl_render)
{
  vaapi_context_impl_t *va_context;
  xine_va_display_t *va_display;
  const char *p, *vendor;
  VAStatus vaStatus;
  size_t   i;

  va_display = _x_va_display_open(xine, visual_type, visual, opengl_render ? XINE_VA_DISPLAY_GLX : 0);
  if (!va_display)
    return NULL;

  va_context = calloc(1, sizeof(*va_context));
  if (!va_context) {
    va_display->dispose(&va_display);
    return NULL;
  }

  va_context->xine = xine;
  va_context->va_display_plugin = va_display;
  va_context->c.va_render_surfaces  = va_context->va_render_surfaces_storage;
  va_context->c.va_surface_ids      = va_context->va_surface_ids_storage;
  va_context->c.va_display          = va_context->va_display_plugin->va_display;

  _x_va_reset_va_context(&va_context->c);

  pthread_mutex_init(&va_context->surfaces_lock, NULL);
  pthread_mutex_init(&va_context->ctx_lock, NULL);

  vaStatus = _x_va_initialize(&va_context->c);
  if (!_x_va_check_status(va_context, vaStatus, "vaInitialize()")) {
    _x_va_free(&va_context);
    return NULL;
  }

  va_context->query_va_status = 1;
  va_context->va_head         = 0;

  vendor = vaQueryVendorString(va_context->c.va_display);
  xprintf(va_context->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_open: Vendor : %s\n", vendor);

  for (p = vendor, i = strlen (vendor); i > 0; i--, p++) {
    if (strncmp(p, "VDPAU", strlen("VDPAU")) == 0) {
      xprintf(va_context->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_open: Enable Splitted-Desktop Systems VDPAU-VIDEO workarounds.\n");
      va_context->query_va_status = 0;
      break;
    }
  }

  return va_context;
}


void _x_va_destroy_image(vaapi_context_impl_t *va_context, VAImage *va_image)
{
  VAStatus              vaStatus;

  if (va_image->image_id != VA_INVALID_ID) {
    lprintf("vaapi_destroy_image 0x%08x\n", va_image->image_id);
    vaStatus = vaDestroyImage(va_context->c.va_display, va_image->image_id);
    _x_va_check_status(va_context, vaStatus, "vaDestroyImage()");
  }
  va_image->image_id      = VA_INVALID_ID;
  va_image->width         = 0;
  va_image->height        = 0;
}

VAStatus _x_va_create_image(vaapi_context_impl_t *va_context, VASurfaceID va_surface_id, VAImage *va_image, int width, int height, int clear, int *is_bound)
{
  int i = 0;
  VAStatus vaStatus;

  if (!va_context->c.valid_context || va_context->c.va_image_formats == NULL || va_context->c.va_num_image_formats == 0)
    return VA_STATUS_ERROR_UNKNOWN;

  *is_bound = 0;

  vaStatus = vaDeriveImage(va_context->c.va_display, va_surface_id, va_image);
  if (vaStatus == VA_STATUS_SUCCESS) {
    if (va_image->image_id != VA_INVALID_ID && va_image->buf != VA_INVALID_ID) {
      *is_bound = 1;
    }
  }

  if (!*is_bound) {
    for (i = 0; i < va_context->c.va_num_image_formats; i++) {
      if (va_context->c.va_image_formats[i].fourcc == VA_FOURCC( 'Y', 'V', '1', '2' ) ||
          va_context->c.va_image_formats[i].fourcc == VA_FOURCC( 'I', '4', '2', '0' ) /*||
          va_context->c.va_image_formats[i].fourcc == VA_FOURCC( 'N', 'V', '1', '2' ) */) {
        vaStatus = vaCreateImage( va_context->c.va_display, &va_context->c.va_image_formats[i], width, height, va_image );
        if (!_x_va_check_status(va_context, vaStatus, "vaCreateImage()"))
          goto error;
        break;
      }
    }
  }

  void *p_base = NULL;

  vaStatus = vaMapBuffer( va_context->c.va_display, va_image->buf, &p_base );
  if (!_x_va_check_status(va_context, vaStatus, "vaMapBuffer()"))
    goto error;

  if (clear) {
    if (va_image->format.fourcc == VA_FOURCC( 'Y', 'V', '1', '2' ) ||
        va_image->format.fourcc == VA_FOURCC( 'I', '4', '2', '0' )) {
      memset((uint8_t*)p_base + va_image->offsets[0],   0, va_image->pitches[0] * va_image->height);
      memset((uint8_t*)p_base + va_image->offsets[1], 128, va_image->pitches[1] * (va_image->height/2));
      memset((uint8_t*)p_base + va_image->offsets[2], 128, va_image->pitches[2] * (va_image->height/2));
    } else if (va_image->format.fourcc == VA_FOURCC( 'N', 'V', '1', '2' ) ) {
      memset((uint8_t*)p_base + va_image->offsets[0],   0, va_image->pitches[0] * va_image->height);
      memset((uint8_t*)p_base + va_image->offsets[1], 128, va_image->pitches[1] * (va_image->height/2));
    }
  }

  vaStatus = vaUnmapBuffer( va_context->c.va_display, va_image->buf );
  _x_va_check_status(va_context, vaStatus, "vaUnmapBuffer()");

  lprintf("_x_va_create_image 0x%08x width %d height %d format %s\n", va_image->image_id, va_image->width, va_image->height,
      _x_va_string_of_VAImageFormat(&va_image->format));

  return VA_STATUS_SUCCESS;

error:
  /* house keeping */
  _x_va_destroy_image(va_context, va_image);
  return VA_STATUS_ERROR_UNKNOWN;
}

static VAStatus _x_va_destroy_render_surfaces(vaapi_context_impl_t *va_context)
{
  int                 i;
  VAStatus            vaStatus;

  pthread_mutex_lock(&va_context->surfaces_lock);

  for (i = 0; i < RENDER_SURFACES; i++) {
    if (va_context->c.va_surface_ids[i] != VA_INVALID_SURFACE) {
      vaStatus = vaSyncSurface(va_context->c.va_display, va_context->c.va_surface_ids[i]);
      _x_va_check_status(va_context, vaStatus, "vaSyncSurface()");
      vaStatus = vaDestroySurfaces(va_context->c.va_display, &va_context->c.va_surface_ids[i], 1);
      _x_va_check_status(va_context, vaStatus, "vaDestroySurfaces()");
      va_context->c.va_surface_ids[i] = VA_INVALID_SURFACE;

      ff_vaapi_surface_t *va_surface  = &va_context->c.va_render_surfaces[i];
      va_surface->index               = i;
      va_surface->status              = SURFACE_FREE;
      va_surface->va_surface_id       = va_context->c.va_surface_ids[i];
    }
  }

  pthread_mutex_unlock(&va_context->surfaces_lock);

  return VA_STATUS_SUCCESS;
}

void _x_va_close(vaapi_context_impl_t *va_context)
{
  VAStatus vaStatus;

  pthread_mutex_lock(&va_context->ctx_lock);

  if (va_context->c.va_context_id != VA_INVALID_ID) {
    vaStatus = vaDestroyContext(va_context->c.va_display, va_context->c.va_context_id);
    _x_va_check_status(va_context, vaStatus, "vaDestroyContext()");
    va_context->c.va_context_id = VA_INVALID_ID;
  }

  _x_va_destroy_render_surfaces(va_context);

  if (va_context->c.va_config_id != VA_INVALID_ID) {
    vaStatus = vaDestroyConfig(va_context->c.va_display, va_context->c.va_config_id);
    _x_va_check_status(va_context, vaStatus, "vaDestroyConfig()");
    va_context->c.va_config_id = VA_INVALID_ID;
  }

  va_context->c.valid_context = 0;

  pthread_mutex_unlock(&va_context->ctx_lock);
}

VAStatus _x_va_init(vaapi_context_impl_t *va_context, int va_profile, int width, int height)
{
  VAConfigAttrib va_attrib;
  VAStatus       vaStatus;
  size_t         i;

  _x_va_close(va_context);

  pthread_mutex_lock(&va_context->ctx_lock);

  va_context->c.width = width;
  va_context->c.height = height;

  xprintf(va_context->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : Context width %d height %d\n", va_context->c.width, va_context->c.height);

  /* allocate decoding surfaces */
  unsigned rt_format = VA_RT_FORMAT_YUV420;
#if VA_CHECK_VERSION(0, 37, 0) && defined (VA_RT_FORMAT_YUV420_10BPP)
  if (va_profile == VAProfileHEVCMain10) {
    rt_format = VA_RT_FORMAT_YUV420_10BPP;
  }
#endif
  vaStatus = vaCreateSurfaces(va_context->c.va_display, rt_format, va_context->c.width, va_context->c.height, va_context->c.va_surface_ids, RENDER_SURFACES, NULL, 0);
  if (!_x_va_check_status(va_context, vaStatus, "vaCreateSurfaces()"))
    goto error;

  /* hardware decoding needs more setup */
  if (va_profile >= 0) {
    xprintf(va_context->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : Profile: %d (%s) Entrypoint %d (%s) Surfaces %d\n",
            va_profile, _x_va_profile_to_string(va_profile), VAEntrypointVLD, _x_va_entrypoint_to_string(VAEntrypointVLD), RENDER_SURFACES);

    memset (&va_attrib, 0, sizeof(va_attrib));
    va_attrib.type = VAConfigAttribRTFormat;

    vaStatus = vaGetConfigAttributes(va_context->c.va_display, va_profile, VAEntrypointVLD, &va_attrib, 1);
    if (!_x_va_check_status(va_context, vaStatus, "vaGetConfigAttributes()"))
      goto error;

    if ((va_attrib.value & VA_RT_FORMAT_YUV420) == 0)
      goto error;

    vaStatus = vaCreateConfig(va_context->c.va_display, va_profile, VAEntrypointVLD, &va_attrib, 1, &va_context->c.va_config_id);
    if (!_x_va_check_status(va_context, vaStatus, "vaCreateConfig()")) {
      va_context->c.va_config_id = VA_INVALID_ID;
      goto error;
    }

    vaStatus = vaCreateContext(va_context->c.va_display, va_context->c.va_config_id, va_context->c.width, va_context->c.height,
                               VA_PROGRESSIVE, va_context->c.va_surface_ids, RENDER_SURFACES, &va_context->c.va_context_id);
    if (!_x_va_check_status(va_context, vaStatus, "vaCreateContext()")) {
      va_context->c.va_context_id = VA_INVALID_ID;
      goto error;
    }
  }

  pthread_mutex_lock(&va_context->surfaces_lock);

  /* assign surfaces */
  for (i = 0; i < RENDER_SURFACES; i++) {
    ff_vaapi_surface_t *va_surface  = &va_context->c.va_render_surfaces[i];
    va_surface->index               = i;
    va_surface->status              = SURFACE_FREE;
    va_surface->va_surface_id       = va_context->c.va_surface_ids[i];
  }
  va_context->va_head = 0;

  pthread_mutex_unlock(&va_context->surfaces_lock);

  /* unbind frames from surfaces */
  for (i = 0; i < RENDER_SURFACES; i++) {
    if (va_context->frames[i]) {
      vaapi_accel_t *accel = va_context->frames[i]->accel_data;
      if (!accel->f->render_vaapi_surface) {
        _x_assert(accel->index == i);
      } else {
        accel->index = RENDER_SURFACES;
      }
    }
  }

  va_context->c.valid_context = 1;

  pthread_mutex_unlock(&va_context->ctx_lock);

  return VA_STATUS_SUCCESS;

 error:
  pthread_mutex_unlock(&va_context->ctx_lock);
  xprintf(va_context->xine, XINE_VERBOSITY_LOG, LOG_MODULE "Error initializing VAAPI decoding\n");
  _x_va_close(va_context);
  return VA_STATUS_ERROR_UNKNOWN;
}

static int _x_va_has_profile(VAProfile *va_profiles, int va_num_profiles, VAProfile profile)
{
  int i;
  for (i = 0; i < va_num_profiles; i++) {
    if (va_profiles[i] == profile)
      return 1;
  }
  return 0;
}

int _x_va_profile_from_imgfmt(vaapi_context_impl_t *va_context, unsigned format)
{
  VAStatus            vaStatus;
  int                 profile     = -1;
  int                 i;
  int                 va_num_profiles;
  int                 max_profiles;
  VAProfile           *va_profiles = NULL;

  _x_assert(va_context->c.va_display);

  max_profiles = vaMaxNumProfiles(va_context->c.va_display);
  va_profiles = calloc(max_profiles, sizeof(*va_profiles));
  if (!va_profiles)
    goto out;

  vaStatus = vaQueryConfigProfiles(va_context->c.va_display, va_profiles, &va_num_profiles);
  if(!_x_va_check_status(va_context, vaStatus, "vaQueryConfigProfiles()"))
    goto out;

  xprintf(va_context->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE " VAAPI Supported Profiles :\n");
  for (i = 0; i < va_num_profiles; i++) {
    xprintf(va_context->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE "    %s\n", _x_va_profile_to_string(va_profiles[i]));
  }

  static const int mpeg2_profiles[] = { VAProfileMPEG2Main, VAProfileMPEG2Simple, -1 };
  static const int mpeg4_profiles[] = { VAProfileMPEG4Main, VAProfileMPEG4AdvancedSimple, VAProfileMPEG4Simple, -1 };
  static const int h264_profiles[]  = { VAProfileH264High, VAProfileH264Main, -1 };
#if VA_CHECK_VERSION(0, 37, 0)
  static const int hevc_profiles[]  = { VAProfileHEVCMain, VAProfileHEVCMain10, -1 };
  static const int hevc_profiles10[]  = { VAProfileHEVCMain10, -1 };
#endif
  static const int wmv3_profiles[]  = { VAProfileVC1Main, VAProfileVC1Simple, -1 };
  static const int vc1_profiles[]   = { VAProfileVC1Advanced, -1 };

  const int *profiles = NULL;
  switch (IMGFMT_VAAPI_CODEC(format)) 
  {
    case IMGFMT_VAAPI_CODEC_MPEG2:
      profiles = mpeg2_profiles;
      break;
    case IMGFMT_VAAPI_CODEC_MPEG4:
      profiles = mpeg4_profiles;
      break;
    case IMGFMT_VAAPI_CODEC_H264:
      profiles = h264_profiles;
      break;
#if VA_CHECK_VERSION(0, 37, 0)
    case IMGFMT_VAAPI_CODEC_HEVC:
      switch (format) {
        case IMGFMT_VAAPI_HEVC_MAIN10:
          profiles = hevc_profiles10;
          break;
        case IMGFMT_VAAPI_HEVC:
        default:
          profiles = hevc_profiles;
          break;
      }
      break;
#endif
    case IMGFMT_VAAPI_CODEC_VC1:
      switch (format) {
        case IMGFMT_VAAPI_WMV3:
          profiles = wmv3_profiles;
          break;
        case IMGFMT_VAAPI_VC1:
            profiles = vc1_profiles;
            break;
      }
      break;
  }

  if (profiles) {
    int i;
    for (i = 0; profiles[i] != -1; i++) {
      if (_x_va_has_profile(va_profiles, va_num_profiles, profiles[i])) {
        profile = profiles[i];
        xprintf(va_context->xine, XINE_VERBOSITY_LOG, LOG_MODULE " VAAPI Profile %s supported by your hardware\n", _x_va_profile_to_string(profiles[i]));
        break;
      }
    }
  }

  if (profile < 0)
    xprintf(va_context->xine, XINE_VERBOSITY_LOG, LOG_MODULE " VAAPI Profile for video format %d not supported by hardware\n", format);

out:
  free(va_profiles);
  return profile;
}

#ifdef DEBUG_SURFACE
# define DBG_SURFACE printf
#else
# define DBG_SURFACE(...) do { } while (0)
#endif

ff_vaapi_surface_t *_x_va_alloc_surface(vaapi_context_impl_t *va_context)
{
  ff_vaapi_surface_t   *va_surface = NULL;
  VAStatus              vaStatus;

  lprintf("get_vaapi_surface\n");

  pthread_mutex_lock(&va_context->surfaces_lock);

  /* Get next VAAPI surface marked as SURFACE_FREE */
  while (1) {
    va_surface = &va_context->c.va_render_surfaces[va_context->va_head];
    va_context->va_head = (va_context->va_head + 1) % RENDER_SURFACES;

    if (va_surface->status == SURFACE_FREE) {

      VASurfaceStatus surf_status = 0;

      if (va_context->query_va_status) {
        vaStatus = vaQuerySurfaceStatus(va_context->c.va_display, va_surface->va_surface_id, &surf_status);
        _x_va_check_status(va_context, vaStatus, "vaQuerySurfaceStatus()");
      } else {
        surf_status = VASurfaceReady;
      }

      if (surf_status == VASurfaceReady) {
        va_surface->status = SURFACE_ALOC;
        DBG_SURFACE("alloc_vaapi_surface 0x%08x\n", va_surface->va_surface_id);
        break;
      }
      DBG_SURFACE("alloc_vaapi_surface busy\n");
    }
    DBG_SURFACE("alloc_vaapi_surface miss\n");
  }

  pthread_mutex_unlock(&va_context->surfaces_lock);

  return va_surface;
}

void _x_va_render_surface(vaapi_context_impl_t *va_context, ff_vaapi_surface_t *va_surface)
{
  DBG_SURFACE("render_vaapi_surface 0x%08x\n", va_surface->va_surface_id);
  _x_assert(va_surface->status == SURFACE_ALOC);

  pthread_mutex_lock(&va_context->surfaces_lock);
  va_surface->status = SURFACE_RENDER;
  pthread_mutex_unlock(&va_context->surfaces_lock);
}

void _x_va_release_surface(vaapi_context_impl_t *va_context, ff_vaapi_surface_t *va_surface)
{
  _x_assert(va_surface->status == SURFACE_ALOC ||
            va_surface->status == SURFACE_RENDER ||
            va_surface->status == SURFACE_RELEASE);

  pthread_mutex_lock(&va_context->surfaces_lock);

  if (va_surface->status == SURFACE_RENDER) {
    va_surface->status = SURFACE_RENDER_RELEASE;
    DBG_SURFACE("release_surface 0x%08x -> RENDER_RELEASE\n", va_surface->va_surface_id);
  } else if (va_surface->status != SURFACE_RENDER_RELEASE) {
    va_surface->status = SURFACE_FREE;
    DBG_SURFACE("release_surface 0x%08x -> FREE\n", va_surface->va_surface_id);
  }

  pthread_mutex_unlock(&va_context->surfaces_lock);
}

void _x_va_surface_displayed(vaapi_context_impl_t *va_context, ff_vaapi_surface_t *va_surface)
{
  _x_assert(va_surface->status == SURFACE_RENDER ||
            va_surface->status == SURFACE_RENDER_RELEASE);

  pthread_mutex_lock(&va_context->surfaces_lock);

  if (va_surface->status == SURFACE_RENDER_RELEASE) {
    va_surface->status = SURFACE_FREE;
    DBG_SURFACE("release_surface 0x%08x -> FREE\n", va_surface->va_surface_id, vo_frame);
  } else if (va_surface->status == SURFACE_RENDER) {
    va_surface->status = SURFACE_RELEASE;
    DBG_SURFACE("release_surface 0x%08x -> RELEASE\n", va_surface->va_surface_id, vo_frame);
  } else {
    DBG_SURFACE("release_surface 0x%08x INVALID STATE %d\n",
                va_surface->va_surface_id, vo_frame, va_surface->status);
  }

  pthread_mutex_unlock(&va_context->surfaces_lock);
}
