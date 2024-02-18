#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwObject.h"
#include "cwText.h"
#include "cwTextBuf.h"
#include "cwThread.h"

#include "cwMidi.h"
#include "cwMidiDecls.h"
#include "cwMidiFile.h"
#include "cwMidiDevice.h"
#include "cwMidiDeviceTest.h"

namespace cw
{
  namespace midi
  {
    namespace device
    {
      typedef struct test_msg_str
      {
        msg_t        msg;
        time::spec_t t;
      } test_msg_t;
      
      typedef struct test_str
      {
        test_msg_t* msgA;
        unsigned    msgN;
        unsigned    msg_idx;
        unsigned    file_dev_idx;
        unsigned    port_idx;
      } test_t;

      rc_t _test_create( test_t*& pRef )
      {
        rc_t    rc = kOkRC;
        test_t* p  = nullptr;
        
        p = mem::allocZ<test_t>();

        p->msgN         = 0;
        p->msgA         = nullptr;
        p->file_dev_idx = kInvalidIdx;
        p->port_idx     = kInvalidIdx;
        pRef            = p;

        return rc;
      }

      rc_t _test_open( test_t* p, unsigned fileDevIdx, unsigned portIdx, const char* fname )
      {
        rc_t           rc = kOkRC;
        
        if( p->file_dev_idx == kInvalidIdx )
        {
          file::handle_t mfH;
        
          if((rc = open(mfH,fname)) != kOkRC )
            goto errLabel;

          p->msgN         = msgCount(mfH);
          p->msgA         = mem::allocZ<test_msg_t>(p->msgN);
          p->file_dev_idx = fileDevIdx;
          p->port_idx     = portIdx;

          close(mfH);

        }
        
      errLabel:
        return rc;
      }

      rc_t _test_destroy( test_t* p )
      {
        if( p != nullptr )
        {
          mem::release(p->msgA);
          mem::release(p);
        }

        return kOkRC;
      }
      
      void _test_callback( const packet_t* pktArray, unsigned pktCnt )
      {
        unsigned i,j;
        time::spec_t cur_time = time::current_time();
        
        for(i=0; i<pktCnt; ++i)
        {
          const packet_t* p = pktArray + i;

          test_t* t = (test_t*)p->cbArg;

          for(j=0; j<p->msgCnt; ++j)
            if( p->msgArray != NULL )
            {
              if( t->msg_idx < t->msgN && p->devIdx == t->file_dev_idx && p->portIdx == t->port_idx )
              {
                t->msgA[t->msg_idx].msg = p->msgArray[j];
                t->msgA[t->msg_idx].t   = cur_time;
                t->msg_idx += 1;
              }
                  
              if( isNoteOn(p->msgArray[j].status,p->msgArray[j].d1) )
                printf("%ld %ld %i 0x%x %i %i\n", p->msgArray[j].timeStamp.tv_sec, p->msgArray[j].timeStamp.tv_nsec, p->msgArray[j].ch, p->msgArray[j].status,p->msgArray[j].d0, p->msgArray[j].d1);
            }
            else
            {
              printf("0x%x ",p->sysExMsg[j]);
            }
        }
      }

      bool _test_is_not_equal( const file::trackMsg_t* tmsg, const test_msg_t& m )
      { return tmsg->status != m.msg.status || tmsg->u.chMsgPtr->d0 != m.msg.d0 || tmsg->u.chMsgPtr->d1 != m.msg.d1; }
      
      bool _test_is_equal( const file::trackMsg_t* tmsg, const test_msg_t& m )
      { return !_test_is_not_equal(tmsg,m); }
      
      void _test_print( const file::trackMsg_t* tmsg, const test_msg_t& m )
      {
        const char* eql_mark = _test_is_equal(tmsg,m) ? "" : "*";
        printf("%2i 0x%2x %3i %3i : %2i 0x%2x %3i %3i : %s\n",tmsg->u.chMsgPtr->ch, tmsg->status, tmsg->u.chMsgPtr->d0, tmsg->u.chMsgPtr->d1, m.msg.ch, m.msg.status, m.msg.d0, m.msg.d1, eql_mark);
      }

