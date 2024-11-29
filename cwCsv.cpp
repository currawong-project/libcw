#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwFile.h"
#include "cwObject.h"
#include "cwCsv.h"
#include "cwNumericConvert.h"
#include <type_traits>

namespace cw
{
  namespace csv
  {
    typedef struct col_str
    {
      char*    title;
      unsigned char_idx;
    } col_t;
    
    typedef struct csv_str
    {
      file::handle_t fH;
      
      char*          lineBuf;
      unsigned       lineCharCnt;
      
      col_t*         colA;
      unsigned       colN;

      unsigned       curLineIdx;
      unsigned       curColCnt;
    } csv_t;

    typedef rc_t (*field_handler_t)( csv_t* p, unsigned col_idx, unsigned lineBufCharIdx );

    csv_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,csv_t>(h); }

    rc_t _destroy( csv_t* p )
    {
      rc_t rc = kOkRC;
      
      file::close(p->fH);

      for(unsigned i=0; i<p->colN; ++i)
        mem::release(p->colA[i].title);

      mem::release(p->colA);
      mem::release(p->lineBuf);
      mem::release(p);
      return rc;
    }

    rc_t _print_field_handler( csv_t* p, unsigned col_idx, unsigned lineBufCharIdx )
    {
      printf("%i '%s'\n",col_idx, p->lineBuf + lineBufCharIdx);
      return kOkRC;
    }

    rc_t _fill_titles_field_handler( csv_t* p, unsigned col_idx, unsigned lineBufCharIdx )
    {
      assert( col_idx < p->colN );
      p->colA[col_idx].title = mem::duplStr(p->lineBuf + lineBufCharIdx);
      p->colA[col_idx].char_idx = lineBufCharIdx;
      return kOkRC;
    }

    rc_t _update_col_array( csv_t* p, unsigned col_idx, unsigned lineBufCharIdx )
    {
      rc_t rc = kOkRC;
      
      if( col_idx > p->colN )
      {
        rc = cwLogError(kSyntaxErrorRC,"Too many CSV columns on line index:%i",p->curLineIdx);
        goto errLabel;
      }
      
      p->colA[col_idx].char_idx = lineBufCharIdx;

    errLabel:
      return rc;
    }
    

    rc_t _parse_line_buf( csv_t* p, unsigned& fieldN_Ref, field_handler_t fieldHandlerCb )
    {
      enum {
        kBeforeField,
        kInField,
        kInQuotedField,
        kEscapeState
      };

      
      rc_t     rc     = kOkRC;
      unsigned fieldN = 0;
      unsigned state  = kBeforeField;
      unsigned bi     = 0;
      const char field_seperator_char = ',';
      const char dquote = '"';

      fieldN_Ref = 0;
      
      for(unsigned i=0; i<p->lineCharCnt; ++i)
      {
        char c = p->lineBuf[i];
        
        switch( state )
        {
          case kBeforeField:
            if( isspace(c) )
              continue;
                
            state = c==dquote ? kInQuotedField : kInField;
            [[fallthrough]];
            
          case kInField:
            if(c == field_seperator_char )
            {
              if( fieldHandlerCb != nullptr )
              {
                p->lineBuf[i] = 0; // zero terminate the field
                
                if((rc = fieldHandlerCb( p, fieldN, bi)) != kOkRC )
                  goto errLabel;
              }
              
              fieldN += 1;
              bi = i+1;
              state = kBeforeField;
            }
            break;
            
          case kInQuotedField:
            if(c == dquote)
            {
              if( i+1 < p->lineCharCnt && p->lineBuf[i+1] == dquote )
                state = kEscapeState;
              else
                state = kInField;
            }
            break;

          case kEscapeState:
            assert( c == dquote );
            state = kInQuotedField;
              
        }
      }

      if( fieldHandlerCb != nullptr )
        if((rc = fieldHandlerCb( p, fieldN, bi )) != kOkRC )
          goto errLabel;
      
      fieldN_Ref = fieldN + 1;

      // Invalidate any fields that were not populated
      // This allows rows that have fewer than p->colN columns to be legal.
      if( p->colA != nullptr && fieldN_Ref < p->colN )
        for(unsigned i=fieldN_Ref; i<p->colN; ++i)
          p->colA[i].char_idx = kInvalidIdx;

    errLabel:
      return rc;
      
    }
    
    rc_t _fill_line_buffer( csv_t* p )
    {
      rc_t rc;

      // read the next line
      if((rc = getLineAuto( p->fH, &p->lineBuf, &p->lineCharCnt )) != kOkRC )
      {
        if( rc != kEofRC )
          rc = cwLogError(rc,"Line buf alloc failed on line index:%i.",p->curLineIdx);
      }

      p->lineCharCnt = textLength(p->lineBuf);

      // trim trailing white space from the line buffer.
      if( p->lineCharCnt > 0 )
      {
        int i = ((int)p->lineCharCnt)-1;
        
        while( i>=0 && isspace(p->lineBuf[i]) )
        {
          p->lineBuf[i] = '\0';
          --i;
          p->lineCharCnt -= 1;
        }
        
      }
        
      return rc;
    }

