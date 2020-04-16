#ifndef cwCommonImpl_H
#define cwCommonImpl_H

#include "config.h"
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cinttypes>
#include <cfloat>
#include <cmath>
#include <algorithm>  // std::min,std::max
#include <utility>    // std::forward
#include <limits>     // std::numeric_limits<
#include <atomic>
#include <cstdint>

#if defined(OS_LINUX) || defined(OS_OSX)
#define cwPOSIX_FILE_SYS
#include <time.h>         // timespec
#include <netinet/in.h>	  // struct sockaddr_in
#include <unistd.h>       // close()
#define cwPathSeparatorChar '/'
#endif

#define cwStringNullGuard(s) ((s)==nullptr ? "" : (s))


#define cwAllFlags(f,m)  (((f) & (m)) == (m))                    // Test if all of a group 'm' of binary flags in 'f' are set.
#define cwIsFlag(f,m)    (((f) & (m)) ? true : false)            // Test if any one of a the bits in 'm' is also set in 'f'.
#define cwIsNotFlag(f,m) (cwIsFlag(f,m)==false)                  // Test if none of the bits in 'm' are set in 'f'.
#define cwSetFlag(f,m)   ((f) | (m))                             // Return 'f' with the bits in 'm' set.
#define cwClrFlag(f,m)   ((f) & (~(m)))                          // Return 'f' with the bits in 'm' cleared.
#define cwTogFlag(f,m)   ((f)^(m))                               // Return 'f' with the bits in 'm' toggled.
#define cwEnaFlag(f,m,b) ((b) ? cwSetFlag(f,m) : cwClrFlag(f,m)) // Set or clear bits in 'f' based on bits in 'm' and the state of 'b'.

// In-place assignment version of the above bit operations
#define cwSetBits(f,m)   ((f) |= (m))                             // Set 'f' with the bits in 'm' set.
#define cwClrBits(f,m)   ((f) &= (~(m)))                          // Set 'f' with the bits in 'm' cleared.
#define cwTogBits(f,m)   ((f)^=(m))                               // Return 'f' with the bits in 'm' toggled.
#define cwEnaBits(f,m,b) ((b) ? cwSetBits(f,m) : cwClrBits(f,m))  // Set or clear bits in 'f' based on bits in 'm' and the state of 'b'.



namespace cw
{

  
#define cwAssert(cond) while(1){ if(!(cond)){ cwLogFatal(kAssertFailRC,"Assert failed on condition:%s",#cond ); } break; }
  

  template< typename H, typename T >
    T* handleToPtr( H h )
  {
    cwAssert( h.p != nullptr );
    return h.p;
  }

  typedef struct idLabelPair_str
  {
    unsigned    id;
    const char* label;
  } idLabelPair_t;

  const char* idToLabel( const idLabelPair_t* array, unsigned id, unsigned eolId );
  unsigned    labelToId( const idLabelPair_t* array, const char* label, unsigned eolId );


  inline rc_t rcSelect() { return kOkRC; }

  template<typename T, typename... ARGS>
    rc_t rcSelect(T rc, ARGS... args)
  {
    if( rc != kOkRC )
      return rc;
    
    return rcSelect(args...);
  }


  void sleepSec( unsigned secs ); // sleep seconds
  void sleepMs( unsigned ms ); // sleep milliseconds
  void sleepUs( unsigned us ); // sleep seconds
  void sleepNs( unsigned ns ); // sleep nanoseconds
  
  
}

#endif
