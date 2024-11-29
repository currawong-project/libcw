#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwDynRefTbl.h"

namespace cw
{
  namespace dyn_ref_tbl
  {
    typedef struct dyn_ref_tbl_str
    {
      dyn_ref_t* dynRefA;       // dynRefA[ dynRefN ] one entry per dyn. level
      unsigned   dynRefN;       //
      unsigned*  levelLookupA;  // levelLoopA[ levelLookupN ] - one entry per velocity value (0-127)
      unsigned   levelLookupN;  // levelLooupN = kMidiVelCnt
      
    } dyn_ref_tbl_t;

    dyn_ref_tbl_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,dyn_ref_tbl_t>(h); }
        
    rc_t _destroy( dyn_ref_tbl_t* p )
    {
      rc_t rc = kOkRC;

      auto relse = [](dyn_ref_t& x) { mem::release(x.marker); };
            
      std::for_each(p->dynRefA, p->dynRefA+p->dynRefN, relse );
      mem::release(p->dynRefA);
      mem::release(p->levelLookupA);
      mem::release(p);
      return rc;
    }

    void _fill_level_lookup_table( dyn_ref_tbl_t* p )
    {

      for(midi::byte_t vel=0; vel<midi::kMidiVelCnt; ++vel)
        for(unsigned i=0; i<p->dynRefN; ++i)
          if( p->dynRefA[i].velocity >= vel )
          {
            midi::byte_t d0 = p->dynRefA[i].velocity - vel;          
            midi::byte_t d1 = i>0 ? (vel -  p->dynRefA[i-1].velocity) : d0;          
            unsigned     j = d0 <= d1 ? i : i-1;

            assert( vel <= p->levelLookupN );
            
            p->levelLookupA[vel] = p->dynRefA[j].level;
            break;
          }
      
    }

    rc_t _parse_cfg( dyn_ref_tbl_t* p, const object_t* cfg )
    {
      rc_t rc = kOkRC;
      
      // parse the dynamics ref. array
      p->dynRefN = cfg->child_count();
  
      if( p->dynRefN == 0 )
        cwLogWarning("The dynamic reference array cfg. is empty.");
      else
      {
        p->dynRefA = mem::allocZ<dyn_ref_t>(p->dynRefN);
  
        for(unsigned i=0; i<p->dynRefN; ++i)
        {
          const object_t* r = cfg->child_ele(i);
          const char* marker = nullptr;
          
          if((rc = r->getv("mark", marker,
                           "level", p->dynRefA[i].level,
                           "vel", p->dynRefA[i].velocity)) != kOkRC )
          {
            rc = cwLogError(kSyntaxErrorRC,"Error parsing the dynamics reference array.");
            goto errLabel;
          }
    
          p->dynRefA[i].marker = mem::duplStr(marker);
        }
      }

  
    errLabel:

      return rc;
    }
  }
}

cw::rc_t cw::dyn_ref_tbl::create( handle_t& hRef, const object_t* cfg )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  dyn_ref_tbl_t* p = mem::allocZ<dyn_ref_tbl_t>();

  if((rc = _parse_cfg( p, cfg )) != kOkRC )
  {
    rc = cwLogError(rc,"Dynamics reference table parse failed.");
    goto errLabel;
  }

  p->levelLookupN = midi::kMidiVelCnt;
  p->levelLookupA = mem::allocZ<unsigned>(p->levelLookupN);
  _fill_level_lookup_table(p);

  hRef.set(p);
  
 errLabel:

  return rc;
}

cw::rc_t cw::dyn_ref_tbl::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if(!hRef.isValid())
    return rc;

  dyn_ref_tbl_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  
  return rc;
}

const char* cw::dyn_ref_tbl::level_to_marker( handle_t h, unsigned level )
{
  dyn_ref_tbl_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->dynRefN; ++i)
    if( p->dynRefA[i].level )
      return p->dynRefA[i].marker;
  return nullptr;
}

unsigned    cw::dyn_ref_tbl::marker_to_level( handle_t h, const char* marker )
{
  dyn_ref_tbl_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->dynRefN; ++i)
    if( textIsEqual(p->dynRefA[i].marker,marker) )
      return p->dynRefA[i].level;
  return kInvalidIdx;
}

cw::midi::byte_t     cw::dyn_ref_tbl::level_to_velocity( handle_t h , unsigned level )
{
  dyn_ref_tbl_t* p = _handleToPtr(h);
  for(unsigned i=0; i<p->dynRefN; ++i)
    if( p->dynRefA[i].level == level )
      return p->dynRefA[i].velocity;
  return midi::kInvalidMidiByte;
}

unsigned    cw::dyn_ref_tbl::velocity_to_level( handle_t h, midi::byte_t vel )
{
  dyn_ref_tbl_t* p = _handleToPtr(h);
  
  if( vel <= p->levelLookupN )
    return p->levelLookupA[vel];
  
  cwLogError(kInvalidArgRC,"The velocity value %i is out of range on level lookup.");
  return 0;
}

void cw::dyn_ref_tbl::report( handle_t h )
{
  dyn_ref_tbl_t* p = _handleToPtr(h);
  unsigned i = 0;

  printf("Dynamics Table\n");
  auto rpt = [](dyn_ref_t& x) { printf("%2i %3i %s\n",x.level,x.velocity,x.marker); };
  
  std::for_each(p->dynRefA,p->dynRefA+p->dynRefN, rpt );

  printf("Dynamics Table Level Lookup Table.\n");
  auto rpt_level = [&i](unsigned& x) { printf("%3i %2i\n",i++,x); };

  std::for_each(p->levelLookupA,p->levelLookupA+p->levelLookupN, rpt_level);
}
