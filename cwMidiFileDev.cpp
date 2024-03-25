#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwFile.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwText.h"
#include "cwTextBuf.h"
#include "cwMidi.h"
#include "cwMidiDecls.h"
#include "cwMidiFile.h"
#include "cwMidiDevice.h"
#include <poll.h>
#include "cwMidiFileDev.h"

namespace cw
{
  namespace midi
  {
    namespace device
    {
      namespace file_dev
      {

        typedef struct file_msg_str
        {
          unsigned long long      amicro;   // 
          msg_t*                  msg;      // msg_t as declared in cwMidiDecls.h
          unsigned                file_idx;
          unsigned                msg_idx;
        } file_msg_t;
      
        
        typedef struct file_str
        {
          char*                label;
          char*                fname;
          
          msg_t*               msgA;
          unsigned             msgN;
          
          bool                 enable_fl;
        } file_t;
      
        typedef struct file_dev_str
        {
          cbFunc_t         cbFunc;
          void*            cbArg;
          
          file_t*          fileA;      // fileA[ fileN ]
          unsigned         fileN;      //
        
          file_msg_t*      msgA;       // msgA[ msgN ]
          unsigned         msgAllocN;  //
          unsigned         msgN; 

          unsigned         devCnt;     // always 1
          unsigned         base_dev_idx;
          char*            dev_name;

          bool             is_activeFl;

          // The following indexes are all int p->msgA[ p->msgN ]
          unsigned         beg_msg_idx;      // beg_msg_idx is the first msg to transmit
          unsigned         end_msg_idx;      // end_msg_idx indicates the last msg to transmit, end_msg_idx+1 will not be transmitted
          unsigned         next_wr_msg_idx;  // next_wr_msg_idx is the next msg to transmit
          unsigned         next_rd_msg_idx;  // next_rd_msg_idx is the last msg transmitted

          unsigned long long   start_delay_micros;
          
          unsigned long long read_ahead_micros;

          bool                  latency_meas_enable_in_fl;
          bool                  latency_meas_enable_out_fl;
          latency_meas_result_t latency_meas_result;
          
        } file_dev_t;

        file_dev_t * _handleToPtr(handle_t h)
        { return handleToPtr<handle_t,file_dev_t>(h); }

        rc_t _validate_file_index(file_dev_t* p, unsigned file_idx)
        {
          rc_t rc = kOkRC;
        
          if( file_idx >= p->fileN )
            rc = cwLogError(kInvalidArgRC,"The MIDI device file/port index %i is invalid.",file_idx);
        
          return rc;
        }

        bool _file_exists( const file_t& r )
        { return r.msgN > 0; }

        rc_t _validate_file_existence(file_dev_t* p, unsigned file_idx )
        {
          rc_t rc;
          if((rc = _validate_file_index(p,file_idx)) == kOkRC )
            if( !_file_exists(p->fileA[file_idx]) )
              rc = cwLogError(kInvalidArgRC,"The MIDI device file at file/port index %i does not exist.",file_idx);
            
          return rc;
        }

        rc_t _validate_dev_index(file_dev_t* p, unsigned dev_idx )
        {
          rc_t rc = kOkRC;
          
          if( dev_idx >= p->devCnt )
            rc = cwLogError(kInvalidArgRC,"The MIDI file device index %i is invalid.",dev_idx );
          return rc;
        }

        rc_t _validate_port_index(file_dev_t* p, unsigned port_idx )
       { return _validate_file_index(p,port_idx); }

        void _reset_indexes( file_dev_t* p )
        {
          p->beg_msg_idx     = kInvalidIdx;
          p->end_msg_idx     = kInvalidIdx;
          p->next_wr_msg_idx = kInvalidIdx;
          p->next_rd_msg_idx = kInvalidIdx;          
        }
        
        rc_t _close_file( file_dev_t* p, unsigned file_idx )
        {
          rc_t rc = kOkRC;

          
          if((rc = _validate_file_index(p,file_idx)) != kOkRC )
            goto errLabel;

          if( _file_exists(p->fileA[file_idx] ) )
          {
            // if the beg/end msg index refers to the file being closed then invalidate the beg/end msg idx
            if( p->beg_msg_idx != kInvalidIdx && p->beg_msg_idx < p->msgN && p->msgA[ p->beg_msg_idx ].file_idx == file_idx )
              p->beg_msg_idx = kInvalidIdx;
            
            if( p->end_msg_idx != kInvalidIdx && p->end_msg_idx < p->msgN && p->msgA[ p->end_msg_idx ].file_idx == file_idx )
              p->end_msg_idx = kInvalidIdx;

            mem::release(p->fileA[file_idx].fname);
            p->fileA[file_idx].msgN = 0;
          }

          
        errLabel:
          return rc;
        }
      
