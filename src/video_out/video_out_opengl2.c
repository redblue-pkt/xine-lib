/*
 * kate: space-indent on; indent-width 2; mixedindent off; indent-mode cstyle; remove-trailing-space on;
 * Copyright (C) 2012-2022 the xine project
 * Copyright (C) 2012 Christophe Thommeret <hftom@free.fr>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 * video_out_opengl2.c, a video output plugin using opengl 2.0
 *
 *
 */

/* #define LOG */
#define LOG_MODULE "video_out_opengl2"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>

#include <xine.h>
#include <xine/video_out.h>
#include <xine/vo_scale.h>
#include <xine/xine_internal.h>
#include <xine/xineutils.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include "opengl/xine_gl.h"

#include "mem_frame.h"
#include "hw_frame.h"
typedef mem_frame_t opengl2_frame_t;

typedef struct {
  int       ovl_w, ovl_h;
  int       ovl_x, ovl_y;
  
  GLuint    tex;
  int       tex_w, tex_h;
  
  int       unscaled;
  int       vid_scale;

  int       extent_width;
  int       extent_height;
} opengl2_overlay_t;

typedef struct {
  int    compiled;
  GLuint shader;
  GLuint program;
} opengl2_program_t;

typedef enum {
  OGL2_TEX_y = 0,
  OGL2_TEX_u,
  OGL2_TEX_v,
  OGL2_TEX_yuv,
  OGL2_TEX_uv,

  OGL2_TEX_HW0,
  OGL2_TEX_HW1,
  OGL2_TEX_HW2,

  OGL2_TEX_LAST
} opengl2_tex_t;

typedef struct {
  GLuint tex[OGL2_TEX_LAST];
  int width;
  int height;
} opengl2_yuvtex_t;

typedef struct {
  vo_driver_t        vo_driver;
  vo_scale_t         sc;

  xine_gl_t         *gl;

  int                texture_float;
  opengl2_program_t  yuv420_program;
  opengl2_program_t  nv12_program;
  opengl2_program_t  yuv422_program;
  opengl2_yuvtex_t   yuvtex;
  GLuint             videoPBO;
  GLuint             overlayPBO;
  GLuint             fbo;
  GLuint             videoTex[2];
  int                last_gui_width;
  int                last_gui_height;

  int                ovl_changed;
  int                ovl_vid_scale;
  int                num_ovls;
  uint32_t           ovls_drawn;
  opengl2_overlay_t  overlays[XINE_VORAW_MAX_OVL];

  float              csc_matrix[3 * 4];
  int                color_standard;
  int                update_csc;
  int                saturation;
  int                contrast;
  int                brightness;
  int                hue;
  struct {
    int              value, changed;
    float            mid, side, corn;
    opengl2_program_t program;
  }                  sharp;
  struct {
    int              flags, changed;
  }                  transform;

  opengl2_program_t  bicubic_pass1_program;
  opengl2_program_t  bicubic_pass2_program;
  GLuint             bicubic_lut_texture;
  GLuint             bicubic_pass1_texture;
  int                bicubic_pass1_texture_width;
  int                bicubic_pass1_texture_height;
  GLuint             bicubic_fbo;
  int                scale_bicubic;
  
  pthread_mutex_t    drawable_lock;
  uint32_t           display_width;
  uint32_t           display_height;

  config_values_t   *config;

  xine_t            *xine;

  int		            zoom_x;
  int		            zoom_y;

  int                cm_state;
  uint8_t            cm_lut[32];

  int                max_video_width;
  int                max_video_height;
  int                max_display_width;
  int                max_display_height;

  vo_accel_generic_t accel;

  int                exit_indx;
  int                exiting;

  /* HW decoding support */
  xine_hwdec_t  *hw;
  xine_glconv_t *glconv;
} opengl2_driver_t;

/* libGL likes to install its own exit handlers.
 * Trying to render after one of them will freeze or crash,
 * so make sure we're last.
 * HAIR RAISING HACK:
 * Passing arguments to an exit handler is supported on SunOS,
 * deprecated in Linux, and impossible anywhere else.
 */

#define MAX_EXIT_TARGETS 8
/* These are process local, right? */
opengl2_driver_t *opengl2_exit_vector[MAX_EXIT_TARGETS] =
  {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

static void opengl2_exit (void) {
  int i;
  for (i = MAX_EXIT_TARGETS - 1; i >= 0; i--) {
    opengl2_driver_t *this = opengl2_exit_vector[i];
    if (this) {
      if (this != (opengl2_driver_t *)1) {
        this->exiting = 1;
        /* wait for last render */
        pthread_mutex_lock (&this->drawable_lock);
        pthread_mutex_unlock (&this->drawable_lock);
      }
      opengl2_exit_vector[i] = NULL;
    }
  }
}

static void opengl2_exit_unregister (opengl2_driver_t *this) {
  int indx = this->exit_indx;
  if (indx == 1)
    opengl2_exit_vector[0] = (opengl2_driver_t *)1;
  else if ((indx > 1) && (indx <= MAX_EXIT_TARGETS))
    opengl2_exit_vector[indx - 1] = NULL;
}

static void opengl2_exit_register (opengl2_driver_t *this) {
  int i;
  if (!opengl2_exit_vector[0]) {
    opengl2_exit_vector[0] = this;
    this->exit_indx = 1;
    atexit (opengl2_exit);
    return;
  }
  if (opengl2_exit_vector[0] == (opengl2_driver_t *)1) {
    opengl2_exit_vector[0] = this;
    this->exit_indx = 1;
    return;
  }
  for (i = 1; i < MAX_EXIT_TARGETS; i++) {
    if (!opengl2_exit_vector[i]) {
      opengl2_exit_vector[i] = this;
      this->exit_indx = i + 1;
      return;
    }
  }
  this->exit_indx = MAX_EXIT_TARGETS + 1;
}

/* !exit_stuff */

/* import common color matrix stuff */
#define CM_LUT
#define CM_HAVE_YCGCO_SUPPORT 1
#define CM_DRIVER_T opengl2_driver_t
#include "color_matrix.c"


typedef struct {
  video_driver_class_t driver_class;
  xine_t              *xine;
  unsigned             visual_type;
} opengl2_class_t;

static void opengl2_accel_lock (vo_frame_t *frame, int lock) {
  (void)frame;
  (void)lock;
}

static const char *bicubic_pass1_frag=
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex, lut;\n"
"uniform float spline;\n"
"void main() {\n"
"    vec2 coord = gl_TexCoord[0].xy;\n"
"    vec2 TexCoord = vec2( floor( coord.x - 0.5 ) + 0.5, coord.y );\n"
"    vec4 wlut = texture2DRect( lut, vec2( ( coord.x - TexCoord.x ) * 1000.0, spline ) );\n"
"    vec4 sum  = texture2DRect( tex, TexCoord + vec2( -1.0, 0.0) ) * wlut[0];\n"
"         sum += texture2DRect( tex, TexCoord )                    * wlut[1];\n"
"         sum += texture2DRect( tex, TexCoord + vec2(  1.0, 0.0) ) * wlut[2];\n"
"         sum += texture2DRect( tex, TexCoord + vec2(  2.0, 0.0) ) * wlut[3];\n"
"    gl_FragColor = sum;\n"
"}\n";

static const char *bicubic_pass2_frag=
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex, lut;\n"
"uniform float spline;\n"
"void main() {\n"
"    vec2 coord = gl_TexCoord[0].xy;\n"
"    vec2 TexCoord = vec2( coord.x, floor( coord.y - 0.5 ) + 0.5 );\n"
"    vec4 wlut = texture2DRect( lut, vec2( ( coord.y - TexCoord.y ) * 1000.0, spline ) );\n"
"    vec4 sum  = texture2DRect( tex, TexCoord + vec2( 0.0, -1.0 ) ) * wlut[0];\n"
"         sum += texture2DRect( tex, TexCoord )                     * wlut[1];\n"
"         sum += texture2DRect( tex, TexCoord + vec2( 0.0,  1.0 ) ) * wlut[2];\n"
"         sum += texture2DRect( tex, TexCoord + vec2( 0.0,  2.0 ) ) * wlut[3];\n"
"    gl_FragColor = sum;\n"
"}\n";

#define LUTWIDTH 1000
#define N_SPLINES 2
#define CATMULLROM_SPLINE   0
#define COS_SPLINE          1

static float compute_cos_spline( float x )
{
    if ( x < 0.0 )
        x = -x;
    return 0.5 * cos( M_PI * x / 2.0 ) + 0.5;
}

static float compute_catmullrom_spline( float x )
{
    if ( x < 0.0 )
        x = -x;
    if ( x < 1.0 )
        return ((9.0 * (x * x * x)) - (15.0 * (x * x)) + 6.0) / 6.0;
    if ( x <= 2.0 )
        return ((-3.0 * (x * x * x)) + (15.0 * (x * x)) - (24.0 * x) + 12.0) / 6.0;
    return 0.0;
}

static int create_lut_texture( opengl2_driver_t *that )
{
  int i = 0;
  float *lut = calloc( sizeof(float) * LUTWIDTH * 4 * N_SPLINES, 1 );
  if ( !lut )
    return 0;

  while ( i < LUTWIDTH ) {
    float t, v1, v2, v3, v4, coefsum;
    t = (float)i / (float)LUTWIDTH;

    v1 = compute_catmullrom_spline( t + 1.0 ); coefsum  = v1;
    v2 = compute_catmullrom_spline( t );       coefsum += v2;
    v3 = compute_catmullrom_spline( t - 1.0 ); coefsum += v3;
    v4 = compute_catmullrom_spline( t - 2.0 ); coefsum += v4;
    lut[i * 4]       = v1 / coefsum;
    lut[(i * 4) + 1] = v2 / coefsum;
    lut[(i * 4) + 2] = v3 / coefsum;
    lut[(i * 4) + 3] = v4 / coefsum;

    lut[(i * 4) + (LUTWIDTH * 4)] = compute_cos_spline( t + 1.0 );
    lut[(i * 4) + (LUTWIDTH * 4) + 1] = compute_cos_spline( t );
    lut[(i * 4) + (LUTWIDTH * 4) + 2] = compute_cos_spline( t - 1.0 );
    lut[(i * 4) + (LUTWIDTH * 4) + 3] = compute_cos_spline( t - 2.0 );

    ++i;
  }

  that->bicubic_lut_texture = 0;
  glGenTextures( 1, &that->bicubic_lut_texture );
  if ( !that->bicubic_lut_texture ) {
    free( lut );
    return 0;
  }

  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, that->bicubic_lut_texture );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA32F, LUTWIDTH, N_SPLINES, 0, GL_RGBA, GL_FLOAT, lut );
  free( lut );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );
  return 1;
}

