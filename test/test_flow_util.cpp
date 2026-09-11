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

using namespace cw;


const char* type_json = R"(
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

const char* data_json = R"(
  [ { "field_a":1, "field_b":-2, "field_c":3.0, "field_d":3.5, sel_fld:0 },
    { "field_a":2, "field_b":-3, "field_c":4.0, "field_d":3.6, sel_fld:1 },
    { "field_a":3, "field_b":-4, "field_c":5.0, "field_d":3.7, sel_fld:0 },
    { "field_a":4, "field_b":-5, "field_c":6.0, "field_d":3.8, sel_fld:1 }
  ])";


rc_t create_recd_array( const char*          type_json,
                        const char*          data_json,
                        unsigned             allocRecdN,
                        flow::recd_type_t*&  recd_type_ref,
                        flow::recd_array_t*& recd_array_ref,
                        const char*&         err_msg_ref )
{
  rc_t      rc       = kOkRC;
  object_t* type_cfg = nullptr;
  object_t* data_cfg = nullptr;
  
  recd_type_ref  = nullptr;
  recd_array_ref = nullptr;
  
  if((rc = objectFromString(type_json,type_cfg)) != kOkRC )
  {
    err_msg_ref =  "Type cfg. parse failed.";
    goto errLabel;
  }
  
  if((rc = flow::recd_type_create(  recd_type_ref, nullptr, type_cfg )) != kOkRC )
  {
    err_msg_ref = "Type create failed.";
    goto errLabel;
  }

  if( data_json != nullptr )
  {
    if((rc = objectFromString( data_json, data_cfg )) != kOkRC )
    {
      err_msg_ref = "Data parse failed.";
      goto errLabel;
    }
  }
  
  if((rc = flow::recd_array_create( recd_array_ref, recd_type_ref, allocRecdN, data_cfg )) != kOkRC )
  {
    err_msg_ref = "Data array create failed.";
    goto errLabel;
  }

errLabel:
  if(type_cfg != nullptr )
    type_cfg->free();
  
  if(data_cfg != nullptr )
    data_cfg->free();
  
  return rc;
}


TEST( FlowUtilTest, RecdArray )
{
  rc_t                rc         = kOkRC;
  flow::recd_type_t*  type       = nullptr;
  flow::recd_array_t* recd_array = nullptr;
  unsigned            allocRecdN = 8;
  const char*         err_msg    = "";
  unsigned            a_fld_idx      = kInvalidIdx;
  unsigned            b_fld_idx      = kInvalidIdx;
  unsigned            c_fld_idx      = kInvalidIdx;
  unsigned            d_fld_idx      = kInvalidIdx;
  
  if((rc = create_recd_array( type_json, data_json,allocRecdN,type,recd_array,err_msg)) != kOkRC )
  {
    FAIL() << err_msg;
    goto errLabel;
  }

  if((rc = recd_type_to_field_index( type, 
                                     "field_a", a_fld_idx,
                                     "field_b", b_fld_idx,
                                     "field_c", c_fld_idx,
                                     "field_d", d_fld_idx )) != kOkRC )
  {
    FAIL() << "Record type to field index failed.";
    goto errLabel;
  }
  
  //recd_array_print( recd_array );

  for(unsigned i=0; i<recd_array->recdN; ++i)
  {
    unsigned f_a;
    int      f_b;
    float    f_c;
    double   f_d;

    if((rc = recd_get(recd_array->recdA +i,
                      a_fld_idx, f_a,
                      b_fld_idx, f_b,
                      c_fld_idx, f_c,
                      d_fld_idx, f_d)) != kOkRC )
    {
      FAIL() << "Record access at index " << i << " failed.";
    }

    switch(i)
    {
      case 0:
        EXPECT_EQ(f_a,1);
        EXPECT_EQ(f_b,-2);
        EXPECT_EQ(f_c,3.0);
        EXPECT_EQ(f_d,3.5);
        break;
        
      case 1:
        EXPECT_EQ(f_a,2);
        EXPECT_EQ(f_b,-3);
        EXPECT_EQ(f_c,4.0);
        EXPECT_EQ(f_d,3.6);
        break;
        
      case 2:
        EXPECT_EQ(f_a,3);
        EXPECT_EQ(f_b,-4);
        EXPECT_EQ(f_c,5.0);
        EXPECT_EQ(f_d,3.7);
        break;
        
      case 3:
        EXPECT_EQ(f_a,4);
        EXPECT_EQ(f_b,-5);
        EXPECT_EQ(f_c,6.0);
        EXPECT_EQ(f_d,3.8);
        break;
    }
    
  }
  
  recd_array_destroy(recd_array);
  recd_type_destroy(type);
    
errLabel:
  EXPECT_EQ(rc,kOkRC);
}

