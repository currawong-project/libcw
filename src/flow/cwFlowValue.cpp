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

namespace cw
{
  namespace flow
  {
    idLabelPair_t _valVerbLevelA[] = {
      { kSilentValPrintVerb,  "silent"    },
      { kMinimalValPrintVerb, "minimal"   },
      { kSummaryValPrintVerb, "summary"   },
      { kAllValPrintVerb,     "all"       },
      { kInvalidValPrintVerb,    "<invalid>" }
    };

    idLabelPair_t _typeLabelFlagsA[] = {
      
      { kBoolTFl,  "bool" },
      { kUIntTFl,  "uint" },
      { kIntTFl,   "int", },
      { kFloatTFl, "float"},
      { kDoubleTFl,"double"},
      
      { kBoolMtxTFl,  "bool_mtx" },
      { kUIntMtxTFl,  "uint_mtx" },
      { kIntMtxTFl,   "int_mtx"  },
      { kFloatMtxTFl, "float_mtx" },
      { kDoubleMtxTFl,"double_mtx" },
      
      { kABufTFl,   "audio" },
      { kFBufTFl,   "spectrum" },
      { kMBufTFl,   "midi" },
      { kRBufTFl,   "record" },
      { kStringTFl, "string" },
      { kCfgTFl,    "cfg" },
      { kMidiTFl,   "m3" },

      // alias types to map to cwDspTypes.h
      { kFloatTFl, "srate"},
      { kFloatTFl, "sample"},
      { kFloatTFl, "coeff"},
      { kDoubleTFl, "ftime" },

      { kNumericTFl, "numeric" },
      { kAllTFl,     "all" },

      { kRuntimeTFl, "runtime" },

      { kInvalidTFl, "<invalid>" }
    };

    const char* _typeFlagToLabel( unsigned flag )
    {
      return idToLabel(_typeLabelFlagsA,flag,kInvalidTFl);
    }

    void _recd_type_destroy_field_list( recd_field_t* f )
    {  
      while( f != nullptr )
      {
        recd_field_t* f0 = f->link;

        mem::release(f->doc);
        mem::release(f->label);
        mem::release(f);
    
        f = f0;
      }
    }

    unsigned _recd_field_list_set_index( recd_field_t* fld, unsigned index )
    {
      for(recd_field_t* f=fld; f!=nullptr; f=f->link)
        f->val_idx = index++;
      return index;
    }

    const recd_field_t* _find_field_from_index( const recd_field_t* f, unsigned field_idx )
    {
      const recd_field_t* result = nullptr;
      
      for(; f!=nullptr && result==nullptr; f=f->link)
        if( f->val_idx == field_idx )
            result = f;
        
      return result;
    }

    const recd_field_t* _find_field_from_index( const recd_type_t* rt, unsigned field_idx )
    {
      const recd_field_t* result = nullptr;
      
      if( field_idx < rt->fieldN )
        result = _find_field_from_index( rt->fieldL, field_idx );
      else
        if( rt->base != nullptr )
          result = _find_field_from_index( rt->base, field_idx - rt->fieldN );

      return result;
    }


    const recd_field_t* _recd_field_from_label( const recd_field_t* fieldL, const char* label )
    {
      const recd_field_t* f;
      for(f=fieldL; f!=nullptr; f=f->link)
        if( textIsEqual(f->label,label) )
          return f;
      return nullptr;
    }

    recd_field_t*  _recd_field_alloc_and_link( recd_field_t*& field_list_ref, const char* label, const char* doc_string )
    {
      rc_t          rc    = kOkRC;
      recd_field_t* field = nullptr;

      // verify that  this is not a duplicate field name
      if( _recd_field_from_label( field_list_ref, label ) != nullptr )
      {
        cwLogError(kInvalidStateRC,"The field label '%s' is duplicated in the record.",cwStringNullGuard(label));
        goto errLabel;
      }

      // allocate the field record
      field          = mem::allocZ<recd_field_t>();
      field->label   = mem::duplStr(label);
      field->doc     = mem::duplStr(doc_string);
      field->uid     = id_table::get_id(field->label);
      field->src_uid = kInvalidIdx;
      
      // add the new field to the end of the field list
      if( field_list_ref == nullptr )
        field_list_ref = field;
      else
      {
        recd_field_t* f = field_list_ref;
        while( f->link != nullptr )
          f = f->link;
        
        assert(f!=nullptr);
        f->link = field;
      }

    errLabel:

      return field;
      
    }

    rc_t _recd_field_list_from_cfg( recd_field_t*& field_list_ref, const object_t* field_dict_cfg )
    {
      rc_t rc = kOkRC;
  
      if( !field_dict_cfg->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The field cfg. is not a dictionary.");
        goto errLabel;
      }
      else
      {
        unsigned row_cnt = field_dict_cfg->child_count();

        for(unsigned i=0; i<row_cnt; ++i)
        {
          const object_t* pair       = field_dict_cfg->child_ele(i);
          recd_field_t*   field      = nullptr;
          const char*     type_label = nullptr;
          const char*     doc_string = nullptr;
          const object_t* val_cfg    = nullptr;

          // parse the required fields
          if((rc = pair->pair_value()->getv("type",type_label,
                                            "doc",doc_string)) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing the record type field '%s'.",cwStringNullGuard(pair->pair_label()));
            goto errLabel;
          }

          // allocate a new field record
          if((field = _recd_field_alloc_and_link(field_list_ref, pair->pair_label(), doc_string )) == nullptr )
          {
            rc = cwLogError(kOpFailRC,"Record field type allocation failed on '%s'.",cwStringNullGuard(pair->pair_label()));
            goto errLabel;
          }
                
          // validate the value type flag
          if((field->value.tflag = value_type_label_to_flag( type_label )) == kInvalidTFl )
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
            if((rc = value_from_value(v,field->value)) != kOkRC )
            {
              rc = cwLogError(rc,"The default value assignment failed for the field '%s'.",cwStringNullGuard(pair->pair_label()));
              goto errLabel;          
            }
          } 
        }
    
      }
    errLabel:
      return rc;
    }


    rc_t _recd_set_default_value( recd_field_t* fieldL, recd_t* r )
    {
      rc_t          rc = kOkRC;      
      recd_field_t* f  = fieldL;
      
      for(; f!=nullptr; f=f->link)
      {
        if(f->value.tflag != kInvalidTFl)
        {
          if((rc = value_from_value( f->value, r->valA[f->val_idx] )) != kOkRC )
          {
            rc = cwLogError(rc,"Set default value failed on the field '%s'.",cwStringNullGuard(f->label));
            goto errLabel;
          }
        }
      }
      
    errLabel:
      return rc;
    }

    
    const recd_field_t* _find_field( const recd_field_t* fieldL, const char* label, unsigned label_charN )
    {
      for(const recd_field_t* f = fieldL; f!=nullptr; f=f->link)
      {
        unsigned n = textLength(f->label);
        
        if( n==label_charN && textIsEqual(f->label,label,label_charN) )
          return f;
      }   
      return nullptr;      
    }

    // Return true if any of the field labels in the field list 'f0' are duplicated in the field list 'ref'.
    const recd_field_t* _get_duplicate_field( const recd_field_t* f0, const recd_field_t* ref )
    {
      // for each field in f0
      for(; f0 != nullptr; f0=f0->link )
      {
        const recd_field_t* f = nullptr;

        // if this field label is found in 'ref' ...
        if((f = _find_field(ref,f0->label,textLength(f0->label))) != nullptr )
        {

          // a duplicate label was found
          return f;
        }
        
      }
      return nullptr;
    }

    const recd_field_t* _find_value_field( const recd_field_t* fieldL, const char* field_label)
    {
      const char*   period = firstMatchChar( field_label, '.' );
      const recd_field_t* f      = nullptr;;

      // if we are searching for a value field label
      if((f = _find_field( fieldL, field_label, textLength(field_label) )) ==  nullptr )
      {
        goto errLabel;
      }
      
      
    errLabel:      
      return f;
    }

    unsigned _field_label_to_index( const recd_type_t* type, const char* field_label )
    {
      if( field_label == nullptr )
        return kInvalidIdx;

      for(unsigned i=0; i<type->fieldMapN; ++i)
        if( textIsEqual(type->fieldMapA[i].field_desc->label,field_label))
          return i;
      return kInvalidIdx;
    }

    
    bool _fields_are_equivalent( const recd_field_t* f0, const recd_field_t* f1)
    {

      if( !textIsEqual(f0->label,f1->label) )
        return false;
      
      return /*f0->val_idx==f1->val_idx &&*/ f0->value.tflag == f1->value.tflag;
    }
    
    void _recd_type_print_field( unsigned field_idx, unsigned level, const recd_field_t* f )
    {
        unsigned labelN = textLength(f->label) + 2;
        char label[ labelN ];
        label[0] = 0;
        label[labelN-1] = 0;
          
        strcat(label,f->label);

        const bool print_type_label_fl = true;
        cwLogPrint("(lvl:%i : fidx:%i vidx:%2i u:%3i su:%3i %s:",level, field_idx,f->val_idx,f->uid,f->src_uid,f->label);
        value_print(&f->value,print_type_label_fl,kMinimalValPrintVerb);
        cwLogPrint(")");
    }
    
    void _recd_type_print_fields( unsigned level, const recd_type_t* rt )
    {
      const recd_field_t* f;

      for(f=rt->fieldL; f!=nullptr; f=f->link)        
      {
        unsigned field_idx = recd_type_field_index( rt, f->label);
        _recd_type_print_field(field_idx,level,f);
      }

      
    }
    
    void _recd_type_print( unsigned level_idx, const recd_type_t* rt )
    {
      if( rt->fieldL != nullptr )
      {
        _recd_type_print_fields(level_idx,rt);
        cwLogPrint("\n");
      }
      
      if( rt->base != nullptr )
      {
        _recd_type_print( level_idx + 1, rt->base );
      }      

    }

    void _recd_print_field_list( const recd_field_t* fieldL,  const value_t* valA )
    {
      const bool print_type_label_fl = false;
      const recd_field_t* f;
      
      if( fieldL == nullptr )
        return;
      
      cwLogPrint("(");
      for(f=fieldL; f!=nullptr; f=f->link)
      {
        cwLogPrint("%i:%s=",f->val_idx,f->label);
        
        value_print(valA + f->val_idx, print_type_label_fl, kMinimalValPrintVerb);
        
        if( f->link != nullptr)
          cwLogPrint(",");
        
      }
      cwLogPrint(")");
    }

    void _recd_print_field( const recd_t* recd, const recd_field_map_t* fm )
    {
      const bool          print_type_label_fl = false;
      const recd_field_t* fd                  = fm->field_desc;
      value_t val;
        
      //for(unsigned level_idx=0; level_idx<fm->level_idx; level_idx++)
      //  recd = recd->base;
        
      cwLogPrint("%s=",fd->label);

      if( recd_get_from_uid(recd,fd->uid,val) == kOkRC )        
        value_print(&val, print_type_label_fl, kMinimalValPrintVerb);

      // value_print(recd->valA + fd->val_idx, print_type_label_fl, kMinimalValPrintVerb);
    }

      
    void _recd_print( const recd_t* recd )
    {      
      cwLogPrint("(");
      for(unsigned i=0; i<recd->type->fieldMapN; ++i)
      {
        _recd_print_field(recd, recd->type->fieldMapA + i );
        
        if( i+1 < recd->type->fieldMapN )
          cwLogPrint(",");
        
      }
      cwLogPrint(")");
    }
        
