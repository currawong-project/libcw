#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwScoreFollower.h"

#include "cmGlobal.h"
#include "cmFloatTypes.h"
#include "cmRpt.h"
#include "cmErr.h"
#include "cmCtx.h"
#include "cmTime.h"
#include "cmMidi.h"
#include "cmSymTbl.h"
#include "cmScore.h"

namespace cw
{
  namespace score_follower
  {

    typedef struct score_follower_str
    {
      unsigned search_area_locN;
      unsigned key_wnd_locN;
      char* score_csv_fname;
      
      
    } score_follower_t;

    score_follower_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,score_follower_t>(h); }

    
    rc_t _parse_cfg( score_follower_t* p, const object_t* cfg )
    {
      rc_t rc = kOkRC;

      const char* score_csv_fname;
      
      if((rc = cfg->getv("score_csv_fname",     score_csv_fname,
                         "search_area_locN",    p->search_area_locN,
                         "key_wnd_locN",        p->key_wnd_locN )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC, "Score follower argument parsing failed.");
        goto errLabel;
      }

      if((app->score_csv_fname = filesys::expandPath( score_csv_fname )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Score follower score file expansion failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }
    
    rc_t _destroy( score_follower_t* p)
    {
      mem::release(p->score_csv_fname);
      mem::release(p);
      return kOkRC;
    }
  }
}

cw::rc_t cw::score_follower::create( handle_t& hRef, const object_t* cfg, double srate )
{
  rc_t rc = kOkRC;
  score_follower_t* p = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  p = mem::allocZ<score_follower_t>();

  if((rc = _parse_cfg(p,cfg)) != kOkRC )
    goto errLabel;

  cmScRC_t      cmScoreInitialize( cmCtx_t* ctx, cmScH_t* hp, const cmChar_t* fn, double srate, const unsigned* dynRefArray, unsigned dynRefCnt, cmScCb_t cbFunc, void* cbArg, cmSymTblH_t stH );
  
  //cmRC_t       cmScMatcherInit(  cmScMatcher* p, double srate, cmScH_t scH, unsigned scWndN, unsigned midiWndN, cmScMatcherCb_t cbFunc, void* cbArg );
  
  

  hRef.set(p);
  
 errLabel:
  if( rc != kOkRC )
  {
    _destroy(p);
    cwLogError(rc,"Score follower create failed.");
  }
  
  return rc;
}
    
cw::rc_t cw::score_follower::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  score_follower_t* p = nullptr;
  
  if( !hRef.isValid() )
    return rc;

  p = _handleToPtr(hRef);
  
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  return rc;    
}

cw::rc_t cw::score_follower::reset( handle_t h, unsigned loc )
{
  rc_t rc = kOkRC;
  return rc;
}
    
cw::rc_t cw::score_follower::exec(  handle_t h, unsigned smpIdx, unsigned muid, unsigned status, uint8_t d0, uint8_t d1, unsigned* scLocIdxPtr )
{
  rc_t rc = kOkRC;
  return rc;

}
