#ifndef cwSpScQueueTmpl_H
#define cwSpScQueueTmpl_H

namespace cw
{

  template< typename T >
    class spScQueueTmpl
  {
  public:
    spScQueueTmpl( unsigned eleN )
    {
      _aV  = mem::allocZ<T*>(eleN);
      _mem = mem::allocZ<T>(eleN);
      _wi.load(std::memory_order_release);
      _ri.load(std::memory_order_release);
      
      for(unsigned i=0; i<eleN; ++i)
        _aV[i] = _mem + i;
    }

    virtual ~spScQueueTmpl()
    {
      mem::release(_aV);
      mem::release(_mem);
    }

    T* get()
    {
      unsigned wi = _wi.load( std::memory_order_relaxed );
      return _aV[wi];
    }

    rc_t push( T* v )
    {
      unsigned ri = _ri.load( std::memory_order_acquire );
      unsigned wi = _wi.load( std::memory_order_relaxed );

      // calc. the count of full elements
      unsigned n =  wi >= ri ? wi-ri : (_aN-ri) + wi;

      // there must always be at least one empty element because
      // wi can never be advanced to equal ri.
      if( n >= _aN-1 )
        return kBufTooSmallRC;

      // store the new element
      _aV[wi] = v;

      wi = (wi+1) % _aN;
    
      _wi.store( wi, std::memory_order_release );
    
      return kOkRC;
    }

    T* pop()
    {
      unsigned ri = _ri.load( std::memory_order_relaxed );
      unsigned wi = _wi.load( std::memory_order_acquire );

      unsigned n =  wi >= ri ? wi-ri : (_aN-ri) + wi;

      if( n == 0 )
        return nullptr;

      T* v = _aV[ri];

      ri = (ri+1) % _aN;

      _ri.store( ri, std::memory_order_release);

      return v;
    }

  

    
  private:
    unsigned              _aN = 0;
    T**                   _aV = nullptr;
    T*                    _mem;
    std::atomic<unsigned> _wi;   
    std::atomic<unsigned> _ri;

    // Note: // _wi==_ri indicates an empty buffer.
    // _wi may never be advanced such that it equals _ri, however
    // _ri may be advanced such that it equals _wi.
  };


  rc_t testSpScQueueTmpl();

}
#endif
