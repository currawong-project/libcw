
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwMath.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwDspTypes.h" // real_t, sample_t
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwIdTable.h"
#include "cwFlowValue.h"
#include "cwFlowRecd.h"


namespace cw
{
  namespace flow
  {
    typedef struct recd_reg_node_str
    {
      const recd_type_t*        recd_type;
      struct recd_reg_node_str* link;
    } recd_reg_node_t;

    typedef struct recd_registry_str
    {
      bool             is_initialized_fl;
      recd_reg_node_t* nodeL;
      unsigned         next_id;
    } recd_registry_t;
    

    recd_registry_t __global_recd_reg__{};

    bool _is_recd_field_desc_physically_equivalent( const recd_field_desc_t* f0, const recd_field_desc_t* f1 )
    { return f0->label_id == f1->label_id && f0->val_idx==f1->val_idx && f0->dflt_value.tflag == f1->dflt_value.tflag; }

    bool _is_recd_type_physically_equivalent( const recd_type_t* rt0, const recd_type_t* rt1 )
    {
      if( rt0 == nullptr && rt1 == nullptr )
        return true;

      if( rt0 == nullptr || rt1 == nullptr )
        return false;

      if( rt0->fieldDescN != rt1->fieldDescN )
        return false;
      
      for(unsigned i=0; i<rt0->fieldDescN; ++i)
        if( !_is_recd_field_desc_physically_equivalent( rt0->fieldDescA+i,rt1->fieldDescA+i) )
          return false;

      return _is_recd_type_physically_equivalent(rt0->base_type,rt1->base_type);
    }

    // Physical equivalence is defined as matching inheritance chains through the base pointer links 
    // and matching field descriptions at all levels.  It guarantees that the physical layout of the
    // associatd records is identical.
    void _recd_register_type( recd_type_t* recd_type )
    {
      recd_reg_node_t* node = __global_recd_reg__.nodeL;

      assert(   __global_recd_reg__.is_initialized_fl );
      
      recd_type->class_id = kInvalidId;
      
      // compare the type to register with each of the existing types
      for(; node != nullptr; node=node->link)
      {
        // if the new type is physically equivalent to an existing type
        if( _is_recd_type_physically_equivalent(node->recd_type,recd_type) )
        {
          // then their class_id's are the same
          recd_type->class_id = node->recd_type->class_id;
          break;
        }
      }

      // if an equivalent type was not found
      if( recd_type->class_id == kInvalidIdx )
      {
        // assign an unused id to the new type
        recd_type->class_id = __global_recd_reg__.next_id++;
        recd_reg_node_t* n = mem::allocZ<recd_reg_node_t>();
        n->recd_type = recd_type;
        n->link = __global_recd_reg__.nodeL;
        __global_recd_reg__.nodeL = n;
      }
    }


    const recd_field_desc_t* _field_label_to_desc( const recd_type_t* rt, const char* label )
    {
      for(unsigned i=0; i<rt->fieldDescN; ++i)
        if( textIsEqual(rt->fieldDescA[i].label,label))
          return rt->fieldDescA + i;
      
      return nullptr;
    }
    const recd_field_desc_t* _field_find_equivalent( const recd_type_t* rt, unsigned label_id, unsigned t_flag )
    {
      for(unsigned i=0; i<rt->fieldDescN; ++i)
        if( rt->fieldDescA[i].label_id==label_id && rt->fieldDescA[i].dflt_value.tflag == t_flag )
          return rt->fieldDescA + i;

      return nullptr;
    }

    const recd_field_desc_t* _field_find_equivalent( const recd_type_t* rt, const recd_field_desc_t* fd )
    {
      return _field_find_equivalent( rt, fd->label_id, fd->dflt_value.tflag);
    }

