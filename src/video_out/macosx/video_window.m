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
 * Written by Daniel Mack <xine@zonque.org>
 * 
 * Most parts of this code were taken from VLC, http://www.videolan.org
 * Thanks for the good research!
 */

#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>

#import "video_window.h"

NSString *XineViewDidResizeNotification = @"XineViewDidResizeNotification";

#define DEFAULT_VIDEO_WINDOW_SIZE (NSMakeSize(320, 200))

/*
#define LOG
*/
#undef  LOG_MOUSE

@protocol XineOpenGLViewDelegate

- (void) mouseDown:(NSEvent *)theEvent inXineView:(XineOpenGLView *)theView;
- (void) mouseMoved:(NSEvent *)theEvent inXineView:(XineOpenGLView *)theView;
- (void) otherMouseDown:(NSEvent *)theEvent inXineView:(XineOpenGLView *)theView;
- (void) rightMouseDown:(NSEvent *)theEvent inXineView:(XineOpenGLView *)theView;
- (NSSize) xineViewWillResize:(NSSize)oldSize toSize:(NSSize)proposedSize;
- (void) xineViewDidResize:(NSNotification *)note;

@end

@implementation XineVideoWindow

- (void) setContentSize: (NSSize) size {
#ifdef LOG
    NSLog(@"setContent called with new size w:%d h:%d", size.width, size.height);
#endif
    [xineView setViewSizeInMainThread:size];

    [super setContentSize: size];
}

- (id) init
{
    return [self initWithContentSize:DEFAULT_VIDEO_WINDOW_SIZE];
}