    rc_t _recd_type_create_default_map( recd_type_t* type )
    {
      rc_t rc = kOkRC;

      mem::release(type->fieldMapA);
      type->fieldMapN = 0;

      // the length of the field map is the count of all fields in the type (including base fields)
      type->fieldMapN = recd_type_max_field_count( type );
      type->fieldMapA = mem::allocZ<recd_field_map_t>(type->fieldMapN);

      unsigned            map_idx  = 0;
      const recd_field_t* fld_desc = type->fieldL;
      
      // the fields in the top level type go into the first type->fieldN map entries
      for(; fld_desc!=nullptr; fld_desc=fld_desc->link,++map_idx)
      {
        assert(map_idx < type->fieldMapN );
        type->fieldMapA[map_idx].field_desc = fld_desc;
        type->fieldMapA[map_idx].level_idx  = 0;
      }

      // The following map entries are taken from the map on the second level
      for(unsigned i=0; type->base != nullptr && i<type->base->fieldMapN; ++i,++map_idx)
      {
        assert(map_idx < type->fieldMapN );
          
        type->fieldMapA[map_idx].field_desc = type->base->fieldMapA[i].field_desc;
        type->fieldMapA[map_idx].level_idx  = type->base->fieldMapA[i].level_idx + 1;
      }

      type->fieldMapN = map_idx;
       
      return rc;
    }

    rc_t _recd_type_create_map( recd_type_t* type, const char* const * labelA, unsigned labelN )
    {
      rc_t rc             = kOkRC;
      unsigned max_fieldN = recd_type_max_field_count( type );

      // verify that there are not more field labels to map than there are fields
      if( max_fieldN < labelN )
      {
        cwLogError(kInvalidArgRC,"The count of field labels (%i) exceeds the count of possible fields. (%i).",labelN,max_fieldN);
        goto errLabel;
      }

      // if the map was previously allocated then release it
      mem::release(type->fieldMapA);
      type->fieldMapN = 0;

      // allocate the new map
      type->fieldMapN = labelN;
      type->fieldMapA = mem::allocZ<recd_field_map_t>(type->fieldMapN);
      
      // for each label
      for(unsigned i=0; i<labelN; ++i)
      {
        const recd_field_t* fld_desc = nullptr;
        unsigned level_idx = kInvalidIdx;

        // first attempt to locate the label in the top level record
        if((fld_desc = _find_field( type->fieldL, labelA[i], textLength(labelA[i]) )) != nullptr )
        {
          level_idx = 0;
        }
        else 
        {
          // next attempt to locate the label in the base 
          for(unsigned j=0; type->base != nullptr && j<type->base->fieldMapN; ++j)
            if( textIsEqual(type->base->fieldMapA[j].field_desc->label,labelA[i]) )
            {
              fld_desc = type->base->fieldMapA[j].field_desc;
              level_idx = type->base->fieldMapA[j].level_idx + 1;
              break;
            }
        }

        // if the label was not found
        if( fld_desc == nullptr )
        {
          cwLogError(kInvalidArgRC,"The record map field '%s' could not be found.",cwStringNullGuard(labelA[i]));
          goto errLabel;
        }

        // setup the map record
        type->fieldMapA[i].field_desc = fld_desc;
        type->fieldMapA[i].level_idx  = 0;
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Field map create failed.");
      
      return rc;
    }

    rc_t _recd_type_create_map( recd_type_t* type, const object_t* map_list_cfg )
    {
      rc_t     rc  = kOkRC;
      unsigned labelN = 0;
      char**   labelA = nullptr;

      // validate map list cfg object
      if( map_list_cfg == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"An null record type field map was encountered.");
        goto errLabel;
      }

      // verify the map list cfg is a list
      if( !map_list_cfg->is_list() )
      {
        rc = cwLogError(kInvalidArgRC,"The record type field map specifier must be a list of strings.");
        goto errLabel;        
      }

      // get the count of labels
      if((labelN = map_list_cfg->child_count()) == 0)
      {
        cwLogWarning("The record type field map list is empty.");
        goto errLabel;        
      }

      // allocate an array of strings to hold the labels
      labelA = mem::allocZ<char*>(labelN);

      for(unsigned i=0; i<labelN; ++i)
      {
        char* label = nullptr;
        const object_t* label_cfg;

        //  get the label string cfg
        if((label_cfg = map_list_cfg->child_ele(i)) == nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"The record type map list element at index %i could not be accessed.",i);
          goto errLabel;
        }

        // get the C string
        if((rc = label_cfg->value(label)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"The record type map list element at index %i could not be parsed as an integer.",i);
          goto errLabel;
        }

        // store the label
        labelA[i] = label;
      }
      
      // given the list of label create the field map
      rc = _recd_type_create_map(type,labelA,labelN);
      
    errLabel:      
      mem::release(labelA);
      return rc;
    }

  } // flow
} // cw


unsigned       cw::flow::value_print_verbosity_from_string( const char* s )
{ return labelToId(_valVerbLevelA,s,kInvalidValPrintVerb); }

const char*    cw::flow::value_print_verbosity_to_string( unsigned verbosity )
{ return idToLabel(_valVerbLevelA,verbosity,kInvalidValPrintVerb); }

cw::flow::abuf_t* cw::flow::abuf_create( srate_t srate, unsigned chN, unsigned frameN )
{
  if( chN*frameN == 0 )
  {
    cwLogError(kInvalidArgRC,"The %s audio signal parameter cannot be zero.", chN==0 ? "channel count" : "frame count");
    return nullptr;
  }
  
  abuf_t* a       = mem::allocZ<abuf_t>();
  a->srate        = srate;
  a->chN          = chN;
  a->frameN       = frameN;
  a->bufAllocSmpN = chN*frameN;

  
  a->buf          = mem::allocZ<sample_t>(a->bufAllocSmpN);
  
  return a;
}

void  cw::flow::abuf_destroy( abuf_t*& abuf )
{
  if( abuf == nullptr )
    return;
  
  mem::release(abuf->buf);
  mem::release(abuf);
}

cw::flow::abuf_t*  cw::flow::abuf_duplicate( abuf_t* dst, const abuf_t* src )
{
  abuf_t* abuf = nullptr;

  if( dst != nullptr && dst->bufAllocSmpN < src->bufAllocSmpN )
    mem::release(dst->buf);
  
  if( dst == nullptr || dst->buf == nullptr )    
    abuf = abuf_create( src->srate, src->chN, src->frameN );
  else
    abuf = dst;

  if( abuf != nullptr )
    vop::copy(abuf->buf,src->buf,src->chN*src->frameN);

  return abuf;
}

void cw::flow::abuf_zero( abuf_t* abuf )
{
  vop::zero(abuf->buf,abuf->bufAllocSmpN);
}

cw::rc_t  cw::flow::abuf_set_channel( abuf_t* abuf, unsigned chIdx, const sample_t* v, unsigned vN )
{
  rc_t rc = kOkRC;
  
  if( vN > abuf->frameN )
    rc = cwLogError(kInvalidArgRC,"Cannot copy source vector of length %i into an abuf of length %i.", vN, abuf->frameN);
  else
    if( chIdx > abuf->chN )
      rc = cwLogError(kInvalidArgRC,"The abuf destination channel %i is out of range.", chIdx);
    else
      vop::copy( abuf->buf + (chIdx*abuf->frameN), v, vN);
  
  return rc;
}

const cw::flow::sample_t*   cw::flow::abuf_get_channel( abuf_t* abuf, unsigned chIdx )
{
  assert( abuf->buf != nullptr );
  return abuf->buf + (chIdx*abuf->frameN);
}

void cw::flow::abuf_print( const abuf_t* abuf, unsigned verbosity )
{
  if( verbosity == kSilentValPrintVerb )
    return;
  
  if( abuf == nullptr )
  {
    cwLogPrint("abuf: <null>");
    return;
  }

  if( verbosity >= kMinimalValPrintVerb )
  {
    cwLogPrint("abuf: chN:%i frameN:%i srate:%8.1f ", abuf->chN, abuf->frameN, abuf->srate );

    if( abuf->chN > 0 && verbosity >= kSummaryValPrintVerb )
    {
      cwLogPrint("(");
      for(unsigned i=0; i<abuf->chN; ++i)
      {
        cwLogPrint("rms:%f ",abuf->buf==nullptr ? 0.0 : vop::rms(abuf->buf + i*abuf->frameN, abuf->frameN));

        if( verbosity >= kAllValPrintVerb && abuf->buf != nullptr )
        {
          cwLogPrint("[ ");
          for(unsigned j=0; j<abuf->frameN; ++j)
            cwLogPrint("%f ",abuf->buf[ (i*abuf->frameN)+j ] );
          cwLogPrint("]");
        }
      }
      cwLogPrint(") ");      
    }

  }  
}

cw::flow::fbuf_t* cw::flow::fbuf_create( srate_t srate, unsigned chN, const unsigned* maxBinN_V, const unsigned* binN_V, const unsigned* hopSmpN_V, const fd_sample_t** magV, const fd_sample_t** phsV, const fd_sample_t** hzV )
{
  for(unsigned i=0; i<chN; ++i)
    if( binN_V[i] > maxBinN_V[i] )
    {
      cwLogError(kInvalidArgRC,"A channel bin count (%i) execeeds the max bin count (%i).",binN_V[i],maxBinN_V[i]);
      return nullptr;;
    }
  
  fbuf_t* f = mem::allocZ<fbuf_t>();

  bool proxy_fl = magV != nullptr || phsV != nullptr || hzV != nullptr;
  
  // Calculate the total count of bins for each data vector.
  unsigned maxTotalBinN = proxy_fl ? 0 : vop::sum(maxBinN_V, chN);
  
  // allocate memory
  f->mem       = nullptr;

  f->srate     = srate;
  f->chN       = chN;
  f->maxBinN_V = mem::allocZ<unsigned>(chN);
  f->binN_V    = mem::allocZ<unsigned>(chN);
  f->hopSmpN_V = mem::allocZ<unsigned>(chN);
  f->magV      = mem::allocZ<fd_sample_t*>(chN);
  f->phsV      = mem::allocZ<fd_sample_t*>(chN);
  f->hzV       = mem::allocZ<fd_sample_t*>(chN);
  f->readyFlV  = mem::allocZ<bool>(chN);

  vop::copy( f->binN_V,    binN_V,    chN );
  vop::copy( f->maxBinN_V, maxBinN_V, chN );
  vop::copy( f->hopSmpN_V, hopSmpN_V, chN );  
  
  if( proxy_fl )
  {
    for(unsigned chIdx=0; chIdx<chN; ++chIdx)
    {      
      f->magV[ chIdx ] = (fd_sample_t*)magV[chIdx];
      f->phsV[ chIdx ] = (fd_sample_t*)phsV[chIdx];
      f->hzV[  chIdx ] = (fd_sample_t*)hzV[chIdx];
    }
  }
  else
  {
    fd_sample_t* m  = mem::allocZ<fd_sample_t>(maxTotalBinN*kFbufVectN);
    f->mem = m;
    for(unsigned chIdx=0; chIdx<chN; ++chIdx)
    {   
      f->magV[chIdx] = m + 0 * f->binN_V[chIdx];
      f->phsV[chIdx] = m + 1 * f->binN_V[chIdx];
      f->hzV[ chIdx] = m + 2 * f->binN_V[chIdx];
      m += f->maxBinN_V[chIdx];
      assert( m <= m + kFbufVectN * maxTotalBinN );
    }
  }

  return f;  
}


