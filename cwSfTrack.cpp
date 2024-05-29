#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwMidiFile.h"
#include "cwFileSys.h"
#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwSfMatch.h"
#include "cwSfTrack.h"

namespace cw
{
  namespace sftrack
  {
    typedef struct sftrack_str
    {
      sfscore::handle_t    scH;
      callback_func_t      cbFunc;
      void*                cbArg;
      sfmatch::handle_t    matchH;
      unsigned             mn;       // length of midiBuf[]  
      sfmatch::midi_t*     midiBuf;  // midiBuf[mn]   MIDI event window

      result_t*            res;      // res[rn] result buffer
      unsigned             rn;       // length of res[] (set to 2*score event count)
      unsigned             ri;       // next avail res[] recd.

      double               s_opt;          // 
      unsigned             missCnt;        // current count of consecutive trailing non-matches
      unsigned             ili;            // index into sfmatch_t.loc[] to start scan following reset
      unsigned             eli;            // index into sfmatch_t.loc[] of the last positive match. 
      unsigned             mni;            // current count of MIDI events since the last call to reset()
      unsigned             mbi;            // index of oldest MIDI event in midiBuf[]; stays at 0 when the buffer is full.
      unsigned             begSyncLocIdx;  // start of score window, in mp->loc[], of best match in previous scan
      unsigned             initHopCnt;     // max window hops during the initial (when the MIDI buffer fills for first time) sync scan 
      unsigned             stepCnt;        // count of forward/backward score loc's to examine for a match during _step().
      unsigned             maxMissCnt;     // max. number of consecutive non-matches during step prior to executing a _scan().
      unsigned             scanCnt;        // current count of times a resync-scan was executed during _step()

      unsigned             flags;
    } sftrack_t;
    
    sftrack_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,sftrack_t>(h); }

    rc_t _destroy( sftrack_t* p )
    {
      rc_t rc = kOkRC;
      destroy(p->matchH);
      mem::release(p->midiBuf);
      mem::release(p->res);
      mem::release(p);
      return rc;
    }

    rc_t _reset( sftrack_t* p, unsigned scLocIdx )
    {
      rc_t rc = kOkRC;
  
      p->mbi           = max_midi_wnd_count(p->matchH);
      p->mni           = 0;
      p->begSyncLocIdx = kInvalidIdx;
      p->s_opt         = DBL_MAX;
      p->missCnt       = 0;
      p->scanCnt       = 0;
      p->ri            = 0;
      p->eli           = kInvalidIdx;
      p->ili           = 0;

      unsigned              locN = loc_count(p->matchH);
      const sfmatch::loc_t* loc  = loc_base(p->matchH);

      
      // convert scLocIdx to an index into p->mp->loc[]
      unsigned i = 0;
      for(unsigned safety_idx=0; safety_idx<10; ++safety_idx)
      {
        for(i=0; i<locN; ++i)
          if( loc[i].scLocIdx == scLocIdx )
          {
            p->ili = i;
            break;
          }

        assert(locN>0);
        if( i!=locN || scLocIdx==loc[locN-1].scLocIdx)
          break;

        scLocIdx += 1;
      }

      if( i==locN)
      {
        rc = cwLogError(kOpFailRC, "Score matcher reset failed.");
        goto errLabel;
      }

    errLabel:

      return rc;
    }

    bool _input_midi(  sftrack_t* p, double sec, unsigned smpIdx, unsigned muid, unsigned status, midi::byte_t d0, midi::byte_t d1 )
    {
      if( (status&0xf0) != midi::kNoteOnMdId)
        return false;

      if( d1 == 0 )
        return false;

      unsigned mi = p->mn-1;

      //printf("%3i %4s\n",p->mni,cmMidiToSciPitch(d0,NULL,0));

      // shift the new MIDI event onto the end of the MIDI buffer
      memmove(p->midiBuf, p->midiBuf+1, sizeof(sfmatch::midi_t)*mi);
      p->midiBuf[mi].oLocId   = kInvalidIdx;
      p->midiBuf[mi].scEvtIdx = kInvalidIdx;
      p->midiBuf[mi].mni      = p->mni++;
      p->midiBuf[mi].sec      = sec;      
      p->midiBuf[mi].smpIdx   = smpIdx;
      p->midiBuf[mi].muid     = muid;
      p->midiBuf[mi].pitch    = d0;
      p->midiBuf[mi].vel      = d1;
      if( p->mbi > 0 )
        --p->mbi;

      return true;
    }

