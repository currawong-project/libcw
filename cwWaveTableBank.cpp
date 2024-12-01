//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
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
#include "cwAudioTransforms.h"
#include "cwWaveTableBank.h"
#include "cwMidi.h"
#include "cwTest.h"

namespace cw
{
  namespace wt_bank
  {
    typedef struct vel_str
    {
      unsigned vel;
      unsigned bsi;
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
      char*               label;
      pitch_t*            pitchA;
      unsigned            pitchN;
      
      multi_ch_wt_seq_t** pvM;  // pgM[128x128] pgmM[pitch][vel] // each row holds the vel's for a given pitch
      struct instr_str*   link;
    } instr_t;

    typedef struct wt_bank_str
    {
      unsigned allocAudioBytesN;
      unsigned padSmpN;
      
      unsigned instrN;
      instr_t** instrA;
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
      mem::release(instr->pvM);
      mem::release(instr);
    }
    
    rc_t _destroy( wt_bank_t* p )
    {
      rc_t rc = kOkRC;

      for(unsigned i=0; i<p->instrN; ++i )
        _destroy_instr(p->instrA[i]);
      
      mem::release(p);
      return rc;
    }

    void _alloc_wt( wt_bank_t*      p,
                    wt_t*           wt,
                    wt_tid_t        tid,
                    srate_t         srate,
                    const sample_t* aV,
                    unsigned        posn_smp_idx,
                    unsigned        aN,
                    double          hz,
                    double          rms)
    {
      wt->tid          = tid;
      wt->aN           = aN;
      wt->hz           = hz;
      wt->rms          = rms;
      wt->cyc_per_loop = 1;
      wt->srate        = srate;
      wt->pad_smpN     = p->padSmpN;
      wt->posn_smp_idx = posn_smp_idx;
      
      unsigned allocSmpCnt = p->padSmpN + wt->aN + p->padSmpN;
      p->allocAudioBytesN += allocSmpCnt * sizeof(sample_t);            
      
      // allocate the wavetable audio buffer
      wt->aV = mem::allocZ<sample_t>( allocSmpCnt );

      // fill the wavetable from the audio file
      vop::copy(wt->aV+p->padSmpN, aV + posn_smp_idx, wt->aN);
      
      // fill the wavetable prefix
      vop::copy(wt->aV, wt->aV + p->padSmpN + (wt->aN-p->padSmpN), p->padSmpN );

      // fill the wavetable suffix
      vop::copy(wt->aV + p->padSmpN + wt->aN, wt->aV + p->padSmpN, p->padSmpN );

    }

    rc_t _create_instr_pv_map( instr_t* instr )
    {
      rc_t rc = kOkRC;
      
      // each row contains the velocities for a given pitch
      instr->pvM = mem::allocZ<multi_ch_wt_seq_t*>(midi::kMidiNoteCnt * midi::kMidiVelCnt );
      
      for(unsigned i=0; i<instr->pitchN; ++i)
      {
        unsigned vel0 = 0;
        
        for(unsigned j=0; j<instr->pitchA[i].velN; ++j)
        {
          unsigned pitch = instr->pitchA[i].midi_pitch;
          unsigned vel   = instr->pitchA[i].velA[j].vel;

          if( pitch >= midi::kMidiNoteCnt )
            rc = cwLogError(kInvalidArgRC,"An invalid pitch value (%i) was encounted.",pitch);
          
          if( vel >= midi::kMidiVelCnt )
            rc = cwLogError(kInvalidArgRC,"An invalid velocity value (%i) was encountered.",vel);

          multi_ch_wt_seq_t* mcs = &instr->pitchA[i].velA[j].mc_seq;

          // if there is a gap between vel0 and vel
          if( vel0 > 0 )
          {
            // find the center of the gap
            unsigned vel_c = vel0 + (vel-vel0)/2;

            // vel0:vel_c = mcs0
            for(unsigned v=vel0+1; v<=vel_c; ++v)
            {
              instr->pvM[(v*midi::kMidiNoteCnt) + pitch ] = instr->pvM[(vel0*midi::kMidiNoteCnt) + pitch ];
            }

            // vel_c+1:vel-1 = mcs
            for(unsigned v=vel_c+1; v<vel; ++v)
            {
              instr->pvM[(v*midi::kMidiNoteCnt) + pitch ] = mcs;
            }
          }
          
          instr->pvM[(vel*midi::kMidiNoteCnt) + pitch ] = mcs;
          vel0 = vel;          
        }
      }

      if( rc != kOkRC )
        rc = cwLogError(rc,"Pitch-velocy map creation failed on instrument:'%s'.",cwStringNullGuard(instr->label));
      
      return rc;
    }
  }
}


