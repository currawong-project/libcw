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
#include "cwMath.h"
#include "cwVectOps.h"
#include "cwDspTypes.h"
#include "cwDsp.h"
#include "cwWaveTableBank.h"
#include "cwMidi.h"
#include "cwTest.h"

#ifdef NOT_DEF
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
          if(af->segA != nullptr )
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
                     unsigned               padSmpN       = 0,
                     srate_t                srate         = 0)
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
        wr.instr_id  = count_fl ? kInvalidId : _find_or_add_instr_id(p,instr_label);
        wr.chN       = chL->child_count();
        wr.chA       = count_fl ? nullptr : af->chA + ch_idx;
        wr.srate     = srate;
        ch_idx      += wr.chN;

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
                               "cyc_per_loop",sr.cyc_per_loop,
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
        if((rc = _parse_cfg( p, af,wtL, buf, af_info.chCnt, padN, af_info.srate)) != kOkRC )
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
      if( rc != kOkRC )
        rc = cwLogError(rc,"Wave table load failed on '%s'.",cwStringNullGuard(cfg_fname));
      
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


    const wt_t* _get_wave_table( wt_bank_t* p, unsigned instr_idx, unsigned pitch, unsigned vel )
    {
      rc_t rc = kOkRC;

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
    
    typedef struct seg_osc_str
    {
      seg_tid_t    tid;
      const seg_t* seg;
      unsigned     id;
      double       xphase;
      double       loop_frac_smpN; // length of the loop including fractional part
      unsigned     cur_loopN;   // count of loops
      sample_t*    envV;
      unsigned     envN;
      unsigned     delay_smp_idx;
      unsigned     cur_smp_idx;
      unsigned     ch_idx;
    } seg_osc_t;

    void _seg_osc_setup( seg_osc_t*   osc,
                         unsigned     id,
                         unsigned     ch_idx,
                         const seg_t* seg,
                         double       smp_per_cyc,
                         sample_t*    envV,
                         unsigned     envN,
                         unsigned     delay_smp_idx,
                         seg_tid_t    tid = kInvalidTId )
    {
      osc->seg = seg;
      osc->id  = id;
      osc->tid = tid==kInvalidTId ? seg->tid : tid;
      osc->loop_frac_smpN = osc->tid==kAttackTId ? seg->aN : smp_per_cyc * seg->cyc_per_loop;
      osc->xphase = 0;
      osc->envV = envV;
      osc->envN = envN;
      osc->delay_smp_idx = delay_smp_idx;
      osc->cur_smp_idx = 0;
      osc->ch_idx = ch_idx;
    }

    void _seg_osc_update( seg_osc_t* osc, sample_t* yV, unsigned yN, unsigned& actual_yN_ref )
    {
      unsigned     yi   = 0;
      const float* xV   = osc->seg->aV + osc->seg->padN;
      
      actual_yN_ref = 0;

      for(yi=0; yi<yN; ++yi,++osc->cur_smp_idx)
        if( osc->cur_smp_idx >= osc->delay_smp_idx )
        {

          double       xfi  = std::floor(osc->xphase);      
          double       frac = osc->xphase - xfi;
          int          xi   = (int)xfi;
                  
          yV[yi] += xV[xi] + (xV[xi+1] - xV[xi]) * frac * osc->envV[xi];

          /*
          float f = frac;
          float f_1 = f - 1.0;
          float f_2 = f - 2.0;
          float f1  = f + 1.0;
          
          yV[yi] += osc->envV[xi] * ((-f)*f_1*f_2*xV[xi-1]/6.0f + f1*f_1*f_2*xV[xi]/2.0f - f1*f*f_2*xV[xi+1]/2.0f + f1*f*f_1*xV[xi+2]/6.0f);
          */
          
          //if( osc->seg->tid==kLoopTId && osc->ch_idx==0 && osc->cur_loopN < 4 )
          //  printf("%i,%i,%i,%i,%f,%f,%f,%f\n",osc->id,osc->cur_smp_idx,yi,xi,frac,xV[xi],osc->envV[xi],yV[yi]);
        
          osc->xphase += 1.0f; // osc->seg->tid == kLoopTId ? 0.5f : 1.0f;

          // if the end of the wave table is encountered
          if( osc->xphase >= osc->loop_frac_smpN )
          {
            if( osc->tid != kLoopTId)
              goto errLabel;

            osc->xphase = osc->xphase - osc->loop_frac_smpN;            
            osc->cur_loopN += 1;
          }
        }

        
    errLabel:
      actual_yN_ref = yi;
      
    }


    void _seg_osc_update_0( seg_osc_t* osc, sample_t* yV, unsigned yN, unsigned& actual_yN_ref )
    {
      unsigned     yi   = 0;
      const float* xV   = osc->seg->aV + osc->seg->padN;
      double       xphs = osc->xphase;
      double       xfi  = std::floor(osc->xphase);      
      double       frac = xphs - xfi;
      int          xi   = (int)xfi;
      
      actual_yN_ref = 0;

      for(yi=0; yi<yN; ++yi,++osc->cur_smp_idx)
        if( osc->cur_smp_idx >= osc->delay_smp_idx )
        {        
        
          //yV[yi] = xV[xi] + (xV[xi+1] - xV[xi]) * frac;

          float f = frac;
          float f_1 = f - 1.0;
          float f_2 = f - 2.0;
          float f1  = f + 1.0;

          yV[yi] += osc->envV[xi] * ((-f)*f_1*f_2*xV[xi-1]/6.0f + f1*f_1*f_2*xV[xi]/2.0f - f1*f*f_2*xV[xi+1]/2.0f + f1*f*f_1*xV[xi+2]/6.0f);

          //if( loop_fl && ch_idx==0 && seg_loopN_ref < 4 )
          //  printf("%i,%i,%f,%f,%f\n",yi,xi,frac,xV[xi],yV[yi]);
        
          xi += 1;


          // if the end of the wave table is encountered
          if( frac+xi >= osc->loop_frac_smpN )
          {
            if( osc->tid != kLoopTId)
              goto errLabel;
          
            xphs = (frac+xi) - osc->loop_frac_smpN;
            xi   = (unsigned)std::floor(xphs);
            frac = xphs - xi;
            osc->cur_loopN += 1;
          }
        }

        
    errLabel:
      actual_yN_ref = yi;
      osc->xphase = frac + xi;
      
    }

    
    rc_t _gen_note( const wt_t* wt, srate_t srate, sample_t** outChV, unsigned y_frm_cnt )
    {
      rc_t rc = kOkRC;
      const unsigned frmPerUpdate = 64;
      
      double hz          = midi_to_hz( wt->pitch );
      double smp_per_cyc = srate / hz;

      // for each audio output channel
      for(unsigned ch_idx=0; ch_idx<wt->chN; ++ch_idx)
      {
        seg_osc_t a_osc, b_osc, l0_osc, l1_osc;
        seg_osc_t* cur_osc0 = &a_osc;
        seg_osc_t* cur_osc1 = nullptr;
        seg_osc_t* cur_oscb = nullptr;

        unsigned a_envN = wt->chA[ch_idx].segA[0].aN;
        sample_t a_envV[ a_envN ];
        vop::ones(a_envV,a_envN);
        
        unsigned l_envN = wt->chA[ch_idx].segA[1].aN;
        sample_t l_envV[ l_envN ];
        dsp::hann(l_envV,l_envN);

        unsigned b_envN = wt->chA[ch_idx].segA[1].aN;
        sample_t b_envV[ b_envN ];
        vop::zero(b_envV,b_envN);
        vop::copy(b_envV, l_envV+l_envN/2, l_envN/2);
        

        _seg_osc_setup( &a_osc,  0, ch_idx, wt->chA[ch_idx].segA,     smp_per_cyc, a_envV, a_envN, 0 );
        _seg_osc_setup( &b_osc,  1, ch_idx, wt->chA[ch_idx].segA + 1, smp_per_cyc, b_envV, b_envN, 0, kAttackTId );
        _seg_osc_setup( &l0_osc, 2, ch_idx, wt->chA[ch_idx].segA + 1, smp_per_cyc, l_envV, l_envN, 0 );
        _seg_osc_setup( &l1_osc, 3, ch_idx, wt->chA[ch_idx].segA + 1, smp_per_cyc, l_envV, l_envN, l_envN/2 );
        
        sample_t*    yV          = outChV[ch_idx];
        unsigned     y_frm_idx   = 0;

        // while the output channel is not full
        while( y_frm_idx < y_frm_cnt )
        {
          unsigned y_upd_cnt = 0;

          // while the current frame update has not generated frmPerUpdate samples
          while( y_upd_cnt < frmPerUpdate && y_frm_idx < y_frm_cnt )
          {
            unsigned y_actual_upd_cnt = 0;

            unsigned n = std::min(frmPerUpdate, std::min(frmPerUpdate-y_upd_cnt, y_frm_cnt-y_frm_idx));

            if( cur_oscb != nullptr )
            {
              _seg_osc_update( cur_oscb, yV + y_frm_idx, n, y_actual_upd_cnt);
              if( y_actual_upd_cnt != n )
                cur_oscb = nullptr;
            }
            
            _seg_osc_update( cur_osc0, yV + y_frm_idx, n, y_actual_upd_cnt);
            
            if( cur_osc1 != nullptr )
              _seg_osc_update( cur_osc1, yV + y_frm_idx, n, y_actual_upd_cnt);
            
            y_upd_cnt += y_actual_upd_cnt;
            y_frm_idx += y_actual_upd_cnt;

            // if the segment ran out of samples ...
            if( y_actual_upd_cnt < n )
            {
              // (only attack segments run out of samples - because they do not loop)
              assert( cur_osc0->tid == kAttackTId );

              cur_osc0 = &l0_osc;
              //cur_osc1 = &l1_osc;
              //cur_oscb = &b_osc;
            }
          }
        }

      }
      return rc;
    }

    /*
    void _gen_osc_update(const sample_t* xV, unsigned xN, double loop_frac_smpN, double& xPhs_ref, sample_t* yV, unsigned yN,  unsigned& actual_yN_ref, bool loop_fl, unsigned& seg_loopN_ref, unsigned ch_idx  )
    {
      unsigned yi   = 0;
      double   xphs = xPhs_ref;
      double   xfi  = std::floor(xphs);      
      double   frac = xphs - xfi;
      int      xi   = (int)xfi;
      
      actual_yN_ref = 0;

      for(yi=0; yi<yN; ++yi)
      {        
        
        //yV[yi] = xV[xi] + (xV[xi+1] - xV[xi]) * frac;

        float f = frac;
        float f_1 = f - 1.0;
        float f_2 = f - 2.0;
        float f1  = f + 1.0;
        
        yV[yi] = (-f)*f_1*f_2*xV[xi-1]/6.0f + f1*f_1*f_2*xV[xi]/2.0f - f1*f*f_2*xV[xi+1]/2.0f + f1*f*f_1*xV[xi+2]/6.0f;

        //if( loop_fl && ch_idx==0 && seg_loopN_ref < 4 )
        //  printf("%i,%i,%f,%f,%f\n",yi,xi,frac,xV[xi],yV[yi]);
        
        xi += 1;

        // if the end of the wave table is encountered
        if( frac+xi >= loop_frac_smpN )
        {
          if( !loop_fl)
            goto errLabel;
          
          xphs = (frac+xi) - loop_frac_smpN;
          xi   = (unsigned)std::floor(xphs);
          frac = xphs - xi;
          seg_loopN_ref += 1;
        }
      }

        
    errLabel:
      actual_yN_ref = yi;
      xPhs_ref = frac + xi;
    }
    
    rc_t _gen_note_0( const wt_t* wt, srate_t srate, sample_t** outChV, unsigned y_frm_cnt )
    {
      rc_t rc = kOkRC;
      const unsigned frmPerUpdate = 64;
      
      double hz          = midi_to_hz( wt->pitch );
      double smp_per_cyc = srate / hz;

      // for each audio output channel
      for(unsigned ch_idx=0; ch_idx<wt->chN; ++ch_idx)
      {
        sample_t*    yV          = outChV[ch_idx];
        unsigned     y_frm_idx   = 0;
        const seg_t* seg         = wt->chA[ch_idx].segA;
        unsigned     seg_idx     = 0;
        unsigned     seg_smp_cnt = seg->aN;
        double       seg_phase   = 0;
        double       seg_frac_smpN = seg_smp_cnt;
        unsigned     seg_loop_cnt  = 0;

        // while the output channel is not full
        while( y_frm_idx < y_frm_cnt )
        {
          unsigned y_upd_cnt = 0;

          // while the current frame update has not generated frmPerUpdate samples
          while( y_upd_cnt < frmPerUpdate && y_frm_idx < y_frm_cnt )
          {
            unsigned y_actual_upd_cnt = 0;

            // TODO: handle case where y_frm_cnt is not an even multiple of frmPerUpdate

            // attempt to generate frmPerUpdate samples into yV[ y_frm_idx:y_frm_idx + frmPerUpdate ]
            _gen_osc_update(seg->aV + seg->padN, seg_smp_cnt, seg_frac_smpN, seg_phase, yV + y_frm_idx, frmPerUpdate, y_actual_upd_cnt, seg->tid==kLoopTId, seg_loop_cnt, ch_idx  );

            
            y_upd_cnt += y_actual_upd_cnt;
            y_frm_idx += y_actual_upd_cnt;

            // if the segment ran out of samples ...
            if( y_actual_upd_cnt < frmPerUpdate )
            {
              // (only attack segments run out of samples - because they do not loop)
              assert( seg->tid == kAttackTId );              

              // ...then advance to the next segment
              seg_idx += 1;
              if( seg_idx >= wt->chA[ch_idx].segN )
              {
                // done
                goto errLabel;
              }
              
              seg           = wt->chA[ch_idx].segA + seg_idx;
              seg_phase     = 0;
              seg_smp_cnt   = seg->aN;
              seg_frac_smpN = smp_per_cyc * seg->cyc_per_loop;
              seg_loop_cnt  = 0;
            }
          }
        }
      }
    errLabel:
      return rc;
    }
    */
    
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
  wt_bank_t* p = _handleToPtr(h);

  return _get_wave_table(p,instr_idx, pitch, vel);  
}

