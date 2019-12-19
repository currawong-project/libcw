#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFileSys.h"
#include "cwText.h"
#include "cwFile.h"

#ifdef cwLINUX
#include <sys/stat.h>
#endif


namespace cw
{

  typedef struct file_str
  {
    FILE* fp;
    char* fnStr;
  } file_t;


#define _fileHandleToPtr(h) handleToPtr<fileH_t,file_t>(h)
  
char*  _fileToBuf( fileH_t h, unsigned nn, unsigned* bufByteCntPtr )
{
  errno = 0;

  unsigned n   = fileByteCount(h);
  char*    buf = nullptr;

  // if the file size calculation is ok
  if( errno != 0 )
  {
    cwLogSysError(kOpFailRC,errno,"Invalid file buffer length on '%s'.", cwStringNullGuard(fileName(h)));
    goto errLabel;
  }
  
  // allocate the read target buffer
  if((buf = memAlloc<char>(n+nn)) == nullptr)
  {
    cwLogError(kMemAllocFailRC,"Read buffer allocation failed.");
    goto errLabel;
  }

  // read the file
  if( fileRead(h,buf,n) != kOkRC )
    goto errLabel;

  // zero memory after the file data
  memset(buf+n,0,nn);

  if( bufByteCntPtr != nullptr )
    *bufByteCntPtr = n;

  return buf;

 errLabel:
  if( bufByteCntPtr != nullptr )
    *bufByteCntPtr = 0;

  memRelease(buf);

  return nullptr;
    
}

char* _fileFnToBuf( const char* fn, unsigned nn, unsigned* bufByteCntPtr  )
{
  fileH_t h;
  char*   buf = nullptr;

  if( fileOpen(h,fn,kReadFileFl | kBinaryFileFl) != kOkRC )
    goto errLabel;

  buf = _fileToBuf(h,nn,bufByteCntPtr);

 errLabel:
  fileClose(h);
  
  return buf;
}

  rc_t _fileGetLine( file_t* p, char* buf, unsigned* bufByteCntPtr )
{
  // fgets() reads up to n-1 bytes into buf[]
  if( fgets(buf,*bufByteCntPtr,p->fp) == nullptr )
  {
    // an read error or EOF condition occurred
    *bufByteCntPtr = 0;

    if( !feof(p->fp ) )
      return cwLogSysError(kReadFailRC,errno,"File read line failed");
    
    return kReadFailRC;
  }

  return kOkRC;
}

}


cw::rc_t cw::fileOpen( fileH_t& hRef, const char* fn, unsigned flags )
{
  char mode[] = "/0/0/0";
  file_t* p   = nullptr;
  rc_t    rc;

  if((rc = fileClose(hRef)) != kOkRC )
    return rc;

  if( cwIsFlag(flags,kReadFileFl) )
    mode[0]     = 'r';
  else
    if( cwIsFlag(flags,kWriteFileFl) )
      mode[0]   = 'w';
    else
      if( cwIsFlag(flags,kAppendFileFl) )
        mode[0] = 'a';
      else
        cwLogError(kInvalidArgRC,"File open flags must contain 'kReadFileFl','kWriteFileFl', or 'kAppendFileFl'.");
  
  if( cwIsFlag(flags,kUpdateFileFl) )
    mode[1] = '+';

  // handle requests to use stdin,stdout,stderr
  FILE* sfp = nullptr;
  if( cwIsFlag(flags,kStdoutFileFl) )
  {
    sfp = stdout;
    fn  = "stdout";
  }
  else
    if( cwIsFlag(flags,kStderrFileFl) )
    {
      sfp = stderr;
      fn  = "stderr";
    }
    else
      if( cwIsFlag(flags,kStdinFileFl) )
      {
        sfp = stdin;
        fn  = "stdin";
      }


  if( fn == nullptr )
    return cwLogError(kInvalidArgRC,"File object allocation failed due to empty file name.");

  unsigned byteCnt = sizeof(file_t) + strlen(fn) + 1;

  if((p = memAllocZ<file_t>(byteCnt)) == nullptr )
    return cwLogError(kOpFailRC,"File object allocation failed for file '%s'.",cwStringNullGuard(fn));

  p->fnStr = (char*)(p+1);
  strcpy(p->fnStr,fn);
  
  if( sfp != nullptr )
    p->fp = sfp;
  else
  {
    errno                        = 0;
    if((p->fp = fopen(fn,mode)) == nullptr )
    {
      rc_t rc = cwLogSysError(kOpenFailRC,errno,"File open failed on file:'%s'.",cwStringNullGuard(fn));
      memRelease(p);
      return rc;
    }
  }
 
  hRef.set(p);

  return kOkRC;
}