cw::rc_t cw::wt_bank::create( handle_t& hRef, unsigned padSmpN, const char* instr_json_fname )
{
  rc_t rc = kOkRC;

  if(destroy(hRef) != kOkRC )
    return rc;

  wt_bank_t* p = mem::allocZ<wt_bank_t>();
  p->padSmpN = padSmpN;

  hRef.set(p);

  if( instr_json_fname != nullptr )
    if((rc = load(hRef,instr_json_fname)) != kOkRC )
    {      
      hRef.clear();
    }
  

  if(rc != kOkRC )
  {
    rc = cwLogError(rc,"WT Bank create failed.");
    _destroy(p);
  }
  
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
                          "bsi",vel->bsi,
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
        
        wts->wtN = wtL->child_count() + 1;
        wts->wtA = mem::allocZ<wt_t>( wts->wtN );
        
        for(unsigned wti=0; wti<wts->wtN-1; ++wti)
        {
          const object_t* wtR  = wtL->child_ele(wti);
          wt_t*           wt   = wts->wtA + wti + 1;
          unsigned        wtbi = kInvalidIdx;
          unsigned        wtei = kInvalidIdx;
          double          rms  = 0;
          
          if((rc = wtR->getv("wtbi",wtbi,
                             "wtei",wtei,
                             "rms",rms,
                             "est_hz",wt->hz)) != kOkRC )
          {
            rc = cwLogError(rc,"Instrument file syntax error in wavetable record at index %i, channel index:%i, velocity index:%i midi pitch:%i.",wti,ch_idx,j,pitch->midi_pitch);
            goto errLabel;
          }

          // if this is the first looping wave table then insert the attack wave table before it
          if( wti==0 )
          {
            _alloc_wt(p,wts->wtA,dsp::wt_osc::kOneShotWtTId,srate,abuf.ch_buf[ch_idx], vel->bsi, wtbi-vel->bsi,hz,0);            
          }

          _alloc_wt(p,wt,dsp::wt_osc::kLoopWtTId,srate,abuf.ch_buf[ch_idx],wtbi,wtei-wtbi,hz,rms);
        }
      }
    }    
  }

  if((rc = _create_instr_pv_map( instr )) != kOkRC )
    goto errLabel;

  p->instrA = mem::resizeZ<instr_t*>( p->instrA, p->instrN+1 );
  p->instrA[ p->instrN++ ] = instr;
    
errLabel:
  if(rc != kOkRC )
  {
    if( instr != nullptr )
      _destroy_instr(instr);
    rc = cwLogError(rc,"Wave table bank load failed on '%s'.",cwStringNullGuard(instr_json_fname));
  }

  _audio_buf_free(abuf);

  if( f != nullptr )
    f->free();
  
  return rc;
}

void cw::wt_bank::report( handle_t h )
{
  wt_bank_t* p = _handleToPtr(h);
  
  for(unsigned instr_idx=0; instr_idx<p->instrN; ++instr_idx)
  {
    instr_t* instr = p->instrA[instr_idx];
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

          fl = false;
          
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
          {
            cwLogPrint("\n");           
          }
        }
        
      }
    }
  }
  
  
}

unsigned cw::wt_bank::instr_count( handle_t h )
{
  wt_bank_t* p = _handleToPtr(h);
  return p->instrN;
}

unsigned cw::wt_bank::instr_index( handle_t h, const char* instr_label )
{
  wt_bank_t* p = _handleToPtr(h);
  
  for(unsigned i=0; i<p->instrN; ++i)
    if( textIsEqual(p->instrA[i]->label,instr_label) )
      return i;
  
  return kInvalidIdx;
}

cw::rc_t cw::wt_bank::instr_pitch_velocities( handle_t h, unsigned instr_idx, unsigned pitch, unsigned* velA, unsigned velCnt, unsigned& velCnt_Ref )
{
  rc_t       rc = kOkRC;
  wt_bank_t* p  = _handleToPtr(h);

  velCnt_Ref = 0;
  
  if( instr_idx > p->instrN || pitch >= midi::kMidiNoteCnt )
  {
    rc = cwLogError(kInvalidArgRC,"Invalid wave table request : instr:%i (instrN:%i) pitch:%i.",instr_idx,p->instrN,pitch);
    goto errLabel;
  }

  for(unsigned i=0; i<p->instrA[instr_idx]->pitchN; ++i)
    if( p->instrA[instr_idx]->pitchA[i].midi_pitch == pitch )
    {
      if( p->instrA[instr_idx]->pitchA[i].velN > velCnt )
      {
        rc = cwLogError(kBufTooSmallRC,"The velocity buffer is too small (%i>%i) for instr:%i pitch:%i.",p->instrA[instr_idx]->pitchA[i].velN,velCnt,instr_idx,pitch);
        goto errLabel;
      }
      
      for(unsigned j=0; j<p->instrA[instr_idx]->pitchA[i].velN; ++j)
        velA[j] = p->instrA[instr_idx]->pitchA[i].velA[j].vel;

      velCnt_Ref = p->instrA[instr_idx]->pitchA[i].velN;
      break;
    }

  if( velCnt_Ref==0)
  {
    rc = cwLogError(kInvalidArgRC,"No sampled wave tables exist for instr:%i pitch:%i.",instr_idx,pitch);
    goto errLabel;
  }

errLabel:
  return rc;
}



cw::rc_t cw::wt_bank::get_wave_table( handle_t h, unsigned instr_idx, unsigned pitch, unsigned vel, cw::wt_bank::multi_ch_wt_seq_t const * & mcs_Ref )
{
  rc_t rc = kOkRC;
  wt_bank_t* p = _handleToPtr(h);

  mcs_Ref = nullptr;
  
  if( instr_idx > p->instrN || pitch >= midi::kMidiNoteCnt || vel >= midi::kMidiVelCnt )
  {
    rc = cwLogError(kInvalidArgRC,"Invalid wave table request : instr:%i (instrN:%i) pitch:%i vel:%i.",instr_idx,p->instrN,pitch,vel);
    goto errLabel;
  }

  
  mcs_Ref = p->instrA[instr_idx]->pvM[(vel*midi::kMidiNoteCnt) + pitch ];

  if( mcs_Ref == nullptr )
  {
    rc = cwLogError(kInvalidStateRC,"The wave table request : instr:%i (instrN:%i) pitch:%i vel:%i is not populated.",instr_idx,p->instrN,pitch,vel);
    goto errLabel;
  }

errLabel:

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
