#include "cwCommon.h"
#include "cwLog.h"

#include "cwCommonImpl.h"

#include <time.h>

namespace cw
{
  void _sleep( struct timespec* ts )
  {
    // TODO: consider handling errors from nanosleep
    nanosleep(ts,NULL);
  }

}

const char* cw::idToLabel( const idLabelPair_t* array, unsigned id, unsigned eolId )
{
  const idLabelPair_t* p = array;
  for(; p->id != eolId; ++p)
    if( p->id == id )
      return p->label;

  return nullptr;
}

unsigned cw::labelToId( const idLabelPair_t* array, const char* label, unsigned eolId )
{
  const idLabelPair_t* p = array;
  for(; p->id != eolId; ++p)
    if( std::strcmp(label,p->label) == 0 )
      return p->id;

  return eolId;
}





void cw::sleepSec( unsigned secs )
{
  struct timespec ts;
  ts.tv_sec  = secs;
  ts.tv_nsec = 0;

  cw::_sleep(&ts);  
}

void cw::sleepMs( unsigned ms )
{
  struct timespec ts;
  ts.tv_sec  = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;

  cw::_sleep(&ts); 
}

void cw::sleepUs( unsigned us )
{
  struct timespec ts;
  ts.tv_sec  = us / 1000000;
  ts.tv_nsec = (us % 1000000) * 1000;

  cw::_sleep(&ts);  
}

void cw::sleepNs( unsigned ns )
{
  struct timespec ts;
  ts.tv_sec  = 0;
  ts.tv_nsec = ns;

  cw::_sleep(&ts); 
}
