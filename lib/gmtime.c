#include <time.h>
#include <stdlib.h>

time_t _xine_private_gmtime(struct tm *tm) {
  time_t ret;
  char *tz;

  tz = getenv("TZ");
  setenv("TZ", "", 1);
  tzet();
  ret = mktime(tm);
  if (tz) setenv("TZ", tz, 1);
  else unsetenv("TZ");
  tzset();

  return ret;
}
