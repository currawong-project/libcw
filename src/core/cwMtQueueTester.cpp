#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwObject.h"
#include "cwMath.h"

#include "cwNbMpScQueue.h"
#include "cwMpScNbCircQueue.h"
#include "cwMtQueueTester.h"

#include "cwThread.h"
#include "cwThreadMach.h"
#include "cwFile.h"


namespace cw
{
  namespace mt_queue_tester
  {
    struct shared_str;
    
    typedef struct test_str
    {
      unsigned           id;    // thread id
      unsigned           iter;  // execution counter
      unsigned           value; // 
      struct shared_str* share; // pointer to global shared data
    } test_t;

    typedef test_t cq_data_t;
    
    typedef struct mp_sc_nb_circ_queue::cq_str<cq_data_t> cq_t;
    
    typedef struct shared_str
    {
      nbmpscq::handle_t     qH;
      cq_t*                 cq;
      std::atomic<unsigned> cnt;
      
    } test_share_t;

      
    
    bool _nbmpscq_threadFunc( void* arg )
    {
      test_t* t = (test_t*)arg;

      // get and increment a global shared counter
      t->value = t->share->cnt.fetch_add(1,std::memory_order_acq_rel);

      // push the current thread instance record
      push(t->share->qH,t,sizeof(test_t));

      // incrmemen this threads exec. counter
      t->iter += 1;
      
      sleepMs( rand() & 0xf );
      
      return true;
    }

    void nbmpscq_main( file::handle_t fH, nbmpscq::handle_t qH )
    {
      nbmpscq::blob_t b = get(qH);
      if( b.blob != nullptr )
      {
        test_t* t =  (test_t*)b.blob;
        printf(fH,"%i %i %i %i\n",t->id,t->iter,t->value,b.blobByteN);
        advance(qH);
      }
      
    }

    bool _cq_threadFunc( void* arg )
    {
      rc_t rc = kOkRC;
      test_t* t = (test_t*)arg;

      // get and increment a global shared counter
      t->value = t->share->cnt.fetch_add(1,std::memory_order_acq_rel);

      // push the current thread instance record
      if((rc = mp_sc_nb_circ_queue::push<cq_data_t>(t->share->cq, *t )) != kOkRC )
      {
        cwLogError(rc,"Circular queue is full.");
      }

      // incrmement this threads exec. counter
      t->iter += 1;
      
      sleepUs( rand() & 0xf );
      
      return true;
    }
    
    unsigned fail_N = 0;
    void cq_main( file::handle_t fH, cq_t* cq )
    {
      
      cq_data_t t;

      unsigned res_cnt = cq->res_cnt.load();
      
      if( mp_sc_nb_circ_queue::pop<cq_data_t>(cq, t ) == kOkRC )
      {
        printf(fH,"%i %i %i %i\n",t.id,t.iter,t.value,res_cnt);
        
        fail_N = 0;
      }
      else
      {
        if( fail_N < 10 )
        {
          //printf(fH,"F: %i %i : %i %i\n",res_idx,res_cnt,cq->tail_idx,value);
          fail_N += 1;
        }
      }
    }    
  }

  rc_t _check_results( const char* fname )
  {
    rc_t           rc        = kOkRC;
    unsigned       lineN     = 0;
    unsigned*      valueA    = nullptr;
    char*          lineBuf   = nullptr;
    unsigned       lineCharN = 0;
    file::handle_t fH;

    cwLogInfo("Validation started ...");
    
    if((rc = open(fH,fname,file::kReadFl)) != kOkRC )
    {
      rc = cwLogError(rc,"Result file open failed on '%s'.",fname);
      goto errLabel;
    }

    if((rc = file::lineCount(fH,lineN)) != kOkRC )
    {
      rc = cwLogError(rc,"Line count could not be deteremined on '%s'.",fname);
      goto errLabel;
    }

    if( lineN == 0 )
    {
      rc = cwLogError(rc,"Empty file detected on '%s'.",fname);
      goto errLabel;
    }

    lineN -= 1;
    
    valueA = mem::allocZ<unsigned>(lineN);

    for(unsigned i=0; i<lineN; ++i)
    {
      unsigned v0,v1,v2,v3;
      
      if((rc = getLineAuto(fH,&lineBuf,lineCharN)) != kOkRC )
      {
        rc = cwLogError(rc,"Line buffer load failed on line index %i of %i in '%s'.",i,lineN,fname);
        goto errLabel;
      }

      if( lineCharN == 0 )
      {
        rc = cwLogError(rc,"A blank line was encountered at line index %i in '%s'.",i,fname);
        goto errLabel;
      }

      if( sscanf(lineBuf,"%i %i %i %i",&v0,&v1,&v2,&v3) != 4 )
      {
        rc = cwLogError(rc,"Line parse failed at line index %i in '%s'.",i,fname);
        goto errLabel;
      }

      valueA[i] = v2;
    }
    
    std::sort( valueA, valueA+lineN, [](auto a, auto b){return a<b;});

    for(unsigned i=0,j=0; i<lineN; ++i,++j)
      if( valueA[i] != j )
      {
        unsigned k = j;
        if(i+1 < lineN)
          k = valueA[i+1] - 1;
        
        cwLogInfo("Missing: %i to %i",j,k);

        j = k;
      } 

    cwLogInfo("Validation complete : %i rows.",lineN);

  errLabel:
    close(fH);
    mem::release(valueA);
    mem::release(lineBuf);

    return rc;
  }
}


