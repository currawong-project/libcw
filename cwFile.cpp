//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwObject.h"
#include "cwMem.h"
#include "cwFileSys.h"
#include "cwText.h"
#include "cwFile.h"

#ifdef OS_LINUX
#include <sys/stat.h>
#endif


namespace cw
{

  namespace file
  {
    typedef struct file_str
    {
      FILE* fp;
      char* fnStr;
      rc_t  lastRC;
    } this_t;


    this_t* _handleToPtr(handle_t h)
    {
      return handleToPtr<handle_t,this_t>(h);
    }
  
    char*  _fileToBuf( handle_t h, unsigned nn, unsigned* bufByteCntPtr )
    {
      errno = 0;

      unsigned n   = byteCount(h);
      char*    buf = nullptr;

      // if the file size calculation is ok
      if( errno != 0 )
      {
        cwLogSysError(kOpFailRC,errno,"Invalid file buffer length on '%s'.", cwStringNullGuard(name(h)));
        goto errLabel;
      }
  
      // allocate the read target buffer
      if((buf = mem::alloc<char>(n+nn)) == nullptr)
      {
        cwLogError(kMemAllocFailRC,"Read buffer allocation failed.");
        goto errLabel;
      }

      // read the file
      if( read(h,buf,n) != kOkRC )
        goto errLabel;

      // zero memory after the file data
      memset(buf+n,0,nn);

      if( bufByteCntPtr != nullptr )
        *bufByteCntPtr = n;

      return buf;

    errLabel:
      if( bufByteCntPtr != nullptr )
        *bufByteCntPtr = 0;

      mem::release(buf);

      return nullptr;
    
    }

    char* _fileFnToBuf( const char* fn, unsigned nn, unsigned* bufByteCntPtr  )
    {
      handle_t h;
      char*   buf = nullptr;

      if( open(h,fn,kReadFl | kBinaryFl) != kOkRC )
        goto errLabel;

      buf = _fileToBuf(h,nn,bufByteCntPtr);

    errLabel:
      close(h);
  
      return buf;
    }

    rc_t _fileGetLine( this_t* p, char* buf, unsigned* bufByteCntPtr )
    {
      // fgets() reads up to n-1 bytes into buf[]
      if( fgets(buf,*bufByteCntPtr,p->fp) == nullptr )
      {
        // an read error or EOF condition occurred
        *bufByteCntPtr = 0;

        if( !feof(p->fp ) )
          return p->lastRC = cwLogSysError(kReadFailRC,errno,"File read line failed");

        p->lastRC = kEofRC;
        return kEofRC;
      }

      return kOkRC;
    }

  }

}

cw::rc_t cw::file::open( handle_t& hRef, const char* fn, unsigned flags )
{
  this_t* p      = nullptr;
  char*   exp_fn = nullptr;
  rc_t    rc     = kOkRC;
  char mode[]    = "/0/0/0";
  
  if((rc = close(hRef)) != kOkRC )
    return rc;

  if( cwIsFlag(flags,kReadFl) )
    mode[0]     = 'r';
  else
    if( cwIsFlag(flags,kWriteFl) )
      mode[0]   = 'w';
    else
      if( cwIsFlag(flags,kAppendFl) )
        mode[0] = 'a';
      else
        cwLogError(kInvalidArgRC,"File open flags must contain 'kReadFl','kWriteFl', or 'kAppendFl'.");
  
  if( cwIsFlag(flags,kUpdateFl) )
    mode[1] = '+';

  // handle requests to use stdin,stdout,stderr
  FILE* sfp = nullptr;
  if( cwIsFlag(flags,kStdoutFl) )
  {
    sfp = stdout;
    fn  = "stdout";
  }
  else
    if( cwIsFlag(flags,kStderrFl) )
    {
      sfp = stderr;
      fn  = "stderr";
    }
    else
      if( cwIsFlag(flags,kStdinFl) )
      {
        sfp = stdin;
        fn  = "stdin";
      }

  // verify the filename is not empty
  if( fn == nullptr || strlen(fn)==0 )
    return cwLogError(kInvalidArgRC,"File object allocation failed due to empty file name.");

  
  
  unsigned byteCnt = sizeof(this_t) + strlen(fn) + 1;

  // create the file object
  if((p = mem::allocZ<this_t>(byteCnt)) == nullptr )
    return cwLogError(kOpFailRC,"File object allocation failed for file '%s'.",cwStringNullGuard(fn));

  // copy  in the file name
  p->fnStr = (char*)(p+1);
  strcpy(p->fnStr,fn);

  // if a special file was requestd
  if( sfp != nullptr )
    p->fp = sfp;
  else 
  {
    exp_fn = filesys::expandPath(fn);
    errno = 0;
    
    if((p->fp = fopen(exp_fn,mode)) == nullptr )
      rc = p->lastRC = cwLogSysError(kOpenFailRC,errno,"File open failed on file:'%s'.",cwStringNullGuard(fn));

    mem::release(exp_fn);
    
  }
 

  if( rc != kOkRC )
    mem::release(p);
  else
    hRef.set(p);
  
  return rc;
}