cw::rc_t cw::fileClose(   fileH_t& hRef )
{
  if( fileIsValid(hRef) == false )
    return kOkRC;

  file_t* p = _fileHandleToPtr(hRef);
  
  errno                = 0;
  if( p->fp != nullptr )
    if( fclose(p->fp) != 0 )
      return cwLogSysError(kCloseFailRC,errno,"File close failed on '%s'.", cwStringNullGuard(p->fnStr));
  
  memRelease(p);
  hRef.set(nullptr);

  return kOkRC;
}

bool       cw::fileIsValid( fileH_t h )
{ return h.isValid(); }

cw::rc_t cw::fileRead(    fileH_t h, void* buf, unsigned bufByteCnt )
{
  file_t* p = _fileHandleToPtr(h);
  
  errno = 0;
  if( fread(buf,bufByteCnt,1,p->fp) != 1 )
    return cwLogSysError(kReadFailRC,errno,"File read failed on '%s'.", cwStringNullGuard(p->fnStr));

  return kOkRC;
}

cw::rc_t cw::fileWrite(   fileH_t h, const void* buf, unsigned bufByteCnt )
{
  file_t* p = _fileHandleToPtr(h);
  
  errno = 0;
  if( fwrite(buf,bufByteCnt,1,p->fp) != 1 )
    return cwLogSysError(kWriteFailRC,errno,"File write failed on '%s'.", cwStringNullGuard(p->fnStr));

  return kOkRC;
}

cw::rc_t cw::fileSeek(    fileH_t h, enum fileSeekFlags_t flags, int offsByteCnt )
{
  file_t*  p         = _fileHandleToPtr(h);
  unsigned fileflags = 0;

  if( cwIsFlag(flags,kBeginFileFl) )
    fileflags = SEEK_SET;
  else
    if( cwIsFlag(flags,kCurFileFl) )
      fileflags = SEEK_CUR;
    else
      if( cwIsFlag(flags,kEndFileFl) )
        fileflags = SEEK_END;
      else
        return cwLogError(kInvalidArgRC,"Invalid file seek flag on '%s'.",cwStringNullGuard(p->fnStr));
  
  errno = 0;
  if( fseek(p->fp,offsByteCnt,fileflags) != 0 )
    return cwLogSysError(kSeekFailRC,errno,"File seek failed on '%s'",cwStringNullGuard(p->fnStr));

  return kOkRC;
}

cw::rc_t cw::fileTell( fileH_t h, long* offsPtr )
{
  cwAssert( offsPtr != nullptr );
  *offsPtr           = -1;
  file_t* p = _fileHandleToPtr(h);
  errno              = 0;

  if((*offsPtr = ftell(p->fp)) == -1)
    return cwLogSysError(kOpFailRC,errno,"File tell failed on '%s'.", cwStringNullGuard(p->fnStr));
  return kOkRC;
}


bool       cw::fileEof(     fileH_t h )
{ return feof( _fileHandleToPtr(h)->fp ) != 0; }