      void _test_print( unsigned long long t0, const file::trackMsg_t* tmsg, unsigned long long t1, const test_msg_t& m, unsigned dt )
      {
        const char* eql_mark = _test_is_equal(tmsg,m) ? "" : "*";
        printf("%6llu %2i 0x%2x %3i %3i : %6llu %2i 0x%2x %3i %3i : %6i : %s\n",t0, tmsg->u.chMsgPtr->ch, tmsg->status, tmsg->u.chMsgPtr->d0, tmsg->u.chMsgPtr->d1, t1, m.msg.ch, m.msg.status, m.msg.d0, m.msg.d1, dt, eql_mark);
      }
      


      rc_t _test_analyze( test_t* p, const char* fname )
      {
        rc_t rc = kOkRC;

        file::handle_t           mfH;
        const file::trackMsg_t** tmsgA;
        unsigned                 tmsgN;
        unsigned                 max_diff_micros = 0;
        unsigned                 sum_micros = 0;
        unsigned                 sum_cnt = 0;
        unsigned                 i0 = kInvalidIdx;
        unsigned                 j0 = kInvalidIdx;

        // open the MIDI file under test
        if((rc = open(mfH,fname)) != kOkRC )
          goto errLabel;

        tmsgA = msgArray(mfH);
        tmsgN = msgCount(mfH);

        printf("file:%i test:%i\n",tmsgN,p->msg_idx);

        // for file trk msg and recorded msg
        for(unsigned i=0,j=0; i<tmsgN && j<p->msg_idx; ++i)
        {
          // skip non-channel messages
          if( isChStatus(tmsgA[i]->status)) 
          {

            unsigned long long d0 = 0;
            unsigned long long d1 = 0;
            unsigned           dt = 0;

            // if there is a previous file msg
            if( i0 != kInvalidIdx )
            {
              // get the elapsed time between the cur and prev file msg
              d0 = tmsgA[i]->amicro - tmsgA[i0]->amicro;

              // if there is a previous recorded msg
              if( j0 != kInvalidIdx )
              {
                // get the time elapsed between the cur and prev recorded msg
                d1 = time::elapsedMicros(p->msgA[j0].t,p->msgA[j].t);

                dt  = (unsigned)(d0>d1 ? d0-d1 : d1-d0);

                sum_micros += dt;
                sum_cnt    += 1;
                
                if( dt > max_diff_micros )
                  max_diff_micros = dt;
                                
              }
            }
            
            _test_print(d0, tmsgA[i], d1, p->msgA[j], dt );

            i0 = i;
            j0 = j;
            j += 1;
          }
        }
        
        printf("max diff:%i avg diff:%i micros\n",max_diff_micros,sum_cnt==0 ? 0 : sum_micros/sum_cnt);
        
      errLabel:
        close(mfH);

        return rc;
      }

      
    }
  }
}

