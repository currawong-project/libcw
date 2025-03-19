#ifndef cwMpScNbCircQueue_h
#define cwMpScNbCircQueue_h
namespace cw
{
  namespace mp_sc_nb_circ_queue
  {

    template< typename T >
    struct cq_str
    {
      T*                    array;
      unsigned              alloc_cnt;
      unsigned              tail_idx;
      std::atomic<unsigned> head_idx;
      std::atomic<unsigned> cnt;    
    };

    template< typename T >
    struct cq_str<T>* alloc( unsigned alloc_cnt )
    {
      struct cq_str<T>* p = mem::allocZ<struct cq_str<T>>();

      // increment alloc_cnt to next power of two so we can use
      // a bit mask rather than modulo to keep the head and tail indexes in range
      alloc_cnt = math::nextPowerOfTwo(alloc_cnt);
    
      p->array = mem::allocZ<T>(alloc_cnt);
      p->alloc_cnt = alloc_cnt;

      return p;      
    }

    template< typename T >
    rc_t free( struct cq_str<T>*& p_ref)
    {
      if( p_ref != nullptr )
      {
          mem::release(p_ref->array);
          mem::release(p_ref);
      }
      return kOkRC;
    }

    
    template< typename T >
    rc_t push( struct cq_str<T>* p, T* value )
    {
      rc_t rc = kOkRC;

      // allocate a slot in the array - on succes this thread has space to push
      // (acquire prevents reordering with rd/wr ops below - sync with fetch_sub() below)
      if( p->cnt.fetch_add(1,std::memory_order_acquire) >= p->alloc_cnt )
      {
        rc = kBufTooSmallRC;
      }
      else
      {
        // get the current head and then advance it
        unsigned idx = p->head_idx.fetch_add(1,std::memory_order_acquire) & p->alloc_cnt;

        p->array[ idx ] = value;
      }
      return rc;
    }

    template< typename T >
    rc_t pop( struct cq_str<T>* p, T*& value_ref )
    {
      rc_t rc = kOkRC;
      unsigned n = p->cnt.load(std::memory_order_acquire);
    
      if( n == 0 )
      {
        rc = kEofRC;
      }
      else
      {

        value_ref = p->array[ p->tail_idx  ];

        p->tail_idx = (p->tail_idx + 1) & p->alloc_cnt;
      
        p->cnt.fetch_sub(1,std::memory_order_release );
      }

      return rc;
    }

    rc_t test( const object_t* cfg );
  

  }
}
#endif
