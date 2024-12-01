//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwMpScNbQueue_h
#define cwMpScNbQueue_h

namespace cw
{
  template<typename T>
  class MpScNbQueue
  {
  public:
    
    typedef struct node_str
    {
      std::atomic<struct node_str*> next     = nullptr;
      T*                            payload  = nullptr;
    } node_t;
    
    MpScNbQueue()
    {
      node_t* stub = mem::allocZ<node_t>();
      _head = stub;   // last-in
      _tail = stub;   // first-out 
    }
    
    virtual ~MpScNbQueue()
    { mem::free(_tail); }

    MpScNbQueue( const MpScNbQueue& ) = delete;
    MpScNbQueue( const MpScNbQueue&& ) = delete;
    MpScNbQueue& operator=(const MpScNbQueue& ) = delete;
    MpScNbQueue& operator=(const MpScNbQueue&& ) = delete;
    

    void    push( T* payload )
    {
      // BUG: malloc() isn't non-blocking
      node_t* new_node = mem::allocZ<node_t>(1);
      
      new_node->payload = payload;
      new_node->next.store(nullptr);
      // Note that the elements of the queue are only accessed from the end of the queue (tail).
      // New nodes can therefore safely be updated in two steps:

      // 1. Atomically set _head to the new node and return 'old-head'
      node_t* prev   = _head.exchange(new_node,std::memory_order_acq_rel);  

      // Note that at this point only the new node may have the 'old-head' as it's predecssor.
      // Other threads may therefore safely interrupt at this point.
      
      // 2. Set the old-head next pointer to the new node (thereby adding the new node to the list)
      prev->next.store(new_node,std::memory_order_release); // RELEASE 'next' to consumer            

      // After the first insertion:
      // tail -> stub
      // stub.next -> new_node
      // head -> new_node
      
    }
    
    T*      pop()
    {
      T*      payload = nullptr;
      node_t* t       = _tail;
      node_t* next    = t->next.load(std::memory_order_acquire);  //  ACQUIRE 'next' from producer
      if( next != nullptr )
      {
        _tail   = next;    
        payload = next->payload;
        mem::free(t);  // BUG: free() isn't non-blocking
      }
  
      return payload;
      
    }
    
    bool    isempty() const
    {
      return _tail->next.load(std::memory_order_acquire) == nullptr;  //  ACQUIRE 'next'  from producer
    }

  private:

    node_t*              _stub;
    node_t*              _tail;
    std::atomic<node_t*> _head;
  };

  void mpScNbQueueTest();
}


#endif
