#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwFile.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwThread.h"
#include "cwMidi.h"
#include "cwMidiDecls.h"
#include "cwMidiFile.h"
#include "cwMidiFileDev.h"
#include <poll.h>

namespace cw
{
  namespace midi
  {
    namespace file_dev
    {
      
      typedef struct file_str
      {
        char*                label;
        midi::file::handle_t mfH;
        bool                 enable_fl;
      } file_t;
      
      typedef struct file_dev_str
      {
        thread::handle_t threadH; //
        int              pipefdA[2];
        file_t*          fileA;   // fileA[ fileN ]
        unsigned         fileN;   //
        
        msg_t*           msgA;    // msgA[ msgN ]
        unsigned         msgN;    //

        time::spec_t     start_ts;
        unsigned         next_msg_idx;
        unsigned         next_rd_msg_idx;

        unsigned         thread_timeout_microsecs;
        unsigned         msg_extra_microsecs;
      } file_dev_t;

      file_dev_t * _handleToPtr(handle_t h)
      { return handleToPtr<handle_t,file_dev_t>(h); }

      rc_t _validate_file_index(file_dev_t* p, unsigned file_idx)
      {
        rc_t rc = kOkRC;
        
        if( file_idx >= p->fileN )
          rc = cwLogError(kInvalidArgRC,"The MIDI device file index %i is invalid.",file_idx);
        
        return rc;
      }

      rc_t _close_midi_file( file_dev_t* p, unsigned file_idx )
      {
        rc_t rc = kOkRC;

        if((rc = _validate_file_index(p,file_idx)) != kOkRC )
          goto errLabel;

        if((rc = close(p->fileA[file_idx].mfH)) != kOkRC )
        {
          rc = cwLogError(rc,"MIDI file close failed on MIDI file device index %i.",file_idx);
          goto errLabel;
        }
        
      errLabel:
        return rc;
      }
      
      rc_t _destroy( file_dev_t* p )
      {
        rc_t rc = kOkRC;

        if((rc = destroy(p->threadH)) != kOkRC )
        {
          rc = cwLogError(rc,"MIDI file device thread destroy failed.");
          goto errLabel;
        }

        for(unsigned i=0; i<p->fileN; ++i)
        {
          if( p->fileA[i].mfH.isValid() )
            close( p->fileA[i].mfH );
          
          mem::release(p->fileA[i].label);
        }
          
        mem::release(p->fileA);
        mem::release(p->msgA);
      errLabel:
        return rc;
      }

      unsigned _calc_msg_count( file_dev_t* p )
      {
        unsigned msgN = 0;
        for(unsigned i=0; i<p->fileN; ++i)
          if( p->fileA[i].mfH.isValid() )
            msgN += msgCount( p->fileA[i].mfH );
        
        return msgN;          
      }

      void _fill_msg_array( file_dev_t* p )
      {
        for(unsigned i=0,k=0; i<p->fileN; ++i)
        {
          if( p->fileA[i].mfH.isValid() )
          {
            unsigned           fileMsgN    = msgCount(p->fileA[i].mfH);
            const file::trackMsg_t** fileMsgPtrA = msgArray(p->fileA[i].mfH);
            
            for(unsigned j=0; j<fileMsgN; ++j)
            {
              p->msgA[k].msg      = fileMsgPtrA[j];
              p->msgA[k].file_idx = i;
              ++k;
            }
          }
        }
      }

      rc_t _prepare_msg_array( file_dev_t* p )
      {
        rc_t     rc   = kOkRC;
        p->msgN = _calc_msg_count(p);
        
        p->msgA = mem::resize<msg_t>(p->msgA,p->msgN);

        _fill_msg_array(p);

        auto f = [](const msg_t& a0,const msg_t& a1) -> bool { return a0.msg->amicro < a1.msg->amicro; };
        std::sort(p->msgA,p->msgA+p->msgN,f);

        p->next_msg_idx = 0;
        
        return rc;        
      }

      
      rc_t _enable_file(  handle_t h, unsigned file_idx, bool enable_fl )
      {
        rc_t rc;
  
        file_dev_t* p = _handleToPtr(h);
  
        if((rc = _validate_file_index(p, file_idx)) != kOkRC )
          goto errLabel;

        p->fileA[ file_idx ].enable_fl = enable_fl;

      errLabel:

        if(rc != kOkRC )
          rc = cwLogError(rc,"MIDI file device %s failed on file index %i.", enable_fl ? "enable" : "disable", file_idx );
        
        return rc;
      }
      
