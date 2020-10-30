#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwNN.h"

/*




 */

namespace cw
{
  namespace nn
  {
    

    typedef struct layer_desc_str
    {
      unsigned        layerTId;
      unsigned        activationId;
      unsigned        weightInitId;
      unsigned        biasInitId;
    } layer_desc_t;

    typedef struct network_desc_str
    {
      layer_desc_t* layers;
      unsigned      layerN;
    } network_desc_t;
    
    typedef struct layer_str
    {
      const layer_desc_t* desc;
      const mtx::d_t*     iM;
      mtx::d_t            wM;
      mtx::d_t            aM;
    } layer_t;
    
    typedef struct nn_str
    {
      const network_desc_t* desc;
      layer_t*              layerL;
    } nn_t;


    nn_t* _allocNet( nn_t* nn, const object_t& nnCfg, unsigned inNodeN )
    {
    }

    nn_t* _initNet( nn_t* nn )
    {
    }

    rc_t _netForward( nn_t* p )
    {
      
    }

    rc_t _netReverse( nn_t* )
    {
    }


    rc_t _batchUpdate( const mtx::d_t& ds, const train_args_t& args, unsigned ttlTrainExampleN )
    {
    }

    rc_t train( handle_t h, dataset::handle_t dsH, const train_args_t& args )
    {
      mtx::d_t ds_mtx;
      mtx::d_t label_mtx;
      unsigned trainExampleN = dataset::example_count(dsH);
      unsigned batchPerEpoch = trainExampleN/args.batchN;
      

      for(unsigned i=0; i<epochN; ++i)
      {
        for(unsigned j=0;  j<batchsPerEpoch; ++j)
        {
          dataset::batchd(dsH, j, ds_mtx, label_mtx,args.batchN, batchPerEpoch);
          
          _batchUpdate(ds_mtx,args,ttlTrainExampleN);
          
        }
      }

      
    }


    
  }

  rc_t test( const char* cfgFn, const char* projLabel )
  {
    object_t* cfg = nullptr;
    rc_t      rc  = kOkRC;
    
    if((rc = objectFromFile( cfgFn, cfg )) != kOkRC )
    {
      
    }

    

  errLabel:
    if( cfg != nullptr )
      cfg->free();
    
    return rc;
  }

}



