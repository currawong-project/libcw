#ifndef cwDataSets_h
#define cwDataSets_h


namespace cw
{
  namespace dataset
  {
    namespace mnist
    {
      typedef handle<struct mnist_str> handle_t;

      rc_t create( handle_t& h, const char* dir );
      rc_t destroy( handle_t& h );
      
      // Each column has one example.
      // The top row contains the labels.
      const mtx::fmtx_t* train(    handle_t h );
      const mtx::fmtx_t* validate( handle_t h );
      const mtx::fmtx_t* test(     handle_t h );

      rc_t test(const char* dir, const char* imageFn );

      
    }
  }

  
}


#endif
