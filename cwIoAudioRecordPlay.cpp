#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwFile.h"
#include "cwTime.h"
#include "cwIo.h"
#include "cwIoAudioRecordPlay.h"
#include "cwAudioFile.h"

namespace cw
{
  namespace audio_record_play
  {
    typedef io::sample_t sample_t;
    
    typedef struct am_audio_str
    {
      time::spec_t         timestamp;
      unsigned             chCnt;
      unsigned             dspFrameCnt;
      struct am_audio_str* link;
      sample_t         audioBuf[]; // [[ch0:dspFramCnt][ch1:dspFrmCnt]] total: chCnt*dspFrameCnt samples
    } am_audio_t;

    typedef struct audio_record_play_str
    {
      io::handle_t       ioH;
      
      am_audio_t*    audioBeg;       // first in a chain of am_audio_t audio buffers
      am_audio_t*    audioEnd;       // last in a chain of am_audio_t audio buffers
        
      am_audio_t*    audioFile;      // one large audio buffer holding the last loaded audio file

      double         srate;
      unsigned       curFrameCnt;
      unsigned       curFrameIdx;
      bool           recordFl;
      bool           startedFl;

      bool           mute_fl;
      
      unsigned*      audioInChMapA;
      unsigned       audioInChMapN;
      unsigned*      audioOutChMapA;
      unsigned       audioOutChMapN;
      
    } audio_record_play_t;

    audio_record_play_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,audio_record_play_t>(h); }

    void _am_audio_free_list( audio_record_play_t* p )
    {
      for(am_audio_t* a=p->audioBeg; a!=nullptr; )
      {
        am_audio_t* tmp = a->link;
        mem::release(a);
        a = tmp;
      }

      if( p->audioFile == p->audioBeg )
        p->audioFile = nullptr;
      else
        mem::release(p->audioFile);
      
      p->audioBeg = nullptr;
      p->audioEnd = nullptr;
      p->curFrameIdx = 0;
      p->curFrameCnt = 0;
      
    }
    
    am_audio_t* _am_audio_alloc( unsigned dspFrameCnt, unsigned chCnt )
    {
      unsigned    sample_byte_cnt = chCnt * dspFrameCnt * sizeof(sample_t);        
      void*       vp              = mem::allocZ<uint8_t>( sizeof(am_audio_t) + sample_byte_cnt );
      am_audio_t* a               = (am_audio_t*)vp;
        
      a->chCnt       = chCnt;
      a->dspFrameCnt = dspFrameCnt;
        
      return a;
    }

    
    rc_t _destroy( audio_record_play_t* p )
    {
      _am_audio_free_list(p);
      p->audioInChMapN = 0;
      p->audioOutChMapN = 0;
      mem::release(p->audioInChMapA);
      mem::release(p->audioOutChMapA);
      mem::release(p->audioFile);
      mem::release(p);
      return kOkRC;
    }

