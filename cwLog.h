#ifndef cwLOG_H
#define cwLOG_H

namespace cw
{

  typedef enum
  {
   kInvalid_LogLevel,
   kDebug_LogLevel,
   kInfo_LogLevel,
   kWarning_LogLevel,
   kError_LogLevel,
   kFatal_LogLevel,
  } logLevelId_t;

  typedef handle<struct log_str> logHandle_t;

  extern logHandle_t logNullHandle;
  
  typedef void (*logOutputCbFunc_t)( void* cbArg, unsigned level, const char* text );
  typedef void (*logFormatCbFunc_t)( void* cbArg, logOutputCbFunc_t outFunc, void* outCbArg, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* msg );
  
  rc_t logCreate(  logHandle_t& hRef, unsigned level=kDebug_LogLevel, logOutputCbFunc_t outCb=nullptr, void* outCbArg=nullptr,  logFormatCbFunc_t fmtCb=nullptr, void* fmtCbArg=nullptr  );
  rc_t logDestroy( logHandle_t& hRef );

  rc_t logMsg( logHandle_t h, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* fmt, va_list vl );
  rc_t logMsg( logHandle_t h, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* fmt, ... );

  void     logSetLevel( logHandle_t h, unsigned level );
  unsigned logLevel( logHandle_t h );

  const char* logLevelToLabel( unsigned level );
  
  void logDefaultOutput( void* arg, unsigned level, const char* text );
  void logDefaultFormatter( void* cbArg, logOutputCbFunc_t outFunc, void* outCbArg, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* msg );

  rc_t logCreateGlobal(  unsigned level=kDebug_LogLevel, logOutputCbFunc_t outCb=nullptr, void* outCbArg=nullptr,  logFormatCbFunc_t fmtCb=nullptr, void* fmtCbArg=nullptr  );
  rc_t logDestroyGlobal( );
  
  void         logSetGlobalHandle( logHandle_t h );
  logHandle_t  logGlobalHandle();
  
}




#define cwLogVDebugH(h,rc,fmt, vl) cw::logMsg( h, cw::kDebug_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogDebugH( h,rc,fmt,...) cw::logMsg( h, cw::kDebug_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#define cwLogVInfoH(h,rc,fmt, vl) cw::logMsg( h, cw::kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogInfoH( h,rc,fmt,...) cw::logMsg( h, cw::kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#define cwLogVWarningH(h,rc,fmt, vl) cw::logMsg( h, cw::kWarning_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogWarningH( h,rc,fmt,...) cw::logMsg( h, cw::kWarning_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#define cwLogVErrorH(h,rc,fmt, vl) cw::logMsg( h, cw::kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogErrorH( h,rc,fmt,...) cw::logMsg( h, cw::kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#define cwLogVSysErrorH(h,rc,sysRc,fmt, vl) cw::logMsg( h, cw::kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, sysRc, rc, fmt, vl )
#define cwLogSysErrorH( h,rc,sysRc,fmt,...) cw::logMsg( h, cw::kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, sysRc, rc, fmt, ##__VA_ARGS__ )

#define cwLogVFatalH(h,rc,fmt, vl) cw::logMsg( h, cw::kFatal_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogFatalH( h,rc,fmt,...) cw::logMsg( h, cw::kFatal_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#ifdef cwLOG_DEBUG

#define cwLogVDebugRC(rc,fmt, vl) cwLogVDebugH( cw::logGlobalHandle(), (rc), (fmt), (vl) )
#define cwLogDebugRC( rc,fmt,...) cwLogDebugH(  cw::logGlobalHandle(), (rc), (fmt), ##__VA_ARGS__ )

#define cwLogVDebug(fmt, vl)      cwLogVDebugH( cw::logGlobalHandle(), cw::kOkRC, (fmt), (vl) )
#define cwLogDebug( fmt,...)      cwLogDebugH(  cw::logGlobalHandle(), cw::kOkRC, (fmt), ##__VA_ARGS__ )

#else

#define cwLogVDebugRC(rc,fmt, vl)
#define cwLogDebugRC( rc,fmt,...)

#define cwLogVDebug(fmt, vl)
#define cwLogDebug( fmt,...)

#endif

#define cwLogVInfoRC(rc,fmt, vl)  cwLogVInfoH( cw::logGlobalHandle(), (rc), (fmt), (vl) )
#define cwLogInfoRC( rc,fmt,...)  cwLogInfoH(  cw::logGlobalHandle(), (rc), (fmt), ##__VA_ARGS__ )

#define cwLogVInfo(fmt, vl)       cwLogVInfoH( cw::logGlobalHandle(), cw::kOkRC, (fmt), (vl) )
#define cwLogInfo( fmt,...)       cwLogInfoH(  cw::logGlobalHandle(), cw::kOkRC, (fmt), ##__VA_ARGS__ )

#define cwLogVWarningRC(rc,fmt, vl) cwLogVWarningH( cw::logGlobalHandle(), (rc), (fmt), (vl) )
#define cwLogWarningRC( rc,fmt,...) cwLogWarningH(  cw::logGlobalHandle(), (rc), (fmt), ##__VA_ARGS__ )

#define cwLogVWarning(fmt, vl)    cwLogVWarningH( cw::logGlobalHandle(), cw::kOkRC, (fmt), (vl) )
#define cwLogWarning( fmt,...)    cwLogWarningH(  cw::logGlobalHandle(), cw::kOkRC, (fmt), ##__VA_ARGS__ )

#define cwLogVSysError(rc,sysRC,fmt, vl)   cwLogVSysErrorH( cw::logGlobalHandle(), (rc), (sysRC), (fmt), (vl) )
#define cwLogSysError( rc,sysRC,fmt,...)   cwLogSysErrorH(  cw::logGlobalHandle(), (rc), (sysRC), (fmt), ##__VA_ARGS__ )

#define cwLogVError(rc,fmt, vl)   cwLogVErrorH( cw::logGlobalHandle(), (rc), (fmt), (vl) )
#define cwLogError( rc,fmt,...)   cwLogErrorH( cw::logGlobalHandle(),  (rc), (fmt), ##__VA_ARGS__ )

#define cwLogVFatal(rc,fmt, vl)   cwLogVFatalH( cw::logGlobalHandle(), (rc), (fmt), (vl) )
#define cwLogFatal( rc,fmt,...)   cwLogFatalH( cw::logGlobalHandle(),  (rc), (fmt), ##__VA_ARGS__ )


#endif
