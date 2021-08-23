#ifndef cwLOG_H
#define cwLOG_H

namespace cw
{

  namespace log
  {
    typedef enum
    {
     kInvalid_LogLevel,
     kPrint_LogLevel,
     kDebug_LogLevel,
     kInfo_LogLevel,
     kWarning_LogLevel,
     kError_LogLevel,
     kFatal_LogLevel,
    } logLevelId_t;

    typedef handle<struct log_str> handle_t;
  
    typedef void (*logOutputCbFunc_t)( void* cbArg, unsigned level, const char* text );
    typedef void (*logFormatCbFunc_t)( void* cbArg, logOutputCbFunc_t outFunc, void* outCbArg, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* msg );
  
    rc_t create(  handle_t& hRef, unsigned level=kDebug_LogLevel, logOutputCbFunc_t outCb=nullptr, void* outCbArg=nullptr,  logFormatCbFunc_t fmtCb=nullptr, void* fmtCbArg=nullptr  );
    rc_t destroy( handle_t& hRef );

    rc_t msg( handle_t h, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* fmt, va_list vl );
    rc_t msg( handle_t h, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* fmt, ... );

    void     setLevel( handle_t h, unsigned level );
    unsigned level( handle_t h );

    const char* levelToLabel( unsigned level );
  
    void defaultOutput( void* arg, unsigned level, const char* text );
    void defaultFormatter( void* cbArg, logOutputCbFunc_t outFunc, void* outCbArg, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* msg );

    rc_t createGlobal(  unsigned level=kDebug_LogLevel, logOutputCbFunc_t outCb=nullptr, void* outCbArg=nullptr,  logFormatCbFunc_t fmtCb=nullptr, void* fmtCbArg=nullptr  );
    rc_t destroyGlobal( );
  
    void      setGlobalHandle( handle_t h );
    handle_t  globalHandle();
  }
  
}




#define cwLogVDebugH(h,rc,fmt, vl) cw::log::msg( h, cw::log::kDebug_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogDebugH( h,rc,fmt,...) cw::log::msg( h, cw::log::kDebug_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#define cwLogVPrintH(h,fmt, vl) cw::log::msg( h, cw::log::kPrint_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, cw::kOkRC, fmt, vl )
#define cwLogPrintH( h,fmt,...) cw::log::msg( h, cw::log::kPrint_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, cw::kOkRC, fmt, ##__VA_ARGS__ )

#define cwLogVInfoH(h,fmt, vl) cw::log::msg( h, cw::log::kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, cw::kOkRC, fmt, vl )
#define cwLogInfoH( h,fmt,...) cw::log::msg( h, cw::log::kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, cw::kOkRC, fmt, ##__VA_ARGS__ )

#define cwLogVWarningH(h,rc,fmt, vl) cw::log::msg( h, cw::log::kWarning_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogWarningH( h,rc,fmt,...) cw::log::msg( h, cw::log::kWarning_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#define cwLogVErrorH(h,rc,fmt, vl) cw::log::msg( h, cw::log::kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogErrorH( h,rc,fmt,...) cw::log::msg( h, cw::log::kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#define cwLogVSysErrorH(h,rc,sysRc,fmt, vl) cw::log::msg( h, cw::log::kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, sysRc, rc, fmt, vl )
#define cwLogSysErrorH( h,rc,sysRc,fmt,...) cw::log::msg( h, cw::log::kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, sysRc, rc, fmt, ##__VA_ARGS__ )

#define cwLogVFatalH(h,rc,fmt, vl) cw::log::msg( h, cw::log::kFatal_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogFatalH( h,rc,fmt,...) cw::log::msg( h, cw::log::kFatal_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#ifdef cwLOG_DEBUG

#define cwLogVDebugRC(rc,fmt, vl) cwLogVDebugH( cw::log::globalhandle(), (rc), (fmt), (vl) )
#define cwLogDebugRC( rc,fmt,...) cwLogDebugH(  cw::log::globalHandle(), (rc), (fmt), ##__VA_ARGS__ )

