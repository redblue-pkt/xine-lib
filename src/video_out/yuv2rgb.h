
#ifndef HAVE_YUV2RGB_H
#define HAVE_YUV2RGB_h

#include <inttypes.h>


/* internal function use to scale yuv data */
typedef void (*scale_line_func_t) (uint8_t *source, uint8_t *dest,
				   int width, int step);


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
#define MODE_PALETTE 9

typedef struct yuv2rgb_s yuv2rgb_t;

struct yuv2rgb_s {

  /* 
   * this is the function to call for the yuv2rgb and scaling process
   */
  void (*yuv2rgb_fun) (yuv2rgb_t *this, uint8_t * image, uint8_t * py,
		       uint8_t * pu, uint8_t * pv) ;

  /* 
   * this is the function to call for the yuy2->rgb and scaling process
   */
  void (*yuy22rgb_fun) (yuv2rgb_t *this, uint8_t * image, uint8_t * p);

  /* private stuff below */

  uint32_t      matrix_coefficients;
  int           source_width, source_height;
  int           y_stride, uv_stride;
  int           dest_width, dest_height;
  int           rgb_stride;
  int           step_dx, step_dy;
  int           do_scale;
  uint8_t      *y_buffer;
  uint8_t      *u_buffer;
  uint8_t      *v_buffer;
  void	       *y_chunk;
  void	       *u_chunk;
  void	       *v_chunk;

  void         *table_rV[256];
  void         *table_gU[256];
  int           table_gV[256];
  void         *table_bU[256];

  scale_line_func_t scale_line;
} ;


/* call once on startup */
yuv2rgb_t *yuv2rgb_init (int mode, int swapped);

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

void yuv2rgb_init_mmxext (yuv2rgb_t *this, int mode, int swapped);
void yuv2rgb_init_mmx (yuv2rgb_t *this, int mode, int swapped);
void yuv2rgb_init_mlib (yuv2rgb_t *this, int mode, int swapped);

/*
void Color565DitherYV12MMX1X(unsigned char *lum, unsigned char *cr,
                             unsigned char *cb, unsigned char *out,
                             int rows, int cols, int mod );
*/

#endif
