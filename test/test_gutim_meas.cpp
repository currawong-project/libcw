#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwObject.h"

#include "cwGutimMeas.h"

using namespace cw;


TEST( GutimMeas, CreateTest )
{
  rc_t                 rc    = kOkRC;
  gutim_meas::handle_t h;
  const char*          fname = RSRC_DIR "/gutim_meas_group_info.json";

  if((rc = create(h,fname)) != kOkRC )
  {
    FAIL() << "Create failed.";
  }

  if((rc = destroy(h)) != kOkRC )
  {
    FAIL() << "Destroy failed.";  
  }

  EXPECT_EQ(rc,kOkRC);
}

TEST( GutimMeas, TreeValidate )
{
  rc_t        rc    = kOkRC;
  object_t*   tree  = nullptr;
  const char* fname = RSRC_DIR "/gutim_meas_group_info.json";
  
  if((rc = objectFromFile(fname,tree)) != kOkRC )
  {
    FAIL() << "Parse failed.";
  }

  if((rc = objectToFile("/home/kevin/temp/temp.cfg",tree)) != kOkRC )
  {
    FAIL() << "Dump failed.";  
  }

  EXPECT_EQ(rc,kOkRC);
}
