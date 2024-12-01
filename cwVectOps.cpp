//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwVectOps.h"

cw::rc_t cw::vop::test( const test::test_args_t& args )
{
  int v1[] = { 1,2,1,2,1,2,1,2,1,2 };
  int v0[ 10 ];
  
  cw::vop::deinterleave( v0, v1, 5, 2 );
  cw::vop::print(v0,10,"%i ");
  return cw::kOkRC;
}