unsigned   cw::fileByteCount(  fileH_t h )
{
  struct stat sr;
  int         f;
  file_t*     p = _fileHandleToPtr(h);
  const char errMsg[] = "File byte count request failed.";

  errno = 0;

  if((f = fileno(p->fp)) == -1)
  {
    cwLogSysError(kInvalidOpRC,errno,"%s because fileno() failed on '%s'.",errMsg,cwStringNullGuard(p->fnStr));
    return 0;
  }
  
  if(fstat(f,&sr) == -1)
  {
    cwLogSysError(kInvalidOpRC,errno,"%s because fstat() failed on '%s'.",errMsg,cwStringNullGuard(p->fnStr));
    return 0;
  }

  return sr.st_size;
}

cw::rc_t   cw::fileByteCountFn( const char* fn, unsigned* fileByteCntPtr )
{
  cwAssert( fileByteCntPtr != nullptr );
  rc_t    rc;
  fileH_t h;

  if((rc = fileOpen(h,fn,kReadFileFl)) != kOkRC )
    return rc;

  if( fileByteCntPtr != nullptr)
    *fileByteCntPtr   = fileByteCount(h);

  fileClose(h);

  return rc;    
}

cw::rc_t cw::fileCompare( const char* fn0, const char* fn1, bool& isEqualRef )
{
  rc_t     rc         = kOkRC;
  unsigned bufByteCnt = 2048;
  fileH_t  h0;
  fileH_t  h1;
  file_t*  p0         = nullptr;
  file_t*  p1         = nullptr;
  char     b0[ bufByteCnt ];
  char     b1[ bufByteCnt ];

  isEqualRef = true;

  if((rc = fileOpen(h0,fn0,kReadFileFl)) != kOkRC )
    goto errLabel;

  if((rc = fileOpen(h1,fn1,kReadFileFl)) != kOkRC )
    goto errLabel;

  p0 = _fileHandleToPtr(h0);
  p1 = _fileHandleToPtr(h1);

  while(1)
  {
    size_t n0 = fread(b0,1,bufByteCnt,p0->fp);
    size_t n1 = fread(b1,1,bufByteCnt,p1->fp);
    if( n0 != n1 || memcmp(b0,b1,n0) != 0 )
    {
      isEqualRef = false;
      break;
    }

    if( n0 != bufByteCnt || n1 != bufByteCnt )
      break;
  }

 errLabel:
  fileClose(h0);
  fileClose(h1);
  return rc;
}


const char* cw::fileName( fileH_t h )
{
  file_t* p = _fileHandleToPtr(h);
  return p->fnStr;
}

cw::rc_t cw::fileFnWrite( const char* fn, const void* buf, unsigned bufByteCnt )
{
  fileH_t h;
  rc_t rc;

  if((rc = fileOpen(h,fn,kWriteFileFl)) != kOkRC )
    goto errLabel;

  rc = fileWrite(h,buf,bufByteCnt);

 errLabel:
  fileClose(h);
  
  return rc;
}


cw::rc_t    cw::fileCopy( 
    const char* srcDir, 
    const char* srcFn, 
    const char* srcExt, 
    const char* dstDir, 
    const char* dstFn, 
    const char* dstExt)
{
  rc_t     rc        = kOkRC;
  unsigned byteCnt   = 0;
  char*    buf       = nullptr;
  char*    srcPathFn = nullptr;
  char*    dstPathFn = nullptr;

  // form the source path fn
  if((srcPathFn = fileSysMakeFn(srcDir,srcFn,srcExt,nullptr)) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"The soure file name for dir:%s name:%s ext:%s could not be formed.",cwStringNullGuard(srcDir),cwStringNullGuard(srcFn),cwStringNullGuard(srcExt));
    goto errLabel;
  }

  // form the dest path fn
  if((dstPathFn = fileSysMakeFn(dstDir,dstFn,dstExt,nullptr)) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"The destination file name for dir:%s name:%s ext:%s could not be formed.",cwStringNullGuard(dstDir),cwStringNullGuard(dstFn),cwStringNullGuard(dstExt));
    goto errLabel;
  }

  // verify that the source exists
  if( fileSysIsFile(srcPathFn) == false )
  {
    rc = cwLogError(kOpenFailRC,"The source file '%s' does not exist.",cwStringNullGuard(srcPathFn));
    goto errLabel;
  }

  // read the source file into a buffer
  if((buf = fileFnToBuf(srcPathFn,&byteCnt)) == nullptr )
    rc = cwLogError(kReadFailRC,"Attempt to fill a buffer from '%s' failed.",cwStringNullGuard(srcPathFn));
  else
  {
    // write the file to the output file
    if( fileFnWrite(dstPathFn,buf,byteCnt) != kOkRC )
      rc = cwLogError(kWriteFailRC,"An attempt to write a buffer to '%s' failed.",cwStringNullGuard(dstPathFn));    
  }

 errLabel:
  // free the buffer
  memRelease(buf);
  memRelease(srcPathFn);
  memRelease(dstPathFn);
  return rc;

}