cw::flow::fbuf_t*  cw::flow::fbuf_create( srate_t srate, unsigned chN, unsigned maxBinN, unsigned binN, unsigned hopSmpN, const fd_sample_t** magV, const fd_sample_t** phsV, const fd_sample_t** hzV )
{
  unsigned maxBinN_V[ chN ];
  unsigned binN_V[ chN ];
  unsigned hopSmpN_V[ chN ];

  vop::fill( maxBinN_V, chN, maxBinN );
  vop::fill( binN_V, chN, binN );
  vop::fill( hopSmpN_V, chN, binN );
  return fbuf_create( srate, chN, maxBinN_V, binN_V, hopSmpN_V, magV, phsV, hzV );
}

void cw::flow::fbuf_zero( fbuf_t* f )
{
  for(unsigned i=0; i<f->chN; ++i)
  {
    vop::zero(f->magV[i],f->maxBinN_V[i]);
    vop::zero(f->phsV[i],f->maxBinN_V[i]);
    vop::zero(f->hzV[i], f->maxBinN_V[i]);
  }
}

void cw::flow::fbuf_destroy( fbuf_t*& fbuf )
{
  if( fbuf == nullptr )
    return;

  mem::release(fbuf->maxBinN_V);
  mem::release(fbuf->binN_V);
  mem::release(fbuf->hopSmpN_V);
  mem::release(fbuf->magV);
  mem::release(fbuf->phsV);
  mem::release(fbuf->hzV);
  mem::release(fbuf->readyFlV);
  
  mem::release( fbuf->mem);  
  mem::release( fbuf);


  
}

cw::flow::fbuf_t*  cw::flow::fbuf_duplicate( fbuf_t* dst, const fbuf_t* src )
{
  fbuf_t* fbuf = nullptr;
  
  if( dst != nullptr && dst->memByteN < src->memByteN )
    fbuf_destroy(dst);

  if( dst == nullptr )
    fbuf = fbuf_create( src->srate, src->chN, src->maxBinN_V, src->binN_V, src->hopSmpN_V );
  else
    fbuf = dst;
  
  for(unsigned i=0; i<fbuf->chN; ++i)
  {
    fbuf->maxBinN_V[i] = src->maxBinN_V[i];
    fbuf->binN_V[i]    = src->binN_V[i];
    fbuf->hopSmpN_V[i] = src->hopSmpN_V[i]; 

    vop::copy( fbuf->magV[i], src->magV[i], fbuf->binN_V[i] );
    vop::copy( fbuf->phsV[i], src->phsV[i], fbuf->binN_V[i] );
    vop::copy( fbuf->hzV[i],  src->hzV[i],  fbuf->binN_V[i] );    
  }
  return fbuf;
}


cw::flow::mbuf_t* cw::flow::mbuf_create( const midi::ch_msg_t* msgA, unsigned msgN )
{
  mbuf_t* m = mem::allocZ<mbuf_t>();
  m->msgA = msgA;
  m->msgN = msgN;
  return m;
}

void cw::flow::mbuf_destroy( mbuf_t*& buf )
{
  mem::release(buf);
}

void  cw::flow::fbuf_print( const fbuf_t* fbuf, unsigned verbosity )
{
  if( verbosity == kSilentValPrintVerb )
    return;
  
  if( fbuf == nullptr )
  {
    cwLogPrint("fbuf: <null>");
    return;
  }

  if( verbosity >= kMinimalValPrintVerb )
  {
    cwLogPrint("fbuf: chN:%i flags:0x%x srate:%8.1f ", fbuf->chN, fbuf->flags, fbuf->srate );

    cwLogPrint("(");
    for(unsigned i=0; i<fbuf->chN; ++i)
    {
      cwLogPrint("binN:%i hopSmpN:%i ", fbuf->binN_V[i], fbuf->hopSmpN_V[i] );

      if( verbosity >= kSummaryValPrintVerb )
      {
        float mag_mean = fbuf->magV == nullptr ? 0 : vop::mean(fbuf->magV[i], fbuf->binN_V[i]);
        cwLogPrint("mean magn:%f ",mag_mean);
            
        if( verbosity >= kAllValPrintVerb && fbuf->magV != nullptr )
        {
          cwLogPrint("[ ");
          for(unsigned j=0; j<fbuf->binN_V[i]; ++j)
            cwLogPrint("%f %f, ",fbuf->magV[i][j], fbuf->phsV[i][j] );
          cwLogPrint("]");
        }
      }
    }
    cwLogPrint(") ");
  }
  
}


cw::flow::mbuf_t* cw::flow::mbuf_duplicate( const mbuf_t* src )
{
  return mbuf_create(src->msgA,src->msgN);
}

void cw::flow::mbuf_print( const mbuf_t* mbuf, unsigned verbosity )
{
  if( verbosity == kSilentValPrintVerb )
    return;
  
  if( mbuf == nullptr )
  {
    cwLogPrint("mbuf: <null>");
    return;
  }

  if( verbosity >= kMinimalValPrintVerb )
  {
    cwLogPrint("mbuf: cnt: %i", mbuf->msgN );

    if( verbosity >= kSummaryValPrintVerb )
    {
      cwLogPrint("[ ");
      for(unsigned i=0; i<mbuf->msgN; ++i)
        cwLogPrint("(0x%x 0x%x 0x%x) ",mbuf->msgA[i].status + mbuf->msgA[i].ch,mbuf->msgA[i].d0,mbuf->msgA[i].d1);
      cwLogPrint("] ");
    }    
  }
}


cw::flow::rbuf_t* cw::flow::rbuf_create( const recd_type_t* type, const recd_t* recdA, unsigned recdN, unsigned max_recdN )
{
  rbuf_t* m = mem::allocZ<rbuf_t>();
  m->type = type;
  m->recdA = recdA;
  m->recdN = recdN;
  m->maxRecdN = max_recdN;
  return m;
}

void cw::flow::rbuf_destroy( rbuf_t*& buf )
{
  mem::release(buf);
}

cw::flow::rbuf_t* cw::flow::rbuf_duplicate( const rbuf_t* src )
{
  return rbuf_create(src->type,src->recdA,src->recdN,src->maxRecdN);
}

void  cw::flow::rbuf_setup( rbuf_t* rbuf, recd_type_t* type, recd_t* recdA, unsigned recdN, unsigned maxRecdN )
{
  rbuf->type = type;
  rbuf->recdA = recdA;
  rbuf->recdN = recdN;
  rbuf->maxRecdN = maxRecdN;
}

void cw::flow::rbuf_print( const rbuf_t* rbuf, unsigned verbosity )
{
  if( verbosity == kSilentValPrintVerb )
  {
    return;
  }

  if( rbuf == nullptr )
  {
    cwLogPrint("rbuf: <null>");
    return;
  }

    
  if( verbosity >= kMinimalValPrintVerb )
  {
    if( rbuf->recdN > 5 )
      cwLogPrint("cnt:%i ",rbuf->recdN);
    
    if( verbosity == kSummaryValPrintVerb )
    {
      if( rbuf->type != nullptr )
        recd_type_print(rbuf->type);
    }
    
    if( verbosity == kAllValPrintVerb )
    {
      for(unsigned i=0; i<rbuf->recdN; ++i)
        recd_print( rbuf->recdA + i);
    }
    
  }

  
}

unsigned cw::flow::value_type_label_to_flag( const char* s )
{
  unsigned flags = labelToId(_typeLabelFlagsA,s,kInvalidTFl);
  if( flags == kInvalidTFl )
    cwLogError(kInvalidArgRC,"Invalid type flag: '%s'",cwStringNullGuard(s));
    
  return flags;
}

const char* cw::flow::value_type_flag_to_label( unsigned flag )
{  return _typeFlagToLabel(flag); }


void cw::flow::value_release( value_t* v )
{
  if( v == nullptr )
    return;
        
  switch( v->tflag & kTypeMask )
  {
    case kInvalidTFl:
      break;
          
    case kBoolTFl:
    case kUIntTFl:
    case kIntTFl:
    case kFloatTFl:
    case kDoubleTFl:
      break;
          
    case kABufTFl:
      abuf_destroy( v->u.abuf );
      break;
          
    case kFBufTFl:
      fbuf_destroy( v->u.fbuf );
      break;

    case kMBufTFl:
      mbuf_destroy( v->u.mbuf );
      break;
          
    case kRBufTFl:
      rbuf_destroy( v->u.rbuf );
      break;
          
    case kBoolMtxTFl:
    case kUIntMtxTFl:
    case kIntMtxTFl:
    case kFloatMtxTFl:
    case kDoubleMtxTFl:
      assert(0); // not implemeneted
      break;
          
    case kStringTFl:
      mem::release( v->u.s );
      break;
          
    case kCfgTFl:
      break;

    case kMidiTFl:
      break;
      
    default:
      assert(0);
      break;
  }

  v->tflag = kInvalidTFl;
}


void cw::flow::value_duplicate( value_t& dst, const value_t& src )
{        
  switch( src.tflag & kTypeMask )
  {
    case kInvalidTFl:
      break;
          
    case kBoolTFl:
    case kUIntTFl:
    case kIntTFl:
    case kFloatTFl:
    case kDoubleTFl:
      dst = src;
      break;
          
    case kABufTFl:
          
      dst.u.abuf = src.u.abuf == nullptr ? nullptr : abuf_duplicate(dst.u.abuf,src.u.abuf);
      dst.tflag = src.tflag;
      break;
          
    case kFBufTFl:
      dst.u.fbuf = src.u.fbuf == nullptr ? nullptr : fbuf_duplicate(dst.u.fbuf,src.u.fbuf);
      dst.tflag = src.tflag;
      break;

    case kMBufTFl:
      dst.u.mbuf = src.u.mbuf == nullptr ? nullptr : mbuf_duplicate(src.u.mbuf);
      dst.tflag = src.tflag;
      break;

    case kRBufTFl:
      dst.u.rbuf = src.u.rbuf == nullptr ? nullptr : rbuf_duplicate(src.u.rbuf);
      dst.tflag = src.tflag;
      break;
          
    case kBoolMtxTFl:
    case kUIntMtxTFl:
    case kIntMtxTFl:
    case kFloatMtxTFl:
    case kDoubleMtxTFl:
      assert(0); // not implemeneted
      break;
          
    case kStringTFl:
      dst.u.s = mem::duplStr( dst.u.s );
      dst.tflag = src.tflag;
      break;
                    
    case kCfgTFl:
      dst = src;
      break;

    case kMidiTFl:
      dst.u.midi = src.u.midi;
      break;
          
    default:
      assert(0);
      break;
  }

}