TEST( FlowUtilTest, RecdArrayConcat )
{
  rc_t                rc         = kOkRC;
  
  flow::recd_type_t*  src0_type       = nullptr;
  flow::recd_array_t* src0_recd_array = nullptr;

  flow::recd_type_t*  src1_type       = nullptr;
  flow::recd_array_t* src1_recd_array = nullptr;

  flow::recd_type_t*  dst_type       = nullptr;
  flow::recd_array_t* dst_recd_array = nullptr;
  
  unsigned            allocRecdN = 8;
  const char*         err_msg    = "";
  unsigned            a_fld_idx      = kInvalidIdx;
  unsigned            b_fld_idx      = kInvalidIdx;
  unsigned            c_fld_idx      = kInvalidIdx;
  unsigned            d_fld_idx      = kInvalidIdx;
  
  if((rc = create_recd_array( type_json, data_json, allocRecdN, src0_type, src0_recd_array,err_msg)) != kOkRC )
  {
    FAIL() << err_msg;
    goto errLabel;
  }

  if((rc = create_recd_array( type_json, data_json, allocRecdN, src1_type, src1_recd_array,err_msg)) != kOkRC )
  {
    FAIL() << err_msg;
    goto errLabel;
  }

  if((rc = flow::recd_type_create(  dst_type, src0_type, nullptr )) != kOkRC )
  {
    FAIL() << "Dest. type create failed.";
    goto errLabel;
  }
  
  if((rc = flow::recd_array_create( dst_recd_array, dst_type, allocRecdN, nullptr )) != kOkRC )
  {
    FAIL() << "Dest. data array create failed.";
    goto errLabel;
  }
  
  if((rc = recd_array_concat( dst_recd_array, src0_recd_array->recdA, src0_recd_array->recdN )) != kOkRC )
  {
    FAIL() << "concat 1 failed.";
    goto errLabel;
  }

  if((rc = recd_array_concat( dst_recd_array, src1_recd_array->recdA, src1_recd_array->recdN )) != kOkRC )
  {
    FAIL() << "concat 2 failed.";
    goto errLabel;
  }

  if((rc = recd_type_to_field_index( dst_type, 
                                     "field_a", a_fld_idx,
                                     "field_b", b_fld_idx,
                                     "field_c", c_fld_idx,
                                     "field_d", d_fld_idx )) != kOkRC )
  {
    FAIL() << "Record type to field index failed.";
    goto errLabel;
  }

  //recd_type_print( src0_recd_array->type );
  //recd_type_print( src1_recd_array->type );
  //recd_type_print( dst_recd_array->type );

  //recd_array_print( src0_recd_array );
  //recd_array_print( src1_recd_array );
  //recd_array_print( dst_recd_array );

  for(unsigned i=0; i<dst_recd_array->recdN; ++i)
  {
    unsigned f_a;
    int      f_b;
    float    f_c;
    double   f_d;

    if((rc = recd_get(dst_recd_array->recdA +i,
                      a_fld_idx, f_a,
                      b_fld_idx, f_b,
                      c_fld_idx, f_c,
                      d_fld_idx, f_d)) != kOkRC )
    {
      FAIL() << "Record access at index " << i << " failed.";
    }

    switch(i)
    {
      case 0:
      case 4:
        EXPECT_EQ(f_a,1);
        EXPECT_EQ(f_b,-2);
        EXPECT_EQ(f_c,3.0);
        EXPECT_EQ(f_d,3.5);
        break;
        
      case 1:
      case 5:
        EXPECT_EQ(f_a,2);
        EXPECT_EQ(f_b,-3);
        EXPECT_EQ(f_c,4.0);
        EXPECT_EQ(f_d,3.6);
        break;
        
      case 2:
      case 6:
        EXPECT_EQ(f_a,3);
        EXPECT_EQ(f_b,-4);
        EXPECT_EQ(f_c,5.0);
        EXPECT_EQ(f_d,3.7);
        break;

      case 3:
      case 7:
        EXPECT_EQ(f_a,4);
        EXPECT_EQ(f_b,-5);
        EXPECT_EQ(f_c,6.0);
        EXPECT_EQ(f_d,3.8);
        break;
        
      default:
        break;
    }  
  }
    
  recd_array_destroy(src0_recd_array);
  recd_type_destroy(src0_type);
  
  recd_array_destroy(src1_recd_array);
  recd_type_destroy(src1_type);
  
  recd_array_destroy(dst_recd_array);
  recd_type_destroy(dst_type);
  
errLabel:
  EXPECT_EQ(rc,kOkRC);
}



