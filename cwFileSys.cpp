#include "cwCommon.h"
#include "cwLog.h"

#include "cwFileSys.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwString.h"
#include "cwText.h"

#ifdef OS_LINUX
#include <libgen.h> // basename() dirname()
#include <sys/stat.h>
#include <dirent.h> // opendir()/readdir()
#include <wordexp.h>
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

  if( dirStr == nullptr )
    return false;

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

  if( fnStr == nullptr )
    return false;
  
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

  if( fnStr == nullptr )
    return false;

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



char* cw::filesys::vMakeFn( const char* dir0, const char* fn, const char* ext, va_list vl )
{
  rc_t        rc      = kOkRC;
  char*       dir     = nullptr;
  char*       rp      = nullptr;
  const char* dp      = nullptr;
  unsigned    n       = 0;
  char        pathSep = cwPathSeparatorChar;
  char        extSep  = '.';
  va_list     vl_t;
  va_copy(vl_t,vl);

  if( dir0 != nullptr )
    dir = expandPath(dir0);

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
  while( (dp = va_arg(vl_t,const char*)) != nullptr )
    n += strlen(dp) + 1;  // add 1 for ending sep

  va_end(vl_t);
  
  // add 1 for terminating zero and allocate memory

  if((rp = mem::allocZ<char>( n+1 )) == nullptr )
  {
    rc = cwLogError(kMemAllocFailRC,"Unable to allocate file name memory.");
    goto errLabel;
  }

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

  mem::release(dir);
  
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


char* cw::filesys::vMakeVersionedFn(const char* dir, const char* fn_prefix, const char* ext, va_list vl )
{
  char*          fn          = nullptr;
  const unsigned max_version = 1024;
  unsigned version;
  for(version = 0; version<max_version; ++version)
  {
    char name[ PATH_MAX ];
    va_list vl0;
    va_copy(vl0,vl);
    
    snprintf(name,PATH_MAX,"%s_%i",fn_prefix,version);

    fn = vMakeFn( dir, name, ext, vl0 );
    
    va_end(vl0);
    
    if( !isFile(fn) )
      break;

    mem::release(fn);
  }

  if( version >= max_version )
    cwLogError(kInvalidStateRC,"%i versioned files already exist - another one cannot be created.",max_version);

  if( fn == nullptr )
    cwLogError(kOpFailRC,"Create versioned filename failed.");

  return fn;
}

char* cw::filesys::makeVersionedFn( const char* dir, const char* fn_prefix, const char* ext, ... )
{
  va_list vl;
  va_start(vl,ext);
  char* fnOut = filesys::vMakeVersionedFn(dir,fn_prefix,ext,vl);
  va_end(vl);
  return fnOut;  
}

char* cw::filesys::replaceDirectory( const char* fn0, const char* dir  )
{
  pathPart_t* pp = nullptr;
  char*       fn = nullptr;
  
  if((pp = pathParts( fn0 )) == nullptr )
  {
    cwLogError(kOpFailRC,"File name parse failed.");
    return nullptr;
  }

  if((fn = makeFn( dir, pp->fnStr, pp->extStr)) == nullptr )
  {
    cwLogError(kOpFailRC,"Unable to replace directory.");
  }

  mem::release(pp);
  return fn;
  
}

char* cw::filesys::replaceFilename(  const char* fn0, const char* name )
{
  pathPart_t* pp = nullptr;
  char*       fn = nullptr;
  
  if((pp = pathParts( fn0 )) == nullptr )
  {
    cwLogError(kOpFailRC,"File name parse failed.");
    return nullptr;
  }

  if((fn = makeFn( pp->dirStr, name, pp->extStr)) == nullptr )
  {
    cwLogError(kOpFailRC,"Unable to replace file name.");
  }

  mem::release(pp);
  return fn;
}

char* cw::filesys::replaceExtension( const char* fn0, const char* ext  )
{
  pathPart_t* pp = nullptr;
  char*       fn = nullptr;
  
  if((pp = pathParts( fn0 )) == nullptr )
  {
    cwLogError(kOpFailRC,"File name parse failed.");
    return nullptr;
  }

  if((fn = makeFn( pp->dirStr, pp->fnStr, ext)) == nullptr )
  {
    cwLogError(kOpFailRC,"Unable to replace file extension.");
  }

  mem::release(pp);
  return fn;
}



char* cw::filesys::expandPath( const char* dir )
{
  rc_t      rc     = kOkRC;
  int       sysRC  = 0;
  int       flags  = WRDE_NOCMD;
  char*     newDir = nullptr;
  wordexp_t res;

  memset(&res,0,sizeof(res));

  // if dir is empty
  if( dir == nullptr )
    return nullptr;

  // skip leading white space
  for(; *dir; ++dir)
    if( !isspace(*dir) )
      break;

  // if dir is empty
  if( strlen(dir) == 0 )
    return nullptr;

  if((sysRC = wordexp(dir,&res,flags)) != 0)
  {
    switch(sysRC)
    {
      case WRDE_BADCHAR:
        rc = cwLogError(kOpFailRC,"Bad character.");
        break;
        
      case WRDE_BADVAL:
        rc = cwLogError(kOpFailRC,"Bad value.");
        break;
        
      case WRDE_CMDSUB:
        rc = cwLogError(kOpFailRC,"Command substitution forbidden.");
        break;
        
      case WRDE_NOSPACE:
        rc = cwLogError(kOpFailRC,"Mem. alloc failed.");
        break;
        
      case WRDE_SYNTAX:
        rc = cwLogError(kOpFailRC,"Syntax error..");
        break;   
    }

    goto errLabel;
  }

  switch( res.we_wordc )
  {
    case 0:
      newDir = str::dupl(dir);
      break;
      
    case 1:
      newDir = str::dupl(res.we_wordv[0]);
      break;
      
    default:
      newDir = str::join(" ", (const char**)res.we_wordv, res.we_wordc );
  }
  
 errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"Path expansion failed.");

  wordfree(&res);

  
  return newDir;
  
}