    const recd_field_desc_t* _field_find_equivalent_recurse( const recd_type_t* rt, const recd_common_field_t* rcf )
    {
      const recd_field_desc_t* ret_fd;

      if( rt == nullptr )
        return nullptr;
      
      if((ret_fd= _field_find_equivalent( rt, rcf->label_id, rcf->val_type_tflag)) != nullptr )
        return ret_fd;

      return _field_find_equivalent_recurse(rt->base_type,rcf);
    }
    
  
    rc_t _recd_field_desc_array_from_cfg( recd_type_t* rt, const object_t* field_dict_cfg )
    {
      rc_t rc = kOkRC;
  
      if( !field_dict_cfg->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The field cfg. is not a dictionary.");
        goto errLabel;
      }
      else
      {
        unsigned fieldN  = field_dict_cfg->child_count();
        rt->fieldDescA = mem::allocZ<recd_field_desc_t>(fieldN);

        for(unsigned i=0; i<fieldN; ++i)
        {
          const object_t*    pair       = field_dict_cfg->child_ele(i);
          const char*        type_label = nullptr;
          const char*        doc_string = nullptr;
          const object_t*    val_cfg    = nullptr;
          const char*        field_label= nullptr;

          // parse the required fields
          if((rc = pair->pair_value()->getv("type",type_label,
                                            "doc",doc_string)) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing the record type field '%s'.",cwStringNullGuard(pair->pair_label()));
            goto errLabel;
          }

          // verify that the field label is not blank
          field_label = pair->pair_label();
          if( field_label==nullptr || textLength(field_label)==0)
          {
            rc = cwLogError(kInvalidArgRC,"A blank or missing field label was encountered.");
            goto errLabel;
          }

          // verify that this is not a duplicate field label
          rt->fieldDescN = i+1;
          if( _field_label_to_desc( rt, field_label) != nullptr )
          {
            rc = cwLogError(kInvalidArgRC,"The field label '%s' is used multiple times in a record type.",cwStringNullGuard(field_label));
            goto errLabel;
          }

          // fill the field record
          rt->fieldDescA[i].label    = mem::duplStr(field_label);
          rt->fieldDescA[i].label_id = id_table::get_id(field_label);
          rt->fieldDescA[i].doc      = mem::duplStr(doc_string);
          rt->fieldDescA[i].val_idx  = i;

          // validate the value type flag
          if((rt->fieldDescA[i].dflt_value.tflag = value_type_label_to_flag( type_label )) == kInvalidTFl )
          {
            rc = cwLogError(kSyntaxErrorRC,"The value type label '%s' is not valid on the field specifier '%s'.",cwStringNullGuard(type_label),cwStringNullGuard(pair->pair_label()));
            goto errLabel;
          }

          // get the optional default value 
          if((val_cfg = pair->pair_value()->find("value")) != nullptr )
          {
            value_t v;
            v.tflag = kInvalidTFl;

            // parse the value into 'v'
            if((rc = value_from_cfg(val_cfg,v)) != kOkRC )
            {
              rc = cwLogError(rc,"The default value parse failed for the field '%s'.",cwStringNullGuard(pair->pair_label()));
              goto errLabel;
            }

            // convert the value from 'v' into field->value
            if((rc = value_from_value(v,rt->fieldDescA[i].dflt_value)) != kOkRC )
            {
              rc = cwLogError(rc,"The default value assignment failed for the field '%s'.",cwStringNullGuard(pair->pair_label()));
              goto errLabel;          
            }
          }
        }
      }
      
    errLabel:

      if( rc == kOkRC )
        std::sort(rt->fieldDescA, rt->fieldDescA + rt->fieldDescN, [](const recd_field_desc_t& f0,const recd_field_desc_t& f1){ return f0.label_id<f1.label_id; } );
      
      return rc;
    }

    bool _is_field_label_used_recurse( const recd_type_t* rt, const char* label  )
    {
      if( rt == nullptr )
        return false;
      
      for(unsigned i=0; i<rt->fieldDescN; ++i)
        if( textIsEqual(rt->fieldDescA[i].label,label) )
          return true;

      return _is_field_label_used_recurse(rt->base_type,label);
    }

    unsigned _recd_type_total_field_count( const recd_type_t* rt )
    {
      unsigned n = rt->fieldDescN;
      if( rt->base_type != nullptr )
        n += _recd_type_total_field_count(rt->base_type);
      return n;      
    }

    void _recd_type_print( unsigned level, const recd_type_t* rt )
    {
      if( rt == nullptr )
        return;

      cwLogPrint("lvl  : lbl_id val_idx     label         class_id:%i\n",rt->class_id);
      cwLogPrint("---  : ------ ------- ---------------:\n");
      
      if( rt->fieldDescN == 0 )
      {
        cwLogPrint("%3i : No Fields\n",level);
      }
      else
      {
        for(unsigned i=0; i<rt->fieldDescN; ++i)
        {
          cwLogPrint("%3i : ",level);
          recd_field_desc_print(rt->fieldDescA + i );
          cwLogPrint("\n");
        } 
      }
      
      _recd_type_print(level+1,rt->base_type);
    }