cw::rc_t cw::fileBackup( const char* dir, const char* name, const char* ext )
{
  rc_t               rc      = kOkRC;
  char*              newName = nullptr;
  char*              newFn   = nullptr;
  unsigned           n       = 0;
  char*              srcFn   = nullptr;
  fileSysPathPart_t* pp      = nullptr;

  // form the name of the backup file
  if((srcFn = fileSysMakeFn(dir,name,ext,nullptr)) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"Backup source file name formation failed.");
    goto errLabel;
  }

  // if the src file does not exist then there is nothing to do
  if( fileSysIsFile(srcFn) == false )
    return rc;

  // break the source file name up into dir/fn/ext.
  if((pp = fileSysPathParts(srcFn)) == nullptr || pp->fnStr==nullptr)
  {
    rc = cwLogError(kOpFailRC,"The file name '%s' could not be parsed into its parts.",cwStringNullGuard(srcFn));
    goto errLabel;
  }

  // iterate until a unique file name is found
  for(n=0; 1; ++n)
  {
    memRelease(newFn);

    // generate a new file name
    newName = memPrintf(newName,"%s_%i",pp->fnStr,n);
    
    // form the new file name into a complete path
    if((newFn = fileSysMakeFn(pp->dirStr,newName,pp->extStr,nullptr)) == nullptr )
    {
      rc = cwLogError(kOpFailRC,"A backup file name could not be formed for the file '%s'.",cwStringNullGuard(newName));
      goto errLabel;
    }

    // if the new file name is not already in use ...
    if( fileSysIsFile(newFn) == false )
    {
      // .. then duplicate the file
      if((rc = fileCopy(srcFn,nullptr,nullptr,newFn,nullptr,nullptr)) != kOkRC )
        rc = cwLogError(rc,"The file '%s' could not be duplicated as '%s'.",cwStringNullGuard(srcFn),cwStringNullGuard(newFn));

      break;
    }


  }

 errLabel:

  memRelease(srcFn);
  memRelease(newFn);
  memRelease(newName);
  memRelease(pp);

  return rc;

}


char*  cw::fileToBuf( fileH_t h, unsigned* bufByteCntPtr )
{ return _fileToBuf(h,0,bufByteCntPtr); }

char*  cw::fileFnToBuf( const char* fn, unsigned* bufByteCntPtr )
{ return _fileFnToBuf(fn,0,bufByteCntPtr); }

char*  cw::fileToStr( fileH_t h, unsigned* bufByteCntPtr )
{ return _fileToBuf(h,1,bufByteCntPtr); }

char*  cw::fileFnToStr( const char* fn, unsigned* bufByteCntPtr )
{ return _fileFnToBuf(fn,1,bufByteCntPtr); }

