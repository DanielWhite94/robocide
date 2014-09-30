#include "depth.h"

bool depthIsValid(Depth depth)
{
  return (depth>=0 && depth<DepthMax);
}
