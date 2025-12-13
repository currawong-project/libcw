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
#include "cwAudioFile.h"
#include "cwUtility.h"
#include "cwFileSys.h"
#include "cwAudioFileOps.h"
#include "cwVectOps.h"
#include "cwMath.h"
#include "cwDspTypes.h"
#include "cwDsp.h"
#include "cwAudioTransforms.h"

namespace cw
{
  namespace dsp
  {

    namespace wnd_func
    {

      idLabelPair_t wndLabelArray[] =
      {
        { kHannWndId,       "hann" },
        { kHammingWndId,    "hamming" },
        { kTriangleWndId,   "triangle" },
        { kKaiserWndId,     "kaiser" },
        { kHannMatlabWndId, "hann_matlab" },
        { kUnityWndId,      "unity" },
        { kInvalidWndId,    "<invalid>" }
      };
            
      const char* wndIdToLabel( unsigned id )
      {  return cw::idToLabel( wndLabelArray, id, kInvalidWndId );  }
      
      unsigned    wndLabelToId( const char* label )
      {  return cw::labelToId( wndLabelArray, label, kInvalidWndId ); }
      
      rc_t _test( const char* windowLabel, const double* wndV, unsigned maxWndN, unsigned wndN )
      {
        rc_t rc = kOkRC;
        wnd_func::fobj_t* p = nullptr;

        unsigned wndId = wndLabelToId( windowLabel );
        
        if((rc = create(p,wndId,maxWndN,wndN,3)) == kOkRC )
        {
          vop::print(p->wndV, p->wndN, "%f ", windowLabel);

          printf("diff: %f\n", vop::sum_sq_diff( wndV, p->wndV, wndN));
          
          destroy(p);                     
        }

        return rc;
      }
      
      rc_t test()
      {
        double hann_15[] = { 0.0, 0.04951557, 0.1882551 , 0.38873953, 0.61126047, 0.8117449,  0.95048443, 1.0,        0.95048443, 0.8117449, 0.61126047, 0.38873953, 0.1882551, 0.04951557, 0.0 };
        double hann_16[] = { 0.0, 0.04322727, 0.1654347 , 0.3454915 , 0.55226423, 0.75,       0.9045085 , 0.9890738,  0.9890738,  0.9045085, 0.75,       0.55226423, 0.3454915, 0.1654347 , 0.04322727,  0.0 };
        double hamm_15[] = { 0.08,0.12555432, 0.25319469, 0.43764037, 0.64235963, 0.82680531, 0.95444568, 1.0,        0.95444568, 0.82680531,0.64235963, 0.43764037, 0.25319469, 0.12555432, 0.08 };          
        double hamm_16[] = { 0.08,0.11976909, 0.23219992, 0.39785218, 0.58808309, 0.77,       0.91214782, 0.9899479,  0.9899479 , 0.91214782,0.77,       0.58808309, 0.39785218, 0.23219992, 0.11976909, 0.08};
        double tri_15[]  = { 0.0, 0.14285714, 0.28571429, 0.42857143, 0.57142857, 0.71428571, 0.85714286, 1.0,        0.85714286, 0.71428571,0.57142857, 0.42857143, 0.28571429, 0.14285714, 0.0 };
        double tri_16[]  = { 0.0, 0.13333333, 0.26666667, 0.4,        0.53333333, 0.66666667, 0.8,        0.93333333, 0.93333333, 0.8,       0.66666667, 0.53333333, 0.4,        0.26666667, 0.13333333, 0.0 };        
        double ones_5[]  = { 1.0, 1.0, 1.0, 1.0, 1.0 };
        
        rc_t rc = kOkRC;
        
        _test( "hann",    hann_15, 15, 15);
        _test( "hann",    hann_16, 16, 16);
        _test( "hamming", hamm_15, 15, 15);
        _test( "hamming", hamm_16, 16, 16);
        _test( "triangle", tri_15, 15, 15);
        _test( "triangle", tri_16, 16, 16);
        _test( "unity",    ones_5,  5,  5);

        if( rc != kOkRC )
          cwLogError(rc,"Window test failed.");
        return rc;
      }
    }

