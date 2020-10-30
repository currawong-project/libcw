#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwFile.h"
#include "cwFileSys.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwDataSets.h"
#include "cwSvg.h"
#include "cwTime.h"

namespace cw
{
  namespace dataset
  {
    namespace mnist
    {
      typedef struct mnist_str
      {
        mtx::f_t* train = nullptr;
        mtx::f_t* valid = nullptr;
        mtx::f_t* test  = nullptr;
        
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

      rc_t _read_file( const char* dir, const char* fn, mtx::f_t*& m )
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
        m = mtx::alloc<float>( dimV, dimN, v, mtx::kAliasReleaseFl );

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

  char* inDir = filesys::expandPath(dir);

  p = mem::allocZ<mnist_t>(1);

  // read the training data
  if((rc = _read_file( inDir, "mnist_train", p->train )) != kOkRC )
  {
    rc = cwLogError(rc,"MNIST training set load failed.");
    goto errLabel;
  }

  // read the validation data
  if((rc = _read_file( inDir, "mnist_valid", p->valid )) != kOkRC )
  {
    rc = cwLogError(rc,"MNIST validation set load failed.");
    goto errLabel;
  }

  // read the testing data
  if((rc = _read_file( inDir, "mnist_test", p->test )) != kOkRC )
  {
    rc = cwLogError(rc,"MNIST test set load failed.");
    goto errLabel;    
  }

  h.set(p);

 errLabel:
  if( rc != kOkRC )
    _destroy(p);

  mem::release(inDir);
  
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

const cw::mtx::f_t* cw::dataset::mnist::train(    handle_t h )
{
  mnist_t* p = _handleToPtr(h);
  return p->train;
}

const cw::mtx::f_t* cw::dataset::mnist::validate( handle_t h )
{
  mnist_t* p = _handleToPtr(h);
  return p->valid;
}

const cw::mtx::f_t* cw::dataset::mnist::test(     handle_t h )
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
      const mtx::f_t* m = train(h);
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


namespace cw
{
  namespace dataset
  {

    //---------------------------------------------------------------------------------------------------------------
    // struct matrix_str<T>
    //
    
    template< typename T >
      struct matrix_str
    {
      struct mtx::mtx_str<T>* dataM;
      struct mtx::mtx_str<T>* labelM;
    };

    template< typename T0, typename T1 >
      void _matrix_load( struct matrix_str<T0>& m, const struct mtx::mtx_str<T1>& dataM, const struct mtx::mtx_str<T1>& labelM )
    {
      m.dataM  = mtx::alloc<T0,T1>(dataM,nullptr,nullptr);
      m.labelM = mtx::alloc<T0,T1>(labelM,nullptr,nullptr);
    }

    template< typename T >
      void _matrix_release( struct matrix_str<T>& m )
    {
      mtx::release(m.dataM);
      mtx::release(m.labelM);
    }
    

    //---------------------------------------------------------------------------------------------------------------
    // example_t
    //
    
    typedef struct examples_str
    {
      unsigned type;
      union
      {
        struct matrix_str<float>  f;
        struct matrix_str<double> d;
      } u;
    } examples_t;

    template< typename T >
      rc_t _examples_load( examples_t& ex, unsigned dstTypeFlag, const struct mtx::mtx_str<T>& dataM, const struct mtx::mtx_str<T>& labelM )
    {
      rc_t rc = kOkRC;
      
      switch( dstTypeFlag )
      {
        case kFloatFl:
          _matrix_load<float,T>(ex.u.f,dataM,labelM);
          ex.type = dstTypeFlag;
          break;
          
        case kDoubleFl:
          _matrix_load<double,T>(ex.u.d,dataM,labelM);
          ex.type = dstTypeFlag;
          break;

        default:
          rc = cwLogError(kInvalidArgRC,"An invalid example type (%i) was encountered.", dstTypeFlag);
      }
      
      return rc;      
    }
          
    void _examples_destroy( examples_t& ex )
    {
      switch( ex.type )
      {
        case kFloatFl:  _matrix_release(ex.u.f); break;
        case kDoubleFl: _matrix_release(ex.u.d); break;
      }      
    }

    rc_t _examples_data_dimV( const examples_t& ex, const unsigned*& dimV, unsigned& dimN )
    {
      switch( ex.type )
      {
        case kFloatFl:  dimV=ex.u.f.dataM->dimV; dimN=ex.u.f.dataM->dimN; break;
        case kDoubleFl: dimV=ex.u.d.dataM->dimV; dimN=ex.u.d.dataM->dimN; break;
        default:
          assert(0);
      }
      return kOkRC;
    }
    
    rc_t _examples_label_dimV( const examples_t& ex, const unsigned*& dimV, unsigned& dimN )
    {
      switch( ex.type )
      {
        case kFloatFl:  dimV=ex.u.f.labelM->dimV; dimN=ex.u.f.labelM->dimN; break;
        case kDoubleFl: dimV=ex.u.d.labelM->dimV; dimN=ex.u.d.labelM->dimN; break;
        default:
          assert(0);
      }
      return kOkRC;
    }

    rc_t _examples_batch_f( const examples_t& ex, unsigned dataOffsetN, unsigned labelOffsetN, const float*& dataM, const float*& labelM )
    {
      dataM  = ex.u.f.dataM->base  + dataOffsetN;
      labelM = ex.u.f.labelM->base + labelOffsetN;
      
      return kOkRC;
    }

    rc_t _examples_batch_d( const examples_t& ex, unsigned dataOffsetN, unsigned labelOffsetN, const double*& dataM, const double*& labelM )
    {
      dataM  = ex.u.d.dataM->base  + dataOffsetN;
      labelM = ex.u.d.labelM->base + labelOffsetN;
      
      return kOkRC;
    }

    //---------------------------------------------------------------------------------------------------------------
    // datasubset_t
    //
    
    typedef struct datasubset_str
    {
      examples_t examples;
      unsigned   batchN;
      unsigned   iterIdx;
      unsigned   iterN;      
    } datasubset_t;

    void _datasubset_destroy( datasubset_str& ss )
    {
      ss.iterIdx = 0;
      ss.iterN   = 0;
      _examples_destroy(ss.examples);
    }

    template< typename T >
      rc_t _datasetsubset_load( datasubset_t& ss, unsigned dstTypeFlag, unsigned batchN, const struct mtx::mtx_str<T>& dataM, const struct mtx::mtx_str<T>& labelM )
    {
      unsigned exampleN = 0;
      switch( dataM.dimN )
      {
        case 2: exampleN = dataM.dimV[1]; break;
        case 3: exampleN = dataM.dimV[2]; break;
        default:
          cwLogError(kInvalidArgRC,"The dataset must be contained in a matrix of 2 or 3 dimensions.");
      }
      
      ss.batchN = batchN;
      ss.iterN  = exampleN/batchN;
      return _examples_load( ss.examples, dstTypeFlag, dataM, labelM );
    }

    rc_t _datasubset_data_dimV( const datasubset_t& ss, const unsigned*& dimV, unsigned& dimN )
    { return _examples_data_dimV( ss.examples, dimV, dimN ); }
    
    rc_t _datasubset_label_dimV( const datasubset_t& ss, const unsigned*& dimV, unsigned& dimN )
    { return _examples_label_dimV( ss.examples, dimV, dimN ); }

    rc_t _datasubset_batch_f( datasubset_t& ss, unsigned dataOffsetN, unsigned labelOffsetN, const float*& dataM, const float*& labelM )
    {
      rc_t rc;
      
      if( ss.iterIdx >= ss.iterN )
        return kEofRC;
      
      rc = _examples_batch_f( ss.examples, dataOffsetN * ss.iterIdx, labelOffsetN * ss.iterIdx, dataM, labelM );

      ++ss.iterIdx;
      return rc;
    }

    rc_t _datasubset_batch_d( datasubset_t& ss, unsigned dataOffsetN, unsigned labelOffsetN, const double*& dataM, const double*& labelM )
    {
      rc_t rc;
      
      if( ss.iterIdx >= ss.iterN )
        return kEofRC;
      
      rc = _examples_batch_d( ss.examples, dataOffsetN * ss.iterIdx, labelOffsetN * ss.iterIdx, dataM, labelM );

      ++ss.iterIdx;
      return rc;
    }
    
    //---------------------------------------------------------------------------------------------------------------
    // datasetMgr_t
    //

    enum
    {
     kTrainSsIdx,
     kValidSsIdx,
     kTestSsIdx,
     kDataSubSetN
    };
    
    typedef struct datasetMgr_str
    {
      const object_t* cfg;
      unsigned     typeFlag;
      datasubset_t ssA[ kDataSubSetN ];
      unsigned     dataRealN;
      unsigned     labelRealN;      
    } datasetMgr_t;

    datasetMgr_t* _handleToPtr( handle_t h )
    { return handleToPtr< handle_t, datasetMgr_t >(h); }

    unsigned _ssFlagToIndex( unsigned flags )
    {
      flags &= (kTrainSsFl | kValidSsFl | kTestSsFl );
      
      switch( flags )
      {
        case kTrainSsFl: return kTrainSsIdx;
        case kValidSsFl: return kValidSsIdx;
        case kTestSsFl:  return kTestSsIdx;
      }
      
      cwLogError(kInvalidArgRC,"Invalid subset flags (0x%x).", flags );
      return kInvalidIdx;
    }

    void _unload( datasetMgr_t* p )
    {
      for(unsigned i=0; i<kDataSubSetN; ++i)
        _datasubset_destroy( p->ssA[i] );
    }
    
    rc_t _destroy( datasetMgr_t* p )
    {
      _unload(p);
      mem::release(p);
      
      return kOkRC;      
    }


    unsigned _mtx_to_realN( const mtx::f_t& m )
    {
      switch( m.dimN )
      {
        case 1: return 1; 
        case 2: return m.dimV[0]; 
        case 3: return m.dimV[0] * m.dimV[1];
      }
      
      cwLogError(kInvalidArgRC,"%i invalid matrix rank.",m.dimN);
      return 0;
    }

    //rc_t _load( datasetMgr_t* p, unsigned ssFlags, unsigned batchN, const mtx::f_t& dataM, const mtx::f_t& labelM )
    
    template< typename T >
      rc_t _load( datasetMgr_t* p, unsigned ssFlags, unsigned batchN, const struct mtx::mtx_str<T>& dataM, const struct mtx::mtx_str<T>& labelM )
    {
      rc_t rc = kOkRC;
      unsigned ssIdx;
      if(( ssIdx = _ssFlagToIndex(ssFlags)) != kInvalidIdx )
        if((rc =  _datasetsubset_load( p->ssA[ssIdx], p->typeFlag, batchN, dataM, labelM )) != kOkRC )
        {
          p->dataRealN  = _mtx_to_realN(dataM);
          p->labelRealN = _mtx_to_realN(labelM);
          return kOkRC;
        }
      
      return kInvalidArgRC;
    }

    rc_t _mnist_load_subset( datasetMgr_t* p, unsigned ssFlags, unsigned batchN, const mtx::f_t& m )
    {
      rc_t rc = kOkRC;
      mtx::f_t* labelM  = mtx::slice_alias(m,0,0,1);    // the first row contains the labels
      mtx::f_t* dsM     = mtx::slice_alias(m,1,0);      // all successive rows contain the data
      mtx::f_t* oneHotM = mtx::alloc_one_hot<float>(*labelM); // convert the labels to a one hot encoding

      //unsigned dsExampleN = mtx::ele_count<float>(*labelM);  // total count of examples in this dataset

      rc = _load<float>( p, ssFlags, batchN, *dsM, *oneHotM );

      // Inform the matrix objects that the ownership
      // of the data and dimV memory from 'dsM' and 'oneHotM'
      // has been taken over by the dataset object. 
      //clear_memory_release_flag( *oneHotM );
      //clear_memory_release_flag( *dsM );
      
      mtx::release(labelM);
      mtx::release(oneHotM);
      mtx::release(dsM);
      
      return rc;      
    }
    
    rc_t _mnist_load( datasetMgr_t* p, const object_t* ele, unsigned batchN, unsigned flags )
    {
      rc_t        rc    = kOkRC;
      const char* inDir = nullptr;
      mnist::handle_t mnistH;

      // locate 
      if( ele->get("inDir",inDir) != kOkRC )
        return cwLogError(kSyntaxErrorRC,"MNIST 'indir' cfg. label not found.");
      
      if( (rc = mnist::create(mnistH, inDir )) != kOkRC )
      {
        return cwLogError(rc,"MNIST dataset instantiation failed.");
      }
      else
      {

        const mtx::f_t* rM = mnist::train(mnistH);
        const mtx::f_t* vM = mnist::validate(mnistH);
        const mtx::f_t* tM = mnist::test(mnistH);


        _mnist_load_subset( p, kTrainSsFl, batchN, *rM );
        _mnist_load_subset( p, kValidSsFl, batchN, *vM );
        _mnist_load_subset( p, kTestSsFl,  batchN, *tM );
        
        mnist::destroy(mnistH);
      }

      return rc;
    }
  }
}

cw::rc_t cw::dataset::create( handle_t& h, const object_t* cfg, unsigned flags )
{
  rc_t rc;
  if((rc = destroy(h)) != kOkRC )
    return rc;

  datasetMgr_t* p = mem::allocZ<datasetMgr_t>(1);

  p->cfg      = cfg;
  p->typeFlag = flags;
  h.set(p);
  
  return rc;
}

cw::rc_t cw::dataset::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  
  if( !h.isValid() )
    return kOkRC;

  datasetMgr_t* p = _handleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();

  return rc;
}

cw::rc_t cw::dataset::load( handle_t h, const char* dsLabel, unsigned batchN, unsigned validPct, unsigned testPct, unsigned flags )
{
  rc_t             rc    = kOkRC;
  datasetMgr_t*    p     = _handleToPtr(h);
  const object_t*  dataL = p->cfg->find("dataL");

  // empty the data mgr x_dsA[] before loading the next dataset
  _unload(p);
  

  // for each possible dataset
  for(unsigned i=0; i<dataL->child_count(); ++i)
  {
    const object_t* ele   = dataL->child_ele(i);
    const char*     label = nullptr;

    // get the name of this dataset
    if( ele->get("name", label ) != kOkRC )
    {
      // all ele's must have a 'name' field
      cwLogError(kLabelNotFoundRC,"Dataset cfg. element at index %i does not have a 'name' field.",i);
      goto errLabel;
    }

    // if this is the target dataset
    if( strcmp(dsLabel,label) == 0 )
    {
      if( strcmp(label,"mnist") == 0 )
        return _mnist_load(p, ele, batchN,flags);
    }
    
    
  }
  
  errLabel:
      return rc;
}
  


cw::rc_t cw::dataset::subset_dims( handle_t h, unsigned subsetFl, const unsigned*& dimV_Ref, unsigned& dimN_Ref )
{
  datasetMgr_t* p = _handleToPtr(h);
  unsigned ssIdx;
  
  if((ssIdx = _ssFlagToIndex(subsetFl)) == kInvalidIdx )
    return kInvalidArgRC;
  
  return _datasubset_data_dimV( p->ssA[ssIdx], dimV_Ref, dimN_Ref );
}

cw::rc_t cw::dataset::label_dims(  handle_t h, unsigned subsetFl, const unsigned*& dimV_Ref, unsigned& dimN_Ref )
{
  datasetMgr_t* p = _handleToPtr(h);
  unsigned ssIdx;
  
  if((ssIdx = _ssFlagToIndex(subsetFl)) == kInvalidIdx )
    return kInvalidArgRC;
  
  return _datasubset_label_dimV( p->ssA[ssIdx], dimV_Ref, dimN_Ref );
}

cw::rc_t  cw::dataset::batch_f(  handle_t h, unsigned subsetFl, const float*& dataM_Ref, const float*& labelM_Ref )
{
  datasetMgr_t* p = _handleToPtr(h);
  unsigned ssIdx;
  
  if((ssIdx = _ssFlagToIndex(subsetFl)) == kInvalidIdx )
    return kInvalidArgRC;
  
  return _datasubset_batch_f( p->ssA[ssIdx], p->dataRealN, p->labelRealN, dataM_Ref, labelM_Ref );
}

cw::rc_t cw::dataset::batch_d(  handle_t h, unsigned subsetFl, const double*& dataM_Ref, const double*& labelM_Ref )
{
  datasetMgr_t* p = _handleToPtr(h);
  unsigned ssIdx;
  
  if((ssIdx = _ssFlagToIndex(subsetFl)) == kInvalidIdx )
    return kInvalidArgRC;
  
  return _datasubset_batch_d( p->ssA[ssIdx], p->dataRealN, p->labelRealN, dataM_Ref, labelM_Ref );
}


  
cw::rc_t cw::dataset::test( const object_t* cfg )
{
  handle_t        h;
  rc_t            rc        = kOkRC;
  const char*     dsLabel   = nullptr;
  unsigned        batchN    = 64;
  unsigned        validPct  = 10;
  unsigned        testPct   = 10;
  unsigned        typeFlag  = kFloatFl;
  time::spec_t    t0;
  const float*    dataM     = nullptr;
  const float*    labelM    = nullptr;
  const unsigned *dataDimV  = nullptr;
  const unsigned *labelDimV = nullptr;
  unsigned        dataDimN  = 0;
  unsigned        labelDimN = 0;
  unsigned        batchCnt      = 0;
  time::get(t0);
  
  if((rc = cfg->getv("dsLabel",dsLabel,"batchN",batchN,"validPct",validPct,"testPct",testPct)) != kOkRC )
    return cwLogError(rc,"Dataset test failed.  Argument parse failed.");

  if((rc = create(h,cfg,typeFlag)) != kOkRC )
    return cwLogError(rc,"Dataset manager create failed.");
  
  if((rc = load(h, dsLabel, batchN, validPct, testPct, kDoubleFl )) != kOkRC )
  {
    cwLogError(rc,"'%s' dataset load failed.", cwStringNullGuard(dsLabel));
    goto errLabel;
  }

  if((rc = subset_dims(h,kTrainSsFl,dataDimV, dataDimN )) != kOkRC )
    goto errLabel;

  if((rc = label_dims(h,kTrainSsFl,labelDimV, labelDimN )) != kOkRC )
    goto errLabel;

  vop::print(dataDimV,dataDimN,"%i ","data: ");
  vop::print(labelDimV,labelDimN,"%i ","label: ");

  batchCnt = dataDimV[1]/batchN;
  printf("batchCnt:%i\n",batchCnt);

  for(unsigned i=0; true; ++i )
  {
    if((rc = batch_f(h,kTrainSsFl,dataM,labelM)) != kOkRC )
    {
      printf("rc:%i : %i %i\n",rc,batchCnt,i);
      break;
    }
    
    if( i==0 )
    {
      vop::print(dataM,3,"%f ");
      
    }
  }

  printf("elapsed %i ms\n",time::elapsedMs( t0 ) );

 errLabel:
  destroy(h);

  return rc;  
}
