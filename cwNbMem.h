namespace cw
{
  namespace nbmem
  {
    typedef handle< struct nbmem_str > handle_t;

    rc_t create(  handle_t& h, unsigned preallocMemN );
    rc_t destroy( handle_t& h );

    void* alloc( handle_t h, unsigned byteN );
    void  release(  handle_t h, void* p );

    void report( handle_t h );
    rc_t test();
    rc_t test_multi_threaded();
  }
}
