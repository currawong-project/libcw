#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwFile.h"
#include "cwFileSys.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwVariant.h"
#include "cwDataSets.h"
#include "cwSvg.h"
#include "cwTime.h"
#include "cwText.h"
#include "cwMath.h"

//----------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------

namespace cw
{
  namespace dataset
  {
    namespace wtr
    {    
      typedef struct col_str
      {
        rdr::col_t      col;      // Public fields - See rdr::col_t.
        unsigned char*  cur;      // Cache of the current column data contents.
        unsigned        curByteN; // Count of bytes in cur[].
        unsigned*       curDimV;  // Cache of the current column dimensions.
        struct col_str* link;     // Link to next col_t record.
      } col_t;
    
      typedef struct wtr_str
      {
        file::handle_t fH;               // Output file handle
        unsigned       record_count;     // Total count of rows.
        col_t*         colL;             // Linked list of column descriptions
        unsigned       totalVarDimN;     // Total count of unknown dim's among all columns
      } wtr_t;

      inline wtr_t* _handleToPtr( handle_t h )
      { return handleToPtr<handle_t,wtr_t>(h); }

      rc_t _destroy( wtr_t* p )
      {      
        col_t* c = p->colL;
        while( c != nullptr )
        {
          col_t* c0 = c->link;
          mem::free(const_cast<char*>(c->col.label));
          mem::release(c->col.dimV);
          mem::release(c->col.maxDimV);
          mem::release(c->cur);
          mem::release(c->curDimV);
          mem::release(c);
          c = c0;
        }
      
        file::close(p->fH);
        mem::release(p);

        return kOkRC;
      }

      col_t* _columnIdToPtr( wtr_t* p, unsigned columnId )
      {
        col_t* c = p->colL;
        for(; c!=nullptr; c=c->link )
          if( c->col.id == columnId )
            return c;

        cwLogError(kInvalidArgRC,"The dataset column id %i was not found.",columnId);
        return nullptr;
  
      }

      // eleN = count of elements in dV[]
      // dimV[ dimN ] = dimensions for variable sized data elements. cumprod(dimV) must equal eleN
      rc_t _write_column_to_buf( wtr_t* p, unsigned columnId, unsigned eleN, const unsigned* dimV, unsigned dimN, const void* dV, unsigned typeFlags, col_t*& colPtrRef )
      {
        col_t* c = _columnIdToPtr(p,columnId);

        if( c == nullptr )
          return cwLogError(kInvalidArgRC,"Unable to locate the column description associated with id: %i.",columnId);

        // if this is a fixed size column
        if( c->col.varDimN == 0 )
        {
          // verify that the element count matches the fixed element count
          if( eleN != c->col.maxEleN )
            return cwLogError(kInvalidArgRC,"Data vector in column '%s' has %i elements but should have %i elements.", cwStringNullGuard(c->col.label), eleN, c->col.maxEleN );

          if( dimV != nullptr || dimN != 0 )
            cwLogWarning("The dimension vector for the fixed sized column '%s' is ignored in the write() function.",cwStringNullGuard(c->col.label));

        }
        else // this is a variable sized column
        {
          unsigned tmpEleN = 1;
          for(unsigned i=0; i<c->col.rankN; ++i)
          {
            tmpEleN           *= dimV[i];                               // track the count of elements
            c->col.maxDimV[i]  = std::max(c->col.maxDimV[i], dimV[i] ); // track the max. dimension 
            c->curDimV[i]      = dimV[i];                               // store the this columns dimensions      
          }

          // verify that the sizeof the data matches the size given in the dimensions
          if( tmpEleN != eleN )
            return cwLogError(kInvalidArgRC,"The product of the dimension vector does not equal the count of elements in column '%s'.",c->col.label);
    
        }

        if( p->record_count == 0)
        {
          // set data type
          c->col.max.flags = typeFlags;
          c->col.min.flags = typeFlags;
        }
        else
        {
          // verify data type is the same for all elements
          if( c->col.max.flags != typeFlags )
            return cwLogError(kInvalidArgRC,"The data vector type '%s' does not match the column type '%s'.", variant::flagsToLabel(typeFlags), variant::flagsToLabel(c->col.max.flags));
    
        }

        // store the bytes associated with col/row
        unsigned bytesPerEle = variant::flagsToBytes(typeFlags);

  
        if( bytesPerEle == 0  )
          return cwLogError(kInvalidArgRC,"Invalid type identifier in column '%s'.", cwStringNullGuard(c->col.label)); 
        else
        {
          c->curByteN = bytesPerEle * eleN;
          c->cur = mem::resize<unsigned char>(c->cur,c->curByteN);
          memcpy(c->cur,dV,c->curByteN);
        }
  
        colPtrRef = c;
  
        return kOkRC;
      }


      rc_t _write_hdr( wtr_t* p )
      {
        col_t* c;
        rc_t   rc;

        p->totalVarDimN = 0;

        // get the count of columns
        unsigned col_count = 0;
        for(c=p->colL; c!=nullptr; c=c->link)
          ++col_count;

        if((rc = file::write( p->fH, p->record_count )) != kOkRC ) goto errLabel;
        if((rc = file::write( p->fH, col_count ))       != kOkRC ) goto errLabel;

        for(c=p->colL; c!=nullptr; c=c->link)
        {
          if((rc = file::writeStr(  p->fH, c->col.label ))     != kOkRC ) goto errLabel;
          if((rc = file::write( p->fH, c->col.id ))            != kOkRC ) goto errLabel;
          if((rc = file::write( p->fH, c->col.varDimN ))       != kOkRC ) goto errLabel;
          if((rc = file::write( p->fH, c->col.rankN ))         != kOkRC ) goto errLabel;
          if((rc = file::write( p->fH, c->col.maxEleN ))       != kOkRC ) goto errLabel;
          if((rc = variant::write(  p->fH, c->col.max))        != kOkRC ) goto errLabel;
          if((rc = variant::write(  p->fH, c->col.min ))       != kOkRC ) goto errLabel;
        
          for(unsigned i=0; i<c->col.rankN; ++i)
          {
            if((rc = file::write( p->fH, c->col.dimV[i] ))   != kOkRC ) goto errLabel;
            if((rc = file::write( p->fH, c->col.maxDimV[i])) != kOkRC ) goto errLabel;          
          }

          p->totalVarDimN += c->col.varDimN;
        
        }

      errLabel:
        return rc;
      }

      rc_t _re_write_hdr( wtr_t* p )
      {
        rc_t rc;
        if((rc = file::seek( p->fH, file::kBeginFl, 0)) != kOkRC )
          return cwLogError( kSeekFailRC, "Data file Header seek failed.");

        if((rc = _write_hdr( p )) != kOkRC )
          return cwLogError( rc, "Header re-write failed.");

        return rc;
      }
    }
  }  
}

cw::rc_t cw::dataset::wtr::create( handle_t& h, const char* fn )
{
  rc_t rc;
  if((rc = destroy(h)) != kOkRC )
    return rc;

  auto p = mem::allocZ<wtr_t>(1);

  if((rc = file::open(p->fH,fn,file::kWriteFl)) != kOkRC )
  {
    rc = cwLogError(rc,"Data file creation failed.");
    goto errLabel;
  }

  h.set(p);
  
 errLabel:
  if(rc != kOkRC )
    _destroy(p);
    
  return rc;
}

cw::rc_t cw::dataset::wtr::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
      
  if( !h.isValid())
    return rc;

  wtr_t* p = _handleToPtr(h);

  if(( rc = _re_write_hdr( p )) != kOkRC )
    return rc;

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;  
}

cw::rc_t cw::dataset::wtr::define_columns( handle_t h, const char* label, unsigned columnId, unsigned rankN, const unsigned* dimV )
{
  rc_t      rc   = kOkRC;
  wtr_t*    p    = _handleToPtr(h);
  col_t*    c    = mem::allocZ<col_t>(1);
  c->col.label   = mem::duplStr(label);
  c->col.id      = columnId;
  c->col.rankN   = rankN;
  c->col.varDimN = 0;
  c->col.dimV    = mem::allocDupl<unsigned>( dimV, rankN );
  c->col.maxDimV = mem::allocDupl<unsigned>( dimV, rankN );
  c->curDimV     = mem::allocDupl<unsigned>( dimV, rankN );
  c->col.maxEleN = 1;
  
  for(unsigned i=0; i<rankN; ++i)
  {
    c->col.maxEleN *= dimV[i];
    
    if( dimV[i] == 0  )
    {
      c->col.varDimN   +=1;
    }
  }

  // link the new col recd to the end of the column list
  col_t* c0 = p->colL;
  col_t* c1 = nullptr;
  for(; c0!=nullptr; c0=c0->link)
    c1 = c0;

  if( c1 == nullptr )
    p->colL = c;
  else
    c1->link = c;

  return rc;
}

cw::rc_t cw::dataset::wtr::write( handle_t h, unsigned columnId, const int*    dV, unsigned eleN, const unsigned* dimV, unsigned rankN )
{
  rc_t      rc;
  wtr_t* p = _handleToPtr(h);
  col_t*    c = nullptr;
  
  if((rc = _write_column_to_buf( p, columnId, eleN, dimV, rankN, dV, variant::kInt32VFl, c)) != kOkRC )
    return rc;

  if( p->record_count == 0 )
  {
    c->col.min.u.i32 = vop::min( dV, eleN );
    c->col.max.u.i32 = vop::max( dV, eleN );
    //printf("0i %i %i\n", columnId, c->col.min.u.i32 );
  }
  else
  {
    //printf("1i %i %i\n", columnId, c->col.min.u.i32 );
    c->col.min.u.i32 = std::min(c->col.min.u.i32, vop::min( dV, eleN ));
    c->col.max.u.i32 = std::max(c->col.max.u.i32, vop::max( dV, eleN ));
  }

  return rc;
}

cw::rc_t cw::dataset::wtr::write( handle_t h, unsigned columnId, const float*  dV, unsigned eleN, const unsigned* dimV, unsigned rankN )
{
  rc_t      rc;
  wtr_t* p = _handleToPtr(h);
  col_t*    c = nullptr;
  
  if((rc = _write_column_to_buf( p, columnId, eleN, dimV, rankN, dV, variant::kFloatVFl, c)) != kOkRC )
    return rc;

  if( p->record_count == 0 )
  {
    c->col.min.u.f = vop::min( dV, eleN );
    c->col.max.u.f = vop::max( dV, eleN );
  }
  else
  {
    c->col.min.u.f = std::min(c->col.min.u.f, vop::min( dV, eleN ));
    c->col.max.u.f = std::max(c->col.max.u.f, vop::max( dV, eleN ));
  }

  return rc;
}