    void _store_result( sftrack_t* p, unsigned oLocId, unsigned scEvtIdx, unsigned flags, const sfmatch::midi_t* mp, double cost )
    {
      // don't store missed score note results
      assert( mp != NULL );
      bool       matchFl    = cwIsFlag(flags,sfmatch::kSmMatchFl);
      bool       tpFl       = oLocId!=kInvalidIdx && matchFl;
      bool       fpFl       = oLocId==kInvalidIdx || matchFl==false;
      result_t * rp         = NULL;
      unsigned   result_idx = kInvalidIdx;
      result_t   r;

      assert( tpFl==false || (tpFl==true && oLocId != kInvalidIdx ) );

      // it is possible that the same MIDI event is reported more than once
      // (due to step->scan back tracking) - try to find previous result records
      // associated with this MIDI event

      // TODO: This process looks expensive - as the result array grows a linear
      // search is done over the entire length looking for previous matches.
      // - and why do we want this behavior anyway?
      if( cwIsFlag(p->flags,kBacktrackResultsFl) )
      {
        for(unsigned i=0; i<p->ri; ++i) 
          if( p->res[i].mni == mp->mni )
          {
            // if this is not the first time this note was reported and it is a true positive 
            if( tpFl )
            {
              rp = p->res + i;
              result_idx = i;
              break;
            }

            // a match was found but this was not a true-pos so ignore it
            return;
          }
      }
      
      if( rp == NULL )
      {
        // if the result array is full ...
        if( p->ri >= p->rn )
        {
          // then use a single record to hold the result so that we can still make the callback
          rp = &r; 
          memset(rp,0,sizeof(r));
        }
        else
        {
          // otherwise append select the next available record to receive the result
          rp = p->res + p->ri;
          result_idx = p->ri;
          ++p->ri;
        }
      }

      // BUG BUG BUG BUG:
      // for some reason oLocId seems to be set to scEvtIdx and so we replace it
      // with the correct value here - but this problem seems to originate in sfMatch
      // which is where it should be fixed
      if( scEvtIdx != kInvalidIdx )
      {
        const sfscore::event_t* evt = event( p->scH, scEvtIdx );
        assert(evt != nullptr );
        oLocId = evt->oLocId;
      }
      
      rp->index    = result_idx;
      rp->oLocId   = oLocId;
      rp->scEvtIdx = scEvtIdx;
      rp->mni      = mp->mni;
      rp->muid     = mp->muid;
      rp->sec      = mp->sec;
      rp->smpIdx   = mp->smpIdx;
      rp->pitch    = mp->pitch;
      rp->vel      = mp->vel;
      rp->flags    = flags | (tpFl ? sfmatch::kSmTruePosFl : 0) | (fpFl ? sfmatch::kSmFalsePosFl : 0);
      rp->cost     = cost;
  
      if( p->cbFunc != NULL )
        p->cbFunc(p->cbArg,rp);

    }

    
    unsigned _scan( sftrack_t* p, unsigned bli, unsigned hopCnt )
    {
      
      assert( p->matchH.isValid() && sfmatch::max_midi_wnd_count(p->matchH) > 0 );

      unsigned i_opt = kInvalidIdx;
      double   s_opt = DBL_MAX;
      rc_t     rc    = kOkRC;
      unsigned mmn   = sfmatch::max_midi_wnd_count(p->matchH);
      unsigned msn   = sfmatch::max_score_wnd_count(p->matchH);
      unsigned i;

      // initialize the internal values set by this function
      p->missCnt = 0;
      p->eli     = kInvalidIdx;
      p->s_opt   = DBL_MAX;
  
      // if the MIDI buf is not full
      if( p->mbi != 0 )
        return kInvalidIdx;

      // calc the edit distance from pitchV[] to a sliding score window
      for(i=0; rc==kOkRC && (hopCnt==kInvalidCnt || i<hopCnt); ++i)
      {
        rc = sfmatch::exec(p->matchH, bli + i, msn, p->midiBuf, mmn, s_opt );

        double opt_cost = cost(p->matchH);
        switch(rc)
        {
          case kOkRC:  // normal result 
            if( opt_cost < s_opt )
            {
              s_opt = opt_cost;
              i_opt = bli + i;
            }
            break;

          case kEofRC: // score window encountered the end of the score
            break;

          default: // error state
            return kInvalidIdx;
        }
      }
          
      // store the cost assoc'd with i_opt
      p->s_opt = s_opt;

      if( i_opt == kInvalidIdx )
        return kInvalidIdx;


      // set the oLocId field in midiBuf[], trailing miss count and
      // return the latest positive-match oLocId
      p->eli = sfmatch::sync(p->matchH,i_opt,p->midiBuf,mmn,&p->missCnt);

      // if no positive matches were found
      if( p->eli == kInvalidIdx )
        i_opt = kInvalidIdx;
      else
      {
        // record result
        for(const sfmatch::path_t* cp = optimal_path(p->matchH); cp!=NULL; cp=cp->next)
          if( cp->code != sfmatch::kSmInsIdx )
            _store_result(p, cp->oLocId, cp->scEvtIdx, cp->flags, p->midiBuf + cp->ri - 1,p->s_opt);
      }

      return i_opt;
  
    }

