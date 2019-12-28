#include "cwCommon.h"
#include "cwLog.h"

#include "cwFileSys.h"
#include "cwCommonImpl.h"
#include "cwMem.h"

#ifdef cwLINUX
#include <libgen.h>
#include <sys/stat.h>
#endif

namespace cw
{

  bool _fileSysConcat( char* rp, unsigned rn, char sepChar, const char* suffixStr )
  {
    unsigned m = strlen(rp);

    // m==0 if no sep needs to be inserted or removed

    //if( m == 0 )
    //  return false;

    if( m != 0 )
    {

      // if a sep char needs to be inserted
      if( rp[m-1] != sepChar && suffixStr[0] != sepChar )
      {
        cwAssert((m+1)<rn);

        if((m+1)>=rn)
          return false;

        rp[m]  = sepChar;
        rp[m+1]= 0;
        ++m;
      }  
      else
        // if a sep char needs to be removed
        if( rp[m-1] == sepChar && suffixStr[0] == sepChar )
        {
          rp[m-1] = 0;
          --m;
        }

    }

    cwAssert( rn>=m && strlen(rp)+strlen(suffixStr) <= rn );
    strncat(rp,suffixStr,rn-m);

    return true;
  }
}

bool cw::filesys::isDir( const char* dirStr )
{
  struct stat s;

  errno = 0;

  if( stat(dirStr,&s)  != 0 )
  {
    // if the dir does not exist
    if( errno == ENOENT )
      return false;

    cwLogSysError( kOpFailRC, errno, "'stat' failed on '%s'",dirStr);
    return false;
  }
 
  return S_ISDIR(s.st_mode);
}

bool cw::filesys::isFile( const char* fnStr )
{
  struct stat s;
  errno = 0;

  if( stat(fnStr,&s)  != 0 )
  {

    // if the file does not exist
    if( errno == ENOENT )
      return false;

    cwLogSysError( kOpFailRC, errno, "'stat' failed on '%s'.",fnStr);
    return false;
  }
 
  return S_ISREG(s.st_mode);
}


bool cw::filesys::isLink( const char* fnStr )
{
  struct stat s;
  errno = 0;

  if( lstat(fnStr,&s)  != 0 )
  {
    // if the file does not exist
    if( errno == ENOENT )
      return false;

    cwLogSysError( kOpFailRC, errno, "'stat' failed on '%s'.",fnStr);
    return false;
  }
 
  return S_ISLNK(s.st_mode);
}



char* cw::filesys::vMakeFn( const char* dir, const char* fn, const char* ext, va_list vl )
{
  rc_t        rc      = kOkRC;
  char*       rp      = nullptr;
  const char* dp      = nullptr;
  unsigned    n       = 0;
  char        pathSep = cwPathSeparatorChar;
  char        extSep  = '.';
  va_list     vl_t;
  va_copy(vl_t,vl);

  // get prefix directory length
  if( dir != nullptr )
    n += strlen(dir) + 1;  // add 1 for ending sep

  // get file name length
  if( fn != nullptr )
    n += strlen(fn);

  // get extension length
  if( ext != nullptr )
    n += strlen(ext) + 1;  // add 1 for period

  // get length of all var args dir's
  while( (dp = va_arg(vl,const char*)) != nullptr )
    n += strlen(dp) + 1;  // add 1 for ending sep

   // add 1 for terminating zero and allocate memory

  if((rp = mem::allocZ<char>( n+1 )) == nullptr )
  {
    rc = cwLogError(kMemAllocFailRC,"Unable to allocate file name memory.");
    goto errLabel;
  }

  va_copy(vl,vl_t);

  rp[n] = 0;
  rp[0] = 0;

  // copy out the prefix dir
  if( dir != nullptr )
    strncat(rp,dir,n-strlen(rp));

  // copy out each of the var arg's directories
  while((dp = va_arg(vl,const char*)) != nullptr )
    if(!_fileSysConcat(rp,n,pathSep,dp) )
    {
      cwAssert(0);
      goto errLabel;
    }


  // copy out the file name
  if( fn != nullptr )
    if(!_fileSysConcat(rp,n,pathSep,fn))
    {
      cwAssert(0);
      goto errLabel;
    }
  
  // copy out the extension
  if( ext != nullptr )
    if(!_fileSysConcat(rp,n,extSep,ext))
    {
      cwAssert(0);
      goto errLabel;
    }

  cwAssert(strlen(rp)<=n);
  
 errLabel:

  if( rc != kOkRC && rp != nullptr )
    mem::release( rp );

  return rp;
}


