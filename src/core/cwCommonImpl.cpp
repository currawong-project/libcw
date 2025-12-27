//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"

#include "cwCommonImpl.h"

#if cwWEBSOCK
#include <libwebsockets.h>
#endif

#if cwFFTW
#include <fftw3.h>
#endif

#if cwALSA
#include <alsa/asoundlib.h>
#endif

#if cwMKL
#include <mkl.h>
#endif

#include <time.h>

namespace cw
{
  void _sleep( struct timespec* ts )
  {
    // TODO: consider handling errors from nanosleep
    nanosleep(ts,NULL);
  }

  const idLabelPair_t* _idToSlot( const idLabelPair_t* array, unsigned id, unsigned eolId )
  {
    const idLabelPair_t* p = array;
    for(; p->id != eolId; ++p)
      if( p->id == id )
        break;
    
    return p;    
  }

  
}

void cw::report_dependecy_versions()
{
#if cwWEBSOCK
  cwLogInfo("libwebsockets version: %i.%i.%i : %i : '%s'",LWS_LIBRARY_VERSION_MAJOR,LWS_LIBRARY_VERSION_MINOR,LWS_LIBRARY_VERSION_PATCH,LWS_LIBRARY_VERSION_NUMBER,cwStringNullGuard(lws_get_library_version()));
#else
  cwLogInfo("libwebsock is not available.");
#endif

#if cwFFTW
  cwLogInfo("FFTW version:'%s'",fftw_version);
#else    
  cwLogInfo("FFTW is not available.");      
#endif

#if cwALSA
  cwLogInfo("ALSA version:'%s'",SND_LIB_VERSION_STR);
#else    
  cwLogInfo("ALSA is not available.");         
#endif

#if cwMKL
  MKLVersion ver;
  mkl_get_version(&ver);
  cwLogInfo("Intel Math Kernel library version: %d.%d Update %d\\n", ver.MajorVersion, ver.MinorVersion, ver.UpdateVersion);
#else
  cwLogInfo("Intel Math Kernel library is not available.");
#endif
 
}


const char* cw::idToLabelNull( const idLabelPair_t* array, unsigned id, unsigned eolId )
{
  const idLabelPair_t* p = _idToSlot(array,id,eolId);

  return p->id == eolId ? nullptr : p->label;
}

const char* cw::idToLabel( const idLabelPair_t* array, unsigned id, unsigned eolId )
{
  const idLabelPair_t* p = _idToSlot(array,id,eolId);

  return p->label;
}

unsigned cw::labelToId( const idLabelPair_t* array, const char* label, unsigned eolId )
{
  const idLabelPair_t* p = array;

  if( label != nullptr )
    for(; p->id != eolId; ++p)
      if( p->label != nullptr && std::strcmp(label,p->label) == 0 )
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
