/*
 * Copyright (C) 2004 the xine project
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: 
 *
 * Most parts of this code were taken from VLC, http://www.videolan.org
 * Thanks for the good research, folks!
 */

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#import "video_window.h"

@implementation XineVideoWindow

- (void) displayTexture {
    if ([openGLView lockFocusIfCanDraw]) {
        [openGLView drawRect: [openGLView bounds]];
        [openGLView reloadTexture];
        [openGLView unlockFocus];
    }
}

- (void) setContentSize: (NSSize) size {
    width = size.width;
    height = size.height;

    [openGLView setVideoSize: width : height];
    
    [super setContentSize: size];
}

- (id) initWithContentRect: (NSRect)rect 
       styleMask:(unsigned int)styleMask 
       backing:(NSBackingStoreType)bufferingType 
       defer:(BOOL)flag 
       screen:(NSScreen *)aScreen {
    self = [super initWithContentRect: rect
                      styleMask: styleMask
                      backing: bufferingType
                      defer: flag
                      screen: aScreen];

    openGLView = [[XineOpenGLView alloc] initWithFrame:rect];
    [self setContentView: openGLView];
    [self setTitle: @"xine video output"];

    return self;
}

- (XineOpenGLView *) getGLView {
    return openGLView;
}

- (void) goFullScreen {
    [openGLView goFullScreen];
}

- (void) exitFullScreen {
    [openGLView exitFullScreen];
}

@end


@implementation XineOpenGLView

- (id) initWithFrame: (NSRect) frame {
    
    NSOpenGLPixelFormatAttribute attribs[] = {
        NSOpenGLPFAAccelerated,
        NSOpenGLPFANoRecovery,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAColorSize, 24,
        NSOpenGLPFAAlphaSize, 8,
        NSOpenGLPFADepthSize, 24,
        NSOpenGLPFAWindow,
        0
    };

    NSOpenGLPixelFormat * fmt = [[NSOpenGLPixelFormat alloc]
        initWithAttributes: attribs];

    if (!fmt) {
        printf  ("Cannot create NSOpenGLPixelFormat\n");
        return nil;
    }

    self = [super initWithFrame:frame pixelFormat: fmt];

    currentContext = [self openGLContext];
    [currentContext makeCurrentContext];
    [currentContext update];

    /* Black background */
    glClearColor (0.0, 0.0, 0.0, 0.0);
    
    i_texture      = 0;
    initDone       = 0;
    isFullScreen   = 0;
    width          = frame.size.width;
    height         = frame.size.height;
    texture_buffer = nil;

    [self initTextures];

    return self;
}

- (void) reshape {
    if (!initDone)
        return;
    
    [currentContext makeCurrentContext];

    NSRect bounds = [self bounds];
    glViewport (0, 0, (GLint) bounds.size.width,
                (GLint) bounds.size.height);

    f_x = 1.0;
    f_y = 1.0;
}


