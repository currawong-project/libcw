#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwPianoScore.h"
#include "cwSvg.h"
#include "cwSvgScore.h"

namespace cw
{
  namespace svg_score
  {

    typedef struct svg_score_str
    {
      score::handle_t pianoScoreH;
      
    } svg_score_t;

    svg_score_t* _handleToPtr( handle_t h )
    {
      return handleToPtr<handle_t,svg_score_t>(h);
    }

    rc_t _destroy( svg_score_t* p )
    {
      rc_t rc = kOkRC;
      mem::release(p);
      return rc;
    }
    
  }
}

cw::rc_t cw::svg_score::create(  handle_t& hRef )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  svg_score_t* p = mem::allocZ<svg_score_t>();
    
  //errLabel:
  if(rc != kOkRC )
    _destroy(p);

  return rc;
}

cw::rc_t cw::svg_score::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if( !hRef.isValid() )
    return rc;

  svg_score_t* p = _handleToPtr(hRef);
  
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;  
}

cw::rc_t cw::svg_score::setPianoScore( handle_t h, score::handle_t pianoScH )
{
  svg_score_t* p = _handleToPtr(h);
  p->pianoScoreH = pianoScH;
  return kOkRC;
}

cw::rc_t cw::svg_score::write( handle_t h, const char* outFname, const char* cssFname )
{
  rc_t rc = kOkRC;
  return rc;    
}

cw::rc_t cw::svg_score::write( const object_t* cfg )
{
  rc_t                rc                = kOkRC;
  const char*         piano_score_fname = nullptr;
  const char*         cm_score_fname    = nullptr;
  const char*         css_fname         = nullptr;
  const char*         out_fname         = nullptr;
  score::handle_t     pianoScoreH;
  svg_score::handle_t svgScoreH;

  if((rc = cfg->getv("piano_score_fname",piano_score_fname,
                     "cm_score_fname",cm_score_fname,
                     "css_fname",css_fname,
                     "out_fname",out_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"Error parsing arguments.");
    goto errLabel;    
  }

  if((rc = create(svgScoreH)) != kOkRC )
  {
    rc = cwLogError(rc,"Error creating svg score.");
    goto errLabel;
  }

  if((rc = create(pianoScoreH,piano_score_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"Error opening piano score.");
    goto errLabel;
  }

  if((rc = setPianoScore(svgScoreH,pianoScoreH)) != kOkRC )
  {
    rc = cwLogError(rc,"Error on setPianoScore().");
    goto errLabel;
  }

 errLabel:

  destroy(svgScoreH);
  
  destroy(pianoScoreH);
  
  return rc;
  
}
