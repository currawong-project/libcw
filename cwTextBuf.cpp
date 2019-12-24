#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTextBuf.h"

namespace cw
{
  namespace textBuf
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
    } this_t;
  }
}

#define _handleToPtr(h) handleToPtr<handle_t,this_t>(h)

cw::rc_t cw::textBuf::create( handle_t& hRef, unsigned initCharN, unsigned expandCharN )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  this_t* p        = memAllocZ<this_t>();
  p->buf           = memAllocZ<char>(initCharN);
  p->expandCharN   = expandCharN;
  p->allocCharN    = initCharN;
  p->boolTrueText  = memDuplStr("true");
  p->boolFalseText = memDuplStr("false");
  hRef.set(p);
  return rc;
}

cw::textBuf::handle_t cw::textBuf::create( unsigned initCharN, unsigned expandCharN )
{
  handle_t h;
  rc_t rc;
  if((rc = create(h,initCharN,expandCharN)) != kOkRC )
  {
    cwLogError(rc,"Log create ailed.");
    h.clear();
  }
  return h;  
}

cw::rc_t cw::textBuf::destroy(handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  this_t* p = _handleToPtr(hRef);
  
  memRelease(p->buf);
  memRelease(p->boolTrueText);
  memRelease(p->boolFalseText);
  memRelease(p);
  hRef.clear();
  return rc;
}

void cw::textBuf::clear( handle_t h )
{
  this_t* p = _handleToPtr(h);
  p->endN = 0;
}

const char* cw::textBuf::text( handle_t h )
{
  this_t* p = _handleToPtr(h);
  return p->buf;
}

cw::rc_t cw::textBuf::print( handle_t h, const char* fmt, va_list vl )
{
  va_list vl1;
  va_copy(vl1,vl);

  this_t* p = _handleToPtr(h);
  
  int n = vsnprintf(nullptr,0,fmt,vl1);

  if( n > 0 )
  {
    n += 1; // add one to make space for  the terminating zero
      
    if( p->endN + n > p->allocCharN )
    {
      unsigned minExpandCharN = (p->endN + n) - p->allocCharN;
      unsigned expandCharN = std::max(minExpandCharN,p->expandCharN);
      p->allocCharN += expandCharN;
      p->buf = memResizeZ<char>( p->buf, p->allocCharN );    
    }
    
    int m = vsnprintf(p->buf + p->endN, n, fmt, vl );

    cwAssert(m==(n-1));
    p->endN += n-1; // subtract 1 to no count the terminating zero in the character count
  }
  
  va_end(vl1);
  return kOkRC;
}

cw::rc_t cw::textBuf::print( handle_t h, const char* fmt,  ... )
{
  va_list vl;
  va_start(vl,fmt);
  rc_t rc = print(h,fmt,vl);
  va_end(vl);
  return rc;
}

cw::rc_t cw::textBuf::printBool( handle_t h, bool v )
{
  this_t* p = _handleToPtr(h);  
  return print(h,"%s", v ? p->boolTrueText : p->boolFalseText);
}

cw::rc_t cw::textBuf::printInt( handle_t h, int v )
{ return print(h,"%i",v); }

cw::rc_t cw::textBuf::printUInt( handle_t h, unsigned v )
{ return print(h,"%i",v); }

cw::rc_t cw::textBuf::printFloat( handle_t h, double v )
{ return print(h,"%f",v); }

cw::rc_t cw::textBuf::setBoolFormat( handle_t h, bool v, const char* s)
{
  this_t* p = _handleToPtr(h);
  
  if( v )    
    p->boolTrueText = memReallocStr(p->boolTrueText,s);
  else
    p->boolFalseText = memReallocStr(p->boolFalseText,s);
  return kOkRC;
}

cw::rc_t cw::textBuf::setIntFormat( handle_t h, unsigned width, unsigned flags )
{
  this_t* p = _handleToPtr(h);
  p->intWidth = width;
  p->intFlags = flags;
  return kOkRC;
}

cw::rc_t cw::textBuf::setFloatFormat( handle_t h, unsigned width, unsigned decPlN )
{
  this_t* p = _handleToPtr(h);
  p->floatWidth = width;
  p->floatDecPlN = decPlN;
  return kOkRC;
}

cw::rc_t cw::textBuf::test()
{
  handle_t h;
  rc_t rc;

  if((rc = create(h,8,8)) != kOkRC )
    return rc;

  print(h,"Hello\n");
  print(h,"foo\n");

  printf("%s", text(h) );

  return destroy(h);
}
