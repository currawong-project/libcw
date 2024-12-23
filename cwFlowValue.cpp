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
#include "cwFlowValue.h"

namespace cw
{
  namespace flow
  {

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
      { kTimeTFl,   "time" },
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

        if( f->group_fl )
          _recd_type_destroy_field_list(f->u.group_fieldL);

        mem::release(f->doc);
        mem::release(f->label);
        mem::release(f);
    
        f = f0;
      }
    }

    unsigned _recd_field_list_set_index( recd_field_t* fld, unsigned index )
    {
      for(recd_field_t* f=fld; f!=nullptr; f=f->link)
        if( f->group_fl )
          index = _recd_field_list_set_index(f->u.group_fieldL,index);
        else
          f->u.index = index++;
      return index;
    }


    const char* _recd_field_index_to_label(const recd_field_t* fld, unsigned field_idx)
    {
      const char* label = nullptr;
      
      for(const recd_field_t* f=fld; f!=nullptr; f=f->link)
        if( f->group_fl )
          label = _recd_field_index_to_label(f->u.group_fieldL,field_idx);
        else
        {
          if(f->u.index == field_idx )
            label = f->label;          
        }
      
      return label;      
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
            rc = cwLogError(rc,"Error parsing the field '%s'.",cwStringNullGuard(pair->pair_label()));
            goto errLabel;
          }

          // allocate the field record
          field = mem::allocZ<recd_field_t>();
      
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

          field->label = mem::duplStr(pair->pair_label());
          field->doc = mem::duplStr(doc_string);
      
          if( textIsEqual(type_label,"group") )
          {
            const object_t* field_dict = nullptr;

            field->group_fl = true;

            // get the group 'fields' dictionary
            if((rc = pair->pair_value()->getv("fields",field_dict)) != kOkRC )
            {
              rc = cwLogError(rc,"The field group '%s' does not have a field list.",pair->pair_label());
              goto errLabel;
            }

            // recursively read the group field list
            if((rc = _recd_field_list_from_cfg(field->u.group_fieldL,field_dict)) != kOkRC )
            {
              rc = cwLogError(rc,"The creation of field group '%s' failed.",pair->pair_label());
              goto errLabel;
            }

        
          }
          else
          {
      
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
    
      }
    errLabel:
      return rc;
    }

    void _recd_set_value_type( recd_field_t* fieldL, recd_t* r )
    {
      recd_field_t* f = fieldL;
      for(; f!=nullptr; f=f->link)
        if( f->group_fl )
          _recd_set_value_type( f->u.group_fieldL, r );
        else
          r->valA[ f->u.index ].tflag = f->value.tflag; 
    }

    rc_t  _recd_set_default_value( recd_field_t* fieldL, recd_t* r )
    {
      rc_t          rc = kOkRC;      
      recd_field_t* f  = fieldL;
      
      for(; f!=nullptr; f=f->link)
      {
        if( f->group_fl )
          _recd_set_default_value( f->u.group_fieldL, r );
        else
        {
          if(f->value.tflag != kInvalidTFl)
          {
            if((rc = value_from_value( f->value, r->valA[f->u.index] )) != kOkRC )
            {
              rc = cwLogError(rc,"Set default value failed on the field '%s'.",cwStringNullGuard(f->label));
              goto errLabel;
            }
          }
        }
      }
      
    errLabel:
      return rc;
    }
    
    
    const recd_field_t* _find_field( const recd_field_t* fieldL, const char* label, unsigned label_charN, bool group_fl )
    {
      for(const recd_field_t* f = fieldL; f!=nullptr; f=f->link)
      {
        unsigned n = textLength(f->label);
        
        if( (f->group_fl == group_fl) && n==label_charN && textIsEqual(f->label,label,label_charN) )
          return f;
      }   
      return nullptr;      
    }

    const recd_field_t* _find_value_field( const recd_field_t* fieldL, const char* field_label)
    {
      const char*   period = firstMatchChar( field_label, '.' );
      const recd_field_t* f      = nullptr;;

      // if we are searching for a value field label
      if( period == nullptr )
      {
        if((f = _find_field( fieldL, field_label, textLength(field_label), false )) ==  nullptr )
        {
          goto errLabel;
        }
      }
      else // otherwise we are searching for a group 
      {
        if((f = _find_field( fieldL, field_label, period-field_label, true )) == nullptr )
        {
          goto errLabel;
        }
        
        return _find_value_field(f->u.group_fieldL,period+1);

      }
      
    errLabel:      
      return f;
    }
    
    unsigned _calc_value_field_index( const recd_type_t* recd_type, const char* field_label)
    {      
      const recd_field_t* f;
      unsigned index = kInvalidIdx;
      
      // if the field label is in the local record
      if((f = _find_value_field( recd_type->fieldL, field_label )) != nullptr )
      {
        assert(f->group_fl == false );
        index = f->u.index;
      }
      else
      {
        // recursively look for the field in the base type
        if( recd_type->base != nullptr )
        {
          if(( index = _calc_value_field_index( recd_type->base, field_label )) != kInvalidIdx )
            index += recd_type->fieldN;
        }
      }
      
      return index;
    }

    void _recd_type_print_fields( const recd_type_t* rt0, const recd_field_t* fieldL, const char* group_label, unsigned indent )
    {
      const recd_field_t* f;

      char indent_str[ indent+1 ];
      for(unsigned i=0; i<indent; ++i)
        indent_str[i] = ' ';
      indent_str[indent] = '\0';

      // print non-group field first
      for(f=fieldL; f!=nullptr; f=f->link)        
        if( f->group_fl == false )
        {
          unsigned labelN = textLength(f->label) + textLength(group_label) + 2;
          char label[ labelN ];
          label[0] = 0;
          label[labelN-1] = 0;
          
          if( group_label != nullptr )
          {
            strcpy(label,group_label);
            strcat(label,".");
          }
          strcat(label,f->label);
          
          unsigned field_idx = recd_type_field_index( rt0, label);
          cwLogPrint("%s%i %i %s\n",indent_str,field_idx,f->u.index,f->label);
        }

      // print group fields next
      for(f=fieldL; f!=nullptr; f=f->link)        
        if( f->group_fl )
        {
          cwLogPrint("%s %s:\n",indent_str,f->label);
          _recd_type_print_fields(rt0,f->u.group_fieldL,f->label,indent+2);
        }
      
    }
    
    void _recd_type_print( const recd_type_t* rt0, const recd_type_t* rt )
    {
      if( rt->base != nullptr )
        _recd_type_print( rt0, rt->base );

      _recd_type_print_fields(rt0,rt->fieldL,nullptr,0);
    }

    void _recd_print_field( const char* group_label, const recd_field_t* fieldL,  const value_t* valA )
    {
      const recd_field_t* f;
      for(f=fieldL; f!=nullptr; f=f->link)
        if(f->group_fl)
          _recd_print_field(f->label,f->u.group_fieldL,valA);
        else
        {
          if( group_label != nullptr )
            cwLogPrint("%i %s.%s ",f->u.index,group_label,f->label);
          else
            cwLogPrint("%i %s ",f->u.index,f->label);
          value_print(valA + f->u.index,true);
          cwLogPrint("\n");
        }
    }
      

    rc_t _recd_print( const recd_type_t* rt, const recd_t* r )
    {
      rc_t rc = kOkRC;
      
      if(rt->base != nullptr )
      {
        if( r->base == nullptr )
        {
          rc = cwLogError(kInvalidStateRC,"recd with base type does not have a base instance.");
          goto errLabel;
        }
        
        _recd_print( rt->base, r->base );
      }
      
      _recd_print_field( nullptr,rt->fieldL,  r->valA );

    errLabel:
      return rc;
      
    }

  } // flow
} // cw

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

