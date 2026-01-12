//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
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
}

unsigned cw::textLength( const char* s )
{ return s == nullptr ? 0 : strlen(s); }

const char* cw::textCopy( char* dst, unsigned dstN, const char* src, unsigned srcN )
{
  if( dst == nullptr || dstN == 0 )
    return nullptr;

  if( srcN == 0 )
    srcN = textLength(src);
  
  if( src == nullptr || srcN==0 || dstN==1 )
  {
    dst[0] = 0;
  }
  else
  {
    
    assert( dstN >= 2 );
    unsigned n = std::min( dstN-1, srcN );
    memcpy(dst,src,n);
    dst[n] = 0;
  }
  return dst;
}

const char* cw::textCat(  char* dst, unsigned dstN, const char* src, unsigned srcN )
{
  unsigned n = textLength(dst);
  
  if( n>=dstN)
    return nullptr;
  
  return textCopy(dst + n, dstN-n, src, srcN );
}

void cw::textToLower( char* s )
{
  if( s != nullptr )
    for(; *s; ++s)
      *s = std::tolower(*s);
}

void cw::textToUpper( char* s )
{
  if( s != nullptr )
    for(; *s; ++s)
      *s = std::toupper(*s);
}

void cw::textToLower( char* dst, const char* src, unsigned dstN )
{
  if( src != nullptr && dstN>0 )
  {
    unsigned sn = std::min(dstN,textLength(src)+1);
    unsigned i;
    for(i=0; i<sn; ++i)
      dst[i] = std::tolower( src[i] );
    dst[i-1] = 0;
  }
}

void cw::textToUpper( char* dst, const char* src, unsigned dstN )
{
  if( src != nullptr && dstN>0 )
  {
    unsigned sn = std::min(dstN,textLength(src)+1);
    unsigned i;
    for(i=0; i<sn; ++i)
      dst[i] = std::toupper( src[i] );
    dst[i-1] = 0;
  }
}

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

int cw::textCompareI( const char* s0, const char* s1 )
{
  char b0N = textLength(s0)+1;
  char b1N = textLength(s1)+1;
  char b0[ b0N ];
  char b1[ b1N ];
  textToLower(b0,s0,b0N);
  textToLower(b1,s1,b1N);
  return textCompare(b0,b1);
}

int cw::textCompareI( const char* s0, const char* s1, unsigned n )
{
  char b0[ n+1 ];
  char b1[ n+1 ];
  textToLower(b0,s0,n+1);
  textToLower(b1,s1,n+1);
  return textCompare(b0,b1,n);
}

const char* cw::nextWhiteChar( const char* s )
{ return _nextWhiteChar(s,false); }

const char* cw::nextWhiteCharEOS( const char* s )
{ return _nextWhiteChar(s,true); }

const char* cw::nextNonWhiteChar( const char* s )
{ return _nextNonWhiteChar(s,false); }

const char* cw::nextNonWhiteCharEOS( const char* s )
{ return _nextNonWhiteChar(s,true); }

char* cw::firstMatchChar( char* s, char c )
{
  if( s == nullptr )
    return nullptr;
  
  for(; *s; ++s)
    if(*s == c)
      return s;
  return nullptr;
}

const char* cw::firstMatchChar( const char* s, char c )
{ return firstMatchChar((char*)s,c); }

char* cw::firstMatchChar( char* s, unsigned n, char c )
{
  if( s == nullptr )
    return nullptr;
  
  for(unsigned i=0; *s && i<n; ++s,++i)
    if(*s == c)
      return s;
  return nullptr;
}


const char* cw::firstMatchChar( const char* s, unsigned n, char c )
{
  return firstMatchChar((char*)s,c);
}

char* cw::lastMatchChar( char* s, char c )
{
  unsigned sn;
  
  if( s == nullptr )
    return nullptr;
  
  sn = textLength(s);
  if( sn == 0 )
    return nullptr;
  
  for(char* s1=s+(sn-1); s<=s1; --s1)
    if( *s1 == c )
      return s1;
    
  return nullptr;
}

const char* cw::lastMatchChar( const char* s, char c )
{
  return lastMatchChar((char*)s,c);
}

char* cw::removeTrailingWhitespace( char* s )
{
  char* s0;
  unsigned sn;
  
  if( s == nullptr )
    return nullptr;

  if((sn = textLength(s)) == 0 )
    return s;

  s0 = s + (sn-1);

  for(; s0>=s; --s0)
  {
    if( !isspace(*s0) )
      break;
    *s0 = 0;
  }

  return s;
}


bool cw::isInteger( const char* s )
{
  for(; *s; ++s)
    if(!isdigit(*s))
      return false;
  return true;
}

bool cw::isReal( const char* s)
{
  unsigned decN = 0;
  for(; *s; ++s)
    if( *s == '.' )
    {
      if( ++decN > 1)
        return false;
    }   
    else
    {
      if(!isdigit(*s))
        return false;
    }
  
  return true;
}

bool cw::isIdentifier( const char* s )
{
  if( !isalpha(*s) && *s != '_' )
    return false;

  for(++s; *s; ++s)
    if( !isalnum(*s) && *s != '_' )
      return false;

  return true;
}


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
  if( v == nullptr )
  {
    cwLogError(kInvalidArgRC,"The source string in a call to 'toText()' was null.");
    return 0;
  }
  
  unsigned i;
  for(i=0; i<bufN; ++i)
  {
    buf[i] = v[i];
    if(v[i]==0)
      return i; // on success return the length of the string in buf[] and v[]
  }

  return 0; // if buf is too small return 0
}
