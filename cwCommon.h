#ifndef cwCOMMON_H
#define cwCOMMON_H

#include <cstdio>  // declares 'NULL'
#include <cstdarg>
 


#define kInvalidIdx ((unsigned)-1)
#define kInvalidId  ((unsigned)-1)
#define kInvalidCnt ((unsigned)-1)


namespace cw
{

  typedef enum
  {
   kOkRC = 0,
   kObjAllocFailRC,    // an object allocation failed
   kObjFreeFailRC,     // an object free failed
   kInvalidOpRC,       // the current state does not support the operation
   kInvalidArgRC,      
   kInvalidIdRC,       // an identifer was found to be invalid
   kOpenFailRC,
   kCloseFailRC,
   kWriteFailRC,
   kReadFailRC,
   kFlushFailRC,
   kSeekFailRC,
   kEofRC,
   kResourceNotAvailableRC,
   kMemAllocFailRC,
   kGetAttrFailRC,
   kSetAttrFailRC,
   kTimeOutRC,
   kProtocolErrorRC,
   kOpFailRC,
   kSyntaxErrorRC,
   kBufTooSmallRC,
   kAssertFailRC,  // used with cwLogFatal
   kBaseAppRC
  } cwRC_t;

  typedef unsigned rc_t;
  

  template< typename T >
    struct handle
  {
    typedef T p_type;
    T* p = nullptr;

    void set(T* ptr)     { this->p=ptr; }
    void clear()         { this->p=nullptr; }
    bool isValid() const { return this->p != nullptr; }
  };

}


#endif