- (id) initWithContentSize:(NSSize)size
{
    NSScreen *screen = [NSScreen mainScreen];
    NSSize screen_size = [screen frame].size;
    
    /* make a centered window */
    NSRect frame;
    frame.size = size;
    frame.origin.x = (screen_size.width - frame.size.width) / 2;
    frame.origin.y = (screen_size.height - frame.size.height) / 2;

    unsigned int style_mask = NSTitledWindowMask | NSMiniaturizableWindowMask |
        NSClosableWindowMask | NSResizableWindowMask;

    return ([self initWithContentRect:frame styleMask:style_mask
                              backing:NSBackingStoreBuffered defer:NO
                               screen:screen]);
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

#ifdef LOG
    NSLog(@"initWithContentRect called with rect x:%d y:%d w:%d h:%d",
          rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
#endif

    xineView = [[XineOpenGLView alloc] initWithFrame:rect];
    [xineView setResizeViewOnVideoSizeChange:YES];

    /* receive notifications about window resizing from the xine view */
    NSNotificationCenter *noticeBoard = [NSNotificationCenter defaultCenter];
    [noticeBoard addObserver:self
                    selector:@selector(xineViewDidResize:)
                        name:XineViewDidResizeNotification
                      object:xineView];

    [self setContentView: xineView];
    [self setTitle: @"xine video output"];

    return self;
}

- (void) dealloc
{
    [xineView release];
    xineView = nil;

    [super dealloc];
}


- (XineOpenGLView *) xineView {
    return xineView;
}

- (NSRect)windowWillUseStandardFrame:(NSWindow *)sender
                        defaultFrame:(NSRect)defaultFrame
{
    NSSize screen_size, video_size;
    NSRect standard_frame;

    if ([xineView isFullScreen])
        return defaultFrame;

    screen_size = defaultFrame.size;
    video_size = [xineView videoSize];

    if (screen_size.width / screen_size.height >
        video_size.width / video_size.height) {
        standard_frame.size.width  = video_size.width *
                                     (screen_size.height / video_size.height);
        standard_frame.size.height = screen_size.height;
    } else {
        standard_frame.size.width  = screen_size.width;
        standard_frame.size.height = video_size.height *
                                     (screen_size.width / video_size.width);
    }

    standard_frame.origin.x =
        (screen_size.width - standard_frame.size.width) / 2;
    standard_frame.origin.y =
        (screen_size.height - standard_frame.size.height) / 2;

    return standard_frame;
}

/* Notifications */

- (void) xineViewDidResize:(NSNotification *)note {
  NSRect frame = [self frame];
  frame.size = [[self contentView] frame].size;

  [self setFrame:[self frameRectForContentRect:frame] display:YES];
}

@end /* XineVideoWindow */


@implementation XineOpenGLView

- (void) setKeepsVideoAspectRatio:(BOOL)flag
{
    keepsVideoAspectRatio = flag;
}

- (BOOL) keepsVideoAspectRatio
{
    return keepsVideoAspectRatio;
}

- (void) setResizeViewOnVideoSizeChange:(BOOL)flag
{
    resizeViewOnVideoSizeChange = flag;
}

- (BOOL) resizeViewOnVideoSizeChange
{
    return resizeViewOnVideoSizeChange;
}

- (BOOL)mouseDownCanMoveWindow {
    return YES;
}

- (void)passEventToDelegate:(NSEvent *)theEvent withSelector:(SEL)selector
{
    NSPoint point = [self convertPoint:[theEvent locationInWindow]
                              fromView:nil];

    if (!NSMouseInRect(point, [self bounds], [self isFlipped])) return;

    if ([delegate respondsToSelector:selector]) {
        [delegate performSelector:selector
                       withObject:theEvent
                       withObject:self];
        return;
    }

    if ([_xineController respondsToSelector:selector]) {
        [_xineController performSelector:selector
                              withObject:theEvent
                              withObject:self];
        return;
    }
}

- (void)mouseMoved:(NSEvent *)theEvent
{
    [self passEventToDelegate:theEvent
                 withSelector:@selector(mouseMoved:inXineView:)];

    [super mouseMoved:theEvent];
}

- (void)mouseDown:(NSEvent *)theEvent
{
    [self passEventToDelegate:theEvent
                 withSelector:@selector(mouseDown:inXineView:)];

    [super mouseDown:theEvent];
}

- (void)rightMouseDown:(NSEvent *)theEvent
{
    [self passEventToDelegate:theEvent
                 withSelector:@selector(rightMouseDown:inXineView:)];

    [super rightMouseDown:theEvent];
}

- (void)otherMouseDown:(NSEvent *)theEvent
{
    [self passEventToDelegate:theEvent
                 withSelector:@selector(otherMouseDown:inXineView:)];

    [super otherMouseDown:theEvent];
}

- (NSSize)videoSize {
    return NSMakeSize(video_width, video_height);
}

- (void) displayTexture {
    if ([self lockFocusIfCanDraw]) {
        [self drawRect: [self bounds]];
        [self reloadTexture];
        [self unlockFocus];
    }
}

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

    self = [super initWithFrame:frame pixelFormat:fmt];

    currentContext = [self openGLContext];
    [currentContext makeCurrentContext];
    [mutex lock];
    [currentContext update];
    [mutex unlock];

    i_texture       = 0;
    initDone        = NO;
    isFullScreen    = NO;
    video_width     = frame.size.width;
    video_height    = frame.size.height;
    texture_buffer  = nil;
    mutex           = [[NSLock alloc] init];
    currentCursor   = [NSCursor arrowCursor];
    _xineController = nil;

    [self initTextures];

    /* Black background */
    glClearColor (0.0, 0.0, 0.0, 0.0);

#ifdef LOG
    NSLog(@"XineOpenGLView: initWithFrame called");
#endif

    return self;
}

- (id) initWithCoder:(NSCoder *)coder
{
#ifdef LOG
    NSLog(@"XineOpenGLView: initWithCoder called");
#endif

    self = [super initWithCoder:coder];

    self = [self initWithFrame:[self frame]];

    if ([coder allowsKeyedCoding]) {
        keepsVideoAspectRatio = [coder decodeBoolForKey:@"keepsVideoAspectRatio"];
        resizeViewOnVideoSizeChange = [coder decodeBoolForKey:
            @"resizeViewOnVideoSizeChange"];
    } else {
        /* Must decode values in the same order as encodeWithCoder: */
        [coder decodeValueOfObjCType:@encode(BOOL) at:&keepsVideoAspectRatio];
        [coder decodeValueOfObjCType:@encode(BOOL) at:&resizeViewOnVideoSizeChange];
    }

    return self;
}