cw::rc_t cw::dataset::wtr::write( handle_t h, unsigned columnId, const double* dV, unsigned eleN, const unsigned* dimV, unsigned rankN )
{
  rc_t      rc;
  wtr_t* p = _handleToPtr(h);
  col_t*    c = nullptr;
  
  if((rc = _write_column_to_buf( p, columnId, eleN, dimV, rankN, dV, variant::kDoubleVFl, c)) != kOkRC )
    return rc;

  if( p->record_count == 0 )
  {
    c->col.min.u.d = vop::min( dV, eleN );
    c->col.max.u.d = vop::max( dV, eleN );
  }
  else
  {
    c->col.min.u.d = std::min(c->col.min.u.d, vop::min( dV, eleN ));
    c->col.max.u.d = std::max(c->col.max.u.d, vop::max( dV, eleN ));
  }

  return rc;
}

cw::rc_t cw::dataset::wtr::write_record( handle_t h )
{
  rc_t      rc;
  wtr_t* p = _handleToPtr(h);
  col_t*    c;

  // if this is the first row in the file then write the file header
  if( p->record_count == 0 )
    if((rc = _write_hdr(p)) != kOkRC )
      return rc;

  unsigned rowByteN = 0;
  
  // calculate the size of the row data
  for(c=p->colL; c!=nullptr; c=c->link)
    rowByteN += c->col.varDimN * sizeof(unsigned) + c->curByteN;
    
  // write the size of this row
  if((rc = file::write(p->fH, rowByteN)) != kOkRC )
    goto errLabel;

  // for each column
  for(c=p->colL; c!=nullptr; c=c->link)
  {
    // if this is a variable sized column
    if( c->col.varDimN > 0 )
    {
      // then write the variable sized dimensions
      for(unsigned i=0; i<c->col.rankN; ++i)
        if( c->col.dimV[i] == 0 )
          if((rc = file::write( p->fH, c->curDimV[i] )) != kOkRC )
            goto errLabel;
    }


    // write the column field value
    if((rc = file::write( p->fH, c->cur, c->curByteN)) != kOkRC )
      goto errLabel;
    
  }
  
 errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"Example index %i write failed", p->record_count);
  else
    p->record_count += 1;

  return rc;
}

/*

File Format for the following data.
where the data record itself is repeated 3 time.

unsigned dim0V[] = {1};
unsigned dim1V[] = {3};
unsigned dim2V[] = {2,0};
unsigned dim3V[] = {2,2};

int      val0[]  = {0};
int      val1[]  = {1,2,3};
int      val2[]  = {4,5,6,7,8,9};
int      val3[]  = {10,11,13,14};

0300 0000 3 recd_count
0400 0000 4 col_count

0400 0000 label size  - col0
636f 6c30 label
0000 0000 id
0000 0000 varDimN
0100 0000 rankN
0100 0000 maxEleN
4000 0000 max type
0000 0000 max value
0000 0000
4000 0000 min type
0000 0000 min value
0000 0000
0100 0000 dimV[0]
0100 0000 maxDimV[0]

0400 0000 label size - col 1
636f 6c31 label
0100 0000 id
0000 0000 varDimN
0100 0000 rankN
0300 0000 maxEleN
4000 0000 max type
0300 0000 max value
0000 0000
4000 0000 min type
0100 0000 max value 
0000 0000
0300 0000 dimV[0]
0300 0000 maxDimV[0]

0400 0000 label size - col 2
636f 6c32 label
0200 0000 id
0100 0000 varDimN
0200 0000 rankN
0000 0000 maxEleN
4000 0000 max type
0900 0000 max value
0000 0000
4000 0000 min type
0400 0000 min value
0000 0000
0200 0000 dimV[0]
0200 0000 maxDimV[0]

0000 0000 dimV[1]
0300 0000 maxDimV[1]

0400 0000 label size - col 3
636f 6c33 label
0300 0000 id
0000 0000 varDimN
0200 0000 rankN
0400 0000 maxEleN
4000 0000 max type
0e00 0000 max value
0000 0000
4000 0000 min type
0a00 0000 min value
0000 0000
0200 0000 dimV[0]
0200 0000 maxDimV[0]
0200 0000 dimV[1]
0200 0000 maxDimV[1]

3c00 0000 recd0 size (60 bytes) 
0000 0000 0 col0

0100 0000 1 col1[0]
0200 0000 2 col1[1]
0300 0000 3 col1[2]

0300 0000 dimV[1] col2 <- variable dimension
0400 0000 4 col2[0]
0500 0000 5
0600 0000 6
0700 0000 7
0800 0000 8
0900 0000 9

0a00 0000 10 col3
0b00 0000 11
0d00 0000 12
0e00 0000 13

3c00 0000 recd1 size (60 bytes)
0100 0000 1 col0 

0100 0000
0200 0000
0300 0000

0300 0000
0400 0000
0500 0000
0600 0000
0700 0000
0800 0000
0900 0000

0a00 0000
0b00 0000
0d00 0000
0e00 0000

3c00 0000 recd2 size (60 bytes)
0200 0000 2 col0

0100 0000
0200 0000
0300 0000

0300 0000
0400 0000
0500 0000
0600 0000
0700 0000
0800 0000
0900 0000

0a00 0000
0b00 0000
0d00 0000
0e00 0000
 */
cw::rc_t cw::dataset::wtr::test( const object_t* cfg )
{
  rc_t     rc    = kOkRC;
  char*    outFn = nullptr;
  handle_t h;
  
  if((rc = cfg->getv("outFn",outFn)) != kOkRC )
    return cwLogError(rc,"wtr test failed.  Argument parse failed.");

  outFn = filesys::expandPath(outFn);

  if((rc = create(h,outFn)) != kOkRC )
  {
    rc = cwLogError(rc,"rdr create failed.");
    goto errLabel;
  }
  else
  {
    enum { kId0, kId1, kId2, kId3 };
    unsigned dim0V[] = {1};
    unsigned dim1V[] = {3};
    unsigned dim2V[] = {2,0};
    unsigned dim3V[] = {2,2};

    unsigned dim0N = cwCountOf(dim0V);
    unsigned dim1N = cwCountOf(dim1V);
    unsigned dim2N = cwCountOf(dim2V);
    unsigned dim3N = cwCountOf(dim3V);

    int val0[] = {0};
    int val1[] = {1,2,3};
    int val2[] = {4,5,6,7,8,9};
    int val3[] = {10,11,13,14};
  
    if((rc = define_columns(h, "col0", kId0, dim0N, dim0V )) != kOkRC )
    {
      rc = cwLogError(rc,"Define column 0 failed.");
      goto errLabel;
    }
    
    if((rc = define_columns(h, "col1", kId1, dim1N, dim1V )) != kOkRC )
    {
      rc = cwLogError(rc,"Define column 1 failed.");
      goto errLabel;
    }
    
    if((rc = define_columns(h, "col2", kId2, dim2N, dim2V )) != kOkRC )
    {
      rc = cwLogError(rc,"Define column 2 failed.");
      goto errLabel;
    }

    if((rc = define_columns(h, "col3", kId3, dim3N, dim3V )) != kOkRC )
    {
      rc = cwLogError(rc,"Define column 3 failed.");
      goto errLabel;
    }
    
    for(unsigned i=0; i<3; ++i)
    {

      val0[0] = i;

      write( h, kId0, val0, dim0V[0] );
      write( h, kId1, val1, dim1V[0] );

      dim2V[1] = 3;
      write( h, kId2, val2, dim2V[0]*dim2V[1], dim2V, dim2N );
      write( h, kId3, val3, dim3V[0]*dim3V[1] );
      write_record(h);
    }
    
  }
  
 errLabel:  
  destroy(h);
  mem::release(outFn);

  return rc;
}

//----------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------
namespace cw
{
  namespace dataset
  {
    namespace rdr
    {
      enum
      {
        kSizeofRecordHeader = sizeof(unsigned)
      };

      typedef struct cache_str
      {
        file::handle_t  fH;
        unsigned        totalRecdN;   // Total count of records in the file
        std::uint8_t *  buf;          // File buffer memory
        unsigned        bufMaxByteN;  // Allocated size of buf[]
        unsigned        bufByteN;     // Bytes in buf[]

        unsigned        baseFileOffs; // Offset of the first record in the file
        
        unsigned*       tocV;       // tocV[tocN] Cached record byte offsets
        unsigned        tocN;       // Count of records in the cache
        unsigned        tocBaseIdx; // Record index of the first record in the cache
        unsigned        tocIdx;     // Record index of next record to return
        unsigned        state;      // See rdr::k???State
        bool            shuffleFl;  // shuffle the file buffer each time it is filled
        
      } cache_t;

      // Backup the file position to the beginning of the last (partial) record in the cache.
      // Note that the last record overlaps the end of the cache is is therefore incomplete.
      rc_t _cache_backup( cache_t* p, unsigned actByteN, unsigned cacheByteN )
      {
        rc_t rc = kOkRC;
        
        if( p->state == kEofRC )
         return kEofRC;

        assert( actByteN >= cacheByteN);
        
        if((rc = file::seek(p->fH,file::kCurFl, -(int)(actByteN-cacheByteN) )) != kOkRC )
          return cwLogError(rc,"Dataset rdr cache align failed.");
          
        return rc;
      }

      // Count the records in the case and re-align the current file position to the last (partial) record in the cache
      rc_t _cache_count_and_align( cache_t* p, unsigned actByteN )
      {
        p->bufByteN = 0;
        for(p->tocN=0; p->bufByteN < actByteN; ++p->tocN )
        {
          if( p->bufByteN + kSizeofRecordHeader >= actByteN )
          {
            _cache_backup( p, actByteN, p->bufByteN);
            break;
          }
          
          unsigned recdByteN = *reinterpret_cast<unsigned*>(p->buf + p->bufByteN);

          // TODO: handle case where the whole buffer has less than one record
          if( p->tocN==0 && actByteN < kSizeofRecordHeader + recdByteN )
          {
            assert(0);
          }

          if( p->bufByteN + recdByteN > actByteN )
          {
            _cache_backup( p, actByteN, p->bufByteN);
            break;
          }

          p->bufByteN += kSizeofRecordHeader + recdByteN;          
        }

        return kOkRC;
      }

