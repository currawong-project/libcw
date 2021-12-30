#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwFile.h"
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwMidiFile.h"
#include "cwUiDecls.h"
#include "cwIo.h"
#include "cwIoMidiRecordPlay.h"

#define TIMER_LABEL "midi_record_play_timer"

namespace cw
{
  namespace midi_record_play
  {
    typedef struct am_midi_msg_str
    {
      unsigned      devIdx;
      unsigned      portIdx;
      unsigned      microsec;

      unsigned      id;
      time::spec_t  timestamp;
      uint8_t       ch;
      uint8_t       status;
      uint8_t       d0;
      uint8_t       d1;
        
    } am_midi_msg_t;
  
    typedef struct midi_record_play_str
    {
      io::handle_t   ioH;
      
      am_midi_msg_t* msgArray;
      unsigned       msgArrayN;
      unsigned       msgArrayInIdx;
      unsigned       msgArrayOutIdx;
      unsigned       midi_timer_period_micro_sec;

      char*          midiOutDevLabel;
      char*          midiOutPortLabel;
      unsigned       midiOutDevIdx;
      unsigned       midiOutPortIdx;

      bool           startedFl;
      bool           recordFl;
      bool           thruFl;
      
      time::spec_t   play_time;                
      time::spec_t   start_time;
      time::spec_t   end_play_event_timestamp;

      bool pedalFl;

      event_callback_t cb;
      void*            cb_arg;
    
    } midi_record_play_t;

    enum
    {
      kMidiRecordPlayTimerId
    };

    midi_record_play_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,midi_record_play_t>(h); }

    rc_t _destroy( midi_record_play_t* p )
    {
      rc_t rc = kOkRC;
      unsigned timerIdx;
      
      if((timerIdx = io::timerLabelToIndex( p->ioH, TIMER_LABEL )) != kInvalidIdx )
        io::timerDestroy( p->ioH, timerIdx);
      
      mem::release(p->msgArray);
      mem::release(p->midiOutDevLabel);
      mem::release(p->midiOutPortLabel);
      mem::release(p);
    
      return rc;
    }

    rc_t _parseCfg(midi_record_play_t* p, const object_t& cfg )
    {
      rc_t rc = kOkRC;
        
      if((rc = cfg.getv(
                        "max_midi_msg_count",          p->msgArrayN,
                        "midi_timer_period_micro_sec", p->midi_timer_period_micro_sec,
                        "midi_out_device",             p->midiOutDevLabel,
                        "midi_out_port",               p->midiOutPortLabel)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"MIDI record play configuration parse failed.");
        goto errLabel;
      }

      // allocate the MIDI msg buffer
      p->msgArray         = mem::allocZ<am_midi_msg_t>( p->msgArrayN );
      p->midiOutDevLabel  = mem::duplStr( p->midiOutDevLabel);
      p->midiOutPortLabel = mem::duplStr( p->midiOutPortLabel);

    errLabel:
      return rc;
    }

    rc_t _stop( midi_record_play_t* p );
    
    rc_t _event_callback( midi_record_play_t* p, unsigned id, const time::spec_t timestamp, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 )
    {
      rc_t rc = kOkRC;
      
      if( !time::isZero(p->end_play_event_timestamp) && time::isGTE(timestamp,p->end_play_event_timestamp))
      {
        rc = _stop(p);
      }
      else
      {
        io::midiDeviceSend( p->ioH, p->midiOutDevIdx, p->midiOutPortIdx, status + ch, d0, d1 );

        if( p->cb )
          p->cb( p->cb_arg, id, timestamp, ch, status, d0, d1 );
        
      }
      
      return rc;
    }

    rc_t _transmit_msg( midi_record_play_t* p, const am_midi_msg_t* am )
    {
      return _event_callback( p, am->id, am->timestamp, am->ch, am->status, am->d0, am->d1 );
    }

    rc_t _transmit_ctl(  midi_record_play_t* p, unsigned ch, unsigned ctlId, unsigned ctlVal )
    {
      time::spec_t ts = {0};
      return _event_callback( p, kInvalidId, ts, ch, midi::kCtlMdId, ctlId, ctlVal );
    }
    
