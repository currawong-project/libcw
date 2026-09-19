#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwIdTable.h"

#include "cwObject.h"
#include "cwAudioFile.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwTracer.h"

#include "cwDspTypes.h" // srate_t, sample_t, coeff_t, ...

#include "cwFlowDecl.h"
#include "cwFlow.h"
#include "cwFlowValue.h"
#include "cwFlowRecd.h"

using namespace cw;
using namespace cw::flow;

const char* type_0_json = R"(
   {
     alloc_cnt:4,
     fields: {
         field_a: { type:uint,   value:10,   doc:"Field A." },
         field_b: { type:int,    value:-20,  doc:"Field B." },
         field_c: { type:float,  value:30.0, doc:"Field C." },
         field_d: { type:double, value:40.0, doc:"Field D." },
         sel_fld: { type:uint,   value:0,    doc:"Select field." }
       }
   })";

const char* data_0_json = R"(
  [ { "field_a":1, "field_b":-2, "field_c":3.0, "field_d":3.5, sel_fld:0 },
    { "field_a":2, "field_b":-3, "field_c":4.0, "field_d":3.6, sel_fld:1 },
    { "field_a":3, "field_b":-4, "field_c":5.0, "field_d":3.7, sel_fld:0 },
    { "field_a":4, "field_b":-5, "field_c":6.0, "field_d":3.8, sel_fld:1 }
  ])";

struct ab_t {
  unsigned a;
  int b;
};

ab_t abA[] = { {1,-2}, {2,-3}, {3,-4}, {4,-5}, {10,-20}, {20,-30}, {30,-40}, {40,-50} };


const char* type_1_json = R"(
   {
     alloc_cnt:4,
     fields: {
         field_aa: { type:uint,   value:100,   doc:"Field A." },
         field_bb: { type:int,    value:-200,  doc:"Field B." },
         field_cc: { type:float,  value:300.0, doc:"Field C." },
         field_dd: { type:double, value:400.0, doc:"Field D." },
       }
   })";


const char* type_2_json = R"(
   {
     alloc_cnt:4,
     fields: {
         field_a: { type:uint,   value:10,   doc:"Field A." },
         field_b: { type:int,    value:-20,  doc:"Field B." },
         field_x: { type:float,  value:30.0, doc:"Field C." },
         field_y: { type:double, value:40.0, doc:"Field D." },
         sel_fld: { type:uint,   value:0,    doc:"Select field." }
       }
   })";

const char* data_2_json = R"(
  [ { "field_a":10, "field_b":-20, "field_x":3.0, "field_y":3.5, sel_fld:0 },
    { "field_a":20, "field_b":-30, "field_x":4.0, "field_y":3.6, sel_fld:1 },
    { "field_a":30, "field_b":-40, "field_x":5.0, "field_y":3.7, sel_fld:0 },
    { "field_a":40, "field_b":-50, "field_x":6.0, "field_y":3.8, sel_fld:1 }
  ])";

const char* type_3_json = R"(
  {
     alloc_cnt:10,
     fields: { field_a:field_x, field_b:field_y }
  }
)";

typedef struct
{
  unsigned a;
  int b;
  float c;
  double d;
} abc_data_t;

abc_data_t abc_dataA[] = {
  { 10,-20,30.0f,30.5 },
  { 20,-30,40.0f,30.6 },
  { 30,-40,50.0f,30.7 },
  { 40,-50,60.0f,30.8 },
};

const char* data_1_json = R"(
  [ { "field_aa":10, "field_bb":-20, "field_cc":30.0, "field_dd":30.5 },
    { "field_aa":20, "field_bb":-30, "field_cc":40.0, "field_dd":30.6 },
    { "field_aa":30, "field_bb":-40, "field_cc":50.0, "field_dd":30.7 },
    { "field_aa":40, "field_bb":-50, "field_cc":60.0, "field_dd":30.8 }
  ])";


rc_t test_recd_array_create( const char* type_json,
                             const char* data_json,
                             unsigned allocRecdN,
                             object_t*& type_cfg_ref,
                             object_t*& data_cfg_ref,
                             recd_array_t*& recd_array_ref,
                             const char*& err_msg_ref )
{
  rc_t rc = kOkRC;
  type_cfg_ref = nullptr;
  data_cfg_ref = nullptr;
  recd_array_ref = nullptr;

  if((rc = objectFromString(type_json,type_cfg_ref)) != kOkRC )
  {
    err_msg_ref =  "Type cfg. parse failed.";
    goto errLabel;
  }

  if((rc = recd_array_create(recd_array_ref,type_cfg_ref,nullptr,0,allocRecdN)) != kOkRC )
  {
    err_msg_ref =  "recd_array create failed.";
    goto errLabel;
  }

  if((rc = objectFromString(data_json,data_cfg_ref)) != kOkRC )
  {
    err_msg_ref =  "Type cfg. parse failed.";
    goto errLabel;
  }

  if((rc = recd_array_append_from_cfg( recd_array_ref, data_cfg_ref )) != kOkRC )
  {
    err_msg_ref =  "Type cfg. parse failed.";
    goto errLabel;
  }
  
errLabel:
  return rc;
}