cw::rc_t cw::mt_queue_tester::test( const object_t* cfg )
{
  rc_t rc=kOkRC,rc0,rc1,rc2,rc3;

  unsigned              threadN    = 2;
  test_t*               threadA    = nullptr;
  unsigned              blkN       = 2;
  unsigned              blkByteN   = 1024;
  unsigned              cqEleCnt   = 1024*64;
  bool                  cqFl       = false;
  const char*           out_fname  = nullptr;
  time::spec_t          t0         = time::current_time();
  unsigned              testDurMs  = 0;
  thread::cbFunc_t      thread_func= nullptr;
  test_share_t          share;
  nbmpscq::handle_t     qH;
  thread_mach::handle_t tmH;
  file::handle_t        fH;

  if((rc = cfg->getv("blkN",blkN,
                     "blkByteN",blkByteN,
                     "circQueueEleCnt",cqEleCnt,
                     "circQueueFl",cqFl,
                     "testDurMs",testDurMs,
                     "threadN",threadN,
                     "out_fname",out_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"Test params parse failed.");
    goto errLabel;
  }

  if((rc = file::open(fH,out_fname,file::kWriteFl)) != kOkRC )
  {
    rc = cwLogError(rc,"Error creating the output file:%s",cwStringNullGuard(out_fname));
    goto errLabel;
  }
  
  if( threadN == 0 )
  {
    rc = cwLogError(kInvalidArgRC,"The 'threadN' parameter must be greater than 0.");
    goto errLabel;
  }

  // create the thread intance records
  threadA = mem::allocZ<test_t>(threadN);

  // create the cwNbMpScQueue queue
  if((rc = create( qH, blkN, blkByteN )) != kOkRC )
  {
    rc = cwLogError(rc,"nbmpsc create failed.");
    goto errLabel;
  }

  thread_func = cqFl ? _cq_threadFunc : _nbmpscq_threadFunc;
  share.cq    = mp_sc_nb_circ_queue::create<cq_data_t>(cqEleCnt); // create the circular queue
  share.qH    = qH;
  share.cnt.store(0);
  
  for(unsigned i=0; i<threadN; ++i)
  {
    threadA[i].id    = i;
    threadA[i].share = &share;
  }
  
  // create the thread machine 
  if((rc = thread_mach::create( tmH, thread_func, threadA, sizeof(test_t), threadN )) != kOkRC )
  {
    rc = cwLogError(rc,"Thread machine create failed.");
    goto errLabel;
  }

  // start the thread machine
  if((rc = thread_mach::start(tmH)) != kOkRC )
  {
    cwLogError(rc,"Thread machine start failed.");
    goto errLabel;
  }

  // run the test for 'testDurMs' milliseconds
  while( time::elapsedMs(t0) < testDurMs )
  {
    if( cqFl )
      cq_main(fH,share.cq);
    else
      nbmpscq_main(fH,qH);
    
  }
 
 errLabel:
  file::close(fH);
  
  if((rc0 = thread_mach::destroy(tmH)) != kOkRC )
    cwLogError(rc0,"Thread machine destroy failed.");

  if((rc1 = destroy(qH)) != kOkRC )
    cwLogError(rc1,"nbmpsc queue destroy failed.");

  if((rc2 = destroy(share.cq)) != kOkRC )
    cwLogError(rc2,"mp_sc_nb_circ_queue destroy failed.");

  if((rc3 = _check_results(out_fname)) != kOkRC )
    cwLogError(rc3,"Check failed.");

  mem::release(threadA);
  
  return rcSelect(rc,rc0,rc1,rc2,rc3);  
  
}

