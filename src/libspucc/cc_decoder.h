typedef struct cc_decoder_s cc_decoder_t;

#define CC_FONT_MAX 256

typedef struct cc_confvar_s {
  int cc_enabled;             /* true if closed captions are enabled */
  char font[CC_FONT_MAX + 1];          /* standard captioning font & size */
  int font_size;
  char italic_font[CC_FONT_MAX + 1];   /* italic captioning font & size */
  int center;                 /* true if captions should be centered */
                              /* according to text width */

  /* the following variables are not controlled by configuration files; they */
  /* are intrinsic to the properties of the configuration options and the */
  /* currently played video */
  int x;                      /* coordinates of the captioning area */
  int y;
  int width;
  int height;
  int max_char_height;        /* captioning font properties */
  int max_char_width;
  int video_width;            /* video dimensions */
  int video_height;
  int can_cc;                 /* true if captions can be displayed */
                              /* (i.e., font fits on screen) */

  cc_decoder_t *decoder;      /* back pointer to decoder (necessary for */
                              /* sending some messages after config changes) */
} cc_confvar_t;


typedef struct cc_config_s {
  cc_confvar_t vars;
  pthread_mutex_t cc_mutex;  
} cc_config_t;


cc_decoder_t *cc_decoder_open(osd_renderer_t *renderer, metronom_t *metronom,
                              config_values_t *cfg, cc_config_t *cc_cfg);
void cc_decoder_close(cc_decoder_t *this_obj);
void cc_decoder_init(config_values_t *cfg, cc_config_t *cc_cfg);
void cc_notify_frame_change(cc_decoder_t *this, int width, int height);
void decode_cc(cc_decoder_t *this, uint8_t *buffer, uint32_t buf_len,
	       uint32_t pts, uint32_t scr);
