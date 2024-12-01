//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTest.h"
#include "cwObject.h"
#include "cwVectOps.h"
#include "cwMtx.h"


namespace cw
{
  namespace mtx
  {
    bool _mtx_object_is_list(  const object_t* cfg, unsigned& dimN )
    {
      if( cfg->is_list() )
      {
        dimN += 1;
        return true;
      }

      return false;      
    }

   unsigned _mtx_object_get_degree(  const object_t* cfg )
   {
     unsigned dimN = 0;
     const object_t* o = cfg;
     while( _mtx_object_is_list(o,dimN) )
       o = o->child_ele(0);

     return dimN;
   }

    rc_t _mtx_object_get_shape( const object_t* cfg, unsigned i,  unsigned* dimV, unsigned dimN, unsigned& eleN )
    {
      rc_t rc = kOkRC;
      
      if( !cfg->is_list() )
        return kOkRC;

      dimV[i] = cfg->child_count();

      eleN = eleN == 0 ? dimV[i] : eleN * dimV[i];

      if((rc = _mtx_object_get_shape(cfg->child_ele(0), i+1, dimV, dimN, eleN )) != kOkRC )
        return rc;
      
      if( cfg->child_ele(0)->is_list() )
      {
        unsigned ch0 = cfg->child_ele(0)->child_count(); 
        for(unsigned j=1; j<dimV[i]; ++j)
          if( ch0 != cfg->child_ele(j)->child_count())
            return cwLogError(kSyntaxErrorRC,"A matrix contains an inconsistent dimension length on dimension index %i",i+1);

        
      }
      
      return rc;
    }


    unsigned _offsetMulV( const unsigned* mulV, unsigned dimN, unsigned* idxV )
    {
      unsigned n = 0;
      for(unsigned i=0; i<dimN; ++i)
        n += idxV[i] * mulV[i];
      return n;
    }
    
    unsigned _offsetDimV( const unsigned* dimV, unsigned dimN, unsigned* idxV )
    {
      unsigned n = idxV[0];
      for(unsigned i=1; i<dimN; ++i)
      {
        unsigned m = idxV[i];
        for(int j=i-1; j>=0; --j)
          m *= dimV[j];
        n += m;
      }

      return n;
    }

  }
}




cw::rc_t cw::mtx::test( const test::test_args_t& args )
{
  rc_t rc = kOkRC;
  const object_t* cfg = args.test_args;

  d_t* mtx0 = nullptr;
  d_t* mtx1 = nullptr;
  d_t* mtx2 = nullptr;
  d_t* mtx3 = nullptr;
  d_t* mtx4 = nullptr;
  d_t* mtx_y0   = nullptr;
  d_t* mtx_y1   = nullptr;
  d_t* mtx_y2   = nullptr;
  d_t* mtx_y3   = nullptr;
  d_t* mtx_y4   = nullptr;
  d_t* mtx_y5   = nullptr;
  d_t  y;
  
  const object_t* m0 = cfg->find("m0");
  if( m0 != nullptr )
    mtx0 = allocCfg<double>(m0);

  const object_t* m1 = cfg->find("m1");
  if( m1 != nullptr )
    mtx1 = allocCfg<double>(m1);

  const object_t* m2 = cfg->find("m2");
  if( m2 != nullptr )
    mtx2 = allocCfg<double>(m2);

  const object_t* m3 = cfg->find("m3");
  if( m3 != nullptr )
    mtx3 = allocCfg<double>(m3);

  const object_t* m4 = cfg->find("m4");
  if( m4 != nullptr )
    mtx4 = allocCfg<double>(m4);
  
  const object_t* y0 = cfg->find("y0");
  if( y0 != nullptr )
    mtx_y0 = allocCfg<double>(y0);

  const object_t* y1 = cfg->find("y1");
  if( y1 != nullptr )
    mtx_y1 = allocCfg<double>(y1);
  
  unsigned n = offset(*mtx1,1,1);
  cwLogPrint("offset: %i\n",n);
  
  
  report(*mtx0,"m0");
  report(*mtx1,"m1");
  report(*mtx2,"m2");
  report(*mtx3,"m3");
  report(*mtx4,"m4");
  report(*mtx_y0,"y0");
  report(*mtx_y1,"y1");

  
  if( mtx_mul(y,*mtx1,*mtx0) == kOkRC )
  {
    report(y,"y0");
    if( !is_equal(*mtx_y0,y) )
      rc = cwLogError(kTestFailRC,"Test 0 fail.");
  }


  transpose(*mtx0);
  transpose(*mtx1);
  
  if( mtx_mul(y,*mtx1,*mtx0) == kOkRC )
  {
    report(y,"y1");
    if( !is_equal(*mtx_y1,y) )
      rc = cwLogError(kTestFailRC,"Test 1 fail.");
  }


  
  transpose(*mtx0);
  report(*mtx0,"m0");
  mtx_y2 = join(0,*mtx0,*mtx4);
  if( mtx_y2 != nullptr )
    report(*mtx_y2,"y2");

  report(*mtx0,"m0");
  report(*mtx4,"m4");
  mtx_y3 = join(1,*mtx0,*mtx4);
  if( mtx_y3 != nullptr )
    report(*mtx_y3,"y3");


  mtx_y4 = slice_alias(*mtx_y3,0,0,1);
  if( mtx_y4 != nullptr )
  {
    report(*mtx_y4,"y4 - slice");

    ele(*mtx_y4,2) = 1;
    ele(*mtx_y4,3) = 2;
    report(*mtx_y4,"y4 - mod");
  
  
    mtx_y5 = alloc_one_hot(*mtx_y4);
    if( mtx_y5 != nullptr )
      report(*mtx_y5,"y5 -(one_hot(y4))");
  }
  
  release(mtx0);
  release(mtx1);
  release(mtx2);
  release(mtx3);
  release(mtx4);
  release(mtx_y0);
  release(mtx_y1);
  release(mtx_y2);
  release(mtx_y3);
  release(mtx_y4);
  release(mtx_y5);
  release(y);
  return rc;
}
