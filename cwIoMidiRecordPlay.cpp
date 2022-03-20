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

    typedef struct midi_device_str
    {
      char*          midiOutDevLabel;
      char*          midiOutPortLabel;
      unsigned       midiOutDevIdx;
      unsigned       midiOutPortIdx;
      bool           enableFl;
      unsigned       velTableN;
      uint8_t*       velTableArray;
      bool           pedalMapEnableFl;
      unsigned       pedalDownVelId;
      unsigned       pedalDownHalfVelId;
      unsigned       pedalDownVel;
      unsigned       pedalDownHalfVel;
    } midi_device_t;

      enum
      {
        kHalfPedalDone,
        kWaitForBegin,
        kWaitForNoteOn,
        kWaitForNoteOff,
        kWaitForPedalUp,
        kWaitForPedalDown,
      };

    
    typedef struct midi_record_play_str
    {
      io::handle_t   ioH;
      
      am_midi_msg_t* msgArray;                    // msgArray[ msgArrayN ]
      unsigned       msgArrayN;                   // Count of messages allocated in msgArray.
      unsigned       msgArrayInIdx;               // Next available space for loaded MIDI messages (also the current count of msgs in msgArray[])
      unsigned       msgArrayOutIdx;              // Next message to transmit in msgArray[]     
      unsigned       midi_timer_period_micro_sec; // Timer period in microseconds

      am_midi_msg_t* iMsgArray;                    // msgArray[ msgArrayN ]
      unsigned       iMsgArrayN;                   // Count of messages allocated in msgArray.
      unsigned       iMsgArrayInIdx;               // Next available space for incoming MIDI messages (also the current count of msgs in msgArray[])

      
      midi_device_t* midiDevA;
      unsigned       midiDevN;

      bool           startedFl;
      bool           recordFl;
      bool           thruFl;
      bool           logInFl;   // log incoming message when not in 'record' mode.
      bool           logOutFl;  // log outgoing messages
      
      bool     halfPedalFl;
      unsigned halfPedalState;
      unsigned halfPedalNextUs;
      unsigned halfPedalNoteDelayUs;
      unsigned halfPedalNoteDurUs;
      unsigned halfPedalUpDelayUs;
      unsigned halfPedalDownDelayUs;
      uint8_t  halfPedalMidiPitch;
      uint8_t  halfPedalMidiNoteVel;
      uint8_t  halfPedalMidiPedalVel;
            
      time::spec_t   play_time;                
      time::spec_t   start_time;
      time::spec_t   end_play_event_timestamp;
      time::spec_t   store_time;

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

      for(unsigned i=0; i<p->midiDevN; ++i)
      {
        mem::release(p->midiDevA[i].midiOutDevLabel);
        mem::release(p->midiDevA[i].midiOutPortLabel);
        mem::release(p->midiDevA[i].velTableArray);
      }

      mem::release(p->midiDevA);
      mem::release(p->msgArray);
      mem::release(p->iMsgArray);
      mem::release(p);
    
      return rc;
    }

    rc_t _parseCfg(midi_record_play_t* p, const object_t& cfg )
    {
      rc_t rc = kOkRC;
      const object_t* midiDevL = nullptr;
      if((rc = cfg.getv(
                        "max_midi_msg_count",          p->msgArrayN,
                        "midi_timer_period_micro_sec", p->midi_timer_period_micro_sec,
                        "midi_device_list",            midiDevL,
                        "log_in_flag",                 p->logInFl,
                        "log_out_flag",                p->logOutFl,
                        "half_pedal_flag",             p->halfPedalFl)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"MIDI record play configuration parse failed.");
        goto errLabel;
      }

      p->iMsgArrayN = p->msgArrayN;
      
      if( midiDevL->child_count() > 0 )
      {
        p->midiDevN = midiDevL->child_count();
        p->midiDevA = mem::allocZ<midi_device_t>(p->midiDevN);

        printf("Midi record play devices:%i\n",p->midiDevN);
        
        for(unsigned i=0; i<p->midiDevN; ++i)
        {
          const object_t* ele              = midiDevL->child_ele(i);
          const char*     midiOutDevLabel  = nullptr;
          const char*     midiOutPortLabel = nullptr;
          const object_t* velTable         = nullptr;
          const object_t* pedalRecd        = nullptr;
          bool            enableFl         = false;
          
          if((rc = ele->getv( "midi_out_device",   midiOutDevLabel,
                              "midi_out_port",     midiOutPortLabel,
                              "enableFl",          enableFl)) != kOkRC )
          {
            rc = cwLogError(kSyntaxErrorRC,"MIDI record play device list configuration parse failed.");
            goto errLabel;          
          }
          

          if((rc = ele->getv_opt( "vel_table", velTable,
                                  "pedal",     pedalRecd)) != kOkRC )
          {
            rc = cwLogError(kSyntaxErrorRC,"MIDI record play device optional argument parsing failed.");
            goto errLabel;          
          }
          
          p->midiDevA[i].midiOutDevLabel  = mem::duplStr( midiOutDevLabel);
          p->midiDevA[i].midiOutPortLabel = mem::duplStr( midiOutPortLabel);
          p->midiDevA[i].enableFl         = enableFl;

          if( velTable != nullptr )
          {
            p->midiDevA[i].velTableN     = velTable->child_count();
            p->midiDevA[i].velTableArray = mem::allocZ<uint8_t>(p->midiDevA[i].velTableN);

            
            for(unsigned j=0; j<p->midiDevA[i].velTableN; ++j)
            {
              if((rc = velTable->child_ele(j)->value( p->midiDevA[i].velTableArray[j] )) != kOkRC )
              {
                rc = cwLogError(kSyntaxErrorRC,"An error occured while parsing the velocity table for MIDI device:'%s' port:'%s'.",midiOutDevLabel,midiOutPortLabel);
                goto errLabel;
              }
            }              
          }

          if( pedalRecd != nullptr )
          {
            if((rc = pedalRecd->getv( "down_id",  p->midiDevA[i].pedalDownVelId,
                                      "down_vel", p->midiDevA[i].pedalDownVel,
                                      "half_id",  p->midiDevA[i].pedalDownHalfVelId,
                                      "half_vel", p->midiDevA[i].pedalDownHalfVel )) != kOkRC )
            {
              rc = cwLogError(kSyntaxErrorRC,"An error occured while parsing the pedal record for MIDI device:'%s' port:'%s'.",midiOutDevLabel,midiOutPortLabel);
              goto errLabel;              
            }
            else
            {
              p->midiDevA[i].pedalMapEnableFl = true;
            }
          }
          
          
        }
      }
      
      // allocate the MIDI msg buffer
      p->msgArray         = mem::allocZ<am_midi_msg_t>( p->msgArrayN );
      p->iMsgArray        = mem::allocZ<am_midi_msg_t>( p->iMsgArrayN );
      
    errLabel:
      return rc;
    }

    rc_t _stop( midi_record_play_t* p );

    
    const am_midi_msg_t*  _midi_store( midi_record_play_t* p, unsigned devIdx, unsigned portIdx, const time::spec_t& ts, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 )
    {
      am_midi_msg_t* am = nullptr;
      
      // verify that space exists in the record buffer
      if( p->iMsgArrayInIdx < p->iMsgArrayN )
      {
        // MAKE THIS ATOMIC
        unsigned id = p->iMsgArrayInIdx;
        ++p->iMsgArrayInIdx;

        am = p->iMsgArray + id;
        
        am->id        = id;
        am->devIdx    = devIdx;
        am->portIdx   = portIdx;
        am->timestamp = ts;
        am->ch        = ch;
        am->status    = status;
        am->d0        = d0;
        am->d1        = d1;
        
      }

      return am;
    }
    
    rc_t _event_callback( midi_record_play_t* p, unsigned id, const time::spec_t timestamp, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1, bool log_fl=true )
    {
      rc_t rc = kOkRC;
      
      if( !time::isZero(p->end_play_event_timestamp) && time::isGT(timestamp,p->end_play_event_timestamp))
      {
        rc = _stop(p);
        printf("ZERO\n");
      }
      else
      {

        if( p->halfPedalFl )
        {
          if( status==midi::kCtlMdId && d0 == midi::kSustainCtlMdId && d1 != 0 )
            d1 = p->halfPedalMidiPedalVel;
        }
        
        
        for(unsigned i=0; i<p->midiDevN; ++i)
          if(p->midiDevA[i].enableFl )
          {
            uint8_t out_d1 = d1;
            
            if( !p->halfPedalFl )
            {
              // map the note on velocity
              if( status==midi::kNoteOnMdId and d1 != 0 and p->midiDevA[i].velTableArray != nullptr )
              {
                if( d1 >= p->midiDevA[i].velTableN )
                  cwLogError(kInvalidIdRC,"A MIDI note-on velocity (%i) outside the velocity table range was encountered.",d1);
                else                
                  out_d1 = p->midiDevA[i].velTableArray[ d1 ];
              }

              // map the pedal down velocity
              if( status==midi::kCtlMdId && d0 == midi::kSustainCtlMdId && p->midiDevA[i].pedalMapEnableFl )
              {
                if( d1 == p->midiDevA[i].pedalDownVelId )
                  out_d1 = p->midiDevA[i].pedalDownVel;
                else
                  if( d1 == p->midiDevA[i].pedalDownHalfVelId )
                    out_d1 = p->midiDevA[i].pedalDownHalfVel;
                  else
                    cwLogError(kInvalidIdRC,"Unexpected pedal down velocity (%i) during pedal velocity mapping.",d1);
              }
            }
            
            io::midiDeviceSend( p->ioH, p->midiDevA[i].midiOutDevIdx, p->midiDevA[i].midiOutPortIdx, status + ch, d0, out_d1 );
          }

        if( p->cb )
          p->cb( p->cb_arg, id, timestamp, ch, status, d0, d1 );

        if( log_fl && p->logOutFl )
        {
          // Note: The device of outgoing messages is set to p->midiDevN + 1 to distinguish it from
          // incoming messages.
          _midi_store( p, p->midiDevN, 0, timestamp, ch, status, d0, d1 );
        }
        
      }
      
      return rc;
    }

    rc_t _transmit_msg( midi_record_play_t* p, const am_midi_msg_t* am, bool log_fl=true )
    {
      return _event_callback( p, am->id, am->timestamp, am->ch, am->status, am->d0, am->d1, log_fl );
    }

    rc_t _transmit_note( midi_record_play_t* p, unsigned ch, unsigned pitch, unsigned vel, unsigned microsecs )
    {
      time::spec_t ts = {0};
      time::microsecondsToSpec( ts, microsecs );
      return _event_callback( p, kInvalidId, ts, ch, midi::kNoteOnMdId, pitch, vel );
    }

    rc_t _transmit_ctl(  midi_record_play_t* p, unsigned ch, unsigned ctlId, unsigned ctlVal, unsigned microsecs )
    {
      time::spec_t ts = {0};
      time::microsecondsToSpec( ts, microsecs );
      return _event_callback( p, kInvalidId, ts, ch, midi::kCtlMdId, ctlId, ctlVal );
    }
    
    rc_t _transmit_pedal( midi_record_play_t* p, unsigned ch, unsigned pedalCtlId, bool pedalDownFl, unsigned microsecs )
    {
      return _transmit_ctl( p, ch, pedalCtlId, pedalDownFl ? 127 : 0, microsecs);
    }

    void _half_pedal_update( midi_record_play_t* p, unsigned cur_time_us )
    {
      if( cur_time_us >= p->halfPedalNextUs )
      {
        unsigned midi_ch = 0;
        switch( p->halfPedalState )
        {
          
          case kWaitForBegin:
            printf("down: %i %i\n",cur_time_us/1000,p->halfPedalMidiPedalVel);
            _transmit_ctl( p, midi_ch, midi::kSustainCtlMdId, p->halfPedalMidiPedalVel, cur_time_us);
            p->halfPedalState = kWaitForNoteOn;
            p->halfPedalNextUs += p->halfPedalNoteDelayUs;
            break;
          
          case kWaitForNoteOn:
            printf("note: %i\n",cur_time_us/1000);
            _transmit_note( p, midi_ch, p->halfPedalMidiPitch, p->halfPedalMidiNoteVel, cur_time_us );
            p->halfPedalNextUs += p->halfPedalNoteDurUs;
            p->halfPedalState = kWaitForNoteOff;
            break;
              
          case kWaitForNoteOff:
            printf("off:  %i\n",cur_time_us/1000);
            _transmit_note( p, midi_ch, p->halfPedalMidiPitch, 0, cur_time_us );
            p->halfPedalNextUs += p->halfPedalUpDelayUs;
            p->halfPedalState = kWaitForPedalUp;
            break;
            
          case kWaitForPedalUp:
            printf("up:   %i\n",cur_time_us/1000);
            _transmit_ctl( p, midi_ch, midi::kSustainCtlMdId, 0, cur_time_us);
            p->halfPedalNextUs += p->halfPedalDownDelayUs;
            p->halfPedalState = kWaitForPedalDown;            
            break;

          case kWaitForPedalDown:
            //printf("down:   %i\n",cur_time_us/1000);
            //_transmit_ctl( p, midi_ch, midi::kSustainCtlMdId, p->halfPedalMidiPedalVel, cur_time_us);
            //_stop(p);
            p->halfPedalState = kHalfPedalDone;
            break;
            
          
          case kHalfPedalDone:
            break;
        }
      }
    }

    // Set the next location to store an incoming MIDI message
    void _set_midi_msg_next_index( midi_record_play_t* p, unsigned next_idx )
    {
      p->iMsgArrayInIdx = next_idx;
    }

    // Set the next index of the next MIDI message to transmit
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

    // Fill the play buffer from a previously store AM file.
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

      p->msgArrayInIdx = n;

      cwLogInfo("Read %i from '%s'.",n,fn);

    errLabel:

      file::close(fH);
        
      return rc;        
    }

    // Write the record buffer to an AM file
    rc_t _midi_write( midi_record_play_t* p, const char* fn )
    {
      rc_t           rc = kOkRC;
      file::handle_t fH;

      if( p->iMsgArrayInIdx == 0 )
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
      if((rc = write(fH,p->iMsgArrayInIdx)) != kOkRC )
      {
        rc = cwLogError(kWriteFailRC,"Header write to '%s' failed.",cwStringNullGuard(fn));
        goto errLabel;          
      }

      // write the file data
      if((rc = write(fH,p->iMsgArray,sizeof(am_midi_msg_t)*p->iMsgArrayInIdx)) != kOkRC )
      {
        rc = cwLogError(kWriteFailRC,"Data write to '%s' failed.",cwStringNullGuard(fn));
        goto errLabel;                    
      }

    errLabel:
      file::close(fH);

      cwLogInfo("Saved %i events to '%s'.", p->iMsgArrayInIdx, fn );
        
      return rc;
    }

    rc_t _write_csv( midi_record_play_t* p, const char* fn )
    {
      rc_t           rc = kOkRC;
      file::handle_t fH;

      if( p->iMsgArrayInIdx == 0 )
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

      file::printf(fH,"dev,port,microsec,id,sec,ch,status,d0,d1\n");
      
      for(unsigned i=0; i<p->iMsgArrayInIdx; ++i)
      {
        const am_midi_msg_t* m = p->iMsgArray + i;

        double secs = time::elapsedSecs( p->iMsgArray[0].timestamp, p->iMsgArray[i].timestamp );
        
        char sciPitch[ midi::kMidiSciPitchCharCnt + 1 ];
        if( m->status == midi::kNoteOnMdId )
          midi::midiToSciPitch( m->d0, sciPitch, midi::kMidiSciPitchCharCnt );
        else
          strcpy(sciPitch,"");

        if((rc = file::printf(fH, "%3i,%3i,%8i,%3i,%8.4f,%2i,0x%2x,%5s,%3i,%3i\n",
                              m->devIdx, m->portIdx, m->microsec, m->id, secs,
                              m->ch, m->status, sciPitch, m->d0, m->d1 )) != kOkRC )
        {
          rc  = cwLogError(rc,"Write failed on line:%i", i+1 );
          goto errLabel;
        }
      }

    errLabel:
      file::close(fH);

      cwLogInfo("Saved %i events to '%s'.", p->iMsgArrayInIdx, fn );

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

      // if we were recording
      if( p->recordFl )
      {

        // set the 'microsec' value for each MIDI msg as an offset from the first message[]
        for(unsigned i=0; i<p->iMsgArrayInIdx; ++i)
        {
          p->msgArray[i].microsec = time::elapsedMicros(p->iMsgArray[0].timestamp,p->iMsgArray[i].timestamp);
        }
          
        cwLogInfo("MIDI messages recorded: %i",p->msgArrayInIdx );
          
      }
      else
      {
        io::timerStop( p->ioH, io::timerIdToIndex(p->ioH, kMidiRecordPlayTimerId) );

        // TODO:
        // BUG BUG BUG: should work for all channels

        // all notes off
        _transmit_ctl(   p, 0, 121, 0, 0 ); // reset all controllers
        _transmit_ctl(   p, 0, 123, 0, 0 ); // all notes off
        _transmit_ctl(   p, 0,   0, 0, 0 ); // switch to bank 0
        
      }

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
          //printf("IN: 0x%x 0x%x 0x%x\n", pkt->msgArray[j].status, pkt->msgArray[j].d0, pkt->msgArray[j].d1 );
          
          if( (p->recordFl || p->logInFl) && p->startedFl )
          {

            // verify that space exists in the record buffer
            if( p->iMsgArrayInIdx >= p->iMsgArrayN )
            {
              _stop(p);
              rc = cwLogError(kBufTooSmallRC,"MIDI message record buffer is full. % messages.",p->iMsgArrayN);
              goto errLabel;
            }
            else
            {
              // copy the msg into the record buffer
              midi::msg_t*   mm = pkt->msgArray + j;

              if( midi::isChStatus(mm->status) )
              {
                const am_midi_msg_t* am = _midi_store( p, pkt->devIdx, pkt->portIdx, mm->timeStamp, mm->status & 0x0f, mm->status & 0xf0, mm->d0, mm->d1 );

                if( p->thruFl && am != nullptr )
                  _transmit_msg( p, am, false );

              }
            }
          }
        }

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

          if( p->halfPedalFl )
            _half_pedal_update( p, cur_time_us );
          else  
            while( p->msgArray[ p->msgArrayOutIdx ].microsec <= cur_time_us )
            {

              am_midi_msg_t* mm = p->msgArray + p->msgArrayOutIdx;

              //_print_midi_msg(mm);

              _transmit_msg( p, mm );
            
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
  p->halfPedalState        = kHalfPedalDone;
  p->halfPedalNextUs       = 0;
  p->halfPedalNoteDelayUs  = 100 * 1000;
  p->halfPedalNoteDurUs    = 1000 * 1000;
  p->halfPedalUpDelayUs    = 1000 * 1000;
  p->halfPedalDownDelayUs  = 1000 * 1000;
  p->halfPedalMidiPitch    = 64;
  p->halfPedalMidiNoteVel  = 64;
  p->halfPedalMidiPedalVel = 127;
  

  for( unsigned i=0; i<
          p->midiDevN; ++i)
  {
    midi_device_t* dev = p->midiDevA + i;
    
    if((dev->midiOutDevIdx = io::midiDeviceIndex(p->ioH,dev->midiOutDevLabel)) == kInvalidIdx )
    {
      rc = cwLogError(kInvalidArgRC,"The MIDI output device: '%s' was not found.", cwStringNullGuard(dev->midiOutDevLabel) );
      goto errLabel;
    }
    
    if((dev->midiOutPortIdx = io::midiDevicePortIndex(p->ioH,dev->midiOutDevIdx,false,dev->midiOutPortLabel)) == kInvalidIdx )
    {
      rc = cwLogError(kInvalidArgRC,"The MIDI output port: '%s' was not found.", cwStringNullGuard(dev->midiOutPortLabel) );
      goto errLabel;
    }

    printf("%s %s : %i %i\n",dev->midiOutDevLabel, dev->midiOutPortLabel, dev->midiOutDevIdx, dev->midiOutPortIdx );
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

  // set the end play time
  if( end_play_event_timestamp == nullptr or time::isZero(*end_play_event_timestamp) )
    time::setZero(p->end_play_event_timestamp);
  else
    p->end_play_event_timestamp = *end_play_event_timestamp;
  
  time::get(p->start_time);
        
  if( p->recordFl || p->logInFl or p->logOutFl )
  {
    _set_midi_msg_next_index(p, 0 );
  }

  if( !p->recordFl )
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

    if( p->halfPedalFl )
    {
      p->halfPedalNextUs = 0;
      p->halfPedalState  = kWaitForBegin;
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

cw::rc_t cw::midi_record_play::save_csv( handle_t h, const char* fn )
{
  midi_record_play_t* p  = _handleToPtr(h);  
  return _write_csv(p,fn);
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
    p->msgArray[i].devIdx    = kInvalidIdx;
    p->msgArray[i].portIdx   = kInvalidIdx;
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

      _transmit_pedal( p, mm->ch, midi::kSustainCtlMdId,   damp_down_fl, 0 );
      _transmit_pedal( p, mm->ch, midi::kSostenutoCtlMdId, sost_down_fl, 0 );
      _transmit_pedal( p, mm->ch, midi::kSoftPedalCtlMdId, soft_down_fl, 0 );

      printf("PEDAL: %s.\n", damp_down_fl ? "Down" : "Up");
      
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
  return p->recordFl ? p->iMsgArrayInIdx : p->msgArrayOutIdx;
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

unsigned cw::midi_record_play::device_count( handle_t h )
{
  midi_record_play_t* p = _handleToPtr(h);
  return p->midiDevN;
}

bool cw::midi_record_play::is_device_enabled( handle_t h, unsigned devIdx )
{
  midi_record_play_t* p = _handleToPtr(h);
  bool fl = false;
  
  if( devIdx >= p->midiDevN )
    cwLogError(kInvalidArgRC,"The MIDI record-play device index '%i' is invalid.",devIdx );
  else
    fl = p->midiDevA[devIdx].enableFl;
  
  return fl;
}

void cw::midi_record_play::enable_device( handle_t h, unsigned devIdx, bool enableFl )
{
  midi_record_play_t* p = _handleToPtr(h);
  
  if( devIdx >= p->midiDevN )
    cwLogError(kInvalidArgRC,"The MIDI record-play device index '%i' is invalid.",devIdx );
  else
  {
    p->midiDevA[devIdx].enableFl = enableFl;
    printf("Enable: %i = %i\n",devIdx,enableFl);
  }
}

void cw::midi_record_play::half_pedal_params( handle_t h, unsigned noteDelayMs, unsigned pitch, unsigned vel, unsigned pedal_vel, unsigned noteDurMs, unsigned downDelayMs )
{
  midi_record_play_t* p = _handleToPtr(h);
  p->halfPedalNoteDelayUs = noteDelayMs * 1000;
  p->halfPedalNoteDurUs   = noteDurMs   * 1000;
  p->halfPedalDownDelayUs = downDelayMs   * 1000;
  p->halfPedalMidiPitch   = pitch;
  p->halfPedalMidiNoteVel = vel;
  p->halfPedalMidiPedalVel= pedal_vel;
      
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