    rc_t _transmit_pedal( midi_record_play_t* p, unsigned ch, unsigned pedalCtlId, bool pedalDownFl )
    {
      return _transmit_ctl( p, ch, pedalCtlId, pedalDownFl ? 127 : 0);
    }



    void _set_midi_msg_next_index( midi_record_play_t* p, unsigned next_idx )
    {
      p->msgArrayInIdx = next_idx;
        
      //io::uiSendValue( p->ioH, kInvalidId, uiFindElementUuId(p->ioH,kMsgCntId), p->midiMsgArrayInIdx );

    }

    void _set_midi_msg_next_play_index(midi_record_play_t* p, unsigned next_idx)
    {
      p->msgArrayOutIdx = next_idx;
    }


    // Read the am_midi_msg_t records from a file written by _midi_write()
    // If msgArrayCntRef==0 and msgArrayRef==NULL then an array will be allocated and it is up
    // to the caller to release it, otherwise the msgArrayCntRef should be set to the count
    // of available records in msgArrayRef[].  Note the if there are more records in the file
    // than there are record in msgArrayRef[] then a warning will be issued and only
    // msgArrayCntRef records will be returned.
    cw::rc_t _am_file_read( const char* fn, unsigned& msgArrayCntRef, am_midi_msg_t*& msgArrayRef )
    {
      rc_t           rc = kOkRC;
      unsigned       n  = 0;
      file::handle_t fH;
        
      if((rc = file::open(fH,fn,file::kReadFl)) != kOkRC )
      {
        rc = cwLogError(kOpenFailRC,"Unable to locate the AM file: '%s'.", fn );
        goto errLabel;
      }

      if((rc = file::read(fH,n)) != kOkRC )
      {
        rc = cwLogError(kReadFailRC,"Header read failed on Audio-MIDI file: '%s'.", fn );
        goto errLabel;
      }

      if( msgArrayCntRef == 0 || msgArrayRef == nullptr )
      {
        msgArrayRef = mem::allocZ<am_midi_msg_t>(n);        
      }
      else
      {
        if( n > msgArrayCntRef )
        {
          cwLogWarning("The count of message in Audio-MIDI file '%s' reduced from %i to %i.", fn, n, msgArrayCntRef );
          n = msgArrayCntRef;
        }
      }
      
      if((rc = file::read(fH,msgArrayRef,n*sizeof(am_midi_msg_t))) != kOkRC )
      {
        rc = cwLogError(kReadFailRC,"Data read failed on Audio-MIDI file: '%s'.", fn );
        goto errLabel;
      }

      msgArrayCntRef = n;

    errLabel:
        
      return rc;        
    }
    
    rc_t _midi_read( midi_record_play_t* p, const char* fn )
    {
      rc_t           rc = kOkRC;
      unsigned       n  = 0;
      file::handle_t fH;
        
      if((rc = file::open(fH,fn,file::kReadFl)) != kOkRC )
      {
        rc = cwLogError(kOpenFailRC,"Unable to locate the file: '%s'.", fn );
        goto errLabel;
      }

      if((rc = file::read(fH,n)) != kOkRC )
      {
        rc = cwLogError(kReadFailRC,"Header read failed on Audio-MIDI file: '%s'.", fn );
        goto errLabel;
      }

      if( n > p->msgArrayN )
      {
        cwLogWarning("The count of message in Audio-MIDI file '%s' reduced from %i to %i.", fn, n, p->msgArrayN );
        n = p->msgArrayN;
      }

      if((rc = file::read(fH,p->msgArray,n*sizeof(am_midi_msg_t))) != kOkRC )
      {
        rc = cwLogError(kReadFailRC,"Data read failed on Audio-MIDI file: '%s'.", fn );
        goto errLabel;
      }

      _set_midi_msg_next_index(p, n );

      cwLogInfo("Read %i from '%s'.",n,fn);

    errLabel:
        
      return rc;        
    }
      
