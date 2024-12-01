//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwCsv.h"
#include "cwFile.h"
#include "cwFileSys.h"
#include "cwGutimReg.h"

namespace cw
{
  namespace gutim
  {
    namespace reg
    {
      typedef struct reg_file_str
      {
        char*  player_name;
        char*  take_label;
        char*  path;
        char*  midi_fname;        
        file_t r;
      } reg_file_t;
      
      typedef struct reg_str
      {
        reg_file_t* fileA;
        unsigned    fileN;        
      } reg_t;

      reg_t* _handleToPtr( handle_t h )
      { return handleToPtr<handle_t,struct reg_str>(h); }

      rc_t _destroy( reg_t* p )
      {
        for(unsigned i=0; i<p->fileN; ++i)
        {
          mem::release(p->fileA[i].player_name);
          mem::release(p->fileA[i].take_label);
          mem::release(p->fileA[i].path);
          mem::release(p->fileA[i].midi_fname);
        }
        mem::release(p->fileA);
        mem::release(p);
        
        return kOkRC;
      }
      
    }
  }
}


cw::rc_t cw::gutim::reg::create(  handle_t& hRef, const char* fname )
{
  rc_t          rc = kOkRC;
  csv::handle_t csvH;
  const char* colLabelA[] = {"player_name","take_label","session_number","take_number","beg_loc","end_loc","skip_score_follow_fl","path" };
  unsigned    colLabelN   = sizeof(colLabelA)/sizeof(colLabelA[0]);
  unsigned    lineAllocN       = 0;
  filesys::pathPart_t* pathPart    = nullptr;

  auto cmp_func = [](const reg_file_t& r0, const reg_file_t& r1)
    {
      int cmp = textCompare(r0.player_name,r1.player_name);

      if( cmp < 0 )
        return true;
      if( cmp > 0 )
        return false;
            
      if( r0.r.session_number < r1.r.session_number )
        return true;
      if( r0.r.session_number > r1.r.session_number )
        return false;
      
      if( r0.r.take_number < r1.r.take_number )
        return true;
      if( r0.r.take_number > r1.r.take_number )
        return false;

      return false;
    };
    
  if((rc = destroy(hRef)) != kOkRC )
    return rc;
  
  reg_t* p = mem::allocZ<reg_t>();

  // open the CSV
  if((rc = create(csvH,fname,colLabelA,colLabelN)) != kOkRC )
  {
    rc = cwLogError(rc,"GUTIM reg CSV file open failed on '%s'.",cwStringNullGuard(fname));
    goto errLabel;
  }

  // parse the reg CSV fname
  pathPart = filesys::pathParts(fname);
    
  // get the count of lines in the CSV file
  if((rc = line_count(csvH,lineAllocN)) != kOkRC )
  {
    rc = cwLogError(rc,"CSV line count failed.");
    goto errLabel;
  }

  // rewind CSV file
  if((rc = rewind(csvH)) != kOkRC )
  {
    rc = cwLogError(rc,"GUTIM registry CSV '%s' rewind failed.",cwStringNullGuard(fname));
    goto errLabel;
  }

  // verify that that the CSV is not tempy
  if( lineAllocN <= 1 )
  {
    rc = cwLogError(kInvalidStateRC,"The GUTIM registry CSV '%s' is empty.",cwStringNullGuard(fname));
    goto errLabel;
  }

  lineAllocN -= 1; // subtract one to account for column titles

  p->fileA = mem::allocZ<reg_file_t>(lineAllocN);
  
  // skip col. labels
  if((rc = next_line(csvH)) != kOkRC )
  {
    rc = cwLogError(rc,"CSV line advance failed on first line.");
    goto errLabel;
  }

  for(unsigned i=0; i<lineAllocN; ++i)
  {
    reg_file_t& fr = p->fileA[i];
        
    if((rc = getv( csvH,
                   "player_name",          fr.r.player_name,
                   "take_label",           fr.r.take_label,
                   "path",                 fr.r.path,
                   "session_number",       fr.r.session_number,
                   "take_number",          fr.r.take_number,
                   "beg_loc",              fr.r.beg_loc,
                   "end_loc",              fr.r.end_loc,
                   "skip_score_follow_fl", fr.r.skip_score_follow_fl )) != kOkRC )
    {
      rc = cwLogError(rc,"GUTIM registry CSV parse failed.");
      goto errLabel;
    }
      
    fr.player_name   = mem::duplStr(fr.r.player_name);
    fr.r.player_name = fr.player_name;
    
    fr.take_label   = mem::duplStr(fr.r.take_label);
    fr.r.take_label =  fr.take_label;
    
    fr.path         = mem::duplStr(fr.r.path);
    fr.r.path       = fr.path;
    
    fr.midi_fname   = filesys::makeFn(pathPart->dirStr, "midi", "mid", fr.r.path, nullptr );
    fr.r.midi_fname = fr.midi_fname;

    p->fileN += 1;
    
    // advance the current CSV 
    if((rc = next_line( csvH )) != kOkRC )
    {
      if( rc == kEofRC )
      {
        rc = kOkRC;
        break;
      }
      
      rc = cwLogError(rc,"CSV 'next line' failed on '%s'.",cwStringNullGuard(fname));
      goto errLabel;        
    }      
  }

  std::sort(p->fileA,p->fileA+p->fileN,cmp_func);

  hRef.set(p);
    
errLabel:
    if(rc != kOkRC )
      _destroy(p);

    destroy(csvH);
    
    mem::release(pathPart);
    return rc;    
}

cw::rc_t cw::gutim::reg::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if( !hRef.isValid() )
    return rc;

  reg_t* p = _handleToPtr(hRef);
  
  if((rc = _destroy(p)) != kOkRC )
  {
    cwLogError(rc,"Gutim registry destroy failed.");
    goto errLabel;
  }
  
  hRef.clear();

errLabel:
  return rc;
}

unsigned      cw::gutim::reg::file_count(  handle_t h )
{
  reg_t* p = _handleToPtr(h);
  return p->fileN;
}

cw::gutim::reg::file_t  cw::gutim::reg::file_record( handle_t h, unsigned file_idx )
{
  reg_t* p = _handleToPtr(h);
  return p->fileA[ file_idx ].r;
}

void cw::gutim::reg::report( handle_t h )
{
  reg_t* p = _handleToPtr(h);

  cwLogInfo("performer  sess take b-loc e-loc skip  path");
  cwLogInfo("---------- ---- ---- ----- ----- ----- -------------------------------------------");
  for(unsigned i=0; i<p->fileN; ++i)
  {
    const reg_file_t& fr = p->fileA[i];
    const file_t& r = fr.r;
    
    cwLogInfo("%10s %4i %4i %5i %5i %5s %s %s",
              r.player_name,
              r.session_number,
              r.take_number,
              r.beg_loc,
              r.end_loc,
              r.skip_score_follow_fl?"true":"false",
              r.path,
              r.midi_fname);
    
  }

}


cw::rc_t cw::gutim::reg::test( const object_t* cfg )
{
  const char* dir = nullptr;
  rc_t        rc  = kOkRC;
  handle_t    h;
  
  if((rc = cfg->getv("dir",dir)) != kOkRC )
  {
    rc = cwLogError(rc,"The arg. parse GUTIM registry test.");
    goto errLabel;
  }

  if((rc = create(h,dir)) != kOkRC )
  {
    rc = cwLogError(rc,"The GUTIM registry create failed.");
    goto errLabel;
  }
  
  report(h);
  
errLabel:
  destroy(h);
  
  return rc;
}