    // Validate that the record base recd_type chain  matches the link base type chain.
    // This function is verifying that the new link->recd_type chain matches the existing
    // link recd_type chain begining on the first base of the data record.
    rc_t _recd_type_link_validate( const recd_t* r, const recd_type_link_t* link )
    {
      rc_t               rc        = kOkRC;
      const recd_type_t* l_rt      = nullptr; // new link recd_type
      const recd_type_t* d_rt      = nullptr; // data link recd_type
      unsigned           level_cnt = 1; 
      
      // the incoming link must be valid
      if( link == nullptr || link->recd_type == nullptr )
      {
        return cwLogError(kInvalidArgRC,"The link passed for validation is not yet configured.");
        goto errLabel;
      }

      // if the record has no base then there isn't anything to validate
      if( r->base == nullptr )
      {
        // verify that the link recd type also has no base type
        if( link->recd_type->base_type != nullptr )
        {
          return cwLogError(kInvalidArgRC,"The link passed for validation has a base recd_type but the data record does not have base data.");
          goto errLabel;
        }

        // we are done - because the record has no base
        return rc;
      }

      // move all data and rt pointers to the first base level
      r    = r->base;
      l_rt = link->recd_type->base_type;
      d_rt = r->link->recd_type;

      while( r != nullptr )
      {
        // the new recd_type must be non-null
        if( l_rt == nullptr )
        {
          rc  = cwLogError(kInvalidStateRC,"The new recd_type chain ended on level %i before the the recd data chain.",level_cnt);
          goto errLabel;
        }

        // the existing data recd_type must be non-null
        if( d_rt == nullptr )
        {
          rc = cwLogError(kInvalidStateRC,"The data recd_type chain ended on level %i before the the recd data chain.",level_cnt);
          goto errLabel;
        }

        // the class_id of the new and existing recd_type's must match
        if( d_rt->class_id != l_rt->class_id )
        {
          rc = cwLogError(kInvalidStateRC,"The data recd_type class_id %i does not match th new recd_type class id %i on level %i.",d_rt->class_id,l_rt->class_id,level_cnt);
          goto errLabel;          
        }

        // move to the down a level
        r          = r->base;
        l_rt       = l_rt->base_type;
        d_rt       = d_rt->base_type;
        level_cnt += 1;
      }

      // The new recd_type chain should have also ended
      if( l_rt != nullptr )
      {
        rc = cwLogError(kInvalidStateRC,"The recd data chain ended on level %i before the the new recd type chain.",level_cnt-1);
        goto errLabel;
      }

      // The data recd_type chain should have ended
      if( d_rt != nullptr )
      {
        rc = cwLogError(kInvalidStateRC,"The recd data chain ended on level %i before the the data recd type chain.",level_cnt-1);
        goto errLabel;
      }

    errLabel:
      return rc;
      
    }

  }  
}

void cw::flow::recd_registry_create()
{
  recd_registry_destroy();
  __global_recd_reg__.is_initialized_fl = true;
}

void cw::flow::recd_registry_destroy()
{
  recd_registry_t* p    = &__global_recd_reg__;
  recd_reg_node_t* node = p->nodeL;
  while( node !=nullptr )
  {
    recd_reg_node_t* n = node->link;
    mem::release(node);
    node = n;
  }
  __global_recd_reg__.is_initialized_fl = false;

}

void cw::flow::recd_field_desc_print( const recd_field_desc_t* f )
{
  const bool print_type_label_fl = true;
  cwLogPrint("%6i %6i %15s : ",f->label_id,f->val_idx,cwStringNullGuard(f->label));
  value_print(&f->dflt_value,print_type_label_fl,kMinimalValPrintVerb); 
}

cw::rc_t  cw::flow::recd_type_create( recd_type_t*& recd_type_ref, const recd_type_t* base_type, const object_t* cfg )
{
  rc_t            rc          = kOkRC;  
  const object_t* fields_dict = nullptr;;
  recd_type_t*    recd_type   = mem::allocZ<recd_type_t>();
  
  recd_type_ref = nullptr;

  if( cfg != nullptr )
  {
    // get the fields list
    if((rc = cfg->getv("fields",fields_dict)) != kOkRC )
    {
      rc = cwLogError(rc,"The 'fields' dictionary was not found in the record 'fmt' specifier.");
      goto errLabel;
    }

    // load the fields list
    if((rc = _recd_field_desc_array_from_cfg(recd_type,fields_dict)) != kOkRC )
    {
      goto errLabel;
    }
  }

  // if a base was given verify that no field labels were re-used by the top level
  if( base_type != nullptr )
  {
    for(unsigned i=0; i<recd_type->fieldDescN; ++i)
    {
      if( _is_field_label_used_recurse( base_type, recd_type->fieldDescA[i].label  ) )
      {
        rc = cwLogError(kInvalidStateRC,"The field '%s' is already used in the base record type.",cwStringNullGuard(recd_type->fieldDescA[i].label));
        goto errLabel;
      }
    }
  }
  
  recd_type->class_id  = kInvalidId;
  recd_type->base_type = base_type;
  recd_type_ref        = recd_type;

  // Register this recd_type_t and assign it a class_id
  _recd_register_type( recd_type );
  assert( recd_type->class_id != kInvalidIdx);
  
errLabel:
  if( rc != kOkRC )
  {
    rc = cwLogError(rc,"recd_type create failed.");
    recd_type_destroy(recd_type);
  }
  
  return rc;
}