    rc_t _midi_write( midi_record_play_t* p, const char* fn )
    {
      rc_t           rc = kOkRC;
      file::handle_t fH;

      if( p->msgArrayInIdx == 0 )
      {
        cwLogWarning("Nothing to write.");
        return rc;
      }

      // open the file
      if((rc = file::open(fH,fn,file::kWriteFl)) != kOkRC )
      {
        rc = cwLogError(kOpenFailRC,"Unable to create the file '%s'.",cwStringNullGuard(fn));
        goto errLabel;
      }

      // write the file header
      if((rc = write(fH,p->msgArrayInIdx)) != kOkRC )
      {
        rc = cwLogError(kWriteFailRC,"Header write to '%s' failed.",cwStringNullGuard(fn));
        goto errLabel;          
      }

      // write the file data
      if((rc = write(fH,p->msgArray,sizeof(am_midi_msg_t)*p->msgArrayInIdx)) != kOkRC )
      {
        rc = cwLogError(kWriteFailRC,"Data write to '%s' failed.",cwStringNullGuard(fn));
        goto errLabel;                    
      }

      // update UI msg count
      //io::uiSendValue( p->ioH, kInvalidId, uiFindElementUuId(p->ioH,kMsgCntId), p->msgArrayInIdx );

      file::close(fH);

      cwLogInfo("Saved %i events to '%s'.", p->msgArrayInIdx, fn );
        
    errLabel:
      return rc;
    }

    rc_t _midi_file_write( const char* fn, const am_midi_msg_t* msgArray, unsigned msgArrayCnt )
    {
      rc_t                 rc                 = kOkRC;
      const unsigned       midiFileTrackCnt   = 1;
      const unsigned       midiFileTicksPerQN = 192;
      const unsigned       midiFileTempoBpm   = 120;
      const unsigned       midiFileTrkIdx     = 0;
      file::handle_t       fH;
      midi::file::handle_t mfH;
      time::spec_t         t0;      


      if( msgArrayCnt == 0 )
      {
        cwLogWarning("Nothing to write.");
        return rc;
      }

      if((rc = midi::file::create( mfH, midiFileTrackCnt,  midiFileTicksPerQN )) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI file create failed. File:'%s'", cwStringNullGuard(fn));
        goto errLabel;                  
      }

      if((rc = midi::file::insertTrackTempoMsg( mfH, midiFileTrkIdx, 0, midiFileTempoBpm )) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI file tempo message insert failed. File:'%s'", cwStringNullGuard(fn));
        goto errLabel;                  
      }

      t0 = msgArray[0].timestamp;
      
      for(unsigned i=0; i<msgArrayCnt; ++i)
      {

        double secs = time::elapsedMicros( t0, msgArray[i].timestamp ) / 1000000.0;
        unsigned atick = secs * midiFileTicksPerQN * midiFileTempoBpm / 60.0;
        
        if((rc = insertTrackChMsg( mfH, midiFileTrkIdx, atick, msgArray[i].ch + msgArray[i].status, msgArray[i].d0, msgArray[i].d1 )) != kOkRC )
        {
          rc = cwLogError(rc,"MIDI file message insert failed. File: '%s'.",cwStringNullGuard(fn));
          goto errLabel;                  
          
        }

      }
      
      if((rc = midi::file::write( mfH, fn )) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI file write failed on '%s'.",cwStringNullGuard(fn));
        goto errLabel;                  
      }

      if((rc = midi::file::close( mfH )) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI file close failed on '%s'.",cwStringNullGuard(fn));
        goto errLabel;                  
      }

    errLabel:
      return rc;
    }

    
    void _print_midi_msg( const am_midi_msg_t* mm )
    {
      printf("%i %i : %10i : %2i 0x%02x 0x%02x 0x%02x\n", mm->devIdx, mm->portIdx, mm->microsec, mm->ch, mm->status, mm->d0, mm->d1 );
    }
      
    void _report_midi( midi_record_play_t* p )
    {
      for(unsigned i=0; i<p->msgArrayInIdx; ++i)
      {
        am_midi_msg_t* mm = p->msgArray + i;
        _print_midi_msg(mm);
      }
    }

