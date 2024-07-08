#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwAudioFile.h"
#include "cwDspTypes.h"
#include "cwWaveTableBank.h"
#include "cwVectOps.h"
#include "cwMidi.h"

namespace cw
{
  namespace wt_bank
  {

    typedef struct instr_str
    {
      char*             label;
      unsigned          id;
      struct instr_str* link;
    } instr_t;
    
    typedef struct af_str
    {
      object_t* cfg;

      const char* audio_fname;
      
      wt_t*     wtA;
      unsigned  wtN;

      ch_t*     chA;
      unsigned  chN;

      seg_t*    segA;
      unsigned  segN;

      struct af_str* link;
    } af_t;


    typedef struct wt_map_str
    {
      wt_t**       map;   // bankM[pitch(128)][vel(128)]
    } wt_map_t;
    
    typedef struct wt_bank_str
    {      
      af_t* afList;     // One af_t record per cfg file found in the source directory given to create()

      wt_map_t* wtMapA; // wtMapA[ wtMapN ] one wt_map_t record per instr_t record
      unsigned  wtMapN; // wtMapN is the same as the length of instrList

      unsigned next_instr_id; // Current length of instrList
      instr_t* instrList;     // List of instruments
      
    } wt_bank_t;

    wt_bank_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,wt_bank_t>(h); }

    void _report_map( const wt_map_t* map )
    {
      for(unsigned i=0; i<midi::kMidiNoteCnt; ++i)
      {
        cwLogPrint("%3i : ",i);
        for(unsigned j=0; j<midi::kMidiVelCnt; ++j)
        {
          wt_t* wt = map->map[(j*midi::kMidiNoteCnt) + i];
          
          cwLogPrint("%i:%1i ",j,wt != nullptr);
        }
        cwLogPrint("\n");
      }
          
    }

    void _report( wt_bank_t* p )
    {
      for(af_t* af = p->afList; af!=nullptr; af=af->link)
      {
        cwLogInfo("%s",af->audio_fname);
        
        for(unsigned i=0; i<af->wtN; ++i)
        {
          const wt_t* wt = af->wtA + i;

          cwLogInfo("  pitch:%i vel:%i ", wt->pitch, wt->vel );
          
          for(unsigned j=0; j<wt->chN; ++j)
          {
            const ch_t* ch = wt->chA + j;

            cwLogInfo("    ch:%i",ch->ch_idx);
            
            for(unsigned k=0; k<ch->segN; ++k)
            {
              const seg_t* seg = ch->segA + k;

              cwLogInfo("      type:%i smpN:%i cost:%f", seg->tid, seg->aN, seg->cost);
             
            }
          }
        }
      }

      if( p->wtMapN > 0 )
        _report_map( p->wtMapA ); 
    }

    rc_t _destroy( wt_bank_t* p )
    {
      rc_t rc = kOkRC;
      af_t* af = p->afList;
      while( af != nullptr )
      {
        af_t* af0 = af->link;

        for(unsigned i=0; i<af->segN; ++i)
          mem::release(af->segA[i].aV);
        
        af->cfg->free();
        mem::release(af->wtA);
        mem::release(af->chA);
        mem::release(af->segA);
        mem::release(af);
        
        af = af0;
      }


      for(unsigned i=0; i<p->wtMapN; ++i)
        mem::release(p->wtMapA[i].map);

      instr_t* instr=p->instrList;
      while( instr!=nullptr )
      {
        instr_t* i0 = instr->link;
        mem::release(instr->label);
        mem::release(instr);
        instr=i0;
      }
      
      mem::release(p->wtMapA);
      
      
      mem::release(p);
      return rc;
    }    

    void  _load_segment_audio( seg_t& seg,
                               const sample_t* const * audio_ch_buf,
                               unsigned audio_ch_bufN,
                               unsigned padSmpN,
                               unsigned ch_idx,
                               seg_tid_t tid,
                               unsigned bsi,
                               unsigned esi )
    {
      assert( ch_idx < audio_ch_bufN );
      
      seg.aN   = esi - bsi;
      seg.aV   = mem::alloc<sample_t>( padSmpN + seg.aN + padSmpN );
      seg.padN = padSmpN;

      // audio vector layout
      // aV[ padSmpN + aN + padSmpN ]

      vop::copy( seg.aV,                    audio_ch_buf[ ch_idx ] + esi - padSmpN, padSmpN );
      vop::copy( seg.aV + padSmpN,          audio_ch_buf[ ch_idx ] + bsi,           seg.aN );
      vop::copy( seg.aV + padSmpN + seg.aN, audio_ch_buf[ ch_idx ] + bsi,           padSmpN );
      
    }