    rc_t _parseCfg(audio_record_play_t* p, const object_t& cfg )
    {
      rc_t rc = kOkRC;

      const object_t* audioInChMapL  = nullptr;
      const object_t* audioOutChMapL = nullptr;

      if((rc = cfg.getv_opt("audio_in_ch_map",  audioInChMapL,
                            "audio_out_ch_map", audioOutChMapL)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse cfg failed.");
        goto errLabel;          
      }

      if( audioInChMapL != nullptr )
      {
        p->audioInChMapN = audioInChMapL->child_count();
        p->audioInChMapA = mem::allocZ<unsigned>( p->audioInChMapN );

        for(unsigned i=0; i<p->audioInChMapN; ++i)
          audioInChMapL->child_ele(i)->value(p->audioInChMapA[i]);
      }


      if( audioOutChMapL != nullptr )
      {
        p->audioOutChMapN = audioOutChMapL->child_count();
        p->audioOutChMapA = mem::allocZ<unsigned>( p->audioOutChMapN );

        for(unsigned i=0; i<p->audioOutChMapN; ++i)
          audioOutChMapL->child_ele(i)->value(p->audioOutChMapA[i]);
      }
      

    errLabel:
      return rc;
    }


    am_audio_t* _am_audio_from_sample_index( audio_record_play_t* p, unsigned sample_idx, unsigned& sample_offs_ref )
    {
      unsigned    n = 0;
      am_audio_t* a = p->audioBeg;

      if( p->audioBeg == nullptr )
        return nullptr;
        
      for(; a!=nullptr; a=a->link)
      {
        // if sample index falls inside this buffer
        if( n <= sample_idx && sample_idx < n + a->dspFrameCnt ) 
        {
          sample_offs_ref = sample_idx - n; // store the offset into this buffer of 'sample_idx'
          return a;
        }

        n += a->dspFrameCnt;

      }
          
      return nullptr;
    }

    void _audio_record( audio_record_play_t* p, const io::audio_msg_t& asrc )
    {
      unsigned chCnt = p->audioInChMapN==0 ? asrc.iBufChCnt : p->audioInChMapN;
      am_audio_t* a  = _am_audio_alloc(asrc.dspFrameCnt,chCnt);

      chCnt = std::min( chCnt, asrc.iBufChCnt );
        
      for(unsigned chIdx=0; chIdx<chCnt; ++chIdx)
      {
        unsigned srcChIdx = p->audioInChMapA == nullptr ? chIdx : p->audioInChMapA[chIdx];

        if( srcChIdx >= asrc.iBufChCnt )
          cwLogError(kInvalidArgRC,"Invalid input channel map index:%i >= %i.",srcChIdx,asrc.iBufChCnt);
        else
          memcpy(a->audioBuf + chIdx*asrc.dspFrameCnt, asrc.iBufArray[ srcChIdx ], asrc.dspFrameCnt * sizeof(sample_t));
      }
      
      a->chCnt        = chCnt;
      a->dspFrameCnt  = asrc.dspFrameCnt;

      if( p->audioEnd != nullptr )
        p->audioEnd->link = a;   // link the new audio record to the end of the audio sample buffer chain
      p->audioEnd       = a;     // make the new audio record the last ele. of the chain

      // if this is the first ele of the chain
      if( p->audioBeg == nullptr )
      {
        p->audioBeg    = a;
        p->srate       = asrc.srate;
        p->curFrameIdx = 0;
        p->curFrameCnt = 0;
      }

      p->curFrameIdx += asrc.dspFrameCnt;
      p->curFrameCnt += asrc.dspFrameCnt;
      
    }

    void _audio_play( audio_record_play_t* p, io::audio_msg_t& adst )
    {
      unsigned adst_idx = 0;

      if( !p->mute_fl )
      {
        while(adst_idx < adst.dspFrameCnt)
        {
          am_audio_t* a;
          unsigned sample_offs = 0;
          if((a = _am_audio_from_sample_index(p, p->curFrameIdx, sample_offs )) == nullptr )
            break;

          unsigned n  = std::min(a->dspFrameCnt - sample_offs, adst.dspFrameCnt );


          // TODO: Verify that this is correct - it looks like sample_offs should have to be incremented
        
          for(unsigned i=0; i<a->chCnt; ++i)
          {
            unsigned dstChIdx = p->audioOutChMapA != nullptr && i < p->audioOutChMapN ? p->audioOutChMapA[i] : i;

            if( dstChIdx < adst.oBufChCnt )
              memcpy( adst.oBufArray[ dstChIdx ] + adst_idx, a->audioBuf + sample_offs, n * sizeof(sample_t));
          }

          p->curFrameIdx += n;
          adst_idx       += n;
        }
      }
      
      // TODO: zero unused channels

      if( adst_idx < adst.dspFrameCnt )
        for(unsigned i=0; i<adst.oBufChCnt; ++i)
          memset( adst.oBufArray[i] + adst_idx, 0, (adst.dspFrameCnt - adst_idx) * sizeof(sample_t));
    }
    
    void _audio_through( audio_record_play_t* p, io::audio_msg_t& m )
    {
      unsigned chN     = std::min(m.iBufChCnt,m.oBufChCnt);
      unsigned byteCnt = m.dspFrameCnt * sizeof(sample_t);

      // Copy the input to the output
      for(unsigned i=0; i<chN; ++i)
        if( m.oBufArray[i] != NULL )
        {      
          // the input channel is not disabled
          if( m.iBufArray[i] != NULL )
          {
            for(unsigned j=0; j<m.dspFrameCnt; ++j )
              m.oBufArray[i][j] =  m.iBufArray[i][j];
          }
          else
          {
            // the input channel is disabled but the output is not - so fill the output with zeros
            memset(m.oBufArray[i], 0, byteCnt);
          }
        }
    }
      
    rc_t _audio_write_as_wav( audio_record_play_t* p, const char* fn )
    {
      rc_t                rc       = kOkRC;
      unsigned            frameCnt = 0;
      audiofile::handle_t afH;

      // if there is no audio to write
      if( p->audioBeg == nullptr )
        return rc;

      // create an audio file
      if((rc = audiofile::create( afH, fn, p->srate, 0, p->audioBeg->chCnt )) != kOkRC )
      {
        cwLogError(rc,"Audio file create failed.");
        goto errLabel;
      }

      // write each buffer 
      for(am_audio_t* a=p->audioBeg; a!=nullptr; a=a->link)
      {
        float* chBufArray[ a->chCnt ];
        for(unsigned i=0; i<a->chCnt; ++i)
          chBufArray[i] = a->audioBuf + (i*a->dspFrameCnt);
            
        if((rc = writeFloat( afH, a->dspFrameCnt, a->chCnt, chBufArray )) != kOkRC )
        {
          cwLogError(rc,"An error occurred while writing and audio buffer.");
          goto errLabel;
        }

        frameCnt += a->dspFrameCnt;
        
      }
        
    errLabel:

      // close the audio file
      if((rc = audiofile::close(afH)) != kOkRC )
      {
        cwLogError(rc,"Audio file close failed.");
        goto errLabel;
      }

      double secs = p->srate==0 ? 0 : (double)frameCnt/p->srate;

      cwLogInfo("Saved %f seconds of audio to %s.", secs, fn);
        
      return rc;
    }

    rc_t _audio_write_buffer_times( audio_record_play_t* p, const char* fn )
    {
      rc_t           rc = kOkRC;
      am_audio_t*    a0 = p->audioBeg;
      file::handle_t fH;
        
      // if there is no audio to write
      if( p->audioBeg == nullptr )
        return rc;

      // create the file
      if((rc = file::open(fH,fn,file::kWriteFl)) != kOkRC )
      {
        cwLogError(rc,"Create audio buffer time file failed.");
        goto errLabel;
      }

      file::print(fH,"{ [\n");
        
      // write each buffer
      for(am_audio_t* a=p->audioBeg; a!=nullptr; a=a->link)
      {
        unsigned elapsed_us = time::elapsedMicros( a0->timestamp, a->timestamp );
        file::printf(fH,"{ elapsed_us:%i chCnt:%i frameCnt:%i }\n", elapsed_us, a->chCnt, a->dspFrameCnt );
        a0 = a;
      }

      file::print(fH,"] }\n");

      // close the file
      if((rc = file::close(fH)) != kOkRC )
      {
        cwLogError(rc,"Close the audio buffer time file.");
        goto errLabel;
      }

    errLabel:
        
      return rc;        
    }

    rc_t _audio_read( audio_record_play_t* p, const char* fn )
    {
      rc_t                 rc = kOkRC;
      audiofile::handle_t  afH;
      audiofile::info_t    af_info;        
      am_audio_t*          am_audioFile;
      
      if((rc = audiofile::open(afH, fn, &af_info)) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"Audio file '%s' open failed.",fn);
        goto errLabel;         
      }
        
      if((am_audioFile = _am_audio_alloc(af_info.frameCnt,af_info.chCnt)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"Allocate audio buffer (%i samples) failed.",af_info.frameCnt*af_info.chCnt);
        goto errLabel;
      }
      else
      {
        unsigned audioFrameCnt = 0;
        float* chArray[ af_info.chCnt ];
          
        for(unsigned i=0; i<af_info.chCnt; ++i)
          chArray[i] = am_audioFile->audioBuf + (i*af_info.frameCnt);
          
        if((rc = audiofile::readFloat(afH, af_info.frameCnt, 0, af_info.chCnt, chArray, &audioFrameCnt)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"Audio file read failed.");
          goto errLabel;
        }

        
        _am_audio_free_list(p);
        
        p->audioFile   = am_audioFile;
        p->audioBeg    = am_audioFile;
        p->audioEnd    = am_audioFile;
        p->srate       = af_info.srate;
        p->curFrameCnt = af_info.frameCnt;
        p->curFrameIdx = 0;

        double secs = p->srate==0 ? 0 : p->curFrameCnt / p->srate;
        cwLogInfo("Audio loaded: srate:%f secs:%f file:%s", p->srate, secs, cwStringNullGuard(fn));
        
      }
        
    errLabel:
      if((rc = audiofile::close(afH)) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"Audio file close failed.");
        goto errLabel;
      }
        