cw::rc_t cw::wt_bank::gen_notes( handle_t h, unsigned instr_idx, const unsigned* pitchA, const unsigned* velA, unsigned noteN, double note_dur_sec, const char* out_fname, double inter_note_gap_sec )
{
  rc_t        rc       = kOkRC;
  wt_bank_t*  p        = _handleToPtr(h);
  const wt_t* wtA[ noteN ];
  srate_t     srate    = 0;
  unsigned    outFrmN  = 0;
  unsigned    noteSmpN = 0;
  unsigned    gapSmpN  = 0; 
  unsigned    yFrmIdx  = 0;
  unsigned    chN      = 0;
  sample_t*   outV     = nullptr;

  // Examine the wave table and determine the srate,audio ch. count, and output signal size.
  for(unsigned i=0; i<noteN; ++i)
  {
    if((wtA[i] = _get_wave_table(p,instr_idx,pitchA[i],velA[i])) == nullptr )
    {
      rc = cwLogError(kInvalidArgRC,"The wave table at instr:%i pitch:%i vel:%i does not exist.",instr_idx,pitchA[i],velA[i]);
      goto errLabel;
    }

    if( i==0 )
    {
      srate = wtA[i]->srate;
      chN   = wtA[i]->chN;

      noteSmpN = (unsigned)(srate * note_dur_sec);
      gapSmpN  = (unsigned)(srate * inter_note_gap_sec);
      
    }
    else
    {
      assert( srate == wtA[i]->srate );
      assert( chN   == wtA[i]->chN );
    }

    printf("pitch:%i vel:%i s/cyc:%f\n", wtA[i]->pitch, wtA[i]->vel, srate/midi_to_hz(wtA[i]->pitch) );
    
    for(unsigned j=0; j<wtA[i]->chN; ++j)
    {
      printf("  ch:%i\n",wtA[i]->chA[j].ch_idx);
      
      for(unsigned k=0; k<wtA[i]->chA[j].segN; ++k)
      {
        printf("    %i aN:%i padN:%i\n",
               wtA[i]->chA[j].segA[k].tid,
               wtA[i]->chA[j].segA[k].aN,
               wtA[i]->chA[j].segA[k].padN );
      }
    }
    
    outFrmN += noteSmpN + gapSmpN;
  }

  
  if( outFrmN==0 || chN == 0 )
  {
    rc = cwLogError(kInvalidArgRC,"The sample rate:%f, output audio signal length (%i), or channel count (%i) is 0.",srate,outFrmN,chN);
    goto errLabel;
  }
  else
  {
    sample_t* outChV[ chN ];
    
    outV = mem::allocZ<sample_t>(outFrmN*chN);

    // for each note
    for(unsigned i=0; i<noteN; ++i)
    {
      // calc. the output sample frames for this note
      unsigned yNoteFrmN = yFrmIdx+noteSmpN > outFrmN ? outFrmN-yFrmIdx : noteSmpN;

      // load the output audio channel vector
      for(unsigned ch_idx=0; ch_idx<chN; ++ch_idx)
      {
        outChV[ch_idx] = outV + ch_idx*outFrmN + yFrmIdx;

        assert( yFrmIdx+yNoteFrmN  < outFrmN );
      }

      // generate the note audio 
      if((rc = _gen_note( wtA[i], srate, outChV, yNoteFrmN )) != kOkRC )
      {
        rc = cwLogError(rc,"Note generation failed on instr:%i pitch:%i vel:%i",instr_idx,pitchA[i],velA[i]);
        goto errLabel;
      }

      yFrmIdx += yNoteFrmN + gapSmpN;

    }

    for(unsigned i=0; i<chN; ++i)
      outChV[i] = outV + i*outFrmN;
    
    // write the output signal to an audio file
    if((rc = audiofile::writeFileFloat(out_fname, srate, 32, outFrmN, chN, outChV )) != kOkRC )
    {
      rc = cwLogError(rc,"Audio file write failed on '%s'.",cwStringNullGuard(out_fname));
      goto errLabel;
    }
    
  }


errLabel:

  mem::release(outV);
  return rc;
 
}

