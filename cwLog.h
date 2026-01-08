//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwLOG_H
#define cwLOG_H

namespace cw
{

  namespace log
  {
    
    typedef enum
    {
     kInvalid_LogLevel,
     kDebug_LogLevel,
     kInfo_LogLevel,
     kWarning_LogLevel,
     kError_LogLevel,
     kFatal_LogLevel,
     kPrint_LogLevel,
     kMax_LogLevel = kPrint_LogLevel
    } logLevelId_t;

    enum {
      kNoFlags         = 0x00,
      kDateTimeFl      = 0x01,  // Print the date/time the log message was generated.
      kFileOutFl       = 0x02,  // Send the log to a file.
      kConsoleFl       = 0x04,  // Print the log to the console.
      kSkipQueueFl     = 0x08,  // Print the log message immediately, don't queue the results for later output from exec().
      kOverwriteFileFl = 0x10,  // Turn off automatic log file versioning, instead use the 'log_fname' parameter literally and overwrite it if it exists.
    };

    const unsigned     kDefaultQueueBlkCnt     = 16;
    const unsigned     kDefaultQueueBlkByteCnt = 4096;
    const logLevelId_t kDefault_LogLevel        = kPrint_LogLevel;
     
    typedef handle<struct log_str> handle_t;
  
    typedef void (*logOutputCbFunc_t)( void* cbArg, unsigned level, const char* text );
    typedef void (*logFormatCbFunc_t)( void* cbArg, logOutputCbFunc_t outFunc, void* outCbArg, unsigned flags, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* msg );

    logLevelId_t levelFromString( const char* label );
    const char*  levelToString( logLevelId_t level );

    typedef struct log_args_str
    {
      unsigned          flags;
      logLevelId_t      level;
      const char*       log_fname;
      unsigned          queueBlkCnt;
      unsigned          queueBlkByteCnt;
      logOutputCbFunc_t outCbFunc;
      void*             outCbArg;
      logFormatCbFunc_t fmtCbFunc;
      void*             fmtCbArg;      
    } log_args_t;

    // Setup the arguments for a minimal, direct to console, log.
    void init_minimum_args( log_args_t& args );

    // Setup the arguments for default use with the use of the internal queue.
    void init_default_args( log_args_t& args );
    
    rc_t create(  handle_t& hRef, const log_args_t& args );
    
    rc_t destroy( handle_t& hRef );

    rc_t exec( handle_t h );

    enum { kNoMsgFlags=0, kNoLevelCheckMsgFl=0x01 };
    
    rc_t msg( handle_t h, unsigned flags, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* fmt, va_list vl );
    rc_t msg( handle_t h, unsigned flags, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t rc, const char* fmt, ... );

    void         setLevel( handle_t h, logLevelId_t level );
    logLevelId_t level( handle_t h );

    void*             outputCbArg( handle_t h );
    logOutputCbFunc_t outputCb( handle_t h );
    
    void*             formatCbArg( handle_t h );
    logFormatCbFunc_t formatCb( handle_t h );
    
    void setOutputCb( handle_t h, logOutputCbFunc_t outFunc, void* outCbArg );
    void setFormatCb( handle_t h, logFormatCbFunc_t fmtFunc, void* fmtCbArg );

    const char* levelToLabel( unsigned level );

    unsigned flags( handle_t h );
    void set_flags( handle_t h, unsigned flags );
  
    void defaultOutput( void* arg, unsigned level, const char* text );
    void defaultFormatter( void*             cbArg,
                           logOutputCbFunc_t outFunc,
                           void*             outCbArg,
                           unsigned          flags,
                           unsigned          level,
                           const char*       function,
                           const char*       filename,
                           unsigned          line,
                           int               systemErrorCode,
                           rc_t              rc,
                           const char*       msg );

    rc_t createGlobal(  const log_args_t& args  );
    
    rc_t destroyGlobal( );
  
    void      setGlobalHandle( handle_t h );
    handle_t  globalHandle();
  }
  
}




#define cwLogVDebugH(h,rc,fmt, vl) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kDebug_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogDebugH( h,rc,fmt,...) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kDebug_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#define cwLogVPrintH(h,fmt, vl) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kPrint_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, cw::kOkRC, fmt, vl )
#define cwLogPrintH( h,fmt,...) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kPrint_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, cw::kOkRC, fmt, ##__VA_ARGS__ )

#define cwLogVInfoH(h,fmt, vl) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, cw::kOkRC, fmt, vl )
#define cwLogInfoH( h,fmt,...) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, cw::kOkRC, fmt, ##__VA_ARGS__ )

#define cwLogVWarningH(h,rc,fmt, vl) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kWarning_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogWarningH( h,rc,fmt,...) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kWarning_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#define cwLogVErrorH(h,rc,fmt, vl) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogErrorH( h,rc,fmt,...) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#define cwLogVSysErrorH(h,rc,sysRc,fmt, vl) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, sysRc, rc, fmt, vl )
#define cwLogSysErrorH( h,rc,sysRc,fmt,...) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kError_LogLevel, __FUNCTION__, __FILE__, __LINE__, sysRc, rc, fmt, ##__VA_ARGS__ )

#define cwLogVFatalH(h,rc,fmt, vl) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kFatal_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, vl )
#define cwLogFatalH( h,rc,fmt,...) cw::log::msg( h, cw::log::kNoMsgFlags, cw::log::kFatal_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, rc, fmt, ##__VA_ARGS__ )

#define cwLogVDebugRC(rc,fmt, vl) cwLogVDebugH( cw::log::globalhandle(), (rc), (fmt), (vl) )
#define cwLogDebugRC( rc,fmt,...) cwLogDebugH(  cw::log::globalHandle(), (rc), (fmt), ##__VA_ARGS__ )

#define cwLogVDebug(fmt, vl)      cwLogVDebugH( cw::log::globalHandle(), cw::kOkRC, (fmt), (vl) )
#define cwLogDebug( fmt,...)      cwLogDebugH(  cw::log::globalHandle(), cw::kOkRC, (fmt), ##__VA_ARGS__ )

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

#define cwLogVModRC(rc,fmt, vl) cw::log::msg( cw::log::globalHandle(), cw::log::kNoMsgFlags, cw::log::kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, (rc), (fmt), (vl) )
#define cwLogModRC( rc,fmt,...) cw::log::msg( cw::log::globalHandle(), cw::log::kNoMsgFlags, cw::log::kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, (rc), (fmt), ##__VA_ARGS__ )

#define cwLogVMod(fmt, vl)      cw::log::msg( cw::log::globalHandle(), cw::log::kNoMsgFlags, cw::log::kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, cw::kOkRC, (fmt), (vl) )
#define cwLogMod( fmt,...)      cw::log::msg( cw::log::globalHandle(), cw::log::kNoMsgFlags, cw::log::kInfo_LogLevel, __FUNCTION__, __FILE__, __LINE__, 0, cw::kOkRC, (fmt), ##__VA_ARGS__ )

#else

#define cwLogVModRC(rc,fmt, vl)
#define cwLogModRC( rc,fmt,...)

#define cwLogVMod(fmt, vl)
#define cwLogMod( fmt,...)

#endif



#endif