void  cw::flow::recd_type_destroy( recd_type_t*& recd_type_ref )
{
  if( recd_type_ref == nullptr )
    return;

  recd_type_t* rt = recd_type_ref;

  if( rt->fieldDescA!=nullptr )
  {
    for(unsigned i=0; i<rt->fieldDescN; ++i)
    { 
      mem::release(rt->fieldDescA[i].label);
      mem::release(rt->fieldDescA[i].doc);
    }
    
    mem::release(rt->fieldDescA);
    rt->fieldDescN = 0;
  }

  mem::release(recd_type_ref);
}

void cw::flow::recd_type_print( const recd_type_t* rt )
{
  _recd_type_print(0,rt);
}

const char* cw::flow::recd_type_field_index_to_label( const recd_type_t* rt, unsigned field_idx )
{
  if( field_idx > rt->fieldDescN )
    return nullptr;
  
  return rt->fieldDescA[ field_idx ].label;
}



cw::rc_t cw::flow::recd_format_create( recd_fmt_t*& recd_fmt_ref, const object_t* cfg, unsigned dflt_alloc_cnt )
{
  rc_t         rc        = kOkRC;
  recd_fmt_t*  recd_fmt  = nullptr;
  
  recd_fmt_ref = nullptr;

  recd_fmt = mem::allocZ<recd_fmt_t>();

  if( cfg->find( "fields" ) != nullptr )
  {
    recd_fmt->fieldD_cfg = cfg;
  }

  recd_fmt->alloc_cnt = dflt_alloc_cnt;
  
  if((rc =cfg->getv_opt("alloc_cnt",recd_fmt->alloc_cnt,
                        "required",recd_fmt->req_fieldL)) != kOkRC )
  {
    rc = cwLogError(rc,"Error parsing record format 'alloc_cnt'.");
    goto errLabel;
  }

  if(recd_fmt->req_fieldL != nullptr && !recd_fmt->req_fieldL->is_list() )
  {
    rc = cwLogError(rc,"The 'required' field list is not a list.");
    goto errLabel;
  }

  recd_fmt_ref = recd_fmt;

errLabel:
  if(rc != kOkRC )
  {
    recd_format_destroy(recd_fmt);
    rc = cwLogError(rc,"Record format creation failed.");
  }
  
  return rc;  
}

void cw::flow::recd_format_destroy( recd_fmt_t*& recd_fmt_ref )
{
  if( recd_fmt_ref != nullptr )
  {
    mem::release(recd_fmt_ref);
  }
}



namespace cw
{
  namespace flow
  {
    unsigned _get_unique_class_ids( const recd_type_t* const * recdTypeA, unsigned recdTypeN, unsigned* classIdA, bool* rt_dropA )
    {
      unsigned classIdN = 0;
      
      // for each recd type
      for(unsigned i=0; i<recdTypeN; ++i)
      {
        unsigned this_class_id = recdTypeA[i] == nullptr ? kInvalidId : recdTypeA[i]->class_id;
        
        rt_dropA[i] = false;

        // for each known class_id
        unsigned j;
        for(j=0; j<classIdN; ++j)
        {
          // if this type was already found then drop this type pointer.
          if( this_class_id == classIdA[j] )
          {
            rt_dropA[i] = true;
            break;
          }
        }

        // this is the first recd_type of it's class store the class_id
        if( !rt_dropA[i] )
        {
          classIdA[ classIdN++ ] = this_class_id;
        }
      }

      return classIdN;
    }

    rc_t _get_all_fields( const recd_type_t* rt, unsigned rcfi, recd_common_field_t* rcfA, unsigned& rcfN_ref )
    {
      if( rt == nullptr )
      {
        rcfN_ref = rcfi;
        return kOkRC;
      }
      
      for(unsigned i=0; i<rt->fieldDescN; ++i)
      {
        if( rcfi >= rcfN_ref )
          return cwLogError(kBufTooSmallRC,"The field array is too small.");

        recd_common_field_t* rcf = rcfA + rcfi;
        rcf->field_label    = rt->fieldDescA[i].label;
        rcf->label_id       = rt->fieldDescA[i].label_id;
        rcf->val_type_tflag = rt->fieldDescA[i].dflt_value.tflag;
        
        rcfi += 1;
      }
      
      return _get_all_fields(rt->base_type, rcfi, rcfA, rcfN_ref );
    }

    rc_t _get_complete_field_list( const recd_type_t* rt, recd_common_field_t*& rcfA_ref, unsigned& rcfN_ref )
    {
      rcfA_ref = nullptr;
      rcfN_ref = 0;

      rc_t                 rc          = kOkRC;
      unsigned             rcfN        = _recd_type_total_field_count( rt );
      recd_common_field_t* rcfA        = mem::allocZ<recd_common_field_t>(rcfN);
      unsigned             actual_rcfN = rcfN;
      
      if((rc = _get_all_fields( rt, 0, rcfA, actual_rcfN )) != kOkRC )
        goto errLabel;

      assert( actual_rcfN == rcfN);
      
      rcfA_ref = rcfA;
      rcfN_ref = actual_rcfN;
    errLabel:
      if( rc != kOkRC )
      {
        mem::release(rcfA);
        
      }
      return rc;
    }

