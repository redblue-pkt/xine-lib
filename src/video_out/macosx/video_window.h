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
 */

#include <Cocoa/Cocoa.h>

typedef enum {
    XINE_FULLSCREEN_OVERSCAN,
    XINE_FULLSCREEN_CROP
} XineVideoWindowFullScreenMode;


@interface XineOpenGLView : NSOpenGLView {
    IBOutlet id                    delegate;
    int                            video_width, video_height;
    char *                         texture_buffer;
    unsigned long                  i_texture;
    BOOL                           initDone;
    BOOL                           isFullScreen;
    XineVideoWindowFullScreenMode  fullscreen_mode;
    NSOpenGLContext *              opengl_context;
    NSOpenGLContext *              fullScreenContext;
    NSOpenGLContext *              currentContext;
}

- (void) displayTexture;
- (void) drawQuad;
- (void) drawRect: (NSRect) rect;
- (void) goFullScreen: (XineVideoWindowFullScreenMode) mode;
- (void) exitFullScreen;
- (BOOL) isFullScreen;
- (void) reshape;
- (void) initTextures;
- (void) reloadTexture;
- (char *) getTextureBuffer;
- (void) setVideoSize:(NSSize)size;
- (void) setViewSizeInMainThread:(NSSize)size;
- (void) setNormalSize;
- (void) setHalfSize;
- (void) setDoubleSize;
- (NSSize) videoSize;

/* Delegate methods */
- (id) delegate;
- (void) setDelegate:(id)aDelegate;

@end


@interface XineVideoWindow : NSWindow {
    int               width, height;
    BOOL              keepAspectRatio;
    XineOpenGLView   *openGLView;
}

- (void) setContentSize: (NSSize) size;
- (void) displayTexture;
- (XineOpenGLView *) getGLView;
- (void) fitToScreen;
- (void) setKeepsAspectRatio: (BOOL) i;
- (int) keepsAspectRatio;
@end


/* XineOpenGLView delegate methods */

@interface NSObject (XineOpenGLViewDelegate)
    
- (NSSize)xineViewWillResize:(NSSize)previousSize
                      toSize:(NSSize)proposedFrameSize;
- (void)xineViewDidResize:(NSNotification *)aNotification;

@end


/* XineOpenGLView notifications */

extern NSString *XineViewDidResizeNotification;