- (void) encodeWithCoder:(NSCoder *)coder
{
    [super encodeWithCoder:coder];

    if ([coder allowsKeyedCoding]) {
        [coder encodeBool:keepsVideoAspectRatio forKey:@"keepsVideoAspectRatio"];
        [coder encodeBool:resizeViewOnVideoSizeChange
                   forKey:@"resizeViewOnVideoSizeChange"];
    } else {
        [coder encodeValueOfObjCType:@encode(BOOL) at:&keepsVideoAspectRatio];
        [coder encodeValueOfObjCType:@encode(BOOL) at:&resizeViewOnVideoSizeChange];
    }

}

- (void) dealloc {
    if (texture_buffer)
        free (texture_buffer);

    if (fullScreenContext) {
        [NSOpenGLContext clearCurrentContext];
        [mutex lock];
        [fullScreenContext clearDrawable];
        [fullScreenContext release];
        [mutex unlock];
        if (currentContext == fullScreenContext) currentContext = nil;
        fullScreenContext = nil;
    }

    if (currentContext) {
        [NSOpenGLContext clearCurrentContext];
        [mutex lock];
        [currentContext clearDrawable];
        [currentContext release];
        [mutex unlock];
        currentContext = nil;
    }

    [mutex dealloc];

    // Enabling the [super dealloc] below (which should be correct behaviour)
    // crashes -- not sure why ...
    //
    // [super dealloc];
    //
    // Maybe dealloc in main thread?
}

- (void) reshape {
    [mutex lock];

    if (!initDone) {
        [mutex unlock];
        return;
    }
   
    [currentContext makeCurrentContext];

    NSRect bounds = [self bounds];
    glViewport (0, 0, bounds.size.width, bounds.size.height);

    [mutex unlock];
}

- (void) setNormalSize {
    NSSize size;
    
    if (isFullScreen)
        return;
    
    size.width = video_width;
    size.height = video_height;

    [self setViewSizeInMainThread:size];
}

- (void) setHalfSize {
    NSSize size;
    
    if (isFullScreen)
        return;
    
    size.width = video_width / 2;
    size.height = video_height / 2;

    [self setViewSizeInMainThread:size];
}

- (void) setDoubleSize {
    NSSize size;
    
    if (isFullScreen)
        return;
    
    size.width = video_width * 2;
    size.height = video_height * 2;

    [self setViewSizeInMainThread:size];
}

- (void) initTextures {
    [mutex lock];

    [currentContext makeCurrentContext];

    /* Free previous texture if any */
    if (i_texture)
        glDeleteTextures (1, &i_texture);

    if (texture_buffer)
        texture_buffer = realloc (texture_buffer, sizeof (char) *
                                  video_width * video_height * 3);
    else
        texture_buffer = malloc (sizeof (char) *
                                 video_width * video_height * 3);

    /* Create textures */
    glGenTextures (1, &i_texture);

    glEnable (GL_TEXTURE_RECTANGLE_EXT);
    glEnable (GL_UNPACK_CLIENT_STORAGE_APPLE);

    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei (GL_UNPACK_ROW_LENGTH, video_width);

    glBindTexture (GL_TEXTURE_RECTANGLE_EXT, i_texture);
    glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

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
            
    glTexImage2D (GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA,
            video_width, video_height, 0,
            GL_YCBCR_422_APPLE, GL_UNSIGNED_SHORT_8_8_APPLE,
            texture_buffer);

    initDone = YES;
    [mutex unlock];
}

- (void) reloadTexture {
    if (!initDone) {
        return;
    }
    
    [mutex lock];

    [currentContext makeCurrentContext];

    glBindTexture (GL_TEXTURE_RECTANGLE_EXT, i_texture);
    glPixelStorei (GL_UNPACK_ROW_LENGTH, video_width);

    /* glTexSubImage2D is faster than glTexImage2D
     *  http://developer.apple.com/samplecode/Sample_Code/Graphics_3D/TextureRange/MainOpenGLView.m.htm
     */
    glTexSubImage2D (GL_TEXTURE_RECTANGLE_EXT, 0, 0, 0,
            video_width, video_height,
            GL_YCBCR_422_APPLE, GL_UNSIGNED_SHORT_8_8_APPLE,
            texture_buffer);

    [mutex unlock];
}

