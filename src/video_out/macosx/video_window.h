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
 */

#include <Cocoa/Cocoa.h>

@interface XineOpenGLView : NSOpenGLView {
        int               width, height;
        char             *texture_buffer;
        unsigned long     i_texture;
        float             f_x;
        float             f_y;
        int               initDone;
        int               isFullScreen;
	NSOpenGLContext * opengl_context;
        NSOpenGLContext * fullScreenContext;
        NSOpenGLContext * currentContext;
}

- (void) drawQuad;
- (void) drawRect: (NSRect) rect;
- (void) goFullScreen;
- (void) exitFullScreen;
- (void) reshape;
- (void) initTextures;
- (void) reloadTexture;
- (id) initWithFrame: (NSRect) frame;
- (char *) getTextureBuffer;
- (void) setVideoSize: (int) w: (int) h;

@end


@interface XineVideoWindow : NSWindow {
	int               width, height;
	XineOpenGLView   *openGLView;
}

- (void) setContentSize: (NSSize) size;
- (void) displayTexture;
- (XineOpenGLView *) getGLView;
- (void) goFullScreen;
- (void) exitFullScreen;

@end
