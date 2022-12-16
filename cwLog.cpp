#include "cwCommon.h"
#include "cwLog.h"

#include "cwCommonImpl.h"
#include "cwMem.h"

namespace cw
{
  namespace log
  {
    typedef struct log_str
    {
      logOutputCbFunc_t outCbFunc;
      void*             outCbArg;
      logFormatCbFunc_t fmtCbFunc;
      void*             fmtCbArg;
      unsigned          level;
    } log_t;


    handle_t __logGlobalHandle__;

    idLabelPair_t logLevelLabelArray[] =
    {
     { kPrint_LogLevel, "" },
     { kDebug_LogLevel,   "debug" },
     { kInfo_LogLevel,    "info" },
     { kWarning_LogLevel, "warning" },
     { kError_LogLevel,   "error" },
     { kFatal_LogLevel,  "fatal" },
     { kInvalid_LogLevel, "<invalid>" }
    };
  
    log_t* _handleToPtr( handle_t h ) { return handleToPtr<handle_t,log_t>(h); }
  }
}




cw::rc_t cw::log::create(  handle_t& hRef, unsigned level, logOutputCbFunc_t outCbFunc, void* outCbArg,  logFormatCbFunc_t fmtCbFunc, void* fmtCbArg  )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC)
    return rc;

  log_t* p = mem::allocZ<log_t>();
  p->outCbFunc = outCbFunc == nullptr ? defaultOutput : outCbFunc;
  p->outCbArg  = outCbArg;
  p->fmtCbFunc = fmtCbFunc == nullptr ? defaultFormatter : fmtCbFunc;
  p->fmtCbArg  = fmtCbArg;
  p->level     = level;
  hRef.p = p;
  
  return rc; 
}

cw::rc_t cw::log::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if( hRef.p == nullptr )
    return rc;

  mem::release( hRef.p );
  return rc;  
}

cw::rc_t cw::log::msg( handle_t h, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* fmt, va_list vl )
{
  log_t* p = _handleToPtr(h);

  va_list     vl1;
  va_copy(vl1,vl);
  
  int n = vsnprintf(nullptr,0,fmt,vl);
  cwAssert(n != -1);

  if( n != -1 )
  {

    char msg[n+1]; // add 1 to allow space for the terminating zero
    int m = vsnprintf(msg,n+1,fmt,vl1);
    cwAssert(m==n);
    
    p->fmtCbFunc( p->fmtCbArg, p->outCbFunc, p->outCbArg, level, function, filename, line, systemErrorCode, rc, msg );
  }

  va_end(vl1);
  return rc;  
}

cw::rc_t cw::log::msg( handle_t h, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t returnCode, const char* fmt, ... )
{
  rc_t rc;
  va_list vl;
  va_start(vl,fmt);
  rc = msg( h, level, function, filename, line, systemErrorCode, returnCode, fmt, vl );
  va_end(vl);
  return rc;
}

void     cw::log::setLevel( handle_t h, unsigned level )
{
  log_t* p = _handleToPtr(h);
  p->level = level;
}

unsigned cw::log::level( handle_t h )
{
  log_t* p = _handleToPtr(h);
  return p->level;
}

void cw::log::setOutputCb( handle_t h, logOutputCbFunc_t outFunc, void* outCbArg )
{
  log_t* p = _handleToPtr(h);
  p->outCbFunc = outFunc;
  p->outCbArg  = outCbArg;
}

void cw::log::setFormatCb( handle_t h, logFormatCbFunc_t fmtFunc, void* fmtCbArg )
{
  log_t* p = _handleToPtr(h);
  p->fmtCbFunc = fmtFunc;
  p->fmtCbArg = fmtCbArg;
  
}


const char* cw::log::levelToLabel( unsigned level )
{
  const char* label;
  if((label = idToLabel(logLevelLabelArray,level,kInvalid_LogLevel)) == nullptr)
    label = "<unknown>";
  
  return label;
}



void  cw::log::defaultOutput( void* arg, unsigned level, const char* text )
{
  FILE* f = level >= kWarning_LogLevel ? stderr : stdout;
  fprintf(f,"%s",text);
  fflush(f);
}

void cw::log::defaultFormatter( void* cbArg, logOutputCbFunc_t outFunc, void* outCbArg, unsigned level, const char* function, const char* filename, unsigned lineno, int sys_errno, rc_t rc, const char* msg )
{
  // TODO: This code is avoids the use of dynamic memory allocation but relies on stack allocation. It's a security vulnerability.
  //       
  
  const char* systemLabel = sys_errno==0 ? "" : "System Error: ";
  const char* systemMsg   = sys_errno==0 ? "" : strerror(sys_errno);
  const char* levelStr    = levelToLabel(level);
  
  const char* rcFmt = "rc:%i";
  int rcn = snprintf(nullptr,0,rcFmt,rc);
  char rcs[rcn+1];
  int rcm = snprintf(rcs,rcn+1,rcFmt,rc);
  cwAssert(rcn==rcm);
  const char* rcStr = rcs;

  const char* syFmt = "%s (%i) %s";
  int syn = snprintf(nullptr,0,syFmt,systemLabel,sys_errno,systemMsg);
  char sys[syn+1];
  int sym = snprintf(sys,syn+1,syFmt,systemLabel,sys_errno,systemMsg);
  cwAssert(syn==sym);
  const char* syStr = sys;

  const char* loFmt = "%s line:%i %s";
  int  lon = snprintf(nullptr,0,loFmt,function,lineno,filename);
  char los[lon+1];
  int  lom = snprintf(los,lon+1,loFmt,function,lineno,filename);
  cwAssert(lon==lom);
  const char* loStr = los;

  // don't print the function,file,line when this is an 'info' msg.
  if( level == kInfo_LogLevel )
  {
    loStr = "";
    syStr = "";
  }
  
  // dont' print the rc msg if this is info or debug
  if( level < kWarning_LogLevel )
    rcStr = "";

  if( level == kPrint_LogLevel )
  {
    const char* fmt = "%s";
    int  n = snprintf(nullptr,0,fmt,msg);
    cwAssert(n != -1);
    char s[n+1];
    int m = snprintf(s,n+1,fmt,msg);
    cwAssert(m==n);
    outFunc(outCbArg,level,s);     
  }
  else
  {
    // levelStr, msg,sys_msg, rc, function, lineno, filename 
    const char* fmt = "%s: %s %s %s %s\n";

    int  n = snprintf(nullptr,0,fmt,levelStr,msg,syStr,rcStr,loStr);
    cwAssert(n != -1);
    char s[n+1];
    int m = snprintf(s,n+1,fmt,levelStr,msg,syStr,rcStr,loStr);
    cwAssert(m==n);
    outFunc(outCbArg,level,s);     
  }
  
}


cw::rc_t cw::log::createGlobal(  unsigned level, logOutputCbFunc_t outCb, void* outCbArg,  logFormatCbFunc_t fmtCb, void* fmtCbArg  )
{
  handle_t h;
  rc_t rc;
  
  if((rc = create(h, level, outCb, outCbArg, fmtCb, fmtCbArg  )) == kOkRC )
    setGlobalHandle(h);

  return rc;
}

cw::rc_t cw::log::destroyGlobal()
{
  return destroy(__logGlobalHandle__);
}


void cw::log::setGlobalHandle( handle_t h )
{
  __logGlobalHandle__ = h;
}

cw::log::handle_t cw::log::globalHandle()
{
  return __logGlobalHandle__;
}
