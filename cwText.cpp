#include "cwCommon.h"
#include "cwLog.h"
#include "cwText.h"
#include "cwCommonImpl.h"
#include "cwMem.h"

namespace cw
{
  const char* _nextWhiteChar( const char* s, bool eosFl )
  {
    if( s == nullptr )
      return nullptr;

    for(; *s; ++s )
      if( isspace(*s) )
        return s;

    return eosFl ? s : nullptr;
  }


  const char* _nextNonWhiteChar( const char* s, bool eosFl )
  {
    if( s == nullptr )
      return nullptr;

    for(; *s; ++s )
      if( !isspace(*s) )
        return s;

    return eosFl ? s : nullptr;
  }
  /*
  unsigned _toText( char* buf, unsigned bufN, unsigned char v )
  {
    if( bufN < 1 )
      return 0;
    buf[0] = v;
    return 1;
  }

  unsigned _toText( char* buf, unsigned bufN, char v )
  {
    if( bufN < 1 )
      return 0;
    buf[0] = v;
    return 1;
  }
  */
  
}


unsigned cw::textLength( const char* s )
{ return s == nullptr ? 0 : strlen(s); }

int cw::textCompare( const char* s0, const char* s1 )
{
  if( s0 == nullptr || s1 == nullptr )
    return s0==s1 ? 0 : 1; // if both pointers are nullptr then trigger a match

  return strcmp(s0,s1);
}

int cw::textCompare( const char* s0, const char* s1, unsigned n)
{
  if( s0 == nullptr || s1 == nullptr )
    return s0==s1 ? 0 : 1; // if both pointers are nullptr then trigger a match

  return strncmp(s0,s1,n);  
}

const char* cw::nextWhiteChar( const char* s )
{ return _nextWhiteChar(s,false); }

const char* cw::nextWhiteCharEOS( const char* s )
{ return _nextWhiteChar(s,true); }

const char* cw::nextNonWhiteChar( const char* s )
{ return _nextNonWhiteChar(s,false); }

const char* cw::nextNonWhiteCharEOS( const char* s )
{ return _nextNonWhiteChar(s,true); }


char* cw::textJoin( const char* s0, const char* s1 )
{
  if( s0 == nullptr && s1 == nullptr )
    return nullptr;
  
  unsigned s0n = textLength(s0);
  unsigned s1n = textLength(s1);
  unsigned sn  = s0n + s1n + 1;

  char* s = mem::alloc<char>(sn+1);
  s[0] = 0;
  
  if( s0 != nullptr )
    strcpy(s,mem::duplStr(s0));

  if( s0 != nullptr && s1 != nullptr )
    strcpy(s + strlen(s0), mem::duplStr(s1) );

  return s;
}

char* cw::textAppend( char* s0,  const char* s1 )
{
  if( s0 == nullptr && s1==nullptr)
    return nullptr;

  return mem::appendStr(s0,s1);
}



unsigned cw::toText( char* buf, unsigned bufN, bool v )
{ return toText( buf, bufN, v ? "true" : "false" );  }

unsigned cw::toText( char* buf, unsigned bufN, char v )
{ return snprintf(buf,bufN, "%c", v ); }

unsigned cw::toText( char* buf, unsigned bufN, unsigned char v )
{ return snprintf(buf,bufN, "%c", v ); }

unsigned cw::toText( char* buf, unsigned bufN, unsigned short v )
{ return snprintf(buf,bufN,"%i",v); }

unsigned cw::toText( char* buf, unsigned bufN, short v )
{ return snprintf(buf,bufN,"%i",v); }

unsigned cw::toText( char* buf, unsigned bufN, unsigned int v  )
{ return snprintf(buf,bufN,"%i",v); }

unsigned cw::toText( char* buf, unsigned bufN, int v )
{ return snprintf(buf,bufN,"%i",v); }

unsigned cw::toText( char* buf, unsigned bufN, unsigned long long v  )
{ return snprintf(buf,bufN,"%lli",v); }

unsigned cw::toText( char* buf, unsigned bufN, long long v )
{ return snprintf(buf,bufN,"%lli",v); }

unsigned cw::toText( char* buf, unsigned bufN, float v )
{ return snprintf(buf,bufN,"%f",v); }

unsigned cw::toText( char* buf, unsigned bufN, double v )
{ return snprintf(buf,bufN,"%f",v); }

unsigned cw::toText( char* buf, unsigned bufN, const char* v )
{
  assert( v != nullptr );
  
  unsigned sn = strlen(v) + 1;

  // bufN must be greater than the length of v[]
  if( sn >= bufN )
    return 0;

  strncpy(buf,v,sn);

  return sn-1;
}