        rc_t _destroy( file_dev_t* p )
        {
          rc_t rc = kOkRC;

          for(unsigned i=0; i<p->fileN; ++i)
          {
            mem::release(p->fileA[i].msgA);          
            mem::release(p->fileA[i].label);
            mem::release(p->fileA[i].fname);
          }

          mem::release(p->fileA);
          mem::release(p->msgA);
          mem::release(p->dev_name);
          mem::release(p);
          //errLabel:
          return rc;
        }


        rc_t _open_midi_file( file_dev_t* p, unsigned file_idx, const char* fname )
        {
          rc_t                 rc = kOkRC;
          midi::file::handle_t mfH;
          
          if((rc = open(mfH, fname)) != kOkRC )
          {
            rc = cwLogError(rc,"MIDI file open failed on '%s'.",cwStringNullGuard(fname));
            goto errLabel;
          }
          else
          {
            unsigned                 msg_idx     = 0;
            unsigned                 msgN        = msgCount(mfH);;
            const file::trackMsg_t** fileMsgPtrA = msgArray(mfH);
            
            p->fileA[file_idx].msgA = mem::resizeZ<msg_t>(p->fileA[file_idx].msgA,msgN);
            
            for(unsigned j=0; j<msgN; ++j)
              if( isChStatus(fileMsgPtrA[j]->status) )
              {
                msg_t* m = p->fileA[file_idx].msgA + msg_idx;
                
                m->uid       = j; 
                m->ch        = fileMsgPtrA[j]->u.chMsgPtr->ch;
                m->status    = fileMsgPtrA[j]->status;
                m->d0        = fileMsgPtrA[j]->u.chMsgPtr->d0;
                m->d1        = fileMsgPtrA[j]->u.chMsgPtr->d1;
                m->timeStamp = time::microsecondsToSpec(fileMsgPtrA[j]->amicro); 

                msg_idx += 1;
              }

            p->fileA[file_idx].msgN  = msg_idx;
            p->fileA[file_idx].fname = mem::duplStr(fname);
            close( mfH );

          }
          
          
        errLabel:
          return rc;
        }
    
        
        unsigned _calc_msg_count( file_dev_t* p )
        {
          unsigned msgAllocN = 0;
          for(unsigned i=0; i<p->fileN; ++i)
            if( p->fileA[i].msgA != nullptr )
              msgAllocN += p->fileA[i].msgN;
        
          return msgAllocN;          
        }

        // Set msg_idx to kInvalidIdx to seek to the current value of p->beg_msg_idx
        // or 0 if p->beg_msg_idx was never set.
        // If msg_idx is a valid msg index then it will be assigned to p->beg_msg_idx
        rc_t _seek_to_msg_index( file_dev_t* p, unsigned file_idx, unsigned msg_idx )
        {
          rc_t rc;
          unsigned i = 0;
          unsigned beg_msg_idx = p->beg_msg_idx;
          
          if((rc = _validate_file_existence(p,file_idx)) != kOkRC )
            goto errLabel;

          // if no target msg was given ...
          if( msg_idx == kInvalidIdx )
          {
            // ... then use the previous target msg or 0
            beg_msg_idx = p->beg_msg_idx == kInvalidIdx ? 0 : p->beg_msg_idx;
          }
          else // if a target msg was given ..
          {
            // locate the msg in p->msgA[]
            for(i=0; i<p->msgN; ++i)
              if( p->msgA[i].file_idx == file_idx && p->msgA[i].msg_idx == msg_idx )
              {
                p->beg_msg_idx = i;
                beg_msg_idx    = i;
                break;
              }

            if( i == p->msgN )
            {
              rc = cwLogError(kEleNotFoundRC,"The 'begin' MIDI file event at index %i in %s was not found.",msg_idx,cwStringNullGuard(p->fileA[file_idx].label));
              goto errLabel;
            }
          }

          p->next_wr_msg_idx = beg_msg_idx;
          p->next_rd_msg_idx = beg_msg_idx;
          
        errLabel:
          return rc;
        }

