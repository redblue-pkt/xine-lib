/* dummy file to use xine mm_support function */

#include "xineutils.h"
#include "../dsputil.h"


/* Function to test if multimedia instructions are supported...  */
int mm_support(void)
{
  return xine_mm_accel();
}

#ifdef __TEST__
int main ( void )
{
  int mm_flags;
  mm_flags = mm_support();
  printf("mm_support = 0x%08u\n",mm_flags);
  return 0;
}
#endif