cw::rc_t cw::fileLineCount( fileH_t h, unsigned* lineCntPtr )
{
  rc_t     rc      = kOkRC;
  file_t*  p       = _fileHandleToPtr(h);
  unsigned lineCnt = 0;
  long     offs;
  int      c;


  cwAssert( lineCntPtr != nullptr );
  *lineCntPtr = 0;

  if((rc = fileTell(h,&offs)) != kOkRC )
    return rc;

  errno = 0;

  while(1)
  {
    c = fgetc(p->fp);

    if( c == EOF ) 
    {
      if( errno )
        rc = cwLogSysError(kReadFailRC,errno,"File read char failed on 's'.", cwStringNullGuard(fileName(h)));
      else
        ++lineCnt; // add one in case the last line isn't terminated with a '\n'. 

      break;
    }

    // if an end-of-line was encountered
    if( c == '\n' )
      ++lineCnt;

  }

  if((rc = fileSeek(h,kBeginFileFl,offs)) != kOkRC )
    return rc;

  *lineCntPtr = lineCnt;

  return rc;
}


cw::rc_t cw::fileGetLine( fileH_t h, char* buf, unsigned* bufByteCntPtr )
{
  cwAssert( bufByteCntPtr != nullptr );
  file_t* p  = _fileHandleToPtr(h);
  unsigned  tn = 128;
  char  t[ tn ];
  unsigned  on = *bufByteCntPtr;
  long      offs;
  rc_t rc;

  // store the current file offset
  if((rc = fileTell(h,&offs)) != kOkRC )
    return rc;
  
  // if no buffer was given then use t[]
  if( buf == nullptr || *bufByteCntPtr == 0 )
  {
    *bufByteCntPtr = tn;
    buf            = t;
  }

  // fill the buffer from the current line 
  if((rc = _fileGetLine(p,buf,bufByteCntPtr)) != kOkRC )
    return rc;

  // get length of the string  in the buffer
  // (this is one less than the count of bytes written to the buffer)
  unsigned n = strlen(buf);

  // if the provided buffer was large enough to read the entire string 
  if( on > n+1 )
  {
    //*bufByteCntPtr = n+1;
    return kOkRC;
  }

  //
  // the provided buffer was not large enough 
  //

  // m tracks the length of the string
  unsigned m = n;

  while( n+1 == *bufByteCntPtr )
  {
    // fill the buffer from the current line
    if((rc = _fileGetLine(p,buf,bufByteCntPtr)) != kOkRC )
      return rc;

    n = strlen(buf);
    m += n;
  }

  // restore the original file offset
  if((rc = fileSeek(h,kBeginFileFl,offs)) != kOkRC )
    return rc;

  // add 1 for /0, 1 for /n and 1 to detect buf-too-short
  *bufByteCntPtr = m+3;
  
  return kBufTooSmallRC;
  
}

cw::rc_t cw::fileGetLineAuto( fileH_t h, char** bufPtrPtr, unsigned* bufByteCntPtr )
{
  rc_t  rc  = kOkRC;
  bool  fl  = true;
  char* buf = *bufPtrPtr;

  *bufPtrPtr = nullptr;

  while(fl)
  {
    fl         = false;

    switch( rc = fileGetLine(h,buf,bufByteCntPtr) )
    {
      case kOkRC:
        {
          *bufPtrPtr = buf;
        }
        break;
        
      case kBufTooSmallRC:
        buf = memResizeZ<char>(buf,*bufByteCntPtr);
        fl  = true;
        break;

      default:
        memRelease(buf);
        break;
    }
  }

  

  return rc;
}