- (void) initTextures {
    [currentContext makeCurrentContext];

    /* Free previous texture if any */
    if (i_texture)
        glDeleteTextures (1, &i_texture);

    if (texture_buffer)
        texture_buffer = realloc (texture_buffer, sizeof (char) * width * height * 3);
    else
        texture_buffer = malloc (sizeof (char) * width * height * 3);

    /* Create textures */
    glGenTextures (1, &i_texture);

    glEnable (GL_TEXTURE_RECTANGLE_EXT);
    glEnable (GL_UNPACK_CLIENT_STORAGE_APPLE);

    glBindTexture (GL_TEXTURE_RECTANGLE_EXT, i_texture);

    /* Use VRAM texturing */
    glTexParameteri (GL_TEXTURE_RECTANGLE_EXT,
            GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_CACHED_APPLE);

    /* Tell the driver not to make a copy of the texture but to use
       our buffer */
    glPixelStorei (GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);

    /* Linear interpolation */
    glTexParameteri (GL_TEXTURE_RECTANGLE_EXT,
            GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_EXT,
            GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    /* I have no idea what this exactly does, but it seems to be
       necessary for scaling */
    glTexParameteri (GL_TEXTURE_RECTANGLE_EXT,
            GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_RECTANGLE_EXT,
            GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);

    glTexImage2D (GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA,
            width, height, 0,
            GL_YCBCR_422_APPLE, GL_UNSIGNED_SHORT_8_8_APPLE,
            texture_buffer);

    initDone = 1;
}

- (void) reloadTexture {
    if (!initDone)
        return;
    
    [currentContext makeCurrentContext];

    glBindTexture (GL_TEXTURE_RECTANGLE_EXT, i_texture);

    /* glTexSubImage2D is faster than glTexImage2D
     *  http://developer.apple.com/samplecode/Sample_Code/Graphics_3D/TextureRange/MainOpenGLView.m.htm
     */
    glTexSubImage2D (GL_TEXTURE_RECTANGLE_EXT, 0, 0, 0,
            width, height,
            GL_YCBCR_422_APPLE, GL_UNSIGNED_SHORT_8_8_APPLE,
            texture_buffer);
}

- (void) goFullScreen {
    /* Create the new pixel format */
    NSOpenGLPixelFormatAttribute attribs[] = {
        NSOpenGLPFAAccelerated,
        NSOpenGLPFANoRecovery,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAColorSize, 24,
        NSOpenGLPFAAlphaSize, 8,
        NSOpenGLPFADepthSize, 24,
        NSOpenGLPFAFullScreen,
        NSOpenGLPFAScreenMask,
        CGDisplayIDToOpenGLDisplayMask (kCGDirectMainDisplay),
        0
    };
    
    NSOpenGLPixelFormat * fmt = [[NSOpenGLPixelFormat alloc]
        initWithAttributes: attribs];    
    if (!fmt) {
        printf ("Cannot create NSOpenGLPixelFormat\n");
        return;
    }

    /* Create the new OpenGL context */
    fullScreenContext = [[NSOpenGLContext alloc]
        initWithFormat: fmt shareContext: nil];
    if (!fullScreenContext) {
        printf ("Failed to create new NSOpenGLContext\n");
        return;
    }
    currentContext = fullScreenContext;

    /* Capture display, switch to fullscreen */
    if (CGCaptureAllDisplays() != CGDisplayNoErr) {
        printf ("CGCaptureAllDisplays() failed\n");
        return;
    }
    [fullScreenContext setFullScreen];
    [fullScreenContext makeCurrentContext];

    /* Fix ratio */
    width = CGDisplayPixelsWide (kCGDirectMainDisplay);
    height = CGDisplayPixelsHigh (kCGDirectMainDisplay);

    f_x = 1.0;
    f_y = 1.0;

    /* Update viewport, re-init textures */
    glViewport (0, 0, width, height);
    [self initTextures];

    /* Redraw the last picture */
    [self setNeedsDisplay: YES];

    isFullScreen = 1;
}

- (void) exitFullScreen {
    /* Free current OpenGL context */
    [NSOpenGLContext clearCurrentContext];
    [fullScreenContext clearDrawable];
    [fullScreenContext release];
    CGReleaseAllDisplays();

    currentContext = [self openGLContext];
    [self initTextures];
    [self reshape];

    /* Redraw the last picture */
    [self setNeedsDisplay: YES];

    isFullScreen = 0;
}

- (void) drawQuad {
    glBegin (GL_QUADS);
        /* Top left */
        glTexCoord2f (0.0, 0.0);
        glVertex2f (-f_x, f_y);
        /* Bottom left */
        glTexCoord2f (0.0, (float) height);
        glVertex2f (-f_x, -f_y);
        /* Bottom right */
        glTexCoord2f ((float) width, (float) height);
        glVertex2f (f_x, - f_y);
        /* Top right */
        glTexCoord2f ((float) width, 0.0);
        glVertex2f (f_x, f_y);
    glEnd();
}

- (void) drawRect: (NSRect) rect {
    [currentContext makeCurrentContext];

    /* Swap buffers only during the vertical retrace of the monitor.
       http://developer.apple.com/documentation/GraphicsImaging/Conceptual/OpenGL/chap5/chapter_5_section_44.html */

    long params[] = { 1 };
    CGLSetParameter (CGLGetCurrentContext(), kCGLCPSwapInterval, params);
  
    /* Black background */
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!initDone) {
        [currentContext flushBuffer];
        return;
    }

    /* Draw */
    glBindTexture (GL_TEXTURE_RECTANGLE_EXT, i_texture);
    [self drawQuad];

    /* Wait for the job to be done */
    [currentContext flushBuffer];
}

- (char *) getTextureBuffer {
    return texture_buffer;
}

- (void) setVideoSize: (int) w : (int) h {
    width = w;
    height = h;
    [self initTextures];
}

@end

