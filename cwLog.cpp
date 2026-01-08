//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"

#include "cwLog.h"

#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwNbMpScQueue.h"
#include "cwText.h"
#include "cwFile.h"
#include "cwFileSys.h"

namespace cw
{
  namespace log
  {
    typedef struct blob_hdr_str
    {
      unsigned level;            // 0-3
      unsigned line;             // 4-7 
      int      systemErrorCode;  // 8-11
      rc_t     rc;               //12-16
    } blob_hdr_t;
    
    typedef struct log_str
    {
      logOutputCbFunc_t outCbFunc;
      void*             outCbArg;
      logFormatCbFunc_t fmtCbFunc;
      void*             fmtCbArg;
      logLevelId_t      level;
      unsigned          flags;
      nbmpscq::handle_t qH;
      file::handle_t    fileH;
    } log_t;


    handle_t __logGlobalHandle__;

    idLabelPair_t logLevelLabelArray[] =
    {
     { kPrint_LogLevel, "" },
     { kDebug_LogLevel,   "debug" },
     { kInfo_LogLevel,    "info" },
     { kWarning_LogLevel, "warn" },
     { kError_LogLevel,   "error" },
     { kFatal_LogLevel,   "fatal" },
     { kInvalid_LogLevel, "<invalid>" }
    };
  
    log_t* _handleToPtr( handle_t h ) { return handleToPtr<handle_t,log_t>(h); }

    rc_t _enqueue_msg( log_t* p, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t result_code, const char* msg )
    {
      rc_t rc = kOkRC;

      // determine the size of the blob to enqueue
      unsigned func_char_cnt  = textLength(function) + 1;
      unsigned fname_char_cnt = textLength(filename) + 1;
      unsigned msg_char_cnt   = textLength(msg) + 1;
      unsigned blob_byte_cnt  = sizeof(blob_hdr_t) + func_char_cnt + fname_char_cnt + msg_char_cnt;

      // allocate blob memory
      uint8_t     blob[ blob_byte_cnt ];

      // determine the formatting of the blob
      blob_hdr_t* hdr            = (blob_hdr_t*)blob;
      char*       blob_func_name = (char*)(blob + sizeof(blob_hdr_t));
      char*       blob_file_name = blob_func_name + func_char_cnt;
      char*       blob_msg       = blob_file_name + fname_char_cnt;

      assert( blob_msg + msg_char_cnt <= (char*)(blob + blob_byte_cnt) );
      
      // serialize the message into blob[]
      hdr->level           = level;
      hdr->line            = line;
      hdr->systemErrorCode = systemErrorCode;
      hdr->rc              = result_code;

      strncpy(blob_func_name,function,func_char_cnt);
      strncpy(blob_file_name,filename,fname_char_cnt);
      strncpy(blob_msg,msg,msg_char_cnt);

      // push the blob
      if((rc = nbmpscq::push(p->qH,blob,blob_byte_cnt)) != kOkRC )
      {
        p->flags = cwClrFlag(p->flags,kSkipQueueFl);
        
        // don't use the log to report this error because it will recurse.
        cwLogError(rc,"log queue push failed.");
      }
      
      return rc;      
    }

    // Output handler when msg() is called but the log handle is not yet valid.
    void _do_output_no_handle( void* cbArg, unsigned level, const char* text )
    {
      fprintf(stderr,"%s",text);
    }

    void _console_output( unsigned level, const char* text )
    {
      FILE* f = level >= kWarning_LogLevel ? stderr : stdout;
      fprintf(f,"%s",text);
      fflush(f);
      
    }
    
    void _do_output( void* cbArg, unsigned level, const char* text )
    {
      log_t* p = (log_t*)cbArg;

      if( p == nullptr )
        _console_output(level,text);
      else
      {
        if( cwIsFlag(p->flags,kFileOutFl) && p->fileH.isValid() )
          file::print(p->fileH,text);

        if( p->outCbFunc != nullptr )
          p->outCbFunc(p->outCbArg, level, text );

        if( cwIsFlag(p->flags,kConsoleFl) && p->outCbFunc != defaultOutput )
          _console_output(level,text);

      }
    }