      bool _thread_func( void* arg )
      {
        file_dev_t* p                = (file_dev_t*)arg;
        unsigned    max_sleep_micros = p->thread_timeout_microsecs/2;
        unsigned    sleep_micros     = max_sleep_micros;
        int         sysRC            = 0;
        
        if( p->next_msg_idx < p->msgN )
        {          
          unsigned    cur_time = time::elapsedMicros(p->start_ts);
          unsigned    msg_time = p->msgA[ p->next_msg_idx ].msg->amicro;
          unsigned    write_cnt= 0;

          // for all msgs before the current time
          while( msg_time <= cur_time || msg_time - cur_time < p->msg_extra_microsecs )
          {
            //
            // consume msg here
            //
            
            // advance to next msg
            p->next_msg_idx += 1;   // TODO: should be an increment 'release' memory barrier here
            
            if( p->next_msg_idx >= p->msgN )
              break;
          
            msg_time = p->msgA[ p->next_msg_idx ].msg->amicro;

            write_cnt += 1;
          }

          sleep_micros = msg_time - cur_time;

          if( write_cnt )
            if((sysRC = write(p->pipefdA[1],&write_cnt,sizeof(write_cnt))) < (int)sizeof(write_cnt) )
            {
              cwLogSysError(kWriteFailRC,errno,"Pipe write failed.");
            }

          //printf("%i %i %i %i %i %i %i\n",p->next_msg_idx,p->msgN,write_cnt,sleep_micros,cur_time,msg_time-cur_time,max_sleep_micros);
        }

        sleepUs(std::min(sleep_micros,max_sleep_micros));
        
        return true;
      }
    }
  } 
}


cw::rc_t cw::midi::file_dev::create( handle_t& hRef, const char* labelA[], unsigned max_file_cnt )
{
  rc_t rc;
  int sysRC = 0;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  file_dev_t* p = mem::allocZ<file_dev_t>();
  
  p->fileN                    = max_file_cnt;
  p->fileA                    = mem::allocZ<file_t>(p->fileN);
  p->next_msg_idx             = 0;
  p->thread_timeout_microsecs = 20000;
  p->msg_extra_microsecs      = 3000;

  for(unsigned i=0; i<p->fileN; ++i)
  {
    if( labelA[i] != nullptr )
    {
      p->fileA[i].label = mem::duplStr(labelA[i]);
    }
    else
    {
      rc = cwLogError(kInvalidArgRC,"Count of MIDI file device labels must match the max file count.");
      goto errLabel;
    }
  }

  if((sysRC = pipe(p->pipefdA)) != 0 )
  {
    rc = cwLogSysError(kOpFailRC,sysRC,"Pipe create failed.");
    goto errLabel;
  }

  if((rc = thread::create(p->threadH,
                          _thread_func,
                          p,
                          p->thread_timeout_microsecs,
                          p->thread_timeout_microsecs)) != kOkRC )
  {
    rc = cwLogError(rc,"The MIDI file device thread create failed.");
    goto errLabel;
  }

  hRef.set(p);

errLabel:
  if(rc != kOkRC )
    _destroy(p);
  return rc;
}

cw::rc_t cw::midi::file_dev::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if(!hRef.isValid() )
    return rc;

  file_dev_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI file device destroy failed.");
    goto errLabel;
  }

  hRef.clear();

errLabel:
  return rc;
}

unsigned cw::midi::file_dev::file_count( handle_t h )
{
  file_dev_t* p = _handleToPtr(h);
  return p->fileN;
}

cw::rc_t cw::midi::file_dev::open_midi_file( handle_t h, unsigned file_idx, const char* fname )
{
  rc_t rc;
  file_dev_t* p = _handleToPtr(h);

  if((rc = _close_midi_file(p,file_idx)) != kOkRC )
    goto errLabel;
    
  if((rc = open(p->fileA[file_idx].mfH, fname )) != kOkRC )
    goto errLabel;

  if((rc = _prepare_msg_array(p)) != kOkRC )
    goto errLabel;

  if(0)
  {
    unsigned level = log::level( log::globalHandle());
    log::setLevel( log::globalHandle(), log::kPrint_LogLevel );
    printMsgs( p->fileA[file_idx].mfH, log::globalHandle());
    log::setLevel( log::globalHandle(), level );
  }

  p->fileA[ file_idx ].enable_fl = true;
  
errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"MIDI file device open failed on '%s'.",cwStringNullGuard(fname));
  
  return rc;  
}
        
cw::rc_t cw::midi::file_dev::seek_to_event( handle_t h, unsigned file_idx, unsigned msg_idx )
{
  rc_t rc = kOkRC;
  return rc;
}

cw::rc_t cw::midi::file_dev::start( handle_t h )
{
  rc_t rc;
  file_dev_t* p = _handleToPtr(h);

  time::get(p->start_ts);
  
  if((rc = unpause(p->threadH)) != kOkRC )
  {
    rc = cwLogError(rc,"Thread un-pause failed.");
    goto errLabel;
  }

errLabel:
  return rc;
}
  