    rc_t _stop( midi_record_play_t* p )
    {
      rc_t rc = kOkRC;
      
      p->startedFl = false;

      time::spec_t t1;
      time::get(t1);

      if( p->recordFl )
      {

        // set the 'microsec' value for each MIDI msg
        for(unsigned i=0; i<p->msgArrayInIdx; ++i)
        {
          p->msgArray[i].microsec = time::elapsedMicros(p->msgArray[0].timestamp,p->msgArray[i].timestamp);
        }
          
        cwLogInfo("MIDI messages recorded: %i",p->msgArrayInIdx );
          
      }
      else
      {
        io::timerStop( p->ioH, io::timerIdToIndex(p->ioH, kMidiRecordPlayTimerId) );

        // TODO: should work for all channels

        // all notes off
        _transmit_ctl(   p, 0, 121, 0 ); // reset all controllers
        _transmit_ctl(   p, 0, 123, 0 ); // all notes off
        _transmit_ctl(   p, 0,   0, 0 ); // switch to bank 0

        // send pgm change 0
        time::spec_t ts = {0};
        _event_callback( p, kInvalidId, ts, 0, midi::kPgmMdId, 0, 0 );
        
        p->pedalFl = false;

      }

      cwLogInfo("Runtime: %5.2f seconds.", time::elapsedMs(p->start_time,t1)/1000.0 );

      return rc;
    }

    rc_t _midi_receive( midi_record_play_t* p, const io::midi_msg_t& m )
    {
      rc_t                  rc  = kOkRC;
      const midi::packet_t* pkt = m.pkt;

      // for each midi msg
      for(unsigned j=0; j<pkt->msgCnt; ++j)
      {
        // if this is a sys-ex msg
        if( pkt->msgArray == NULL )
        {
        }
        else // this is a triple
        {

          //if( !midi::isPedal(pkt->msgArray[j].status,pkt->msgArray[j].d0) )
          //  printf("0x%x 0x%x 0x%x\n", pkt->msgArray[j].status, pkt->msgArray[j].d0, pkt->msgArray[j].d1 );
          
          if( p->recordFl && p->startedFl )
          {

            // verify that space exists in the record buffer
            if( p->msgArrayInIdx >= p->msgArrayN )
            {
              _stop(p);
              rc = cwLogError(kBufTooSmallRC,"MIDI message record buffer is full. % messages.",p->msgArrayN);
              goto errLabel;
            }
            else
            {
              // copy the msg into the record buffer
              am_midi_msg_t* am = p->msgArray + p->msgArrayInIdx;
              midi::msg_t*   mm = pkt->msgArray + j;

              if( midi::isChStatus(mm->status) )
              {

                am->id        = p->msgArrayInIdx;
                am->devIdx    = pkt->devIdx;
                am->portIdx   = pkt->portIdx;
                am->timestamp = mm->timeStamp;
                am->ch        = mm->status & 0x0f;
                am->status    = mm->status & 0xf0;
                am->d0        = mm->d0;
                am->d1        = mm->d1;

                //printf("st:0x%x ch:%i d0:0x%x d1:0x%x\n",am->status,am->ch,am->d0,am->d1);
              
                p->msgArrayInIdx += 1;

                if( p->thruFl )
                {
                  _transmit_msg( p, am );
                  //io::midiDeviceSend( p->ioH, p->midiOutDevIdx, p->midiOutPortIdx, am->status + am->ch, am->d0, am->d1 );
                }

                // send msg count
                //io::uiSendValue( p->ioH, kInvalidId, uiFindElementUuId(p->ioH,kMsgCntId), p->msgArrayInIdx );                  
              }
            }
          }
        }

        /*
          if( pkt->msgArray == NULL )
          printf("io midi cb: 0x%x ",pkt->sysExMsg[j]);
          else
          {
          if( !_midi_filter(pkt->msgArray + j) )
          printf("io midi cb: %ld %ld 0x%x %i %i\n", pkt->msgArray[j].timeStamp.tv_sec, pkt->msgArray[j].timeStamp.tv_nsec, pkt->msgArray[j].status, pkt->msgArray[j].d0, pkt->msgArray[j].d1);                    
          }
        */
      }

    errLabel:
      return rc;
    }

