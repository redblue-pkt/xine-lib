typedef struct cc_decoder_s cc_decoder_t;
typedef struct cc_renderer_s cc_renderer_t;

#define CC_FONT_MAX 256

typedef struct cc_config_s {
  int cc_enabled;             /* true if closed captions are enabled */
  char font[CC_FONT_MAX];     /* standard captioning font & size */
  int font_size;
  char italic_font[CC_FONT_MAX];   /* italic captioning font & size */
  int center;                 /* true if captions should be centered */
                              /* according to text width */

  /* the following variables are not controlled by configuration files; they */
  /* are intrinsic to the properties of the configuration options and the */
  /* currently played video */
  int can_cc;                 /* true if captions can be displayed */
                              /* (e.g., font fits on screen) */

  cc_renderer_t *renderer;    /* closed captioning renderer */
} cc_config_t;

cc_decoder_t *cc_decoder_open(cc_config_t *cc_cfg);
void cc_decoder_close(cc_decoder_t *this_obj);
void cc_decoder_init(void);

void decode_cc(cc_decoder_t *this, uint8_t *buffer, uint32_t buf_len,
	       uint32_t pts, uint32_t scr);

/* Instantiates a new closed captioning renderer. */
cc_renderer_t *cc_renderer_open(osd_renderer_t *osd_renderer,
				metronom_t *metronom, cc_config_t *cc_cfg,
				int video_width, int video_height);

/* Destroys a closed captioning renderer. */
void cc_renderer_close(cc_renderer_t *this_obj);

/* Updates the renderer configuration variables */
void cc_renderer_update_cfg(cc_renderer_t *this_obj, int video_width,
			    int video_height);