        // Set file_idx and msg_idx to kInvalidIdx to make the p->msgN the end index.
        // Set msg_idx to kInvalidIdx to make the last p->fileA[file_idx].msgN the end index.
        rc_t _set_end_msg_index( file_dev_t* p, unsigned file_idx, unsigned msg_idx )
        {
          rc_t rc = kOkRC;
          unsigned i = 0;

          if( file_idx == kInvalidIdx )
            p->end_msg_idx = p->msgN - 1;
          else
          {                    
            if((rc = _validate_file_existence(p,file_idx)) != kOkRC )
              goto errLabel;

            if( msg_idx == kInvalidIdx )
              msg_idx = p->fileA[ file_idx ].msgN - 1;
            
            // locate the msg in p->msgA[]
            for(i=0; i<p->msgN; ++i)
              if( p->msgA[i].file_idx == file_idx && p->msgA[i].msg_idx == msg_idx )
              {
                p->end_msg_idx = i;
                break;
              }

            if( i == p->msgN )
            {
              rc = cwLogError(kEleNotFoundRC,"The 'end' MIDI file event at index %i in %s was not found.",msg_idx,cwStringNullGuard(p->fileA[file_idx].label));
              goto errLabel;
            }
          }

        errLabel:          
          return rc;
        }

        
        unsigned _fill_msg_array_from_msg( file_dev_t* p, unsigned file_idx, msg_t* msgA, unsigned msgN, unsigned msg_idx )
        {
          unsigned  k  = msg_idx;
            
          for(unsigned j=0; j<msgN; ++j)
            if( isChStatus(msgA[j].status) )
            {
              p->msgA[k].msg_idx  = j;
              p->msgA[k].amicro   = time::specToMicroseconds(msgA[j].timeStamp);
              p->msgA[k].msg      = msgA + j;
              p->msgA[k].file_idx = file_idx;
              ++k;
            }
          
          return k;
          
        }
        
        void _fill_msg_array( file_dev_t* p )
        {
          unsigned msg_idx = 0;
          for(unsigned i=0; i<p->fileN; ++i)
            if( p->fileA[i].msgA != nullptr )
              msg_idx = _fill_msg_array_from_msg(p,i,p->fileA[i].msgA,p->fileA[i].msgN,msg_idx);

          p->msgN = msg_idx;
          
        }

        // Combine all the file msg's into a single array and sort them on file_msg_t.amicro.
        rc_t _prepare_msg_array( file_dev_t* p )
        {
          rc_t     rc   = kOkRC;

          // save the current starting message
          unsigned beg_file_idx = p->beg_msg_idx==kInvalidIdx ? kInvalidIdx : p->msgA[ p->beg_msg_idx ].file_idx;
          unsigned beg_msg_idx  = p->beg_msg_idx==kInvalidIdx ? kInvalidIdx : p->msgA[ p->beg_msg_idx ].msg_idx;

          // if the 'beg' file does not exist
          if( beg_file_idx != kInvalidIdx && _file_exists(p->fileA[ beg_file_idx ])==false )
          {  
            beg_file_idx = kInvalidIdx;
            beg_msg_idx  = kInvalidIdx;
          }

          // save the current ending message
          unsigned end_file_idx = p->end_msg_idx==kInvalidIdx ? kInvalidIdx : p->msgA[ p->end_msg_idx ].file_idx;
          unsigned end_msg_idx  = p->end_msg_idx==kInvalidIdx ? kInvalidIdx : p->msgA[ p->end_msg_idx ].msg_idx;

          // if the 'end' file does not exist
          if( end_file_idx != kInvalidIdx && _file_exists(p->fileA[ end_file_idx ])==false )
          {  
            end_file_idx = kInvalidIdx;
            end_msg_idx  = kInvalidIdx;
          }

          
          // calc the count of message in all the files
          p->msgAllocN = _calc_msg_count(p);

          // allocate a single array to hold the messages from all the files
          p->msgA = mem::resize<file_msg_t>(p->msgA,p->msgAllocN);

          // fill p->msgA[] from each of the files
          _fill_msg_array(p);

          // sort p->msgA[] on msgA[].amicro
          auto f = [](const file_msg_t& a0,const file_msg_t& a1) -> bool { return a0.amicro < a1.amicro; };
          std::sort(p->msgA,p->msgA+p->msgN,f);

          // by default we rewind to the first msg
          p->next_wr_msg_idx = 0;
          p->next_rd_msg_idx = 0;

          // if a valid seek position exists then reset it
          if( beg_file_idx != kInvalidIdx && beg_msg_idx != kInvalidIdx )
            if((rc = _seek_to_msg_index( p, beg_file_idx, beg_msg_idx )) != kOkRC )
              rc = cwLogError(rc,"The MIDI file device starting output message could not be restored.");

          if( end_file_idx != kInvalidIdx && end_msg_idx != kInvalidIdx )
            if((rc = _set_end_msg_index(p, end_file_idx, end_msg_idx )) != kOkRC )
              rc = cwLogError(rc,"The MIDI file device ending output message could not be restored.");
              
        
          return rc;        
        }

