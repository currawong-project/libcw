#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwFileSys.h"
#include "cwLib.h"

#ifdef OS_LINUX

#include <dlfcn.h>

typedef void* dynLibH_t;

bool _cmLibIsNull( dynLibH_t lh )
{ return lh == nullptr; };

const char* _cmLibSysError()
{
  const char* msg = dlerror();
  if( msg == nullptr )
    msg = "<none>";
  return msg;
}

dynLibH_t  _cmLibOpen( const char* libFn )
{ return dlopen(libFn,RTLD_LAZY); }

const char* _cmLibClose( dynLibH_t* lH )
{
  if( *lH != nullptr )
  {
    if( dlclose(*lH) == 0 )
      *lH = nullptr;
    else
      return dlerror();
  }

  return nullptr;
}

void* _cmLibSym( dynLibH_t h, const char* symLabel )
{ return dlsym(h,symLabel); }


#endif


namespace cw
{
  namespace lib
  {
    typedef void* dynLibH_t;
    
    typedef struct node_str
    {
      char*            fn;       // NULL for available nodes
      unsigned         id;       // kInvalidId for available nodes
      dynLibH_t        dynLibH;  // The platform dependent library handle
      struct node_str* link;
    } node_t;
    
    typedef struct lib_str
    {
      node_t*  list;   // list of open libraries
      unsigned id;     // next library id
    } lib_t;

    lib_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,lib_t>(h); }

    // Given a library id return the associated node
    node_t* _libIdToNode( lib_t* p, unsigned libId )
    {
      node_t* np  = p->list;

      while( np != nullptr )
      {
        if( np->id == libId )
          return np;

        np = np->link;
      }

      return nullptr;
    }

    // Close a library and make it's node available for reuse
    rc_t _closeLib( node_t* np )
    {
      rc_t        rc     = kOkRC;
      const char* errMsg = nullptr;

      // tell the system to close the library
      if((errMsg = _cmLibClose( &np->dynLibH )) != nullptr )
        rc = cwLogError(kInvalidOpRC,"Library %s close failed. Error:%s.", np->fn, errMsg );
      else
      {
        // mark the node as available
        mem::release(np->fn);
        np->id = kInvalidId;
      }
      
      return rc;
    }

    // Finalize the library manager
    rc_t _finalize( lib_t* p )
    {
      rc_t    rc = kOkRC;
      node_t* np = p->list;
      while( np!=nullptr )
      {
        node_t* n0p = np->link;

        // close the node's library
        rc_t    rc0 = _closeLib( np );

        // store the error code
        if( rc0 != kOkRC )
          rc = rc0;

        // release the node
        mem::release(np);
        
        np = n0p;
      }
      
      mem::release(p);
      return rc;
    }    
  }
}

cw::rc_t  cw::lib::initialize( handle_t& h, const char* dirStr )
{
  rc_t rc;
  if((rc = finalize(h)) != kOkRC )
    return rc;

  lib_t* p = mem::allocZ<lib_t>();

  h.set(p);

  return rc;
}

cw::rc_t  cw::lib::finalize(   handle_t& h )
{
  rc_t rc = kOkRC;
  if( !h.isValid() )
    return rc;

  lib_t* p = _handleToPtr(h);
  if((rc = _finalize(p)) != kOkRC )
    return rc;

  h.clear();

  return rc;  
}