    rc_t _step( sftrack_t* p )
    {
      unsigned              pitch  = p->midiBuf[ p->mn-1 ].pitch;
      unsigned              oLocId = kInvalidIdx;
      unsigned              pidx   = kInvalidIdx;
      unsigned              locN   = loc_count(p->matchH);
      const sfmatch::loc_t* loc    = loc_base(p->matchH);
      
      // the tracker must be sync'd to step
      if( p->eli == kInvalidIdx )
        return cwLogError(kInvalidArgRC, "The p->eli value must be valid to perform a step operation."); 

      // if the end of the score has been reached
      if( p->eli + 1 >= locN )
        return kEofRC;
    
      // attempt to match to next location first
      if( (pidx = match_index(loc + p->eli + 1, pitch)) != kInvalidIdx )
      {
        oLocId = p->eli + 1;
      }
      else
      {
        // 
        for(unsigned i=2; i<p->stepCnt; ++i)
        {
          // go forward 
          if( p->eli+i < locN && (pidx=match_index(loc + p->eli + i, pitch))!=kInvalidIdx )
          {
            oLocId = p->eli + i;
            break;
          }

          // go backward
          if( p->eli >= (i-1)  && (pidx=match_index(loc + p->eli - (i-1), pitch))!=kInvalidIdx )
          {
            oLocId = p->eli - (i-1);
            break;
          }
        }
      }

      unsigned scEvtIdx = oLocId==kInvalidIdx ? kInvalidIdx : loc[oLocId].evtV[pidx].scEvtIdx;

      p->midiBuf[ p->mn-1 ].oLocId   = oLocId;
      p->midiBuf[ p->mn-1 ].scEvtIdx = scEvtIdx;

      if( oLocId == kInvalidIdx )
        ++p->missCnt;
      else
      {
        p->missCnt = 0;
        p->eli     = oLocId;
      }

      // store the result
      _store_result(p, oLocId,  scEvtIdx, oLocId!=kInvalidIdx ? sfmatch::kSmMatchFl : 0, p->midiBuf + p->mn - 1, cost(p->matchH) );

      if( p->missCnt >= p->maxMissCnt )
      {
        unsigned begScanLocIdx = p->eli > p->mn ? p->eli - p->mn : 0;
        p->s_opt               = DBL_MAX;
        unsigned bli           = _scan(p,begScanLocIdx,p->mn*2);
        ++p->scanCnt;

        // if the scan failed find a match
        if( bli == kInvalidIdx )
          return cwLogError(kOpFailRC, "Scan resync. failed."); 
      }

      return kOkRC;
    }
    
  }
}