- (void) calcFullScreenAspect {
    int fs_width, fs_height, x = 0, y = 0, w = 0, h = 0;
   
    fs_width = CGDisplayPixelsWide (kCGDirectMainDisplay);
    fs_height = CGDisplayPixelsHigh (kCGDirectMainDisplay);

    switch (fullscreen_mode) {
    case XINE_FULLSCREEN_OVERSCAN:
        if (((float) fs_width / (float) fs_height) > ((float) video_width / (float) video_height)) {
            w = (float) video_width * ((float) fs_height / (float) video_height);
            h = fs_height;
            x = (fs_width - w) / 2;
            y = 0;
        } else {
            w = fs_width;
            h = (float) video_height * ((float) fs_width / (float) video_width);
            x = 0;
            y = (fs_height - h) / 2;
        }
        break;
    
    case XINE_FULLSCREEN_CROP:
        if (((float) fs_width / (float) fs_height) > ((float) video_width / (float) video_height)) {
            w = fs_width;
            h = (float) video_height * ((float) fs_width / (float) video_width);
            x = 0;
            y = (fs_height - h) / 2;
        } else {
            w = (float) video_width * ((float) fs_height / (float) video_height);
            h = fs_height;
            x = (fs_width - w) / 2;
            y = 0;
        }
        break;
    }

    printf ("MacOSX fullscreen mode: %dx%d => %dx%d @ %d,%d\n",
            video_width, video_height, w, h, x, y);

    [mutex lock];
    glViewport (x, y, w, h);
    [mutex unlock];
}

- (void) goFullScreen: (XineVideoWindowFullScreenMode) mode {
    [mutex lock];
    
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
    [mutex unlock];

    fullscreen_mode = mode;

    [self initTextures];
    [self calcFullScreenAspect];

    /* Redraw the last picture */
    [self setNeedsDisplay: YES];

    isFullScreen = YES;
}

- (void) exitFullScreen {
    initDone = NO;
    
    currentContext = [self openGLContext];
    
    /* Free current OpenGL context */
    [NSOpenGLContext clearCurrentContext];
    [mutex lock];
    [fullScreenContext clearDrawable];
    [mutex unlock];
    [fullScreenContext release];
    fullScreenContext = nil;
    CGReleaseAllDisplays();

    [self reshape];
    [self initTextures];

    /* Redraw the last picture */
    [self setNeedsDisplay: YES];

    isFullScreen = NO;
    initDone = YES;
}

- (void) drawQuad {
    float f_x = 1.0, f_y = 1.0;
    
    glBegin (GL_QUADS);
        /* Top left */
        glTexCoord2f (0.0, 0.0);
        glVertex2f (-f_x, f_y);
        /* Bottom left */
        glTexCoord2f (0.0, (float) video_height);
        glVertex2f (-f_x, -f_y);
        /* Bottom right */
        glTexCoord2f ((float) video_width, (float) video_height);
        glVertex2f (f_x, -f_y);
        /* Top right */
        glTexCoord2f ((float) video_width, 0.0);
        glVertex2f (f_x, f_y);
    glEnd();
}

- (void) drawRect: (NSRect) rect {
    [currentContext makeCurrentContext];
    
    if (!initDone) {
        return;
    }

    [mutex lock];

    /* Swap buffers only during the vertical retrace of the monitor.
       http://developer.apple.com/documentation/GraphicsImaging/Conceptual/OpenGL/chap5/chapter_5_section_44.html */

    long params[] = { 1 };
    CGLSetParameter (CGLGetCurrentContext(), kCGLCPSwapInterval, params);
  
    /* Black background */
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Draw */
    glBindTexture (GL_TEXTURE_RECTANGLE_EXT, i_texture);
    [self drawQuad];

    /* Wait for the job to be done */
    [currentContext flushBuffer];

    [mutex unlock];
}

- (char *) getTextureBuffer {
    return texture_buffer;
}