cw::rc_t cw::flow::value_from_cfg( const object_t* cfg, value_t& value_ref )
{
  rc_t rc = kOkRC;
      
  switch( cfg->type->id )
  {
    case kCharTId:  
    case kUInt8TId:
    case kUInt16TId:
    case kUInt32TId:
      value_ref.tflag = kUIntTFl;
      if((rc = cfg->value(value_ref.u.u)) != kOkRC )
        rc = cwLogError(rc,"Conversion to uint failed.");
      break;
          
    case kInt8TId:
    case kInt16TId:
    case kInt32TId:
      value_ref.tflag = kIntTFl;
      if((rc = cfg->value(value_ref.u.i)) != kOkRC )
        rc = cwLogError(rc,"Conversion to int failed.");
      break;
          
    case kInt64TId:
    case kUInt64TId:
      rc = cwLogError(kInvalidArgRC,"The flow system does not currently implement 64bit integers.");
      goto errLabel;
      break;
          
    case kFloatTId:
      value_ref.tflag = kFloatTFl;
      if((rc = cfg->value(value_ref.u.f)) != kOkRC )
        rc = cwLogError(rc,"Conversion to float failed.");
      break;
          
    case kDoubleTId:
      value_ref.tflag = kDoubleTFl;
      if((rc = cfg->value(value_ref.u.d)) != kOkRC )
        rc = cwLogError(rc,"Conversion to double failed.");
      break;
          
    case kBoolTId:
      value_ref.tflag = kBoolTFl;
      if((rc = cfg->value(value_ref.u.b)) != kOkRC )
        rc = cwLogError(rc,"Conversion to bool failed.");
      break;
          
    case kStringTId:
    case kCStringTId:
      value_ref.tflag = kStringTFl;
      if((rc = cfg->value(value_ref.u.s)) != kOkRC )
        rc = cwLogError(rc,"Conversion to string failed.");
      break;
          
    default:
      value_ref.tflag = kCfgTFl;
      value_ref.u.cfg = cfg;
        
  }
errLabel:

  return rc;
}

cw::rc_t cw::flow::value_from_value( const value_t& src, value_t& dst )
{
  rc_t rc = kOkRC;
  
  if( dst.tflag == kInvalidTFl || dst.tflag & src.tflag)
  {
    dst = src;
    return kOkRC;
  }

  // we only get here if conversion is necessary
  
  switch( src.tflag )
  {
    case kInvalidTFl:
      rc = cwLogError(kInvalidStateRC,"The src operand does not have a valid type.");
      break;
      
    case kBoolTFl:
      rc = value_set(&dst,src.u.b);
      break;
      
    case kUIntTFl:
      rc = value_set(&dst,src.u.u);
      break;
      
    case kIntTFl:
      rc = value_set(&dst,src.u.i);
      break;
      
    case kFloatTFl:
      rc = value_set(&dst,src.u.f);
      break;
      
    case kDoubleTFl:
      rc = value_set(&dst,src.u.d);
      break;
      
    case kBoolMtxTFl:
    case kUIntMtxTFl:
    case kIntMtxTFl:
    case kFloatMtxTFl:
    case kDoubleMtxTFl:
      rc = cwLogError(kNotImplementedRC,"Matrix conversion is not implemented for value to value conversion.");
      break;
      
    case kABufTFl:
    case kFBufTFl:
    case kMBufTFl:
    case kRBufTFl:
    case kStringTFl:
    case kCfgTFl:
    case kMidiTFl:
      rc = cwLogError(kOpFailRC,"Value conversion failed during value to value assignement.");
      break;
      
    default:
      rc = cwLogError(kInvalidArgRC,"An unknown source operand data type 0x%x was encountered.",src.tflag);
    
  }
  return rc;
}


void cw::flow::value_print( const value_t* v, bool label_fl, unsigned verbosity )
{
  if( v == nullptr )
    return;

  if( verbosity == kSilentValPrintVerb )
    return;
        
  switch( v->tflag & kTypeMask )
  {
    case kInvalidTFl:
      cwLogPrint("<invalid>");
      break;
          
    case kBoolTFl:          
      cwLogPrint("%s%s", label_fl ? "b:" : "", v->u.b ? "true" : "false" );
      break;
          
    case kUIntTFl:
      cwLogPrint("%s%i", label_fl ? "u:" : "", v->u.u );
      break;
          
    case kIntTFl:
      cwLogPrint("%s%i", label_fl ? "i:" : "", v->u.i );
      break;
          
    case kFloatTFl:
      cwLogPrint("%s%f", label_fl ? "f:" : "", v->u.f );
      break;
          
    case kDoubleTFl:
      cwLogPrint("%s%f", label_fl ? "d:" : "", v->u.d );
      break;
          
    case kABufTFl:
      abuf_print(v->u.abuf,verbosity);
      break;
          
    case kFBufTFl:
      fbuf_print(v->u.fbuf,verbosity);
      break;

    case kMBufTFl:
      mbuf_print(v->u.mbuf,verbosity);
      break;


    case kRBufTFl:
      rbuf_print(v->u.rbuf,verbosity);
      break;
          
    case kBoolMtxTFl:
    case kUIntMtxTFl:
    case kIntMtxTFl:
    case kFloatMtxTFl:
    case kDoubleMtxTFl:
      assert(0); // not implemeneted
      break;
          
    case kStringTFl:
      cwLogPrint("%s%s", label_fl?"s:" : "", v->u.s);
      break;
           
    case kCfgTFl:
      
      if( label_fl )
        cwLogPrint("c:");
      if( v->u.cfg != nullptr )
        v->u.cfg->print();
      break;

    case kMidiTFl:
      if( label_fl )
        cwLogPrint("m:");
      
      if( v->u.midi != nullptr )
        cwLogPrint("dev:%i port:%i uid:%i ch:%i st:0x%x d0:0x%x d1:0x%x",v->u.midi->devIdx,v->u.midi->portIdx,v->u.midi->uid,v->u.midi->ch,v->u.midi->status,v->u.midi->d0,v->u.midi->d1);
      break;
          
    default:
      assert(0);
      break;
  }

}

bool cw::flow::value_supports_an_ele_count( const value_t* v )
{
  if( v == nullptr )
    return 0;

  switch( v->tflag & kTypeMask )
  {
    case kInvalidTFl:
    case kBoolTFl:          
    case kUIntTFl:
    case kIntTFl:
    case kFloatTFl:
    case kDoubleTFl:
      return false;
          
    case kABufTFl:
    case kFBufTFl:
    case kMBufTFl:
    case kRBufTFl:
      return true;
          
    case kBoolMtxTFl:
    case kUIntMtxTFl:
    case kIntMtxTFl:
    case kFloatMtxTFl:
    case kDoubleMtxTFl:
      assert(0); // not implemeneted
      return true;
          
    case kStringTFl:
      return false;
           
    case kCfgTFl:
      return true;

    case kMidiTFl:
      return false;
          
    default:
      assert(0);
      break;
  }

  return false;
}


bool cw::flow::value_has_elements_now( const value_t* v )
{
  if( v == nullptr )
    return 0;

  switch( v->tflag & kTypeMask )
  {
    case kInvalidTFl:
    case kBoolTFl:          
    case kUIntTFl:
    case kIntTFl:
    case kFloatTFl:
    case kDoubleTFl:
      return false;
          
    case kABufTFl:
      return v->u.abuf != nullptr && v->u.abuf->chN * v->u.abuf->frameN > 0;
          
    case kFBufTFl:
      if(  v->u.fbuf == nullptr  )
        return false;
        
      for(unsigned i=0; i<v->u.fbuf->chN; ++i)
        if( v->u.fbuf->binN_V[i] > 0 )
          return true;
        
      return false;
      

    case kMBufTFl:
      return v->u.mbuf != nullptr && v->u.mbuf->msgN != 0;


    case kRBufTFl:
      return v->u.rbuf != nullptr && v->u.rbuf->recdN != 0;
          
    case kBoolMtxTFl:
    case kUIntMtxTFl:
    case kIntMtxTFl:
    case kFloatMtxTFl:
    case kDoubleMtxTFl:
      assert(0); // not implemeneted
      return false;
          
    case kStringTFl:
      return false;
           
    case kCfgTFl:
      // Empty containers return false
      return v->u.cfg != nullptr && ((v->u.cfg->is_container() && v->u.cfg->child_count()>0) || !v->u.cfg->is_container());

    case kMidiTFl:
      return false;
          
    default:
      assert(0);
      break;
  }

  return 0;
}

bool cw::flow::value_can_auto_notify( const value_t* v )
{
  if( v == nullptr )
    return 0;

  switch( v->tflag & kTypeMask )
  {
    case kInvalidTFl:
    case kBoolTFl:          
    case kUIntTFl:
    case kIntTFl:
    case kFloatTFl:
    case kDoubleTFl:
      return true;
      
    case kABufTFl:
    case kFBufTFl:
      return false;
      
    case kMBufTFl:
    case kRBufTFl:
      return false;
          
    case kBoolMtxTFl:
    case kUIntMtxTFl:
    case kIntMtxTFl:
    case kFloatMtxTFl:
    case kDoubleMtxTFl:
      assert(0); // not implemeneted
      return false;
          
    case kStringTFl:
      return true;
           
    case kCfgTFl:
      return false;

    case kMidiTFl:
      return false;
          
    default:
      assert(0);
      break;
  }

  return false;
}



cw::rc_t cw::flow::value_get( const value_t* val, bool& valRef )
{
  rc_t rc = kOkRC;
  switch( val->tflag & kTypeMask )
  {
    case kBoolTFl:   valRef = val->u.b; break;
    case kUIntTFl:   valRef = val->u.u!=0; break;
    case kIntTFl:    valRef = val->u.i!=0; break;
    case kFloatTFl:  valRef = val->u.f!=0; break;
    case kDoubleTFl: valRef = val->u.d!=0; break;        
    default:
      rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a bool.",_typeFlagToLabel(val->tflag),val->tflag);
  }
  return rc;
}