cw::rc_t cw::sftrack::create( handle_t&         hRef,
                              sfscore::handle_t scH,      // Score handle.  See cmScore.h.
                              unsigned          scWndN,   // Length of the scores active search area. ** See Notes.
                              unsigned          midiWndN, // Length of the MIDI active note buffer.    ** See Notes.
                              unsigned          flags,
                              callback_func_t   cbFunc,   // A cmScMatcherCb_t function to be called to notify the recipient of changes in the score matcher status.
                              void*             cbArg )   // User argument to 'cbFunc'.
{
  rc_t rc = kOkRC;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  if( midiWndN > scWndN )
    return cwLogError(kInvalidArgRC, "The score alignment MIDI event buffer length (%i) must be less than the score window length (%i).",midiWndN,scWndN); 

  sftrack_t* p = mem::allocZ<sftrack_t>();
  
  if(( rc = sfmatch::create(p->matchH,scH,scWndN,midiWndN)) != kOkRC )
  {
    cwLogError(rc,"sfmatch create failed.");
    goto errLabel;
  }

  p->scH        = scH;
  p->cbFunc     = cbFunc;
  p->cbArg      = cbArg;
  p->mn         = midiWndN;
  p->midiBuf    = mem::resize<sfmatch::midi_t>(p->midiBuf,p->mn);
  p->initHopCnt = 50;
  p->stepCnt    = 3;
  p->maxMissCnt = p->stepCnt+1;
  p->rn         = 2 * event_count(scH);
  p->res        = mem::resize<result_t>(p->res,p->rn);
  p->flags      = flags;

  _reset(p,0);

  hRef.set(p);
  
 errLabel:
  if(rc != kOkRC )
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::sftrack::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if(!hRef.isValid())
    return rc;

  sftrack_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  
  return rc;
}

cw::rc_t cw::sftrack::reset( handle_t h, unsigned scLocIdx )
{
  sftrack_t* p = _handleToPtr(h);
  return _reset(p,scLocIdx);
}


cw::rc_t cw::sftrack::exec( handle_t h, double sec, unsigned smpIdx, unsigned muid, unsigned status, midi::byte_t d0, midi::byte_t d1, unsigned* scLocIdxPtr )
{
  sftrack_t* p       = _handleToPtr(h);
  bool       fl      = p->mbi > 0;
  rc_t       rc      = kOkRC;
  unsigned   org_eli = p->eli;

  if( scLocIdxPtr != NULL )
    *scLocIdxPtr = kInvalidIdx;

  // update the MIDI buffer with the incoming note
  if( _input_midi(p,sec,smpIdx,muid,status,d0,d1) == false )
    return rc;

  // if the MIDI buffer transitioned to full then perform an initial scan sync.
  if( fl && p->mbi == 0 )
  {
    if( (p->begSyncLocIdx = _scan(p,p->ili,p->initHopCnt)) == kInvalidIdx )
    {
      rc = kInvalidArgRC; // signal init. scan sync. fail
    }
    else
    {
      //cmScMatcherPrintPath(p);
    }
  }
  else
  {
    // if the MIDI buffer is full then perform a step sync.
    if( !fl && p->mbi == 0 ) 
      rc = _step(p);
  }

  // if we lost sync 
  if( p->eli == kInvalidIdx )
  {
    // IF WE LOST SYNC THEN WE BETTER DO SOMETHING - LIKE INCREASE THE SCAN HOPS
    // ON THE NEXT EVENT.
    p->eli = org_eli;
  }
  else
  {
    if( scLocIdxPtr!=NULL && p->eli != org_eli )
    {
      const sfmatch::loc_t* loc = loc_base(p->matchH);

      // printf("LOC:%i bar:%i\n",p->eli,loc[p->eli].barNumb);
      *scLocIdxPtr = loc[p->eli].scLocIdx;
    }
  }

  return rc;
}

unsigned cw::sftrack::result_count( handle_t h )
{
  sftrack_t* p = _handleToPtr(h);
  return p->ri;
}

