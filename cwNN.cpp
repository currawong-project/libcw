#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwNN.h"


namespace cw
{
  namespace nn
  {
    
    template< typename R >
    struct input_str
    {
      R*        x;
      unsigned  dimN;
      unsigned* dimV;
    };
    
    typedef struct dense_str
    {
      unsigned  xN;   // count of neurons in src layer
      unsigned  yN;   // count of neurons in this layer
      
      real_t*   wM;     // wM[ xN, yN ]  weight matrix
      real_t*   bV;     // bV[ yN ]      bias vector


      real_t*   yV;   // scaled input + bias
      real_t*   aV;   // activation output
      real_t*   dV;   // contribution to cost for each neurode
      real_t*   gV;   // C gradient wrt weight at each neurode

      
    } dense_t;
    
    typedef struct layer_str
    {
    } layer_t;
    
    typedef struct nn_str
    {
      
    } nn_t;


    void _mtx_mul( R* z, R* m, R* x, unsigned mN, unsigned mM )
    {
    }

    void _add( R* y, R* x, unsigned n )
    {
    }

    void _activation( dense_t* l )
    {
    }

    void _dense_forward( dense_t* l0, dense_t* l1 )
    {
      assert( l1->wM.dimV[1] == l0->yN );
      assert( l1->wM.dimV[0] == l1->yN );
      _mtx_mult( l1->zV, l1->wM.base, l0->aV, l0->yN, l1->yN );
      _add( l1->zV, l1->bV, l1->yN );

      _activation(l1)
    }
    
  }
}



