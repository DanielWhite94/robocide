#ifndef DEPTH_H
#define DEPTH_H

#include <stdbool.h>

typedef enum
{
  DepthMax=128,
  DepthInvalid=255
}Depth;
#define DepthBit 8 // Number of bits Depth actually uses.

bool depthIsValid(Depth depth);

#endif