cw::filesys::pathPart_t* cw::filesys::pathParts( const char* path0Str )
{
  unsigned           n  = 0;    // char's in pathStr
  unsigned    dn  = 0;          // char's in the dir part
  unsigned    fn  = 0;          // char's in the name part
  unsigned    en  = 0;          // char's in the ext part
  char*       cp  = nullptr;
  pathPart_t* rp  = nullptr;

  char* pathBaseStr = expandPath(path0Str);
  const char* pathStr = pathBaseStr;
  
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
  {
    // nothing to do
    goto errLabel;
  }
  else
  {

    
  
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
    if((rp = (pathPart_t*)mem::allocZ<char>( n )) == nullptr )
    {
      cwLogError( kMemAllocFailRC, "Unable to allocate the file system path part record for '%s'.",pathStr);
      goto errLabel;
    }
  
    // set the return pointers for each of the parts
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
      strcpy(buf,pathStr);
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
  }
  
 errLabel:

  mem::release(pathBaseStr);
  
  
  return rp;
}

namespace cw
{
  namespace filesys
  {
    typedef struct 
    {
      unsigned    filterFlags;
      dirEntry_t* rp;
      char*       dataPtr;
      char*       endPtr;
      unsigned    entryCnt;
      unsigned    entryIdx;
      unsigned    dataByteCnt;
      unsigned    passIdx;
    } deRecd_t;
    
