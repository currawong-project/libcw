//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwFileSys.h"
#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwSfMatch.h"
#include "cwSfTrack.h"
#include "cwScoreTest.h"

cw::rc_t cw::score_test::test( const object_t* cfg )
{

  rc_t                  rc              = kOkRC;
  const char*           cm_score_fname  = nullptr;
  const object_t*       dynArrayNode    = nullptr;
  const object_t*       sfmatchNode     = nullptr;
  const object_t*       sftrackNode     = nullptr;
  bool                  parse_fl        = false;
  bool                  parse_report_fl = false;
  bool                  parse_warn_fl   = false;
  bool                  score_fl        = false;
  bool                  score_report_fl = false;
  bool                  score_warn_fl   = false;
  bool                  match_fl        = false;
  bool                  track_fl        = false;
  double                srate           = 0;
  dyn_ref_tbl::handle_t dynRefH;
  sfscore::handle_t     scoreH;
  score_parse::handle_t spH;
  

  // parse the test cfg
  if((rc = cfg->getv( "score_fname", cm_score_fname,
                      "srate", srate,
                      "dyn_ref", dynArrayNode,
                      "sfmatch", sfmatchNode,
                      "sftrack", sftrackNode,
                      "parse_fl", parse_fl,
                      "parse_report_fl", parse_report_fl,
                      "parse_warn_fl", parse_warn_fl,
                      "score_fl", score_fl,
                      "score_report_fl", score_report_fl,
                      "score_warn_fl", score_warn_fl,
                      "match_fl", match_fl,
                      "track_fl", track_fl)) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore test parse params failed on.");
    goto errLabel;
  }

  // parse the dynamics reference array
  if((rc = dyn_ref_tbl::create(dynRefH,dynArrayNode)) != kOkRC )
  {
    rc = cwLogError(rc,"The reference dynamics array parse failed.");
    goto errLabel;
  }

  // if parsing was requested
  if( parse_fl )
  {
    // create the score_parse object
    if((rc = score_parse::create(spH,cm_score_fname,srate,dynRefH,parse_warn_fl)) != kOkRC )
    {
      rc = cwLogError(rc,"Score parse failed on '%s'.",cwStringNullGuard(cm_score_fname));
      goto errLabel;
    }
    
    if( parse_report_fl )
      report(spH);

    // if score processing was requested
    if( score_fl )
    {
      if((rc = create(scoreH,spH)) != kOkRC )
      {
        rc = cwLogError(rc,"Score test create failed.");
        goto errLabel;
      }
      
      if( score_report_fl )
        report(scoreH,nullptr);

      if( match_fl )
      {
        if((rc = sfmatch::test(sfmatchNode,scoreH)) != kOkRC )
        {
          rc = cwLogError(rc,"Score match test failed.");
          goto errLabel;
        }
      }

      if( track_fl )
      {
        if((rc = sftrack::test(sftrackNode,scoreH)) != kOkRC )
        {
          rc = cwLogError(rc,"Score track test failed.");
          goto errLabel;
        }
      }
    }
    
  }
  
 errLabel:
  destroy(scoreH);
  destroy(spH);
  destroy(dynRefH);
  return rc;

  
}
