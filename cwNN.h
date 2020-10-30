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
     kInputLayerTId,
     kDenseLayerTId,
     kConv1DConvTId
    };

    enum
    {
     kZeroInitId,
     kUniformInitId,
     kNormalInitId
    };


    
    typedef struct train_args_str
    {
      unsigned epochN;
      unsigned batchN;
      double eta;
      double lambda;
      
    } train_args_t;


    
    rc_t create(  handle_t& h, const object_t& cfg );
    rc_t destroy( handle_t& h );

    rc_t train( handle_t h, dataset::handle_t dsH, const train_args_t& args );

    rc_t test( handle_t h, dataset::handle_t dsH );


    rc_t test( const char* mnistDir );
  }
}


#endif

