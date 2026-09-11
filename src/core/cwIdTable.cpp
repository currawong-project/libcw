#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwIdTable.h"
#include "cwMem.h"
#include "cwText.h"

namespace cw
{
  namespace id_table
  {
    typedef struct id_str
    {
      char*    label;
    } id_t;
    
    typedef struct id_table_str
    {
      id_t*    tableA;
      unsigned tableN;
      unsigned tableAllocN;
    } id_table_t;

    handle_t __global_handle__;

    id_table_t* _handle_to_ptr(handle_t h)
    {  return handleToPtr<handle_t,id_table_t>(h); }

    unsigned _register_label( id_table_t* p, const char* label )
    {
      unsigned id = p->tableN;
      
      if( p->tableN >= p->tableAllocN )
      {
        p->tableAllocN *= 2;
        p->tableA = mem::resizeZ<id_t>(p->tableA,p->tableAllocN);        
      }

      p->tableA[ id ].label = mem::duplStr(label);
      
      p->tableN += 1;

      return id;
    }


    rc_t _destroy( id_table_t* p )
    {      
      mem::release(p->tableA);
      return kOkRC;
    }
  }
}

cw::rc_t cw::id_table::create( handle_t& hRef, unsigned init_table_cnt )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  init_table_cnt = std::max(1u,init_table_cnt);
  
  id_table_t* p = mem::allocZ<id_table_t>();
  p->tableA = mem::allocZ<id_t>(init_table_cnt);
  p->tableAllocN = init_table_cnt;
  p->tableN = 0;

  hRef.set(p);
errLabel:
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::id_table::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if(!hRef.isValid())
    return rc;

  id_table_t* p = _handle_to_ptr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  mem::release(p);
  hRef.clear();
  
  return rc;
}

unsigned    cw::id_table::get_id( handle_t h, const char* label )
{
  unsigned id = kInvalidId;

  id_table_t* p = _handle_to_ptr(h);

  for(unsigned i=0; i<p->tableN; ++i)
    if( textIsEqual(label,p->tableA[i].label) )
      return i;
  
  return _register_label(p,label);
}

const char* cw::id_table::get_label( handle_t h, unsigned id )
{
  id_table_t* p = _handle_to_ptr(h);

  if( id >= p->tableN )
    return nullptr;
    
  return p->tableA[id].label;
}

cw::rc_t cw::id_table::get_label( handle_t h, unsigned id, const char*& label_ref )
{
  rc_t rc = kOkRC;
  
  if((label_ref = get_label(h,id)) == nullptr )
    rc = cwLogError(kEleNotFoundRC,"The id '%i' is not valid.",id);

  return rc;
}

cw::rc_t     cw::id_table::create_global(unsigned init_table_cnt)
{
  rc_t rc = kOkRC;
  
  if( __global_handle__.isValid() )
    destroy_global();
  
  if((rc = create(__global_handle__,init_table_cnt)) != kOkRC )
  {
    rc = cwLogError(rc,"Global id_table create failed.");
  }
  
  return rc;
}

cw::rc_t     cw::id_table::destroy_global()
{
  return destroy(__global_handle__);
}
      
unsigned cw::id_table::get_id( const char* label )
{
  return get_id(__global_handle__,label);
}

const char* cw::id_table::get_label( unsigned id )
{
  return get_label(__global_handle__,id);
}

cw::rc_t cw::id_table::get_label( unsigned id, const char*& label_ref )
{
  return get_label(__global_handle__,id,label_ref);
}
