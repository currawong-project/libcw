#ifndef cwMpScNbCircQueue_h
#define cwMpScNbCircQueue_h
namespace cw
{
  namespace mp_sc_nb_circ_queue
  {
    template< typename T >
    struct node_str
    {
      std::atomic<struct node_str<T>* > next;
      T t;                          
    };
    
    template< typename T >
    struct cq_str
    {
      struct node_str<T>*   array;
      unsigned              alloc_cnt;
      unsigned              index_mask;
      std::atomic<unsigned> res_cnt;
      std::atomic<unsigned> res_head_idx;
      
      std::atomic<struct node_str<T>*> head; // last-in      
      struct node_str<T>*              tail; // first-out
      struct node_str<T>               stub; // dummy node
    };

    template< typename T >
    struct cq_str<T>* create( unsigned alloc_cnt )
    {
      struct cq_str<T>* p = mem::allocZ<struct cq_str<T>>();

      // increment alloc_cnt to next power of two so we can use
      // a bit mask rather than modulo to keep the head and tail indexes in range
      alloc_cnt = math::nextPowerOfTwo(alloc_cnt);
    
      p->array           = mem::allocZ< struct node_str<T> >(alloc_cnt);
      p->alloc_cnt       = alloc_cnt;
      p->index_mask      = alloc_cnt - 1; // decr. alloc_cnt by 1 to create an index mask
      p->res_cnt.store(0);
      p->res_head_idx.store(0);

      p->stub.next.store(nullptr);
      p->head.store(&p->stub);
      p->tail = &p->stub;
      
      for(unsigned i=0; i<alloc_cnt; ++i)
        p->array[i].next.store(nullptr);

      return p;      
    }

    template< typename T >
    rc_t destroy( struct cq_str<T>*& p_ref)
    {
      if( p_ref != nullptr )
      {
          mem::release(p_ref->array);
          mem::release(p_ref);
      }
      return kOkRC;
    }

    
    template< typename T >
    rc_t push( struct cq_str<T>* p, T value )
    {
      rc_t rc = kOkRC;

      // allocate a slot in the array - on succes this thread owns space on the array
      // (acquire prevents reordering with rd/wr ops below - sync with fetch_sub() below)
      if( p->res_cnt.fetch_add(1,std::memory_order_acquire) >= p->alloc_cnt )
      {
        // a slot is not available - undo the increment
        p->res_cnt.fetch_sub(1,std::memory_order_release);
        
        rc = kBufTooSmallRC;
      }
      else
      {
        // get the location of the reserved slot and then advance the res_head_idx.
        unsigned idx = p->res_head_idx.fetch_add(1,std::memory_order_acquire) & p->index_mask;

        struct node_str<T>* n = p->array + idx;
        
        // store the pushed element in the reserved slot
        n->t = value;
        n->next.store(nullptr);

        // Note that the elements of the queue are only accessed from the front of the queue (tail).
        // New nodes are added to the end of the list (head).
        // New node will therefore always have it's next ptr set to null.

        // 1. Atomically set head to the new node and return 'old-head'.
        // We use acq_release to prevent code movement above or below this instruction.
        struct node_str<T>* prev   = p->head.exchange(n,std::memory_order_acq_rel);  

        // Note that at this point only the new node may have the 'old-head' as it's predecssor.
        // Other threads may therefore safely interrupt at this point - they will
        // have the new node as their predecessor. Note that none of these nodes are accessible
        // yet because __tail next__ pointer is still pointing to the 'old-head' - whose next pointer
        // is still null.  
      
        // 2. Set the old-head next pointer to the new node (thereby adding the new node to the list)
        prev->next.store(n,std::memory_order_release); // RELEASE 'next' to consumer            

      }
      return rc;
    }

    template< typename T >
    rc_t pop( struct cq_str<T>* p, T& value_ref )
    {
      rc_t rc = kEofRC;

      // We always access the tail element through tail->next. Always accessing the
      // next tail element via the tail->next pointer is critical to correctness.
      // See note in push().
      struct node_str<T>* next = p->tail->next.load(std::memory_order_acquire); //  ACQUIRE 'next' from producer

      if( next != nullptr )
      {
        value_ref = next->t;
        
        p->tail = next;

        // decrease the count of full slots
        p->res_cnt.fetch_sub(1,std::memory_order_release);

        rc = kOkRC;
        
      }

      return rc;
    }
      

  }
}
#endif