cw::rc_t cw::wt_bank::test( const test::test_args_t& args )
{
  rc_t     rc0  = kOkRC;
  rc_t     rc1  = kOkRC;
  unsigned padN = 8;
  const char* cfg_fname;
  handle_t h;

  unsigned instr_idx = 0;
  unsigned pitchA[] = { 21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21, 21, 21, 21, 21, 21, 21 };
  //unsigned pitchA[] = { 60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60, 60, 60, 60, 60, 60, 60 };
  unsigned velA[] =   {  1, 5,10,16,21,26,32,37,42,48,53,58,64,69,74,80,85,90,96,101,106,112,117,122,127 };
  
  //unsigned velA[] = { 117, 122 };
  double note_dur_sec = 2.5;
    

  if((rc0 = args.test_args->getv("wtb_cfg_fname",cfg_fname)) != kOkRC )
    goto errLabel;
  
  if((rc0 = create(h,cfg_fname,padN)) != kOkRC )
    goto errLabel;


  assert( sizeof(pitchA)/sizeof(pitchA[0]) == sizeof(velA)/sizeof(velA[0]) );
  
  gen_notes(h,instr_idx,pitchA,velA,sizeof(pitchA)/sizeof(pitchA[0]),note_dur_sec,"~/temp/temp.wav");
  
  //report(h);
  
errLabel:
  if((rc1 = destroy(h)) != kOkRC )
  {
    rc1 = cwLogError(rc1,"Wave table bank destroy failed.");
  }
  
  return rcSelect(rc0,rc1);
}
#endif