    rc_t _timer_callback(midi_record_play_t* p, io::timer_msg_t& m)
      {
        rc_t rc = kOkRC;

        // if the MIDI player is started and in 'play' mode and msg remain to be played
        if( p->startedFl && (p->recordFl==false) && (p->msgArrayOutIdx < p->msgArrayInIdx))
        {
          time::spec_t t;
          time::get(t);

          unsigned cur_time_us = time::elapsedMicros(p->play_time,t);

          while( p->msgArray[ p->msgArrayOutIdx ].microsec <= cur_time_us )
          {

            am_midi_msg_t* mm = p->msgArray + p->msgArrayOutIdx;

            //_print_midi_msg(mm);

            bool skipFl = false;

            /*
            // if this is a pedal message
            if( mm->status == midi::kCtlMdId && (mm->d0 == midi::kSustainCtlMdId || mm->d0 == midi::kSostenutoCtlMdId || mm->d0 == midi::kSoftPedalCtlMdId ) )
            {
              // if the pedal is down
              if( p->pedalFl )
              {
                skipFl = mm->d1 > 64;
                p->pedalFl = false;
              }
              else
              {
                skipFl = mm->d1 <= 64;
                p->pedalFl = true;
              }
            }
            */
            
            if( !skipFl )
            {
              _transmit_msg( p, mm );
            }
            _set_midi_msg_next_play_index(p, p->msgArrayOutIdx+1 );

            // if all MIDI messages have been played
            if( p->msgArrayOutIdx >= p->msgArrayInIdx )
            {
              _stop(p);
              break;
            }
          }
        }
        
        return rc;
      }
    
  }
}

cw::rc_t cw::midi_record_play::create( handle_t& hRef, io::handle_t ioH, const object_t& cfg, event_callback_t cb, void* cb_arg )
{
  midi_record_play_t* p = nullptr;
  rc_t rc;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  p = mem::allocZ<midi_record_play_t>();
  
  if((rc = _parseCfg(p,cfg)) != kOkRC )
    goto errLabel;

  p->ioH = ioH;
  p->cb  = cb;
  p->cb_arg = cb_arg;
  
  if((p->midiOutDevIdx = io::midiDeviceIndex(p->ioH,p->midiOutDevLabel)) == kInvalidIdx )
  {
    rc = cwLogError(kInvalidArgRC,"The MIDI output device: '%s' was not found.", cwStringNullGuard(p->midiOutDevLabel) );
    goto errLabel;
  }
        
  if((p->midiOutPortIdx = io::midiDevicePortIndex(p->ioH,p->midiOutDevIdx,false,p->midiOutPortLabel)) == kInvalidIdx )
  {
    rc = cwLogError(kInvalidArgRC,"The MIDI output port: '%s' was not found.", cwStringNullGuard(p->midiOutPortLabel) );
    goto errLabel;
  }
  
  // create the MIDI playback timer
  if((rc = timerCreate( p->ioH, TIMER_LABEL, kMidiRecordPlayTimerId, p->midi_timer_period_micro_sec)) != kOkRC )
  {
    cwLogError(rc,"Audio-MIDI timer create failed.");
    goto errLabel;
  }

 errLabel:
  if( rc == kOkRC )
    hRef.set(p);
  else
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::midi_record_play::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  
  if( !hRef.isValid() )
    return kOkRC;

  midi_record_play_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  
  return rc;
}

cw::rc_t cw::midi_record_play::start( handle_t h, bool rewindFl, const time::spec_t* end_play_event_timestamp )
{
  midi_record_play_t* p = _handleToPtr(h);
  p->startedFl = true;
  p->pedalFl = false;

  // set the end play time
  if( end_play_event_timestamp == nullptr or time::isZero(*end_play_event_timestamp) )
    time::setZero(p->end_play_event_timestamp);
  else
    p->end_play_event_timestamp = *end_play_event_timestamp;
  
  time::get(p->start_time);
        
  if( p->recordFl )
  {
    _set_midi_msg_next_index(p, 0 );
  }
  else
  {
    time::get(p->play_time);
    
    if( rewindFl )
      _set_midi_msg_next_play_index(p,0);
    else
    {
      // Set the begin play time back by the time offset of the current output event.
      // This will cause that event to be played back immediately.
      time::subtractMicros(p->play_time, p->msgArray[ p->msgArrayOutIdx ].microsec );
    }
    
    io::timerStart( p->ioH, io::timerIdToIndex(p->ioH, kMidiRecordPlayTimerId) );
  }

  return kOkRC;
}

cw::rc_t cw::midi_record_play::stop( handle_t h )
{
  midi_record_play_t* p = _handleToPtr(h);
  return _stop(p);
}

