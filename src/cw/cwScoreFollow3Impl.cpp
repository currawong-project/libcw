#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwTime.h"
#include "cwFile.h"
#include "cwCsv.h"
#include "cwVectOps.h"
#include "cwScoreFollow3.h"
#include "cwScoreFollow3Impl.h"

namespace cw
{
  namespace score_follow_3
  {
    
  }
}

cw::rc_t cw::score_follow_3::_create_impl( sf_t* p, const args_t& args )
{
  rc_t rc = kOkRC;
  return rc;
}

cw::rc_t cw::score_follow_3::_destroy_impl( sf_t* p )
{
  // Note that this call may occur without a prior call to _create_impl().
  rc_t rc = kOkRC;
  return rc;
}
  
cw::rc_t cw::score_follow_3::_reset_impl( sf_t* p )
{
  rc_t rc = kOkRC;
  return rc;
}

cw::rc_t cw::score_follow_3::_on_new_note_impl( sf_t* p, unsigned uid, double perf_sec, unsigned pitch, unsigned vel, result_t& result_ref )
{
  rc_t rc = kOkRC;
  return rc;
}

cw::rc_t cw::score_follow_3::_exec_impl( sf_t* p, double perf_sec )
{
  rc_t rc = kOkRC;
  return rc;
}

bool cw::score_follow_3::_is_done_impl( sf_t* p )
{
  return false;
}
