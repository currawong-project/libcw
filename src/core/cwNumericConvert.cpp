//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwTest.h"
#include "cwLex.h"
#include "cwText.h"
#include "cwTest.h"
#include "cwNumericConvert.h"


cw::rc_t cw::numericConvertTest( const test::test_args_t& args )
{
  int8_t x0 = 3;
  int x1 = 127;
  
  cw::numeric_convert( x1, x0 );
  cwLogPrint("%i %i\n",x0,x1);
    

  int v0 = -1;
  double v1 = -1;
  cw::string_to_number("123",v0);
  cw::string_to_number("3.4",v1);
  cwLogPrint("%i %f\n",v0,v1 );

  return cw::kOkRC;
}