/*
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
  
  // calc the total size of memory required for all internal data structures
  f->memByteN = sizeof(unsigned)     * chN*kFbufVectN           // maxBinN_V[],binN_V[],hopSmpN_V[]
              + sizeof(fd_sample_t*) * chN*kFbufVectN           // magV[],phsV[],hzV[] (pointer to bin buffers)
              + sizeof(bool)         * chN*1                    // readyFlV[]
              + sizeof(fd_sample_t)  * maxTotalBinN*kFbufVectN; // bin buffer memory

  // allocate memory
  f->mem       = mem::allocZ<uint8_t>(f->memByteN);

  unsigned*     base_maxBinV = (unsigned*)f->mem;
  fd_sample_t** base_bufV    = (fd_sample_t**)(base_maxBinV + kFbufVectN * chN);
  bool*         base_boolV   = (bool*)(base_bufV + kFbufVectN * chN);
  fd_sample_t*  base_buf     = (fd_sample_t*)(base_boolV + chN);
  
  
  f->srate     = srate;
  f->chN       = chN;
  f->maxBinN_V = base_maxBinV;
  f->binN_V    = f->maxBinN_V + chN;
  f->hopSmpN_V = f->binN_V + chN;
  f->magV      = base_bufV;
  f->phsV      = f->magV + chN;
  f->hzV       = f->phsV + chN;
  f->readyFlV  = base_boolV;

  vop::copy( f->binN_V, binN_V, chN );
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
    fd_sample_t* m         = base_buf;
    for(unsigned chIdx=0; chIdx<chN; ++chIdx)
    {   
      f->magV[chIdx] = m + 0 * f->binN_V[chIdx];
      f->phsV[chIdx] = m + 1 * f->binN_V[chIdx];
      f->hzV[ chIdx] = m + 2 * f->binN_V[chIdx];
      m += f->maxBinN_V[chIdx];
      assert( m <= base_buf + kFbufVectN * maxTotalBinN );
    }
  }

  return f;  
}
*/