cw::rc_t cw::flow::value_set( value_t* val, bool v )
{
  rc_t rc = kOkRC;
      
  switch( val->tflag & kTypeMask )
  {
    case kBoolTFl:   val->u.b=v; break;
    case kUIntTFl:   val->u.u=v; break;
    case kIntTFl:    val->u.i=v; break;
    case kFloatTFl:  val->u.f=v; break;
    case kDoubleTFl: val->u.d=v; break;
    case kInvalidTFl:
      val->u.b   = v;
      val->tflag = kBoolTFl;
      break;

    default:
      rc = cwLogError(kTypeMismatchRC,"A bool could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
  }
      
  return rc;
}
    
cw::rc_t cw::flow::value_get( const value_t* val, uint_t& valRef )
{
  rc_t rc = kOkRC;
  switch( val->tflag & kTypeMask )
  {
    case kBoolTFl:   valRef = val->u.b ? 1 : 0; break;
    case kUIntTFl:   valRef = val->u.u; break;
    case kIntTFl:    valRef = val->u.i; break;
    case kFloatTFl:  valRef = (uint_t)val->u.f; break;
    case kDoubleTFl: valRef = (uint_t)val->u.d; break;
    default:
      rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a uint.",_typeFlagToLabel(val->tflag),val->tflag);
  }
  return rc;
}

cw::rc_t cw::flow::value_set( value_t* val, uint_t v )
{
  rc_t rc = kOkRC;
      
  switch( val->tflag & kTypeMask )
  {
    case kBoolTFl:   val->u.b=v!=0; break;
    case kUIntTFl:   val->u.u=v; break;
    case kIntTFl:    val->u.i=v; break;
    case kFloatTFl:  val->u.f=v; break;
    case kDoubleTFl: val->u.d=v; break;
    case kInvalidTFl:
      val->u.u  = v;
      val->tflag = kUIntTFl;
      break;

    default:
      rc = cwLogError(kTypeMismatchRC,"A uint could not be converted to a  %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
  }
      
  return rc;
}
    
cw::rc_t cw::flow::value_get( const value_t* val, int_t& valRef )
{
  rc_t rc = kOkRC;
  switch( val->tflag & kTypeMask )
  {
    case kBoolTFl:   valRef = val->u.b ? 1 : 0; break;
    case kUIntTFl:   valRef = (int_t)val->u.u; break;
    case kIntTFl:    valRef = val->u.i; break;
    case kFloatTFl:  valRef = (int_t)val->u.f; break;
    case kDoubleTFl: valRef = (int_t)val->u.d; break;
    default:
      rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to an int.",_typeFlagToLabel(val->tflag),val->tflag);
          
  }
  return rc;
}

cw::rc_t cw::flow::value_set( value_t* val, int_t v )
{
  rc_t rc = kOkRC;
      
  switch( val->tflag & kTypeMask )
  {
    case kBoolTFl:   val->u.b=v!=0; break;
    case kUIntTFl:   val->u.u=v; break;
    case kIntTFl:    val->u.i=v; break;
    case kFloatTFl:  val->u.f=v; break;
    case kDoubleTFl: val->u.d=v; break;
    case kInvalidTFl:
      val->u.i   = v;
      val->tflag = kIntTFl;
      break;

    default:
      rc = cwLogError(kTypeMismatchRC,"An int could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
  }
      
  return rc;
}
    

cw::rc_t cw::flow::value_get( const value_t* val, float& valRef )
{
  rc_t rc = kOkRC;
  switch( val->tflag & kTypeMask )
  {
    case kBoolTFl:   valRef = val->u.b ? 1 : 0; break;
    case kUIntTFl:   valRef = (float)val->u.u; break;
    case kIntTFl:    valRef = (float)val->u.i; break;
    case kFloatTFl:  valRef = (float)val->u.f; break;
    case kDoubleTFl: valRef = (float)val->u.d; break;
    default:
      rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a float.",_typeFlagToLabel(val->tflag),val->tflag);
  }
  return rc;
}

cw::rc_t cw::flow::value_set( value_t* val, float v )
{
  rc_t rc = kOkRC;
      
  switch( val->tflag & kTypeMask )
  {
    case kBoolTFl:   val->u.b=v!=0; break;
    case kUIntTFl:   val->u.u=(unsigned)v; break;
    case kIntTFl:    val->u.i=(int)v; break;
    case kFloatTFl:  val->u.f=v; break;
    case kDoubleTFl: val->u.d=v; break;
    case kInvalidTFl:
      val->u.f   = v;
      val->tflag = kFloatTFl;
      break;

    default:
      rc = cwLogError(kTypeMismatchRC,"A float could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
  }
      
  return rc;
}
    
cw::rc_t cw::flow::value_get( const value_t* val, double& valRef )
{
  rc_t rc = kOkRC;
  switch( val->tflag & kTypeMask )
  {
    case kBoolTFl:   valRef = val->u.b ? 1 : 0; break;
    case kUIntTFl:   valRef = (double)val->u.u; break;
    case kIntTFl:    valRef = (double)val->u.i; break;
    case kFloatTFl:  valRef = (double)val->u.f; break;
    case kDoubleTFl: valRef =         val->u.d; break;
    default:
      rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a double.",_typeFlagToLabel(val->tflag),val->tflag);
  }
  return rc;
}

cw::rc_t cw::flow::value_set( value_t* val, double v )
{
  rc_t rc = kOkRC;
      
  switch( val->tflag & kTypeMask )
  {
    case kBoolTFl:   val->u.b=v!=0; break;
    case kUIntTFl:   val->u.u=(unsigned)v; break;
    case kIntTFl:    val->u.i=(int)v; break;
    case kFloatTFl:  val->u.f=(float)v; break;
    case kDoubleTFl: val->u.d=v; break;
    case kInvalidTFl:
      val->u.d   = v;
      val->tflag = kDoubleTFl;
      break;

    default:
      rc = cwLogError(kTypeMismatchRC,"A double could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
  }
      
  return rc;
}
    
cw::rc_t cw::flow::value_get( const value_t* val, const char*& valRef )
{
  rc_t rc = kOkRC;
  if( cwIsFlag(val->tflag & kTypeMask, kStringTFl) )
    valRef = val->u.s;
  else
  {
    rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a string.",_typeFlagToLabel(val->tflag),val->tflag);        
    valRef = nullptr;
  }
      
  return rc;
}

cw::rc_t cw::flow::value_set( value_t* val, const char* v )
{
  rc_t rc = kOkRC;
      
  switch( val->tflag & kTypeMask )
  {
    case kStringTFl:
      val->u.s=mem::duplStr(v);
      break;
          
    case kInvalidTFl:
      val->u.s   = mem::duplStr(v);
      val->tflag = kStringTFl;
      break;
      
    default:
      rc = cwLogError(kTypeMismatchRC,"A string could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
  }
      
  return rc;
}
    
cw::rc_t cw::flow::value_get( value_t* val, abuf_t*& valRef )
{
  rc_t rc = kOkRC;
  if( cwIsFlag(val->tflag & kTypeMask, kABufTFl) )
    valRef = val->u.abuf;
  else
  {
    rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to an abuf.",_typeFlagToLabel(val->tflag),val->tflag);        
    valRef = nullptr;
  }
  return rc;
}

cw::rc_t cw::flow::value_get( value_t* val, const abuf_t*& valRef )
{
  abuf_t* non_const_val;
  rc_t rc = kOkRC;
  if((rc = value_get(val,non_const_val)) == kOkRC )
    valRef = non_const_val;
  return rc;        
}

cw::rc_t cw::flow::value_set( value_t* val, abuf_t* v )
{
  rc_t rc = kOkRC;
      
  switch( val->tflag & kTypeMask )
  {
    case kABufTFl:
      val->u.abuf=v;
      break;
          
    case kInvalidTFl:
      val->u.abuf=v;
      val->tflag = kABufTFl;
      break;

    default:
      rc = cwLogError(kTypeMismatchRC,"A audio signal could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
  }
      
  return rc;
}
        
cw::rc_t cw::flow::value_get( value_t* val, fbuf_t*& valRef )
{
  rc_t rc = kOkRC;
  if( cwIsFlag(val->tflag & kTypeMask, kFBufTFl) )
    valRef = val->u.fbuf;
  else
  {
    valRef = nullptr;
    rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to an fbuf.",_typeFlagToLabel(val->tflag),val->tflag);        
  }
  return rc;
}

cw::rc_t cw::flow::value_get( value_t* val, const fbuf_t*& valRef )
{
  fbuf_t* non_const_val;
  rc_t rc = kOkRC;
  if((rc = value_get(val,non_const_val)) == kOkRC )
    valRef = non_const_val;
  return rc;        
}

cw::rc_t cw::flow::value_set( value_t* val, fbuf_t* v )
{
  rc_t rc = kOkRC;
      
  switch( val->tflag & kTypeMask )
  {
    case kFBufTFl:
      val->u.fbuf=v;
      break;
          
    case kInvalidTFl:
      val->u.fbuf=v;
      val->tflag = kFBufTFl;
      break;

    default:
      rc = cwLogError(kTypeMismatchRC,"A spectrum signal could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
  }
      
  return rc;
}
        
cw::rc_t cw::flow::value_get( value_t* val, mbuf_t*& valRef )
{
  rc_t rc = kOkRC;
  if( cwIsFlag(val->tflag & kTypeMask, kMBufTFl) )
    valRef = val->u.mbuf;
  else
  {
    valRef = nullptr;
    rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to an mbuf.",_typeFlagToLabel(val->tflag),val->tflag);        
  }
  return rc;
}

cw::rc_t cw::flow::value_get( value_t* val, const mbuf_t*& valRef )
{
  mbuf_t* non_const_val;
  rc_t rc = kOkRC;
  if((rc = value_get(val,non_const_val)) == kOkRC )
    valRef = non_const_val;
  return rc;        
}


cw::rc_t cw::flow::value_set( value_t* val, mbuf_t* v )
{
  rc_t rc = kOkRC;
      
  switch( val->tflag & kTypeMask )
  {
    case kMBufTFl:
      val->u.mbuf=v;
      break;
          
    case kInvalidTFl:
      val->u.mbuf=v;
      val->tflag = kMBufTFl;
      break;

    default:
      rc = cwLogError(kTypeMismatchRC,"A MIDI signal could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
  }
      
  return rc;
}


cw::rc_t cw::flow::value_get( value_t* val, rbuf_t*& valRef )
{
  rc_t rc = kOkRC;
  if( cwIsFlag(val->tflag & kTypeMask, kRBufTFl) )
    valRef = val->u.rbuf;
  else
  {
    valRef = nullptr;
    rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to an rbuf.",_typeFlagToLabel(val->tflag),val->tflag);        
  }
  return rc;
}

cw::rc_t cw::flow::value_get( value_t* val, const rbuf_t*& valRef )
{
  rbuf_t* non_const_val;
  rc_t rc = kOkRC;
  if((rc = value_get(val,non_const_val)) == kOkRC )
    valRef = non_const_val;
  return rc;        
}

cw::rc_t cw::flow::value_set( value_t* val, rbuf_t* v )
{
  rc_t rc = kOkRC;
      
  switch( val->tflag & kTypeMask )
  {
    case kRBufTFl:
      val->u.rbuf=v;
      break;
          
    case kInvalidTFl:
      val->u.rbuf=v;
      val->tflag = kRBufTFl;
      break;

    default:
      rc = cwLogError(kTypeMismatchRC,"A recd-buf could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
  }
      
  return rc;
}
    
    
cw::rc_t cw::flow::value_get( value_t* val, const object_t*& valRef )
{
  rc_t rc = kOkRC;
      
  if( cwIsFlag(val->tflag & kTypeMask, kCfgTFl) )
    valRef = val->u.cfg;
  else
  {
    valRef = nullptr;
    rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a cfg.",_typeFlagToLabel(val->tflag),val->tflag);        
        
  }
  return rc;        
}
    
cw::rc_t cw::flow::value_set( value_t* val, const object_t* v )
{
  rc_t rc = kOkRC;
      
  switch( val->tflag & kTypeMask )
  {
    case kCfgTFl:
      val->u.cfg=v;
      break;
          
    case kInvalidTFl:
      val->u.cfg=v;
      val->tflag = kCfgTFl;
      break;

    default:
      rc = cwLogError(kTypeMismatchRC,"A cfg. could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
  }
      
  return rc;
}


cw::rc_t cw::flow::value_get( const value_t* val, midi::ch_msg_t*& valRef )
{
  rc_t rc = kOkRC;
      
  if( cwIsFlag(val->tflag & kTypeMask, kMidiTFl) )
    valRef = val->u.midi;
  else
  {
    valRef = nullptr;
    rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a MIDI record.",_typeFlagToLabel(val->tflag),val->tflag);        
        
  }
  return rc;        
}

cw::rc_t cw::flow::value_get( const value_t* val, const midi::ch_msg_t*& valRef )
{
  rc_t rc = kOkRC;
      
  if( cwIsFlag(val->tflag & kTypeMask, kMidiTFl) )
    valRef = val->u.midi;
  else
  {
    valRef = nullptr;
    rc = cwLogError(kTypeMismatchRC,"The type %s (0x%x) could not be converted to a MIDI record.",_typeFlagToLabel(val->tflag),val->tflag);        
        
  }
  return rc;        
}

cw::rc_t cw::flow::value_set(value_t* val, midi::ch_msg_t* v )
{
  rc_t rc = kOkRC;
      
  switch( val->tflag & kTypeMask )
  {
    case kMidiTFl:
      val->u.midi=v;
      break;
          
    case kInvalidTFl:
      val->u.midi=v;
      val->tflag = kMidiTFl;
      break;

    default:
      rc = cwLogError(kTypeMismatchRC,"A MIDI record could not be converted to a %s (0x%x).",_typeFlagToLabel(val->tflag),val->tflag);          
  }
      
  return rc;  
}

cw::rc_t cw::flow::value_get( const value_t* val, value_t& valRef )
{
  if( val == nullptr )
    return cwLogError(kInvalidArgRC,"The source value reference is null.");
  
  return value_from_value( *val, valRef );
}

cw::rc_t cw::flow::value_get( value_t* val, value_t& valRef )
{
  return value_get((const value_t*)val, valRef );
}

cw::rc_t cw::flow::value_set( value_t* val, const value_t& valRef )
{
  if( val == nullptr )
    return cwLogError(kInvalidArgRC,"The source value reference is null.");
  
  return value_from_value( valRef, *val );
}



//------------------------------------------------------------------------------------------------------------------------
//
// Record
//


cw::rc_t cw::flow::  recd_format_create( recd_fmt_t*& recd_fmt_ref, const object_t* cfg, unsigned dflt_alloc_cnt, const recd_type_t* base_type )
{
  rc_t         rc        = kOkRC;
  recd_fmt_t*  recd_fmt  = nullptr;
  
  recd_fmt_ref = nullptr;

  recd_fmt = mem::allocZ<recd_fmt_t>();

  if( cfg->find( "fields" ) != nullptr )
    if((rc = recd_type_create(recd_fmt->recd_type,base_type,cfg)) != kOkRC )
      goto errLabel;

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
    recd_type_destroy(recd_fmt_ref->recd_type);
    mem::release(recd_fmt_ref);
  }
}


cw::rc_t  cw::flow::recd_type_create( recd_type_t*& recd_type_ref, const recd_type_t* base, const object_t* cfg )
{
  rc_t            rc          = kOkRC;  
  const object_t* fields_dict = nullptr;;

  recd_type_t* recd_type = mem::allocZ<recd_type_t>();
  
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
    if((rc = _recd_field_list_from_cfg(recd_type->fieldL,fields_dict)) != kOkRC )
    {
      goto errLabel;
    }

    // assign the index to the value fields and update recd_type.fieldN
    recd_type->fieldN =  _recd_field_list_set_index(recd_type->fieldL, 0 );
  }

  // if a base was given
  if( base != nullptr )
  {
    const recd_field_t* f;
    if((f = _get_duplicate_field( base->fieldL, recd_type->fieldL )) != nullptr )
    {
      rc = cwLogError(kInvalidStateRC,"The field '%s' is duplicated in the record type.",cwStringNullGuard(f->label));
      goto errLabel;
    }
  }
  
  recd_type->base = base;
  recd_type_ref = recd_type;

  // create the default field map
  if((rc = _recd_type_create_default_map( recd_type )) != kOkRC )
  {
    goto errLabel;
  }

errLabel:
  if( rc != kOkRC )
  {
    rc = cwLogError(rc,"recd_type create failed.");
    recd_type_destroy(recd_type);
  }
  
  return rc;
}


cw::rc_t  cw::flow::recd_type_create_from_map( recd_type_t*& recd_type_ref, const recd_type_t* base_type, const object_t* field_map_cfg )
{
  rc_t         rc               = kOkRC;
  recd_type_t* rt               = nullptr;
  
  recd_type_ref = nullptr;

  if( field_map_cfg == nullptr )
    return cwLogError(kInvalidArgRC,"A null record type field map was encountered.");
  
  if( base_type == nullptr )
    return cwLogError(kInvalidArgRC,"A record type cannot be derived from a map without being given an explicit base type.");

  // create a type with only a base
  if((rc = recd_type_create(rt,base_type,nullptr)) != kOkRC )
    return rc;
  
  unsigned          mapN      = field_map_cfg->child_count();  
  recd_field_map_t* fieldMapA = mem::allocZ<recd_field_map_t>(mapN);

  for(unsigned i=0; i<mapN; ++i)
  {
    const object_t*         pair_cfg      = field_map_cfg->child_ele(i);
    const char*             i_field_label = nullptr;
    const recd_field_map_t* i_field_map   = nullptr;
    const char*             o_field_label = nullptr;
    const recd_field_map_t* o_field_map   = nullptr;
    const recd_field_t*     field_desc    = nullptr;

    // validate the syntax of the input/output field pair
    if( pair_cfg == nullptr || !pair_cfg->is_pair() || pair_cfg->pair_label() == nullptr || pair_cfg->pair_value() == nullptr || !pair_cfg->pair_value()->is_string() )
    {
      cwLogError(kSyntaxErrorRC,"A input/output mapping pair could not be parsed at index %i.",i);
      goto errLabel;
    }

    // get the input record field label
    i_field_label = pair_cfg->pair_label();
          
    // get the output record field label
    if((rc = pair_cfg->pair_value()->value(o_field_label)) != kOkRC )
    {
      cwLogError(kSyntaxErrorRC,"The output map field label could not be parsed at index:%i.",i);
      goto errLabel;
    }

    // verify that the output field label is not already in use
    for(unsigned j=0; j<i; ++j)
      if( textIsEqual(o_field_label,fieldMapA[j].field_desc->label) )
      {
        rc = cwLogError(kSyntaxErrorRC,"The output field label '%s' was used multiple times.",cwStringNullGuard(o_field_label));
        goto errLabel;
      }

    // get the input field desc.
    if((i_field_map = recd_type_field_label_to_map(base_type,i_field_label)) == nullptr )
    {
      cwLogError(kEleNotFoundRC,"The input field named '%s' could not be found.",cwStringNullGuard(i_field_label));
      goto errLabel;      
    }

    // if the input field name matches the output field name then we can use the existing field_desc 
    if(textIsEqual(o_field_label,i_field_label))
      field_desc = i_field_map->field_desc;
    else
    {
      // duplicate the input field desc but use the output field name
      recd_field_t* rf  = mem::allocZ<recd_field_t>();
      rf->label         = mem::duplStr(o_field_label);
      rf->uid           = id_table::get_id(rf->label); 
      rf->src_uid       = id_table::get_id(i_field_map->field_desc->label);
      rf->value         = i_field_map->field_desc->value;
      rf->doc           = mem::duplStr(i_field_map->field_desc->doc);
      rf->val_idx       = kInvalidIdx; // this field will never be directly accessed. i_field_map->field_desc->val_idx; // use the input val index as this val index
      rf->link          = rt->fieldL;
      rt->fieldL        = rf;
      rt->fieldN       += 1;

      field_desc = rf;
    }

    fieldMapA[i].field_desc = field_desc;
    fieldMapA[i].level_idx  = i_field_map->level_idx + 1;
    
  }
  
  mem::release(rt->fieldMapA);
  rt->fieldMapA = fieldMapA;
  rt->fieldMapN = mapN;
  rt->read_only_fl = true;
  recd_type_ref = rt;
  
errLabel:
  if( rc != kOkRC )
  {
    recd_type_destroy(rt);
    mem::release(fieldMapA);
  }
  
  return rc;  
}


void  cw::flow::recd_type_destroy( recd_type_t*& recd_type_ref )
{
  if( recd_type_ref == nullptr )
    return;

  _recd_type_destroy_field_list(recd_type_ref->fieldL);
  mem::release(recd_type_ref->fieldMapA);
  mem::release(recd_type_ref);
}

cw::rc_t cw::flow::recd_type_apply_map( recd_type_t* recd_type, const char* const * labelA, unsigned labelN )
{
  return _recd_type_create_map(recd_type, labelA, labelN );
}

cw::rc_t cw::flow::recd_type_apply_map( recd_type_t* recd_type, const object_t* map_list_cfg )
{
  return _recd_type_create_map(recd_type, map_list_cfg );
}

cw::flow::recd_field_map_t* cw::flow::recd_type_field_label_to_map( const recd_type_t* recd_type, const char* field_label )
{
  for(unsigned i=0; i<recd_type->fieldMapN; ++i)
    if( textIsEqual(recd_type->fieldMapA[i].field_desc->label,field_label))
      return recd_type->fieldMapA + i;
  return nullptr;
}

unsigned cw::flow::recd_type_max_field_count( const recd_type_t* recd_type )
{
  unsigned n = 0;
  for(const recd_type_t* t = recd_type; t!=nullptr; t=t->base)
    n += t->fieldN;
  return n;
}

unsigned cw::flow::recd_type_field_index( const recd_type_t* recd_type, const char* field_label )
{
  unsigned field_idx;
  if((field_idx = _field_label_to_index( recd_type, field_label )) == kInvalidIdx )
  {
    cwLogError(kEleNotFoundRC,"The record field label '%s' was not found.",cwStringNullGuard(field_label));
    recd_type_print(recd_type);    
  }

  return field_idx;
}

cw::rc_t  cw::flow::recd_type_field_index( const recd_type_t* recd_type, const char* field_label, unsigned& field_idx_ref )
{
  rc_t rc = kOkRC;

  if((field_idx_ref = recd_type_field_index(recd_type,field_label)) == kInvalidIdx )
  {
    rc = kEleNotFoundRC;
  }
  return rc;
}


unsigned cw::flow::recd_type_field_index_silent( const recd_type_t* recd_type, const char* field_label )
{ return _field_label_to_index(recd_type, field_label ); }


const char* cw::flow::recd_type_field_index_to_label( const recd_type_t* recd_type, unsigned field_idx )
{
  const recd_field_t* f = _find_field_from_index( recd_type, field_idx );

  return f==nullptr ? nullptr : f->label;
}

bool cw::flow::recd_fields_data_types_are_equivalent( const recd_field_t* fd0, const recd_field_t* fd1 )
{
  return fd0->value.tflag == fd1->value.tflag;
}


bool cw::flow::recd_types_are_equivalent( const recd_type_t* rt0, const recd_type_t* rt1 )
{
  unsigned n0 = 0;
  unsigned n1 = 0;
  
  if( rt0 == nullptr && rt1 == nullptr )
    return true;
  
  if( rt0 == nullptr || rt1 == nullptr )
    return false;

  if( rt0->fieldMapN != rt1->fieldMapN )
    return false;

  for(unsigned i=0; i<rt0->fieldMapN; ++i)
  {
    if( !_fields_are_equivalent(rt0->fieldMapA[i].field_desc,rt1->fieldMapA[i].field_desc) )
      return false;
  }

  return true;
}


namespace cw
{
  namespace flow
  {
    void _recd_type_print_alt( const recd_type_t* rt, unsigned i, unsigned level )
    {
      if( rt == nullptr )
        return;

      for(const recd_field_t* rf = rt->fieldL; rf!=nullptr; rf=rf->link)
      {
        _recd_type_print_field( i++, level, rf );
        cwLogPrint("\n");
      }
      
      _recd_type_print_alt(rt->base,i,level+1);

    }
  }
}

void cw::flow::recd_type_print( const recd_type_t* recd_type )
{
  /*
  //_recd_type_print(0,recd_type);
  for(unsigned i=0; i<recd_type->fieldMapN; ++i)
  {
    const recd_field_map_t* fm = recd_type->fieldMapA + i;
    _recd_type_print_field( i, fm->level_idx, fm->field_desc );
    //_recd_type_print_field( i, -1, fm->field_desc );
    cwLogPrint("\n");
  }
  */
  _recd_type_print_alt(recd_type,0,0);
}

namespace cw
{
  namespace flow
  {
    void _recd_type_print(const recd_t* r, unsigned i, unsigned level )
    {
      if( r == nullptr )
        return;

      for(const recd_field_t* rf = r->type->fieldL; rf!=nullptr; rf=rf->link)
      {
        _recd_type_print_field( i++, level, rf );
        cwLogPrint("\n");
      }
      
      _recd_type_print(r->base,i,level+1);

    }
  }
}

void cw::flow::recd_type_print( const recd_t* r )
{
  _recd_type_print(r,0,0);
}

cw::rc_t cw::flow::recd_set_value( const recd_t* base, recd_t* recd, unsigned field_idx, const value_t& val )
{
  if( recd->type->read_only_fl )
    return cwLogError(kInvalidStateRC,"Attempt to write to a read-only record.");
  
  if( field_idx >= recd->type->fieldN )
    return cwLogError(kInvalidArgRC,"Only 'local' record value may be set.");
  
  // set the base of this record
  recd_set_base(recd,base);
  
  return value_from_value(val, recd->valA[field_idx]);
}    


cw::rc_t cw::flow::recd_init( const recd_type_t* recd_type, const recd_t* base, recd_t* r )
{
  r->type = recd_type;
  r->base = base;
  
  return _recd_set_default_value( recd_type->fieldL, r );
  
}

void cw::flow::recd_print( const recd_t* r )
{ 
  //recd_type_print(r->type);
  return _recd_print(  r ); 
}
/*
re  :
(lvl:0 : fidx:0 vidx:-1 u: 17 su: 16 f1:u:0)
(lvl:0 : fidx:1 vidx:-1 u: 16 su: 17 f0:u:1)
(lvl:2 : fidx:2 vidx: 0 u: 16 su: -1 f0:u:0)
(lvl:2 : fidx:3 vidx: 1 u: 17 su: -1 f1:u:1)
(lvl:2 : fidx:4 vidx: 2 u: 19 su: -1 f2:f:0.000000)
(lvl:2 : fidx:5 vidx: 3 u: 18 su: -1 sel:u:-1)
(f0=1,f1=10,sel=2)

 */
/*
re  :
(lvl:0 : fidx:0 vidx:-1 f1:u:0)
(lvl:0 : fidx:1 vidx:-1 f0:u:1)
(lvl:2 : fidx:2 vidx:0 f0:u:0)
(lvl:2 : fidx:3 vidx:1 f1:u:1)
(lvl:2 : fidx:4 vidx:2 f2:f:0.000000)
(lvl:2 : fidx:5 vidx:3 sel:u:-1)
(f0=1,f1=10,sel=2)
*/
/*
(lvl:0 : fidx:0 vidx: 0 u:  4 su: -1 loc:u:-1)
(lvl:0 : fidx:1 vidx: 1 u:  5 su: -1 meas:u:-1)
(lvl:0 : fidx:2 vidx: 2 u: 12 su: -1 score_vel:u:-1)
(lvl:3 : fidx:3 vidx:-1 u:  1 su: 10 port_id:u:-1)
(lvl:4 : fidx:4 vidx: 0 u:  0 su: -1 midi:m:)
(lvl:4 : fidx:5 vidx: 1 u:  7 su: -1 mp_score_vel:u:0)
(lvl:4 : fidx:6 vidx: 2 u:  8 su: -1 mp_loc:u:-1)
(lvl:4 : fidx:7 vidx: 3 u:  9 su: -1 mp_meas:u:-1)
(lvl:4 : fidx:8 vidx: 4 u: 10 su: -1 mp_port_id:u:-1)

(lvl:0 : fidx:0 vidx: 0 u:  4 su: -1 loc:u:-1)
(lvl:0 : fidx:1 vidx: 1 u:  5 su: -1 meas:u:-1)
(lvl:0 : fidx:2 vidx: 2 u: 12 su: -1 score_vel:u:-1)
(lvl:3 : fidx:3 vidx:-1 u:  1 su: 10 port_id:u:-1)
(lvl:4 : fidx:4 vidx: 0 u:  0 su: -1 midi:m:)
(lvl:4 : fidx:5 vidx: 1 u:  7 su: -1 mp_score_vel:u:0)
(lvl:4 : fidx:6 vidx: 2 u:  8 su: -1 mp_loc:u:-1)
(lvl:4 : fidx:7 vidx: 3 u:  9 su: -1 mp_meas:u:-1)
(lvl:4 : fidx:8 vidx: 4 u: 10 su: -1 mp_port_id:u:-1)

info: 21:54:57.407: root.sf_b:0 : reset: beg:0 end:6   
 */


/*
error: 22:09:36.643: root.vctl:0 : Record 'seg_idx' field read failed.  (0)  rc:22 _exec line:6351 /home/kevin/src/caw/src/libcw/src/flow/cwFlowProc.cpp
(lvl:0 : fidx:0 vidx: 0 u: 11 su: -1 vt_midi:m:)
(lvl:2 : fidx:1 vidx: 0 u:  4 su: -1 loc:u:-1)
(lvl:2 : fidx:2 vidx: 1 u:  5 su: -1 meas:u:-1)
(lvl:2 : fidx:3 vidx: 2 u: 12 su: -1 score_vel:u:-1)
(lvl:5 : fidx:4 vidx:-1 u:  1 su: 10 port_id:u:-1)
(lvl:6 : fidx:5 vidx: 0 u:  0 su: -1 midi:m:)
(lvl:6 : fidx:6 vidx: 1 u:  7 su: -1 mp_score_vel:u:0)
(lvl:6 : fidx:7 vidx: 2 u:  8 su: -1 mp_loc:u:-1)
(lvl:6 : fidx:8 vidx: 3 u:  9 su: -1 mp_meas:u:-1)
(lvl:6 : fidx:9 vidx: 4 u: 10 su: -1 mp_port_id:u:-1)
(vt_midi=
error: 22:09:36.643: Value conversion failed during value to value assignement.  (0)  rc:19 value_from_value line:1232 /home/kevin/src/caw/src/libcw/src/flow/cwFlowValue.cpp
,loc=error: 22:09:36.643: A uint could not be converted to a  <invalid> (0xed0de990).  (0)  rc:29 value_set line:1560 /home/kevin/src/caw/src/libcw/src/flow/cwFlowValue.cpp

 */
cw::rc_t cw::flow::recd_array_create( recd_array_t*&     recd_array_ref,
                                      const recd_type_t* recd_type,
                                      unsigned           allocRecdN,
                                      const object_t*    data_cfg )
{
  rc_t          rc         = kOkRC;
  recd_array_t* recd_array = nullptr;

  recd_array_ref = nullptr;

  // if data_cfg was given
  if( data_cfg != nullptr  )
  {
    if( !data_cfg->is_list() )
      return cwLogError(kInvalidArgRC,"The record data must be presented as a cfg. list.");

    allocRecdN = std::max(allocRecdN,data_cfg->child_count());
  }
  
  recd_array       = mem::allocZ<recd_array_t>();
  recd_array->type = recd_type;
  
  recd_array->valA       = (recd_type->read_only_fl || recd_type->fieldN==0) ? nullptr : mem::allocZ<value_t>(recd_array->type->fieldN * allocRecdN);
  recd_array->recdA      = mem::allocZ<recd_t>(allocRecdN);
  recd_array->allocRecdN = allocRecdN;
  recd_array->recdN      = 0;

  // for each record
  for(unsigned i=0; i<allocRecdN; ++i)
  {
    // set the value array for this record
    recd_array->recdA[i].valA = recd_array->valA==nullptr ? nullptr : recd_array->valA + (i*recd_array->type->fieldN);
    recd_array->recdA[i].type = recd_type;

    // set the value type of all fields in the record
    if( recd_array->recdA[i].valA != nullptr )
    {
      recd_field_t* f = recd_type->fieldL;
      for(; f!=nullptr; f=f->link)
        recd_array->recdA[i].valA[ f->val_idx ].tflag = f->value.tflag; 
    } 
  }

  if( data_cfg != nullptr )
  {
    if((rc = recd_array_append_from_cfg(recd_array,data_cfg)) != kOkRC )
      goto errLabel;
  }
  
  recd_array_ref = recd_array;

errLabel:
  if( rc != kOkRC )
  {
    recd_array_destroy(recd_array);
  }
  return rc;
}

cw::rc_t cw::flow::recd_array_create( recd_array_t*&     recd_array_ref,
                                      const recd_type_t* base_type,
                                      const object_t*    fmt_cfg,
                                      unsigned           allocRecdN,
                                      const object_t*    data_cfg )
{
  rc_t         rc        = kOkRC;
  recd_type_t* recd_type = nullptr;
  
  if((rc = recd_type_create(  recd_type, base_type, fmt_cfg )) != kOkRC )
  {
    goto errLabel;
  }

  if((rc = recd_array_create( recd_array_ref, recd_type, allocRecdN, data_cfg)) != kOkRC )
  {
    goto errLabel;
  }

  recd_array_ref->_type = recd_type;

errLabel:

  if( rc != kOkRC )
    rc = cwLogError(rc,"recd_array_create failed.");
  
  return rc;
}


cw::rc_t cw::flow::recd_array_destroy( recd_array_t*& recd_array_ref )
{
  if( recd_array_ref != nullptr )
  {
    recd_type_destroy(recd_array_ref->_type);
    mem::release(recd_array_ref->valA);
    mem::release(recd_array_ref->recdA);
    mem::release(recd_array_ref);
  }

  return kOkRC;
}

cw::rc_t cw::flow::recd_array_append_from_cfg( recd_array_t* recd_array, const object_t* data_cfg )
{
  rc_t rc = kOkRC;
  
  unsigned recd_idx       = 0;
  const object_t* ele_dict = nullptr;
  
  if( !data_cfg->is_list() )
  {
    rc = cwLogError(kInvalidArgRC,"The recd array data must be a list of dictionaries.");
    goto errLabel;
  }
  
  if( recd_array->recdN + data_cfg->child_count() > recd_array->allocRecdN )
  {
    rc = cwLogError(kBufTooSmallRC,"The data array has %i too few empty slots available.", (recd_array->recdN + data_cfg->child_count()) - recd_array->allocRecdN);
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
      if((field_idx = recd_type_field_index( recd_array->type, pair->pair_label())) == kInvalidIdx )
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
      if( recd_array->recdN >= recd_array->allocRecdN )
      {
        rc = cwLogError(kInvalidStateRC,"The recd_array has insufficient space to hold the provided data list.");
        goto errLabel;
      }
      
      // copy the value into the record
      if((rc = recd_set_value( nullptr, recd_array->recdA + recd_array->recdN, field_idx, field_value )) != kOkRC )
      {
        rc = cwLogError(rc,"The value assignment of the cfg. data field '%s' at element index %i failed.",cwStringNullGuard(pair->pair_label()),recd_idx);
        goto errLabel;              
      }

    }

    recd_idx += 1;
    recd_array->recdN += 1;
  }

errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"recd_array fill from a cfg. failed.");
  
  return rc;
  
}


bool cw::flow::recd_array_is_type_equivalent( recd_array_t* recd_array, const recd_type_t* type )
{
  return recd_types_are_equivalent( recd_array->type, type );
}

cw::rc_t cw::flow::recd_array_concat( recd_array_t* dst_array, const recd_t* recdA, unsigned recdN )
{
  rc_t rc = kOkRC;
  if( dst_array->type->fieldN != 0 || dst_array->type->fieldL != nullptr )
    return cwLogError(kInvalidStateRC,"The destination array in a record concatenation operation must have an empty top level.");

  if( dst_array->recdN + recdN > dst_array->allocRecdN )
    return cwLogError(kBufTooSmallRC,"Not enough space to copy %i records into %i available slots.",recdN,dst_array->allocRecdN - dst_array->recdN );
  
  for(unsigned i=0; i<recdN; ++i)
  {
    assert( recd_types_are_equivalent( dst_array->type, recdA[i].type ) );

    dst_array->recdA[ dst_array->recdN + i ].type = recdA[i].type;
    dst_array->recdA[ dst_array->recdN + i ].valA = recdA[i].valA;
    dst_array->recdA[ dst_array->recdN + i ].base = recdA[i].base;
  }

  dst_array->recdN += recdN;
  
  return rc;
}

cw::rc_t cw::flow::recd_array_split( const recd_t* srcA, unsigned srcRecdN, unsigned src_fld_idx, recd_array_t** dst_arrayAA, unsigned dst_array_cnt, unsigned dflt_dst_idx )
{
  rc_t rc = kOkRC;

  for(unsigned i=0; i<srcRecdN; ++i)
  {
    unsigned sel_dst_idx = dflt_dst_idx;

    // if a default dest. index was not selected and a field index was provided
    if( sel_dst_idx == kInvalidIdx && src_fld_idx != kInvalidIdx )
    {
      if((rc = recd_get(srcA +i,src_fld_idx,sel_dst_idx)) != kOkRC)
      {
        rc = cwLogError(rc,"Source record access failed on input record index %i.",i);
        goto errLabel;
      }
    }

    // validate the dest. index
    if( sel_dst_idx == kInvalidIdx || sel_dst_idx >= dst_array_cnt )
    {
      rc = cwLogWarning("An invalid destination record array selector %i was encountered.",sel_dst_idx);
      goto errLabel;
    }

    // copy a single record into the dest.
    if((rc = recd_array_concat( dst_arrayAA[sel_dst_idx], srcA + i, 1)) != kOkRC )
    {
      rc = cwLogError(rc,"Record array append failed on record index %i",i);
      goto errLabel;
    }
  }

errLabel:
  return rc;
}

cw::rc_t cw::flow::recd_array_remap( const recd_t* srcA, unsigned srcRecdN, recd_array_t* dst_recd_array )
{
  rc_t rc = kOkRC;

  if( dst_recd_array->recdN + srcRecdN > dst_recd_array->allocRecdN )
  {
    return cwLogError(kBufTooSmallRC,"The recd_array remap output array is too small.");
  }
  
  for(unsigned i=0; i<srcRecdN; ++i)
  {
    dst_recd_array->recdA[i].base = srcA + i;
    dst_recd_array->recdA[i].type = dst_recd_array->type;
    
    dst_recd_array->recdA[i].valA = nullptr;

    //cwLogPrint("REMAP:");
    //recd_print(dst_recd_array->recdA + i);
    //cwLogPrint("\n");
  }
  dst_recd_array->recdN += srcRecdN;

  return rc;
}

void cw::flow::recd_array_print( const recd_array_t* recd_array )
{
  for(unsigned i=0; i<recd_array->recdN; ++i)
  {
    recd_print(recd_array->recdA + i );
    cwLogPrint("\n");
  }
}




//------------------------------------------------------------------------------------------------------------------------
//
// List
//

namespace cw {
  namespace flow {
    void _list_clear( list_t* list )
    {
      // Release all of the elements of the list but do not release eleA[]
      for(unsigned i=0; i<list->eleN; ++i)
      {
        mem::release(list->eleA[i].label);
        value_release(&list->eleA[i].value );
      }
      list->eleN = 0;
    }
    
    rc_t _list_destroy( list_t*& list_ref )
    {
      if( list_ref != nullptr )
      {
        _list_clear(list_ref);
        mem::release(list_ref->eleA);
        mem::release(list_ref);
      }
      return kOkRC;
    }
  }
}

cw::rc_t cw::flow::list_create( list_t*& list_ref, const object_t* cfg )
{
  rc_t rc = kOkRC;
  
  bool labelOnlyListFl = false;

  if( !cfg->is_list() && !cfg->is_dict() )
  {
    rc = cwLogError(kInvalidDataTypeRC,"The cfg. given to a flow list is not a JSON list or dictionary.");
    goto errLabel;
  }

  if( cfg->child_count() == 0 )
    cwLogWarning("The cfg. list used to form a flow list is empty.");
  
  if((rc = list_create(list_ref, cfg->child_count())) != kOkRC )
    goto errLabel;

  if( list_ref->eleAllocN == 0 )
    goto errLabel;

  if( cfg->is_list() )
    labelOnlyListFl = true;

  for(unsigned i=0; i<list_ref->eleAllocN; ++i)
  {
    const object_t* ele = cfg->child_ele(i);
    const char* label = nullptr;
    value_t value;
    
    // if this is a label-only list ... 
    if( labelOnlyListFl  )
    {
      // this is a label-only list and so all elements must be strings
      if( ele->is_string()==false )
      {
        rc = cwLogError(kSyntaxErrorRC,"The list element at index '%i' is not a string.",i);
        goto errLabel;
      }

      // get the element label
      if( ele->value(label) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Could not parse the list element at index '%i'.",i);
        goto errLabel;
      }
      
      // ... then the value is the list element index
      value_set(&value,i);
      
    }
    else // ... otherwise this is (label,value) dictioanry
    {
      // verify that the list element is a (label,element) pair.
      if( !ele->is_pair() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The list dictionary element at index '%i' is not a (label,value) pair.",i);
        goto errLabel;
      }

      // validate the dictionary label
      if( ele->pair_label() == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The list dictionary element is missing it's label at index '%i'.",i);
        goto errLabel;
      }

      // convert the dict value to a flow value
      if((rc = value_from_cfg(ele->pair_value(),value)) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to parse the dict. element value field for '%s' at index '%i'.",ele->pair_label(),i);
        goto errLabel;
      }

      // if the conversion did not result in a numeric or string data type
      if( cwIsFlag(value.tflag,kCfgTFl) )
      {
        rc = cwLogError(rc,"List element value field at index '%i' is not a numeric or string type.",i);
        goto errLabel;
      }

      label = ele->pair_label();
    }

    // add the element to the list
    if((rc = list_append( list_ref, label, value )) != kOkRC )
    {
      rc = cwLogError(rc,"List append failed at index '%i'.",i);
      goto errLabel;
    }
  }
    

  errLabel:
    if( rc != kOkRC )
      _list_destroy(list_ref);
    
    return rc;
}

cw::rc_t cw::flow::list_create( list_t*& list_ref, unsigned count )
{
  rc_t rc = kOkRC;
  if((rc = list_destroy(list_ref)) != kOkRC )
    return rc;

  
  list_ref            = mem::allocZ< list_t >();
  list_ref->eleA      = mem::allocZ< list_ele_t >( count );
  list_ref->eleAllocN = count;
  list_ref->eleN      = 0;

  return rc;
}

cw::rc_t cw::flow::list_destroy( list_t*& list_ref )
{
  return _list_destroy(list_ref);
}

cw::rc_t cw::flow::list_clear( list_t* list )
{
  rc_t rc = kOkRC;
  _list_clear(list);
  return rc;
}

cw::rc_t cw::flow::list_append( list_t* list, const char* label, const value_t& value )
{
  rc_t rc = kOkRC;

  // Validate the list label
  if( textLength(label) == 0 )
  {
    rc = cwLogError(kInvalidArgRC,"List elements must have a valid label.");
    goto errLabel;
  }

  // If there is not enough space to accept another list element then reallocate the list.
  if( list->eleN >= list->eleAllocN )
  {
    list->eleAllocN *= 2;
    list->eleA = mem::resizeZ(list->eleA, list->eleAllocN*2 );
  }

  // All element of the list share the same 'value' type and thereore have the same 'tflag'.
  if( list->eleN == 0 )
    list->eleA[0].value.tflag = value.tflag;
  else
    list->eleA[list->eleN].value.tflag = list->eleA[0].value.tflag;

  // Copy the value into the element value field
  if((rc = value_from_value( value, list->eleA[list->eleN].value )) != kOkRC )
  {
    rc = cwLogError(rc,"Value conversion failed. All value types must be convertiable to type of the first list value.");
    goto errLabel;
  }

  // Realloc the label into the element label field.
  list->eleA[list->eleN].label = mem::duplStr(label);
  
  list->eleN += 1;

errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"List append failed.");

  return rc;
 
}


const char* cw::flow::list_ele_label( const list_t* list, unsigned index )
{
  assert( list != nullptr );
    
  if( index >= list->eleN )
  {
    cwLogError(kInvalidArgRC,"The list index '%i' is invalid for a list of length '%i'.",index,list->eleN);
    return nullptr;
  }

  return list->eleA[ index ].label;
}

unsigned    cw::flow::list_ele_index( const list_t* list, const char* label )
{
  assert( list != nullptr );

  for(unsigned i=0; i<list->eleN; ++i)
    if( textIsEqual(list->eleA[i].label,label) )
      return i;

  return kInvalidIdx;  
}


//------------------------------------------------------------------------------------------------------------------------
cw::rc_t cw::flow::value_test( const test::test_args_t& args )
{
  rc_t          rc   = kOkRC;
  return rc;
}