TEST( RecdTest, RecdTypeCreate )
{
  rc_t rc = kOkRC;
  recd_type_t* rt = nullptr;;
  object_t* type_cfg = nullptr;

  id_table::create_global();
  recd_global_registry_create();

  if((rc = objectFromString(type_0_json,type_cfg)) != kOkRC )
  {
    FAIL() <<  "Type cfg. parse failed.";
    goto errLabel;
  }
  
  if((rc = flow::recd_type_create(  rt, nullptr, type_cfg )) != kOkRC )
  {
    FAIL() << "Type create failed.";
    goto errLabel;
  }

  recd_type_print(rt);
  
  EXPECT_EQ(0,0);

errLabel:
  recd_type_destroy(rt);  
  
  if( type_cfg != nullptr )
    type_cfg->free();
  
  recd_global_registry_destroy();
  id_table::destroy_global();
}

TEST( RecdTest, RecdArrayCreate )
{
  rc_t          rc         = kOkRC;
  recd_array_t* recd_array = nullptr;
  object_t*     type_cfg   = nullptr;
  object_t*     data_cfg   = nullptr;
  unsigned      allocRecdN = 10;
  const char*   err_msg    = "";
  unsigned      afi        = kInvalidIdx;
  unsigned      bfi        = kInvalidIdx;
  unsigned      cfi        = kInvalidIdx;
  unsigned      dfi        = kInvalidIdx;
  unsigned      selfi      = kInvalidIdx;
  
  id_table::create_global();
  recd_global_registry_create();
  
  if((rc = test_recd_array_create(type_0_json, data_0_json, allocRecdN, type_cfg, data_cfg, recd_array, err_msg )) != kOkRC )
  {
    FAIL() << err_msg;
    goto errLabel;
  }
  
  if((rc = recd_array_print(recd_array)) != kOkRC )
  {
    FAIL() <<  "recd_array print failed.";
    goto errLabel;    
  }

  if((rc = recd_array_field_index(recd_array,
                                  "field_a",afi,
                                  "field_b",bfi,
                                  "field_c",cfi,
                                  "field_d",dfi,
                                  "sel_fld",selfi)) != kOkRC )
  {
    FAIL() <<  "recd_array_field_index() failed.";
    goto errLabel;        
  }

  for(unsigned i=0; i<recd_array_count(recd_array); ++i)
  {
    unsigned a = 0;
    int b = 0;
    float c = 0;
    double d = 0;
    unsigned sel=-1;

    if((rc = recd_get(recd_array->recdA + i, afi, a, bfi, b, cfi, c, dfi, d, selfi, sel )) != kOkRC )
    {
      FAIL() << "recd_get() failed on record index:" << i;
      goto errLabel;
    }

    switch(i)
    {
      case 0:
        EXPECT_EQ(a,1);
        EXPECT_EQ(b,-2);
        EXPECT_EQ(c,3.0);
        EXPECT_EQ(d,3.5);
        break;
        
      case 1:
        EXPECT_EQ(a,2);
        EXPECT_EQ(b,-3);
        EXPECT_EQ(c,4.0);
        EXPECT_EQ(d,3.6);
        break;
        
      case 2:
        EXPECT_EQ(a,3);
        EXPECT_EQ(b,-4);
        EXPECT_EQ(c,5.0);
        EXPECT_EQ(d,3.7);
        break;
        
      case 3:
        EXPECT_EQ(a,4);
        EXPECT_EQ(b,-5);
        EXPECT_EQ(c,6.0);
        EXPECT_EQ(d,3.8);
        break;
    }
    
  }
  
errLabel:
  recd_array_destroy(recd_array);
  
  if( type_cfg != nullptr )
    type_cfg->free();
  
  if( data_cfg != nullptr )
    data_cfg->free();

  recd_global_registry_destroy();
  id_table::destroy_global();
  
  EXPECT_EQ(rc,kOkRC);
  
}