/*
cw::flow::fbuf_t* cw::flow::fbuf_create( srate_t srate, unsigned chN, const unsigned* maxBinN_V, const unsigned* binN_V, const unsigned* hopSmpN_V, const fd_sample_t** magV, const fd_sample_t** phsV, const fd_sample_t** hzV )
{
  for(unsigned i=0; i<chN; ++i)
    if( binN_V[i] > maxBinN_V[i] )
    {
      cwLogError(kInvalidArgRC,"A channel bin count (%i) execeeds the max bin count (%i).",binN_V[i],maxBinN_V[i]);
      return nullptr;;
    }
  
  fbuf_t* f = mem::allocZ<fbuf_t>();
  
  f->srate     = srate;
  f->chN       = chN;
  f->maxBinN_V = mem::allocZ<unsigned>(chN);
  f->binN_V    = mem::allocZ<unsigned>(chN);
  f->hopSmpN_V = mem::allocZ<unsigned>(chN); 
  f->magV      = mem::allocZ<fd_sample_t*>(chN);
  f->phsV      = mem::allocZ<fd_sample_t*>(chN);
  f->hzV       = mem::allocZ<fd_sample_t*>(chN);
  f->readyFlV  = mem::allocZ<bool>(chN);

  vop::copy( f->binN_V, binN_V, chN );
  vop::copy( f->maxBinN_V, maxBinN_V, chN );
  vop::copy( f->hopSmpN_V, hopSmpN_V, chN );  
  
  if( magV != nullptr || phsV != nullptr || hzV != nullptr )
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
    unsigned maxTotalBinsN = vop::sum( maxBinN_V, chN );
        
    fd_sample_t* buf       = mem::allocZ<fd_sample_t>( kFbufVectN * maxTotalBinsN );
    fd_sample_t* m         = buf;
    for(unsigned chIdx=0; chIdx<chN; ++chIdx)
    {   
      f->magV[chIdx] = m + 0 * f->binN_V[chIdx];
      f->phsV[chIdx] = m + 1 * f->binN_V[chIdx];
      f->hzV[ chIdx] = m + 2 * f->binN_V[chIdx];
      m += f->maxBinN_V[chIdx];
      assert( m <= buf + kFbufVectN * maxTotalBinsN );
    }

    f->buf = buf;
      
  }

  return f;  
}
*/

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

