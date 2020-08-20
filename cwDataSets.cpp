#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwFileSys.h"
#include "cwMtx.h"
#include "cwDataSets.h"
#include "cwSvg.h"


namespace cw
{
  namespace dataset
  {
    namespace mnist
    {
      typedef struct mnist_str
      {
        mtx::fmtx_t* train = nullptr;
        mtx::fmtx_t* valid = nullptr;
        mtx::fmtx_t* test  = nullptr;
        
      } mnist_t;

      inline mnist_t* _handleToPtr(handle_t h )
      { return handleToPtr<handle_t,mnist_t>(h); }

      rc_t _destroy( mnist_t* p )
      {
        rc_t rc = kOkRC;

        mtx::release(p->train);
        mtx::release(p->valid);
        mtx::release(p->test);
        mem::release(p);
        return rc;
      }

      rc_t _read_file( const char* dir, const char* fn, mtx::fmtx_t*& m )
      {
        rc_t           rc       = kOkRC;
        file::handle_t fH;
        unsigned       exampleN = 0;
        const unsigned kPixN    = 784;
        const unsigned kRowN    = kPixN+1;
        unsigned       dimV[]   = {kRowN,0};
        const unsigned dimN     = sizeof(dimV)/sizeof(dimV[0]);        
        float*         v        = nullptr;
        char*          path     = filesys::makeFn(dir, fn, ".bin", NULL );

        // open the file
        if((rc = file::open(fH,path, file::kReadFl | file::kBinaryFl )) != kOkRC )
        {
          rc = cwLogError(rc,"MNIST file open failed on '%s'.",cwStringNullGuard(path));
          goto errLabel;
        }
        
        // read the count of examples
        if((rc = readUInt(fH,&exampleN)) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to read MNIST example count.");
          goto errLabel;
        }

        // allocate the data memory
        v = mem::alloc<float>( kRowN * exampleN );

        // read each example
        for(unsigned i=0,j=0; i<exampleN; ++i,j+=kRowN)
        {
          unsigned digitLabel;

          // read the digit image label
          if((rc = readUInt(fH,&digitLabel)) != kOkRC )
          {
            rc = cwLogError(rc,"Unable to read MNIST label on example %i.",i);
            goto errLabel;
          }

          v[j] = digitLabel;

          // read the image pixels
          if((rc = readFloat(fH,v+j+1,kPixN)) != kOkRC )
          {
            rc = cwLogError(rc,"Unable to read MNIST data vector on example %i.",i);
            goto errLabel;
          }

        }

        dimV[1] = exampleN;
        m = mtx::alloc<float>( dimN, dimV, v, mtx::kAliasReleaseFl );

      errLabel:
        file::close(fH);
        mem::release(path);
        return rc;        
      }
    }
  }
}


cw::rc_t cw::dataset::mnist::create( handle_t& h, const char* dir )
{
  rc_t           rc;
  mnist_t*       p = nullptr;
  
  if((rc = destroy(h)) != kOkRC )
    return rc;

  p = mem::allocZ<mnist_t>(1);

  // read the training data
  if((rc = _read_file( dir, "mnist_train", p->train )) != kOkRC )
  {
    rc = cwLogError(rc,"MNIST training set load failed.");
    goto errLabel;
  }

  // read the validation data
  if((rc = _read_file( dir, "mnist_valid", p->valid )) != kOkRC )
  {
    rc = cwLogError(rc,"MNIST validation set load failed.");
    goto errLabel;
  }

  // read the testing data
  if((rc = _read_file( dir, "mnist_test", p->test )) != kOkRC )
  {
    rc = cwLogError(rc,"MNIST test set load failed.");
    goto errLabel;    
  }

  h.set(p);

 errLabel:
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::dataset::mnist::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  if( !h.isValid())
    return rc;

  mnist_t* p = _handleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;  
}

const cw::mtx::fmtx_t* cw::dataset::mnist::train(    handle_t h )
{
  mnist_t* p = _handleToPtr(h);
  return p->train;
}

const cw::mtx::fmtx_t* cw::dataset::mnist::validate( handle_t h )
{
  mnist_t* p = _handleToPtr(h);
  return p->valid;
}

const cw::mtx::fmtx_t* cw::dataset::mnist::test(     handle_t h )
{
  mnist_t* p = _handleToPtr(h);
  return p->test;
}



cw::rc_t cw::dataset::mnist::test( const char* dir, const char* imageFn )
{
  rc_t rc = kOkRC;
  handle_t h;
  if((rc = create(h, dir )) == kOkRC )
  {
    svg::handle_t svgH;

    if((rc = svg::create(svgH)) != kOkRC )
      rc = cwLogError(rc,"SVG Test failed on create.");
    else
    {
      const mtx::fmtx_t* m = train(h);
      /*
      unsigned zn = 0;
      unsigned i = 1;
      for(; i<m->dimV[1]; ++i)
      {
        const float* v0 = m->base + (28*28+1) * (i-1) + 1;
        const float* v1 = m->base + (28*28+1) * (i-0) + 1;
        float d = 0;
        
        for(unsigned j=0; j<28*28; ++j)
          d += fabs(v0[j]-v1[j]);

        if( d==0 )
          ++zn;
        else
        {
          printf("%i %i %f\n",i,zn,d);
          zn = 0;
        }
      }

      printf("i:%i n:%i zn:%i\n",i,m->dimV[1],zn);
      */
      
      for(unsigned i=0; i<10; ++i)
      {
        svg::offset(svgH, 0, i*30*5 );
        svg::image(svgH, m->base + (28*28+1)*i, 28, 28, 5, svg::kInvGrayScaleColorMapId);
      }
      
      svg::write(svgH, imageFn, nullptr, svg::kStandAloneFl | svg::kGenInlineStyleFl, 10,10,10,10);
      
      
      svg::destroy(svgH);
    }
    
    rc = destroy(h);
  }

  return rc;
}