      return rc;
    }      

    rc_t _audio_callback( audio_record_play_t* p, io::audio_msg_t& m )
    {
      
      rc_t rc = kOkRC;

      if( p->startedFl )
      {
        if( p->recordFl )
        {
          if( m.iBufChCnt > 0 )
            _audio_record(p,m);            
        }
        else
        {
          if( m.oBufChCnt > 0 )
          {
            _audio_play(p,m);
          }
        }
      }
      else
      {
        for(unsigned i=0; i<m.oBufChCnt; ++i)
          memset( m.oBufArray[i], 0, m.dspFrameCnt * sizeof(sample_t)); 
      }
      
      return rc;
    }
  }
}



cw::rc_t cw::audio_record_play::create( handle_t& hRef, io::handle_t ioH, const object_t& cfg )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  audio_record_play_t* p = mem::allocZ<audio_record_play_t>();

  if((rc = _parseCfg(p,cfg)) != kOkRC )
    return rc;
  
  p->ioH = ioH;

  hRef.set(p);
  
  return rc;
}

cw::rc_t cw::audio_record_play::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if( !hRef.isValid() )
    return rc;

  audio_record_play_t* p = _handleToPtr(hRef);
  
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;
}
   
cw::rc_t cw::audio_record_play::start( handle_t h )
{
  rc_t                 rc = kOkRC;
  audio_record_play_t* p  = _handleToPtr(h);
  
  p->curFrameIdx = 0;
  p->startedFl   = true;
  
  if( p->recordFl )
    _am_audio_free_list(p);
  
  return rc;
}