        void _update_active_flag( file_dev_t* p )
        {
            unsigned i=0;
            for(; i<p->fileN; ++i)
              if( _file_exists(p->fileA[i]) && p->fileA[i].enable_fl )
                break;
            
            p->is_activeFl = i < p->fileN;          
        }
      
        rc_t _enable_file(  handle_t h, unsigned file_idx, bool enable_fl )
        {
          rc_t rc;
  
          file_dev_t* p = _handleToPtr(h);
  
          if((rc = _validate_file_existence(p, file_idx)) != kOkRC )
            goto errLabel;

          p->fileA[ file_idx ].enable_fl = enable_fl;

          _update_active_flag(p);
          
        errLabel:

          if(rc != kOkRC )
            rc = cwLogError(rc,"MIDI file device %s failed on file index %i.", enable_fl ? "enable" : "disable", file_idx );
        
          return rc;
        }

        void _callback( file_dev_t* p, unsigned file_idx, msg_t* msgA, unsigned msgN )
        {
          if( p->cbFunc != nullptr )
          {
            packet_t pkt = {};
            pkt.cbArg    = p->cbArg;
            pkt.devIdx   = p->base_dev_idx;
            pkt.portIdx  = file_idx;
            pkt.msgArray = msgA;
            pkt.msgCnt   = msgN;
            
            p->cbFunc( &pkt, 1 );
          }
        }

        void _packetize_and_transmit_msgs( file_dev_t* p, unsigned xmt_msg_cnt  )
        {
          msg_t msgA[ xmt_msg_cnt ];
          
          unsigned pkt_msg_idx = 0;
          unsigned file_idx_0  = kInvalidIdx;

          assert( p->next_rd_msg_idx != kInvalidIdx && p->next_wr_msg_idx != kInvalidIdx );
          
          for(; p->next_rd_msg_idx < p->next_wr_msg_idx && pkt_msg_idx < xmt_msg_cnt; ++p->next_rd_msg_idx)            
          {
            const file_msg_t& m = p->msgA[ p->next_rd_msg_idx ];
            
            if( p->fileA[ m.file_idx ].enable_fl && m.msg != nullptr  )
            {
              // 
              if( p->latency_meas_enable_in_fl && isNoteOn(m.msg->status,m.msg->d1) )
              {
                p->latency_meas_enable_in_fl = false;
                time::get(p->latency_meas_result.note_on_input_ts);
              }
              
              // if the file_idx is not the same as the previous messages then
              // send the currently stored messages from msgA[] - because packet
              // messages must all belong to the same port
              if( file_idx_0 != kInvalidIdx && m.file_idx != file_idx_0 )
              {
                _callback(p,file_idx_0,msgA,pkt_msg_idx);
                pkt_msg_idx = 0;
              }
              
              msgA[pkt_msg_idx]  = *m.msg;
              file_idx_0         = m.file_idx;
              pkt_msg_idx       += 1;
            }
          }
          
          if( pkt_msg_idx > 0 )
            _callback(p,file_idx_0,msgA,pkt_msg_idx);
        }