      void _cache_shuffle_toc( cache_t* p )
      {
        // for each record address in tocV[]
        for(unsigned i=0; i<p->tocN; ++i)
        {
          // generate a random index into tocV[]
          unsigned idx = math::randUInt(0,p->tocN-1);

          // swap location i with a random location
          unsigned tmp = p->tocV[i];
          p->tocV[i]   = p->tocV[idx];
          p->tocV[idx] = tmp;          
        }
        
      }
      
      void _cache_fill_toc( cache_t* p )
      {
        unsigned cacheByteOffs = 0;
        
        for(unsigned i=0; i<p->tocN; ++i)
        {
          p->tocV[i] = cacheByteOffs;
          
          unsigned recdByteN = *reinterpret_cast<unsigned*>(p->buf + cacheByteOffs);

          cacheByteOffs += kSizeofRecordHeader + recdByteN;          
        }
      }
      
      
      rc_t _cache_fill( cache_t* p )
      {
        rc_t     rc       = kOkRC;
        unsigned actByteN = 0;

        // Note that his function is always called when the file is pointing to the
        // record length at the start of a record
        
        // Fill the cache with as much data as possible from the file
        if((rc = file::read(p->fH, p->buf, p->bufMaxByteN, &actByteN)) != kOkRC )
        {
          if(rc == kEofRC)
            p->state = kEofState;
          else
            return cwLogError(rc,"dataset rdr cache fill failed.");          
        }

        // Get a count of the records in the cache (p->tocN) and adjust the file position such that
        // it is left pointing to the beginning of the first record after the cache.
        if((rc = _cache_count_and_align(p,actByteN)) != kOkRC )
          return rc;

        // alllocate the TOC
        p->tocV   = mem::resize<unsigned>(p->tocV,p->tocN);

        // Fill the p->tocV[]
        _cache_fill_toc(p);

        if( p->shuffleFl)
          _cache_shuffle_toc(p);

        return rc;
      }

      rc_t _cache_rewind( cache_t*  p )
      {
        rc_t rc;
        
        // rewind the file to the beginning of the 
        if(( rc = file::seek(p->fH,file::kBeginFl,p->baseFileOffs)) != kOkRC )
        {
          rc = cwLogError(rc,"rdr cache file seek failed.");
          goto errLabel;
        }
        
        if((rc = _cache_fill(p)) != kOkRC )
          goto errLabel;
        
        p->tocBaseIdx = 0;
        p->tocIdx     = 0;
        
      errLabel:
        return rc;
      }

      rc_t _cache_advance( cache_t* p )
      {
        rc_t rc = kOkRC;
        unsigned n = p->tocN;
        if((rc = _cache_fill(p)) != kOkRC )
          goto errLabel;

        p->tocBaseIdx += n;

      errLabel:
        return rc;
      }

      rc_t cache_setup( cache_t* p, file::handle_t fH, unsigned bufMaxByteN, unsigned baseFileOffs, unsigned totalRecordN, bool shuffleFl )
      {
        rc_t rc = kOkRC;
        
        p->fH           = fH;
        p->buf          = mem::alloc<uint8_t>( bufMaxByteN );
        p->bufMaxByteN  = bufMaxByteN;
        p->bufByteN     = 0;
        p->baseFileOffs = baseFileOffs;
        p->state        = kOkState;
        p->totalRecdN   = totalRecordN;
        p->shuffleFl    = shuffleFl;
        rc = _cache_rewind(p);

        return rc;       
      }

      rc_t cache_close( cache_t* p )
      {
        mem::release(p->tocV);
        mem::release(p->buf);
        return kOkRC;
      }

      rc_t cache_read( cache_t* p, const std::uint8_t*& recdRef, unsigned& recdByteN )
      {
        rc_t rc = kOkRC;
        unsigned tocIdx;
        
        if( p->tocIdx == p->totalRecdN )
        {
          rc       = kEofRC;
          p->state = kEofState;
          goto errLabel;
        }

        if( p->tocIdx == p->tocBaseIdx + p->tocN )
          if((rc = _cache_advance(p)) != kOkRC )
            goto errLabel;

        
        tocIdx = p->tocIdx - p->tocBaseIdx;
        
        recdByteN = *reinterpret_cast<unsigned*>(p->buf + p->tocV[ tocIdx ]);
        
        recdRef = p->buf + (kSizeofRecordHeader + p->tocV[ tocIdx ]);

        p->tocIdx += 1;

      errLabel:
        return rc;
        
      }

      rc_t cache_seek( cache_t* p, unsigned recordIdx )
      {
        rc_t rc = kOkRC;

        if( recordIdx >= p->totalRecdN )
          return cwLogError(kSeekFailRC,"rdr cache seek index %i greater than last index: %i.",recordIdx,p->totalRecdN-1);

        // if the requested record index is inside the cache
        if( p->tocBaseIdx <= recordIdx && recordIdx < p->tocBaseIdx + p->tocN )
          p->tocIdx = recordIdx;
        else
        {          
          // if the requested record index is prior to the cache
          if( recordIdx < p->tocBaseIdx )
            if((rc = _cache_rewind(p)) != kOkRC )
              goto errLabel;

          // recordIdx now must be past the beginning of the cache
          assert( recordIdx >= p->tocBaseIdx );

          // advance the cache until recordIdx is inside of it
          while( recordIdx >= p->tocBaseIdx + p->tocN )
          {
            if((rc = _cache_advance(p)) != kOkRC )
              goto errLabel;
          }

          assert( p->tocBaseIdx <= recordIdx && recordIdx < p->tocBaseIdx + p->tocN );
          
          p->tocIdx = recordIdx;
            
        }

      errLabel:
        return rc;
          
      }
      
      typedef struct 
      {
        col_t     col;          // Public record
        unsigned* varDimIdxV;   // varDimIdxV[] Dimension indexes that are variable in this column.
        unsigned  varDimIdxN;   // Count of values in varDimIdxV[].
      } c_t;

      typedef struct rdr_str
      {
        c_t*           colA;          // colA[ column_count ] Per column data.
        unsigned       column_count;  // Count of elements in colA[].
        unsigned       record_count;  // Count of total examples.
        file::handle_t fH;            // Backing data file handle.
        const std::uint8_t*  buf;           // buf[ bufMaxByteN ] File read buffer
        unsigned       bufMaxByteN;   // Allocated size of buf[] in bytes. (also sizeof fixed size records)
        unsigned       bufCurByteN;   // Current count of bytes used in buf[].
        bool           isFixedSizeFl; // True if all fields are fixed size

        unsigned       flags;            // kShuffleFl
        unsigned       curRecordIdx;     // Index of record in buf[].
        unsigned       nextRecordIdx;    // Index of the next record to read.
        long           baseFileByteOffs; // File byte offset of the first data record

        cache_t*       cache;
        
        unsigned       state;  // See k???State enum

        
      } rdr_t;

      typedef struct type_str
      {
        const char* label;
        unsigned    typeId;
        unsigned    variantFl;       
      } type_t;

      type_t _typeRefA[] = {
        { "int",    kIntRdrFl,    variant::kInt32VFl  },
        { "float",  kFloatRdrFl,  variant::kFloatVFl  },
        { "double", kDoubleRdrFl, variant::kDoubleVFl },
        { nullptr, 0, 0 }
      };
      
      rdr_t* _handleToPtr(handle_t h )
      { return handleToPtr<handle_t,rdr_t>(h); }


      const type_t* _typeIdToDesc( unsigned typeId )
      {
        for(const type_t* t=_typeRefA; t->label!=nullptr; ++t)
          if( t->typeId == typeId )
            return t;

        cwLogError(kInvalidArgRC,"The dataset rdr typeId %i is not valid.", typeId);
        return nullptr;
      }

      const type_t* _varTypeFlagsToDesc( unsigned variantFl )
      {
        for(const type_t* t=_typeRefA; t->label!=nullptr; ++t)
          if( t->variantFl == variantFl )
            return t;

        return nullptr;
      }
      
      const char* _typeIdToLabel( unsigned typeId )
      {
        const type_t* t;
        if((t = _typeIdToDesc(typeId)) == nullptr )
          return nullptr;
        return t->label;
      }

      bool _typeIdMatch( unsigned typeId, unsigned variantTypeFl )
      {
        const type_t* t;
        
        if((t = _typeIdToDesc(typeId)) == nullptr )
          return false;

        return t->typeId==typeId && t->variantFl==variantTypeFl;
      }

      const c_t* _colFromId( rdr_t* p, unsigned columnId )
      {
        for(unsigned i=0; i<p->column_count; ++i)
          if( p->colA[i].col.id == columnId )
            return p->colA + i;

        cwLogError(kInvalidArgRC,"Invalid columnId (%i).", columnId );
        return nullptr;
      }

      const c_t* _colFromLabel( rdr_t* p, const char* colLabel )
      {
        for(unsigned i=0; i<p->column_count; ++i)
          if( textCompare(p->colA[i].col.label, colLabel) == 0 )
            return p->colA + i;

        cwLogError(kInvalidArgRC,"Invalid column label:%s.", colLabel );
        return nullptr;
      }
      
      rc_t _destroy( rdr_t* p )
      {
        for(unsigned i=0; i<p->column_count; ++i)
        {
          mem::release( p->colA[i].col.dimV );
          mem::release( p->colA[i].col.maxDimV );
          mem::release( p->colA[i].varDimIdxV);
          mem::free( const_cast<char*>(p->colA[i].col.label) );          
        }

        cache_close(p->cache);
        mem::release(p->cache);
        file::close(p->fH);
        mem::release(p->colA);
        //mem::free(const_cast<std::uint8_t*>(p->buf));
        mem::release(p);
        
        return kOkRC;
      }