static const char *blur_sharpen_frag=
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex;\n"
"uniform float mid, side, corn;\n"
"void main() {\n"
"  vec2 pos = gl_TexCoord[0].xy;\n"
"  vec4 c1;\n"
"  c1 =   texture2DRect (tex, pos) * mid\n"
"     +  (texture2DRect (tex, pos + vec2 (-1.0,  0.0))\n"
"       + texture2DRect (tex, pos + vec2 ( 0.0, -1.0))\n"
"       + texture2DRect (tex, pos + vec2 ( 1.0,  0.0))\n"
"       + texture2DRect (tex, pos + vec2 ( 0.0,  1.0))) * side\n"
"     +  (texture2DRect (tex, pos + vec2 (-1.0, -1.0))\n"
"       + texture2DRect (tex, pos + vec2 ( 1.0, -1.0))\n"
"       + texture2DRect (tex, pos + vec2 (-1.0,  1.0))\n"
"       + texture2DRect (tex, pos + vec2 ( 1.0,  1.0))) * corn;\n"
"  gl_FragColor = c1 ;\n"
"}\n";

static const char *yuv420_frag =
"uniform sampler2D texY, texU, texV;\n"
"uniform vec4 r_coefs, g_coefs, b_coefs;\n"
"void main(void) {\n"
"    vec4 rgb;\n"
"    vec4 yuv;\n"
"    vec2 coord = gl_TexCoord[0].xy;\n"
"    yuv.r = texture2D (texY, coord).r;\n"
"    yuv.g = texture2D (texU, coord).r;\n"
"    yuv.b = texture2D (texV, coord).r;\n"
"    yuv.a = 1.0;\n"
"    rgb.r = dot (yuv, r_coefs);\n"
"    rgb.g = dot (yuv, g_coefs);\n"
"    rgb.b = dot (yuv, b_coefs);\n"
"    rgb.a = 1.0;\n"
"    gl_FragColor = rgb;\n"
"}\n";

static const char *nv12_frag =
"uniform sampler2D texY, texUV;\n"
"uniform vec4 r_coefs, g_coefs, b_coefs;\n"
"void main (void) {\n"
"    vec4 rgb;\n"
"    vec4 yuv;\n"
"    vec2 coord = gl_TexCoord[0].xy;\n"
"    yuv.r = texture2D (texY, coord).r;\n"
"    yuv.g = texture2D (texUV, coord).r;\n"
"    yuv.b = texture2D (texUV, coord).a;\n"
"    yuv.a = 1.0;\n"
"    rgb.r = dot( yuv, r_coefs );\n"
"    rgb.g = dot( yuv, g_coefs );\n"
"    rgb.b = dot( yuv, b_coefs );\n"
"    rgb.a = 1.0;\n"
"    gl_FragColor = rgb;\n"
"}\n";

static const char *yuv422_frag =
"uniform sampler2D texYUV;\n"
"uniform vec4 r_coefs, g_coefs, b_coefs;\n"
"uniform vec2 texSize;\n"
"void main(void) {\n"
"    float pixel_x;\n"
"    vec3 rgb;\n"
"    vec4 yuv;\n"
"    vec4 coord = gl_TexCoord[0].xyxx;\n"
"    pixel_x = floor(coord.x * texSize.x);"
"    pixel_x = pixel_x - step(1.0, mod(pixel_x, 2.0));\n"
"    coord.z = (pixel_x + 0.5) / texSize.x;\n"
"    coord.w = (pixel_x + 1.5) / texSize.x;\n"
"    yuv.r = texture2D(texYUV, coord.xy).r;\n"
"    yuv.g = texture2D(texYUV, coord.zy).a;\n"
"    yuv.b = texture2D(texYUV, coord.wy).a;\n"
"    yuv.a = 1.0;\n"
"    rgb.r = dot( yuv, r_coefs );\n"
"    rgb.g = dot( yuv, g_coefs );\n"
"    rgb.b = dot( yuv, b_coefs );\n"
"    gl_FragColor = vec4(rgb, 1.0);\n"
"}\n";

static void load_csc_matrix( GLuint prog, float *cf )
{
    glUniform4f( glGetUniformLocation( prog, "r_coefs" ), cf[0], cf[1], cf[2], cf[3] );
    glUniform4f( glGetUniformLocation( prog, "g_coefs" ), cf[4], cf[5], cf[6], cf[7] );
    glUniform4f( glGetUniformLocation( prog, "b_coefs" ), cf[8], cf[9], cf[10], cf[11] );
}

static int opengl2_build_program( opengl2_driver_t *this, opengl2_program_t *prog, const char **source, const char *name )
{
  xprintf( this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": compiling shader %s\n", name );
  if ( !(prog->shader = glCreateShader( GL_FRAGMENT_SHADER )) )
    return 0;
  if ( !(prog->program = glCreateProgram()) )
    return 0;

  glShaderSource( prog->shader, 1, source, NULL );
  glCompileShader( prog->shader );

  GLint length;
  GLchar *log;
  glGetShaderiv( prog->shader, GL_INFO_LOG_LENGTH, &length );
  log = (GLchar*)malloc( length );
  if ( !log )
    return 0;

  glGetShaderInfoLog( prog->shader, length, &length, log );
  if ( length ) {
    xprintf( this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": Shader %s Compilation Log:\n", name );
    if ( this->xine->verbosity >= XINE_VERBOSITY_DEBUG ) {
      fwrite( log, 1, length, stdout );
      fflush( stdout );
    }
  }
  free( log );

  GLint result;
  glGetShaderiv( prog->shader, GL_COMPILE_STATUS, &result );
  if (result != GL_TRUE) {
    xprintf( this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": compiling shader %s failed\n", name );
    return 0;
  }

  glAttachShader( prog->program, prog->shader );
  glLinkProgram( prog->program );
  glGetProgramiv( prog->program, GL_INFO_LOG_LENGTH, &length );
  log = (GLchar*)malloc( length );
  if ( !log )
    return 0;

  glGetProgramInfoLog( prog->program, length, &length, log );
  if ( length ) {
    xprintf( this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": Shader %s Linking Log:\n", name );
    if ( this->xine->verbosity >= XINE_VERBOSITY_DEBUG ) {
      fwrite( log, 1, length, stdout );
      fwrite( "\n", 1, 1, stdout );
      fflush( stdout );
    }
  }
  free( log );

  glGetProgramiv( prog->program, GL_LINK_STATUS, &result );
  if (result != GL_TRUE) {
    xprintf( this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": linking shader %s failed\n", name );
    return 0;
  }

  prog->compiled = 1;

  return 1;
}

static void opengl2_delete_program( opengl2_program_t *prog )
{
  glDeleteProgram( prog->program );
  glDeleteShader( prog->shader );
}

static void _config_texture(GLenum target, GLuint texture, GLsizei width, GLsizei height,
                            GLenum format, GLenum type, GLenum minmag_filter)
{
  if (texture) {
    glBindTexture(target, texture);
    if (format)
      glTexImage2D(target, 0, format, width, height, 0, format, type, NULL);
    glTexParameterf(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minmag_filter);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, minmag_filter);
  }
}