TEST( RecdTest, RecdArrayInherit )
{
  rc_t          rc           = kOkRC;
  recd_array_t* recd_array_0 = nullptr;
  recd_array_t* recd_array_1 = nullptr;
  object_t*     type_0_cfg   = nullptr;
  object_t*     data_0_cfg   = nullptr;
  object_t*     type_1_cfg   = nullptr;
  unsigned      allocRecdN   = 10;
  const char*   err_msg      = "";
  unsigned      afi          = kInvalidIdx;
  unsigned      bfi          = kInvalidIdx;
  unsigned      cfi          = kInvalidIdx;
  unsigned      dfi          = kInvalidIdx;

  id_table::create_global();
  recd_global_registry_create();
  
  if((rc = test_recd_array_create(type_0_json, data_0_json, allocRecdN, type_0_cfg, data_0_cfg, recd_array_0, err_msg )) != kOkRC )
  {
    FAIL() << err_msg;
    goto errLabel;
  }

  if((rc = objectFromString(type_1_json,type_1_cfg)) != kOkRC )
  {
    FAIL() <<  "Type cfg. parse failed.";
    goto errLabel;
  }
  
  if((rc = recd_array_create(recd_array_1,type_1_cfg,recd_array_0->typeA,recd_array_0->typeN,allocRecdN)) != kOkRC )
  {
    FAIL() << "Second record array create failed.";
    goto errLabel;
  }
  
  if((rc = recd_array_field_index(recd_array_1,
                                  "field_aa",afi,
                                  "field_bb",bfi,
                                  "field_cc",cfi,
                                  "field_dd",dfi)) != kOkRC )
  {
    FAIL() <<  "recd_array_field_index() failed.";
    goto errLabel;        
  }

  for(unsigned i=0; i<recd_array_count(recd_array_0); ++i)
  {
    if((rc = recd_append(recd_array_1, recd_array_0->recdA + i,
                      afi, abc_dataA[i].a,
                      bfi, abc_dataA[i].b,
                      cfi, abc_dataA[i].c,
                      dfi, abc_dataA[i].d)) != kOkRC )
    {
      FAIL() << "recd_set() failed on record index: " << i;
    }
  }


  recd_array_print(recd_array_1);

errLabel:  

  recd_array_destroy(recd_array_1);
  recd_array_destroy(recd_array_0);

  if( type_0_cfg != nullptr )
    type_0_cfg->free();

  if( type_1_cfg != nullptr )
    type_1_cfg->free();
  
  if( data_0_cfg != nullptr )
    data_0_cfg->free();

  
  recd_global_registry_destroy();
  id_table::destroy_global();
  
  EXPECT_EQ(rc,kOkRC);
}

TEST( RecdTest, RecdArrayMerge )
{
  rc_t          rc           = kOkRC;
  recd_array_t* recd_array_0 = nullptr;
  recd_array_t* recd_array_1 = nullptr;
  recd_array_t* recd_array_2 = nullptr;  
  object_t*     type_0_cfg   = nullptr;
  object_t*     data_0_cfg   = nullptr;
  object_t*     type_1_cfg   = nullptr;
  object_t*     data_1_cfg   = nullptr;
  unsigned      allocRecdN   = 10;
  const char*   err_msg      = "";
  unsigned      afi          = kInvalidIdx;
  unsigned      bfi          = kInvalidIdx;
  const recd_type_t* base_typeA[] = { nullptr, nullptr };
  const unsigned base_typeN = std::size(base_typeA);
  
  id_table::create_global();
  recd_global_registry_create();

  // create input array 0
  if((rc = test_recd_array_create(type_0_json, data_0_json, allocRecdN, type_0_cfg, data_0_cfg, recd_array_0, err_msg )) != kOkRC )
  {
    FAIL() << err_msg;
    goto errLabel;
  }

  // create input array 1
  if((rc = test_recd_array_create(type_2_json, data_2_json, allocRecdN, type_1_cfg, data_1_cfg, recd_array_1, err_msg )) != kOkRC )
  {
    FAIL() << err_msg;
    goto errLabel;
  }
  else
  {
    assert( recd_array_0->typeN == 1 );
    assert( recd_array_1->typeN == 1 );
    
    base_typeA[0] = recd_array_0->typeA[0];
    base_typeA[1] = recd_array_1->typeA[0];

    // create the output array
    if((rc = recd_array_create(recd_array_2,nullptr, base_typeA, base_typeN, allocRecdN)) != kOkRC )
    {
      FAIL() << "Output record array create failed.";
      goto errLabel;
    }

    // get field indexes for two fields that are common to both input arrays
    if((rc = recd_array_field_index(recd_array_2,
                                    "field_a",afi,
                                    "field_b",bfi)) != kOkRC )
    {
      FAIL() <<  "recd_array_field_index() failed.";
      goto errLabel;              
    }
    

    // insert records into recd_array_2 from recd_array_0
    for(unsigned i=0; i<recd_array_count(recd_array_0); ++i)
    {
      if(( rc = recd_array_append_pass_through( recd_array_2, recd_array_0->recdA + i)) != kOkRC )
      {
        FAIL() << "Record pass-through append failed on recd_array_0";
        goto errLabel;
      }
    }

    // insert records into recd_array_2 from recd_array_1
    for(unsigned i=0; i<recd_array_count(recd_array_1); ++i)
    {
      if(( rc = recd_array_append_pass_through( recd_array_2, recd_array_1->recdA + i)) != kOkRC )
      {
        FAIL() << "Record pass-through append failed on recd_array_1";
        goto errLabel;
      }
    }

    // read and validate the output array
    for(unsigned i=0; i<recd_array_count(recd_array_2); ++i)
    {
      
      unsigned a;
      int b;
      if((rc = recd_get( recd_array_2->recdA + i, afi, a, bfi, b)) != kOkRC )
      {
        FAIL() << "recd_get failed on recd_array_2 at record index:" << i;
        goto errLabel;
      }

      EXPECT_EQ(abA[i].a,a);
      EXPECT_EQ(abA[i].b,b);
    }

    recd_array_print_info(recd_array_2);
    
  }

errLabel:
  recd_array_destroy(recd_array_2);
  recd_array_destroy(recd_array_1);
  recd_array_destroy(recd_array_0);

  if( type_0_cfg != nullptr )
    type_0_cfg->free();

  if( type_1_cfg != nullptr )
    type_1_cfg->free();
  
  if( data_0_cfg != nullptr )
    data_0_cfg->free();

  if( data_1_cfg != nullptr )
    data_1_cfg->free();
  
  recd_global_registry_destroy();
  id_table::destroy_global();
  
  EXPECT_EQ(rc,kOkRC);  
}