TEST( FlowUtilTest, RecdArraySplit )
{
  rc_t                rc              = kOkRC;
  
  flow::recd_type_t*  dst0_type       = nullptr;
  flow::recd_array_t* dst0_recd_array = nullptr;

  flow::recd_type_t*  dst1_type       = nullptr;
  flow::recd_array_t* dst1_recd_array = nullptr;

  flow::recd_type_t*  src_type        = nullptr;
  flow::recd_array_t* src_recd_array  = nullptr;

  flow::recd_array_t* dst_recd_arrayA[] = { nullptr, nullptr };
  unsigned            dst_recd_arrayN   = 2;

  unsigned            allocRecdN = 8;
  const char*         err_msg    = "";
  unsigned            a_fld_idx      = kInvalidIdx;
  unsigned            b_fld_idx      = kInvalidIdx;
  unsigned            c_fld_idx      = kInvalidIdx;
  unsigned            d_fld_idx      = kInvalidIdx;
  unsigned            sel_fld_idx    = kInvalidIdx;
  
  if((rc = create_recd_array( type_json, data_json,allocRecdN,src_type,src_recd_array,err_msg)) != kOkRC )
  {
    FAIL() << err_msg;
    goto errLabel;
  }

  if((rc = flow::recd_type_create(  dst0_type, src_type, nullptr )) != kOkRC )
  {
    FAIL() << "Dest. type create failed.";
    goto errLabel;
  }
  
  if((rc = flow::recd_array_create( dst0_recd_array, dst0_type, allocRecdN, nullptr )) != kOkRC )
  {
    FAIL() << "Dest. data array create failed.";
    goto errLabel;
  }

  if((rc = flow::recd_type_create(  dst1_type, src_type, nullptr )) != kOkRC )
  {
    FAIL() << "Dest. type create failed.";
    goto errLabel;
  }
  
  if((rc = flow::recd_array_create( dst1_recd_array, dst1_type, allocRecdN, nullptr )) != kOkRC )
  {
    FAIL() << "Dest. data array create failed.";
    goto errLabel;
  }

  if((rc = recd_type_to_field_index( src_type, 
                                     "field_a", a_fld_idx,
                                     "field_b", b_fld_idx,
                                     "field_c", c_fld_idx,
                                     "field_d", d_fld_idx,
                                     "sel_fld", sel_fld_idx)) != kOkRC )
  {
    FAIL() << "Record type to field index failed.";
  }


  dst_recd_arrayA[0] = dst0_recd_array;
  dst_recd_arrayA[1] = dst1_recd_array;
  
  if((rc = flow::recd_array_split( src_recd_array->recdA, src_recd_array->recdN, sel_fld_idx, dst_recd_arrayA, dst_recd_arrayN )) != kOkRC )
  {
    FAIL() << "recd_array_split() failed."; 
  }

  //recd_array_print( src_recd_array );
  //recd_array_print( dst0_recd_array );
  //recd_array_print( dst1_recd_array );
  
  for(unsigned j=0; j<dst_recd_arrayN; ++j)
  {
    for(unsigned i=0; i<dst_recd_arrayA[j]->recdN; ++i)
    {
      unsigned f_a;
      int      f_b;
      float    f_c;
      double   f_d;
      unsigned f_s;

      if((rc = recd_get(dst_recd_arrayA[j]->recdA +i,
                        a_fld_idx, f_a,
                        b_fld_idx, f_b,
                        c_fld_idx, f_c,
                        d_fld_idx, f_d,
                        sel_fld_idx, f_s)) != kOkRC )
      {
        FAIL() << "Record access at index " << i << " failed.";
      }

      switch(i)
      {
        case 0:
          if( f_s == 0 )
          {
            EXPECT_EQ(f_a,1);
            EXPECT_EQ(f_b,-2);
            EXPECT_EQ(f_c,3.0);
            EXPECT_EQ(f_d,3.5);
          }
          else
          {
            EXPECT_EQ(f_a,2);
            EXPECT_EQ(f_b,-3);
            EXPECT_EQ(f_c,4.0);
            EXPECT_EQ(f_d,3.6);
          }
          break;
        
        case 1:
          if( f_s == 0 )
          {
            EXPECT_EQ(f_a,3);
            EXPECT_EQ(f_b,-4);
            EXPECT_EQ(f_c,5.0);
            EXPECT_EQ(f_d,3.7);
          }
          else
          {
            EXPECT_EQ(f_a,4);
            EXPECT_EQ(f_b,-5);
            EXPECT_EQ(f_c,6.0);
            EXPECT_EQ(f_d,3.8);
          }
          break;
        
        default:
          break;
      }  
    }
  }
  
errLabel:

  recd_array_destroy(dst0_recd_array);
  recd_type_destroy(dst0_type);
  
  recd_array_destroy(dst1_recd_array);
  recd_type_destroy(dst1_type);
  
  recd_array_destroy(src_recd_array);
  recd_type_destroy(src_type);
  
  EXPECT_EQ(rc,kOkRC);
}