    instr_t* _find_instr( wt_bank_t* p, const char* instr_label )
    {
      instr_t* instr = p->instrList;
      for(; instr != nullptr; instr=instr->link )
        if( textIsEqual(instr->label,instr_label) )
          return instr;
      
      return nullptr;
    }
    
    unsigned _find_or_add_instr_id( wt_bank_t* p, const char* instr_label )
    {
      instr_t* instr;
      if((instr = _find_instr(p,instr_label)) == nullptr )
      {
        instr        = mem::allocZ<instr_t>();
        instr->id    = p->next_instr_id++;
        instr->label = mem::duplStr(instr_label);
        
        instr->link  = p->instrList;
        p->instrList = instr;
      }
      
      return instr->id;        
    }    
    
    // If 'count_fl' is set then p->wtN,p->chN and p->segN are set but no
    // data is stored. 
    rc_t _parse_cfg( wt_bank_t*             p,
                     af_t*                  af,
                     const object_t*        wtL,
                     const sample_t* const* audio_ch_buf  = nullptr,
                     unsigned               audio_ch_bufN = 0,
                     unsigned               padSmpN       = 0 )
    {
      rc_t        rc          = kOkRC;
      unsigned    wtN         = wtL->child_count();
      unsigned    ch_idx      = 0;
      unsigned    seg_idx     = 0;
      const char* instr_label = nullptr;
      
      // count_Fl true if counting wt/ch/seg records otherwise building wt/ch/seg data structures
      bool        count_fl    = audio_ch_buf == nullptr; 
      
      // for each wt cfg record
      for(unsigned i=0; i<wtN; ++i)
      {
        const object_t* wt  = nullptr;
        const object_t* chL = nullptr;
        wt_t            w;
        wt_t&           wr  = count_fl ? w : af->wtA[i];
        
        af->wtN  += count_fl ? 1 : 0;

        // get the ith wt cfg
        if((wt = wtL->child_ele(i)) == nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"Unexpected missing wave table record at index %i.",i);
          goto errLabel;
        }

        // parse th eith wt cfg
        if((rc = wt->getv("instr",instr_label,
                          "pitch",wr.pitch,
                          "vel",wr.vel,
                          "chL",chL)) != kOkRC )
        {
          rc = cwLogError(rc,"Error parsing the wave table record at index %i.",i);
          goto errLabel;
        }

        if( wr.pitch >= midi::kMidiNoteCnt )
        {
          rc = cwLogError(kSyntaxErrorRC,"The MIDI pitch value %i is invalid on the WT record %s vel:%i.",wr.pitch,cwStringNullGuard(instr_label),wr.vel);
          goto errLabel;
        }

        if( wr.vel >= midi::kMidiVelCnt )
        {
          rc = cwLogError(kSyntaxErrorRC,"The MIDI velocity value %i is invalid on the WT record %s pitch:%i.",wr.vel,cwStringNullGuard(instr_label),wr.pitch);
          goto errLabel;
        }
        
        // count or store the wt recd
        wr.instr_id = count_fl ? kInvalidId : _find_or_add_instr_id(p,instr_label);
        wr.chN  = chL->child_count();
        wr.chA  = count_fl ? nullptr : af->chA + ch_idx;
        ch_idx += wr.chN;

        // for each channel cfg on this wt cfg
        for(unsigned j=0; j<wr.chN; ++j)
        {
          const object_t* ch   = nullptr;
          const object_t* segL = nullptr;
          ch_t         c;          
          ch_t&        cr   = count_fl ? c : wr.chA[j];
          
          af->chN  += count_fl ? 1 : 0;

          // get the jth ch cfg
          if((ch = chL->child_ele(j)) == nullptr )
          {
            rc = cwLogError(kSyntaxErrorRC,"Unexpected missing channel record at wt index %i ch index:%i.",i,j);
            goto errLabel;
          }

          // parse the ch cfg
          if((rc = ch->getv("ch_idx", cr.ch_idx,
                            "segL",   segL)) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing the channel record at wt index %i ch index:%i.",i,j);
            goto errLabel;
          }

          // count or store the ch cfg
          cr.segN  = segL->child_count();
          cr.segA  = count_fl ? nullptr : af->segA + seg_idx;
          seg_idx += cr.segN;

          // for each seg cfg on this ch cfg
          for(unsigned k=0; k<cr.segN; ++k)
          {
            const object_t* seg = nullptr;
            unsigned        bsi = kInvalidIdx;
            unsigned        esi = kInvalidIdx;
            seg_t           s;
            seg_t&          sr  = count_fl ? s : cr.segA[k];
            
            af->segN += count_fl ? 1 : 0;

            // get the kth seg cfg
            if((seg = segL->child_ele(k)) == nullptr )
            {
              rc = cwLogError(kSyntaxErrorRC,"Unexpected missing segment record at wt index %i ch index:%i seg index:%i.",i,j,k);
              goto errLabel;
            }

            // parse the kth seg cfg
            if((rc = seg->getv("cost",sr.cost,
                               "bsi",bsi,
                               "esi",esi)) != kOkRC )
            {
              rc = cwLogError(rc,"Error parsing the segment record at wt index %i ch index:%i seg index:%i.",i,j,k);
              goto errLabel;
            }

            // set the type of this seg
            sr.tid = k==0 ? kAttackTId : kLoopTId;

            // if storing then load the audio into the seg
            if( !count_fl )
            {
              if( cr.ch_idx >= audio_ch_bufN )
              {
                rc = cwLogError(kSyntaxErrorRC,"The invalid audio channel index %i was encountered on wt index %i ch index:%i seg index:%i.",cr.ch_idx,i,j,k);
                goto errLabel;
              }

              _load_segment_audio(sr,audio_ch_buf,audio_ch_bufN, padSmpN, cr.ch_idx, sr.tid, bsi, esi );
            }
          }
        }          
      }