namespace cw
{
  namespace wt_bank
  {
    typedef struct vel_str
    {
      unsigned vel;
      multi_ch_wt_seq_t mc_seq;
    } vel_t;
    
    typedef struct pitch_str
    {
      unsigned midi_pitch;
      vel_t*   velA;
      unsigned velN;
    } pitch_t;
    
    typedef struct instr_str
    {
      char*    label;
      pitch_t* pitchA;
      unsigned pitchN;
      struct instr_str* link;      
    } instr_t;

    typedef struct wt_bank_str
    {
      unsigned allocAudioBytesN;
      unsigned padSmpN;
      instr_t* instrList;
    } wt_bank_t;

    typedef struct audio_buf_str
    {
      audiofile::handle_t afH;
      
      unsigned   allocFrmN = 0;
      unsigned   allocChN = 0;
      unsigned   chN = 0;
      unsigned   frmN = 0;
      sample_t** ch_buf = nullptr;  // chBuf[chN][frmN]
    } audio_buf_t;

    wt_bank_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,wt_bank_t>(h); }

    void _audio_buf_free( audio_buf_t& ab )
    {
      audiofile::close(ab.afH);
      
      for(unsigned i=0; i<ab.chN; ++i)
        mem::release(ab.ch_buf[i]);
      
      mem::release(ab.ch_buf);
      ab.allocChN = 0;
      ab.allocFrmN = 0;
      ab.chN = 0;
      ab.frmN = 0;
    }
    