static int opengl2_check_textures_size( opengl2_driver_t *this_gen, int w, int h )
{
  opengl2_driver_t *this = this_gen;
  opengl2_yuvtex_t *ytex = &this->yuvtex;
  int uvh, i;

  w = (w + 15) & ~15;
  if ( (w == ytex->width) && (h == ytex->height) )
    return 1;

  glDeleteTextures (OGL2_TEX_LAST, ytex->tex);
  glDeleteTextures (2, this->videoTex);

  if ( !this->videoPBO ) {
    glGenBuffers( 1, &this->videoPBO );
    if ( !this->videoPBO )
      return 0;
  }

  if ( !this->fbo ) {
    glGenFramebuffers( 1, &this->fbo );
    if ( !this->fbo )
      return 0;
  }

  glGenTextures (2, this->videoTex);
  if (!this->videoTex[0] || !this->videoTex[1])
    return 0;

  glGenTextures (OGL2_TEX_LAST, ytex->tex);
  uvh = (h + 1) >> 1;
  _config_texture (GL_TEXTURE_2D, ytex->tex[OGL2_TEX_y],   w,      h,   GL_LUMINANCE, GL_UNSIGNED_BYTE, GL_NEAREST);
  _config_texture (GL_TEXTURE_2D, ytex->tex[OGL2_TEX_u],   w >> 1, uvh, GL_LUMINANCE, GL_UNSIGNED_BYTE, GL_NEAREST);
  _config_texture (GL_TEXTURE_2D, ytex->tex[OGL2_TEX_v],   w >> 1, uvh, GL_LUMINANCE, GL_UNSIGNED_BYTE, GL_NEAREST);
  _config_texture (GL_TEXTURE_2D, ytex->tex[OGL2_TEX_yuv], w,      h,   GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, GL_NEAREST);
  _config_texture (GL_TEXTURE_2D, ytex->tex[OGL2_TEX_uv],  w >> 1, uvh, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, GL_NEAREST);

  if (this->hw) {
    for (i = 0; i < 3; i++) {
      _config_texture (GL_TEXTURE_2D, ytex->tex[OGL2_TEX_HW0 + i], 0, 0, 0, 0, GL_NEAREST);
    }
  }

  glBindTexture( GL_TEXTURE_2D, 0 );

  glBindBuffer( GL_PIXEL_UNPACK_BUFFER, this->videoPBO );
  glBufferData( GL_PIXEL_UNPACK_BUFFER, w * h * 2, NULL, GL_STREAM_DRAW );
  glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );

  ytex->width = w;
  ytex->height = h;

  _config_texture (GL_TEXTURE_RECTANGLE_ARB, this->videoTex[0], w, h, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
  _config_texture (GL_TEXTURE_RECTANGLE_ARB, this->videoTex[1], w, h, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);

  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );

  glBindFramebuffer( GL_FRAMEBUFFER, this->fbo );
  glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE_ARB, this->videoTex[0], 0 );
  glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_RECTANGLE_ARB, this->videoTex[1], 0 );
  glBindFramebuffer( GL_FRAMEBUFFER, 0 );

  return 1;
}


static void opengl2_upload_overlay(opengl2_driver_t *this, opengl2_overlay_t *o, vo_overlay_t *overlay)
{
  if ( o->tex && ((o->tex_w != o->ovl_w) || (o->tex_h != o->ovl_h)) ) {
    glDeleteTextures( 1, &o->tex );
    o->tex = 0;
  }

  if ( !o->tex ) {
    glGenTextures( 1, &o->tex );
    o->tex_w = o->ovl_w;
    o->tex_h = o->ovl_h;
  }

  if ( overlay->rle && !this->overlayPBO ) {
    glGenBuffers( 1, &this->overlayPBO );
    if ( !this->overlayPBO ) {
      xprintf( this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": overlay PBO failed\n" );
      return;
    }
  }

  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, o->tex );

  if (overlay->argb_layer) {
    pthread_mutex_lock(&overlay->argb_layer->mutex); /* buffer can be changed or freed while unlocked */

    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, o->tex_w, o->tex_h, 0, GL_BGRA, GL_UNSIGNED_BYTE,
                  overlay->argb_layer->buffer );

    pthread_mutex_unlock(&overlay->argb_layer->mutex);

  } else {
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, this->overlayPBO );
    glBufferData( GL_PIXEL_UNPACK_BUFFER_ARB, o->tex_w * o->tex_h * 4, NULL, GL_STREAM_DRAW );

    void *rgba = glMapBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY );
    _x_overlay_to_argb32(overlay, rgba, o->tex_w, "RGBA");

    glUnmapBuffer( GL_PIXEL_UNPACK_BUFFER_ARB );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, o->tex_w, o->tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0 );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER_ARB, 0 );
  }

  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  glTexParameterf( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
  glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );
}

static void opengl2_overlay_begin (vo_driver_t *this_gen, vo_frame_t *frame_gen, int changed)
{
  //fprintf(stderr, "opengl2_overlay_begin\n");
  (void)frame_gen;
  opengl2_driver_t  *this = (opengl2_driver_t *) this_gen;

  if ( changed ) {
    this->ovl_changed = 1;

    if (!this->gl->make_current(this->gl)) {
      return;
    }
  }
}


static void opengl2_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay)
{
  opengl2_driver_t  *this = (opengl2_driver_t *) this_gen;

  (void)frame_gen;
  if ( !this->ovl_changed || this->ovl_changed>XINE_VORAW_MAX_OVL )
    return;

  if ( overlay->width<=0 || overlay->height<=0 )
    return;

  opengl2_overlay_t *ovl = &this->overlays[this->ovl_changed-1];

  ovl->ovl_w = overlay->width;
  ovl->ovl_h = overlay->height;
  ovl->ovl_x = overlay->x;
  ovl->ovl_y = overlay->y;
  ovl->unscaled = overlay->unscaled;
  ovl->extent_width = overlay->extent_width;
  ovl->extent_height = overlay->extent_height;
  if ( overlay->extent_width == -1 )
    ovl->vid_scale = 1;
  else
    ovl->vid_scale = 0;

  if (overlay->rle) {
    if (!overlay->rgb_clut || !overlay->hili_rgb_clut) {
      _x_overlay_clut_yuv2rgb(overlay, this->color_standard);
    }
  }

  if (overlay->argb_layer || overlay->rle) {
    opengl2_upload_overlay(this, ovl, overlay);
    ++this->ovl_changed;
  }
}


static void opengl2_overlay_end (vo_driver_t *this_gen, vo_frame_t *vo_img)
{
  //fprintf(stderr, "opengl2_overlay_end\n");
  opengl2_driver_t  *this = (opengl2_driver_t *) this_gen;
  unsigned i;

  (void)vo_img;
  if ( !this->ovl_changed )
    return;

  this->num_ovls = this->ovl_changed - 1;

  /* free unused textures and buffers */
  for ( i = this->num_ovls; i < XINE_VORAW_MAX_OVL && this->overlays[i].tex; ++i ) {
    this->overlays[i].ovl_w = 0;
    this->overlays[i].ovl_h = 0;
    glDeleteTextures( 1, &this->overlays[i].tex );
    this->overlays[i].tex = 0;
  }

  this->gl->release_current(this->gl);
}


static int opengl2_redraw_needed( vo_driver_t *this_gen )
{
  opengl2_driver_t  *this = (opengl2_driver_t *) this_gen;

  _x_vo_scale_compute_ideal_size( &this->sc );
  if ( _x_vo_scale_redraw_needed( &this->sc ) ) {
    _x_vo_scale_compute_output_size( &this->sc );
    return 1;
  }
  return this->update_csc | this->sharp.changed | this->transform.changed;
}



static void opengl2_update_csc_matrix (opengl2_driver_t *that, opengl2_frame_t *frame) {
  int color_standard;

  color_standard = cm_from_frame (&frame->vo_frame);

  if ( that->update_csc || that->color_standard != color_standard ) {
    float hue = (float)that->hue * M_PI / 128.0;
    float saturation = (float)that->saturation / 128.0;
    float contrast = (float)that->contrast / 128.0;
    float brightness = that->brightness;
    float uvcos = saturation * cos( hue );
    float uvsin = saturation * sin( hue );
    int i;

    if ((color_standard >> 1) == 8) {
      /* YCgCo. This is really quite simple. */
      uvsin *= contrast;
      uvcos *= contrast;
      /* csc_matrix[rgb][yuv1] */
      that->csc_matrix[1] = -1.0 * uvcos - 1.0 * uvsin;
      that->csc_matrix[2] =  1.0 * uvcos - 1.0 * uvsin;
      that->csc_matrix[5] =  1.0 * uvcos;
      that->csc_matrix[6] =                1.0 * uvsin;
      that->csc_matrix[9] = -1.0 * uvcos + 1.0 * uvsin;
      that->csc_matrix[10] = -1.0 * uvcos - 1.0 * uvsin;
      for (i = 0; i < 12; i += 4) {
        that->csc_matrix[i] = contrast;
        that->csc_matrix[i + 3] = (brightness * contrast
          - 128.0 * (that->csc_matrix[i + 1] + that->csc_matrix[i + 2])) / 255.0;
      }
    } else {
      /* YCbCr */
      float kb, kr;
      float vr, vg, ug, ub;
      float ygain, yoffset;

      switch (color_standard >> 1) {
        case 1:  kb = 0.0722; kr = 0.2126; break; /* ITU-R 709 */
        case 4:  kb = 0.1100; kr = 0.3000; break; /* FCC */
        case 7:  kb = 0.0870; kr = 0.2120; break; /* SMPTE 240 */
        default: kb = 0.1140; kr = 0.2990;        /* ITU-R 601 */
      }
      vr = 2.0 * (1.0 - kr);
      vg = -2.0 * kr * (1.0 - kr) / (1.0 - kb - kr);
      ug = -2.0 * kb * (1.0 - kb) / (1.0 - kb - kr);
      ub = 2.0 * (1.0 - kb);

      if (color_standard & 1) {
        /* fullrange mode */
        yoffset = brightness;
        ygain = contrast;
        uvcos *= contrast * 255.0 / 254.0;
        uvsin *= contrast * 255.0 / 254.0;
      } else {
        /* mpeg range */
        yoffset = brightness - 16.0;
        ygain = contrast * 255.0 / 219.0;
        uvcos *= contrast * 255.0 / 224.0;
        uvsin *= contrast * 255.0 / 224.0;
      }

      /* csc_matrix[rgb][yuv1] */
      that->csc_matrix[1] = -uvsin * vr;
      that->csc_matrix[2] = uvcos * vr;
      that->csc_matrix[5] = uvcos * ug - uvsin * vg;
      that->csc_matrix[6] = uvcos * vg + uvsin * ug;
      that->csc_matrix[9] = uvcos * ub;
      that->csc_matrix[10] = uvsin * ub;
      for (i = 0; i < 12; i += 4) {
        that->csc_matrix[i] = ygain;
        that->csc_matrix[i + 3] = (yoffset * ygain
          - 128.0 * (that->csc_matrix[i + 1] + that->csc_matrix[i + 2])) / 255.0;
      }
    }

    that->color_standard = color_standard;
    that->update_csc = 0;

    xprintf (that->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": b %d c %d s %d h %d [%s]\n",
      that->brightness, that->contrast, that->saturation, that->hue, cm_names[color_standard]);
  }
}



