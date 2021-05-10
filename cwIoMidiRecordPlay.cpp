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
#include "cwUiDecls.h"
#include "cwIo.h"
#include "cwIoMidiRecordPlay.h"


namespace cw
{
  namespace midi_record_play
  {
    typedef struct am_midi_msg_str
    {
      unsigned      devIdx;
      unsigned      portIdx;
      time::spec_t  timestamp;
      uint8_t       ch;
      uint8_t       status;
      uint8_t       d0;
      uint8_t       d1;

      unsigned microsec;
        
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

    void _set_midi_msg_next_index( midi_record_play_t* p, unsigned next_idx )
    {
      p->msgArrayInIdx = next_idx;
        
      //io::uiSendValue( p->ioH, kInvalidId, uiFindElementUuId(p->ioH,kMsgCntId), p->midiMsgArrayInIdx );

    }

    void _set_midi_msg_next_play_index(midi_record_play_t* p, unsigned next_idx)
    {
      p->msgArrayOutIdx = next_idx;
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
        io::midiDeviceSend( p->ioH, p->midiOutDevIdx, p->midiOutPortIdx, midi::kCtlMdId, 123, 0 );
        io::midiDeviceSend( p->ioH, p->midiOutDevIdx, p->midiOutPortIdx, midi::kCtlMdId, midi::kSustainCtlMdId, 0 );
        io::midiDeviceSend( p->ioH, p->midiOutDevIdx, p->midiOutPortIdx, midi::kCtlMdId, midi::kSostenutoCtlMdId, 0 );
        // soft pedal
        io::midiDeviceSend( p->ioH, p->midiOutDevIdx, p->midiOutPortIdx, midi::kCtlMdId, 67, 0 );

      }

      cwLogInfo("Runtime: %5.2f seconds.", time::elapsedMs(p->start_time,t1)/1000.0 );

      return rc;
    }

    rc_t _midi_callback( midi_record_play_t* p, const io::midi_msg_t& m )
    {
      rc_t                  rc  = kOkRC;
      const midi::packet_t* pkt = m.pkt;

      // for each midi msg
      for(unsigned j=0; j<pkt->msgCnt; ++j)
      {
          
        // if this is a sys-ex msg
        if( pkt->msgArray == NULL )
        {
          // this is a sys ex msg use: pkt->sysExMsg[j]
        }
        else // this is a triple
        {
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
                  io::midiDeviceSend( p->ioH, p->midiOutDevIdx, p->midiOutPortIdx, am->status + am->ch, am->d0, am->d1 );


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

            io::midiDeviceSend( p->ioH, p->midiOutDevIdx, p->midiOutPortIdx, mm->status + mm->ch, mm->d0, mm->d1 );
            
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

cw::rc_t cw::midi_record_play::create( handle_t& hRef, io::handle_t ioH, const object_t& cfg )
{
  midi_record_play_t* p = nullptr;
  rc_t rc;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  p = mem::allocZ<midi_record_play_t>();
  
  if((rc = _parseCfg(p,cfg)) != kOkRC )
    goto errLabel;

  p->ioH = ioH;
  
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
  if((rc = timerCreate( p->ioH, "midi_record_play_timer", kMidiRecordPlayTimerId, p->midi_timer_period_micro_sec)) != kOkRC )
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

cw::rc_t cw::midi_record_play::start( handle_t h )
{
  midi_record_play_t* p = _handleToPtr(h);
  p->startedFl = true;

  time::get(p->start_time);
        
  if( p->recordFl )
  {
    _set_midi_msg_next_index(p, 0 );
  }
  else
  {
    _set_midi_msg_next_play_index(p,0);
    io::timerStart( p->ioH, io::timerIdToIndex(p->ioH, kMidiRecordPlayTimerId) );
    time::get(p->play_time);
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
      _midi_callback(p,*m.u.midi);
    break;

  default:
    rc = kOkRC;
  
  }

  return rc;
}