cw::rc_t cw::file::close( handle_t& hRef )
{
  if( isValid(hRef) == false )
    return kOkRC;

  this_t* p = _handleToPtr(hRef);
  
  errno                = 0;
  if( p->fp != nullptr )
    if( fclose(p->fp) != 0 )
      return p->lastRC = cwLogSysError(kCloseFailRC,errno,"File close failed on '%s'.", cwStringNullGuard(p->fnStr));
  
  mem::release(p);
  hRef.clear();

  return kOkRC;
}

bool       cw::file::isValid( handle_t h )
{ return h.isValid(); }

cw::rc_t  cw::file::lastRC( handle_t h )
{
  this_t*  p = _handleToPtr(h);
  return p->lastRC;  
}

cw::rc_t cw::file::read(    handle_t h, void* buf, unsigned bufByteCnt, unsigned* actualByteCntRef )
{
  rc_t     rc            = kOkRC;
  this_t*  p             = _handleToPtr(h);
  unsigned actualByteCnt = 0;

  if( p->lastRC != kOkRC )
    return p->lastRC;
  
  errno = 0;
  if(( actualByteCnt = fread(buf,1,bufByteCnt,p->fp)) != bufByteCnt )
  {
    if( feof( p->fp ) != 0 )
      rc = p->lastRC = kEofRC;
    else
      rc= p->lastRC = cwLogSysError(kReadFailRC,errno,"File read failed on '%s'.", cwStringNullGuard(p->fnStr));
  }

  if( actualByteCntRef != nullptr )
    *actualByteCntRef = actualByteCnt;
  
  return rc;
}

cw::rc_t cw::file::write(   handle_t h, const void* buf, unsigned bufByteCnt )
{
  this_t* p = _handleToPtr(h);

  if( p->lastRC != kOkRC )
    return p->lastRC;

  if( bufByteCnt )
  {
    errno = 0;
    if( fwrite(buf,bufByteCnt,1,p->fp) != 1 )
      return p->lastRC = cwLogSysError(kWriteFailRC,errno,"File write failed on '%s'.", cwStringNullGuard(p->fnStr));
  }
  
  return kOkRC;
}

cw::rc_t cw::file::seek(    handle_t h, enum seekFlags_t flags, int offsByteCnt )
{
  this_t*  p         = _handleToPtr(h);
  unsigned fileflags = 0;

  if( cwIsFlag(flags,kBeginFl) )
    fileflags = SEEK_SET;
  else
    if( cwIsFlag(flags,kCurFl) )
      fileflags = SEEK_CUR;
    else
      if( cwIsFlag(flags,kEndFl) )
        fileflags = SEEK_END;
      else
        return cwLogError(kInvalidArgRC,"Invalid file seek flag on '%s'.",cwStringNullGuard(p->fnStr));
  
  errno = 0;
  if( fseek(p->fp,offsByteCnt,fileflags) != 0 )
    return cwLogSysError(kSeekFailRC,errno,"File seek failed on '%s'",cwStringNullGuard(p->fnStr));

  // if the seek succeeded then override any previous error state
  p->lastRC = kOkRC;

  return kOkRC;
}