      rc_t _readHdr( rdr_t* p, unsigned cacheByteN, unsigned flags )
      {
        rc_t     rc           = kOkRC;
        unsigned bufOffsByteN = 0;
      
        p->bufMaxByteN = 0;
        p->isFixedSizeFl = true;
      
        if((rc = read(p->fH,p->record_count)) != kOkRC ) goto errLabel;
        if((rc = read(p->fH,p->column_count))  != kOkRC ) goto errLabel;
        
        p->colA = mem::allocZ<c_t>( p->column_count);

        // for each column
        for(unsigned i=0; i<p->column_count; ++i)
        {
          c_t* c = p->colA + i;
        
          if((rc = readStr( p->fH,(char**)&c->col.label,255)) != kOkRC ) goto errLabel;
          if((rc = read(p->fH,c->col.id))                 != kOkRC ) goto errLabel;
          if((rc = read(p->fH,c->col.varDimN))            != kOkRC ) goto errLabel;
          if((rc = read(p->fH,c->col.rankN ))             != kOkRC ) goto errLabel;
          if((rc = read(p->fH,c->col.maxEleN ))           != kOkRC ) goto errLabel;        
          if((rc = variant::read(  p->fH, c->col.max))    != kOkRC ) goto errLabel;
          if((rc = variant::read(  p->fH, c->col.min ))   != kOkRC ) goto errLabel;

          
          c->col.dimV    = mem::allocZ<unsigned>( c->col.rankN );
          c->col.maxDimV = mem::allocZ<unsigned>( c->col.rankN );
          c->varDimIdxV  = mem::allocZ<unsigned>( c->col.rankN );
          c->varDimIdxN  = 0;
          
          c->col.maxEleN = c->col.rankN==0 ? 0 : 1;
          
          for(unsigned j=0; j<c->col.rankN; ++j)
          {
            if((rc = file::read( p->fH, c->col.dimV[j] ))   != kOkRC ) goto errLabel;
            if((rc = file::read( p->fH, c->col.maxDimV[j])) != kOkRC ) goto errLabel;

            if( c->col.dimV[j] == 0 )
              c->varDimIdxV[c->varDimIdxN++] = j;

            c->col.maxEleN *= c->col.maxDimV[j];
          }

          unsigned bytesPerEle = variant::flagsToBytes(c->col.max.flags);

          const type_t* t;
          if((t = _varTypeFlagsToDesc(c->col.max.flags)) == nullptr )
            rc = cwLogError(kInvalidDataTypeRC,"The column %s is not a valid data type (e.g. int, float double).",cwStringNullGuard(c->col.label));
          else
            c->col.typeId = t->typeId;

          // TODO: why maintain both eleN and maxEleN and byteN and maxByteN?
          c->col.eleN       = c->col.maxEleN;
          c->col.maxByteN   = bytesPerEle * c->col.maxEleN;
          c->col.byteOffset = bufOffsByteN;
          c->col.byteN      = c->col.maxByteN;

          p->bufMaxByteN += c->col.maxByteN + c->varDimIdxN * sizeof(unsigned); // Track the max file buffer size

          if( c->col.varDimN != 0 && p->isFixedSizeFl  )
            p->isFixedSizeFl = false;
          
          bufOffsByteN = p->bufMaxByteN;
        }

        p->buf  = nullptr; //mem::alloc<std::uint8_t>(p->bufMaxByteN);
        p->cache = mem::allocZ<cache_t>(1);
        
        // store the file offset to the first data record
        if((rc = tell(p->fH,&p->baseFileByteOffs)) != kOkRC )
        {
          rc = cwLogError(rc,"rdr dataset tell file position failed.");
          goto errLabel;
        }

        rc = cache_setup( p->cache, p->fH, cacheByteN, p->baseFileByteOffs, p->record_count, cwIsFlag(flags,kShuffleFl) );

        
      errLabel:
        if( rc != kOkRC )
        {
          rc = cwLogError(rc,"Data set file header read failed.");
          p->state = kErrorState;
        }
      
        return rc;
      }


      // Seek to the a record, but don't actually read it.
      rc_t _seek( rdr_t* p, unsigned recdIdx )
      {
        rc_t rc;
        if((rc = cache_seek(p->cache,recdIdx)) != kOkRC )
          p->state = p->cache->state;
        return rc;
      }
      
      rc_t _parse_var_record( rdr_t* p )
      {
        rc_t rc = kOkRC;
        
        p->bufCurByteN = 0;
   
        for(unsigned i=0; i<p->column_count; ++i)
        {
          c_t* c = p->colA + i;
          
          // if this is a variabled sized column
          if( c->col.varDimN != 0 )
          {
            const unsigned* varDimV = reinterpret_cast<const unsigned*>(p->buf + p->bufCurByteN );
            unsigned  eleN    = c->col.rankN==0 ? 0 : 1;

            // for each dim. of this column
            for(unsigned j=0,k=0; j<c->col.rankN; ++j)
            {
              // if this is a variable sized dimension then set the actual dim. size
              if( k<c->varDimIdxN && c->varDimIdxV[k] == j )
              {
                c->col.dimV[j] = varDimV[k];
                k += 1;
                p->bufCurByteN += sizeof(varDimV[k]);
              }

              // calc the count of elements in this field
              eleN *= c->col.dimV[j];
            }
            
            // set the size and count of elements in this field 
            c->col.eleN  = eleN;
            c->col.byteN = variant::flagsToBytes( c->col.max.flags ) * eleN;
          }
          
          c->col.byteOffset = p->bufCurByteN;
          p->bufCurByteN   += c->col.byteN;
        
        }
        return rc;
      }
      

      rc_t _read_record( rdr_t* p )
      {
        rc_t rc = kOkRC;
        
        unsigned recordByteN;

        if((rc = cache_read( p->cache, p->buf, recordByteN )) != kOkRC )
        {
          p->state = p->cache->state;
          goto errLabel;
        }
        
        // if all columns in the record do not have a fixed size then update
        // the column pointers into the data record
        if( !p->isFixedSizeFl )
          if((rc = _parse_var_record( p )) != kOkRC )
            goto errLabel;
        
        p->curRecordIdx = p->nextRecordIdx;
        p->nextRecordIdx += 1;
      errLabel:
        return rc;
      }

      rc_t _get( rdr_t* p, unsigned columnId, const void*& vpRef, unsigned& nRef, const unsigned*& dimVRef, unsigned reqTypeId )
      {
        const c_t* c;;
  
        if((c = _colFromId(p,columnId)) == nullptr )
          return kInvalidArgRC;

        if( c->col.typeId != reqTypeId )
          return cwLogError(kInvalidArgRC,"Cannot convert the column '%s' from type:%s to type:%s.", _typeIdToLabel(c->col.typeId), _typeIdToLabel(reqTypeId));
  
        nRef    = c->col.eleN;
        dimVRef = c->col.dimV;
        vpRef   = p->buf + c->col.byteOffset;
        return kOkRC;
      }
      
    }   
  }  
}

cw::rc_t cw::dataset::rdr::create( handle_t& h, const char* fn, unsigned cacheBufByteN, unsigned flags )
{
  rc_t rc;
  if((rc = destroy(h)) != kOkRC )
    return rc;

  auto p = mem::allocZ<rdr_t>(1);
  
  if((rc = file::open(p->fH, fn,file::kReadFl)) == kOkRC )
    if((rc = _readHdr(p,cacheBufByteN,flags)) != kOkRC )
      goto errLabel;

  p->state        = kOkState;
  p->curRecordIdx = kInvalidIdx;
  p->flags        = flags;
  h.set(p);

 errLabel:
  if(rc != kOkRC )
    _destroy(p);

  return rc;
}

cw::rc_t cw::dataset::rdr::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
      
  if( !h.isValid())
    return rc;

  rdr_t* p = _handleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;  
}

unsigned cw::dataset::rdr::column_count( handle_t h )
{
  rdr_t* p = _handleToPtr(h);
  return p->column_count;
}

const cw::dataset::rdr::col_t* cw::dataset::rdr::column_cfg( handle_t h, unsigned colIdx )
{
  rdr_t* p = _handleToPtr(h);
  
  if( colIdx >= p->column_count )
    return nullptr;
  
  return &p->colA[colIdx].col;
}

const cw::dataset::rdr::col_t* cw::dataset::rdr::column_cfg( handle_t h, const char* colLabel )
{
  rdr_t* p = _handleToPtr(h);
  const c_t* c;

  if((c = _colFromLabel(p, colLabel )) == nullptr )
    return nullptr;

  return &c->col;
}

unsigned cw::dataset::rdr::record_count( handle_t h)
{
  rdr_t* p = _handleToPtr(h);
  return p->record_count;
}

unsigned cw::dataset::rdr::cur_record_index(  handle_t h )
{
  rdr_t* p = _handleToPtr(h);
  return p->curRecordIdx;
}

unsigned cw::dataset::rdr::next_record_index( handle_t h )
{
  rdr_t* p = _handleToPtr(h);
  return p->nextRecordIdx;
}

unsigned cw::dataset::rdr::state( handle_t h )
{
  rdr_t* p = _handleToPtr(h);
  return p->state;
}

cw::rc_t cw::dataset::rdr::seek( handle_t h, unsigned recordIdx )
{
  rdr_t* p = _handleToPtr(h);
  return _seek(p,recordIdx);
}


cw::rc_t cw::dataset::rdr::read( handle_t h, unsigned record_index )
{
  rc_t rc = kOkRC;
  rdr_t* p  = _handleToPtr(h);
  
  if( record_index != kInvalidIdx )
    if((rc = _seek(p,record_index)) != kOkRC )
      return rc;
  
  return _read_record(p);
}

cw::rc_t cw::dataset::rdr::get( handle_t h, unsigned columnId, const int*&    vRef, unsigned& nRef, const unsigned*& dimVRef )
{
  rdr_t* p  = _handleToPtr(h);
  const void*  vp = nullptr;
  rc_t   rc = _get(p, columnId, vp, nRef, dimVRef, kIntRdrFl );

  vRef = rc!=kOkRC ? nullptr : static_cast<const int*>(vp);

  return rc;
}

cw::rc_t cw::dataset::rdr::get( handle_t h, unsigned columnId, const float*&  vRef, unsigned& nRef, const unsigned*& dimVRef )
{
  rdr_t* p  = _handleToPtr(h);
  const void*  vp = nullptr;
  rc_t   rc = _get(p, columnId, vp, nRef, dimVRef, kFloatRdrFl );

  vRef = rc!=kOkRC ? nullptr : static_cast<const float*>(vp);

  return rc;
}

cw::rc_t cw::dataset::rdr::get( handle_t h, unsigned columnId, const double*& vRef, unsigned& nRef, const unsigned*& dimVRef )
{
  rdr_t* p  = _handleToPtr(h);
  const void*  vp = nullptr;
  rc_t   rc = _get(p, columnId, vp, nRef, dimVRef, kDoubleRdrFl );

  vRef = rc!=kOkRC ? nullptr : static_cast<const double*>(vp);

  return rc;
}
 