bool cw::midi_record_play::is_started( handle_t h )
{
  midi_record_play_t* p  = _handleToPtr(h);
  return p->startedFl;
}


cw::rc_t cw::midi_record_play::clear( handle_t h )
{
  rc_t                rc = kOkRC;
  midi_record_play_t* p  = _handleToPtr(h);
  _set_midi_msg_next_index(p,0);
  return rc;
}

cw::rc_t cw::midi_record_play::set_record_state( handle_t h, bool record_fl )
{
  rc_t                rc = kOkRC;
  midi_record_play_t* p  = _handleToPtr(h);
  p->recordFl = record_fl;
  return rc;  
}

bool cw::midi_record_play::record_state( handle_t h )
{
  midi_record_play_t* p  = _handleToPtr(h);
  return p->recordFl;
}

cw::rc_t cw::midi_record_play::set_thru_state( handle_t h, bool thru_fl )
{
  rc_t                rc = kOkRC;
  midi_record_play_t* p  = _handleToPtr(h);
  p->thruFl = thru_fl;
  return rc;  
}

bool cw::midi_record_play::thru_state( handle_t h )
{
  midi_record_play_t* p  = _handleToPtr(h);
  return p->thruFl;
}

cw::rc_t cw::midi_record_play::save( handle_t h, const char* fn )
{
  midi_record_play_t* p  = _handleToPtr(h);  
  return _midi_write(p,fn);
}

cw::rc_t cw::midi_record_play::open( handle_t h, const char* fn )
{
  midi_record_play_t* p  = _handleToPtr(h);  
  return _midi_read(p,fn);
}

cw::rc_t cw::midi_record_play::load( handle_t h, const midi_msg_t* msg, unsigned msg_count )
{
  rc_t                rc = kOkRC;
  midi_record_play_t* p  = _handleToPtr(h);
  
  if( msg_count > p->msgArrayN )
  {  
    mem::release(p->msgArray);
    p->msgArray = mem::allocZ<am_midi_msg_t>( msg_count );
    p->msgArrayN = msg_count;
  }

  for(unsigned i=0; i<msg_count; ++i)
  {
    p->msgArray[i].id        = msg[i].id;
    p->msgArray[i].timestamp = msg[i].timestamp;
    p->msgArray[i].ch        = msg[i].ch;
    p->msgArray[i].status    = msg[i].status;
    p->msgArray[i].d0        = msg[i].d0;
    p->msgArray[i].d1        = msg[i].d1;
    p->msgArray[i].devIdx    = p->midiOutDevIdx;
    p->msgArray[i].portIdx   = p->midiOutPortIdx;
    p->msgArray[i].microsec  = time::elapsedMicros(p->msgArray[0].timestamp,p->msgArray[i].timestamp);
  }

  p->msgArrayInIdx =  msg_count;
  p->msgArrayOutIdx = 0;
    
  return rc;
}

cw::rc_t cw::midi_record_play::seek( handle_t h, time::spec_t seek_timestamp )
{
  rc_t rc           = kOkRC;
  bool damp_down_fl = false;  // TODO: track pedals on all channels
  bool sost_down_fl = false;
  bool soft_down_fl = false;

  midi_record_play_t* p = _handleToPtr(h);
      
  for(unsigned i=0; i<p->msgArrayInIdx; ++i)
  {
    am_midi_msg_t* mm = p->msgArray + i;

    if( time::isLTE(seek_timestamp,mm->timestamp) )
    {
      p->msgArrayOutIdx = i;

      _transmit_pedal( p, mm->ch, midi::kSustainCtlMdId,   damp_down_fl );
      _transmit_pedal( p, mm->ch, midi::kSostenutoCtlMdId, sost_down_fl );
      _transmit_pedal( p, mm->ch, midi::kSoftPedalCtlMdId, soft_down_fl );
      
      //io::midiDeviceSend( p->ioH, p->midiOutDevIdx, p->midiOutPortIdx, mm->status + mm->ch, midi::kSustainCtlMdId,   damp_down_fl ? 127 : 0 );
      //io::midiDeviceSend( p->ioH, p->midiOutDevIdx, p->midiOutPortIdx, mm->status + mm->ch, midi::kSostenutoCtlMdId, sost_down_fl ? 127 : 0 );
      //io::midiDeviceSend( p->ioH, p->midiOutDevIdx, p->midiOutPortIdx, mm->status + mm->ch, midi::kSoftPedalCtlMdId, soft_down_fl ? 127 : 0 );
      break;
    }

    if( mm->status == midi::kCtlMdId )
    {
      switch( mm->d0 )
      {
      case midi::kSustainCtlMdId:
        damp_down_fl = mm->d1 > 64;
        break;
            
      case midi::kSostenutoCtlMdId:
        sost_down_fl = mm->d1 > 64;
        break;
            
      case midi::kSoftPedalCtlMdId:
        soft_down_fl = mm->d1 > 64;
        break;
            
      default:
        break;
      }
    }
        
  }

  return rc;
      
}


