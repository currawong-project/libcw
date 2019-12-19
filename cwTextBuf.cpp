#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTextBuf.h"

namespace cw
{
  typedef struct textBuf_str
  {
    char*    buf;
    unsigned expandCharN;         // count of characters to expand buf
    unsigned allocCharN;          // current allocated size of buf
    unsigned endN;                // current count of character in buf
    char*    boolTrueText;
    char*    boolFalseText;
    int      intWidth;
    unsigned intFlags;
    int      floatWidth;
    int      floatDecPlN;
  } textBuf_t;

}

#define _textBufHandleToPtr(h) handleToPtr<textBufH_t,textBuf_t>(h)

cw::rc_t cw::textBufCreate( textBufH_t& hRef, unsigned initCharN, unsigned expandCharN )
{
  rc_t rc;
  if((rc = textBufDestroy(hRef)) != kOkRC )
    return rc;

  textBuf_t* p   = memAllocZ<textBuf_t>();
  p->buf         = memAllocZ<char>(initCharN);
  p->expandCharN = expandCharN;
  p->boolTrueText = memDuplStr("true");
  p->boolFalseText = memDuplStr("false");
  hRef.set(p);
  return rc;
}

cw::rc_t cw::textBufDestroy(textBufH_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  textBuf_t* p = _textBufHandleToPtr(hRef);
  
  memRelease(p->buf);
  memRelease(p->boolTrueText);
  memRelease(p->boolFalseText);
  hRef.release();
  return rc;
}

const char* cw::textBufText( textBufH_t h )
{
  textBuf_t* p = _textBufHandleToPtr(h);
  return p->buf;
}

cw::rc_t cw::textBufPrintf( textBufH_t h, const char* fmt, va_list vl )
{
  va_list vl1;
  va_copy(vl1,vl);

  textBuf_t* p = _textBufHandleToPtr(h);
  
  int n = snprintf(nullptr,0,fmt,vl);

  
  if( p->endN + n > p->allocCharN )
  {
    unsigned minExpandCharN = (p->endN + n) - p->allocCharN;
    unsigned expandCharN = std::max(minExpandCharN,p->expandCharN);
    p->allocCharN =+ expandCharN;
    p->buf = memResizeZ<char>( p->buf, p->allocCharN );    
  }
    
  int m = snprintf(p->buf + p->endN, n, fmt, vl );

  cwAssert(m=n);
  p->endN += n;
   
  va_end(vl1);
  return kOkRC;
}

cw::rc_t cw::textBufPrintf( textBufH_t h, const char* fmt,  ... )
{
  va_list vl;
  va_start(vl,fmt);
  rc_t rc = textBufPrintf(h,fmt,vl);
  va_end(vl);
  return rc;
}

cw::rc_t cw::textBufPrintBool( textBufH_t h, bool v )
{
  textBuf_t* p = _textBufHandleToPtr(h);  
  return textBufPrintf(h,"%s", v ? p->boolTrueText : p->boolFalseText);
}

cw::rc_t cw::textBufPrintInt( textBufH_t h, int v )
{ return textBufPrintf(h,"%i",v); }

cw::rc_t cw::textBufPrintUInt( textBufH_t h, unsigned v )
{ return textBufPrintf(h,"%i",v); }

cw::rc_t cw::textBufPrintFloat( textBufH_t h, double v )
{ return textBufPrintf(h,"%f",v); }

cw::rc_t cw::textBufSetBoolFormat( textBufH_t h, bool v, const char* s)
{
  textBuf_t* p = _textBufHandleToPtr(h);
  
  if( v )    
    p->boolTrueText = memReallocStr(p->boolTrueText,s);
  else
    p->boolFalseText = memReallocStr(p->boolFalseText,s);
  return kOkRC;
}

cw::rc_t cw::textBufSetIntFormat( textBufH_t h, unsigned width, unsigned flags )
{
  textBuf_t* p = _textBufHandleToPtr(h);
  p->intWidth = width;
  p->intFlags = flags;
  return kOkRC;
}

cw::rc_t cw::textBufSetFloatFormat( textBufH_t h, unsigned width, unsigned decPlN )
{
  textBuf_t* p = _textBufHandleToPtr(h);
  p->floatWidth = width;
  p->floatDecPlN = decPlN;
  return kOkRC;
}