cw::rc_t cw::dataset::rdr::report( handle_t h )
{
  rc_t   rc = kOkRC;
  rdr_t* p  = _handleToPtr(h);

  for(unsigned i=0; i<p->column_count; ++i)
  {
    const c_t* c = p->colA + i;
    printf("id:%5i vdN:%5i mxEleN:%5i rank:%3i %8s", c->col.id, c->col.varDimN, c->col.maxEleN, c->col.rankN, _typeIdToLabel(c->col.typeId) );

    printf(" min:"); variant::print(c->col.min);
    printf(" max:"); variant::print(c->col.max);
      
    printf(" | ");
      
    for(unsigned j=0; j<c->col.rankN; ++j)
      printf("%i ",c->col.dimV[j]);

    printf(" | ");
        
    for(unsigned j=0; j<c->col.rankN; ++j)
      printf("%i ",c->col.maxDimV[j]);

    printf("\n");
  }
  
  return rc;
}

cw::rc_t cw::dataset::rdr::test( const object_t* cfg )
{
  rc_t     rc   = kOkRC;
  char*    inFn = nullptr;
  unsigned cacheByteN = 128;
  handle_t h;
  
  if((rc = cfg->getv("inFn",inFn,"cacheByteN",cacheByteN)) != kOkRC )
    return cwLogError(rc,"rdr test failed.  Argument parse failed.");

  inFn = filesys::expandPath(inFn);

  if((rc = create(h,inFn,cacheByteN,kShuffleFl)) != kOkRC )
  {
    rc = cwLogError(rc,"rdr create failed.");
  }
  else
  {
    const int*      v    = nullptr;
    unsigned        vN   = 0;
    const unsigned* dimV = nullptr;
    
    report(h);

    while( (rc=read(h)) == kOkRC )
    {
      get(h,0,v,vN,dimV); vop::print(v,vN,"%i ","c0:");
      get(h,1,v,vN,dimV); vop::print(v,vN,"%i ","c1:");
      get(h,2,v,vN,dimV); vop::print(v,vN,"%i ","c2:");
      get(h,3,v,vN,dimV); vop::print(v,vN,"%i ","c3:");
    }

    if( rc != kEofRC )
      rc = cwLogError(kOpFailRC,"The read operation failed.");
    
    destroy(h);
  }

  mem::release(inFn);
  return rc;
}

//----------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------
namespace cw {
  namespace dataset {
    namespace adapter {
      
      typedef struct col_str
      {
        const rdr::col_t* col;           // Column description
        bool              oneHotFl;      // Convert this column to a one-hot vector
        unsigned          maxEleN;       // Max count of elements in the buffer from this column
        int               oneHotMax;     // Max value in this column
        int               oneHotMin;     // Min value in this column
        unsigned*         batchDimV;     // batchDivV[  col.rankN, batchN ] or nullptr for fixed size columns
        struct col_str*   link;          // 
      } col_t;
      
      typedef struct field_str
      {
        unsigned          id;               // Field Id
        unsigned          flags;            // Field flags 
        bool              isFixedSizeFl;    // Do all columns in this field have a fixed size.
        unsigned          bytesPerEle;      // Size of each element in buf[] (determined by flags | k<DataType>fl)
        unsigned          bufMaxEleN;       // Allocated size of buf[] for a batch size of maxBatchN 
        unsigned          bufEleN;          // Current count of elements in buf[] for the entire batch.
        unsigned          bufMaxFieldByteN; // Max. size in bytes  of one field record.
        unsigned          bufByteN;         // Current count of bytes in buf.
        std::uint8_t*     buf;              // buf[ bufMaxFieldByteN*batchN ]
        unsigned*         batchEleNV;       // batchEleN[ maxBatchN ] Count of ele's in each record of a batch.
        col_t*            colL;             // List of columns assigned to this field
        colMap_t**        colMapM;          // colMapM[ batchN ]
        colMap_t*         colMapA;          // colMapA[ batchN*columnN ] Storage for colMapM[]
        struct field_str* link;             // 
      } field_t;
        
      typedef struct adapter_str
      {
        unsigned      maxBatchN;  // Max. possible value of batchN in a call to read().
        unsigned      batchN;     // Count of records returned in the last call to read().
        rdr::handle_t rdrH;       // Source data file 
        field_t*      fieldL;     // List of field descriptions
        unsigned      state;      // Exception state
      } adapter_t;

      inline adapter_t* _handleToPtr(handle_t h )
      { return handleToPtr<handle_t,adapter_t>(h); }

      rc_t _destroy( adapter_t* p )
      {
        rc_t rc = kOkRC;

        field_t* f = p->fieldL;
        while( f != nullptr )
        {
          field_t* f0 = f->link;

          col_t* c = f->colL;
          while( c != nullptr )
          {
            col_t* c0 = c->link;
            
            // if this is a var width column
            if( c->col->varDimN > 0 )
              mem::release(c->batchDimV);
            mem::release(c);
            c = c0;
          }

          mem::release(f->batchEleNV);
          mem::release(f->buf);
          mem::release(f->colMapM);
          mem::release(f->colMapA);
          mem::release(f);
          f=f0;
        }

        rdr::destroy(p->rdrH);
        
        mem::release(p);
        return rc;
      }

      field_t* _fieldIdToRecd( adapter_t* p, unsigned fieldId )
      {
        field_t* f = p->fieldL;
        for(; f!=nullptr; f=f->link)
          if( f->id == fieldId )
            return f;

        cwLogError(kInvalidArgRC,"Invalid field id '%i'.",fieldId);
        
        return nullptr;
      }

      rc_t _calc_one_hot_ele_count( col_t* c, unsigned& eleN_Ref )
      {
        rc_t rc = kOkRC;
        
        if( !variant::isInt(c->col->min) || !variant::isInt(c->col->max) )
          return cwLogError(kInvalidArgRC,"One-hot columns must be integer valued.");

        if( c->col->rankN != 1 || c->col->maxDimV[0] != 1 )
          return cwLogError(kInvalidArgRC,"One-hot columns must be scalar integers.");

        
        if((rc = variant::get(c->col->min,c->oneHotMin)) != kOkRC )
          return cwLogError(rc,"Unable to obtain the one-hot minimum value.");

        if((rc = variant::get(c->col->max,c->oneHotMax)) != kOkRC )
          return cwLogError(rc,"Unable to obtain the maximum value.");
            
        eleN_Ref = (c->oneHotMax - c->oneHotMin) + 1;  
                
        return rc;
      }

      rc_t _assign_column( adapter_t* p, field_t* f, const char* colLabel, bool oneHotFl )
      {
        rc_t  rc = kOkRC;
        col_t* c = mem::allocZ<col_t>(1);
        
        if((c->col = rdr::column_cfg(p->rdrH, colLabel)) == nullptr )
          rc = kInvalidArgRC;
        else
        {
          c->oneHotFl = oneHotFl;

          // locate the last link in the column list
          col_t* c0 = f->colL;
          while( c0!=nullptr && c0->link != nullptr )
            c0=c0->link;

          // add the new record to the end of the list
          if( c0 == nullptr )
            f->colL = c;
          else
            c0->link = c;

          // if one-hot encoding was requested
          if( oneHotFl )
            rc = _calc_one_hot_ele_count(c,c->maxEleN);          
          else
            c->maxEleN = c->col->maxEleN;

          // update the size of the field buffer to account for the column size
          f->bufMaxEleN += c->col->maxEleN;

          // if this is a variable length column
          if( c->col->varDimN > 0 )
            f->isFixedSizeFl = false;

          if( cwIsFlag(f->flags,kTrackColDimFl) )
          {
            // if this is a fixed size column then batchDimV is null
            // otherwise it is a [batchN,rankN] matrix used to hold the dim's of each returned data ele from this column
            c->batchDimV = c->col->varDimN == 0 ? nullptr : mem::allocZ<unsigned>(p->maxBatchN*c->col->rankN);
          }
        }
        
        if( rc != kOkRC )
          rc = cwLogError(rc,"'%s' Column assignment failed.", cwStringNullGuard(colLabel));
        
        return rc;
      }

      rc_t _allocate_field_buffer( adapter_t* p, field_t* f )
      {
        rc_t rc       = kOkRC;
        f->bufMaxEleN = 0;

        // calc the field width as the sum of the max column widths
        unsigned colN = 0;
        for(col_t* c=f->colL; c!=nullptr; c=c->link)
        {
          f->bufMaxEleN += c->maxEleN;
          colN          += 1;
        }

        f->bufMaxFieldByteN = f->bufMaxEleN * f->bytesPerEle;
        f->buf = mem::alloc<std::uint8_t>(p->maxBatchN * f->bufMaxFieldByteN);

        // if col. dim tracking is enabled for this field
        if( cwIsFlag(f->flags,kTrackColDimFl) )
        {
          // allocate the column dim tracking data structures
          f->colMapM = mem::allocZ<colMap_t*>( p->maxBatchN );
          f->colMapA = mem::allocZ<colMap_t>( p->maxBatchN * colN );

          // initialize the fixed portion of the col. tracking records
          for(unsigned i=0; i<p->maxBatchN; ++i)
          {
            f->colMapM[i] = f->colMapA + i*colN;

            // for batch index i for each column
            unsigned j=0, eleOffs=0;
            for(col_t* c=f->colL; c!=nullptr; c=c->link,++j)
            {
              f->colMapM[i][j].colId = c->col->id;
              f->colMapM[i][j].rankN = c->col->rankN;

              // if this is a fixed size field then the col. map can be completely populated in advance of reading the data
              // TODO: don't allocate the complete colMapA[] array because every colN records are duplicates anyway.
              // just point  colMapM[] to a single row of colMapA[].
              if( !f->isFixedSizeFl )
              {
                f->colMapM[i][j].eleN           = c->oneHotFl ? c->maxEleN : c->col->eleN;
                f->colMapM[i][j].fieldEleOffset = eleOffs;
                f->colMapM[i][j].dimV           = c->col->dimV;
                
                eleOffs += c->col->eleN;
              }
              else
              {
                f->colMapM[i][j].dimV  = c->batchDimV + (i*c->col->rankN);
              }
            }
          }
        }
        return rc;
      }