    rc_t _audio_buf_alloc( audio_buf_t& ab, const char* fname )
    {
      rc_t rc = kOkRC;
      audiofile::info_t info;
      unsigned actualFrmCnt = 0;
      
      // open the audio file
      if((rc = audiofile::open(ab.afH,fname,&info)) != kOkRC )
      {
        rc = cwLogError(rc,"Instrument audio file open failed.");
        goto errLabel;
      }

      // if the buffer has too few channels
      if( info.chCnt > ab.allocChN )
      {
        ab.ch_buf = mem::resizeZ<sample_t*>( ab.ch_buf, info.chCnt );
        ab.allocChN = info.chCnt;
      }
      
      for(unsigned i=0; i<info.chCnt; ++i)
      {
        // if this channel has too few frames
        if( ab.ch_buf[i] == nullptr || info.frameCnt > ab.allocFrmN )
        {
          ab.ch_buf[i] = mem::resizeZ<sample_t>( ab.ch_buf[i], info.frameCnt );
          ab.allocFrmN = info.frameCnt;
        }
      }
      
      if((rc = audiofile::readFloat(ab.afH, info.frameCnt, 0, info.chCnt, ab.ch_buf, &actualFrmCnt )) != kOkRC )
      {
        rc = cwLogError(rc,"The instrument audio file read failed.");
        goto errLabel;
      }

      assert( info.frameCnt == actualFrmCnt );
      
      ab.chN = info.chCnt;
      ab.frmN = actualFrmCnt;

    errLabel:
      return rc;
    }



