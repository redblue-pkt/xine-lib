
#ifndef HAVE_YUV2RGB_H
#define HAVE_YUV2RGB_h

#include <inttypes.h>

/*
 * modes supported - feel free to implement yours
 */

#define MODE_15_RGB 1
#define MODE_15_BGR 2
#define MODE_16_RGB 3
#define MODE_16_BGR 4
#define MODE_24_RGB 5
#define MODE_24_BGR 6
#define MODE_32_RGB 7
#define MODE_32_BGR 8

typedef struct yuv2rgb_s yuv2rgb_t;

struct yuv2rgb_s {

  /* 
   * this is the function to call for the yuv2rgb and scaling process
   */
  void (*yuv2rgb_fun) (yuv2rgb_t *this, uint8_t * image, uint8_t * py,
		       uint8_t * pu, uint8_t * pv) ;

  /* private stuff below */

  uint32_t      matrix_coefficients;
  int           source_width, source_height;
  int           y_stride, uv_stride;
  int           dest_width, dest_height;
  int           rgb_stride;
  int           step_dx, step_dy;
  int           do_scale;
  uint8_t      *y_buffer, *y_chunk;
  uint8_t      *u_buffer, *u_chunk;
  uint8_t      *v_buffer, *v_chunk;

  void         *table_rV[256];
  void         *table_gU[256];
  int           table_gV[256];
  void         *table_bU[256];

  void (* yuv2rgb_c_internal) (yuv2rgb_t *this,
			       uint8_t *, uint8_t *,
			       uint8_t *, uint8_t *,
			       void *, void *, int);
} ;


/* call once on startup */
yuv2rgb_t *yuv2rgb_init (int mode);

/*
 * set up yuv2rgb function, determine scaling parameters if necessary
 * returns 0 on failure, 1 otherwise
 */
int yuv2rgb_setup (yuv2rgb_t *this, 
		   int source_width, int source_height,
		   int y_stride, int uv_stride,
		   int dest_width, int dest_height,
		   int rgb_stride);

/*
 * internal stuff below this line
 */

void yuv2rgb_init_mmxext (yuv2rgb_t *this, int mode);
void yuv2rgb_init_mmx (yuv2rgb_t *this, int mode);

/*
void Color565DitherYV12MMX1X(unsigned char *lum, unsigned char *cr,
                             unsigned char *cb, unsigned char *out,
                             int rows, int cols, int mod );
*/

#endif