      assert( ch_idx == af->chN);
      assert( seg_idx == af->segN );
      
    errLabel:
      return rc;
    }
                      
    rc_t _load_af_wt_array( wt_bank_t* p, af_t* af, const object_t* wtL, unsigned padN )
    {
      rc_t rc = kOkRC;
      audiofile::handle_t afH;
      audiofile::info_t   af_info;
      sample_t* smpV = nullptr;
      
      if((rc = open( afH, af->audio_fname, &af_info )) != kOkRC )
      {
        rc = cwLogError(rc,"Audio sample file open failed.");
        goto errLabel;
      }
      else
      {
        sample_t* buf[ af_info.chCnt ];
        unsigned  actualFrmN = 0;
        unsigned  smpN = af_info.frameCnt * af_info.chCnt;
        
        // allocate memory to hold the entire audio file
        smpV       = mem::alloc<sample_t>( smpN );

        // create the channel buffer
        for(unsigned i=0; i<af_info.chCnt; ++i)
          buf[i] = smpV + (i * af_info.frameCnt);

        // read the audio file
        if((rc = readFloat(afH, af_info.frameCnt, 0, af_info.chCnt, buf, &actualFrmN )) != kOkRC )
        {
          rc = cwLogError(rc,"Audio file read failed on '%s'.",cwStringNullGuard(af->audio_fname));
          goto errLabel;
        }

        // Parse the cfg and fill the associated af.
        if((rc = _parse_cfg( p, af,wtL, buf, af_info.chCnt, padN)) != kOkRC )
          goto errLabel;
        
      }
      
    errLabel:

      mem::release(smpV);
      close(afH);
      
      return rc;
      
    }

    rc_t _load_cfg_file( wt_bank_t* p, const char* cfg_fname, unsigned padN )
    {
      rc_t            rc           = kOkRC;
      srate_t         srate        = 0;
      const object_t* wtL          = nullptr;

      af_t* af = mem::allocZ<af_t>();

      af->link = p->afList;
      p->afList = af;
 

      if((rc = objectFromFile(cfg_fname,af->cfg)) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to open the wavetable file '%s'.",cwStringNullGuard(cfg_fname));
        goto errLabel;
      }

      if((rc = af->cfg->getv("audio_fname",af->audio_fname,
                             "srate",srate,
                             "wt",wtL)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"The wave table header parse failed.");
        goto errLabel;
      }

      // Determine the count of wt,ch, and seg records required by this af_t record,
      // and fill in af->wtN, af->chN and af->segN
      if((rc = _parse_cfg( p, af, wtL )) != kOkRC )
        goto errLabel;

      cwLogInfo("wtN:%i chN:%i segN:%i audio:%s",af->wtN,af->chN,af->segN,af->audio_fname);
  
      af->wtA  = mem::allocZ<wt_t>(af->wtN);
      af->chA  = mem::allocZ<ch_t>(af->chN);
      af->segA = mem::allocZ<seg_t>(af->segN);

      // Parse the wt list and load the af.
      if((rc = _load_af_wt_array( p, af, wtL, padN )) != kOkRC )
        goto errLabel;
      
    errLabel:
      return rc;
    }

    void _load_wt_map( wt_bank_t* p, unsigned instr_id, wt_t**& wt_map )
    {
      for(af_t* af=p->afList; af!=nullptr; af=af->link)
        for(unsigned i=0; i<af->wtN; i++)
          if( af->wtA[i].instr_id == instr_id )
          {
            unsigned idx =  af->wtA[i].vel * midi::kMidiNoteCnt + af->wtA[i].pitch;

            assert( idx < midi::kMidiNoteCnt * midi::kMidiVelCnt );
            
            if( wt_map[idx] != nullptr )
              cwLogWarning("Multiple wt records map to instr:%i pitch:%i vel:%i. Only one will be preserved",instr_id,af->wtA[i].pitch,af->wtA[i].vel);
            
            wt_map[idx] = af->wtA + i;
          }
    }

    rc_t _create_wt_map_array( wt_bank_t* p )
    {
      rc_t rc = kOkRC;
      p->wtMapN = p->next_instr_id;
      p->wtMapA = mem::allocZ<wt_map_t>(p->wtMapN);
      
      for(unsigned i=0; i<p->wtMapN; ++i)
      {
        p->wtMapA[i].map = mem::allocZ<wt_t*>(midi::kMidiNoteCnt * midi::kMidiVelCnt );

        _load_wt_map( p, i, p->wtMapA[i].map );
        
      }

      return rc;
    }

  }
}