static void opengl2_update_overlays( opengl2_driver_t *that )
{
  if (that->ovl_changed) {
    int i;

    that->ovl_vid_scale = 0;
    for (i = 0; i < that->num_ovls; ++i) {
      opengl2_overlay_t *o = &that->overlays[i];
      
      /* handle DVB subs scaling, e.g. 720x576->1920x1080
       * FIXME: spu_dvb now always sends extent_width >= 0.
       * do we need this anymore ?? */
      if (o->vid_scale) {
        that->ovl_vid_scale = 1;
        if ((o->ovl_w > 720) || (o->ovl_h > 576)) {
          that->ovl_vid_scale = 0;
          break;
        }
      }
    }
    that->ovl_changed = 0;
  }
}


/* DVB subtitles are split into rectangular regions, with no respect to text lines.
 * Instead, they just touch each other exactly. Make sure they still do after scaling. */
typedef struct {
  int x1, y1, x2, y2;
} opengl2_rect_t;

static void opengl2_rect_set (opengl2_rect_t *r, opengl2_overlay_t *o) {
  r->x1 = o->ovl_x;
  r->y1 = o->ovl_y;
  r->x2 = r->x1 + o->ovl_w;
  r->y2 = r->y1 + o->ovl_h;
}

static void opengl2_rect_scale (opengl2_rect_t *r, float fx, float fy) {
  r->x1 *= fx;
  r->y1 *= fy;
  r->x2 *= fx;
  r->y2 *= fy;
}


static void opengl2_draw_scaled_overlays( opengl2_driver_t *that, opengl2_frame_t *frame )
{
  int i;

  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();
  glOrtho( 0.0, frame->width, 0.0, frame->height, -1.0, 1.0 );

  glEnable( GL_BLEND );

  that->ovls_drawn = 0;
  for ( i=0; i<that->num_ovls; ++i ) {
    opengl2_overlay_t *o = &that->overlays[i];
    opengl2_rect_t or;

    if ( o->unscaled )
      continue;
    /* if extent == video size, blend it here, and make it take part in bicubic scaling.
     * other scaled overlays with known extent:
     * draw overlay over scaled video frame -> more sharpness in overlay. */
    if ((o->extent_width > 0) && (o->extent_width != frame->width) &&
        (o->extent_height > 0) && (o->extent_height != frame->height))
      continue;

    that->ovls_drawn |= 1 << i;
    opengl2_rect_set (&or, o);
    if (o->vid_scale && that->ovl_vid_scale) {
      float fx = frame->width / 720.0, fy = frame->height / 576.0;
      opengl2_rect_scale (&or, fx, fy);
    }

    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, o->tex );

    glBegin( GL_QUADS );
      glTexCoord2f( 0, 0 );                  glVertex3f( or.x1, or.y1, 0.);
      glTexCoord2f( 0, o->tex_h );           glVertex3f( or.x1, or.y2, 0.);
      glTexCoord2f( o->tex_w, o->tex_h );    glVertex3f( or.x2, or.y2, 0.);
      glTexCoord2f( o->tex_w, 0 );           glVertex3f( or.x2, or.y1, 0.);
    glEnd();
  }
  glDisable( GL_BLEND );
}


static void opengl2_draw_unscaled_overlays( opengl2_driver_t *that )
{
  int i;

  glEnable( GL_BLEND );
  for ( i=0; i<that->num_ovls; ++i ) {
    opengl2_overlay_t *o = &that->overlays[i];
    vo_scale_map_t map;

    if (that->ovls_drawn & (1 << i))
      continue;

    map.in.x0 = 0;
    map.in.y0 = 0;
    map.in.x1 = o->ovl_w;
    map.in.y1 = o->ovl_h;
    map.out.x0 = o->ovl_x;
    map.out.y0 = o->ovl_y;
    if (!o->unscaled) {
      map.out.x1 = o->extent_width;
      map.out.y1 = o->extent_height;
      if (_x_vo_scale_map (&that->sc, &map) != VO_SCALE_MAP_OK)
        continue;
    } else {
      map.out.x1 = o->ovl_x + o->ovl_w;
      map.out.y1 = o->ovl_y + o->ovl_h;
    }

    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, o->tex );

    glBegin( GL_QUADS );
      glTexCoord2f (map.in.x0, map.in.y0);    glVertex3f (map.out.x0, map.out.y0, 0.);
      glTexCoord2f (map.in.x0, map.in.y1);    glVertex3f (map.out.x0, map.out.y1, 0.);
      glTexCoord2f (map.in.x1, map.in.y1);    glVertex3f (map.out.x1, map.out.y1, 0.);
      glTexCoord2f (map.in.x1, map.in.y0);    glVertex3f (map.out.x1, map.out.y0, 0.);
    glEnd();
  }
  glDisable( GL_BLEND );
}



static GLuint opengl2_swap_textures( opengl2_driver_t *that, GLuint current_dest )
{
  if (current_dest == that->videoTex[0]) {
    glDrawBuffer( GL_COLOR_ATTACHMENT1 );
    return that->videoTex[1];
  }

  glDrawBuffer( GL_COLOR_ATTACHMENT0 );
  return that->videoTex[0];
}



static GLuint opengl2_sharpness( opengl2_driver_t *that, opengl2_frame_t *frame, GLuint video_texture )
{
  GLuint ret = video_texture;
  
  if (!that->sharp.program.compiled) {
    if (!opengl2_build_program (that, &that->sharp.program, &blur_sharpen_frag, "blur_sharpen_frag"))
      return ret;
  }

  ret = opengl2_swap_textures( that, video_texture );

  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();
  glOrtho( 0.0, frame->width, 0.0, frame->height, -1.0, 1.0 );

  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( GL_TEXTURE_RECTANGLE_ARB, video_texture );

  glUseProgram (that->sharp.program.program);
  glUniform1i (glGetUniformLocationARB (that->sharp.program.program, "tex"), 0);
  glUniform1f (glGetUniformLocationARB (that->sharp.program.program, "mid"), that->sharp.mid);
  glUniform1f (glGetUniformLocationARB (that->sharp.program.program, "side"), that->sharp.side);
  glUniform1f (glGetUniformLocationARB (that->sharp.program.program, "corn"), that->sharp.corn);

  glBegin( GL_QUADS );
    glTexCoord2f( 0, 0 );                           glVertex3f( 0, 0, 0.);
    glTexCoord2f( 0, frame->height );               glVertex3f( 0, frame->height, 0.);
    glTexCoord2f( frame->width, frame->height );    glVertex3f( frame->width, frame->height, 0.);
    glTexCoord2f( frame->width, 0 );                glVertex3f( frame->width, 0, 0.);
  glEnd();

  glUseProgram( 0 );

  return ret;
}


typedef struct {
  int guiw, guih;
  int sx1, sx2, sy1, sy2, dx1, dx2, dy1, dy2, dw, dh;
  GLuint video_texture;
} opengl2_draw_info_t;