    void _destroy_instr( instr_t* instr )
    {
      mem::release(instr->label);
      for(unsigned i=0; i<instr->pitchN; ++i)
      {
        pitch_t* pr = instr->pitchA + i;
        for(unsigned j=0; j<pr->velN; ++j)
        {
          vel_t* vr = pr->velA + j;
          for(unsigned ch_idx=0; ch_idx<vr->mc_seq.chN; ++ch_idx)
          {
            wt_seq_t* wts = vr->mc_seq.chA + ch_idx;
            for(unsigned wti=0; wti<wts->wtN; ++wti)
            {
              wt_t* wt = wts->wtA + wti;
              mem::release(wt->aV);              
            }
            mem::release(wts->wtA);
          }
          mem::release(vr->mc_seq.chA);            
        }
        mem::release(pr->velA);
      }
      mem::release(instr->pitchA);
      mem::release(instr);
    }
    
    rc_t _destroy( wt_bank_t* p )
    {
      rc_t rc = kOkRC;

      instr_t* instr = p->instrList;
      while( instr != nullptr )
      {
        instr_t* instr0 = instr->link;
        _destroy_instr(instr);
        instr = instr0;        
      }
      
      mem::release(p);
      return rc;
    }
  }
}


cw::rc_t cw::wt_bank::create( handle_t& hRef, unsigned padSmpN )
{
  rc_t rc = kOkRC;

  if(destroy(hRef) != kOkRC )
    return rc;

  wt_bank_t* p = mem::allocZ<wt_bank_t>();
  p->padSmpN = padSmpN;

  hRef.set(p);

  if(rc != kOkRC )
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::wt_bank::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  wt_bank_t* p =nullptr;
  if( !hRef.isValid() )
    return rc;

  p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  
  return rc;
}

