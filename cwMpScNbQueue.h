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
      node_t* stub = memAllocZ<node_t>();
      _head = stub;
      _tail = stub;
    }
    
    virtual ~MpScNbQueue()
    { memFree(_tail); }

    MpScNbQueue( const MpScNbQueue& ) = delete;
    MpScNbQueue( const MpScNbQueue&& ) = delete;
    MpScNbQueue& operator=(const MpScNbQueue& ) = delete;
    MpScNbQueue& operator=(const MpScNbQueue&& ) = delete;
    

    void    push( T* payload )
    {
      node_t* new_node = memAllocZ<node_t>(1);
      new_node->payload = payload;
      new_node->next.store(nullptr);
      node_t* prev   = _head.exchange(new_node,std::memory_order_acq_rel);  // append the new node to the list (aquire-release)
      prev->next.store(new_node,std::memory_order_release);                  // make the new node accessible by the consumer (release to consumer)
      
    }
    
    T*      pop()
    {
      T* payload = nullptr;
      node_t* t    = _tail;
      node_t* next = t->next.load(std::memory_order_acquire);  //  acquire from producer
      if( next != nullptr )
      {
        _tail    = next;    
        payload = next->payload;
        memFree(t);
      }
  
      return payload;
      
    }
    
    bool    isempty() const
    {
      return _tail->next.load(std::memory_order_acquire) == nullptr;  //  acquire from producer
    }

  private:

    void _push( node_t* new_node )
    {
    }

    node_t*              _stub;
    node_t*              _tail;
    std::atomic<node_t*> _head;
  };

  void mpScNbQueueTest();
}


#endif