    cw::rc_t _dirGetEntries(  deRecd_t* drp, const char* dirStr )
    {
      rc_t           rc          = kOkRC;
      DIR*           dirp        = NULL;
      struct dirent* dp          = NULL;
      char           curDirPtr[] = "./";  
      unsigned       dn          = 0;


      if( dirStr == NULL || strlen(dirStr) == 0 )
        dirStr = curDirPtr;

      if( isDir(dirStr) == false )
        return rc;

      unsigned fnCharCnt = strlen(dirStr) + PATH_MAX;
      char     fn[ fnCharCnt + 1 ];


      // copy the directory into fn[] ...
      fn[0]         = 0;
      fn[fnCharCnt] = 0;

      strcpy(fn,dirStr);

      cwAssert( strlen(fn)+2 < fnCharCnt );
  
      // ... and be sure that it is terminated with a path sep char
      if( fn[ strlen(fn)-1 ] != cwPathSeparatorChar )
      {
        char sep[] = { cwPathSeparatorChar, '\0' };                    
        strcat(fn,sep);
      }
      // file names will be appended to the path at this location
      unsigned fni = strlen(fn);

      // open the directory
      if((dirp = opendir(dirStr)) == NULL)
      {
        rc = cwLogSysError(kOpFailRC,errno,"Unable to open the directory:'%s'.",dirStr);

        goto errLabel;
      }

      // get the next directory entry
      while((dp = readdir(dirp)) != NULL )
      {
        // validate d_name 
        if( (dn = strlen(dp->d_name)) > 0 )
        {
          unsigned flags = 0;
        
          // handle cases where d_name begins with '.'
          if( dp->d_name[0] == '.' )
          {
            if( strcmp(dp->d_name,".") == 0 )
            {
              if( cwIsFlag(drp->filterFlags,kCurDirFsFl) == false )
                continue;

              flags |= kCurDirFsFl;
            }

            if( strcmp(dp->d_name,"..") == 0 )
            {
              if( cwIsFlag(drp->filterFlags,kParentDirFsFl) == false )
                continue;

              flags |= kParentDirFsFl;
            }

            if( flags == 0 )
            {
              if( cwIsFlag(drp->filterFlags,kInvisibleFsFl) == false )
                continue;

              flags |= kInvisibleFsFl;
            }
          }

          fn[fni] = 0;
          strncat( fn, dp->d_name, fnCharCnt-fni );
          unsigned fnN = strlen(fn);

          // if the filename is too long for the buffer
          if( fnN > fnCharCnt )
          {
            rc = cwLogSysError(kBufTooSmallRC, errno, "The directory entry:'%s' was too long to be processed.",dp->d_name);
            goto errLabel;
          }

          // is a link
          if( isLink(fn) )
          {
            if( cwIsFlag(drp->filterFlags,kLinkFsFl) == false )
              continue;

            flags |= kLinkFsFl;

            if( cwIsFlag(drp->filterFlags,kRecurseLinksFsFl) )
              if((rc = _dirGetEntries(drp,fn)) != kOkRC )
                goto errLabel;
          }
          else
          {

            // is the entry a file
            if( isFile(fn) )
            {
              if( cwIsFlag(drp->filterFlags,kFileFsFl)==false )
                continue;

              flags |= kFileFsFl;
            }
            else
            {
              // is the entry a dir
              if( isDir(fn) )
              {
                if( cwIsFlag(drp->filterFlags,kDirFsFl) == false)
                  continue;

                flags |= kDirFsFl;

                if( cwIsFlag(drp->filterFlags,kRecurseFsFl) )
                  if((rc = _dirGetEntries(drp,fn)) != kOkRC )
                    goto errLabel;
              }
              else
              {
                continue;
              }
            }
          }

          //cwAssert(flags != 0);

          if( drp->passIdx == 0 )
          {
            ++drp->entryCnt;
        
            // add 1 for the name terminating zero
            drp->dataByteCnt += sizeof(dirEntry_t) + 1;

            if( cwIsFlag(drp->filterFlags,kFullPathFsFl) )
              drp->dataByteCnt += fnN;
            else
              drp->dataByteCnt += dn;

          }
          else
          {
            cwAssert( drp->passIdx == 1 );
            cwAssert( drp->entryIdx < drp->entryCnt );

            unsigned n = 0;
            if( cwIsFlag(drp->filterFlags,kFullPathFsFl) )
            {
              n = fnN+1;
              cwAssert( drp->dataPtr + n <= drp->endPtr );
              strcpy(drp->dataPtr,fn);
            }
            else
            {
              n = dn+1;
              cwAssert( drp->dataPtr + n <= drp->endPtr );
              strcpy(drp->dataPtr,dp->d_name);
            }

            drp->rp[ drp->entryIdx ].flags = flags;
            drp->rp[ drp->entryIdx ].name  = drp->dataPtr;
            drp->dataPtr += n;
            cwAssert( drp->dataPtr <= drp->endPtr);
            ++drp->entryIdx;
          }
        }  
      }

    errLabel:
      if( dirp != NULL )
        closedir(dirp);

      return rc;
    }
  }
}