    rc_t _create_common_field_array( recd_array_t* recd_array )
    {
      rc_t                 rc       = kOkRC;
      unsigned             drop_cnt = 0;
      unsigned             n        = 0;
      bool*                drop_flA = nullptr;
      recd_common_field_t* rcfA     = nullptr;
      unsigned             rcfN     = 0;

      // get the complete list of fields from the first recd_type
      if((rc = _get_complete_field_list( recd_array->typeA[0], rcfA, rcfN )) != kOkRC )
      {
        goto errLabel;
      }

      // drop_flA[] is used to signal fields that are not common to all recd_types
      drop_flA = mem::allocZ<bool>( rcfN );        
      vop::fill(drop_flA, rcfN, false );

      // for each recd_type (except the first one)
      for(unsigned i=1; i<recd_array->typeN; ++i)
      {
        int common_cnt = (int)rcfN;
        // for each field in the reference that has not already been dropped
        for(unsigned rcfi=0; rcfi<rcfN; ++rcfi)
        {
          if( !drop_flA[rcfi] )
          {
            // if  the ref. field is not in the ith recd_type ...
            if( !_field_find_equivalent_recurse(recd_array->typeA[i], rcfA + rcfi ) ) 
            {
              // ... then this is not a common field - drop it
              drop_flA[rcfi] = true;
              drop_cnt += 1;
              common_cnt -= 1;
            }
          }
        }

        if( common_cnt <= 0 )
        {
          cwLogWarning("The type class_id=%i has no common fields and will not be accessible.",recd_array->typeA[i]->class_id);          
        }
        
      }

      // verify that at least 1 common field was found
      if( drop_cnt >= rcfN )
      {
        rc = cwLogError(kInvalidStateRC,"No common fields were found in the recd_array. This is an invalid state.");
        goto errLabel;
      }

      // create and fill comFieldA with all the common fields
      recd_array->comFieldN = rcfN - drop_cnt;
      recd_array->comFieldA = mem::allocZ<recd_common_field_t>(recd_array->comFieldN);

      n = 0;
      for(unsigned i=0; i<rcfN; ++i)
        if( !drop_flA[i] )
        {
          recd_array->comFieldA[n].field_label    = rcfA[i].field_label;
          recd_array->comFieldA[n].label_id       = rcfA[i].label_id;
          recd_array->comFieldA[n].val_type_tflag = rcfA[i].val_type_tflag;
          n += 1;
        }

      assert( n == recd_array->comFieldN );
      
    errLabel:
      mem::release(drop_flA);
      mem::release(rcfA);
      return rc;
    }

    rc_t _field_type_loc( unsigned level, const recd_type_t* rt, const recd_common_field_t* com_field, unsigned& level_cnt_ref, unsigned& val_idx_ref )
    {
      const recd_field_desc_t* fd;

      if( rt == nullptr )
        return cwLogError(kEleNotFoundRC,"The recd_array common field '%s' was not found.",cwStringNullGuard(com_field->field_label));
      
      if((fd = _field_find_equivalent(rt,com_field->label_id,com_field->val_type_tflag)) != nullptr )
      {
        level_cnt_ref = level;
        val_idx_ref = fd->val_idx;
        return kOkRC;
      }

      return _field_type_loc(level+1,rt->base_type, com_field,level_cnt_ref,val_idx_ref);
      
    }

    rc_t _create_type_link_array( recd_array_t* recd_array )
    {
      rc_t rc = kOkRC;
      recd_array->typeLinkA = mem::allocZ<recd_type_link_t>(recd_array->typeN);

      for(unsigned i=0; i<recd_array->typeN; ++i)
      {
        recd_type_link_t* link = recd_array->typeLinkA + i;

        link->recd_type = recd_array->typeA[i];
        link->fieldLocN = recd_array->comFieldN;
        link->fieldLocA = mem::allocZ<recd_field_loc_t>( link->fieldLocN );

        for(unsigned fli=0; fli<link->fieldLocN; ++fli)
        {
          recd_field_loc_t* loc = link->fieldLocA + fli;
          loc->com_field = recd_array->comFieldA + fli;
          loc->level_cnt = kInvalidCnt;
          loc->value_idx = kInvalidIdx;
          
          if((rc = _field_type_loc( 0, link->recd_type, loc->com_field, loc->level_cnt, loc->value_idx )) != kOkRC )
          {
            goto errLabel;
          }
        }
      }
      
    errLabel:
      return rc;
    }