cw::rc_t cw::fileReadChar(   fileH_t h, char*           buf, unsigned cnt ) 
{ return fileRead(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileReadUChar(  fileH_t h, unsigned char*  buf, unsigned cnt ) 
{ return fileRead(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileReadShort(  fileH_t h, short*          buf, unsigned cnt ) 
{ return fileRead(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileReadUShort( fileH_t h, unsigned short* buf, unsigned cnt ) 
{ return fileRead(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileReadLong(   fileH_t h, long*           buf, unsigned cnt ) 
{ return fileRead(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileReadULong(  fileH_t h, unsigned long*  buf, unsigned cnt ) 
{ return fileRead(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileReadInt(    fileH_t h, int*            buf, unsigned cnt ) 
{ return fileRead(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileReadUInt(   fileH_t h, unsigned int*   buf, unsigned cnt ) 
{ return fileRead(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileReadFloat(  fileH_t h, float*          buf, unsigned cnt ) 
{ return fileRead(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileReadDouble( fileH_t h, double*         buf, unsigned cnt ) 
{ return fileRead(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileReadBool(   fileH_t h, bool*           buf, unsigned cnt ) 
{ return fileRead(h,buf,sizeof(buf[0])*cnt); }



cw::rc_t cw::fileWriteChar(   fileH_t h, const char*           buf, unsigned cnt )
{ return fileWrite(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileWriteUChar(  fileH_t h, const unsigned char*  buf, unsigned cnt )
{ return fileWrite(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileWriteShort(  fileH_t h, const short*          buf, unsigned cnt )
{ return fileWrite(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileWriteUShort( fileH_t h, const unsigned short* buf, unsigned cnt )
{ return fileWrite(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileWriteLong(   fileH_t h, const long*           buf, unsigned cnt )
{ return fileWrite(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileWriteULong(  fileH_t h, const unsigned long*  buf, unsigned cnt )
{ return fileWrite(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileWriteInt(    fileH_t h, const int*            buf, unsigned cnt )
{ return fileWrite(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileWriteUInt(   fileH_t h, const unsigned int*   buf, unsigned cnt )
{ return fileWrite(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileWriteFloat(  fileH_t h, const float*          buf, unsigned cnt )
{ return fileWrite(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileWriteDouble( fileH_t h, const double*         buf, unsigned cnt )
{ return fileWrite(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::fileWriteBool(   fileH_t h, const bool*           buf, unsigned cnt )
{ return fileWrite(h,buf,sizeof(buf[0])*cnt); }


cw::rc_t cw::fileWriteStr( fileH_t h, const char* s )
{
  rc_t rc;
  
  unsigned n = textLength(s);

  if((rc = fileWriteUInt(h,&n,1)) != kOkRC )
    return rc;

  if( n > 0 )
    rc = fileWriteChar(h,s,n);
  return rc;
}


cw::rc_t cw::fileReadStr(  fileH_t h, char** sRef, unsigned maxCharN )
{
  unsigned n;
  rc_t rc;

  cwAssert(sRef != nullptr );

  *sRef = nullptr;
  
  if( maxCharN == 0 )
    maxCharN = 16384;

  // read the string length
  if((rc = fileReadUInt(h,&n,1)) != kOkRC )
    return rc;

  // verify that string isn't too long
  if( n > maxCharN  )
  {
    return cwLogError(kInvalidArgRC,"The stored string is larger than the maximum allowable size of %i.",maxCharN);    
  }

  // allocate a read buffer
  char* s = memAllocZ<char>(n+1);

  // fill the buffer from the file
  if((rc = fileReadChar(h,s,n)) != kOkRC )
    return rc;

  s[n] = 0; // terminate the string

  *sRef = s;

  return rc;
}


cw::rc_t cw::filePrint(   fileH_t h, const char* text )
{
  file_t* p = _fileHandleToPtr(h);

  errno = 0;
  if( fputs(text,p->fp) < 0 )
    return cwLogSysError(kOpFailRC,errno,"File print failed on '%s'.", cwStringNullGuard(fileName(h)));

  return kOkRC;
}


cw::rc_t cw::fileVPrintf( fileH_t h, const char* fmt, va_list vl )
{
  file_t* p = _fileHandleToPtr(h);
  
  if( vfprintf(p->fp,fmt,vl) < 0 )
    return cwLogSysError(kOpFailRC,errno,"File print failed on '%s'.", cwStringNullGuard(fileName(h)));
  
  return kOkRC;
}

cw::rc_t cw::filePrintf(  fileH_t h, const char* fmt, ... )
{
  va_list vl;
  va_start(vl,fmt);
  rc_t rc = fileVPrintf(h,fmt,vl);
  va_end(vl);
  return rc;
}