cw::rc_t cw::wt_bank::load( handle_t h, const char* instr_json_fname )
{
  rc_t            rc          = kOkRC;
  wt_bank_t*      p           = _handleToPtr(h);
  object_t*       f           = nullptr;
  const object_t* pitchL      = nullptr;
  const char*     instr_label = nullptr;
  instr_t*        instr       = nullptr;
  audio_buf_t     abuf{};
  
  if((rc = objectFromFile(instr_json_fname,f)) != kOkRC )
    goto errLabel;

  if( f == nullptr || !f->is_dict() )
  {
    rc = cwLogError(kSyntaxErrorRC,"The instrument file header is not valid.");
    goto errLabel;
  }

  if((rc = f->getv("instr",instr_label,
                   "pitchL",pitchL)) != kOkRC )
  {
    rc = cwLogError(rc,"Instrument file syntax error while reading file header.");
    goto errLabel;
  }

  instr         = mem::allocZ<instr_t>();
  instr->label  = mem::duplStr(instr_label);
  instr->pitchN = pitchL->child_count();
  instr->pitchA = mem::allocZ<pitch_t>(instr->pitchN);

  for(unsigned i=0; i<instr->pitchN; ++i)
  {
    double            hz          = 0;
    srate_t           srate       = 0;
    const char*       audio_fname = nullptr;
    const object_t*   velL        = nullptr;
    const object_t*   pitchR      = pitchL->child_ele(i);
    pitch_t*          pitch       = instr->pitchA + i;
    
    if(pitchR == nullptr || !pitchR->is_dict() )
    {
      rc = cwLogError(kSyntaxErrorRC,"The pitch record at index %i is not valid.",i);
      goto errLabel;
    }

    if((rc = pitchR->getv("midi_pitch",pitch->midi_pitch,
                          "srate",srate,
                          "est_hz_mean",hz,
                          "audio_fname",audio_fname,
                          "velL",velL)) != kOkRC )
    {
      rc = cwLogError(rc,"Instrument file syntax error while reading pitch record at index %i.",i);
      goto errLabel;
    }

    cwLogInfo("pitch:%i %i %s",pitch->midi_pitch,p->allocAudioBytesN/(1024*1024),audio_fname);
    
    // read the audio file into abuf
    if((rc = _audio_buf_alloc(abuf, audio_fname )) != kOkRC )
      goto errLabel;
    
    pitch->velN = velL->child_count();
    pitch->velA = mem::allocZ<vel_t>( pitch->velN );
    
    for(unsigned j=0; j<instr->pitchA[i].velN; ++j)
    {
      const object_t* chL  = nullptr;
      const object_t* velR = velL->child_ele(j);
      vel_t*          vel  = pitch->velA + j;

      if( velR==nullptr || !velR->is_dict() )
      {
        rc = cwLogError(rc,"The velocity record at index %i on MIDI pitch %i is invalid.",j,pitch->midi_pitch);
        goto errLabel;
      }
      
      if((rc = velR->getv("vel",vel->vel,
                          "chL",chL)) != kOkRC )
      {
        rc = cwLogError(rc,"Instrument file syntax error while reading the velocity record at index %i from the pitch record for MIDI pitch:%i.",j,pitch->midi_pitch);
        goto errLabel;
      }

      
      vel->mc_seq.chN = chL->child_count();
      vel->mc_seq.chA = mem::allocZ<wt_seq_t>( vel->mc_seq.chN );
      
      for(unsigned ch_idx=0; ch_idx<instr->pitchA[i].velA[j].mc_seq.chN; ++ch_idx)
      {

        const object_t* wtL = chL->child_ele(ch_idx);
        wt_seq_t*       wts = vel->mc_seq.chA + ch_idx;
        
        wts->wtN = wtL->child_count();
        wts->wtA = mem::allocZ<wt_t>( wts->wtN );
        
        for(unsigned wti=0; wti<wts->wtN; ++wti)
        {
          const object_t* wtR  = wtL->child_ele(wti);
          wt_t*           wt   = wts->wtA + wti;
          unsigned        wtbi = kInvalidIdx;
          unsigned        wtei = kInvalidIdx;
          unsigned        allocSmpCnt = 0;
          
          if((rc = wtR->getv("wtbi",wtbi,
                             "wtei",wtei,
                             "rms",wt->rms,
                             "est_hz",wt->hz)) != kOkRC )
          {
            rc = cwLogError(rc,"Instrument file syntax error in wavetable record at index %i, channel index:%i, velocity index:%i midi pitch:%i.",wti,ch_idx,j,pitch->midi_pitch);
            goto errLabel;
          }

          
          wt->cyc_per_loop = 1;
          wt->hz = hz;
          wt->aN = wtei-wtbi;

          allocSmpCnt = p->padSmpN+wt->aN+p->padSmpN;
          p->allocAudioBytesN += allocSmpCnt * sizeof(sample_t);            
          
          // allocate the wavetable audio buffer
          wt->aV = mem::allocZ<sample_t>( allocSmpCnt );

          // fill the wavetable from the audio file
          vop::copy(wt->aV+p->padSmpN, abuf.ch_buf[ch_idx] + wtbi, wt->aN);
          
          // fill the wavetable prefix
          vop::copy(wt->aV, wt->aV + p->padSmpN + (wt->aN-p->padSmpN), p->padSmpN );

          // fill the wavetable suffix
          vop::copy(wt->aV + p->padSmpN + wt->aN, wt->aV + p->padSmpN, p->padSmpN );
          
        }
      }
    }    
  }

  instr->link = p->instrList;
  p->instrList = instr;
  
errLabel:
  if(rc != kOkRC )
  {
    if( instr != nullptr )
      _destroy_instr(instr);
    rc = cwLogError(rc,"Wave table bank load failed on '%s'.",cwStringNullGuard(instr_json_fname));
  }

  _audio_buf_free(abuf);
  
  f->free();
  
  return rc;
}