static int opengl2_draw_video_bicubic (opengl2_driver_t *that, const opengl2_draw_info_t *info) {
  if (!that->bicubic_lut_texture) {
    if (!create_lut_texture (that))
      return 0;
  }

  if (!that->bicubic_pass1_program.compiled
    && !opengl2_build_program (that, &that->bicubic_pass1_program, &bicubic_pass1_frag, "bicubic_pass1_frag"))
    return 0;
  if (!that->bicubic_pass2_program.compiled
    && !opengl2_build_program (that, &that->bicubic_pass2_program, &bicubic_pass2_frag, "bicubic_pass2_frag"))
    return 0;
  if (!that->bicubic_fbo) {
    glGenFramebuffers (1, &that->bicubic_fbo);
    if (!that->bicubic_fbo)
      return 0;
  }
  if ((that->bicubic_pass1_texture_width != info->dw) || (that->bicubic_pass1_texture_height != info->dh)) {
    if (that->bicubic_pass1_texture)
      glDeleteTextures (1, &that->bicubic_pass1_texture);
    glGenTextures (1, &that->bicubic_pass1_texture);
    if (!that->bicubic_pass1_texture)
      return 0;
    _config_texture (GL_TEXTURE_RECTANGLE_ARB, that->bicubic_pass1_texture,
                     info->dw, info->dh, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
    that->bicubic_pass1_texture_width  = info->dw;
    that->bicubic_pass1_texture_height = info->dh;
  }
  glBindFramebuffer (GL_FRAMEBUFFER, that->bicubic_fbo);
  glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE_ARB, that->bicubic_pass1_texture, 0);

  glViewport (0, 0, info->dw, info->dh);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0, info->dw, 0, info->dh, -1, 1);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, info->video_texture);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glActiveTexture (GL_TEXTURE1);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, that->bicubic_lut_texture);
  glUseProgram (that->bicubic_pass1_program.program);
  glUniform1i (glGetUniformLocationARB (that->bicubic_pass1_program.program, "tex"), 0);
  glUniform1i (glGetUniformLocationARB (that->bicubic_pass1_program.program, "lut"), 1);
  glUniform1f (glGetUniformLocationARB (that->bicubic_pass1_program.program, "spline"), CATMULLROM_SPLINE);

  glBegin (GL_QUADS);
    glTexCoord2f (info->sx1, info->sy1); glVertex3f (       0,        0, 0);
    glTexCoord2f (info->sx1, info->sy2); glVertex3f (       0, info->dh, 0);
    glTexCoord2f (info->sx2, info->sy2); glVertex3f (info->dw, info->dh, 0);
    glTexCoord2f (info->sx2, info->sy1); glVertex3f (info->dw,        0, 0);
  glEnd ();

  glActiveTexture (GL_TEXTURE0);
  glBindFramebuffer (GL_FRAMEBUFFER, 0);

  glViewport (0, 0, info->guiw, info->guih);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0, info->guiw, info->guih, 0, -1, 1);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, that->bicubic_pass1_texture);
  glActiveTexture (GL_TEXTURE1);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, that->bicubic_lut_texture);
  glUseProgram (that->bicubic_pass2_program.program);
  glUniform1i (glGetUniformLocationARB (that->bicubic_pass2_program.program, "tex"), 0);
  glUniform1i (glGetUniformLocationARB (that->bicubic_pass2_program.program, "lut"), 1);
  glUniform1f (glGetUniformLocationARB (that->bicubic_pass2_program.program, "spline"), CATMULLROM_SPLINE);

  glBegin (GL_QUADS);
    glTexCoord2f (       0,        0); glVertex3f (info->dx1, info->dy1, 0);
    glTexCoord2f (       0, info->dh); glVertex3f (info->dx1, info->dy2, 0);
    glTexCoord2f (info->dw, info->dh); glVertex3f (info->dx2, info->dy2, 0);
    glTexCoord2f (info->dw,        0); glVertex3f (info->dx2, info->dy1, 0);
  glEnd ();

  glUseProgram (0);

  return 1;
}

static int opengl2_draw_video_cubic_x (opengl2_driver_t *that, const opengl2_draw_info_t *info) {
  if (!that->bicubic_lut_texture) {
    if (!create_lut_texture (that))
      return 0;
  }

  if (!that->bicubic_pass1_program.compiled
    && !opengl2_build_program (that, &that->bicubic_pass1_program, &bicubic_pass1_frag, "bicubic_pass1_frag"))
    return 0;

  glViewport (0, 0, info->guiw, info->guih);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0, info->guiw, info->guih, 0, -1, 1);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, info->video_texture);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glActiveTexture (GL_TEXTURE1);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, that->bicubic_lut_texture);
  glUseProgram (that->bicubic_pass1_program.program);
  glUniform1i (glGetUniformLocationARB (that->bicubic_pass1_program.program, "tex"), 0);
  glUniform1i (glGetUniformLocationARB (that->bicubic_pass1_program.program, "lut"), 1);
  glUniform1f (glGetUniformLocationARB (that->bicubic_pass1_program.program, "spline"), CATMULLROM_SPLINE);

  glBegin (GL_QUADS);
    glTexCoord2f (info->sx1, info->sy1); glVertex3f (info->dx1, info->dy1, 0);
    glTexCoord2f (info->sx1, info->sy2); glVertex3f (info->dx1, info->dy2, 0);
    glTexCoord2f (info->sx2, info->sy2); glVertex3f (info->dx2, info->dy2, 0);
    glTexCoord2f (info->sx2, info->sy1); glVertex3f (info->dx2, info->dy1, 0);
  glEnd ();

  glUseProgram (0);

  return 1;
}

static int opengl2_draw_video_cubic_y (opengl2_driver_t *that, const opengl2_draw_info_t *info) {
  if (!that->bicubic_lut_texture) {
    if (!create_lut_texture (that))
      return 0;
  }

  if (!that->bicubic_pass2_program.compiled
    && !opengl2_build_program (that, &that->bicubic_pass2_program, &bicubic_pass2_frag, "bicubic_pass2_frag"))
    return 0;

  glViewport (0, 0, info->guiw, info->guih);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0, info->guiw, info->guih, 0, -1, 1);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, info->video_texture);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glActiveTexture (GL_TEXTURE1);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, that->bicubic_lut_texture);
  glUseProgram (that->bicubic_pass2_program.program);
  glUniform1i (glGetUniformLocationARB (that->bicubic_pass2_program.program, "tex"), 0);
  glUniform1i (glGetUniformLocationARB (that->bicubic_pass2_program.program, "lut"), 1);
  glUniform1f (glGetUniformLocationARB (that->bicubic_pass2_program.program, "spline"), CATMULLROM_SPLINE);

  glBegin (GL_QUADS);
    glTexCoord2f (info->sx1, info->sy1); glVertex3f (info->dx1, info->dy1, 0);
    glTexCoord2f (info->sx1, info->sy2); glVertex3f (info->dx1, info->dy2, 0);
    glTexCoord2f (info->sx2, info->sy2); glVertex3f (info->dx2, info->dy2, 0);
    glTexCoord2f (info->sx2, info->sy1); glVertex3f (info->dx2, info->dy1, 0);
  glEnd ();

  glUseProgram (0);

  return 1;
}

static int opengl2_draw_video_simple (opengl2_driver_t *that, const opengl2_draw_info_t *info) {
  (void)that;

  glViewport (0, 0, info->guiw, info->guih);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0, info->guiw, info->guih, 0, -1, 1);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, info->video_texture);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glBegin (GL_QUADS);
    glTexCoord2f (info->sx1, info->sy1); glVertex3f (info->dx1, info->dy1, 0);
    glTexCoord2f (info->sx1, info->sy2); glVertex3f (info->dx1, info->dy2, 0);
    glTexCoord2f (info->sx2, info->sy2); glVertex3f (info->dx2, info->dy2, 0);
    glTexCoord2f (info->sx2, info->sy1); glVertex3f (info->dx2, info->dy1, 0);
  glEnd ();

  return 1;
}

static void opengl2_draw_video_bilinear (opengl2_driver_t *that, const opengl2_draw_info_t *info) {
  (void)that;

  glViewport (0, 0, info->guiw, info->guih);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0, info->guiw, info->guih, 0, -1, 1);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, info->video_texture);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glBegin (GL_QUADS);
    glTexCoord2f (info->sx1, info->sy1); glVertex3f (info->dx1, info->dy1, 0);
    glTexCoord2f (info->sx1, info->sy2); glVertex3f (info->dx1, info->dy2, 0);
    glTexCoord2f (info->sx2, info->sy2); glVertex3f (info->dx2, info->dy2, 0);
    glTexCoord2f (info->sx2, info->sy1); glVertex3f (info->dx2, info->dy1, 0);
  glEnd ();
}

static void _upload_texture(GLenum target, GLuint tex, GLenum format, GLenum type,
                            void *data, unsigned pitch, unsigned height, GLuint pbo)
{
  GLenum pbo_target = (target == GL_TEXTURE_2D ? GL_PIXEL_UNPACK_BUFFER : GL_PIXEL_UNPACK_BUFFER_ARB);
  void *mem;
  glBindBuffer (pbo_target, pbo);
  glBindTexture (target, tex);
  mem = glMapBuffer (pbo_target, GL_WRITE_ONLY);
  xine_fast_memcpy (mem, data, pitch * height);
  glUnmapBuffer (pbo_target);
  glTexSubImage2D (target, 0, 0, 0, pitch >> (format == GL_LUMINANCE_ALPHA), height, format, type, 0);
  glBindBuffer (pbo_target, 0);
}