      template< typename S, typename D >
        rc_t _translate_one_hot( std::uint8_t* buf, unsigned bufByteN, const S* src, unsigned srcEleN, const col_t* c, unsigned& dstByteNRef )
      {
        rc_t rc = kOkRC;

        dstByteNRef = 0;
        
        unsigned dstEleN = (c->oneHotMax - c->oneHotMin) + 1;
        unsigned dstByteN = dstEleN * sizeof(D);

        if( dstByteN > bufByteN )
          return cwLogError(kBufTooSmallRC,"The field buffer is too small (src:%i > buf:%i) during one-hot conversion.",dstByteN,bufByteN);

        if( srcEleN != 1 )
          return cwLogError(kInvalidArgRC,"One-hot encoded fields must be scalars. (srcEleN:%i)",srcEleN);

        unsigned oneHotIdx = src[0] - c->oneHotMin;

        if( oneHotIdx >= dstEleN )
          return cwLogError(kInvalidArgRC,"The one-hot index (%i) is out of the one-hot vector size:%i.",oneHotIdx,dstEleN);
        
        memset(buf,0,dstByteN);
        
        D* dst = reinterpret_cast<D*>(buf);
        dst[ oneHotIdx ] = 1;

        dstByteNRef = dstByteN;
        
        return rc;
      }
      
      template< typename S, typename D >
        rc_t _translate_datatype( const col_t* c, std::uint8_t* buf, unsigned bufByteN, const S* src, unsigned srcEleN, unsigned& dstByteNRef )
      {
        if( c->oneHotFl )
          return _translate_one_hot<S,D>( buf, bufByteN, src, srcEleN, c, dstByteNRef );

            
        unsigned dstByteN = srcEleN * sizeof(D);
        D*       dst      = reinterpret_cast<D*>(buf);

        dstByteNRef = 0;
        
        if( dstByteN > bufByteN )          
          return cwLogError(kBufTooSmallRC,"The field buffer is too small (src:%i > buf:%i).",dstByteN,bufByteN);

        // copy, and translate, the rdr::col into the field->buf[]
        for(unsigned i=0; i<srcEleN; ++i)
          dst[i] = src[i];

        dstByteNRef =  dstByteN;
        
        return kOkRC;
              
      }

      template< typename T >
        rc_t _translate_column_tpl(adapter_t* p, field_t* f, col_t* c, std::uint8_t* buf, unsigned bufN, unsigned& dstByteNRef)
      {
        rc_t            rc   = kOkRC;
        const T*         v   = nullptr;
        unsigned        vN   = 0;     
        const unsigned* dimV = nullptr;

        // read the column
        if((rc = rdr::get(p->rdrH, c->col->id, v, vN, dimV ))  != kOkRC )
          return rc;

        switch( f->flags & kTypeMask )
        {
          case kIntFl:    rc = _translate_datatype<T,int>(    c, buf, bufN, v, vN, dstByteNRef ); break;
          case kFloatFl:  rc = _translate_datatype<T,float>(  c, buf, bufN, v, vN, dstByteNRef ); break;
          case kDoubleFl: rc = _translate_datatype<T,double>( c, buf, bufN, v, vN, dstByteNRef ); break;
          default:
            assert(0);
        }


        return rc;
      }

      rc_t _translate_column( adapter_t* p, field_t* f, col_t* c, std::uint8_t* buf, unsigned bufN, unsigned& dstByteNRef )
      {
        rc_t rc = kOkRC;
        
        switch( c->col->typeId )
        {
          case rdr::kIntRdrFl:    rc = _translate_column_tpl<int>(   p,f,c,buf,bufN,dstByteNRef); break;
          case rdr::kFloatRdrFl:  rc = _translate_column_tpl<float>( p,f,c,buf,bufN,dstByteNRef); break;
          case rdr::kDoubleRdrFl: rc = _translate_column_tpl<double>(p,f,c,buf,bufN,dstByteNRef); break;
          default:
            assert(0);
        }

        return rc;
      }

      rc_t _read_field( adapter_t* p, unsigned batchIdx, field_t* f, unsigned& byteNRef )
      {
        rc_t rc = kOkRC;

        byteNRef = 0;

        // on the first use the buffer will not yet be allocated
        if( f->buf == nullptr )
          if((rc = _allocate_field_buffer(p,f)) != kOkRC )
            return rc;

        unsigned availBufByteN    = f->bufMaxFieldByteN;  
        unsigned fieldBufByteOffs = 0;
        
        // for each column of this field
        for(col_t* c=f->colL; c!=nullptr; c=c->link)
        {
          unsigned colByteN    = 0;
          
          // translate each source column into the field buffer
          if((rc = _translate_column( p, f, c, f->buf + f->bufByteN + fieldBufByteOffs, availBufByteN, colByteN  )) != kOkRC )
            return rc;

          assert( availBufByteN >= colByteN );
          
          availBufByteN    -= colByteN;
          fieldBufByteOffs += colByteN;

          // if column dim. tracking is enabled and this is a variable with column ...
          if( cwIsFlag(f->flags,kTrackColDimFl) && c->col->varDimN>0 )
            for( unsigned i=0; i<c->col->rankN; ++i)
              c->batchDimV[ batchIdx * c->col->rankN + i] = c->col->dimV[i]; // ... get the dim's of this column
        }

        byteNRef = fieldBufByteOffs;
        return rc;
      }

      template< typename T >
        cw::rc_t _get( handle_t h, unsigned fieldId, const T*& vV, const unsigned*& nV )
      {
        rc_t       rc = kOkRC;
        adapter_t* p  = _handleToPtr(h);
        field_t*   f;

        if( p->state != kInitState )
          return cwLogError(kInvalidStateRC,"get() failed The adapter is in an invalid state (%i != %i).",p->state,kInitState);

        if((f = _fieldIdToRecd(p,fieldId)) == nullptr )
          return kInvalidArgRC;

        if(f->buf == nullptr )
          return cwLogError( kInvalidStateRC, "read() must be called begore get().");
  
        vV = reinterpret_cast<const T*>(f->buf);
        nV = f->batchEleNV;
  
        return rc;
      }

      template< typename T >
        cw::rc_t _print_field( adapter_t* p, field_t* f, const char* fmt, unsigned batchIdx, const T* v, unsigned vN )
      {
        rc_t      rc  = kOkRC;
        unsigned  i   = 0,k = 0;
        for(col_t* c=f->colL; c!=nullptr; c=c->link,++i)
        {
          colMap_t* cm = f->colMapM[batchIdx] + i;
          
          printf("| %s %i : ", c->col->label, cm->eleN );
          for(unsigned j=0; j<cm->eleN; ++j)
            printf(fmt,v[k++]);
        }
        return rc;
      }
      
      template< typename T >
        cw::rc_t _print_field( adapter_t* p, field_t* f, const char* fmt )
      {
        rc_t   rc = kOkRC;

        printf("Field:%3i \n",f->id);
        for(unsigned i=0,k=0; i<p->batchN; ++i)
        {
          printf("%i : ",i);
          
          T*       v  = reinterpret_cast<T*>(f->buf) + k;
          unsigned vN = f->batchEleNV[i];

          if( cwIsFlag(f->flags,kTrackColDimFl) )
            rc = _print_field(p,f,fmt,i,v,vN);
          else
            for(unsigned j=0; j<vN; ++j)
              printf(fmt,v[j]);

          k += vN;
          printf("\n");
        }
        
        return rc;
      }
      
    }
  }
}