        rc_t _load_messages( handle_t h, unsigned file_idx, const char* fname, const msg_t* msgA, unsigned msgN )
        {
          rc_t rc;
          file_dev_t* p = _handleToPtr(h);

          if((rc = _validate_file_index(p,file_idx)) != kOkRC )
            goto errLabel;

          if((rc = _close_file(p,file_idx)) != kOkRC )
            goto errLabel;

          if( fname != nullptr )
          {
            if((rc = _open_midi_file(p,file_idx,fname)) != kOkRC )
              goto errLabel;            
          }
          else
          {
            if( msgA != nullptr )
            {
              p->fileA[ file_idx ].msgA = mem::allocZ<msg_t>(msgN);
              p->fileA[ file_idx ].msgN = msgN;
              memcpy(p->fileA[ file_idx ].msgA, msgA, msgN*sizeof(msg_t));
            }
            else
            {
              assert(0);
            }
          }
          
          if((rc = _prepare_msg_array(p)) != kOkRC )
            goto errLabel;

          p->fileA[ file_idx ].enable_fl = true;
  
        errLabel:

          _update_active_flag(p);
  
          if( rc != kOkRC )
            rc = cwLogError(rc,"MIDI file device msg. load port failed %i.",file_idx);
  
          return rc;  
        }
        
      }
    }
  } 
}


cw::rc_t cw::midi::device::file_dev::create( handle_t&   hRef,
                                             cbFunc_t    cbFunc,
                                             void*       cbArg,
                                             unsigned    baseDevIdx,
                                             const char* labelA[],
                                             unsigned    max_file_cnt,
                                             const char* dev_name,
                                             unsigned    read_ahead_micros)
{
  rc_t rc;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  file_dev_t* p = mem::allocZ<file_dev_t>();

  p->cbFunc                   = cbFunc;
  p->cbArg                    = cbArg;
  p->fileN                    = max_file_cnt;
  p->fileA                    = mem::allocZ<file_t>(p->fileN);
  p->devCnt                   = 1;
  p->is_activeFl              = false;
  p->read_ahead_micros        = read_ahead_micros;
  p->base_dev_idx             = baseDevIdx;
  p->dev_name                 = mem::duplStr(dev_name);

  _reset_indexes(p);
  
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

  hRef.set(p);

errLabel:
  if(rc != kOkRC )
    _destroy(p);
  return rc;
}

cw::rc_t cw::midi::device::file_dev::destroy( handle_t& hRef )
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

bool cw::midi::device::file_dev::is_active( handle_t h )
{
  file_dev_t* p = _handleToPtr(h);  
  return p->is_activeFl;
}

unsigned cw::midi::device::file_dev::file_count( handle_t h )
{
  file_dev_t* p = _handleToPtr(h);
  return p->fileN;
}

cw::rc_t cw::midi::device::file_dev::open_midi_file( handle_t h, unsigned file_idx, const char* fname )
{
  return _load_messages(h,file_idx,fname,nullptr,0);
}

cw::rc_t cw::midi::device::file_dev::load_messages( handle_t h, unsigned file_idx, const msg_t* msgA, unsigned msgN )
{
  return _load_messages(h,file_idx,nullptr,msgA,msgN);
}


cw::rc_t cw::midi::device::file_dev::enable_file(  handle_t h, unsigned file_idx, bool enableFl )
{ return _enable_file(h,file_idx,enableFl); }

cw::rc_t cw::midi::device::file_dev::enable_file(  handle_t h, unsigned file_idx )
{ return _enable_file(h,file_idx,true); }

cw::rc_t cw::midi::device::file_dev::disable_file( handle_t h,unsigned file_idx )
{ return _enable_file(h,file_idx,false); }


unsigned    cw::midi::device::file_dev::count( handle_t h )
{
  file_dev_t* p = _handleToPtr(h);
  return p->devCnt;  
}

const char* cw::midi::device::file_dev::name( handle_t h, unsigned devIdx )
{
  file_dev_t* p = _handleToPtr(h);

  return _validate_dev_index(p,devIdx)==kOkRC ? p->dev_name : nullptr;
}

unsigned    cw::midi::device::file_dev::nameToIndex(handle_t h, const char* deviceName)
{
  file_dev_t* p = _handleToPtr(h);  
  return textIsEqual(deviceName,p->dev_name) ? 0 : kInvalidIdx;    
}

unsigned    cw::midi::device::file_dev::portCount(  handle_t h, unsigned devIdx, unsigned flags )
{
  file_dev_t* p = _handleToPtr(h);

  if(_validate_dev_index(p,devIdx) != kOkRC )
    return 0;
  
  return flags & kInMpFl ? p->fileN : 0;
}