cw::filesys::dirEntry_t* cw::filesys::dirEntries( const char* dirStr, unsigned filterFlags, unsigned* dirEntryCntPtr )
{
  rc_t     rc = kOkRC;
  deRecd_t r;

  memset(&r,0,sizeof(r));
  r.filterFlags = filterFlags;

  cwAssert( dirEntryCntPtr != NULL );
  *dirEntryCntPtr = 0;

  char* inDirStr  = filesys::expandPath(dirStr);
  
  for(r.passIdx=0; r.passIdx<2; ++r.passIdx)
  {
    if((rc = _dirGetEntries( &r, inDirStr )) != kOkRC )
      goto errLabel;

    if( r.passIdx == 0 && r.dataByteCnt>0 )
    {
      // allocate memory to hold the return values
      if(( r.rp = mem::allocZ<dirEntry_t>( r.dataByteCnt )) == NULL )
      {
        rc = cwLogError(kObjAllocFailRC, "Unable to allocate %i bytes of dir entry memory.",r.dataByteCnt);
        goto errLabel;
      }

      r.dataPtr = (char*)(r.rp + r.entryCnt);
      r.endPtr  = ((char*)r.rp) + r.dataByteCnt; 
    }
  }

 errLabel:
  
  if( rc == kOkRC )
  {
    cwAssert( r.entryIdx == r.entryCnt );
    *dirEntryCntPtr = r.entryCnt;
  }
  else
  {
    if( r.rp != NULL )
      mem::release(r.rp);    

    r.rp = NULL;
  }

  mem::release(inDirStr);

  return r.rp;
}

char* cw::filesys::formVersionedDirectory(const char* recordDir, const char* recordFolder)
{
  char* dir = nullptr;
      
  for(unsigned version_numb=0; true; ++version_numb)
  {
    unsigned n = textLength(recordFolder) + 32;
    char     folder[n+1];

    snprintf(folder,n,"%s_%i",recordFolder,version_numb);
        
    if((dir = filesys::makeFn(recordDir,folder, NULL, NULL)) == nullptr )
    {
      cwLogError(kOpFailRC,"Unable to form a versioned directory from:'%s'",cwStringNullGuard(recordDir));
      return nullptr;
    }

    if( !filesys::isDir(dir) )
      break;

    mem::release(dir);
  }
      
  return dir;
}

char* cw::filesys::makeVersionedDirectory(const char* recordDir, const char* recordFolder )
{
  rc_t rc = kOkRC;
  char* dir;
  if((dir = formVersionedDirectory(recordDir,recordFolder)) == nullptr )
    return nullptr;

  if((rc = makeDir(dir)) != kOkRC )
    return nullptr;

  return dir;  
}

cw::rc_t cw::filesys::makeDir( const char* dirStr )
{

  if( str::len(dirStr) == 0 )
    return kOkRC;
  
  char* s  = filesys::expandPath(dirStr);

  if( !isDir(s) )
    if( mkdir(s, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0 )
      return cwLogSysError( kOpFailRC, errno, "The attempt to create the directory '%s' failed.",dirStr);

  mem::release(s);
  
  return kOkRC;
}
