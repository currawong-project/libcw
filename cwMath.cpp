#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMath.h"

unsigned cw::math::randUInt( unsigned minVal, unsigned maxVal )
{
  return std::max(minVal,std::min(maxVal,minVal + (unsigned)round(((maxVal - minVal) * rand())/RAND_MAX)));
}
