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


#define cwCountOf(a) (sizeof(a)/sizeof(a[0]))

// Taken from here:
// https://groups.google.com/forum/#!topic/comp.std.c/d-6Mj5Lko_s
// and here:
// https://stackoverflow.com/questions/4421681/how-to-count-the-number-of-arguments-passed-to-a-function-that-accepts-a-variabl

#define cwPP_NARG(...) \
         cwPP_NARG_(__VA_ARGS__,cwPP_RSEQ_N())

#define cwPP_NARG_(...) \
         cwPP_128TH_ARG(__VA_ARGS__)

#define cwPP_128TH_ARG( \
          _1, _2, _3, _4, _5, _6, _7, _8, _9,_10, \
         _11,_12,_13,_14,_15,_16,_17,_18,_19,_20, \
         _21,_22,_23,_24,_25,_26,_27,_28,_29,_30, \
         _31,_32,_33,_34,_35,_36,_37,_38,_39,_40, \
         _41,_42,_43,_44,_45,_46,_47,_48,_49,_50, \
         _51,_52,_53,_54,_55,_56,_57,_58,_59,_60, \
         _61,_62,_63,_64,_65,_66,_67,_68,_69,_70, \
         _71,_72,_73,_74,_75,_76,_77,_78,_79,_80, \
         _81,_82,_83,_84,_85,_86,_87,_88,_89,_90, \
         _91,_92,_93,_94,_95,_96,_97,_98,_99,_100, \
         _101,_102,_103,_104,_105,_106,_107,_108,_109,_110, \
         _111,_112,_113,_114,_115,_116,_117,_118,_119,_120, \
         _121,_122,_123,_124,_125,_126,_127,N,...) N
#define cwPP_RSEQ_N() \
         127,126,125,124,123,122,121,120, \
         119,118,117,116,115,114,113,112,111,110, \
         109,108,107,106,105,104,103,102,101,100, \
         99,98,97,96,95,94,93,92,91,90, \
         89,88,87,86,85,84,83,82,81,80, \
         79,78,77,76,75,74,73,72,71,70, \
         69,68,67,66,65,64,63,62,61,60, \
         59,58,57,56,55,54,53,52,51,50, \
         49,48,47,46,45,44,43,42,41,40, \
         39,38,37,36,35,34,33,32,31,30, \
         29,28,27,26,25,24,23,22,21,20, \
         19,18,17,16,15,14,13,12,11,10, \
         9,8,7,6,5,4,3,2,1,0


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
