//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwMpScQueue_h
#define cwMpScQueue_h

namespace cw
{
  
  template<typename T>
  class MpScQueue
  {
  public:
    
    typedef struct node_str
    {
      struct node_str* next     = nullptr;
      T*               payload  = nullptr;
    } node_t;
    
    MpScQueue()
    {
      node_t* stub = mem::allocZ<node_t>();
      _head = stub;   // last-in
      _tail = stub;   // first-out
      mutex::create( _mH );
    }
    
    virtual ~MpScQueue()
    {
      mem::free(_tail);
      mutex::destroy( _mH );
    }

    MpScQueue( const MpScQueue& ) = delete;
    MpScQueue( const MpScQueue&& ) = delete;
    MpScQueue& operator=(const MpScQueue& ) = delete;
    MpScQueue& operator=(const MpScQueue&& ) = delete;
    

    void    push( T* payload )
    {
      node_t* new_node  = mem::allocZ<node_t>(1);
      new_node->payload = payload;
      new_node->next    = nullptr;

      if( mutex::lock(_mH) == kOkRC )
      {
      
        // Note that the elements of the queue are only accessed from the end of the queue (tail).
        // New nodes can therefore safely be updated in two steps:

        node_t* prev = _head;
        _head        = new_node;
        
        // 1. Atomically set _head to the new node and return 'old-head'
        //node_t* prev   = _head.exchange(new_node,std::memory_order_acq_rel);  

        // Note that at this point only the new node may have the 'old-head' as it's predecssor.
        // Other threads may therefore safely interrupt at this point.
      
        // 2. Set the old-head next pointer to the new node (thereby adding the new node to the list)
        prev->next = new_node; // RELEASE 'next' to consumer

        mutex::unlock(_mH);
      }
      
    }
    
    T*      pop()
    {
      T*      payload = nullptr;
      node_t* t       = _tail;

      if( mutex::lock(_mH) == kOkRC )
      {
        node_t* next    = t->next;  //  ACQUIRE 'next' from producer
        if( next != nullptr )
        {
          _tail   = next;    
          payload = next->payload;
          mem::free(t);
        }
        
        mutex::unlock(_mH);
      }
      
      return payload;
      
    }
    
    bool    isempty() const
    {
      return _tail->next == nullptr;  //  ACQUIRE 'next'  from producer
    }

  private:

    node_t*         _stub;
    node_t*         _tail;
    node_t*         _head;
    mutex::handle_t _mH;
  };

}


#endif
