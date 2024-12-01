//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwCmInterface.h"

#include "cmGlobal.h"
#include "cmFloatTypes.h"
#include "cmRpt.h"
#include "cmErr.h"
#include "cmCtx.h"
#include "cmMem.h"
#include "cmMallocDebug.h"
#include "cmLinkedHeap.h"
#include "cmTime.h"
#include "cmMidi.h"
#include "cmSymTbl.h"
#include "cmScore.h"
#include "cmText.h"
#include "cmFileSys.h"
#include "cmProcObj.h"

extern "C" {

  
  void _cm_print_info( void* arg, const char* text )
  {
    cw::log::msg( cw::log::globalHandle(), cw::log::kInfo_LogLevel, nullptr, nullptr, 0, 0, cw::kOkRC, "%s", text );
  }

  void _cm_print_error( void* arg, const char* text )
  {
    cw::log::msg( cw::log::globalHandle(), cw::log::kPrint_LogLevel, nullptr, nullptr, 0, 0, cw::kOkRC, "cm-error: %s", text );
  }
}

namespace cw
{
  namespace cm
  {
    
    extern "C" {
      typedef struct cm_str
      {
        cmCtx_t ctx;
        cmSymTblH_t symTblH;
        cmLHeapH_t  lhH;        
        cmProcCtx* procCtx;
        
      } cm_t;
    }
    
    cm_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,cm_t>(h); }

    rc_t _destroy( cm_t* p )
    {
      if( p != nullptr )
      {

        cmLHeapDestroy(&p->lhH);
        cmSymTblDestroy(&p->symTblH);
        cmProcCtxFree(&p->procCtx);
        
        cmTsFinalize();
        cmFsFinalize();
        cmMdReport( kIgnoreNormalMmFl );
        cmMdFinalize();
        
        mem::release(p);
      }
      return kOkRC;
    }

    rc_t _create_proc_ctx( cm_t* p, cmCtx_t* ctx )
    {
      rc_t rc = kOkRC;
      
      // create the linked heap
      if(cmLHeapIsValid( p->lhH = cmLHeapCreate(1024,ctx)) == false)
      {
        rc = cwLogError(kOpFailRC,"The cm linked heap allocation failed.");
        goto errLabel;
      }

      // intialize the symbol table
      if( cmSymTblIsValid( p->symTblH = cmSymTblCreate(cmSymTblNullHandle,1,ctx)) == false )
      {
        rc = cwLogError(kOpFailRC,"The cm linked heap allocation failed.");
        goto errLabel;
      }

      // initialize the proc context
      if( (p->procCtx = cmProcCtxAlloc(NULL,&ctx->rpt,p->lhH,p->symTblH)) == NULL )
      {
        rc = cwLogError(kOpFailRC,"The cm proc context allocation failed.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }
  }
}

cw::rc_t cw::cm::create( handle_t& hRef )
{
  rc_t            rc              = kOkRC;
  cm_t*           p               = nullptr;
  bool            memDebugFl      = 0; //cmDEBUG_FL;
  unsigned        memGuardByteCnt = memDebugFl ? 8 : 0;
  unsigned        memAlignByteCnt = 16;
  unsigned        memFlags        = memDebugFl ? kTrackMmFl | kDeferFreeMmFl | kFillUninitMmFl : 0;  
  const cmChar_t* appTitle        = "cwtest";

  if((rc = destroy(hRef)) != kOkRC )
    return rc;
  
  p = mem::allocZ<cm_t>();
  
  cmCtxSetup(&p->ctx,appTitle,_cm_print_info,_cm_print_error,NULL,memGuardByteCnt,memAlignByteCnt,memFlags);

  cmMdInitialize( memGuardByteCnt, memAlignByteCnt, memFlags, &p->ctx.rpt );

  cmFsInitialize( &p->ctx, appTitle);

  cmTsInitialize( &p->ctx );

  if((rc = _create_proc_ctx(p, &p->ctx )) == kOkRC )
    hRef.set(p);
  else
  {
    _destroy(p);
    rc = cwLogError(rc,"cm Interface context create failed.");
  }
  
  return rc;
}

cw::rc_t cw::cm::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  cm_t* p = nullptr;
  if( !hRef.isValid() )
    return rc;

  p = _handleToPtr(hRef);
  
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  return rc;  
  
}

extern "C" {
  struct cmCtx_str* cw::cm::context( handle_t h )
  {
    cm_t* p = _handleToPtr(h);
    return &p->ctx;
  }
  
  struct cmProcCtx_str* cw::cm::proc_context( handle_t h )
  {
    cm_t* p = _handleToPtr(h);
    return p->procCtx;
  }
  
}