cw::rc_t cw::file::tell( handle_t h, long* offsPtr )
{
  cwAssert( offsPtr != nullptr );
  *offsPtr           = -1;
  this_t* p = _handleToPtr(h);
  errno              = 0;

  if((*offsPtr = ftell(p->fp)) == -1)
    return p->lastRC = cwLogSysError(kOpFailRC,errno,"File tell failed on '%s'.", cwStringNullGuard(p->fnStr));
  return kOkRC;
}


bool       cw::file::eof(     handle_t h )
{ return feof( _handleToPtr(h)->fp ) != 0; }


unsigned   cw::file::byteCount(  handle_t h )
{
  struct stat sr;
  int         f;
  this_t*     p       = _handleToPtr(h);
  const char errMsg[] = "File byte count request failed.";

  errno = 0;

  if((f = fileno(p->fp)) == -1)
  {
    p->lastRC = cwLogSysError(kInvalidOpRC,errno,"%s because fileno() failed on '%s'.",errMsg,cwStringNullGuard(p->fnStr));
    return 0;
  }
  
  if(fstat(f,&sr) == -1)
  {
    p->lastRC = cwLogSysError(kInvalidOpRC,errno,"%s because fstat() failed on '%s'.",errMsg,cwStringNullGuard(p->fnStr));
    return 0;
  }

  return sr.st_size;
}

cw::rc_t   cw::file::byteCountFn( const char* fn, unsigned* fileByteCntPtr )
{
  cwAssert( fileByteCntPtr != nullptr );
  rc_t    rc;
  handle_t h;

  if((rc = open(h,fn,kReadFl)) != kOkRC )
    return rc;

  if( fileByteCntPtr != nullptr)
    *fileByteCntPtr   = byteCount(h);

  close(h);

  return rc;    
}

cw::rc_t cw::file::compare( const char* fn0, const char* fn1, bool& isEqualRef )
{
  rc_t     rc         = kOkRC;
  unsigned bufByteCnt = 2048;
  handle_t  h0;
  handle_t  h1;
  this_t*  p0         = nullptr;
  this_t*  p1         = nullptr;
  char     b0[ bufByteCnt ];
  char     b1[ bufByteCnt ];

  isEqualRef = true;

  if((rc = open(h0,fn0,kReadFl)) != kOkRC )
    goto errLabel;

  if((rc = open(h1,fn1,kReadFl)) != kOkRC )
    goto errLabel;

  p0 = _handleToPtr(h0);
  p1 = _handleToPtr(h1);

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
  close(h0);
  close(h1);
  return rc;
}


const char* cw::file::name( handle_t h )
{
  this_t* p = _handleToPtr(h);
  return p->fnStr;
}

cw::rc_t cw::file::fnWrite( const char* fn, const void* buf, unsigned bufByteCnt )
{
  handle_t h;
  rc_t rc;

  if((rc = open(h,fn,kWriteFl)) != kOkRC )
    goto errLabel;

  rc = write(h,buf,bufByteCnt);

 errLabel:
  close(h);
  
  return rc;
}