    unsigned _title_to_col_index( csv_t* p, const char* title )
    {
      for(unsigned i=0; i<p->colN; ++i)
      {
        if( textCompare(p->colA[i].title,title) == 0 )
          return i;
      }
      
      return kInvalidIdx;
    }
    

    rc_t _create_col_ref_array( csv_t* p, const char** titleA=nullptr, unsigned titleN=0 )
    {
      rc_t     rc   = kOkRC;
      unsigned colN = 0;
      
      if((rc = _fill_line_buffer(p)) != kOkRC )
        goto errLabel;

      // get a column count
      if((rc = _parse_line_buf(p, colN, nullptr)) != kOkRC )
      {
        rc = cwLogError(rc,"Error parsing the title line.");
        goto errLabel;
      }

      // allocate the column reference array
      p->colA = mem::allocZ<col_t>(colN);
      p->colN = colN;

      // set the column titles
      if((rc = _parse_line_buf(p, colN, _fill_titles_field_handler)) != kOkRC )
      {
        rc = cwLogError(rc,"Error setting the column reference titles.");
        goto errLabel;
      }

      // verfiy the reference title
      for(unsigned i=0; i<titleN; ++i)
        if( _title_to_col_index( p, titleA[i] ) == kInvalidIdx )
        {
          rc = cwLogError(kEleNotFoundRC,"The required column '%s' does not exist.",titleA[i]);
          goto errLabel;
        }

      
    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"col ref array create failed.");
      
      return rc;
    }

    rc_t _get_field_str( csv_t* p, unsigned colIdx, const char*& fieldStr_Ref )
    {
      rc_t rc = kOkRC;
      
      fieldStr_Ref = nullptr;
  
      if( colIdx > p->curColCnt || p->colA[colIdx].char_idx == kInvalidIdx )
      {
        rc = cwLogError(kInvalidArgRC,"The CSV column index '%i' is not valid on line index:%i. Field count:%i", colIdx,p->curLineIdx,p->curColCnt );
        goto errLabel;
      }

      

      fieldStr_Ref = p->lineBuf + p->colA[colIdx].char_idx;

    errLabel:
      return rc;
  
    }

    template < typename T >
    rc_t _parse_number_field( csv_t* p, unsigned colIdx, T& valueRef )
    {
      rc_t rc = kOkRC;
      const char* fieldStr = nullptr;
      
      if((rc = _get_field_str(p,colIdx,fieldStr)) != kOkRC )
        goto errLabel;

      if( fieldStr != nullptr )
      {
        // advance past white space
        while( *fieldStr && isspace(*fieldStr) )
          ++fieldStr;

        // the first char must be a number or decimal point
        if( isdigit(*fieldStr) || (*fieldStr=='.' && std::is_floating_point<T>()) )
        {    
        
          if((rc = string_to_number(fieldStr,valueRef)) != kOkRC )
          {
            rc = cwLogError(rc,"Numeric parse failed on column '%s' on line index:%i",cwStringNullGuard(p->colA[colIdx].title),p->curLineIdx);
            goto errLabel;
          }
        }
      }
          
    errLabel:
      return rc;      
    }

    template < typename T >
    rc_t _parse_number_field( csv_t* p, const char* colLabel, T& valueRef )
    {
      unsigned colIdx;
      if((colIdx = _title_to_col_index(p, colLabel)) == kInvalidIdx )
        return cwLogError(kInvalidArgRC,"The column label '%s' is not valid.",cwStringNullGuard(colLabel));

      return _parse_number_field(p,colIdx,valueRef);
    }

    rc_t _parse_string_field( csv_t* p, unsigned colIdx, const char*& valRef )
    {
      rc_t   rc = kOkRC;
  
      valRef = nullptr;
  
      if((rc = _get_field_str(p,colIdx,valRef)) != kOkRC )
        goto errLabel;

    errLabel:  
      return rc;  
    }

    rc_t _parse_bool_field( csv_t* p, unsigned colIdx, bool& valRef )
    {
      rc_t        rc       = kOkRC;
      const char* fieldStr = nullptr;

      if((rc = _get_field_str(p,colIdx,fieldStr)) != kOkRC )
        goto errLabel;
      else
      {
        if( textIsEqualI(fieldStr,"true")  )
          valRef = true;
        else
          if( textIsEqualI(fieldStr,"false") )
            valRef = false;
          else
            rc = cwLogError(kSyntaxErrorRC,"The value of a boolean must be either 'true' or 'false'.");            
      }
      
    errLabel:  
      return rc;  
    }

    rc_t _parse_bool_field( csv_t* p, const char* colLabel, bool& valRef )
    {
      unsigned colIdx;
      if((colIdx = _title_to_col_index(p, colLabel)) == kInvalidIdx )
        return cwLogError(kInvalidArgRC,"The column label '%s' is not valid.",cwStringNullGuard(colLabel));
      
      return _parse_bool_field(p,colIdx,valRef);
    }
    
    
  }
}