    rc_t _exec( log_t* p )
    {
      rc_t rc = kOkRC;
      
      while( !is_empty(p->qH) )
      {
        // get the next log message from the queue
        nbmpscq::blob_t b = get(p->qH);
    
        if( b.rc != kOkRC )
        {
          p->flags = cwClrFlag(p->flags,kSkipQueueFl);      
          rc = cwLogError(b.rc,"Log message dequeue failed. Log message may have been lost. The queue has been disabled.");
          break;
        }

        const blob_hdr_t* hdr   = (const blob_hdr_t*)(b.blob);
        const char*       func  = (const char*)(hdr+1);
        const char*       fname = func + strlen(func) + 1;
        const char*       msg   = fname + strlen(fname) + 1;

        p->fmtCbFunc( p->fmtCbArg, _do_output, p, p->flags, hdr->level, func, fname, hdr->line, hdr->systemErrorCode, hdr->rc, msg );

        b = advance(p->qH);

        if( b.rc != kOkRC )
        {
          p->flags = cwClrFlag(p->flags,kSkipQueueFl);      
          rc = cwLogError(b.rc,"Log message queue advance failed. Log messages may have been lost. The queue has been disabled.");
          break;
        }
      }

      return rc;    
  }
    

    rc_t _destroy( log_t* p )
    {
      rc_t rc0 = kOkRC, rc1 = kOkRC;

      // if the queue is valid ...
      if( p->qH.isValid() )
      {
        // ... then flush it's contents
        p->flags = kConsoleFl;
        _exec(p);
      }
      

      // Disable file and queue use because we are about to destroy the queue and file.
      p->flags = cwClrFlag(p->flags,kSkipQueueFl);
      p->flags = cwClrFlag(p->flags,kFileOutFl);
      
      if((rc0 = nbmpscq::destroy(p->qH)) != kOkRC )
        rc0 = cwLogError(rc0,"The log internal queue destroy failed.");
      
      if((rc1 = file::close(p->fileH)) != kOkRC )
        rc1 = cwLogError(rc1,"The log file close failed.");

      return rcSelect(rc0,rc1);
    }

    rc_t _create_file( log_t* p, const char* log_filename )
    {
      rc_t                 rc        = kOkRC;
      char*                fname     = nullptr;
      filesys::pathPart_t* pp        = nullptr;
      char*                log_fname = nullptr;
      
      // if file backing was not requested
      if( !cwIsFlag(p->flags,kFileOutFl) )
        goto errLabel;
      
      // if the log filename is invalid
      if( textLength(log_filename) == 0 )
      {
        // Using cwLogWarning() here is safe because the log handle is not yet set therefore the message will be sent straight to the console.
        cwLogError(kInvalidArgRC,"The log file was enabled but no filename was given.  The log file has been disabled.");
        p->flags = cwClrFlag(p->flags,kFileOutFl);
        goto errLabel;
      }

      // expand the path to process a leading tilde
      log_fname = filesys::expandPath(log_filename);

      // if file versioning was not requested
      if( cwIsFlag(p->flags,kOverwriteFileFl) )
        fname = mem::duplStr(log_fname);
      else
      {
        // ... otherwise file versioning was requsted        
        if((pp = filesys::pathParts(log_fname)) == nullptr )
        {
          // Using cwLogError() here is safe because the log handle is not yet set therefore the message will be sent straight to the console.
          rc = cwLogError(kOpFailRC,"The log filename '%s' could not be parsed.",cwStringNullGuard(log_fname));
          goto errLabel;
        }

        if((fname = filesys::makeVersionedFn( pp->dirStr, pp->fnStr, pp->extStr, nullptr )) == nullptr )
        {
        // Using cwLogError() here is safe because the log handle is not yet set therefore the message will be sent straight to the console.
          rc = cwLogError(kOpFailRC,"A versioned variation on the file name '%s' could not be created.",cwStringNullGuard(log_fname));
          goto errLabel;
        }

      }

      // create the log file
      if((rc = file::open(p->fileH,fname, file::kWriteFl)) != kOkRC )
      {
        // Using cwLogError() here is safe because the log handle is not yet set therefore the message will be sent straight to the console.
        rc = cwLogError(rc,"The log file create failed on '%s'.",cwStringNullGuard(log_fname));
        goto errLabel;      
      }

      
    errLabel:
      mem::release(log_fname);
      mem::release(fname);
      mem::release(pp);
      return rc;
    }
    
  }
}

