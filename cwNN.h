#ifndef cwNN_H
#define cwNN_H

namespace cw
{
  namespace nn
  {
    typedef handle<struct nn_str> handle_t;

    enum
    {
     kSigmoidActId,
     kReluActId
    };

    enum
    {
     kInputLayerId,
     kDenseLayerId,
     kConv1DConvId
    };

    enum
    {
     kZeroInitId,
     kUniformInitId,
     kNormalInitId
    };

    typedef struct layer_args_str
    {
      unsigned        typeId;
      unsigned        actId;
      unsigned        weightInitId;
      unsigned        biasInitId;
      unsigned        dimN;
      const unsigned* dimV;
    } layer_args_t;

    typedef struct network_args_str
    {
      layer_args_t* layers;
      unsigned      layerN;
    } network_args_t;


    rc_t parse_args( const object_t& o, network_args_t& args );
    
    rc_t create(  handle_t& h, const network_args_t& args );
    rc_t destroy( handle_t& h );

    template< typename R >
      rc_t train( handle_t h, unsigned epochN, unsigned batchN, const dataset<R>& trainDs );

    template< typename R >
      rc_t infer( handle_t h, const dataset<R>& ds );
  }
}


#endif