    rc_t _set_recd_empty( recd_t* recd, const recd_type_t* top_level_recd_type )
    {
      rc_t                     rc  = kOkRC; 
      unsigned                 fdN = top_level_recd_type->fieldDescN;
      const recd_field_desc_t* fdA = top_level_recd_type->fieldDescA;

      recd->link = nullptr;
      recd->base = nullptr;;

      // fill in the default values for all fields
      for(unsigned i =0; i<fdN; ++i)
      {
        value_t& dst_val = recd->valA[ fdA[i].val_idx ];
        
        dst_val.tflag = kInvalidTFl;
    
        if((rc = value_from_value( fdA[i].dflt_value, dst_val )) != kOkRC )
        {
          rc = cwLogError(rc,"Default field value assignment failed on '%s'.",cwStringNullGuard(fdA[i].label));
          goto errLabel;
        }

      }
      
    errLabel:
      return rc;
  
    }

  }
}

cw::rc_t cw::flow::recd_print( const recd_t* r )
{
  rc_t rc = kOkRC;
  const bool print_type_label_fl = true;

  cwLogPrint("(");
  for(unsigned fi=0; fi<r->link->fieldLocN; ++fi)
  {
    value_t v{};
    
    if((rc = recd_get( r, fi, v)) != kOkRC )
    {
      rc = cwLogError(rc,"Unable to access a field to print.");
      goto errLabel;
    }

    cwLogPrint("%s=",r->link->fieldLocA[fi].com_field->field_label);
    value_print(&v,print_type_label_fl);
    
    if( fi+1 < r->link->fieldLocN )
      cwLogPrint(",");

  }
  cwLogPrint(")");
errLabel:
  return rc;
}

cw::rc_t cw::flow::recd_array_create( recd_array_t*&             recd_array_ref,
                                      const object_t*            top_level_fmt_cfg,
                                      const recd_type_t* const * base_recd_typeA,
                                      unsigned                   base_recd_typeN,
                                      unsigned                   allocRecdN )
{
  rc_t          rc         = kOkRC;
  recd_array_t* recd_array = nullptr;
  unsigned*     classIdA   = nullptr;
  bool*         rt_dropA   = nullptr;
  unsigned      n          = 0;
  unsigned      topLevelFieldN = 0;
  const recd_type_t* const no_base_recd_typeA[] = {nullptr};
  
  if( base_recd_typeN == 0 )
  {
    base_recd_typeA = no_base_recd_typeA;
    base_recd_typeN = 1;
  }
    
  recd_array_ref = nullptr;

  if( top_level_fmt_cfg==nullptr && base_recd_typeN == 0 )
    return cwLogError(kInvalidArgRC,"A record array cannot be created unless at least one base or top level record type is provided.");
  
  recd_array= mem::allocZ<recd_array_t>();

  // get the unique base types
  classIdA = mem::allocZ<unsigned>(base_recd_typeN); // recd_type class_ids for each unique class
  rt_dropA = mem::allocZ<bool>( base_recd_typeN );   // True if the associated recd_type_t* is a duplicate of another other type in recd_typeA[]
  recd_array->typeN = _get_unique_class_ids( base_recd_typeA, base_recd_typeN, classIdA, rt_dropA );
  
  
  // recd_array->typeN now holds the count of unique recd_types recd_typeA[]
  
  recd_array->typeA = mem::allocZ<recd_type_t*>(recd_array->typeN );

  // for each unique base type create a new type containing a top level
  n = 0;
  for(unsigned i=0; i<base_recd_typeN; ++i)
  {
    if( !rt_dropA[i] )
    {
      recd_type_t* new_rt = nullptr;
      if((rc = recd_type_create( new_rt, base_recd_typeA[i], top_level_fmt_cfg )) != kOkRC )
      {
        rc = cwLogError(rc,"Top level create failed.");
        goto errLabel;
      }

      // store the new type 
      recd_array->typeA[n++] = new_rt;

      topLevelFieldN = new_rt->fieldDescN;
    }
  }

  // verify that that the established count of unique recd_types is the same as the stored count
  assert(n == recd_array->typeN );

  recd_array->valA       = topLevelFieldN==0 ? nullptr : mem::allocZ<value_t>(topLevelFieldN * allocRecdN);
  recd_array->recdA      = mem::allocZ<recd_t>(allocRecdN);
  recd_array->allocRecdN = allocRecdN;
  recd_array->recdN      = 0;

  // create and populate recd_array->comFieldA[]
  if((rc = _create_common_field_array( recd_array )) != kOkRC )
  {
    goto errLabel;
  }

  // create and populate recd_array->typeLinkA[]
  if((rc = _create_type_link_array( recd_array )) != kOkRC )
  {
    goto errLabel;
  }
  
  // for each record
  for(unsigned i=0; i<allocRecdN; ++i)
  {
    // set the value array for this record
    recd_array->recdA[i].valA = recd_array->valA==nullptr ? nullptr : recd_array->valA + (i*topLevelFieldN);

    // set the default values for all fields 
    if((rc = _set_recd_empty( recd_array->recdA + i, recd_array->typeA[0] )) != kOkRC )
      goto errLabel;
  }

  
  recd_array_ref = recd_array;

errLabel:
  if( rc != kOkRC )
  {
    recd_array_destroy(recd_array);
  }
  
  mem::release(classIdA);
  mem::release( rt_dropA );
  return rc;
}