char* cw::filesys::makeFn(  const char* dir, const char* fn, const char* ext, ... )
{
  va_list vl;
  va_start(vl,ext);
  char* fnOut = filesys::vMakeFn(dir,fn,ext,vl);
  va_end(vl);
  return fnOut;
}



cw::filesys::pathPart_t* cw::filesys::pathParts( const char* pathStr )
{
  unsigned           n  = 0;    // char's in pathStr
  unsigned           dn = 0;    // char's in the dir part
  unsigned           fn = 0;    // char's in the name part
  unsigned           en = 0;    // char's in the ext part
  char*              cp = nullptr;
  pathPart_t* rp = nullptr;


  if( pathStr==nullptr )
    return nullptr;

  // skip leading white space
  for(; *pathStr; ++pathStr )
    if( !isspace(*pathStr ) )
      break;

  
  // get the length of pathStr
  n = strlen(pathStr);

  // remove trailing spaces
  for(; n > 0; --n )
    if( !isspace(pathStr[n-1]) )
      break;

  // if pathStr is empty
  if( n == 0 )
    return nullptr;
  

  char buf[n+1];
  buf[n] = 0;

  // Get the last word (which may be a file name) from pathStr.
  // (pathStr must be copied into a buf because basename()
  // is allowed to change the values in its arg.)
  strncpy(buf,pathStr,n);
  cp = basename(buf);
  
  if( cp != nullptr )
  {
    char* ep;
    // does the last word have a period in it
    if( (ep = strchr(cp,'.')) != nullptr )
    {
      *ep = 0;         // end the file name at the period
      ++ep;            // set the ext marker
      en = strlen(ep); // get the length of the ext
    }  

    fn = strlen(cp); //get the length of the file name
  }

  // Get the directory part.
  // ( pathStr must be copied into a buf because dirname()
  // is allowed to change the values in its arg.)
  strncpy(buf,pathStr,n);
  
  // if the last char in pathStr[] is '/' then the whole string is a dir.
  // (this is not the answer that dirname() and basename() would give).
  if( pathStr[n-1] == cwPathSeparatorChar )
  {
    fn = 0;
    en = 0;
    dn = n;
    cp = buf;    
  }
  else
  {    
    cp = dirname(buf);
  }


  if( cp != nullptr  )
    dn = strlen(cp);

  // get the total size of the returned memory. (add 3 for ecmh possible terminating zero)
  n = sizeof(pathPart_t) + dn + fn + en + 3;

  // alloc memory
  if((rp = mem::allocZ<pathPart_t>( n )) == nullptr )
  {
    cwLogError( kMemAllocFailRC, "Unable to allocate the file system path part record for '%s'.",pathStr);
    return nullptr;
  }
  
  // set the return pointers for ecmh of the parts
  rp->dirStr = (const char* )(rp + 1);
  rp->fnStr  = rp->dirStr + dn + 1;
  rp->extStr = rp->fnStr  + fn + 1;


  // if there is a directory part
  if( dn>0 )
    strcpy((char*)rp->dirStr,cp);
  else
    rp->dirStr = nullptr;
  
  if( fn || en )
  {
    // Get the trailing word again.
    // pathStr must be copied into a buf because basename() may
    // is allowed to change the values in its arg.
    strncpy(buf,pathStr,n);
    cp = basename(buf);

    
    if( cp != nullptr )
    {
      
      char* ep;
      if( (ep = strchr(cp,'.')) != nullptr )
      {
        *ep = 0;
        ++ep;

        cwAssert( strlen(ep) == en );
        strcpy((char*)rp->extStr,ep);
      }
      

      cwAssert( strlen(cp) == fn );
      if(fn)
        strcpy((char*)rp->fnStr,cp);
    }
  }

  if( fn == 0 )
    rp->fnStr = nullptr;

  if( en == 0 )
    rp->extStr = nullptr;

  return rp;
  
}

