#ifndef X11OSD_H
#define X11OSD_H

  typedef struct x11osd x11osd;

  x11osd *x11osd_create (Display *display, int screen, Window window);

  void x11osd_destroy (x11osd * osd);

  void x11osd_expose (x11osd * osd);

  void x11osd_resize (x11osd * osd, int width, int height);

  void x11osd_drawable_changed (x11osd * osd, Window window);

  void x11osd_clear(x11osd *osd);

  void x11osd_blend(x11osd *osd, vo_overlay_t *overlay);

#endif