static void opengl2_draw( opengl2_driver_t *that, opengl2_frame_t *frame )
{
  if (!that->gl->make_current(that->gl)) {
    return;
  }

  if ( !opengl2_check_textures_size( that, frame->width, frame->height ) ) {
    that->gl->release_current(that->gl);
    return;
  }

  opengl2_update_csc_matrix( that, frame );

  glBindFramebuffer( GL_FRAMEBUFFER, that->fbo );

  if (that->hw && frame->format == that->hw->frame_format) {
    unsigned tex, num_texture, sw_format;
    that->glconv->get_textures(that->glconv, &frame->vo_frame, GL_TEXTURE_2D,
                               &that->yuvtex.tex[OGL2_TEX_HW0], &num_texture, &sw_format);
    for (tex = 0; tex < num_texture; tex++) {
      glActiveTexture (GL_TEXTURE0 + tex);
      glBindTexture (GL_TEXTURE_2D, that->yuvtex.tex[OGL2_TEX_HW0 + tex]);
    }
    switch (sw_format) {
      case XINE_IMGFMT_NV12:
        glUseProgram (that->nv12_program.program);
        glUniform1i (glGetUniformLocation (that->nv12_program.program, "texY"), 0);
        glUniform1i (glGetUniformLocation (that->nv12_program.program, "texUV"), 1);
        load_csc_matrix( that->nv12_program.program, that->csc_matrix );
        break;
      default:
        break;
    }
  }
  if (frame->format == XINE_IMGFMT_YV12) {
    int uvh = (frame->height + 1) >> 1;

    glActiveTexture (GL_TEXTURE0);
    _upload_texture(GL_TEXTURE_2D, that->yuvtex.tex[OGL2_TEX_y], GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    frame->vo_frame.base[0], frame->vo_frame.pitches[0], frame->height, that->videoPBO);
    glActiveTexture (GL_TEXTURE1);
    _upload_texture(GL_TEXTURE_2D, that->yuvtex.tex[OGL2_TEX_u], GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    frame->vo_frame.base[1], frame->vo_frame.pitches[1], uvh, that->videoPBO);
    glActiveTexture (GL_TEXTURE2);
    _upload_texture(GL_TEXTURE_2D, that->yuvtex.tex[OGL2_TEX_v], GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    frame->vo_frame.base[2], frame->vo_frame.pitches[2], uvh, that->videoPBO);

    glUseProgram (that->yuv420_program.program);
    glUniform1i (glGetUniformLocation (that->yuv420_program.program, "texY"), 0);
    glUniform1i (glGetUniformLocation (that->yuv420_program.program, "texU" ), 1);
    glUniform1i (glGetUniformLocation (that->yuv420_program.program, "texV" ), 2);
    load_csc_matrix (that->yuv420_program.program, that->csc_matrix);
  }
  else if (frame->format == XINE_IMGFMT_NV12) {

    glActiveTexture (GL_TEXTURE0);
    _upload_texture(GL_TEXTURE_2D, that->yuvtex.tex[OGL2_TEX_y], GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    frame->vo_frame.base[0], frame->vo_frame.pitches[0], frame->height, that->videoPBO);

    glActiveTexture (GL_TEXTURE1);
    _upload_texture(GL_TEXTURE_2D, that->yuvtex.tex[OGL2_TEX_uv], GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                    frame->vo_frame.base[1], frame->vo_frame.pitches[1], (frame->height+1)/2, that->videoPBO);

    glUseProgram (that->nv12_program.program);
    glUniform1i (glGetUniformLocation (that->nv12_program.program, "texY"), 0);
    glUniform1i (glGetUniformLocation (that->nv12_program.program, "texUV"), 1);
    load_csc_matrix( that->nv12_program.program, that->csc_matrix );
  }
  else if ( frame->format == XINE_IMGFMT_YUY2 ) {
    glActiveTexture( GL_TEXTURE0 );
    _upload_texture(GL_TEXTURE_2D, that->yuvtex.tex[OGL2_TEX_yuv], GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                    frame->vo_frame.base[0], frame->vo_frame.pitches[0], frame->height, that->videoPBO);

    glUseProgram( that->yuv422_program.program );
    glUniform2f (glGetUniformLocationARB( that->yuv422_program.program, "texSize"), frame->width/*vo_frame.pitches[0]/2*/, 0);
    glUniform1i (glGetUniformLocation( that->yuv422_program.program, "texYUV" ), 0 );
    load_csc_matrix( that->yuv422_program.program, that->csc_matrix );
  }
  else {
    /* unknown format */
    xprintf( that->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": unknown image format 0x%08x\n", frame->format );
  }

  glViewport( 0, 0, frame->width, frame->height );
  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();
  glOrtho( 0.0, 1.0, 0.0, 1.0, -1.0, 1.0 );
  glMatrixMode( GL_MODELVIEW );
  glLoadIdentity();

  GLuint video_texture = that->videoTex[0];
  glDrawBuffer( GL_COLOR_ATTACHMENT0 );

  glBegin( GL_QUADS );
    glTexCoord2f( 0, 0 );    glVertex2i( 0, 0 );
    glTexCoord2f( 0, 1 );    glVertex2i( 0, 1 );
    glTexCoord2f( 1, 1 );    glVertex2i( 1, 1 );
    glTexCoord2f( 1, 0 );    glVertex2i( 1, 0 );
  glEnd();

  glUseProgram( 0 );

  // post-processing
  if (that->sharp.changed) {
    that->sharp.side = that->sharp.value / 100.0 * frame->width / 1920.0;
    if (that->sharp.value < 0)
      that->sharp.side /= -6.8;
    else
      that->sharp.side /= -3.4;
    that->sharp.corn = that->sharp.side * 0.707;
    that->sharp.mid = 1.0 - 4.0 * (that->sharp.side + that->sharp.corn);
    that->sharp.changed = 0;
    xprintf (that->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": sharpness %d\n", that->sharp.value);
  }
  if (that->sharp.value)
    video_texture = opengl2_sharpness( that, frame, video_texture );

  // draw scaled overlays
  opengl2_update_overlays( that );
  opengl2_draw_scaled_overlays( that, frame );

  glBindFramebuffer( GL_FRAMEBUFFER, 0 );

  // draw on screen
  {
    int res = 0;
    opengl2_draw_info_t info;

    info.video_texture = video_texture;

    info.guiw = that->sc.gui_width;
    info.guih = that->sc.gui_height;

    info.sx1 = that->sc.displayed_xoffset;
    info.sy1 = that->sc.displayed_yoffset;
    info.sx2 = that->sc.displayed_width + that->sc.displayed_xoffset;
    info.sy2 = that->sc.displayed_height + that->sc.displayed_yoffset;

    info.dw  = that->sc.output_width;
    info.dh  = that->sc.displayed_height;
    if (!(that->transform.flags & XINE_VO_TRANSFORM_FLIP_H)) {
      info.dx1 = that->sc.output_xoffset;
      info.dx2 = that->sc.output_xoffset + that->sc.output_width;
    } else {
      info.dx1 = that->sc.output_xoffset + that->sc.output_width;
      info.dx2 = that->sc.output_xoffset;
    }
    if (!(that->transform.flags & XINE_VO_TRANSFORM_FLIP_V)) {
      info.dy1 = that->sc.output_yoffset;
      info.dy2 = that->sc.output_yoffset + that->sc.output_height;
    } else {
      info.dy1 = that->sc.output_yoffset + that->sc.output_height;
      info.dy2 = that->sc.output_yoffset;
    }
    that->transform.changed = 0;

    if (that->scale_bicubic) {
      if (that->sc.displayed_width != that->sc.output_width) {
        if (that->sc.displayed_height != that->sc.output_height)
          res = opengl2_draw_video_bicubic (that, &info);
        else
          res = opengl2_draw_video_cubic_x (that, &info);
      } else {
        if (that->sc.displayed_height != that->sc.output_height)
          res = opengl2_draw_video_cubic_y (that, &info);
        else
          res = opengl2_draw_video_simple (that, &info);
      }
    }
    if (!res)
      opengl2_draw_video_bilinear (that, &info);
  }

  // draw unscaled overlays
  opengl2_draw_unscaled_overlays (that);

  // if (that->mglXSwapInterval)
    //that->mglXSwapInterval (1);

  that->gl->swap_buffers (that->gl);
  that->gl->release_current (that->gl);
}

static void opengl2_display_frame( vo_driver_t *this_gen, vo_frame_t *frame_gen )
{
  opengl2_driver_t  *this  = (opengl2_driver_t *) this_gen;
  opengl2_frame_t   *frame = (opengl2_frame_t *) frame_gen;

  if ( (frame->width != this->sc.delivered_width) ||
                  (frame->height != this->sc.delivered_height) ||
                  (frame->ratio != this->sc.delivered_ratio) ||
                  (frame->vo_frame.crop_left != this->sc.crop_left) ||
                  (frame->vo_frame.crop_right != this->sc.crop_right) ||
                  (frame->vo_frame.crop_top != this->sc.crop_top) ||
                  (frame->vo_frame.crop_bottom != this->sc.crop_bottom) ) {
    this->sc.force_redraw = 1;    /* trigger re-calc of output size */
  }

  this->sc.delivered_height = frame->height;
  this->sc.delivered_width  = frame->width;
  this->sc.delivered_ratio  = frame->ratio;
  this->sc.crop_left        = frame->vo_frame.crop_left;
  this->sc.crop_right       = frame->vo_frame.crop_right;
  this->sc.crop_top         = frame->vo_frame.crop_top;
  this->sc.crop_bottom      = frame->vo_frame.crop_bottom;

  opengl2_redraw_needed( this_gen );

  if (this->gl->resize) {
    if (this->last_gui_width != this->sc.gui_width || this->last_gui_height != this->sc.gui_height) {
      this->last_gui_width = this->sc.gui_width;
      this->last_gui_height = this->sc.gui_height;
      this->gl->resize(this->gl, this->last_gui_width, this->last_gui_height);
    }
  }

  if( !this->exiting ) {
    pthread_mutex_lock(&this->drawable_lock); /* protect drawable from being changed */
    opengl2_draw( this, frame );
    pthread_mutex_unlock(&this->drawable_lock); /* allow changing drawable again */
  }

  if( !this->exit_indx )
    opengl2_exit_register( this );

  frame->vo_frame.free( &frame->vo_frame );
}