cw::flow::mbuf_t* cw::flow::mbuf_duplicate( const mbuf_t* src )
{
  return mbuf_create(src->msgA,src->msgN);
}


cw::flow::rbuf_t* cw::flow::rbuf_create( const recd_type_t* type, const recd_t* recdA, unsigned recdN )
{
  rbuf_t* m = mem::allocZ<rbuf_t>();
  m->type = type;
  m->recdA = recdA;
  m->recdN = recdN;
  return m;
}

void cw::flow::rbuf_destroy( rbuf_t*& buf )
{
  mem::release(buf);
}

cw::flow::rbuf_t* cw::flow::rbuf_duplicate( const rbuf_t* src )
{
  return rbuf_create(src->type,src->recdA,src->recdN);
}

void  cw::flow::rbuf_setup( rbuf_t* rbuf, recd_type_t* type, recd_t* recdA, unsigned recdN )
{
  rbuf->type = type;
  rbuf->recdA = recdA;
  rbuf->recdN = recdN;
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
          
    case kTimeTFl:
      assert(0);
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
                    
    case kTimeTFl:
      assert(0);
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
    case kTimeTFl:
    case kCfgTFl:
    case kMidiTFl:
      rc = cwLogError(kOpFailRC,"Value conversion failed during value to value assignement.");
      break;
      
    default:
      rc = cwLogError(kInvalidArgRC,"An unknown source operand data type 0x%x was encountered.",src.tflag);
    
  }
  return rc;
}