TEST( RecdTest, RecdArrayRename )
{
  rc_t          rc         = kOkRC;
  recd_array_t* recd_array_0 = nullptr;
  recd_array_t* recd_array_1 = nullptr;
  object_t*     type_0_cfg   = nullptr;
  object_t*     data_0_cfg   = nullptr;
  object_t*     type_1_cfg   = nullptr;
  unsigned      allocRecdN = 10;
  const char*   err_msg    = "";
  unsigned      xfi        = kInvalidIdx;
  unsigned      yfi        = kInvalidIdx;
  
  id_table::create_global();
  recd_global_registry_create();
  
  if((rc = test_recd_array_create(type_0_json, data_0_json, allocRecdN, type_0_cfg, data_0_cfg, recd_array_0, err_msg )) != kOkRC )
  {
    FAIL() << err_msg;
    goto errLabel;
  }

  if((rc = objectFromString(type_3_json,type_1_cfg)) != kOkRC )
  {
    FAIL() <<  "Alias type cfg. parse failed.";
    goto errLabel;
  }

  if((rc = recd_array_create( recd_array_1, type_1_cfg, recd_array_0->typeA, recd_array_0->typeN, allocRecdN )) != kOkRC )
  {
    FAIL() <<  "Aliasing recd. array create failed.";
    goto errLabel;    
  }

  recd_array_print_info(recd_array_1);
  
  // get field indexes for two fields that are common to both input arrays
  if((rc = recd_array_field_index(recd_array_1,
                                  "field_x",xfi,
                                  "field_y",yfi)) != kOkRC )
  {
    FAIL() <<  "recd_array_field_index() failed.";
    goto errLabel;              
  }
  
  for(unsigned i=0; i<recd_array_count(recd_array_0); ++i)
  {
    if((rc = recd_array_append_pass_through( recd_array_1, recd_array_0->recdA + i )) != kOkRC )
    {
      FAIL() <<  "Recd. array pass-through failed.";
      goto errLabel;        
    }
  }

  for(unsigned i=0; i<recd_array_count(recd_array_1); ++i)
  {
    int x;
    unsigned y;

    if((rc = recd_get( recd_array_1->recdA + i, xfi, x, yfi, y)) != kOkRC )
    {
      FAIL() << "Record get failed on index: "<< i;
      goto errLabel;
    }

    EXPECT_EQ(abA[i].a,x);
    EXPECT_EQ(abA[i].b,y);

  }
  



  recd_array_print(recd_array_1);
  
errLabel:
  recd_array_destroy(recd_array_1);
  recd_array_destroy(recd_array_0);

  if( type_0_cfg != nullptr )
    type_0_cfg->free();

  if( type_1_cfg != nullptr )
    type_1_cfg->free();
  
  if( data_0_cfg != nullptr )
    data_0_cfg->free();

  
  recd_global_registry_destroy();
  id_table::destroy_global();
  
  EXPECT_EQ(rc,kOkRC);  
}  