cw::rc_t  cw::lib::open( handle_t h, const char* fn, unsigned& libIdRef )
{
  rc_t         rc  = kOkRC;
  lib_t*       p   = _handleToPtr(h);  
  dynLibH_t    lH  = _cmLibOpen(fn);
  node_t*      np  = p->list;
  unsigned     idx = 0;

  libIdRef = kInvalidId;
  
  if( _cmLibIsNull(lH) )
  {
    // There is apparently no way to get an error code which indicates that the
    // file load attempt failed because the file was not a shared library - 
    // which should not generate an error message - therefore
    // we must match the end of the the error string returned by dlerror() with 
    // 'invalid ELF header'.
    const char* errMsg = _cmLibSysError();
    const char* s      = "invalid ELF header";
    unsigned    sn     = strlen(s);     
    unsigned    mn     = strlen(errMsg);

    // Did this load fail because the file was not a shared library?
    if( errMsg!=nullptr && mn>sn && strcmp(errMsg+mn-sn,s)==0 )
      rc = cwLogError(kOpenFailRC,"Library load failed. No error message." ); // signal error but no error message
    else
      rc = cwLogError(kOpenFailRC,"Library load failed. System Message: %s", errMsg );

    return rc;
  }

  // find an available node
  while( np != nullptr )
  {
    if( np->fn == nullptr )
      break;

    np = np->link;
    ++idx;
  }

  // no available node was found - allocate  a new one
  if( np == nullptr )
  {
    np = mem::allocZ<node_t>(1);
    np->link = p->list;
    p->list = np;
  }

  // initialize the node 
  np->fn      = mem::duplStr(fn);
  np->dynLibH = lH;
  np->id      = p->id++;
  libIdRef    = np->id;
  
  return idx;    
}

cw::rc_t  cw::lib::close( handle_t h, unsigned libId )
{
  lib_t*      p  = _handleToPtr(h);
  node_t*     np;

  // locate the library to close
  if((np = _libIdToNode(p,libId)) == nullptr )
    return cwLogError(kInvalidIdRC,"Library close failed. The library with id:%i not found.",libId);
  
  return _closeLib(np);
}

void* cw::lib::symbol( handle_t h, unsigned libId, const char* symName )
{
  void*   f;
  lib_t*  p  = _handleToPtr(h);  
  node_t* np = _libIdToNode(p,libId);
  
  if( (np == NULL) || _cmLibIsNull(np->dynLibH) )
  {
     cwLogError(kInvalidArgRC,"The library id %i is not valid or the library is closed.",libId);
     return NULL;
  }

  if((f = _cmLibSym(np->dynLibH,symName)) == NULL)
  {
    cwLogError(kInvalidArgRC,"The dynamic symbol '%s' was not found. System Message: %s", cwStringNullGuard(symName), _cmLibSysError());
    return NULL;
  }

  return f;
}

cw::rc_t cw::lib::scan( handle_t h, const char* dirStr )
{
  rc_t                 rc        = kOkRC;
  unsigned             dirEntryN = 0;
  filesys::dirEntry_t* de        = dirEntries( dirStr, filesys::kFileFsFl, &dirEntryN );
  unsigned             libId;
  
  for(unsigned i=0; i<dirEntryN; ++i)
    if( de[i].name != nullptr )
      if((rc = open( h, de[i].name, libId )) != kOkRC )
        cwLogWarning("The file '%s' is not a shared library.",de[i].name);

  mem::release(de);
  return rc;
}

unsigned    cw::lib::count( handle_t h )
{
  lib_t*   p  = _handleToPtr(h);
  unsigned n  = 0;
  node_t*  np = p->list;
  for(; np!=nullptr; np=np->link)
    ++n;
  return n;
}

unsigned    cw::lib::indexToId( handle_t h, unsigned idx )
{
  lib_t*   p  = _handleToPtr(h);
  unsigned n  = 0;
  node_t*  np = p->list;
  
  for(; np!=nullptr; np=np->link)
    if( n == idx )
      return np->id;

  cwLogError(kInvalidArgRC, "The library index %i is not valid.",idx);
  return kInvalidId;
}


const char* cw::lib::name( handle_t h, unsigned id )
{
  lib_t*   p  = _handleToPtr(h);
  node_t* np;
  if((np = _libIdToNode(p,id)) == nullptr )
  {
    cwLogError(kInvalidArgRC,"The library associated with the the id:%i could not be found.",id);
    return nullptr;
  }
  return np->fn;
}