TEST( FlowUtilTest, RecdArrayReformat )
{
  rc_t                rc         = kOkRC;
  flow::recd_type_t*  type       = nullptr;
  object_t*           map_cfg    = nullptr;
  flow::recd_array_t* recd_array = nullptr;
  unsigned            allocRecdN = 8;
  const char*         err_msg    = "";
  unsigned            a_fld_idx  = kInvalidIdx;
  unsigned            b_fld_idx  = kInvalidIdx;
  unsigned            c_fld_idx  = kInvalidIdx;
  unsigned            d_fld_idx  = kInvalidIdx;
  unsigned            sel_fld_idx= kInvalidIdx;
  unsigned            f_a;
  int                 f_b;
  float               f_c;
  double              f_d;
  unsigned            f_s;

  const char* map_json = R"([ "sel_fld","field_d","field_c","field_b","field_a" ])";
  
  if((rc = create_recd_array( type_json, data_json, allocRecdN,type,recd_array,err_msg)) != kOkRC )
  {
    FAIL() << err_msg;
    goto errLabel;
  }

  if((rc = objectFromString(map_json,map_cfg)) != kOkRC )
  {
    FAIL() << "Field map parse failed.";
  }

  if((rc = recd_type_apply_map( type, map_cfg )) != kOkRC )
  {
    FAIL() << "Apply field map failed.";
    goto errLabel;
  }

  if((rc = recd_type_to_field_index( type, 
                                     "field_a", a_fld_idx,
                                     "field_b", b_fld_idx,
                                     "field_c", c_fld_idx,
                                     "field_d", d_fld_idx,
                                     "sel_fld", sel_fld_idx)) != kOkRC )
  {
    FAIL() << "Record type to field index failed.";
  }

  //recd_array_print( recd_array );

  
  for(unsigned i=0; i<recd_array->recdN; ++i)
  {
    if((rc = recd_get(recd_array->recdA + i,
                      a_fld_idx, f_a,
                      b_fld_idx, f_b,
                      c_fld_idx, f_c,
                      d_fld_idx, f_d,
                      sel_fld_idx, f_s)) != kOkRC )
    {
      FAIL() << "Record access at index " << i << " failed.";
    }

    switch(i)
    {
      case 0:
        EXPECT_EQ(f_a,1);
        EXPECT_EQ(f_b,-2);
        EXPECT_EQ(f_c,3.0);
        EXPECT_EQ(f_d,3.5);
        EXPECT_EQ(f_s,0);
        break;

      case 1:
        EXPECT_EQ(f_a,2);
        EXPECT_EQ(f_b,-3);
        EXPECT_EQ(f_c,4.0);
        EXPECT_EQ(f_d,3.6);
        EXPECT_EQ(f_s,1);
        break;
        
      case 2:
        EXPECT_EQ(f_a,3);
        EXPECT_EQ(f_b,-4);
        EXPECT_EQ(f_c,5.0);
        EXPECT_EQ(f_d,3.7);
        EXPECT_EQ(f_s,0);
        break;
        
      case 3:
        EXPECT_EQ(f_a,4);
        EXPECT_EQ(f_b,-5);
        EXPECT_EQ(f_c,6.0);
        EXPECT_EQ(f_d,3.8);
        EXPECT_EQ(f_s,1);
        break;
    }
  }
  
  recd_array_destroy( recd_array );
  recd_type_destroy( type );
  map_cfg->free();

errLabel:
  EXPECT_EQ(rc,kOkRC);
}