cw::rc_t cw::midi::file_dev::stop( handle_t h )
{
  rc_t rc;
  file_dev_t* p = _handleToPtr(h);
  if((rc = pause(p->threadH)) != kOkRC )
  {
    rc = cwLogError(rc,"Thread un-pause failed.");
    goto errLabel;
  }

errLabel:
  return rc;
}

cw::rc_t cw::midi::file_dev::enable_file(  handle_t h, unsigned file_idx )
{ return _enable_file(h,file_idx,true); }

cw::rc_t cw::midi::file_dev::disable_file( handle_t h,unsigned file_idx )
{ return _enable_file(h,file_idx,false); }

int cw::midi::file_dev::file_descriptor( handle_t h )
{
  file_dev_t* p = _handleToPtr(h);
  return p->pipefdA[0];
}

cw::rc_t cw::midi::file_dev::read( handle_t h, msg_t* buf, unsigned buf_msg_cnt, unsigned& actual_msg_cnt_ref )
{
  rc_t        rc             = kOkRC;
  file_dev_t* p              = _handleToPtr(h);
  unsigned    cur_wr_msg_idx = p->next_msg_idx; /// TODO: should be an 'aquire' here

  actual_msg_cnt_ref = 0;
  
  for(unsigned i=0; p->next_rd_msg_idx<cur_wr_msg_idx && i<buf_msg_cnt; ++p->next_rd_msg_idx)
    if( p->fileA[ p->msgA[i].file_idx ].enable_fl )
    {
      memcpy(buf + i, p->msgA + p->next_rd_msg_idx, sizeof(msg_t));
      actual_msg_cnt_ref += 1;
      ++i;
    }

  return rc;
}

cw::rc_t cw::midi::file_dev::test( const object_t* cfg )
{
  handle_t    h;
  rc_t        rc       = kOkRC;
  rc_t        rc1      = kOkRC;
  const char* labelA[] = { "file0" };
  const char* fname    = nullptr;
  unsigned pollfdN = 1;
  struct pollfd* pollfdA = mem::allocZ<struct pollfd>(pollfdN);
  int poll_timeout_ms = 50;
  unsigned limitN = 0;

  if((rc = cfg->getv("fname",fname)) != kOkRC || fname == nullptr)
  {
    cwLogError(rc,"MIDI file dev test arg. parse failed.");
    goto errLabel;
  }

  cwLogInfo("MIDI file dev testing with '%s'.",fname);
  
  if((rc = create(h,labelA,1)) != kOkRC )
    goto errLabel;

  if((rc = open_midi_file(h,0,fname)) != kOkRC )
    goto errLabel;

  if((rc = start(h)) != kOkRC )
    goto errLabel;

  pollfdA[0].fd      = file_descriptor(h);
  pollfdA[0].events  = POLLIN;
  pollfdA[0].revents = 0;

  cwLogInfo("Starting");
  
  while( rc == kOkRC && limitN < 1000 )
  {
    // block here waiting for a midi file dev event
    int poll_res = poll(pollfdA, pollfdN, poll_timeout_ms );

    // if wait timed out
    if( poll_res == 0 )
    {
      poll_res = kOkRC;
      continue;
    }

    // if error
    if( poll_res < 0 )
      rc = cwLogSysError(kOpFailRC,poll_res,"Poll failed.");
    else // if ready to ready
    {
      if( pollfdA[0].revents & POLLIN )
      {
        int      sysRC;
        unsigned rd_buf_cnt = 0;
        
        // read the pipe to get the count of msgs
        if((sysRC = ::read(pollfdA[0].fd,&rd_buf_cnt,sizeof(rd_buf_cnt))) != sizeof(rd_buf_cnt))
        {
          cwLogSysError(kReadFailRC,errno,"Pipe read failed.");
          goto errLabel;
        }
        else
        {
          if( rd_buf_cnt > 0 )
          {
            unsigned actual_cnt = 0;
            msg_t buf[ rd_buf_cnt ];

            // read the file dev messages
            if((rc = read(h,buf,rd_buf_cnt, actual_cnt)) != kOkRC )
            {
              rc = cwLogError(kReadFailRC,"File device read failed.");
              goto errLabel;
            }
            else
            {
              printf("rd:%i %i\n",rd_buf_cnt, actual_cnt);
              limitN += 1;
            }
          }
        }
      }
    }
    
  }

  
  if((rc = stop(h)) != kOkRC )
    goto errLabel;
  
errLabel:
  rc1 = destroy(h);

  rc = rcSelect(rc,rc1);
  
  if(rc != kOkRC )
     rc = cwLogError(rc,"MIDI file dev test failed.");

    
  
  return rc;
  
}