cw::rc_t    cw::file::copy( 
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
  if((srcPathFn = filesys::makeFn(srcDir,srcFn,srcExt,nullptr)) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"The soure file name for dir:%s name:%s ext:%s could not be formed.",cwStringNullGuard(srcDir),cwStringNullGuard(srcFn),cwStringNullGuard(srcExt));
    goto errLabel;
  }

  // form the dest path fn
  if((dstPathFn = filesys::makeFn(dstDir,dstFn,dstExt,nullptr)) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"The destination file name for dir:%s name:%s ext:%s could not be formed.",cwStringNullGuard(dstDir),cwStringNullGuard(dstFn),cwStringNullGuard(dstExt));
    goto errLabel;
  }

  // verify that the source exists
  if( filesys::isFile(srcPathFn) == false )
  {
    rc = cwLogError(kOpenFailRC,"The source file '%s' does not exist.",cwStringNullGuard(srcPathFn));
    goto errLabel;
  }

  // read the source file into a buffer
  if((buf = fnToBuf(srcPathFn,&byteCnt)) == nullptr )
    rc = cwLogError(kReadFailRC,"Attempt to fill a buffer from '%s' failed.",cwStringNullGuard(srcPathFn));
  else
  {
    // write the file to the output file
    if( fnWrite(dstPathFn,buf,byteCnt) != kOkRC )
      rc = cwLogError(kWriteFailRC,"An attempt to write a buffer to '%s' failed.",cwStringNullGuard(dstPathFn));    
  }

 errLabel:
  // free the buffer
  mem::release(buf);
  mem::release(srcPathFn);
  mem::release(dstPathFn);
  return rc;

}

cw::rc_t cw::file::backup( const char* dir, const char* name, const char* ext, const char* dst0_dir )
{
  rc_t                 rc      = kOkRC;
  char*                newName = nullptr;
  char*                newFn   = nullptr;
  unsigned             n       = 0;
  char*                srcFn   = nullptr;
  filesys::pathPart_t* pp      = nullptr;
  const char*          dst_dir = nullptr;
  char*                dst_base_dir = nullptr;

  // expand the destination path
  if( dst0_dir != nullptr )
  {
    if((dst_base_dir = filesys::expandPath(dst0_dir)) == nullptr )
    {
      rc = cwLogError(kOpFailRC,"The backup dest directory '%s' could not be expanded.");
      goto errLabel;
    }
    dst_dir = dst_base_dir;
  }
  
  // form the name of the backup file to backup
  if((srcFn = filesys::makeFn(dir,name,ext,nullptr)) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"Backup source file name formation failed.");
    goto errLabel;
  }

  // if the src file does not exist then there is nothing to do
  if( filesys::isFile(srcFn) == false )
    return rc;

  // break the source file name up into dir/fn/ext.
  if((pp = filesys::pathParts(srcFn)) == nullptr || pp->fnStr==nullptr)
  {
    rc = cwLogError(kOpFailRC,"The file name '%s' could not be parsed into its parts.",cwStringNullGuard(srcFn));
    goto errLabel;
  }

  if( dst_dir == nullptr  )
    dst_dir = pp->dirStr;

  // iterate until a unique file name is found
  for(n=0; 1; ++n)
  {
    mem::release(newFn);

    // generate a new file name
    newName = mem::printf(newName,"%s_%i",pp->fnStr,n);
    
    // form the new file name into a complete path
    if((newFn = filesys::makeFn(dst_dir,newName,pp->extStr,nullptr)) == nullptr )
    {
      rc = cwLogError(kOpFailRC,"A backup file name could not be formed for the file '%s'.",cwStringNullGuard(newName));
      goto errLabel;
    }

    // if the new file name is not already in use ...
    if( filesys::isFile(newFn) == false )
    {
      // .. then duplicate the file
      if((rc = copy(srcFn,nullptr,nullptr,newFn,nullptr,nullptr)) != kOkRC )
        rc = cwLogError(rc,"The file '%s' could not be duplicated as '%s'.",cwStringNullGuard(srcFn),cwStringNullGuard(newFn));

      break;
    }


  }

 errLabel:

  mem::release(dst_base_dir);
  mem::release(srcFn);
  mem::release(newFn);
  mem::release(newName);
  mem::release(pp);

  return rc;

}