- (void) setVideoSize:(NSSize)size
{
    video_width = size.width;
    video_height = size.height;

    if (resizeViewOnVideoSizeChange)
	[self setViewSizeInMainThread:size];

#ifdef LOG
    NSLog(@"setVideoSize called");
#endif

    [self initTextures];
}

- (void) setViewSizeInMainThread:(NSSize)size
{
    /* create an autorelease pool, since we're running in a xine thread that
     * may not have a pool of its own */
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    NSValue *sizeWrapper = [NSValue valueWithBytes:&size
                                          objCType:@encode(NSSize)];
    
    [self performSelectorOnMainThread:@selector(setViewSize:)
                           withObject:sizeWrapper
                           waitUntilDone:NO];

#ifdef LOG
    NSLog(@"setViewSizeInMainThread called");
#endif

    [pool release];
}

- (void) setViewSize:(NSValue *)sizeWrapper
{
    NSSize proposedSize, newSize, currentSize;

    [sizeWrapper getValue:&proposedSize];
    newSize = proposedSize;

    currentSize = [self frame].size;
    if (proposedSize.width == currentSize.width &&
        proposedSize.height == currentSize.height) {
        return;
    }

    /* If our controller handles xineViewWillResize:toSize:, send the
     * message to him first.  Note that the delegate still has a chance
     * to override the controller's resize preference ... */
    if ([_xineController respondsToSelector:@selector(xineViewWillResize:toSize:)]) {
        NSSize oldSize = [self frame].size;
        newSize = [_xineController xineViewWillResize:oldSize toSize:proposedSize];
    }

    /* If our delegate handles xineViewWillResize:toSize:, send the
     * message to him; otherwise, just resize ourselves */
    if ([delegate respondsToSelector:@selector(xineViewWillResize:toSize:)]) {
        NSSize oldSize = [self frame].size;
        newSize = [delegate xineViewWillResize:oldSize toSize:proposedSize];
    }

    [self setFrameSize:newSize];
    [self setBoundsSize:newSize];

    /* Post a notification that we resized and also notify our controller */
    /* and delegate */
    NSNotification *note =
        [NSNotification notificationWithName:XineViewDidResizeNotification
                                      object:self];
    [[NSNotificationCenter defaultCenter] postNotification:note];

    if ([_xineController respondsToSelector:@selector(xineViewDidResize:)]) {
        [_xineController xineViewDidResize:note];
    }

    if ([delegate respondsToSelector:@selector(xineViewDidResize:)]) {
        [delegate xineViewDidResize:note];
    }

    if (isFullScreen)
        [self calcFullScreenAspect];

    [self initTextures];
}

- (BOOL) isFullScreen {
    return isFullScreen;
}

- (id) delegate {
    return delegate;
}

- (void) setDelegate:(id)aDelegate {
    delegate = aDelegate;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void) setCurrentCursor:(NSCursor *)cursor
{
    currentCursor = cursor;
    [self resetCursorRectsInMainThread];
}

- (NSCursor *) currentCursor
{
    return currentCursor;
}

- (void) resetCursorRectsInMainThread
{
    [self discardCursorRects];
    [self performSelectorOnMainThread:@selector(resetCursorRects)
                           withObject:nil
                        waitUntilDone:NO];
}

- (void) resetCursorRects
{
    [self addCursorRect:[self visibleRect] cursor:currentCursor];
    [currentCursor set];
}

- (void) setXineController:(id)controller
{
    _xineController = controller;
}

- (id) xineController
{
    return _xineController;
}

@end /* XineOpenGLView */


@implementation NSWindow (AspectRatioAdditions)

- (void) setKeepsAspectRatio: (BOOL) flag {
    if (flag) {
        NSSize size = [self frame].size;
        [self setAspectRatio:size];
    }
    else {
        [self setResizeIncrements:NSMakeSize(1.0, 1.0)];
    }
}

/* XXX: This is 100% untested ... */
- (BOOL) keepsAspectRatio {
    NSSize size = [self aspectRatio];
    if (size.width == 0 && size.height == 0)
        return false;
    else
        return true;
}

@end /* NSWindow (AspectRatioAdditions) */