cw::rc_t cw::midi::device::test( const object_t* cfg )
{
  rc_t            rc               = kOkRC;
  const char*     testMidiFname    = nullptr;
  const char*     fileDevName      = nullptr;
  const char*     testFileLabel    = nullptr;
  bool            testFileEnableFl = false;
  const object_t* file_ports       = nullptr;
  test_t*         t                = nullptr;
  bool            quit_fl          = false;
  char            ch;
  handle_t        h;


  if((rc = _test_create( t )) != kOkRC )
  {
    rc = cwLogError(rc,"Test create failed.");
    goto errLabel;
  }
  
  if((rc = create(h,_test_callback,t,cfg)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI dev create failed.");
    goto errLabel;
  }

  report(h);

  if((rc = cfg->getv("fileDevName",      fileDevName,
                     "testFileLabel",    testFileLabel,
                     "testFileEnableFl", testFileEnableFl,
                     "file_ports",       file_ports)) != kOkRC )
  {
    rc = cwLogError(rc,"Parse 'file_ports' failed.");
    goto errLabel;
  }


  // for each file dev port
  for(unsigned i=0; i<file_ports->child_count(); ++i)
  {
    const char* fname      = nullptr;
    const char* label      = nullptr;
    bool        enable_fl  = false;
    unsigned    fileDevIdx = kInvalidIdx;
    unsigned    portIdx    = kInvalidIdx;

    // parse the file/label pair
    if((rc = file_ports->child_ele(i)->getv("file",fname,
                                            "enable_fl", enable_fl,
                                            "label",label)) != kOkRC )
    {
      rc = cwLogError(rc,"Parse failed on 'file_port' index %i.",i);
      goto errLabel;
    }

    // get the file device name
    if((fileDevIdx = nameToIndex(h,fileDevName)) == kInvalidIdx )
    {
      rc = cwLogError(kInvalidArgRC,"Unable to locate the MIDI file device '%s'.",cwStringNullGuard(fileDevName));
      goto errLabel;
    }

    // get the file/label port index
    if((portIdx = portNameToIndex(h,fileDevIdx,kInMpFl,label)) == kInvalidIdx )
    {
      rc = cwLogError(kInvalidArgRC,"Unable to locate the port '%s' on device '%s'.",cwStringNullGuard(label),cwStringNullGuard(fileDevName));
      goto errLabel;
    }

    // open the MIDI file on this port
    if((rc = openMidiFile(h,fileDevIdx,portIdx,fname)) != kOkRC )
    {
      rc = cwLogError(rc,"MIDI file open failed on '%s'.",fname);
      goto errLabel;
    }

    if((rc = portEnable(h,fileDevIdx,kInMpFl,portIdx,enable_fl)) != kOkRC )
    {
      rc = cwLogError(rc,"MIDI file enable failed on '%s'.",fname);
      goto errLabel;      
    }

    // if this is the test port
    if( testFileEnableFl && testFileLabel != nullptr && textIsEqual(label,testFileLabel) )
    {
      testMidiFname = fname;

      cwLogInfo("Test label:%s device:%i fname:%s",testFileLabel,fileDevIdx,fname);
      
      if((rc = _test_open(t,fileDevIdx,portIdx,fname)) != kOkRC )
      {
        rc = cwLogError(rc,"Test create failed.");
        goto errLabel;
      }
    }    
  }
  
  cwLogInfo("menu: (q)uit (b)egin (s)top (p)ause (u)npause (n)ote-on\n");

  while( !quit_fl)
  {
    ch = getchar();
    
    switch(ch)
    {
      case 'q':
        quit_fl = true;
        break;
        
      case 'b':
        printf("starting ...\n");
        start(h);
        break;
        
      case 's':
        printf("stopping ...\n");
        stop(h);
        break;
        
      case 'p':
        printf("pausing ...\n");        
        pause(h,true);
        break;
        
      case 'u':
        printf("unpausing ...\n");        
        pause(h,false);
        break;
        
      case 'n':
        printf("sending ...\n");
        send(h,2,0,0x90,60,60);
        break;
    }
  } 

  
errLabel:

  if( testMidiFname != nullptr )
    _test_analyze(t,testMidiFname);
  
  destroy(h);
  _test_destroy(t);
  
  return rc;
}


cw::rc_t cw::midi::device::testReport()
{
  rc_t              rc              = kOkRC;
  textBuf::handle_t tbH;
  handle_t          h;

  if((rc = create(h,nullptr,nullptr,nullptr,0,"test_report")) != kOkRC )
    return rc;

  // create a text buffer to hold the MIDI system report text
  if((rc = textBuf::create(tbH)) != kOkRC )
    goto errLabel;
  
  // generate and print the MIDI system report
  report(h,tbH);
  cwLogInfo("%s",textBuf::text(tbH));

errLabel:
  textBuf::destroy(tbH);
  destroy(h);
  return rc;
  
}