cw::rc_t cw::file::backup( const char* fname, const char* dst_dir )
{
  rc_t rc = kOkRC;
  filesys::pathPart_t* pp = nullptr;
  if((pp = filesys::pathParts(fname)) == nullptr )
  {
    rc = cwLogError(kInvalidArgRC,"The parts (dir,name,ext) of the file name '%s' could not be parsed.");
    goto errLabel;
  }

  rc = backup(pp->dirStr,pp->fnStr,pp->extStr,dst_dir);

 errLabel:
  mem::release(pp);
  return rc;
}



char*  cw::file::toBuf( handle_t h, unsigned* bufByteCntPtr )
{ return _fileToBuf(h,0,bufByteCntPtr); }

char*  cw::file::fnToBuf( const char* fn, unsigned* bufByteCntPtr )
{ return _fileFnToBuf(fn,0,bufByteCntPtr); }

char*  cw::file::toStr( handle_t h, unsigned* bufByteCntPtr )
{ return _fileToBuf(h,1,bufByteCntPtr); }

char*  cw::file::fnToStr( const char* fn, unsigned* bufByteCntPtr )
{ return _fileFnToBuf(fn,1,bufByteCntPtr); }

cw::rc_t cw::file::lineCount( handle_t h, unsigned* lineCntPtr )
{
  rc_t     rc      = kOkRC;
  this_t*  p       = _handleToPtr(h);
  unsigned lineCnt = 0;
  long     offs;
  int      c;


  cwAssert( lineCntPtr != nullptr );
  *lineCntPtr = 0;

  if((rc = tell(h,&offs)) != kOkRC )
    return rc;

  errno = 0;

  while(1)
  {
    c = fgetc(p->fp);

    if( c == EOF ) 
    {
      if( errno )
        rc = cwLogSysError(kReadFailRC,errno,"File read char failed on 's'.", cwStringNullGuard(name(h)));
      else
        ++lineCnt; // add one in case the last line isn't terminated with a '\n'. 

      break;
    }

    // if an end-of-line was encountered
    if( c == '\n' )
      ++lineCnt;

  }

  if((rc = seek(h,kBeginFl,offs)) != kOkRC )
    return rc;

  *lineCntPtr = lineCnt;

  return rc;
}


cw::rc_t cw::file::getLine( handle_t h, char* buf, unsigned* bufByteCntPtr )
{
  cwAssert( bufByteCntPtr != nullptr );
  this_t* p  = _handleToPtr(h);
  unsigned  tn = 128;
  char  t[ tn ];
  unsigned  on = *bufByteCntPtr;
  long      offs;
  rc_t rc;

  // store the current file offset
  if((rc = tell(h,&offs)) != kOkRC )
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
  if((rc = seek(h,kBeginFl,offs)) != kOkRC )
    return rc;

  // add 1 for /0, 1 for /n and 1 to detect buf-too-short
  *bufByteCntPtr = m+3;
  
  return kBufTooSmallRC;
  
}

cw::rc_t cw::file::getLineAuto( handle_t h, char** bufPtrPtr, unsigned* bufByteCntPtr )
{
  rc_t  rc  = kOkRC;
  bool  fl  = true;
  char* buf = *bufPtrPtr;

  *bufPtrPtr = nullptr;

  while(fl)
  {
    fl         = false;

    switch( rc = getLine(h,buf,bufByteCntPtr) )
    {
      case kOkRC:
        {
          *bufPtrPtr = buf;
        }
        break;
        
      case kBufTooSmallRC:
        buf = mem::resizeZ<char>(buf,*bufByteCntPtr);
        fl  = true;
        break;

      default:
        mem::release(buf);
        break;
    }
  }

  

  return rc;
}