    namespace ola
    {
      rc_t test()
      {
        typedef float sample_t;
        
        rc_t     rc         = kOkRC;
        unsigned wndSmpCnt  = 16;
        unsigned hopSmpCnt  = 4;
        unsigned procSmpCnt = 2;
        unsigned wndTypeId  = wnd_func::kUnityWndId;
        unsigned hopCnt     = 8;
        unsigned oSmpCnt    = hopCnt * hopSmpCnt;
        unsigned i,j,k;
  
        sample_t  cV[] = { 1,1,1,1, 2,2,2,2, 3,3,3,3, 4,4,4,4, 4,4,4,4, 4,4,4,4, 4,4,4,4, 4,4,4,4 };
        sample_t* x = mem::allocZ<sample_t>(wndSmpCnt);
        sample_t* y = mem::allocZ<sample_t>(oSmpCnt);
        vop::fill(x,wndSmpCnt,1.0);

        ola::fobj_t* p = nullptr;

        if((rc = ola::create(p, wndSmpCnt, hopSmpCnt, procSmpCnt, wndTypeId )) == kOkRC )          
        {
          
          // Each iteration represents a single audio cylce 
          // which sources/sinks procSmpCnt samples
          for(i=0,j=0,k=0; k<oSmpCnt; i+=procSmpCnt)
          {
            j += procSmpCnt;

            // if there are hopSmpCnt new samples available then there must
            // be a new window of samples available - 
            if( j > hopSmpCnt )
            {
              j -= hopSmpCnt;
              ola::exec(p,x,p->wndSmpCnt);
            }

            const sample_t* op;

            // Get procSmpCnt samples from the output.
            if( (op=ola::execOut(p)) != NULL )
            {
              assert( y + k + p->procSmpCnt <= y + oSmpCnt );
              vop::copy<sample_t>(y+k, op, p->procSmpCnt);
              k += p->procSmpCnt;
            }
          }
        }

        vop::print(cV,oSmpCnt,"%f ","Correct ");
        vop::print(y, oSmpCnt,"%f ","Computed");
        printf("diff:%f\n", vop::sum_sq_diff(cV,y,oSmpCnt));
        
        ola::destroy(p);
        mem::release(x);
        mem::release(y);
        return rc;
        
      }
    }

    namespace shift_buf
    {
      rc_t  test()
      {
        rc_t rc = kOkRC;
        typedef float sample_t;
        
        sample_t m[] = 
          {
            1, 2, 3, 4, 5, 6, 7,
            7, 8, 9,10,11,12,13,
            13,14,15,16,17,18,19,
            19,20,21,22,23,24,25,
            25,26,27,28,29,30,31,
            31,32,33,34,35,36,37,
            37,38,39,40,41,42,43,
            43,44,45,46,47,48,49
          };

        unsigned    procSmpCnt =  5;   // count of samples to be fed to the shift buffer on each cycle
        unsigned    hopSmpCnt  =  6;   // count of samples between shift buffer outputs
        unsigned    maxWndSmpCnt= 7;
        unsigned    wndSmpCnt  =  7;   // count of samples in each shift buffer output

        unsigned    iSmpCnt    = 49;  // count of samples in the input test signal
        unsigned    oColCnt    = iSmpCnt / hopSmpCnt; 
        unsigned    oSmpCnt    = oColCnt * wndSmpCnt; // count of samples in the output test signal
        unsigned    i,j;

        shift_buf::fobj_t* p = nullptr;

        if((rc = shift_buf::create(p,procSmpCnt,maxWndSmpCnt,wndSmpCnt,hopSmpCnt)) == kOkRC )
        {
          sample_t* x = mem::allocZ<sample_t>(iSmpCnt);
          sample_t* y = mem::allocZ<sample_t>(oSmpCnt);

          vop::seq(x,iSmpCnt,1.0f,1.0f);
  
          for(i=0,j=0; i<iSmpCnt; i+=procSmpCnt)
          {
            // Give the shift buffer a block of procSmpCnt samples. 
            // If it returns 'true' then it has hopSmpCnt new samples and
            // a buffer of at least wndSmpCnt.
            for(; shift_buf::exec( p, x + i, procSmpCnt ); j+=wndSmpCnt )
            {

              // cmShiftBufExec() returned true then there are wndSmpCnt samples available.
              assert( y + j + wndSmpCnt <= y + oSmpCnt );
              vop::copy(y + j, p->outV, wndSmpCnt );

              vop::print(p->outV, wndSmpCnt, "%f ");
              vop::print(m + j,   wndSmpCnt, "%f ");
              
              if( !vop::is_equal(p->outV, m+j, wndSmpCnt ))
              {
                rc = cwLogError(kTestFailRC,"shift_buf test failed.");
                goto errLabel;
              }

            }
          }

          mem::release(x);
          mem::release(y);
          shift_buf::destroy(p);
          
        }

      errLabel:
        return rc;
      }
    }