void cw::wt_bank::report( handle_t h )
{
  wt_bank_t* p = _handleToPtr(h);
  for(instr_t* instr = p->instrList; instr!=nullptr; instr=instr->link)
  {
    cwLogPrint("%s \n",instr->label);
    for(unsigned i=0; i<instr->pitchN; ++i)
    {
      const pitch_t* pitch = instr->pitchA + i;
      cwLogPrint("  pitch:%i\n",pitch->midi_pitch);

      for(unsigned j=0; j<pitch->velN; ++j)
      {
        vel_t* vel = pitch->velA + j;
        bool   fl  = true;
        
        cwLogPrint("    vel:%i\n",vel->vel);

        for(unsigned wti=0; fl; ++wti)
        {

          const char* indent = "      ";
          for(unsigned ch_idx=0; ch_idx<vel->mc_seq.chN; ++ch_idx)
            if( wti < vel->mc_seq.chA[ch_idx].wtN )
            {
              wt_t* wt=vel->mc_seq.chA[ch_idx].wtA + wti;
              
              cwLogPrint("%s(%i %f %f) ",indent,wt->aN,wt->rms,wt->hz);
              indent="";
              fl = true;
            }
          
          if( fl )
            cwLogPrint("\n");
        }
        
      }
    }
  }
  
  
}

unsigned cw::wt_bank::instr_count( handle_t h )
{
  wt_bank_t* p = _handleToPtr(h);
  
  unsigned n = 0;
  for(instr_t* instr=p->instrList; instr!=nullptr; instr=instr->link)
    ++n;
  
  return n;
}

unsigned cw::wt_bank::instr_index( handle_t h, const char* instr_label )
{
  wt_bank_t* p = _handleToPtr(h);
  
  unsigned i = 0;
  for(instr_t* instr=p->instrList; instr!=nullptr; instr=instr->link,++i)
    if( textIsEqual(instr->label,instr_label) )
      return i;
  
  return kInvalidIdx;
}

const cw::wt_bank::wt_t* cw::wt_bank::get_wave_table( handle_t h, unsigned instr_idx, unsigned pitch, unsigned vel )
{
  return nullptr;  
}

cw::rc_t cw::wt_bank::gen_notes( handle_t h, unsigned instr_idx, const unsigned* pitchA, const unsigned* velA, unsigned noteN, double dur_secs, const char* out_fname, double inter_note_gap_secs )
{
  rc_t rc = kOkRC;
  return rc;
}
    
cw::rc_t cw::wt_bank::test( const test::test_args_t& args )
{
  rc_t     rc0  = kOkRC;
  rc_t     rc1  = kOkRC;
  unsigned padN = 2;
  const char* cfg_fname;
  handle_t h;

  //unsigned instr_idx = 0;
  //unsigned pitchA[] = { 21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21, 21, 21, 21, 21, 21, 21 };
  //unsigned pitchA[] = { 60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60, 60, 60, 60, 60, 60, 60 };
  //unsigned velA[] =   {  1, 5,10,16,21,26,32,37,42,48,53,58,64,69,74,80,85,90,96,101,106,112,117,122,127 };
  
  //unsigned velA[] = { 117, 122 };
  //double note_dur_sec = 2.5;
    

  if((rc0 = args.test_args->getv("wtb_cfg_fname",cfg_fname)) != kOkRC )
    goto errLabel;
  
  if((rc0 = create(h,padN)) != kOkRC )
    goto errLabel;

  if((rc0 = load(h,cfg_fname)) != kOkRC )
    goto errLabel;

  //assert( sizeof(pitchA)/sizeof(pitchA[0]) == sizeof(velA)/sizeof(velA[0]) );
  
  //gen_notes(h,instr_idx,pitchA,velA,sizeof(pitchA)/sizeof(pitchA[0]),note_dur_sec,"~/temp/temp.wav");
  
  report(h);
  
errLabel:
  if((rc1 = destroy(h)) != kOkRC )
  {
    rc1 = cwLogError(rc1,"Wave table bank destroy failed.");
  }
  
  return rcSelect(rc0,rc1);
}