void cw::log::init_minimum_args( log_args_t& args )
{
  args.flags           = kConsoleFl | kSkipQueueFl;
  args.level           = kInfo_LogLevel;
  args.log_fname       = nullptr;
  args.queueBlkCnt     = 0;
  args.queueBlkByteCnt = 0;
  args.outCbFunc       = nullptr;
  args.outCbArg        = nullptr;
  args.fmtCbFunc       = nullptr;  
}

void cw::log::init_default_args( log_args_t& args )
{
  init_minimum_args(args);

  args.flags           = kNoFlags;
  args.queueBlkCnt     = kDefaultQueueBlkCnt;
  args.queueBlkByteCnt = kDefaultQueueBlkByteCnt;
  args.level           = kDefault_LogLevel;  
}

cw::rc_t cw::log::create(  handle_t&         hRef, const log_args_t& args )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC)
    return rc;

  log_t* p = mem::allocZ<log_t>();

  // create the internal queue
  if((rc = nbmpscq::create(p->qH,args.queueBlkCnt,args.queueBlkByteCnt)) != kOkRC )
  {
    // Using cwLogError() here is safe because the log handle is not yet set therefore the message will be sent straight to the console.
    rc = cwLogError(rc,"The internal log queue create failed.");
    goto errLabel;
  }

  
  p->outCbFunc = args.outCbFunc == nullptr ? defaultOutput    : args.outCbFunc;
  p->outCbArg  = args.outCbFunc == nullptr ? p                : args.outCbArg;
  p->fmtCbFunc = args.fmtCbFunc == nullptr ? defaultFormatter : args.fmtCbFunc;
  p->fmtCbArg  = args.fmtCbFunc == nullptr ? p                : args.fmtCbArg;
  p->level     = args.level;
  p->flags     = args.flags;

  // if the log should be backed by a file.
  if((rc = _create_file(p,args.log_fname)) != kOkRC )
    goto errLabel;
  
  hRef.set(p);

errLabel:
  if( rc != kOkRC )
    _destroy(p);
      
  return rc; 
}

cw::log::logLevelId_t cw::log::levelFromString( const char* label )
{
  for(unsigned i=0; logLevelLabelArray[i].id != kInvalid_LogLevel; ++i)
    if( strcasecmp(label,logLevelLabelArray[i].label) == 0 )
      return (logLevelId_t)logLevelLabelArray[i].id;
  return kInvalid_LogLevel;
}

const char* cw::log::levelToString( logLevelId_t level )
{
  for(unsigned i=0; logLevelLabelArray[i].id != kInvalid_LogLevel; ++i)
    if( logLevelLabelArray[i].id == level )
      return logLevelLabelArray[i].label;
  return nullptr;
}


cw::rc_t cw::log::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  log_t* p = nullptr;
  
  if( !hRef.isValid() )
    return rc;

  p = _handleToPtr(hRef);

  rc = _destroy(p);
  
  mem::release( p );
  hRef.clear();
  
  return rc;  
}

cw::rc_t cw::log::msg( handle_t h, unsigned flags, unsigned level, const char* function, const char* filename, unsigned line, int systemErrorCode, rc_t result_code, const char* fmt, va_list vl )
{
  log_t*  p        = _handleToPtr(h);
  
  // It is possible that this function is called prior to the handle pointer being set.
  // In this case the log message goes directly to the console.
  bool    level_fl = p ==nullptr ||  level >= p->level;

  // if we didn't pass the level check then return without logging
  if( level_fl )
  {  
    va_list vl1;
    va_copy(vl1,vl);
  
    int n = vsnprintf(nullptr,0,fmt,vl);
    cwAssert(n != -1);

    if( n != -1 )
    {
      char msg[n+1]; // add 1 to allow space for the terminating zero
      int m = vsnprintf(msg,n+1,fmt,vl1);
      cwAssert(m==n);

      // if the log has not yet been created.
      if( p == nullptr )
        defaultFormatter( nullptr, _do_output_no_handle, nullptr, 0, level, function, filename, line, systemErrorCode, result_code, msg );
      else
      {
        // if the queue is not being used
        if( cwIsFlag(p->flags,kSkipQueueFl ) )
        {
          p->fmtCbFunc( p->fmtCbArg, _do_output, p, p->flags, level, function, filename, line, systemErrorCode, result_code, msg );
        }
        else
        {
          // enqueue the log message for output in exec()
          _enqueue_msg( p, level, function, filename, line, systemErrorCode, result_code, msg );
        }
      } 
    }
    
    va_end(vl1);

  }
  return result_code;
}