#define cwLogVDebug(fmt, vl)      cwLogVDebugH( cw::log::globalHandle(), cw::kOkRC, (fmt), (vl) )
#define cwLogDebug( fmt,...)      cwLogDebugH(  cw::log::globalHandle(), cw::kOkRC, (fmt), ##__VA_ARGS__ )

#else

#define cwLogVDebugRC(rc,fmt, vl)
#define cwLogDebugRC( rc,fmt,...)

#define cwLogVDebug(fmt, vl)
#define cwLogDebug( fmt,...)

#endif

#define cwLogVPrint(fmt, vl)       cwLogVPrintH( cw::log::globalHandle(), (fmt), (vl) )
#define cwLogPrint( fmt,...)       cwLogPrintH(  cw::log::globalHandle(), (fmt), ##__VA_ARGS__ )

#define cwLogVInfo(fmt, vl)       cwLogVInfoH( cw::log::globalHandle(), (fmt), (vl) )
#define cwLogInfo( fmt,...)       cwLogInfoH(  cw::log::globalHandle(), (fmt), ##__VA_ARGS__ )

#define cwLogVWarningRC(rc,fmt, vl) cwLogVWarningH( cw::log::globalHandle(), (rc), (fmt), (vl) )
#define cwLogWarningRC( rc,fmt,...) cwLogWarningH(  cw::log::globalHandle(), (rc), (fmt), ##__VA_ARGS__ )

#define cwLogVWarning(fmt, vl)    cwLogVWarningH( cw::log::globalHandle(), cw::kOkRC, (fmt), (vl) )
#define cwLogWarning( fmt,...)    cwLogWarningH(  cw::log::globalHandle(), cw::kOkRC, (fmt), ##__VA_ARGS__ )

#define cwLogVSysError(rc,sysRC,fmt, vl)   cwLogVSysErrorH( cw::log::globalHandle(), (rc), (sysRC), (fmt), (vl) )
#define cwLogSysError( rc,sysRC,fmt,...)   cwLogSysErrorH(  cw::log::globalHandle(), (rc), (sysRC), (fmt), ##__VA_ARGS__ )

#define cwLogVError(rc,fmt, vl)   cwLogVErrorH( cw::log::globalHandle(), (rc), (fmt), (vl) )
#define cwLogError( rc,fmt,...)   cwLogErrorH( cw::log::globalHandle(),  (rc), (fmt), ##__VA_ARGS__ )

#define cwLogVFatal(rc,fmt, vl)   cwLogVFatalH( cw::log::globalHandle(), (rc), (fmt), (vl) )
#define cwLogFatal( rc,fmt,...)   cwLogFatalH( cw::log::globalHandle(),  (rc), (fmt), ##__VA_ARGS__ )


// This log level is intended for debugging individual modules.
// By defining cwLOG_MODULE prior to cwLog.h in a given module these logging lines will be enabled.
#ifdef cwLOG_MODULE

#define cwLogVModRC(rc,fmt, vl) cwLogVInfoH( cw::log::globalhandle(), (rc), (fmt), (vl) )
#define cwLogModRC( rc,fmt,...) cwLogInfoH(  cw::log::globalHandle(), (rc), (fmt), ##__VA_ARGS__ )

#define cwLogVMod(fmt, vl)      cwLogVInfoH( cw::log::globalHandle(), cw::kOkRC, (fmt), (vl) )
#define cwLogMod( fmt,...)      cwLogInfoH(  cw::log::globalHandle(), cw::kOkRC, (fmt), ##__VA_ARGS__ )

#else

#define cwLogVModRC(rc,fmt, vl)
#define cwLogModRC( rc,fmt,...)

#define cwLogVMod(fmt, vl)
#define cwLogMod( fmt,...)

#endif



#endif
