#ifndef __OSD_H__
#define __OSD_H__

#ifdef __OSD_C__

typedef struct osd_object_s osd_object_t;
typedef struct osd_renderer_s osd_renderer_t;
typedef struct osd_fontchar_s osd_fontchar_t;
typedef struct osd_font_s osd_font_t;

struct osd_object_s {
  osd_object_t *next;
  osd_renderer_t *renderer;

  int width, height;    /* work area dimentions */
  uint8_t *area;        /* work area */
  int display_x,display_y;  /* where to display it in screen */
  
  /* clipping box inside work area */
  int x1, y1;
  int x2, y2;
  
  uint32_t color[16];	/* color lookup table  */
  uint8_t trans[16];	/* mixer key table */

  int32_t handle;
  
  osd_font_t *font;
};


struct osd_renderer_s {
  pthread_mutex_t osd_mutex;
  video_overlay_instance_t *video_overlay;
  video_overlay_event_t event;
  osd_object_t *osds;
  osd_font_t *fonts;
};

struct osd_fontchar_s {
  uint16_t code;
  uint16_t width;
  uint16_t height;
  uint8_t *bmp;
};

struct osd_font_s {
  char name[40];
  uint16_t version;
  uint16_t num_fontchars;
  osd_fontchar_t *fontchar;
  osd_font_t *next;
};

#else

typedef void osd_object_t;
typedef void osd_renderer_t;

#endif

/*
   initialize the osd rendering engine
*/
osd_renderer_t *osd_renderer_init( video_overlay_instance_t *video_overlay );

/* close osd rendering engine
   loaded fonts are unloaded
   osd objects are closed
*/
void osd_renderer_exit( osd_renderer_t *this );

/*
   open a new osd object. this will allocated an empty (all zero) drawing
   area where graphic primitives may be used.
   It is ok to specify big width and height values. The render will keep
   track of the smallest changed area to not generate too big overlays.
   A default palette is initialized (i sugest keeping color 0 as transparent
   for the sake of simplicity)
*/
osd_object_t *osd_open(osd_renderer_t *this, int width, int height);


/*
   free osd object
*/
void osd_close(osd_object_t *osd_to_close);


/*
   send the osd to be displayed at given pts (0=now)
   the object is not changed. there may be subsequent drawing  on it.
*/
int osd_show(osd_object_t *osd, uint32_t vpts );

/*
   send event to hide osd at given pts (0=now)
   the object is not changed. there may be subsequent drawing  on it.
*/
int osd_hide(osd_object_t *osd, uint32_t vpts );

/*
   Bresenham line implementation on osd object
*/
void osd_line(osd_object_t *osd,
                int x1, int y1, int x2, int y2, int color );

/*
   filled retangle
*/
void osd_filled_rect(osd_object_t *osd,
                int x1, int y1, int x2, int y2, int color );

/*
   set palette (color and transparency)
*/
void osd_set_palette(osd_object_t *osd, uint32_t *color, uint8_t *trans );

/*
   get palette (color and transparency)
*/
void osd_get_palette(osd_object_t *osd, uint32_t *color, uint8_t *trans );

/*
   set position were overlay will be blended
*/
void osd_set_position(osd_object_t *osd, int x, int y );

/*
   load bitmap font into osd engine
*/
char * osd_renderer_load_font(osd_renderer_t *this, char *name);

/*
   unload font
*/
int osd_renderer_unload_font(osd_renderer_t *this, char *name );

/*
  set the font of osd object
*/

int osd_set_font( osd_object_t *osd, char *fontname );


/*
  render text on x,y position (8 bits version)
  no \n yet
*/
int osd_render_text( osd_object_t *osd, int x1, int y1, char *text );

/*
  get width and height of how text will be renderized
*/
int osd_get_text_size( osd_object_t *osd, char *text, int *width, int *height );


#endif