const cw::sftrack::result_t* cw::sftrack::result_base( handle_t h )
{
  sftrack_t* p = _handleToPtr(h);
  return p->res;
}


void cw::sftrack::print( handle_t h )
{
  sftrack_t* p = _handleToPtr(h);
  sfmatch::print_path( p->matchH, p->begSyncLocIdx, p->midiBuf );
}

namespace cw
{
  namespace sftrack
  {
    void _test_cb_func(  void* arg, result_t* rp )
    {
      printf("mni:%i muid:%i loc:%i scevt:%i\n",rp->mni,rp->muid,rp->oLocId,rp->scEvtIdx);
    }
  }
}

cw::rc_t cw::sftrack::test( const object_t* cfg, sfscore::handle_t scoreH )
{
  rc_t            rc                  = kOkRC;  
  bool            report_midi_file_fl = false;
  bool            report_track_fl     = false;  
  const object_t* perf                = nullptr;  
  bool            perf_enable_fl      = false;
  bool            print_fl            = false;
  bool            backtrack_fl        = false;
  unsigned        perf_loc_idx        = 0;
  const char*     perf_midi_fname     = nullptr;
  unsigned        maxScWndN           = 10;
  unsigned        maxMidiWndN         = 7;
  double          srate               = sample_rate(scoreH);;
  unsigned        flags               = 0;
  
  const midi::file::trackMsg_t** midiMsgA = nullptr;
  unsigned                       midiMsgN = 0;
  sftrack::handle_t              trackH;
  midi::file::handle_t           mfH;
  
  // parse the test cfg
  if((rc = cfg->getv("maxScWndN", maxScWndN,
                     "maxMidiWndN", maxMidiWndN,
                     "report_midi_file_fl",report_midi_file_fl,
                     "report_track_fl",report_track_fl,
                     "print_fl",print_fl,
                     "backtrack_fl",backtrack_fl,
                     "perf", perf)) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore test parse params failed.");
    goto errLabel;
  }
  
  if((rc = perf->getv( "enable_fl",   perf_enable_fl,
                       "loc_idx",     perf_loc_idx,
                       "midi_fname",  perf_midi_fname )) != kOkRC )
  {
    rc = cwLogError(rc,"sfscore test parse params 'perf' failed.");
    goto errLabel;
  }

  flags += print_fl     ? kPrintFl            : 0;
  flags += backtrack_fl ? kBacktrackResultsFl : 0;
  
  // create the score tracker
  if((rc = create(trackH, scoreH, maxScWndN, maxMidiWndN, flags, _test_cb_func, nullptr )) != kOkRC )
  {
    rc = cwLogError(rc,"sftrack create failed.");
    goto errLabel;
  }

  if((rc = reset(trackH, perf_loc_idx )) != kOkRC )
  {
    rc = cwLogError(rc,"sftrack reset failed.");
    goto errLabel;    
  }

  // open the MIDI file
  if((rc = open(mfH, perf_midi_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"midi file create failed on '%s'",cwStringNullGuard(perf_midi_fname));
    goto errLabel;    
  }

  if( report_midi_file_fl )
    printMsgs(mfH,log::globalHandle());

  midiMsgN = msgCount(mfH);
  midiMsgA = msgArray(mfH);

  // iterate through the MIDI file
  for(unsigned i=0; i<midiMsgN; ++i)
  {    
    unsigned                      scLocIdx = kInvalidIdx;
    const midi::file::trackMsg_t* trk      = midiMsgA[i];

    // if this is a note on message
    if( midi::isNoteOnStatus(trk->status) )
    {
      double    secs   = trk->amicro / 1000000.0;
      unsigned  smpIdx = secs / srate;
      if((rc = exec(trackH, secs, smpIdx, trk->uid, trk->status, trk->u.chMsgPtr->d0, trk->u.chMsgPtr->d1, &scLocIdx)) != kOkRC )
      {
        if( rc != kEofRC )
          rc = cwLogError(rc,"tracker exec() failed.");
        goto errLabel;
      }
    }
  }
  
  
 errLabel:
  midi::file::close(mfH);
  destroy(trackH);
  
  return rc;
}