cw::rc_t cw::flow::recd_array_destroy( recd_array_t*& recd_array_ref )
{
  recd_array_t* recd_array = recd_array_ref;

  for(unsigned i=0; i<recd_array->typeN; ++i)
  {
    recd_type_destroy( recd_array->typeA[i] );
    if( recd_array->typeLinkA != nullptr )
      mem::release( recd_array->typeLinkA[i].fieldLocA);
  }
  
  mem::release(recd_array->typeA);
  mem::release(recd_array->typeLinkA);
  mem::release(recd_array->comFieldA);
  mem::release(recd_array->valA);
  mem::release(recd_array->recdA);
  mem::release(recd_array_ref);
  
  return kOkRC;
}

cw::rc_t cw::flow::recd_array_empty( recd_array_t* recd_array )
{
  rc_t rc = kOkRC;
  
  for(unsigned i=0; i<recd_array->recdN; ++i)
  {    
    if((rc = _set_recd_empty( recd_array->recdA + i, recd_array->typeA[0] )) != kOkRC )
      goto errLabel;
  }
  
  recd_array->recdN = 0;
  
errLabel:  
  return rc;
}


cw::rc_t cw::flow::recd_array_append_from_cfg( recd_array_t* recd_array, const object_t* data_cfg )
{
  rc_t rc = kOkRC;
  
  unsigned recd_idx       = 0;
  const object_t* ele_dict = nullptr;
  const recd_type_t* recd_type = nullptr;
  const recd_type_link_t* recd_link = nullptr;

  // the incoming data must be provided as a list of dictionaries
  if( !data_cfg->is_list() )
  {
    rc = cwLogError(kInvalidArgRC,"The recd array data must be a list of dictionaries.");
    goto errLabel;
  }

  // verify that the recd_array has space for all of the incoming records
  if( recd_array->recdN + data_cfg->child_count() > recd_array->allocRecdN )
  {
    rc = cwLogError(kBufTooSmallRC,"The data array has %i too few empty slots available.", (recd_array->recdN + data_cfg->child_count()) - recd_array->allocRecdN);
    goto errLabel;
  }

  // find a base type with an empty base
  for(unsigned i=0; i<recd_array->typeN; ++i)
    if(recd_array->typeA[i]->base_type==nullptr)
    {
      recd_type = recd_array->typeA[i];
      recd_link = recd_array->typeLinkA + i;
      break;
    }

  // if the base type was not found
  if( recd_type == nullptr )
  {
    rc = cwLogError(kInvalidArgRC,"The recd_array does not support a record type with a blank base type.");
    goto errLabel;
  }
  
  // For each recd in the 'data' list.
  while( (ele_dict = data_cfg->next_child_ele(ele_dict)) != nullptr )
  {
    const object_t* pair = nullptr;
          
    if( !ele_dict->is_dict() )
    {
      rc = cwLogError(kSyntaxErrorRC,"The data element at index %i is not a dictionary.",recd_idx);
      goto errLabel;
    }

    // For each field in this record
    while( (pair = ele_dict->next_child_ele(pair)) != nullptr )
    {
      unsigned field_idx = kInvalidIdx;
      value_t field_value;

      // verify that the dict. pair is a pair with a valid label
      if( !pair->is_pair() || pair->pair_label()==nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The cfg. data element at index %i is not a pair.",recd_idx);
        goto errLabel;
      }
            
      // get the recd index associated with the field label for this data element
      if((field_idx = recd_array_field_index( recd_array, pair->pair_label())) == kInvalidIdx )
      {
        rc = cwLogError(kEleNotFoundRC,"The cfg. data field '%s' at cfg data element index %i is not valid.",cwStringNullGuard(pair->pair_label()),recd_idx);
        goto errLabel;
      }

      // parse the data element value into a value_t
      if((rc = value_from_cfg( pair->pair_value(), field_value )) != kOkRC )
      {
        rc = cwLogError(rc,"The value of the cfg. data field '%s' at element index %i could not be parsed.",cwStringNullGuard(pair->pair_label()),recd_idx);
        goto errLabel;
      }

      // double check that there is sufficient space
      if( recd_array->recdN + recd_idx >= recd_array->allocRecdN )
      {
        rc = cwLogError(kInvalidStateRC,"The recd_array has insufficient space to hold the provided data list.");
        goto errLabel;
      }
      
      // copy the value into the record
      if((rc = recd_set( recd_link, recd_array->recdA + recd_array->recdN + recd_idx, nullptr, field_idx, field_value )) != kOkRC )
      {
        rc = cwLogError(rc,"The value assignment of the cfg. data field '%s' at element index %i failed.",cwStringNullGuard(pair->pair_label()),recd_idx);
        goto errLabel;              
      }

    }

    recd_idx += 1;
  }

  recd_array->recdN += recd_idx;
  
errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"recd_array fill from a cfg. failed.");
  
  return rc;
  
}