void cw::flow::value_print( const value_t* v, bool info_fl )
{
  if( v == nullptr )
    return;
        
  switch( v->tflag & kTypeMask )
  {
    case kInvalidTFl:
      cwLogPrint("<invalid>");
      break;
          
    case kBoolTFl:
          
      cwLogPrint("%s%s ", info_fl ? "b:" : "", v->u.b ? "true" : "false" );
      break;
          
    case kUIntTFl:
      cwLogPrint("%s%i ", info_fl ? "u:" : "", v->u.u );
      break;
          
    case kIntTFl:
      cwLogPrint("%s%i ", info_fl ? "i:" : "", v->u.i );
      break;
          
    case kFloatTFl:
      cwLogPrint("%s%f ", info_fl ? "f:" : "", v->u.f );
      break;
          
    case kDoubleTFl:
      cwLogPrint("%s%f ", info_fl ? "d:" : "", v->u.d );
      break;
          
    case kABufTFl:
      if( info_fl )
      {
        if( v->u.abuf == nullptr )
          cwLogPrint("abuf: <null>");
        else
          cwLogPrint("abuf: chN:%i frameN:%i srate:%8.1f ", v->u.abuf->chN, v->u.abuf->frameN, v->u.abuf->srate );
      }
      else
      {
        bool null_fl = v->u.abuf==nullptr || v->u.abuf->buf == nullptr;
        cwLogPrint("(");
        for(unsigned i=0; i<v->u.abuf->chN; ++i)
          cwLogPrint("%f ",null_fl ? 0.0 : vop::rms(v->u.abuf->buf + i*v->u.abuf->frameN, v->u.abuf->frameN));
        cwLogPrint(") ");
      }
      break;
          
    case kFBufTFl:
      if( info_fl )
      {
        if( v->u.fbuf == nullptr )
          cwLogPrint("fbuf: <null>");
        else
        {
          cwLogPrint("fbuf: chN:%i srate:%8.1f ", v->u.fbuf->chN, v->u.fbuf->srate );
          for(unsigned i=0; i<v->u.fbuf->chN; ++i)                
            cwLogPrint("(binN:%i hopSmpN:%i) ", v->u.fbuf->binN_V[i], v->u.fbuf->hopSmpN_V[i] );
        }
      }
      else
      {
            
        bool null_fl = v->u.fbuf==nullptr || v->u.fbuf->magV == nullptr;
        cwLogPrint("(");
        for(unsigned i=0; i<v->u.fbuf->chN; ++i)
          cwLogPrint("%f ",null_fl ? 0.0 : vop::mean(v->u.fbuf->magV[i], v->u.fbuf->binN_V[i]));
        cwLogPrint(") ");
            
      }
      break;

    case kMBufTFl:
      if( info_fl )
      {
        if( v->u.mbuf == nullptr )
          cwLogPrint("mbuf: <null>");
        else
        {
          cwLogPrint("mbuf: cnt: %i", v->u.mbuf->msgN );
        }
      }
      else
      {
        //bool null_fl = v->u.mbuf==nullptr || v->u.mbuf->msgA == nullptr;
        for(unsigned i=0; i<v->u.mbuf->msgN; ++i)
          cwLogPrint("(0x%x 0x%x 0x%x) ",v->u.mbuf->msgA[i].status + v->u.mbuf->msgA[i].ch,v->u.mbuf->msgA[i].d0,v->u.mbuf->msgA[i].d1);
      }
      break;


    case kRBufTFl:
      if( info_fl )
      {
        if( v->u.rbuf == nullptr )
          cwLogPrint("rbuf: <null>");
        else
        {
          cwLogPrint("rbuf: cnt: %i", v->u.rbuf->recdN );
        }
      }
      else
      {
        for(unsigned i=0; i<v->u.rbuf->recdN; ++i)
        {
          assert(0);
          // BUG BUG BUG
          // implement _print_record()
        }
      }
      break;
          
    case kBoolMtxTFl:
    case kUIntMtxTFl:
    case kIntMtxTFl:
    case kFloatMtxTFl:
    case kDoubleMtxTFl:
      assert(0); // not implemeneted
      break;
          
    case kStringTFl:
      cwLogPrint("s:%s ", v->u.s);
      break;
           
    case kTimeTFl:
      assert(0);
      break;

    case kCfgTFl:
      cwLogPrint("c:");
      if( v->u.cfg != nullptr )
        v->u.cfg->print();
      break;

    case kMidiTFl:
      cwLogPrint("m:");
      if( v->u.midi != nullptr )
        cwLogPrint("dev:%i port:%i uid:%i ch:%i st:0x%x d0:0x%x d1:0x%x",v->u.midi->devIdx,v->u.midi->portIdx,v->u.midi->uid,v->u.midi->ch,v->u.midi->status,v->u.midi->d0,v->u.midi->d1);
      break;
          
    default:
      assert(0);
      break;
  }

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


//------------------------------------------------------------------------------------------------------------------------
//
// Record
//


cw::rc_t cw::flow::recd_format_create( recd_fmt_t*& recd_fmt_ref, const object_t* cfg, unsigned dflt_alloc_cnt )
{
  rc_t         rc        = kOkRC;
  recd_fmt_t*  recd_fmt  = nullptr;
  
  recd_fmt_ref = nullptr;

  recd_fmt = mem::allocZ<recd_fmt_t>();

  if( cfg->find( "fields" ) != nullptr )
    if((rc = recd_type_create(recd_fmt->recd_type,nullptr,cfg)) != kOkRC )
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
    rc = cwLogError(rc,"Record format creation failed.");
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
  
  recd_type->base = base;
  recd_type_ref = recd_type;
  
errLabel:
  if( rc != kOkRC && recd_type != nullptr )
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

  _recd_type_destroy_field_list(recd_type_ref->fieldL);
  mem::release(recd_type_ref);
}


unsigned cw::flow::recd_type_max_field_count( const recd_type_t* recd_type )
{
  unsigned n = 0;
  for(const recd_type_t* t = recd_type; t!=nullptr; t=t->base)
    n += t->fieldN;
  return n;
}

unsigned cw::flow::recd_type_field_index( const recd_type_t* recd_type, const char* field_label)
{
  unsigned index;
  
  if((index = _calc_value_field_index( recd_type, field_label)) == kInvalidIdx )
  {
    cwLogError(kInvalidArgRC,"The record field label '%s' was not found.",cwStringNullGuard(field_label));
    goto errLabel;
  }

errLabel:
  return index;
}

const char* cw::flow::recd_type_field_index_to_label( const recd_type_t* recd_type, unsigned field_idx )
{
  const char* label = nullptr;
  
  if( field_idx >= recd_type->fieldN )
    label = recd_type_field_index_to_label(recd_type->base, field_idx - recd_type->fieldN );
  else  
    label = _recd_field_index_to_label(recd_type->fieldL,field_idx);

  return label;
}


void cw::flow::recd_type_print( const recd_type_t* recd_type )
{
  _recd_type_print(recd_type,recd_type);
}

cw::rc_t cw::flow::recd_init( const recd_type_t* recd_type, const recd_t* base, recd_t* r )
{
  r->base = base;
  return _recd_set_default_value( recd_type->fieldL, r );
  
}

cw::rc_t cw::flow::recd_print( const recd_type_t* recd_type, const recd_t* r )
{ return _recd_print( recd_type, r ); }



cw::rc_t cw::flow::recd_array_create( recd_array_t*& recd_array_ref, recd_type_t* recd_type, const recd_type_t* base, unsigned allocRecdN )
{
  rc_t          rc         = kOkRC;
  recd_array_t* recd_array = mem::allocZ<recd_array_t>();

  recd_array_ref = nullptr;

  recd_array->type = mem::allocZ<recd_type_t>();
  recd_array->type->fieldL = recd_type->fieldL;
  recd_array->type->fieldN = recd_type->fieldN;
  recd_array->type->base   = base;
  
  recd_array->valA = mem::allocZ<value_t>(recd_array->type->fieldN * allocRecdN);
  recd_array->recdA = mem::allocZ<recd_t>(allocRecdN);
  recd_array->allocRecdN = allocRecdN;


  // for each record
  for(unsigned i=0; i<allocRecdN; ++i)
  {
    // set the value array for this record
    recd_array->recdA[i].valA = recd_array->valA + (i*recd_array->type->fieldN);

    // set the value type of all records in the array
    _recd_set_value_type( recd_array->type->fieldL, recd_array->recdA + i );
  }

  recd_array_ref = recd_array;
  
  //if( rc != kOkRC )
  //  recd_array_destroy(recd_array);

  return rc;
}

cw::rc_t cw::flow::recd_copy( const recd_type_t* recd_type, const recd_t* recdA, unsigned recdN, recd_array_t* recd_array )
{
  rc_t rc = kOkRC;
  
  if( recd_array->allocRecdN < recdN )
  {
    rc = cwLogError(kBufTooSmallRC,"Not enough space in the destination record array.");
    goto errLabel;
  }

  if( recd_type->fieldN != recd_array->type->fieldN )
  {
    rc = cwLogError(kInvalidArgRC,"Field count mismatch between source and destination records..");
    goto errLabel;
  }
  

  for(unsigned i=0; i<recdN; ++i)
    for(unsigned j=0; j<recd_type->fieldN; ++j)
    {
      recd_array->recdA[i].valA[j] = recdA[i].valA[j];
      recd_array->recdA[i].base    = recdA[i].base;
    }

errLabel:
  if( rc!=kOkRC )
    rc = cwLogError(rc,"Record copy failed.");
  
  return rc;
}


cw::rc_t cw::flow::recd_array_destroy( recd_array_t*& recd_array_ref )
{
  if( recd_array_ref != nullptr )
  {
    mem::release(recd_array_ref->type);
    mem::release(recd_array_ref->valA);
    mem::release(recd_array_ref->recdA);
    mem::release(recd_array_ref);
  }

  return kOkRC;
}

cw::rc_t cw::flow::value_test( const test::test_args_t& args )
{
  rc_t          rc   = kOkRC;  
  object_t*     cfg0 = nullptr;
  object_t*     cfg1 = nullptr;
  recd_fmt_t*   fmt0 = nullptr;
  recd_fmt_t*   fmt1 = nullptr;
  recd_array_t* ra0  = nullptr;
  recd_array_t* ra1  = nullptr;
  
  const char* s0 = "{ alloc_cnt:3, fields: {" 
    "a:  { type:bool,          doc:\"A floater.\" },"
    "b:  { type:uint, value:1, doc:\"My uint.\" },"
    "c:  { type:uint, value:2, doc:\"My other uint.\" }"
    "g0: { type:group, doc:\"A group.\""
    "fields:{ a:{type:int, value:0, doc:\"My int.\" }"
    "         b:{type:bool, value:true, doc:\"My flag.\" }"
    "         c:{type:double, value:1, doc:\"Another field.\" }"
    "}}"
    "}}";

  const char* s1 = "{ alloc_cnt:3, fields: {" 
    "d: { type:double,         doc:\"d doc.\" },"
    "e: { type:uint, value:-1, doc:\"e doc.\" },"
    "f: { type:uint, value:-1, doc:\"f doc.\" }"
    "g1: { type:group, doc:\"A group.\""
    "fields:{ a:{type:int, value:0, doc:\"My int.\" }"
    "         b:{type:bool, value:true, doc:\"My flag.\" }"
    "         c:{type:uint, value:1, doc:\"Another field.\" }"
    "}}"    
    "}}";
  
  if((rc = objectFromString(s0,cfg0)) != kOkRC )
  {
    rc = cwLogError(rc,"cfg0 parse failed.");
    goto errLabel;
  }

  if((rc = objectFromString(s1,cfg1)) != kOkRC )
  {
    rc = cwLogError(rc,"cfg1 parse failed.");
    goto errLabel;
  }

  if((rc = recd_format_create( fmt0, cfg0 )) != kOkRC )
  {
    rc = cwLogError(rc,"fmt0 create failed.");
    goto errLabel;
  }

  if((rc = recd_format_create( fmt1, cfg1 )) != kOkRC )
  {
    rc = cwLogError(rc,"fmt1 create failed.");
    goto errLabel;
  }

  if((rc = recd_array_create( ra0, fmt0->recd_type, nullptr,  fmt0->alloc_cnt )) != kOkRC )
  {
    rc = cwLogError(rc,"recd array 0 create failed.");
    goto errLabel;
  }

  if((rc = recd_array_create( ra1, fmt1->recd_type, fmt0->recd_type, fmt1->alloc_cnt )) != kOkRC )
  {
    rc = cwLogError(rc,"recd array 0 create failed.");
    goto errLabel;
  }


  for(unsigned i=0; i<ra0->allocRecdN; ++i)
  {
    recd_t* r = ra0->recdA + i;

    if((rc = recd_set( ra0->type, nullptr, r,
                       recd_type_field_index(ra0->type,"a"), 0.0f*i,
                       recd_type_field_index(ra0->type,"g0.a"), 4.0*i,
                       recd_type_field_index(ra0->type,"g0.b"), 5*i,
                       recd_type_field_index(ra0->type,"g0.c"), 6*i)) != kOkRC )
    {
      cwLogError(rc,"recd_set() failed on ra0.");
      goto errLabel;
    }
  }

  for(unsigned i=0; i<ra0->allocRecdN; ++i)
    recd_print(ra0->type,ra0->recdA+i);


  for(unsigned i=0; i<ra1->allocRecdN; ++i)
  {
    recd_t* r = ra1->recdA + i;
    recd_t* r_base = ra0->recdA + i;

    if((rc = recd_set( ra1->type, r_base, r,
                       recd_type_field_index(ra1->type,"d"), 0.0f*i,
                       recd_type_field_index(ra1->type,"g1.a"), 4.0*i*2,
                       recd_type_field_index(ra1->type,"g1.b"), 5*i*2,
                       recd_type_field_index(ra1->type,"g1.c"), 6*i*2)) != kOkRC )
    {
      cwLogError(rc,"recd_set() failed on ra1.");
      goto errLabel;
    }
  }

  for(unsigned i=0; i<ra1->allocRecdN; ++i)
    recd_print(ra1->type,ra1->recdA+i);
  
  
  recd_array_destroy( ra0 );
  recd_array_destroy( ra1 );
  
  recd_format_destroy( fmt0 );
  recd_format_destroy( fmt1 );

  cfg0->free();
  cfg1->free();
  

errLabel:
  return rc;
}