const char* cw::midi::device::file_dev::portName(   handle_t h, unsigned devIdx, unsigned flags, unsigned portIdx )
{
  file_dev_t* p = _handleToPtr(h);

  if( _validate_dev_index(p,devIdx) != kOkRC )
    return nullptr;

  if( _validate_port_index(p,portIdx) != kOkRC )
    return nullptr;

  return p->fileA[portIdx].label;
}

unsigned    cw::midi::device::file_dev::portNameToIndex( handle_t h, unsigned devIdx, unsigned flags, const char* portName )
{
  file_dev_t* p = _handleToPtr(h);

  if( _validate_dev_index(p,devIdx) != kOkRC )
    return kInvalidIdx;

  if( flags & kInMpFl )
  {
    for(unsigned i=0; i<p->fileN; ++i)
      if( textIsEqual(p->fileA[i].label,portName) )
        return i;
  }
  return kInvalidIdx;  
}

cw::rc_t  cw::midi::device::file_dev::portEnable( handle_t h, unsigned devIdx, unsigned flags, unsigned portIdx, bool enableFl )
{
  rc_t        rc = kOkRC;
  file_dev_t* p = _handleToPtr(h);
  
  if((rc = _validate_dev_index(p,devIdx)) != kOkRC )
    goto errLabel;
  
  if( flags & kInMpFl )
  {
    rc = enable_file(h,portIdx,enableFl);
  }
  else
  {
    rc = cwLogError(kNotImplementedRC,"MIDI file dev output enable/disable has not been implemented.");
  }

    
errLabel:
  if(rc != kOkRC )
    rc = cwLogError(rc,"MIDI file dev port enable/disable failed.");
  
  return rc;
}

unsigned cw::midi::device::file_dev::msg_count( handle_t h, unsigned file_idx )
{
  file_dev_t* p = _handleToPtr(h);
  rc_t rc;
  
  if((rc = _validate_file_existence(p,file_idx)) != kOkRC )
    goto errLabel;
  
  return p->msgN;
    
errLabel:
  return 0;
}

cw::rc_t cw::midi::device::file_dev::seek_to_msg_index( handle_t h, unsigned file_idx, unsigned msg_idx )
{
  file_dev_t* p = _handleToPtr(h);

  return _seek_to_msg_index(p,file_idx,msg_idx);
}

cw::rc_t cw::midi::device::file_dev::set_end_msg_index( handle_t h, unsigned file_idx, unsigned msg_idx )
{
  file_dev_t* p = _handleToPtr(h);
  return _set_end_msg_index(p, file_idx, msg_idx );
}
  


cw::rc_t cw::midi::device::file_dev::rewind( handle_t h )
{
  file_dev_t* p = _handleToPtr(h);

  if( p->beg_msg_idx == kInvalidIdx )
  {
    p->next_wr_msg_idx = 0;
    p->next_rd_msg_idx = 0;    
  }
  else
  {
    p->next_wr_msg_idx = p->beg_msg_idx;
    p->next_rd_msg_idx = p->beg_msg_idx;
  }
  
  return kOkRC;
}

cw::rc_t cw::midi::device::file_dev::set_start_delay(  handle_t h, unsigned start_delay_micros )
{
  file_dev_t* p = _handleToPtr(h);

  p->start_delay_micros = start_delay_micros;

  return kOkRC;
  
}