cw::rc_t cw::file::readChar(   handle_t h, char*           buf, unsigned cnt ) 
{ return read(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::readUChar(  handle_t h, unsigned char*  buf, unsigned cnt ) 
{ return read(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::readShort(  handle_t h, short*          buf, unsigned cnt ) 
{ return read(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::readUShort( handle_t h, unsigned short* buf, unsigned cnt ) 
{ return read(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::readLong(   handle_t h, long*           buf, unsigned cnt ) 
{ return read(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::readULong(  handle_t h, unsigned long*  buf, unsigned cnt ) 
{ return read(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::readInt(    handle_t h, int*            buf, unsigned cnt ) 
{ return read(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::readUInt(   handle_t h, unsigned int*   buf, unsigned cnt ) 
{ return read(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::readFloat(  handle_t h, float*          buf, unsigned cnt ) 
{ return read(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::readDouble( handle_t h, double*         buf, unsigned cnt ) 
{ return read(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::readBool(   handle_t h, bool*           buf, unsigned cnt ) 
{ return read(h,buf,sizeof(buf[0])*cnt); }



cw::rc_t cw::file::writeChar(   handle_t h, const char*           buf, unsigned cnt )
{ return write(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::writeUChar(  handle_t h, const unsigned char*  buf, unsigned cnt )
{ return write(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::writeShort(  handle_t h, const short*          buf, unsigned cnt )
{ return write(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::writeUShort( handle_t h, const unsigned short* buf, unsigned cnt )
{ return write(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::writeLong(   handle_t h, const long*           buf, unsigned cnt )
{ return write(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::writeULong(  handle_t h, const unsigned long*  buf, unsigned cnt )
{ return write(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::writeInt(    handle_t h, const int*            buf, unsigned cnt )
{ return write(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::writeUInt(   handle_t h, const unsigned int*   buf, unsigned cnt )
{ return write(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::writeFloat(  handle_t h, const float*          buf, unsigned cnt )
{ return write(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::writeDouble( handle_t h, const double*         buf, unsigned cnt )
{ return write(h,buf,sizeof(buf[0])*cnt); }

cw::rc_t cw::file::writeBool(   handle_t h, const bool*           buf, unsigned cnt )
{ return write(h,buf,sizeof(buf[0])*cnt); }


cw::rc_t cw::file::writeStr( handle_t h, const char* s )
{
  rc_t rc;
  
  unsigned n = textLength(s);

  if((rc = writeUInt(h,&n,1)) != kOkRC )
    return rc;

  if( n > 0 )
    rc = writeChar(h,s,n);
  return rc;
}


cw::rc_t cw::file::readStr(  handle_t h, char** sRef, unsigned maxCharN )
{
  unsigned n;
  rc_t rc;

  cwAssert(sRef != nullptr );

  *sRef = nullptr;
  
  if( maxCharN == 0 )
    maxCharN = 16384;

  // read the string length
  if((rc = readUInt(h,&n,1)) != kOkRC )
    return rc;

  // verify that string isn't too long
  if( n > maxCharN  )
  {
    return cwLogError(kInvalidArgRC,"The stored string is larger than the maximum allowable size of %i.",maxCharN);    
  }

  // allocate a read buffer
  char* s = mem::allocZ<char>(n+1);

  // fill the buffer from the file
  if((rc = readChar(h,s,n)) != kOkRC )
    return rc;

  s[n] = 0; // terminate the string

  *sRef = s;

  return rc;
}


cw::rc_t cw::file::print(   handle_t h, const char* text )
{
  this_t* p = _handleToPtr(h);

  errno = 0;
  if( fputs(text,p->fp) < 0 )
    return p->lastRC = cwLogSysError(kOpFailRC,errno,"File print failed on '%s'.", cwStringNullGuard(name(h)));

  return kOkRC;
}


cw::rc_t cw::file::vPrintf( handle_t h, const char* fmt, va_list vl )
{
  this_t* p = _handleToPtr(h);
  
  if( vfprintf(p->fp,fmt,vl) < 0 )
    return p->lastRC = cwLogSysError(kOpFailRC,errno,"File print failed on '%s'.", cwStringNullGuard(name(h)));
  
  return kOkRC;
}

cw::rc_t cw::file::printf(  handle_t h, const char* fmt, ... )
{
  va_list vl;
  va_start(vl,fmt);
  rc_t rc = vPrintf(h,fmt,vl);
  va_end(vl);
  return rc;
}