cw::rc_t cw::audio_record_play::stop( handle_t h )
{
  rc_t                 rc = kOkRC;
  audio_record_play_t* p  = _handleToPtr(h);
  p->startedFl = false;
  return rc;
}

bool cw::audio_record_play::is_started( handle_t h )
{
  audio_record_play_t* p  = _handleToPtr(h);
  return p->startedFl;
}


cw::rc_t cw::audio_record_play::rewind( handle_t h )
{
  rc_t                 rc = kOkRC;
  audio_record_play_t* p  = _handleToPtr(h);
  p->curFrameIdx = 0;
  return rc;
}

cw::rc_t cw::audio_record_play::clear( handle_t h )
{
  rc_t                 rc = kOkRC;
  audio_record_play_t* p  = _handleToPtr(h);
  _am_audio_free_list(p);
  return rc;
}

cw::rc_t cw::audio_record_play::set_record_state( handle_t h, bool record_fl )
{
  rc_t                 rc = kOkRC;
  audio_record_play_t* p  = _handleToPtr(h);
  p->recordFl = true;
  return rc;
}

bool cw::audio_record_play::record_state( handle_t h )
{
  rc_t                 rc = kOkRC;
  audio_record_play_t* p  = _handleToPtr(h);
  return p->recordFl;
  return rc;
}

cw::rc_t cw::audio_record_play::set_mute_state( handle_t h, bool mute_fl )
{
  rc_t                 rc = kOkRC;
  audio_record_play_t* p  = _handleToPtr(h);
  p->mute_fl = true;
  return rc;
}

bool cw::audio_record_play::mute_state( handle_t h )
{
  rc_t                 rc = kOkRC;
  audio_record_play_t* p  = _handleToPtr(h);
  return p->mute_fl;
  return rc;
}

cw::rc_t cw::audio_record_play::save( handle_t h, const char* fn )
{
  audio_record_play_t* p  = _handleToPtr(h);
  return _audio_write_as_wav(p,fn);
}

cw::rc_t cw::audio_record_play::open( handle_t h, const char* fn )
{
  rc_t                 rc = kOkRC;
  audio_record_play_t* p  = _handleToPtr(h);
  _audio_read(p,fn);
  return rc;
}

double cw::audio_record_play::duration_seconds( handle_t h )
{
  audio_record_play_t* p  = _handleToPtr(h);  
  return p->srate == 0 ? 0.0 : (double)p->curFrameCnt / p->srate;
}

double cw::audio_record_play::current_loc_seconds( handle_t h )
{
  audio_record_play_t* p  = _handleToPtr(h);
  if( p->srate == 0 )
    return 0;
  
  return (double)(p->startedFl ? p->curFrameIdx : p->curFrameCnt)/ p->srate;
}

cw::rc_t cw::audio_record_play::exec( handle_t h, const io::msg_t& msg )
{
  rc_t                 rc = kOkRC;
  audio_record_play_t* p  = _handleToPtr(h);
  
  switch( msg.tid )
  {
  case io::kAudioTId:
    if( msg.u.audio != nullptr )
      _audio_callback(p,*msg.u.audio);
    break;

  default:
    rc = kOkRC;
  
  }

  return rc;
}