cw::rc_t cw::flow::recd_array_type_link( const recd_array_t* recd_array, unsigned base_type_class_id, const cw::flow::recd_type_link_t*& link_ref )
{
  rc_t rc = kOkRC;
  link_ref = nullptr;
  for(unsigned i=0; i<recd_array->typeN; ++i)
    if( recd_array->typeLinkA[i].recd_type->base_type != nullptr && recd_array->typeLinkA[i].recd_type->base_type->class_id == base_type_class_id )
    {
      link_ref = recd_array->typeLinkA + i;
      return rc;
    }
  
  
  return cwLogError(kInvalidArgRC,"No link could be found for the base class id %i.",base_type_class_id);
}

cw::rc_t cw::flow::recd_array_append_pass_through( recd_array_t* recd_array, const recd_t* src_recd )
{
  rc_t rc   = kOkRC;  
  const recd_type_link_t* link = nullptr;

  if( recd_array->valA != nullptr )
  {
    rc = cwLogError(kInvalidStateRC,"A 'pass-through' record cannot be appended to a record array that has top-level fields.");
    goto errLabel;
  }

  if( recd_array->recdN >= recd_array->allocRecdN )
  {
    rc = cwLogError(kBufTooSmallRC,"The record array is full.");
    goto errLabel;
  }
  
  if((rc = recd_array_type_link(recd_array, src_recd->link->recd_type->class_id, link)) != kOkRC )
  {    
    rc = cwLogError(rc,"Type link not found.");
    goto errLabel;
  }
  
  recd_array->recdA[recd_array->recdN].link = link;
  recd_array->recdA[recd_array->recdN].valA = nullptr;
  recd_array->recdA[recd_array->recdN].base = src_recd;

  // assert that the new link is sane relative to the src record base type
  assert( _recd_type_link_validate( recd_array->recdA + recd_array->recdN, link ) == kOkRC );
  
  recd_array->recdN += 1;
  
errLabel:
  return rc;
}


cw::rc_t cw::flow::recd_array_print( const recd_array_t* recd_array )
{
  rc_t rc = kOkRC;
  for(unsigned i=0; i<recd_array->recdN; ++i)
  {
    if((rc = recd_print(recd_array->recdA + i )) != kOkRC )
      goto errLabel;
    
    cwLogPrint("\n");
  }
  
errLabel:
  return rc;
}

void cw::flow::recd_array_print_info( const recd_array_t* recd_array )
{
  cwLogPrint("allocRecdN:%i recdN:5i\n",recd_array->allocRecdN,recd_array->recdN);

  // for each type contained in this recd_array
  for(unsigned ti=0; ti<recd_array->typeN; ++ti)
  {    
    const recd_type_link_t* link = recd_array->typeLinkA + ti;
    
    // print the complete type
    recd_type_print(link->recd_type);

    cwLogPrint("Common Fields:\n");
    if( link->fieldLocN == 0 )
    {
      cwLogPrint("   no common fields\n");
    }
    else
    {
      cwLogPrint("    lvl vidx : lbl_id data type  field label        \n");
      cwLogPrint("    --- ---- : ------ ---------- -------------------\n");
          
      for(unsigned loc_i=0; loc_i<link->fieldLocN; ++loc_i)
      {
        const recd_field_loc_t* loc = link->fieldLocA + loc_i;
        
        const char* data_type_label =   value_type_flag_to_label( loc->com_field->val_type_tflag );
        
        cwLogPrint("    %3i %4i : %6i %10s %s\n",loc->level_cnt,loc->value_idx,loc->com_field->label_id,data_type_label,loc->com_field->field_label);          
      }
    }
    cwLogPrint("\n");
    
  }
}


unsigned cw::flow::recd_array_field_index( const recd_array_t* recd_array, const char* field_label )
{
  unsigned label_id = id_table::get_id(field_label);
  for(unsigned i=0; i<recd_array->comFieldN; ++i)
    if( recd_array->comFieldA[i].label_id == label_id )
      return i;
  
  return kInvalidIdx;
}

cw::rc_t cw::flow::recd_array_field_index( const recd_array_t* recd_array, const char* field_label, unsigned& field_idx_ref )
{
  if((field_idx_ref = recd_array_field_index(recd_array,field_label)) == kInvalidIdx )
    return cwLogError(kEleNotFoundRC,"The field label '%s' was not found.",cwStringNullGuard(field_label));

  return kOkRC;
}