static int opengl2_get_property( vo_driver_t *this_gen, int property )
{
  opengl2_driver_t *this = (opengl2_driver_t*)this_gen;

  switch (property) {
    case VO_PROP_MAX_NUM_FRAMES:
      return 22;
    case VO_PROP_WINDOW_WIDTH:
      return this->sc.gui_width;
    case VO_PROP_WINDOW_HEIGHT:
      return this->sc.gui_height;
    case VO_PROP_OUTPUT_WIDTH:
      return this->sc.output_width;
    case VO_PROP_OUTPUT_HEIGHT:
      return this->sc.output_height;
    case VO_PROP_OUTPUT_XOFFSET:
      return this->sc.output_xoffset;
    case VO_PROP_OUTPUT_YOFFSET:
      return this->sc.output_yoffset;
    case VO_PROP_HUE:
      return this->hue;
    case VO_PROP_SATURATION:
      return this->saturation;
    case VO_PROP_CONTRAST:
      return this->contrast;
    case VO_PROP_BRIGHTNESS:
      return this->brightness;
    case VO_PROP_SHARPNESS:
      return this->sharp.value;
    case VO_PROP_ZOOM_X:
      return this->zoom_x;
    case VO_PROP_ZOOM_Y:
      return this->zoom_y;
    case VO_PROP_ASPECT_RATIO:
      return this->sc.user_ratio;
    case VO_PROP_MAX_VIDEO_WIDTH:
      return this->max_video_width;
    case VO_PROP_MAX_VIDEO_HEIGHT:
      return this->max_video_height;
    case VO_PROP_CAPS2:
      return VO_CAP2_NV12 | VO_CAP2_TRANSFORM | VO_CAP2_ACCEL_GENERIC;
    case VO_PROP_TRANSFORM:
      return this->transform.flags;
  }

  return -1;
}



static int opengl2_set_property( vo_driver_t *this_gen, int property, int value )
{
  opengl2_driver_t *this = (opengl2_driver_t*)this_gen;

  //fprintf(stderr,"opengl2_set_property: property=%d, value=%d\n", property, value );

  switch (property) {
    case VO_PROP_ZOOM_X:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->zoom_x = value;
        this->sc.zoom_factor_x = (double)value / (double)XINE_VO_ZOOM_STEP;
        _x_vo_scale_compute_ideal_size( &this->sc );
        this->sc.force_redraw = 1;     //trigger re-calc of output size 
      }
      break;
    case VO_PROP_ZOOM_Y:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->zoom_y = value;
        this->sc.zoom_factor_y = (double)value / (double)XINE_VO_ZOOM_STEP;
        _x_vo_scale_compute_ideal_size( &this->sc );
        this->sc.force_redraw = 1;    // trigger re-calc of output size
      }
      break;
    case VO_PROP_ASPECT_RATIO:
      if ( value>=XINE_VO_ASPECT_NUM_RATIOS )
        value = XINE_VO_ASPECT_AUTO;
      this->sc.user_ratio = value;
      this->sc.force_redraw = 1;    // trigger re-calc of output size
      break;
    case VO_PROP_HUE: this->hue = value; this->update_csc = 1; break;
    case VO_PROP_SATURATION: this->saturation = value; this->update_csc = 1; break;
    case VO_PROP_CONTRAST: this->contrast = value; this->update_csc = 1; break;
    case VO_PROP_BRIGHTNESS: this->brightness = value; this->update_csc = 1; break;
    case VO_PROP_SHARPNESS: this->sharp.value = value; this->sharp.changed = 1; break;
    case VO_PROP_TRANSFORM:
      value &= XINE_VO_TRANSFORM_FLIP_H | XINE_VO_TRANSFORM_FLIP_V;
      this->transform.changed |= value ^ this->transform.flags;
      this->transform.flags = value;
      break;
  }

  return value;
}



static void opengl2_get_property_min_max( vo_driver_t *this_gen, int property, int *min, int *max ) 
{
  (void)this_gen;
  switch ( property ) {
    case VO_PROP_HUE:
      *max = 127; *min = -128; break;
    case VO_PROP_SATURATION:
      *max = 255; *min = 0; break;
    case VO_PROP_CONTRAST:
      *max = 255; *min = 0; break;
    case VO_PROP_BRIGHTNESS:
      *max = 127; *min = -128; break;
    case VO_PROP_SHARPNESS:
      *max = 100; *min = -100; break;
    default:
      *max = 0; *min = 0;
  }
}



static int opengl2_gui_data_exchange( vo_driver_t *this_gen, int data_type, void *data )
{
  opengl2_driver_t *this = (opengl2_driver_t*)this_gen;

  switch (data_type) {
#ifndef XINE_DISABLE_DEPRECATED_FEATURES
    case XINE_GUI_SEND_COMPLETION_EVENT:
      break;
#endif

    case XINE_GUI_SEND_EXPOSE_EVENT: {
      this->sc.force_redraw = 1;
      break;
    }

    case XINE_GUI_SEND_DRAWABLE_CHANGED: {
      pthread_mutex_lock(&this->drawable_lock); /* wait for other thread which is currently displaying */
      this->gl->set_native_window(this->gl, data);
      pthread_mutex_unlock(&this->drawable_lock);
      this->sc.force_redraw = 1;
      break;
    }

    case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO: {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;

      _x_vo_scale_translate_gui2video(&this->sc, rect->x, rect->y, &x1, &y1);
      _x_vo_scale_translate_gui2video(&this->sc, rect->x + rect->w, rect->y + rect->h, &x2, &y2);
      rect->x = x1;
      rect->y = y1;
      rect->w = x2-x1;
      rect->h = y2-y1;
      break;
    }

    default:
      return -1;
  }

  return 0;
}



static uint32_t opengl2_get_capabilities( vo_driver_t *this_gen )
{
  opengl2_driver_t *this = (opengl2_driver_t *) this_gen;

  return VO_CAP_YV12 |
         VO_CAP_YUY2 |
        (this->hw ? this->hw->driver_capabilities : 0) |
         VO_CAP_CROP |
         VO_CAP_UNSCALED_OVERLAY |
         VO_CAP_CUSTOM_EXTENT_OVERLAY |
         VO_CAP_ARGB_LAYER_OVERLAY |
      /* VO_CAP_VIDEO_WINDOW_OVERLAY | */
         VO_CAP_COLOR_MATRIX |
         VO_CAP_FULLRANGE |
         VO_CAP_HUE |
         VO_CAP_SATURATION |
         VO_CAP_CONTRAST |
         VO_CAP_BRIGHTNESS |
         VO_CAP_SHARPNESS;
}



static void opengl2_set_bicubic( void *this_gen, xine_cfg_entry_t *entry )
{
  opengl2_driver_t  *this  = (opengl2_driver_t *) this_gen;

  this->scale_bicubic = entry->num_value;
  xprintf( this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": scale_bicubic=%d\n", this->scale_bicubic );
}

static void opengl2_dispose (vo_driver_t *this_gen) {
  opengl2_driver_t *this = (opengl2_driver_t *) this_gen;

  opengl2_exit_unregister (this);

  if (this->hw)
    this->hw->destroy(&this->hw);

  /* cm_close already does this.
  this->xine->config->unregister_callbacks (this->xine->config, "video.output.opengl2_bicubic_scaling", NULL, this, sizeof (*this));
  */
  cm_close (this);
  _x_vo_scale_cleanup (&this->sc, this->xine->config);

  pthread_mutex_destroy(&this->drawable_lock);

  this->gl->make_current(this->gl);

  opengl2_delete_program (&this->yuv420_program);
  opengl2_delete_program (&this->nv12_program);
  opengl2_delete_program (&this->yuv422_program);

  if (this->sharp.program.compiled)
    opengl2_delete_program (&this->sharp.program);

  if ( this->bicubic_pass1_program.compiled )
    opengl2_delete_program( &this->bicubic_pass1_program );
  if ( this->bicubic_pass2_program.compiled )
    opengl2_delete_program( &this->bicubic_pass2_program );
  if ( this->bicubic_lut_texture )
    glDeleteTextures( 1, &this->bicubic_lut_texture );
  if ( this->bicubic_pass1_texture )
    glDeleteTextures( 1, &this->bicubic_pass1_texture );
  if ( this->bicubic_fbo )
    glDeleteFramebuffers( 1, &this->bicubic_fbo );

  glDeleteTextures (OGL2_TEX_LAST, this->yuvtex.tex);
  glDeleteTextures (2, this->videoTex);
  if ( this->fbo )
    glDeleteFramebuffers( 1, &this->fbo );
  if ( this->videoPBO )
    glDeleteBuffers( 1, &this->videoPBO );
  if (this->overlayPBO)
    glDeleteBuffers( 1, &this->overlayPBO );

  int i;
  for ( i=0; i<XINE_VORAW_MAX_OVL; ++i ) {
    glDeleteTextures( 1, &this->overlays[i].tex );
  }

  this->gl->release_current(this->gl);
  this->gl->dispose(&this->gl);

  free (this);
}

static vo_frame_t *opengl2_alloc_frame (vo_driver_t *this_gen) {
  opengl2_driver_t *this = (opengl2_driver_t *)this_gen;
  vo_frame_t *frame;

  if (this->hw) {
    mem_frame_t *mem_frame = this->hw->alloc_frame(this->hw);
    if (mem_frame) {
      return &mem_frame->vo_frame;
    }
  }

  frame = mem_frame_alloc_frame (&this->vo_driver);

  if (frame)
    frame->accel_data = &this->accel;
  return frame;
}

