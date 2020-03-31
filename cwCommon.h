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
   kOkRC = 0,               // 0 
   kObjAllocFailRC,         // 1 - an object allocation failed
   kObjFreeFailRC,          // 2 - an object free failed
   kInvalidOpRC,            // 3 - the current state does not support the operation
   kInvalidArgRC,           // 4 -
   kInvalidIdRC,            // 5 - an identifer was found to be invalid
   kOpenFailRC,             // 6    
   kCloseFailRC,            // 7
   kWriteFailRC,            // 8
   kReadFailRC,             // 9
   kFlushFailRC,            // 10
   kSeekFailRC,             // 11
   kEofRC,                  // 12
   kResourceNotAvailableRC, // 13
   kMemAllocFailRC,         // 14
   kGetAttrFailRC,          // 15
   kSetAttrFailRC,          // 16 
   kTimeOutRC,              // 17 
   kProtocolErrorRC,        // 18
   kOpFailRC,               // 19
   kSyntaxErrorRC,          // 20
   kBufTooSmallRC,          // 21
   kLabelNotFoundRC,        // 22 - use by cwObject to indicate that an optional value does not exist.
   kDuplicateRC,            // 23 - an invalid duplicate was detected
   kAssertFailRC,           // 24 - used with cwLogFatal
   kBaseAppRC               // 25
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