    namespace pv_anl
    {
      rc_t  test()
      {
        rc_t            rc         = kOkRC;
        pv_anl::fobj_t* pva        = nullptr;
        pv_syn::fobj_t* pvs        = nullptr;
        unsigned        procSmpCnt = 0;
        float           srate      = 0;
        float           out_srate  = 0;
        unsigned        wndSmpCnt  = 0;
        unsigned        hopSmpCnt  = 0;
        unsigned        flags      = kCalcHzPvaFl;
        unsigned        wndTypeId  = wnd_func::kHannWndId;
        
        if((rc = create( pva, procSmpCnt, srate, wndSmpCnt, wndSmpCnt, hopSmpCnt, flags )) != kOkRC )
        {
        }

        if((rc = create( pvs, procSmpCnt, out_srate, wndSmpCnt, hopSmpCnt, wndTypeId )) != kOkRC )
        {
        }
        

        destroy(pva);
        destroy(pvs);
        
        return rc;
      }
      
    }

    namespace wt_osc
    {
      typedef float sample_t;
      typedef float srate_t;

      rc_t test()
      {
        rc_t     rc    = kOkRC;
        srate_t  srate = 8;
        sample_t aV[]  = { 7,0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,0 };
        
        struct wt_str<sample_t,srate_t> wt{
          .tid = kLoopWtTId,
          .cyc_per_loop = 0,
          .aV = aV,
          .aN = 16,
          .rms = 0,
          .hz  = 1,
          .srate = srate,
          .pad_smpN = 1,
          .posn_smp_idx = 0
        };

        struct obj_str<sample_t,srate_t> obj;
        init(&obj,&wt);

        unsigned yN = (int)(srate*2);
        sample_t yV[yN];
        unsigned actual = 0;
        process(&obj, yV, yN,actual);
        
        vop::print( yV, yN, "%f");
          
        return rc;
      }
      
    }

    namespace wt_seq_osc
    {
      typedef float sample_t;
      typedef float srate_t;

      rc_t test()
      {
        rc_t     rc    = kOkRC;
        srate_t  srate = 8;
        sample_t aV[]  = { 7,0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,0 };
        
        struct wt_osc::wt_str<sample_t,srate_t> wt{
          .tid = wt_osc::kOneShotWtTId,
          .cyc_per_loop = 0,
          .aV = aV,
          .aN = 16,
          .rms = 0,
          .hz  = 1,
          .srate = srate,
          .pad_smpN = 1,
          .posn_smp_idx = 0
        };

        struct wt_osc::wt_str<sample_t,srate_t> wt0 = wt;
        struct wt_osc::wt_str<sample_t,srate_t> wt1 = wt;
        struct wt_osc::wt_str<sample_t,srate_t> wt2 = wt;


        wt1.tid = wt_osc::kLoopWtTId;
        wt1.posn_smp_idx = 16;
        wt2.tid = wt_osc::kLoopWtTId;
        wt2.posn_smp_idx = 32;
        
        struct wt_osc::wt_str<sample_t,srate_t> wtA[] = {
          wt0,
          wt1,
          wt2
        };

        struct wt_seq_osc::wt_seq_str<sample_t,srate_t> wt_seq{
          .wtA = wtA,
          .wtN = 3
        };

        struct wt_seq_osc::obj_str<sample_t,srate_t> obj;
        init(&obj,&wt_seq);

        unsigned yN = (int)(srate*10);
        sample_t yV[yN];
        unsigned actual = 0;
        unsigned yi = 0;
        unsigned yDspSmpCnt = 8;
        while( yi < yN && is_init(&obj) )
        {
          unsigned frmSmpN = std::min(yN-yi,yDspSmpCnt);          
          process(&obj, yV+yi, frmSmpN, actual);
          assert( actual == frmSmpN );
          yi += actual;
        }
        
        vop::print( yV, yi, "%5.3f",nullptr,8);
          
        return rc;
      }
      
    }

    namespace multi_ch_wt_seq_osc
    {
      typedef float sample_t;
      typedef float srate_t;

