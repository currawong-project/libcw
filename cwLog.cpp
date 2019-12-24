#include "cwCommon.h"
#include "cwLog.h"

#include "cwCommonImpl.h"
#include "cwMem.h"

namespace cw
{

  typedef struct log_str
  {
    logOutputCbFunc_t outCbFunc;
    void*             outCbArg;
    logFormatCbFunc_t fmtCbFunc;
    void*             fmtCbArg;
    unsigned          level;
  } log_t;


  logHandle_t __logGlobalHandle__;
  logHandle_t logNullHandle;
  
#define logHandleToPtr(h) handleToPtr<logHandle_t,log_t>(h)

  idLabelPair_t logLevelLabelArray[] =
  {
   { kDebug_LogLevel, "debug" },
   { kInfo_LogLevel,    "info" },
   { kWarning_LogLevel, "warning" },
   { kError_LogLevel,   "error" },
   { kFatal_LogLevel,  "fatal" },
   { kInvalid_LogLevel, "<invalid>" }
  };
  
}



cw::rc_t cw::logCreate(  logHandle_t& hRef, unsigned level, logOutputCbFunc_t outCbFunc, void* outCbArg,  logFormatCbFunc_t fmtCbFunc, void* fmtCbArg  )
{
  rc_t rc;
  if((rc = logDestroy(hRef)) != kOkRC)
    return rc;

  log_t* p = memAllocZ<log_t>();
  p->outCbFunc = outCbFunc == nullptr ? logDefaultOutput : outCbFunc;
  p->outCbArg  = outCbArg;
  p->fmtCbFunc = fmtCbFunc == nullptr ? logDefaultFormatter : fmtCbFunc;
  p->fmtCbArg  = fmtCbArg;
  p->level     = level;
  hRef.p = p;
  
  return rc; 
}

cw::rc_t cw::logDestroy( logHandle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if( hRef.p == nullptr )
    return rc;

  memRelease( hRef.p );
  return rc;  
}

cw::rc_t cw::logMsg( logHandle_t h, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* fmt, va_list vl )
{
  log_t* p = logHandleToPtr(h);

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

cw::rc_t cw::logMsg( logHandle_t h, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t returnCode, const char* fmt, ... )
{
  rc_t rc;
  va_list vl;
  va_start(vl,fmt);
  rc = logMsg( h, level, function, filename, line, systemErrorCode, returnCode, fmt, vl );
  va_end(vl);
  return rc;
}

void     cw::logSetLevel( logHandle_t h, unsigned level )
{
  log_t* p = logHandleToPtr(h);
  p->level = level;
}

unsigned cw::logLevel( logHandle_t h )
{
  log_t* p = logHandleToPtr(h);
  return p->level;
}


const char* cw::logLevelToLabel( unsigned level )
{
  const char* label;
  if((label = idToLabel(logLevelLabelArray,level,kInvalid_LogLevel)) == nullptr)
    label = "<unknown>";
  
  return label;
}



void  cw::logDefaultOutput( void* arg, unsigned level, const char* text )
{
  FILE* f = level >= kWarning_LogLevel ? stderr : stdout;
  fprintf(f,"%s",text);
}

void cw::logDefaultFormatter( void* cbArg, logOutputCbFunc_t outFunc, void* outCbArg, unsigned level, const char* function, const char* filename, unsigned lineno, int sys_errno, rc_t rc, const char* msg )
{
  // TODO: This code is avoids the use of dynamic memory allocation but relies on stack allocation. It's a security vulnerability.
  //       
  
  const char* systemLabel = sys_errno==0 ? "" : "System Error: ";
  const char* systemMsg   = sys_errno==0 ? "" : strerror(sys_errno);
  const char* levelStr    = logLevelToLabel(level);
  
  const char* rcFmt = "rc:%i";
  int rcn = snprintf(nullptr,0,rcFmt,rc);
  char rcs[rcn+1];
  int rcm = snprintf(rcs,rcn+1,rcFmt,rc);
  cwAssert(rcn==rcm);
  const char* rcStr = rcs;

  const char* syFmt = "%s%s";
  int syn = snprintf(nullptr,0,syFmt,systemLabel,systemMsg);
  char sys[syn+1];
  int sym = snprintf(sys,syn+1,syFmt,systemLabel,systemMsg);
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
    loStr = "";

  // dont' print the rc msg if this is info or debug
  if( level < kWarning_LogLevel )
    rcStr = "";

  // levelStr, msg,sys_msg, rc, function, lineno, filename 
  const char* fmt = "%s: %s %s %s %s\n";

  int  n = snprintf(nullptr,0,fmt,levelStr,msg,syStr,rcStr,loStr);
  cwAssert(n != -1);
  char s[n+1];
  int m = snprintf(s,n+1,fmt,levelStr,msg,syStr,rcStr,loStr);
  cwAssert(m==n);

    outFunc(outCbArg,level,s);   
  
}


cw::rc_t cw::logCreateGlobal(  unsigned level, logOutputCbFunc_t outCb, void* outCbArg,  logFormatCbFunc_t fmtCb, void* fmtCbArg  )
{
  logHandle_t h;
  rc_t rc;
  
  if((rc = logCreate(h, level, outCb, outCbArg, fmtCb, fmtCbArg  )) == kOkRC )
    logSetGlobalHandle(h);

  return rc;

}

cw::rc_t cw::logDestroyGlobal()
{
  return logDestroy(__logGlobalHandle__);
}


void cw::logSetGlobalHandle( logHandle_t h )
{
  __logGlobalHandle__ = h;
}

cw::logHandle_t cw::logGlobalHandle()
{
  return __logGlobalHandle__;
}