cw::rc_t cw::csv::create( handle_t& hRef, const char* fname, const char** titleA, unsigned titleN )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  csv_t* p = mem::allocZ<csv_t>();

  //_store_title_array( p, titleA, titleN );
  
  if((rc = file::open(p->fH,fname,file::kReadFl)) != kOkRC )
    goto errLabel;
  
  if((rc = _create_col_ref_array( p, titleA, titleN )) != kOkRC )
    goto errLabel;

  hRef.set(p);
  
 errLabel:
  
  if( rc != kOkRC )
  {
    rc = cwLogError(rc,"CSV file open failed.");
    _destroy(p);    
  }
  
  return rc;
}

cw::rc_t cw::csv::destroy(handle_t& hRef )
{
  rc_t rc = kOkRC;
  if(!hRef.isValid() )
    return rc;

  csv_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  
  return rc;
}

cw::rc_t cw::csv::line_count( handle_t h, unsigned& lineCntRef )
{
  rc_t rc = kOkRC;
  csv_t* p = _handleToPtr(h);
  lineCntRef = 0;

  long offset = 0;
  if((rc = file::tell(p->fH,&offset)) != kOkRC )
  {
    rc = cwLogError(rc,"file tell() failed.");
    goto errLabel;
  }


  if((rc = file::seek(p->fH,file::kBeginFl,0)) != kOkRC )
  {
    rc = cwLogError(rc,"file seek failed.");
    goto errLabel;
  }
  
  if((rc = file::lineCount(p->fH, &lineCntRef)) != kOkRC )
  {
    rc = cwLogError(rc,"CSV line count query failed.");
    goto errLabel;
  }

  if((rc = file::seek(p->fH,file::kBeginFl,offset)) != kOkRC )
  {
    rc = cwLogError(rc,"file seek() failed.");
    goto errLabel;
  }

 errLabel:
  return rc;
}


unsigned cw::csv::col_count( handle_t h )
{
  csv_t* p = _handleToPtr(h);
  return p->colN;
}

const char* cw::csv::col_title( handle_t h, unsigned idx )
{
  csv_t* p = _handleToPtr(h);
  
  if( idx >= p->colN )
    return nullptr;
  
  return p->colA[idx].title;
}

unsigned cw::csv::title_col_index( handle_t h, const char* title )
{
  csv_t* p = _handleToPtr(h);
  return _title_to_col_index(p,title);
}

bool cw::csv::has_field( handle_t h, const char* title )
{
  csv_t* p = _handleToPtr(h);
  return _title_to_col_index(p,title) != kInvalidIdx;
}


cw::rc_t cw::csv::rewind( handle_t h )
{
  rc_t   rc = kOkRC;
  csv_t* p  = _handleToPtr(h);
  
  if((rc = file::seek(p->fH,file::kBeginFl,0)) != kOkRC )
  {
    rc = cwLogError(rc,"Rewind failed on CSV.");
    goto errLabel;
  }

  if((rc = _fill_line_buffer(p)) != kOkRC )
    goto errLabel;

  p->curLineIdx = 0;
  
 errLabel:
  return rc;
  
}

cw::rc_t cw::csv::next_line( handle_t h )
{
  rc_t rc = kOkRC;
  csv_t* p = _handleToPtr(h);
  
  p->curLineIdx += 1;
  
  if((rc = _fill_line_buffer(p)) != kOkRC )
    goto errLabel;

  // get a column count
  if((rc = _parse_line_buf(p, p->curColCnt, _update_col_array)) != kOkRC )
  {
    rc = cwLogError(rc,"Error parsing line index %i.",p->curLineIdx);
    goto errLabel;
  }

 errLabel:
  return rc;
}

unsigned cw::csv::cur_line_index( handle_t h )
{
  csv_t* p = _handleToPtr(h);
  return p->curLineIdx;
}


cw::rc_t cw::csv::field_char_count( handle_t h, unsigned colIdx, unsigned& charCntRef )
{
  rc_t rc = kOkRC;
  csv_t* p = _handleToPtr(h);
  const char* fieldStr = nullptr;
  
  charCntRef = 0;
  
  if((rc = _get_field_str(p,colIdx,fieldStr)) != kOkRC )
  {
    rc = cwLogError(rc,"Field char count failed.");
    goto errLabel;
  }
  
  charCntRef = textLength(fieldStr);

 errLabel:
  return rc;
}