      rc_t test()
      {
        rc_t     rc    = kOkRC;
        unsigned chN   = 2;
        srate_t  srate = 8;
        sample_t a0V[]  = { 7,0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,0 };
        sample_t a1V[]  = { 17,10,11,12,13,14,15,16,17,10,11,12,13,14,15,16,17,10 };
        sample_t a2V[]  = { 27,20,21,22,23,24,25,26,27,20,21,22,23,24,25,26,27,20 };
        
        struct wt_osc::wt_str<sample_t,srate_t> wt{
          .tid = wt_osc::kOneShotWtTId,
          .cyc_per_loop = 0,
          .aV = a0V,
          .aN = 16,
          .rms = 0,
          .hz  = 1,
          .srate = srate,
          .pad_smpN = 1,
          .posn_smp_idx = 0
        };

        struct wt_osc::wt_str<sample_t,srate_t> wt0 = wt;
        struct wt_osc::wt_str<sample_t,srate_t> wt1 = wt;
        struct wt_osc::wt_str<sample_t,srate_t> wt2 = wt;


        wt1.tid = wt_osc::kLoopWtTId;
        wt1.posn_smp_idx = 16;
        wt1.aV = a1V;
        
        wt2.tid = wt_osc::kLoopWtTId;
        wt2.posn_smp_idx = 32;
        wt2.aV = a2V;
        
        struct wt_osc::wt_str<sample_t,srate_t> wtA[] = {
          wt0,
          wt1,
          wt2
        };

        struct wt_seq_osc::wt_seq_str<sample_t,srate_t> wt_seq{
          .wtA = wtA,
          .wtN = 3
        };

        
        struct wt_seq_osc::wt_seq_str<sample_t,srate_t> chA[] = {
          wt_seq,
          wt_seq
        };

        struct multi_ch_wt_seq_str<sample_t,srate_t> mcs = {
          .chA = chA,
          .chN = chN
        };
        
        
        struct obj_str<sample_t,srate_t> obj;
        
        if((rc = create(&obj,chN)) != kOkRC )
          goto errLabel;

        if((rc = setup(&obj,&mcs)) != kOkRC )
          goto errLabel;
        else
        {
          unsigned yN = (int)(srate*10);
          unsigned actual = 0;
          unsigned yi = 0;
          unsigned yDspSmpCnt = 8;

        
          while( yi < yN && !is_done(&obj) )
          {
            unsigned frmSmpN = std::min(yN-yi,yDspSmpCnt);
            sample_t yV[frmSmpN*chN];
          
            if((rc = process(&obj, yV, chN, frmSmpN, actual)) != kOkRC )
              goto errLabel;

            assert( actual == frmSmpN );
                    
            vop::print( yV, frmSmpN*chN, "%5.3f",nullptr,8);
                    
            yi += actual;
          }
        }
        
      errLabel:
        destroy(&obj);
        
        return rc;
      }
      
    } 
    
    
    rc_t test( const test::test_args_t& args )
    {
      rc_t rc = kOkRC;
      
      if( textIsEqual(args.test_label,"wnd_func") )
      {
        rc = wnd_func::test();
        goto errLabel;
      }

      if( textIsEqual(args.test_label,"ola") )
      {
        rc = ola::test();
        goto errLabel;
      }
      
      if( textIsEqual(args.test_label,"shift_buf") )
      {
        rc = shift_buf::test();
        goto errLabel;
      }

      if( textIsEqual(args.test_label,"wt_osc") )
      {
        rc = wt_osc::test();
        goto errLabel;
      }

      if( textIsEqual(args.test_label,"wt_seq_osc") )
      {
        rc = wt_seq_osc::test();
        goto errLabel;
      }

      if( textIsEqual(args.test_label,"multi_ch_wt_seq_osc") )
      {
        rc = multi_ch_wt_seq_osc::test();
        goto errLabel;
      }
      
      rc = cwLogError(kInvalidArgRC,"Unknown test case module:%s test:%s.",args.module_label,args.test_label);
    errLabel:
      return rc;
    }
  }

  /*
  namespace spec_dist
  {
    rc_t _test_one( const object_t* arg,   )
    {
    }
    
    rc_t create( struct obj_str<T0,T1>*& p, unsigned binN, bool bypassFl=false, T1 ceiling=30, T1 expo=2, T1 thresh=60, T1 uprSlope=0, T1 lwrSlope=2, T1 mix=0 )

    rc_t  test( const object_t* args )
    {
      
    }
    
  }
  */
}
