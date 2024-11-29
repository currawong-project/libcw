#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwObject.h"
#include "cwMidi.h"

#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwSfMatch.h"
#include "cwSfTrack.h"
#include "cwSfAnalysis.h"

namespace cw
{
  namespace sf_analysis
  {
    enum { kSciPitchCharCnt = 7 };
    
    typedef struct anl_evt_str
    {
      unsigned matchCnt;      // should be equal to 1, 0 if missed, > 1 if matched multiple time
      unsigned timeDirErrCnt; // should be 0, incremented every time this event was matched going backwards in time
      unsigned spuriousCnt;   // should be 0, incremented for every nearby spurious note
      
      unsigned oLocId;        // score event location id
      unsigned barNumb;       // bar number of this event
      uint8_t  pitch;         // pitch of this event
      const char* sect_label;    // 
      char     sciPitch[ kSciPitchCharCnt ];   // 
    } anl_event_t;

    unsigned _locate_anl_evt( unsigned oLocId, uint8_t pitch, const anl_event_t* anlEvtA, unsigned anlEvtN, unsigned anlEvtIdx )
    {
      for(unsigned i=0; i<anlEvtN; ++i)
        if( anlEvtA[i].oLocId == oLocId && anlEvtA[i].pitch == pitch  )
          return i;
        
      return kInvalidIdx;
    }

    rc_t _write_as_csv( const anl_event_t* anlEvtA, unsigned anlEvtCnt, const char* fname )
    {
      file::handle_t fH;
      rc_t           rc = kOkRC;

      if((rc = open(fH,fname,file::kWriteFl)) != kOkRC )
      {
        rc = cwLogError(rc,"File create failed on '%s'.",cwStringNullGuard(fname));
        goto errLabel;
      }

      if((rc = printf(fH,"section,bar,oLocId,sciPitch,matchN,timeDirErrN,spuriousN\n")) != kOkRC )
      {
        rc = cwLogError(rc,"File write failed on title line.");
        goto errLabel;
      }

      for(const anl_event_t* e=anlEvtA; e<anlEvtA+anlEvtCnt; ++e)
      {
        if((rc = printf(fH,"%s,%i,%i,%s,%i,%i,%i\n",e->sect_label,e->barNumb,e->oLocId,e->sciPitch,e->matchCnt,e->timeDirErrCnt,e->spuriousCnt)) != kOkRC )
        {
          rc = cwLogError(rc,"File write failed on line %i.",(e-anlEvtA)+1);
          goto errLabel;
          
        }
      }
    errLabel:      
      close(fH);

      if( rc != kOkRC )
        rc = cwLogError(rc,"The score follow analysis CSV report file creation failed on '%s'.",cwStringNullGuard(fname));

      return rc;
    }
    
    
  }
}

cw::rc_t cw::sf_analysis::gen_analysis( sfscore::handle_t        scH,
                                        const sftrack::result_t* resultA,
                                        unsigned                 resultN,
                                        unsigned                 begLoc,
                                        unsigned                 endLoc,
                                        const char*              fname )
{
  rc_t         rc           = kOkRC;
  unsigned     anlEvtAllocN = event_count(scH);
  anl_event_t* anlEvtA      = mem::allocZ<anl_event_t>(anlEvtAllocN);
  unsigned     anlEvtCnt    = 0;
  unsigned     anlEvtIdx    = 0;
  const char*  sect_label0  = "<unknown>";
  
  // fill anlEvtA[] with the score events
  for(unsigned i=0; i<anlEvtAllocN; ++i)
  {
    const sfscore::event_t* scEvt = event(scH,i);
    if( begLoc <= scEvt->oLocId && scEvt->oLocId <= endLoc )
    {
      anl_event_t* anlEvt = anlEvtA + anlEvtCnt++;
      
      const sfscore::section_t* sect;
      const char* sect_label = nullptr;
      if((sect = event_index_to_section( scH, i )) == nullptr )
        sect_label = sect_label0;
      else
        sect_label = sect->label;

      anlEvt->oLocId     = scEvt->oLocId;
      anlEvt->pitch      = scEvt->pitch;
      anlEvt->barNumb    = scEvt->barNumb;
      anlEvt->sect_label = sect_label;
      strncpy(anlEvt->sciPitch,scEvt->sciPitch,kSciPitchCharCnt-1);

      sect_label0 = sect_label;
    }
  }

  if( anlEvtCnt == 0)
  {
    rc = cwLogError(kInvalidStateRC,"The score appears to be empty.");
    goto errLabel;
  }
  
  for(unsigned i=0; i<resultN; ++i)
  {
    const sftrack::result_t* r = resultA + i;
    unsigned aei;

    // if this is a spurious event
    if( r->oLocId == kInvalidIdx )
    {
      // ignore spurious events until at least one matched event is found
      if( anlEvtIdx > 0 )
        anlEvtA[ anlEvtIdx ].spuriousCnt += 1;
    }
    else
    {
      // if this event is in range
      if( begLoc <= r->oLocId && r->oLocId <= endLoc )
      {
        // locate the associated score event 
        if((aei = _locate_anl_evt( r->oLocId, r->pitch, anlEvtA, anlEvtCnt, anlEvtIdx )) == kInvalidIdx )
        {
          rc = cwLogError(kInvalidStateRC,"The score event associated with a matched performance could not be found for event:%i loc:%i.",i,r->oLocId);
          goto errLabel;
        }

        anlEvtA[ aei ].matchCnt += 1;

        anlEvtA[ aei ].timeDirErrCnt += aei < anlEvtIdx;

        anlEvtIdx = aei;

      }      
        
    }
      
  }

  rc = _write_as_csv( anlEvtA, anlEvtCnt, fname );


  errLabel:
    if( rc != kOkRC )
      rc = cwLogError(rc,"Score follow analysis failed.");
    return rc;
}