TEST( FlowUtilTest, RecdArrayReformatRename )
{
  rc_t                rc            = kOkRC;
  flow::recd_type_t*  type0         = nullptr;
  object_t*           type0_cfg     = nullptr;
  
  flow::recd_type_t*  type1         = nullptr;
  
  object_t*           field_map_cfg = nullptr;
  
  unsigned            allocRecdN    = 10;
  
  flow::recd_array_t* recd_array0   = nullptr;
  flow::recd_array_t* recd_array1   = nullptr;
  
  const char* err_msg = nullptr;

  unsigned a_fld_idx = kInvalidIdx;
  unsigned e_fld_idx = kInvalidIdx;
  unsigned c_fld_idx = kInvalidIdx;
  unsigned f_a       = 0;
  double   f_e       = 0.0;
  float    f_c       = 0;
  

  // Rename 'field_d' as 'field_e', drop field_b and field_c.
  // and order the record as: 'field_a','field_e', 'field_c'.
  const char* field_map_json = "{ field_a:field_a, field_d:field_e, field_c:field_c }";

  // create the base type and array.
  if((rc = create_recd_array( type_json, data_json, allocRecdN, type0, recd_array0, err_msg)) != kOkRC )
  {
    FAIL() << err_msg;
    goto errLabel;
  }

  // parse the field mapping json 
  if((rc = objectFromString(field_map_json,field_map_cfg)) != kOkRC )
  {
    FAIL() << "map json parse failed.";
    goto errLabel;
  }

  // create the mapped type
  if((rc = recd_type_create_from_map( type1, type0, field_map_cfg )) != kOkRC )
  {
    FAIL() << "type1 create failed.";
    goto errLabel;
  }

  //recd_type_print(type0);
  //printf("\n");
  //recd_type_print(type1);

  // create an empty array to receive the remapped records
  if((rc = recd_array_create( recd_array1, type1, recd_array0->allocRecdN, nullptr )) != kOkRC )
  {
    FAIL() << "recd_array1 create failed.";
    goto errLabel;
  }

  // fill the dest. array with the remapped records
  if((rc = flow::recd_array_remap( recd_array0->recdA, recd_array0->recdN, recd_array1 )) !=  kOkRC )
  {
    FAIL() << "recd_array remap failed.";
    goto errLabel;
  }
  
  //recd_array_print( recd_array0 );
  //recd_array_print( recd_array1 );

  // get the field indexes of the remapped records
  if((rc = recd_type_to_field_index( recd_array1->type, 
                                     "field_a", a_fld_idx,
                                     "field_e", e_fld_idx,
                                     "field_c", c_fld_idx)) != kOkRC )
  {
    FAIL() << "Field index access failed.";
    goto errLabel;
  }

  // validate the remapped values
  for(unsigned i=0; i<recd_array1->recdN; ++i)
  {
    if((rc = recd_get(recd_array1->recdA + i,
                      a_fld_idx, f_a,
                      e_fld_idx, f_e,
                      c_fld_idx, f_c)) != kOkRC )
    {
      FAIL() << "Record access at index " << i << " failed.";
    }

    switch(i)
    {
      case 0:
        EXPECT_EQ(f_a,1);
        EXPECT_EQ(f_e,3.5);
        EXPECT_EQ(f_c,3.0);
        break;
        
      case 1:
        EXPECT_EQ(f_a,2);
        EXPECT_EQ(f_e,3.6);
        EXPECT_EQ(f_c,4.0);
        break;
        
      case 2:
        EXPECT_EQ(f_a,3);
        EXPECT_EQ(f_e,3.7);
        EXPECT_EQ(f_c,5.0);
        break;

      case 3:
        EXPECT_EQ(f_a,4);
        EXPECT_EQ(f_e,3.8);
        EXPECT_EQ(f_c,6.0);
        break;        
    }    
  }
  
errLabel:
  recd_type_destroy(type0);
  recd_type_destroy(type1);
  recd_array_destroy(recd_array0);
  recd_array_destroy(recd_array1);

  field_map_cfg->free();
  
  EXPECT_EQ( rc, kOkRC );
}