cw::rc_t cw::dataset::adapter::create(  handle_t& hRef, const char* fn, unsigned maxBatchN, unsigned cacheByteN, unsigned flags )
{
  rc_t       rc = kOkRC;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  adapter_t* p = mem::allocZ<adapter_t>(1);

  if((rc = rdr::create(p->rdrH,fn,cacheByteN,flags)) != kOkRC )
    goto errLabel;
  
  p->maxBatchN = maxBatchN;
  p->state     = kPreInitState;
  
  hRef.set(p);

 errLabel:
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::dataset::adapter::destroy( handle_t& hRef )
{
  rc_t       rc = kOkRC;

  if( !hRef.isValid() )
    return rc;
      
  adapter_t* p = _handleToPtr(hRef);
    
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  
  return rc;
}

cw::rc_t cw::dataset::adapter::create_field(  handle_t h, unsigned fieldId, unsigned flags, const char* colLabel, bool oneHotFl )
{
  rc_t       rc        = kOkRC;
  adapter_t* p         = _handleToPtr(h);
  field_t*   f         = mem::allocZ<field_t>(1);
  unsigned   typeFlags = flags & kTypeMask;
  
  f->id         = fieldId;
  f->flags      = flags;
  f->link       = p->fieldL;
  f->batchEleNV = mem::alloc<unsigned>(p->maxBatchN);
  p->fieldL     = f;
  
  switch( typeFlags  )
  {
    case kIntFl:    f->bytesPerEle = sizeof(int);    break;
    case kFloatFl:  f->bytesPerEle = sizeof(float);  break;
    case kDoubleFl: f->bytesPerEle = sizeof(double); break;
    default:
      rc = cwLogError(kInvalidArgRC,"The field data type value 0x%x is not valid.", typeFlags );
  }

  if( colLabel != nullptr )
    rc = _assign_column( p, f, colLabel, oneHotFl );
  
  return rc;
}

cw::rc_t cw::dataset::adapter::assign_column( handle_t h, unsigned fieldId, const char* colLabel, bool oneHotFl )
{
  adapter_t*        p  = _handleToPtr(h);
  const rdr::col_t* c  = nullptr;
  field_t*          f;

  if(( c = rdr::column_cfg(p->rdrH,colLabel)) == nullptr )
    return kInvalidArgRC;

  if((f = _fieldIdToRecd(p,fieldId)) == nullptr )
    return kInvalidArgRC;
  
  
  return _assign_column( p, f, colLabel, oneHotFl );
}

unsigned cw::dataset::adapter::record_count( handle_t h )
{
  adapter_t* p  = _handleToPtr(h);
  return rdr::record_count(p->rdrH);
}


unsigned cw::dataset::adapter::field_fixed_ele_count( handle_t h, unsigned fieldId )
{
  adapter_t* p  = _handleToPtr(h);
  field_t*   f;
  
  if((f = _fieldIdToRecd(p,fieldId)) == nullptr )
    return 0;

  return f->bufEleN;;
}


cw::rc_t cw::dataset::adapter::read( handle_t h, unsigned batchN, const unsigned* recordIdxV )
{
  rc_t       rc = kOkRC;
  adapter_t* p  = _handleToPtr(h);

  switch( p->state )
  {
    case kInitState:
      break;
      
    case kPreInitState:
      p->state = kInitState;
      break;
      
    default:
      return cwLogError(kInvalidStateRC,"Invalid adapter state (%i != %i).",p->state,kInitState);
  }
      
  
  if( batchN > p->maxBatchN )
    return cwLogError(kInvalidArgRC,"The batch count:%i is greater than the max batch count:%i.",batchN,p->maxBatchN);

  p->batchN = 0;

  // for each record in this batch
  for(unsigned i=0; i<batchN; ++i)
  {
    // read the data record
    if((rc = rdr::read(p->rdrH, recordIdxV==nullptr ? kInvalidIdx : recordIdxV[i] )) != kOkRC )
    {
      if( rc == kEofRC )
        p->state = kEofState;
      
      goto errLabel;
    }
    
    // translate each field
    for(field_t* f=p->fieldL; f!=nullptr; f=f->link)
    {
      unsigned fieldByteN = 0;
      if( i == 0 )
      {
        f->bufEleN  = 0;
        f->bufByteN = 0;
      }

      // read the field into f->buf[]
      if((rc = _read_field(p,i,f,fieldByteN)) != kOkRC )
      {
        rc = cwLogError(rc,"Field (id:%i) read failed.",f->id);
        goto errLabel;
      }

      assert( fieldByteN % f->bytesPerEle == 0 );

      // update the buffer state
      unsigned fieldEleN  = fieldByteN / f->bytesPerEle;
      f->bufEleN         += fieldEleN;
      f->bufByteN        += fieldByteN;      
      f->batchEleNV[i]    = fieldEleN;
    }

    p->batchN += 1;
  }
 errLabel:
  if( rc != kOkRC )
    p->state = kErrorState;
  
  return rc;
}

cw::rc_t cw::dataset::adapter::get( handle_t h, unsigned fieldId, const int*& vV, const unsigned*& nV )
{ return _get<int>(h,fieldId,vV,nV);  }

cw::rc_t cw::dataset::adapter::get( handle_t h, unsigned fieldId, const float*& vV, const unsigned*& nV )
{ return _get<float>(h,fieldId,vV,nV);  }

cw::rc_t cw::dataset::adapter::get( handle_t h, unsigned fieldId, const double*& vV, const unsigned*& nV )
{ return _get<double>(h,fieldId,vV,nV);  }


cw::rc_t cw::dataset::adapter::column_map( handle_t h, unsigned fieldId, colMap_t const * const *& colMapV_Ref )
{
  rc_t       rc = kOkRC;
  adapter_t* p  = _handleToPtr(h);
  field_t*   f;

  if( p->state != kInitState )
    return cwLogError(kInvalidStateRC,"Invalid adapter state (%i != %i).",p->state,kInitState);
  
  if((f = _fieldIdToRecd(p,fieldId)) == nullptr )
    return kInvalidArgRC;

  colMapV_Ref = f->colMapM;
  
  return rc;
}


unsigned cw::dataset::adapter::state( handle_t h )
{
  adapter_t* p  = _handleToPtr(h);
  return p->state;
}

cw::rc_t cw::dataset::adapter::print_field( handle_t h, unsigned fieldId, const char* fmt )
{
  rc_t       rc = kOkRC;
  adapter_t* p  = _handleToPtr(h);
  field_t*   f;
  
  if((f = _fieldIdToRecd(p,fieldId)) == nullptr )
    return cwLogError(kInvalidArgRC,"Invalid field id (%i).",fieldId);
  
  switch( f->flags & kTypeMask )
  {
    case kIntFl:    rc = _print_field<int>(   p, f, fmt==nullptr ? "%i " : fmt ); break;
    case kFloatFl:  rc = _print_field<float>( p, f, fmt==nullptr ? "%f " : fmt ); break;
    case kDoubleFl: rc = _print_field<double>(p, f, fmt==nullptr ? "%f " : fmt ); break;
    default:
      rc = cwLogError(kInvalidArgRC,"Unknown type flag: 0x%x.",f->flags & kTypeMask);
  }
  return rc;
}


cw::rc_t cw::dataset::adapter::test( const object_t* cfg )
{
  rc_t     rc         = kOkRC;
  char*    inFn       = nullptr;
  unsigned batchN     = 0;
  unsigned cacheByteN = 128;
  unsigned shuffleFl  = rdr::kShuffleFl;
  handle_t h;

  enum {
    kField0Id = 0,
    kField1Id = 1
  };

  // read the cfg args
  if((rc = cfg->getv("inFn",inFn,"batchN",batchN,"cacheByteN",cacheByteN)) != kOkRC )
    return cwLogError(rc,"adapter test failed.  Argument parse failed.");

  inFn = filesys::expandPath(inFn);

  // create the adapter
  if((rc = create(h, inFn, batchN, cacheByteN, shuffleFl)) != kOkRC )
  {
    rc = cwLogError(rc,"Unable to create dataset adapter for '%s'.",inFn);
    goto errLabel;
  }
  else
  {
    const int*      xV  = nullptr;
    const float*    yV  = nullptr;
    const unsigned* xNV = nullptr;
    const unsigned* yNV = nullptr;
    unsigned recdIdxV[] = { 2,1,0 };
    
    if((rc = create_field(  h, kField0Id, kIntFl | kTrackColDimFl, "col0", true )) != kOkRC )
      goto errLabel;
  
    if((rc = create_field(  h, kField1Id, kFloatFl | kTrackColDimFl, "col1" )) != kOkRC )
      goto errLabel;
    
    if((rc = assign_column( h, kField1Id, "col2" )) != kOkRC )
      goto errLabel;
    
    if((rc = assign_column( h, kField1Id,  "col3" )) != kOkRC )
      goto errLabel;

    assert( cwCountOf(recdIdxV) == batchN );
    
    if((rc = read(h, batchN, recdIdxV )) != kOkRC )
      goto errLabel;
    
    if((rc = get(h, kField0Id, xV, xNV )) != kOkRC )
      goto errLabel;

    if((rc = get(h, kField1Id, yV, yNV )) != kOkRC )
      goto errLabel;
    
    for(unsigned i=0,n0=0,n1=0; i<batchN; ++i)
    {
      for(unsigned j=0; j<xNV[i]; ++j)
        printf("%i ", xV[n0+j]);
      n0 += xNV[i];
      
      printf(": ");
      
      for(unsigned j=0; j<yNV[i]; ++j)
        printf("%f ", yV[n1+j] );
      n1 = yNV[i];
      
      printf("\n");
    }

    print_field(h, kField0Id);
    print_field(h, kField1Id);
    
  }
  
 errLabel:
  destroy(h);
  
  mem::release(inFn);
  return rc;
}


//----------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------

namespace cw
{
  namespace dataset
  {
    namespace mnist
    {
      typedef struct mnist_str
      {

        char* trainFn;
        char* testFn;
        char* validFn;

        unsigned* data_dimV;
        unsigned* label_dimV;
        
        unsigned  exampleN;
        float*    dataM;
        unsigned* labelV;
        
        unsigned kPixN;

        unsigned curIdx;
        
        
      } mnist_t;

      inline mnist_t* _handleToPtr(handle_t h )
      { return handleToPtr<handle_t,mnist_t>(h); }

      rc_t _destroy( mnist_t* p )
      {
        rc_t rc = kOkRC;

        mem::release(p->dataM);
        mem::release(p->labelV);
        mem::release(p->trainFn);
        mem::release(p->validFn);
        mem::release(p->testFn);
        mem::release(p);
        return rc;
      }

      rc_t _read_file_record_count( const char* fn, unsigned& nRef )
      {
        rc_t           rc;
        file::handle_t fH;
        
        // open the file
        if((rc = file::open(fH, fn, file::kReadFl | file::kBinaryFl )) != kOkRC )
        {
          rc = cwLogError(rc,"MNIST file open failed on '%s'.",cwStringNullGuard(fn));
          goto errLabel;
        }
        
        // read the count of examples
        if((rc = read(fH,nRef)) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to read MNIST example count.");
          goto errLabel;
        }

        // close file
        if((rc = file::close(fH)) != kOkRC )
        {
          rc = cwLogError(rc,"MNIST file close failed on '%s'.",cwStringNullGuard(fn));
          goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t _read_file( mnist_t* p, const char* fn, unsigned n, float* dataM, unsigned* labelV )
      {
        file::handle_t fH;
        rc_t           rc       = kOkRC;
        unsigned       exampleN = 0;
        
        // open the file
        if((rc = file::open(fH, fn, file::kReadFl | file::kBinaryFl )) != kOkRC )
        {
          rc = cwLogError(rc,"MNIST file open failed on '%s'.",cwStringNullGuard(fn));
          goto errLabel;
        }

        // read the count of examples
        if((rc = read(fH,exampleN)) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to read MNIST example count.");
          goto errLabel;
        }

        assert( exampleN == n );
        
        // read each example
        for(unsigned i=0; i<exampleN; ++i)
        {
          // read the digit image label
          if((rc = read(fH, labelV[i])) != kOkRC )
          {
            rc = cwLogError(rc,"Unable to read MNIST label on example %i.",i);
            goto errLabel;
          }

          // read the image pixels
          if((rc = readFloat(fH, dataM + i*p->kPixN, p->kPixN)) != kOkRC )
          {
            rc = cwLogError(rc,"Unable to read MNIST data vector on example %i.",i);
            goto errLabel;
          }

        }


      errLabel:
        if( rc != kOkRC)
          rc = cwLogError(rc,"Load failed on MNIST file %s.",cwStringNullGuard(fn));
        
        file::close(fH);
        return rc;        
      }
    }
  }
}


cw::rc_t cw::dataset::mnist::create( handle_t& h, const char* dir )
{
  rc_t     rc;
  mnist_t* p      = nullptr;
  unsigned trainN = 0;
  unsigned validN = 0;
  unsigned testN  = 0;
  
  if((rc = destroy(h)) != kOkRC )
    return rc;

  char* inDir = filesys::expandPath(dir);

  // allocate the object
  p        = mem::allocZ<mnist_t>(1);
  p->kPixN = 784;
  
  p->trainFn = filesys::makeFn(inDir, "mnist_train", ".bin", NULL );
  p->validFn = filesys::makeFn(inDir, "mnist_valid", ".bin", NULL );
  p->testFn  = filesys::makeFn(inDir, "mnist_test",  ".bin", NULL );

  mem::release(inDir);

  _read_file_record_count( p->trainFn, trainN );
  p->exampleN += trainN;
  
  _read_file_record_count( p->validFn, validN );
  p->exampleN += validN;
  
  _read_file_record_count( p->testFn, testN );
  p->exampleN += testN;
  
  
  // allocate the data memory
  p->dataM  = mem::alloc<float>(   p->kPixN * p->exampleN );
  p->labelV = mem::alloc<unsigned>(            p->exampleN );

  // read the training data
  if((rc = _read_file( p, p->trainFn, trainN, p->dataM, p->labelV )) != kOkRC )
  {
    rc = cwLogError(rc,"MNIST training set load failed.");
    goto errLabel;
  }

  // read the validation data
  if((rc = _read_file( p, p->validFn, validN, p->dataM + p->kPixN*trainN, p->labelV + trainN )) != kOkRC )
  {
    rc = cwLogError(rc,"MNIST validation set load failed.");
    goto errLabel;
  }

  // read the testing data
  if((rc = _read_file( p, p->testFn, testN, p->dataM + p->kPixN*(trainN +validN),  p->labelV + (trainN + validN) )) != kOkRC )
  {
    rc = cwLogError(rc,"MNIST test set load failed.");
    goto errLabel;    
  }

  h.set(p);

 errLabel:
  if( rc != kOkRC )
    _destroy(p);

  mem::release(inDir);
  
  return rc;
}

cw::rc_t cw::dataset::mnist::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  if( !h.isValid())
    return rc;

  mnist_t* p = _handleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;  
}


unsigned cw::dataset::mnist::record_count( handle_t h )
{
  mnist_t* p = _handleToPtr(h);
  return p->exampleN;
}


cw::rc_t cw::dataset::mnist::seek(   handle_t h, unsigned exampleIdx )
{
  rc_t     rc = kOkRC;
  mnist_t* p  = _handleToPtr(h);
  
  if( exampleIdx <= p->exampleN )
    p->curIdx = exampleIdx;
  else
    rc = cwLogError(kSeekFailRC,"Illegal seek index. Seek failed.");
  
  return rc;
}

cw::rc_t cw::dataset::mnist::dataM(  handle_t h, const float*& dataM_Ref,  const unsigned*& labelV_Ref, unsigned exampleN, unsigned& actualExampleN_Ref, unsigned exampleIdx )
{
  rc_t     rc = kOkRC;
  mnist_t* p  = _handleToPtr(h);

  if( exampleIdx == kInvalidIdx )
    exampleIdx = p->curIdx;
  
  if( exampleIdx >= p->exampleN )
    return kEofRC;

  if( exampleIdx + exampleN > p->exampleN )
    exampleN = p->exampleN - exampleIdx;

  //memcpy(dataM,  p->dataM  +  exampleIdx * p->kPixN, exampleN * p->kPixN * sizeof(p->dataM[0]) );
  //memcpy(labelV, p->labelV +  exampleIdx,            exampleN            * sizeof(p->labelV[0]) );

  dataM_Ref  = p->dataM  + exampleIdx * p->kPixN;
  labelV_Ref = p->labelV + exampleIdx;

  actualExampleN_Ref = exampleN;

  p->curIdx += exampleN;
  
  return rc;
}


cw::rc_t cw::dataset::mnist::write( handle_t h, const char* fn )
{
  rc_t          rc    = kOkRC;
  unsigned      recdN = record_count(h);
  wtr::handle_t wtrH;
  
  if((rc = wtr::create(wtrH,fn)) != kOkRC )
    return cwLogError(rc,"Dataset wtr create failed.");

  enum { kImagId, kNumbId };
  unsigned numbDimV[] = {1};
  unsigned imagDimV[] = {kPixelRowN,kPixelColN};
  unsigned imagEleN   = imagDimV[0]*imagDimV[1];
  
  if((rc = define_columns( wtrH, "numb", kNumbId, cwCountOf(numbDimV), numbDimV )) != kOkRC )
    goto errLabel;
  
  if((rc = define_columns( wtrH, "imag", kImagId, cwCountOf(imagDimV), imagDimV )) != kOkRC )
    goto errLabel;

  printf("recdN: %i\n",recdN);

  for(unsigned i=0;  i < recdN;  )
  {
    const float*    imagM      = nullptr;
    const unsigned* numbV      = nullptr;
    unsigned        cacheRecdN = std::min(100u,recdN-i);
    unsigned        actRecdN   = 0;
    
    if((rc = dataM(h, imagM, numbV, cacheRecdN, actRecdN, i )) != kOkRC )
    {
      cwLogError(rc,"Extract image data failed.");
      goto errLabel;
    }

    for(unsigned j=0; j<actRecdN; ++j)
    {
      // write the digit this imag represents as an 'int'.
      if((rc = wtr::write( wtrH, kNumbId, ((int*)numbV) + j, 1 )) != kOkRC )
        goto errLabel;

      // write the image data as 'floats'
      if((rc = wtr::write( wtrH, kImagId, imagM + j*imagEleN, imagEleN )) != kOkRC )
        goto errLabel;

      if((rc = wtr::write_record( wtrH )) != kOkRC )
        goto errLabel;
    }
    
    i += actRecdN;
    
  }

 errLabel:
  if(rc != kOkRC )
    cwLogError(rc, "MNIST data file write failed.");
  
  wtr::destroy(wtrH);

  return rc;
}


cw::rc_t cw::dataset::mnist::test( const object_t* cfg )
{
  handle_t h;
  rc_t     rc        = kOkRC;
  char*    inDir     = nullptr;
  char*    outHtmlFn = nullptr;

  if((rc = cfg->getv("inDir",inDir,"outHtmlFn",outHtmlFn)) != kOkRC )
    return cwLogError(rc,"MNIST test failed.  Argument parse failed.");

  inDir     = filesys::expandPath(inDir);
  outHtmlFn = filesys::expandPath(outHtmlFn);
    
  if((rc = create(h, inDir )) == kOkRC )
  {
    svg::handle_t svgH;

    if((rc = svg::create(svgH)) != kOkRC )
      rc = cwLogError(rc,"SVG Test failed on create.");
    else
    {
      const float*    dataM          = nullptr;
      const unsigned* labelV         = nullptr;
      unsigned        exampleN       = 100;
      unsigned        actualExampleN = 0;

      //mnist::seek(   h, 10 );
      mnist::dataM(  h, dataM,  labelV, exampleN, actualExampleN );

      
      for(unsigned i=0; i<actualExampleN; ++i)
      {
        printf("label: %i ", labelV[i] );
        
        svg::offset(svgH, 0, i*30*5 );
        svg::image(svgH, dataM + (kPixelRowN*kPixelColN)*i, kPixelRowN, kPixelColN, 5, svg::kInvGrayScaleColorMapId);
      }
      
      svg::write(svgH, outHtmlFn, nullptr, svg::kStandAloneFl, 10,10,10,10);
      
      
      svg::destroy(svgH);
    }
    
    rc = destroy(h);
  }

  mem::release(outHtmlFn);
  mem::release(inDir);
  return rc;
}

//----------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------

cw::rc_t cw::dataset::test( const object_t* cfg )
{

  rc_t              rc        = kOkRC;
  char*             inDir     = nullptr;
  char*             dsFn      = nullptr;
  char*             outHtmlFn = nullptr;
  mnist::handle_t   mniH;
  adapter::handle_t adpH;
  svg::handle_t     svgH;
  unsigned          batchN     =  100;
  unsigned          cacheByteN = 4096 * 10;
  unsigned          shuffleFl  = rdr::kShuffleFl;
  

  if((rc = cfg->getv("inDir",inDir,"dsFn",dsFn,"outHtmlFn",outHtmlFn,"batchN",batchN,"cacheByteN",cacheByteN)) != kOkRC )
    return cwLogError(rc,"MNIST test failed.  Argument parse failed.");

  inDir     = filesys::expandPath(inDir);
  dsFn      = filesys::expandPath(dsFn);
  outHtmlFn = filesys::expandPath(outHtmlFn);

  // open the native MNIST object
  if((rc = mnist::create(mniH, inDir )) != kOkRC )
  {
    cwLogError(rc,"Unable to open the native MNIST object.");
    goto errLabel;
  }
  else
  {
    // write the MNIST data to a dataset file
    if((rc = mnist::write(mniH, dsFn)) != kOkRC )
    {
      cwLogError(rc,"MNIST dataset write failed");
      goto errLabel;
    }
    
    mnist::destroy(mniH);
    
  }

  // open a dataset adapter
  if((rc = adapter::create(adpH,dsFn,batchN,cacheByteN,shuffleFl)) != kOkRC )
  {
    cwLogError(rc,"Dataset reader create failed.");
    goto errLabel;
  }
  else
  {
    // create an SVG file
    if((rc = svg::create(svgH)) != kOkRC )
      rc = cwLogError(rc,"SVG writer create failed.");
    else
    {

      enum { kImagId, kNumbId };

      // create a field for the image data
      if((rc = create_field( adpH, kImagId, adapter::kFloatFl, "imag" )) != kOkRC )
      {
        cwLogError(rc,"Dataset rdr column define failed.");
        goto errLabel;
      }

      // create a field for the image lable
      if((rc = create_field( adpH, kNumbId, adapter::kIntFl, "numb" )) != kOkRC )
      {
        cwLogError(rc,"Dataset rdr column define failed.");
        goto errLabel;
      }

      for(unsigned j=0,imageN=0; true; ++j )
      {      
        // read a batch of data
        if((rc = adapter::read( adpH, batchN)) != kOkRC )
        {
          if( rc == kEofRC )
            cwLogInfo("Done!.");
          else
            cwLogError(rc,"Batch read failed.");
          goto errLabel;
        }
        else
        {
          const int*      numbV      = nullptr;
          const unsigned* numbNV     = nullptr;
          const float*    imagV      = nullptr;
          const unsigned* imagNV     = nullptr;
          const unsigned  kPixelSize = 5;
        
          adapter::get(adpH, kNumbId, numbV, numbNV );  // get the labels
          adapter::get(adpH, kImagId, imagV, imagNV );  // get the image data

          printf("%3i : ",j);
          
          // print the first 5 images from each batch to an SVG file
          for(unsigned i=0; i<0; ++i,++imageN)
          {
            printf("%i ", numbV[i] );

            // offset the image vertically
            svg::offset(svgH, 0, imageN*30*kPixelSize );
            svg::image(svgH, imagV + (mnist::kPixelRowN*mnist::kPixelColN)*i, mnist::kPixelRowN, mnist::kPixelColN, kPixelSize, svg::kInvGrayScaleColorMapId);
          }
          printf("\n");
      
        }
      }

      svg::write(svgH, outHtmlFn, nullptr, svg::kStandAloneFl, 10,10,10,10);
      
    }    
  }
 errLabel:
  adapter::destroy(adpH);
  svg::destroy(svgH);
  mem::release(inDir);
  mem::release(dsFn);
  mem::release(outHtmlFn);
  return rc;
}

  

  
  
  

