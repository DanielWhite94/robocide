#include <stdlib.h>
#include <sys/time.h>

#include "time.h"

const TimeMs TimeMsInvalid=~((TimeMs)0);

TimeMs timeGet()
{
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return tp.tv_sec*1000llu+tp.tv_usec/1000llu;
}