cw::midi::device::file_dev::exec_result_t cw::midi::device::file_dev::exec( handle_t h, unsigned long long cur_time_us )
{
  exec_result_t r;
  
  file_dev_t*p = _handleToPtr(h);

  // p->end_msg_idx indicates the last msg to transmit, but we wait until the msg following p->end_msg_idx to
  // actually stop transmitting and set r.eof_fl.  This will facilitate providing a natural time gap to loop to the beginning.
  unsigned end_msg_idx  = p->end_msg_idx == kInvalidIdx ? p->msgN-1 : p->end_msg_idx;

  
  // if there are no messages left to send
  if( p->next_wr_msg_idx==kInvalidIdx || p->next_wr_msg_idx >= p->msgN || p->next_wr_msg_idx > p->end_msg_idx )
  {
    r.next_msg_wait_micros = 0;
    r.xmit_cnt = 0;
    r.eof_fl = true;
  }
  else
  {

    if( cur_time_us < p->start_delay_micros )
    {
      r.xmit_cnt = 0;
      r.eof_fl   = false;
      r.next_msg_wait_micros = p->start_delay_micros - cur_time_us;
    }
    else
    {
      cur_time_us -= p->start_delay_micros;
      
      unsigned            base_msg_idx = p->beg_msg_idx == kInvalidIdx ? 0         : p->beg_msg_idx;
      
      assert( base_msg_idx <= p->next_wr_msg_idx );
      
      unsigned long long  msg_time_us  = p->msgA[ p->next_wr_msg_idx ].amicro - p->msgA[ base_msg_idx ].amicro;
      unsigned            xmit_cnt     = 0;
      unsigned long long  end_time_us  = cur_time_us + p->read_ahead_micros;
      
      // for all msgs <= current time + read_ahead_micros
      while( msg_time_us <= end_time_us )
      {
        //
        // consume msg here
        //

        xmit_cnt += 1;
      
        // advance to next msg
        p->next_wr_msg_idx += 1; 
            
        // check for EOF
        if( p->next_wr_msg_idx >= p->msgN)
          break;

        // time of next msg
        msg_time_us = p->msgA[ p->next_wr_msg_idx ].amicro - p->msgA[ base_msg_idx ].amicro;

        // if we went past the end msg then stop
        if( p->next_wr_msg_idx > end_msg_idx )
          break;
      }

      assert( p->next_wr_msg_idx >= p->msgN || msg_time_us > cur_time_us );
    
      r.xmit_cnt             = xmit_cnt;
      r.eof_fl               = p->next_wr_msg_idx >= p->msgN;
      r.next_msg_wait_micros = r.eof_fl ? 0 : msg_time_us - cur_time_us;
    
      // callback with output msg's
      if( xmit_cnt )
        _packetize_and_transmit_msgs( p, xmit_cnt  );
    }
    
  }

  return r;
          
}


cw::rc_t    cw::midi::device::file_dev::send( handle_t h, unsigned devIdx, unsigned portIdx, uint8_t st, uint8_t d0, uint8_t d1 )
{
  return cwLogError(kNotImplementedRC,"MIDI file dev send() not implemented.");

  file_dev_t* p  = _handleToPtr(h);
  
  if( p->latency_meas_enable_out_fl && isNoteOn(st,d1) )
  {
    p->latency_meas_enable_out_fl = false;
    time::get(p->latency_meas_result.note_on_output_ts);
  }
  
}

cw::rc_t    cw::midi::device::file_dev::sendData( handle_t h, unsigned devIdx, unsigned portIdx, const uint8_t* dataPtr, unsigned byteCnt )
{
  return cwLogError(kNotImplementedRC,"MIDI file dev send() not implemented."); 
}

void cw::midi::device::file_dev::latency_measure_reset(handle_t h)
{
  file_dev_t* p  = _handleToPtr(h);
  
  p->latency_meas_result.note_on_input_ts = {};
  p->latency_meas_result.note_on_output_ts = {};
  p->latency_meas_enable_in_fl           = true;
  p->latency_meas_enable_out_fl          = true;
}

cw::midi::device::latency_meas_result_t cw::midi::device::file_dev::latency_measure_result(handle_t h)
{
  file_dev_t* p  = _handleToPtr(h);
  return p->latency_meas_result;
}


void cw::midi::device::file_dev::report( handle_t h, textBuf::handle_t tbH)
{
  file_dev_t* p = _handleToPtr(h);

  print(tbH,"%i : Device: '%s'\n",p->base_dev_idx,p->dev_name);
  
  if( p->fileN )
    print(tbH,"  Input:\n");
  
  
  for(unsigned i=0; i<p->fileN; ++i)
  {
    const char* fname = "<none>";
    
    if( p->fileA[i].fname != nullptr )
    {
      fname = p->fileA[i].fname;
    }
    else
      if( p->fileA[i].msgA != nullptr )
      {
        fname = "msg array";
      }
    
    print(tbH,"               port:%i    '%s' ena:%i msg count:%i %s\n",
          i,
          cwStringNullGuard(p->fileA[i].label),
          p->fileA[i].enable_fl,
          p->fileA[i].msgN,
          fname );
  }
  
}


cw::rc_t cw::midi::device::file_dev::test( const object_t* cfg )
{
  rc_t        rc       = kOkRC;
    
  
  return rc;
  
}