cw::rc_t cw::wt_bank::create( handle_t& hRef, const char* dir, unsigned padN )
{
  rc_t                 rc  = kOkRC;
  filesys::dirEntry_t* de  = nullptr;
  unsigned             deN = 0;
  wt_bank_t*           p   = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;
  
  p = mem::allocZ<wt_bank_t>();

  // get the filenames in the directory 'dir'.
  if((de = filesys::dirEntries( dir, filesys::kFileFsFl | filesys::kFullPathFsFl, &deN )) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"Read failed on directory: %s", cwStringNullGuard(dir));
    goto errLabel;
  }

  // for each filename create an 'af_t' record and load it.
  for(unsigned i=0; i<deN; ++i)
    if((rc = _load_cfg_file( p, de[i].name, padN )) != kOkRC )
      goto errLabel;

  if((rc = _create_wt_map_array(p)) != kOkRC)
     goto errLabel;
  
  hRef.set(p);

errLabel:

  mem::release(de);
  
  if( rc != kOkRC )
    _destroy(p);

  return rc;
}
      
cw::rc_t cw::wt_bank::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if(!hRef.isValid() )
    return rc;

  wt_bank_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  
  return rc;
}

void cw::wt_bank::report( handle_t h )
{
  wt_bank_t* p = _handleToPtr(h);
  _report(p);
}

unsigned cw::wt_bank::instr_count( handle_t h )
{
  wt_bank_t* p = _handleToPtr(h);
  return p->next_instr_id;
}

unsigned cw::wt_bank::instr_index( handle_t h, const char* instr_label )
{
  wt_bank_t* p = _handleToPtr(h);
  instr_t* instr;
  
  if((instr = _find_instr(p,instr_label)) != nullptr )
    return instr->id;

  return kInvalidIdx;
}


const cw::wt_bank::wt_t* cw::wt_bank::get_wave_table( handle_t h, unsigned instr_idx, unsigned pitch, unsigned vel )
{
  rc_t rc = kOkRC;

  wt_bank_t* p = _handleToPtr(h);
  
  if( instr_idx >= p->wtMapN )
  {
    rc = cwLogError(kInvalidArgRC,"Invalid instr_idx %i",instr_idx);
    goto errLabel;
  }

  if( pitch >= midi::kMidiNoteCnt )
  {
    rc = cwLogError(kInvalidArgRC,"Invalid MIDI pitch %i",pitch);
    goto errLabel;
  }

  if( vel >= midi::kMidiVelCnt )
  {
    rc = cwLogError(kInvalidArgRC,"Invalid MIDI pitch %i",pitch);
    goto errLabel;
  }

  return p->wtMapA[ instr_idx ].map[ vel * midi::kMidiNoteCnt + pitch ];

errLabel:
  cwLogError(rc,"Wave table lookup failed.");
  return nullptr;
}

cw::rc_t cw::wt_bank::test( const char* cfg_fname )
{
  rc_t     rc0  = kOkRC;
  rc_t     rc1  = kOkRC;
  unsigned padN = 8;
  
  handle_t h;
  
  if((rc0 = create(h,cfg_fname,padN)) != kOkRC )
    goto errLabel;

  report(h);
  
errLabel:
  if((rc1 = destroy(h)) != kOkRC )
  {
    rc1 = cwLogError(rc1,"Wave table bank destroy failed.");
  }
  
  return rcSelect(rc0,rc1);
}