unsigned cw::midi_record_play::event_count( handle_t h )
{
  midi_record_play_t* p  = _handleToPtr(h);    
  return p->msgArrayInIdx;
}

unsigned cw::midi_record_play::event_index( handle_t h )
{
  midi_record_play_t* p  = _handleToPtr(h);    
  return p->recordFl ? p->msgArrayInIdx : p->msgArrayOutIdx;
}


cw::rc_t  cw::midi_record_play::exec( handle_t h, const io::msg_t& m )
{
  rc_t rc = kOkRC;
  midi_record_play_t* p = _handleToPtr(h);
  
  switch( m.tid )
  {
  case io::kTimerTId:
    if( m.u.timer != nullptr )
      rc = _timer_callback(p,*m.u.timer);
    break;
    
  case io::kMidiTId:
    if( m.u.midi != nullptr )
      _midi_receive(p,*m.u.midi);
    break;

  default:
    rc = kOkRC;
  
  }

  return rc;
}



cw::rc_t cw::midi_record_play::am_to_midi_file( const char* am_filename, const char* midi_filename )
{  
  rc_t           rc          = kOkRC;
  unsigned       msgArrayCnt = 0;
  am_midi_msg_t* msgArray    = nullptr;

  if((rc = _am_file_read( am_filename, msgArrayCnt, msgArray )) != kOkRC )
  {
    rc = cwLogError(rc,"Unable to read AM file '%s'.", cwStringNullGuard(am_filename));
    goto errLabel;
  }

  if((rc = _midi_file_write( midi_filename, msgArray, msgArrayCnt )) != kOkRC )
  {
    rc = cwLogError(rc,"Unable to write AM file '%s' to '%s'.", cwStringNullGuard(am_filename),cwStringNullGuard(midi_filename));
    goto errLabel;
  }

 errLabel:
  mem::release(msgArray);

  return rc;
  
}

cw::rc_t cw::midi_record_play::am_to_midi_dir( const char* inDir )
{  
  rc_t                 rc            = kOkRC;
  filesys::dirEntry_t* dirEntryArray = nullptr;
  unsigned             dirEntryCnt   = 0;
  
  if(( dirEntryArray = dirEntries( inDir, filesys::kDirFsFl, &dirEntryCnt )) == nullptr )
    goto errLabel;

  for(unsigned i=0; i<dirEntryCnt; ++i)
  {
    printf("0x%x %s\n", dirEntryArray[i].flags, dirEntryArray[i].name);
  }
  
 errLabel:
  mem::release(dirEntryArray);
  
  return rc;  
}

cw::rc_t cw::midi_record_play::am_to_midi_file( const object_t* cfg )
{
  rc_t        rc    = kOkRC;
  const char* inDir = nullptr;
  //
  if( cfg == nullptr )
  {
    rc = cwLogError(kInvalidArgRC,"AM to MIDI file: No input directory.");
    goto errLabel;
  }

  //
  if((rc = cfg->getv("inDir",inDir)) != kOkRC )
  {
    rc = cwLogError(rc,"AM to MIDI file: Unable to parse input arg's.");
    goto errLabel;
  }

  //
  if((rc = am_to_midi_dir(inDir)) != kOkRC )
  {
    rc = cwLogError(rc,"AM to MIDI file conversion on directory:'%s' failed.", cwStringNullGuard(inDir));
    goto errLabel;
  }

 errLabel:
  return rc;
  
}