cw::rc_t cw::log::msg( handle_t    h,
                       unsigned    flags,
                       unsigned    level,
                       const char* function,
                       const char* filename,
                       unsigned    line,
                       int         systemErrorCode,
                       rc_t        resultCode,
                       const char* fmt, ... )
{
  va_list vl;
  va_start(vl,fmt);
  msg( h, flags, level, function, filename, line, systemErrorCode, resultCode, fmt, vl );
  va_end(vl);
  
  return resultCode;
}

cw::rc_t cw::log::exec( handle_t h )
{
  rc_t rc = kOkRC;
  log_t* p = _handleToPtr(h);

  return _exec(p);
}


void     cw::log::setLevel( handle_t h, logLevelId_t level )
{
  log_t* p = _handleToPtr(h);
  p->level = level;
}

cw::log::logLevelId_t cw::log::level( handle_t h )
{
  log_t* p = _handleToPtr(h);
  return p->level;
}

void cw::log::set_flags( handle_t h, unsigned flags )
{
  log_t* p = _handleToPtr(h);
  p->flags = flags;
}

unsigned cw::log::flags( handle_t h )
{
  log_t* p = _handleToPtr(h);
  return p->flags;
}
  
void* cw::log::outputCbArg( handle_t h )
{
  log_t* p = _handleToPtr(h);
  return p->outCbArg;
}

cw::log::logOutputCbFunc_t cw::log::outputCb( handle_t h )
{
  log_t* p = _handleToPtr(h);
  return p->outCbFunc;
}

void* cw::log::formatCbArg( handle_t h )
{
  log_t* p = _handleToPtr(h);
  return p->fmtCbArg;
}

cw::log::logFormatCbFunc_t cw::log::formatCb( handle_t h )
{
  log_t* p = _handleToPtr(h);
  return p->fmtCbFunc;  
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
{  return idToLabel(logLevelLabelArray,level,kInvalid_LogLevel); }



void  cw::log::defaultOutput( void* arg, unsigned level, const char* text )
{
  _console_output(level,text);
}

void cw::log::defaultFormatter( void* cbArg, logOutputCbFunc_t outFunc, void* outCbArg, unsigned flags, unsigned level, const char* function, const char* filename, unsigned lineno, int sys_errno, rc_t rc, const char* msg )
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

  int tdn = 256;
  char td[tdn];
  td[0] = 0;
  if( cwIsFlag(flags,kDateTimeFl) && level != kPrint_LogLevel )
    time::formatDateTime( td, (unsigned)tdn );
  tdn = strlen(td);


  // don't print the function,file,line when this is an 'info' msg.
  if( level == kInfo_LogLevel || level == kPrint_LogLevel )
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
    const char* fmt = "%s: %s: %s %s %s %s\n";

    int  n = snprintf(nullptr,0,fmt,levelStr,td,msg,syStr,rcStr,loStr);
    cwAssert(n != -1);
    char s[n+1];
    int m = snprintf(s,n+1,fmt,levelStr,td,msg,syStr,rcStr,loStr);
    cwAssert(m==n);
    outFunc(outCbArg,level,s);     
  }
  
}


cw::rc_t cw::log::createGlobal(  const log_args_t& args  )
{
  rc_t rc;
  
  if((rc = create(__logGlobalHandle__, args  )) != kOkRC )
    rc = cwLogError(rc,"Global log create failed.");
  
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