cw::rc_t cw::csv::parse_field( handle_t h, unsigned colIdx, bool& valRef )
{
  csv_t* p = _handleToPtr(h);
  return _parse_bool_field( p, colIdx, valRef )  ;
}

cw::rc_t cw::csv::parse_field( handle_t h, unsigned colIdx, uint8_t& valRef )
{
  csv_t* p = _handleToPtr(h);
  return _parse_number_field( p, colIdx, valRef )  ;
}

cw::rc_t cw::csv::parse_field( handle_t h, unsigned colIdx, unsigned& valRef )
{
  csv_t* p = _handleToPtr(h);
  return _parse_number_field( p, colIdx, valRef )  ;
}

cw::rc_t cw::csv::parse_field( handle_t h, unsigned colIdx, int& valRef )
{
  csv_t* p = _handleToPtr(h);
  return _parse_number_field( p, colIdx, valRef )  ;
}

cw::rc_t cw::csv::parse_field( handle_t h, unsigned colIdx, double& valRef )
{
  csv_t* p = _handleToPtr(h);
  return _parse_number_field( p, colIdx, valRef )  ;
}

cw::rc_t cw::csv::parse_field( handle_t h, unsigned colIdx, const char*& valRef )
{
  csv_t* p = _handleToPtr(h);
  return _parse_string_field( p, colIdx, valRef );
}

cw::rc_t cw::csv::parse_field( handle_t h, const char* colLabel, bool& valRef )
{
  csv_t* p = _handleToPtr(h);
  return _parse_bool_field(p, colLabel, valRef );
}

cw::rc_t cw::csv::parse_field( handle_t h, const char* colLabel, uint8_t& valRef )
{
  csv_t* p = _handleToPtr(h);
  return _parse_number_field(p, colLabel, valRef );
}

cw::rc_t cw::csv::parse_field( handle_t h, const char* colLabel, unsigned& valRef )
{
  csv_t* p = _handleToPtr(h);
  return _parse_number_field(p, colLabel, valRef );
}

cw::rc_t cw::csv::parse_field( handle_t h, const char* colLabel, int& valRef )
{
  csv_t* p = _handleToPtr(h);
  return _parse_number_field(p, colLabel, valRef );
}

cw::rc_t cw::csv::parse_field( handle_t h, const char* colLabel, double& valRef )
{
  csv_t* p = _handleToPtr(h);
  return _parse_number_field(p, colLabel, valRef );
}

cw::rc_t cw::csv::parse_field( handle_t h, const char* colLabel, const char*& valRef )
{
  csv_t* p  = _handleToPtr(h);
  
  unsigned colIdx;
  if((colIdx = _title_to_col_index(p, colLabel)) == kInvalidIdx )
    return cwLogError(kInvalidArgRC,"The column label '%s' is not valid.",cwStringNullGuard(colLabel));

  return _parse_string_field(p,colIdx,valRef);
}


cw::rc_t cw::csv::test( const object_t* args )
{
  rc_t rc = kOkRC;
  const char* fname = nullptr;
  const object_t* titleL = nullptr;
  csv::handle_t csvH;

  if((rc = args->getv("fname",fname,
                      "titleL",titleL )) != kOkRC )
  {
    rc = cwLogError(rc,"CSV test arg. parse failed.");
    goto errLabel;
  }
  else
  {
    rc_t rc = kOkRC;
    unsigned titleN = titleL == nullptr ? 0 : titleL->child_count();
    const char* titleA[ titleN ];
    unsigned line_cnt = 0;
    
    for(unsigned i=0; i<titleN; ++i)
    {
      if((rc = titleL->child_ele(i)->value(titleA[i])) != kOkRC )
      {
        cwLogError(rc,"Reference title array parsing failed.");
        goto errLabel;
      }
    }

    if((rc = csv::create(csvH,fname,titleA,titleN)) != kOkRC )
    {
      rc = cwLogError(rc,"CSV create failed.");
      goto errLabel;
    }

    line_count(csvH,line_cnt);
    printf("lines:%i\n",line_cnt);
    
    for(unsigned i=0; i<10 && (rc = next_line(csvH)) == kOkRC; ++i )
    {
      const char* opcode = nullptr;
      unsigned d0=0xff,d1=0xff;
      if((rc = getv(csvH,"opcode",opcode,
                    "d0",d0,
                    "d1",d1)) != kOkRC )
      {
        rc = cwLogError(rc,"CSV get failed.");
        break;
      }

      printf("%s %i %i\n",cwStringNullGuard(opcode),d0,d1);
    }
    
    line_count(csvH,line_cnt);
    printf("lines:%i\n",line_cnt);
    
  }

  
 errLabel:
  
  destroy(csvH);
  
  return rc;
}