static vo_driver_t *opengl2_open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {
  opengl2_class_t  *class = (opengl2_class_t *)class_gen;
  opengl2_driver_t *this;
  config_values_t  *config = class->xine->config;

  this = calloc (1, sizeof (*this));
  if (!this)
    return NULL;
#ifndef HAVE_ZERO_SAFE_MEM
  this->hue                            = 0;
  this->brightness                     = 0;
  this->sharp.changed                  = 0;
  this->sharp.value                    = 0;
  this->transform.flags                = 0;
  this->transform.changed              = 0;
  this->sharp.program.compiled         = 0;
  this->bicubic_pass1_program.compiled = 0;
  this->bicubic_pass2_program.compiled = 0;
  this->bicubic_lut_texture            = 0;
  this->bicubic_pass1_texture          = 0;
  this->bicubic_pass1_texture_width    = 0;
  this->bicubic_pass1_texture_height   = 0;
  this->bicubic_fbo                    = 0;
  this->ovl_changed                    = 0;
  this->num_ovls                       = 0;
  this->yuvtex.tex[OGL2_TEX_y]         = 0;
  this->yuvtex.tex[OGL2_TEX_u]         = 0;
  this->yuvtex.tex[OGL2_TEX_v]         = 0;
  this->yuvtex.tex[OGL2_TEX_yuv]       = 0;
  this->yuvtex.tex[OGL2_TEX_uv]        = 0;
  this->yuvtex.width                   = 0;
  this->yuvtex.height                  = 0;
  this->fbo                            = 0;
  this->videoPBO                       = 0;
  this->videoTex[0]                    = 0;
  this->videoTex[1]                    = 0;
  {
    int i;
    for (i = 0; i < XINE_VORAW_MAX_OVL; ++i) {
      this->overlays[i].ovl_w = this->overlays[i].ovl_h = 0;
      this->overlays[i].ovl_x = this->overlays[i].ovl_y = 0;
      this->overlays[i].unscaled = 0;
      this->overlays[i].tex = 0;
      this->overlays[i].tex_w = this->overlays[i].tex_h = 0;
    }
  }
#endif

  this->gl = _x_load_gl (class->xine, class->visual_type, visual_gen, XINE_GL_API_OPENGL);
  if (this->gl) {
    {
      /* TJ. If X server link gets lost, our next render attempt will fire the
       * Xlib fatal error handler -> exit () -> opengl2_exit () with drawable_lock held.
       * opengl2_display_frame () does quite a lot anyway so the "recursive mutex"
       * performance drop should not matter. */
      pthread_mutexattr_t attr;
      pthread_mutexattr_init (&attr);
      pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE);
      pthread_mutex_init (&this->drawable_lock, &attr);
      pthread_mutexattr_destroy (&attr);
    }

    _x_vo_scale_init (&this->sc, 1, 0, config);

    this->accel.lock = opengl2_accel_lock;
    if (class->visual_type == XINE_VISUAL_TYPE_X11) {
      const x11_visual_t *visual = (const x11_visual_t *)visual_gen;
      this->sc.frame_output_cb   = visual->frame_output_cb;
      this->sc.dest_size_cb      = visual->dest_size_cb;
      this->sc.user_data         = visual->user_data;
      this->accel.display        = visual->display;
      this->accel.disp_type      = VO_DISP_TYPE_X11;
    } else /* class->visual_type == XINE_VISUAL_TYPE_WAYLAND) */ {
      const xine_wayland_visual_t *visual = (const xine_wayland_visual_t *)visual_gen;
      this->sc.frame_output_cb            = visual->frame_output_cb;
      this->sc.user_data                  = visual->user_data;
      this->accel.display                 = visual->display;
      this->accel.disp_type               = VO_DISP_TYPE_WAYLAND;
    }

    this->sc.user_ratio = XINE_VO_ASPECT_AUTO;
    this->zoom_x = 100;
    this->zoom_y = 100;

    this->xine   = class->xine;
    this->config = config;

    this->vo_driver.get_capabilities     = opengl2_get_capabilities;
    this->vo_driver.alloc_frame          = opengl2_alloc_frame;
    this->vo_driver.update_frame_format  = mem_frame_update_frame_format;
    this->vo_driver.overlay_begin        = opengl2_overlay_begin;
    this->vo_driver.overlay_blend        = opengl2_overlay_blend;
    this->vo_driver.overlay_end          = opengl2_overlay_end;
    this->vo_driver.display_frame        = opengl2_display_frame;
    this->vo_driver.get_property         = opengl2_get_property;
    this->vo_driver.set_property         = opengl2_set_property;
    this->vo_driver.get_property_min_max = opengl2_get_property_min_max;
    this->vo_driver.gui_data_exchange    = opengl2_gui_data_exchange;
    this->vo_driver.dispose              = opengl2_dispose;
    this->vo_driver.redraw_needed        = opengl2_redraw_needed;

    if (!this->gl->make_current (this->gl)) {
      xprintf (this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": display unavailable for initialization\n");
    } else {
      {
        GLint v[1] = {0};
        glGetIntegerv (GL_MAX_TEXTURE_SIZE, v);
        if (v[0] > 0) {
          this->max_video_width  =
          this->max_video_height = v[0];
          xprintf (this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": max video size %dx%d.\n",
            this->max_video_width, this->max_video_height);
        }
      }
      {
        GLint v[2] = {0, 0};
        glGetIntegerv (GL_MAX_VIEWPORT_DIMS, v);
        if (v[0] > 0) {
          this->max_display_width  = v[0];
          this->max_display_height = v[1] > 0 ? v[1] : v[0];
          xprintf (this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": max output size %dx%d.\n",
            this->max_display_width, this->max_display_height);
        }
      }

      glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
      glClearDepth (1.0f);
      glDepthFunc (GL_LEQUAL);
      glDisable (GL_DEPTH_TEST);
      glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDisable (GL_BLEND);
      glShadeModel (GL_SMOOTH);
      glEnable (GL_TEXTURE_RECTANGLE_ARB);
      glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

      {
        const char *extensions = glGetString (GL_EXTENSIONS);

        this->texture_float = (extensions && strstr (extensions, "ARB_texture_float")) ? 1 : 0;
      }

#define INITWIDTH  720
#define INITHEIGHT 576
      if (opengl2_check_textures_size (this, INITWIDTH, INITHEIGHT)
        && opengl2_build_program (this, &this->yuv420_program, &yuv420_frag, "yuv420_frag")
        && opengl2_build_program (this, &this->nv12_program,   &nv12_frag,   "nv12_frag")
        && opengl2_build_program (this, &this->yuv422_program, &yuv422_frag, "yuv422_frag")) {
        this->gl->release_current (this->gl);

        this->update_csc = 1;
        this->color_standard = 10;
        this->saturation = 128;
        this->contrast = 128;
        cm_init (this);

        if (this->texture_float) {
          this->scale_bicubic = config->register_bool (config,
            "video.output.opengl2_bicubic_scaling", 0,
            _("opengl2: use a bicubic algo to scale the video"),
            _("Set to true if you want bicubic scaling.\n\n"),
            10, opengl2_set_bicubic, this);
        } else {
          this->scale_bicubic = 0;
        }

        this->hw = _x_hwdec_new(this->xine, &this->vo_driver, class->visual_type, visual_gen, 0);
        if (this->hw) {
          this->glconv = this->hw->opengl_interop(this->hw, this->gl);
          if (!this->glconv)
            this->hw->destroy(&this->hw);
          else
            this->vo_driver.update_frame_format = this->hw->update_frame_format;
        }

        xprintf (this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": initialized.\n");
        return &this->vo_driver;
      }
      this->gl->release_current (this->gl);
    }
    pthread_mutex_destroy (&this->drawable_lock);
    this->gl->dispose (&this->gl);
  }
  free (this);
  return NULL;
}

static int opengl2_check_platform (xine_t *xine, unsigned visual_type, const void *visual) {
  xine_gl_t  *gl;
  int result = 0;

  gl = _x_load_gl (xine, visual_type, visual, XINE_GL_API_OPENGL);
  if (!gl)
    return 0;

  if (gl->make_current (gl)) {
    const char *extensions = glGetString (GL_EXTENSIONS);

    if (extensions
      && strstr (extensions, "ARB_texture_rectangle")
      && strstr (extensions, "ARB_texture_non_power_of_two")
      && strstr (extensions, "ARB_pixel_buffer_object")
      && strstr (extensions, "ARB_framebuffer_object")
      && strstr (extensions, "ARB_fragment_shader")
      && strstr (extensions, "ARB_vertex_shader"))
      result = 1;
    gl->release_current (gl);
  }
  gl->dispose (&gl);
  return result;
}

/*
 * class functions
 */

static void *opengl2_init_class( xine_t *xine, unsigned visual_type, const void *visual_gen )
{
  opengl2_class_t *this;

  if (!opengl2_check_platform( xine, visual_type, visual_gen)) {
    return NULL;
  }

  this = calloc(1, sizeof(*this));
  if (!this) {
    return NULL;
  }

  this->driver_class.open_plugin     = opengl2_open_plugin;
  this->driver_class.identifier      = "opengl2";
  this->driver_class.description     = N_("xine video output plugin using opengl 2.0");
  this->driver_class.dispose         = default_video_driver_class_dispose;
  this->xine                         = xine;
  this->visual_type                  = visual_type;

  return this;
}

static void *opengl2_init_class_x11 (xine_t *xine, const void *visual_gen) {
  return opengl2_init_class (xine, XINE_VISUAL_TYPE_X11, visual_gen);
}

static void *opengl2_init_class_wl (xine_t *xine, const void *visual_gen) {
  return opengl2_init_class (xine, XINE_VISUAL_TYPE_WAYLAND, visual_gen);
}

static const vo_info_t vo_info_opengl2 = {
  .priority    = 8,
  .visual_type = XINE_VISUAL_TYPE_X11,
};

static const vo_info_t vo_info_opengl2_wl = {
  .priority    = 8,
  .visual_type = XINE_VISUAL_TYPE_WAYLAND,
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "opengl2", XINE_VERSION_CODE, &vo_info_opengl2,    opengl2_init_class_x11 },
  { PLUGIN_VIDEO_OUT, 22, "opengl2", XINE_VERSION_CODE, &vo_info_opengl2_wl, opengl2_init_class_wl },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
