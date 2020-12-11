#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwFile.h"
#include "cwVariant.h"

namespace cw
{
  namespace variant
  {
    typedef struct _variantDesc_str
    {
      unsigned    flags;
      const char* label;
      const char* fmt;
      unsigned    byteN;
    } variantDesc_t;

    variantDesc_t _variantDescArray[] = {
    
      { kCharVFl,  "char",   "c",  sizeof(char) },
      { kUCharVFl, "uchar",  "c",  sizeof(unsigned char) },
      { kInt8VFl,  "uint8",  "i",  sizeof(std::uint8_t) },
      { kUInt8VFl, "int8",   "i",  sizeof(std::int8_t) },
      { kInt16VFl, "uint16", "i",  sizeof(std::uint16_t) },
      { kUInt16VFl,"int16",  "i",  sizeof(std::int16_t) },
      { kInt32VFl, "uint32", "i",  sizeof(std::uint32_t) },
      { kUInt32VFl,"int32",  "i",  sizeof(std::int32_t) },
      { kInt64VFl, "uint64", "li", sizeof(std::uint64_t) },
      { kUInt64VFl,"int64",  "li", sizeof(std::int64_t) },
      { kBoolVFl,  "bool",   "i",  sizeof(bool) },
      { kFloatVFl, "float",  "f",  sizeof(float) },
      { kDoubleVFl,"double", "f",  sizeof(double) },

      { kPtrVFl | kCharVFl,   "char_ptr",   "p", sizeof(char) },
      { kPtrVFl | kUCharVFl,  "uchar_ptr",  "p", sizeof(unsigned char) },
      { kPtrVFl | kInt8VFl,   "uint8_ptr",  "p", sizeof(std::uint8_t) },
      { kPtrVFl | kUInt8VFl,  "int8_ptr",   "p", sizeof(std::int8_t) },
      { kPtrVFl | kInt16VFl,  "uint16_ptr", "p", sizeof(std::uint16_t) },
      { kPtrVFl | kUInt16VFl, "int16_ptr",  "p", sizeof(std::int16_t) },
      { kPtrVFl | kInt32VFl,  "uint32_ptr", "p", sizeof(std::uint32_t) },
      { kPtrVFl | kUInt32VFl, "int32_ptr",  "p", sizeof(std::int32_t) },
      { kPtrVFl | kInt64VFl,  "uint64_ptr", "p", sizeof(std::uint64_t) },
      { kPtrVFl | kUInt64VFl, "int64_ptr",  "p", sizeof(std::int64_t) },
      { kPtrVFl | kBoolVFl,   "bool_ptr",   "p", sizeof(bool) },
      { kPtrVFl | kFloatVFl,  "float_ptr",  "p", sizeof(float) },
      { kPtrVFl | kDoubleVFl, "double_ptr", "p", sizeof(double) },

      { 0, nullptr, 0 }

    };
  
    const variantDesc_t* _flagsToDesc( unsigned flags, bool reportErrorFl=true )
    {
      variantDesc_t* v = _variantDescArray;
    
      for(; v->flags!=0; ++v)
        if( v->flags == flags )
          return v;

      if( reportErrorFl )
        cwLogError(kInvalidArgRC,"The variant flags 0x%x is invalid.");
      return nullptr;
    }

    const char* safeFlagsToLabel( unsigned flags )
    {
      const variantDesc_t* d = _flagsToDesc(flags,false);
      return d == nullptr ? "<invalid>" : d->label;
    }

    rc_t _get_uint8( const value_t& v, std::uint8_t& r )
    {
      switch( v.flags )
      {
        case kCharVFl:   r = v.u.c;    break;
        case kUCharVFl:  r = v.u.uc;   break;
        case kInt8VFl:   r = v.u.i8;   break;
        case kUInt8VFl:  r = v.u.u8;   break;
        case kBoolVFl:   r = v.u.b;    break;
        default:
          return cwLogError(kInvalidArgRC,"Invalid type conversion. Cannot convert '%s' to 'double'.", safeFlagsToLabel(v.flags) );
      }      
      return kOkRC;
    }
    
    rc_t _get_int8( const value_t& v, std::int8_t& r )
    {
      switch( v.flags )
      {
        case kCharVFl:   r = v.u.c;    break;
        case kUCharVFl:  r = v.u.uc;   break;
        case kInt8VFl:   r = v.u.i8;   break;
        case kUInt8VFl:  r = v.u.u8;   break;
        case kBoolVFl:   r = v.u.b;    break;
        default:
          return cwLogError(kInvalidArgRC,"Invalid type conversion. Cannot convert '%s' to 'double'.", safeFlagsToLabel(v.flags) );
      }      
      return kOkRC;
    }
    
    
    rc_t _get_uint16( const value_t& v, std::uint16_t& r )
    {
      switch( v.flags )
      {
        case kCharVFl:   r = v.u.c;    break;
        case kUCharVFl:  r = v.u.uc;   break;
        case kInt8VFl:   r = v.u.i8;   break;
        case kUInt8VFl:  r = v.u.u8;   break;
        case kInt16VFl:  r = v.u.i16;  break;
        case kUInt16VFl: r = v.u.u16;  break;
        case kBoolVFl:   r = v.u.b;    break;
        default:
          return cwLogError(kInvalidArgRC,"Invalid type conversion. Cannot convert '%s' to 'double'.", safeFlagsToLabel(v.flags) );
      }      
      return kOkRC;
    }
    
    rc_t _get_int16( const value_t& v, std::int16_t& r )
    {
      switch( v.flags )
      {
        case kCharVFl:   r = v.u.c;    break;
        case kUCharVFl:  r = v.u.uc;   break;
        case kInt8VFl:   r = v.u.i8;   break;
        case kUInt8VFl:  r = v.u.u8;   break;
        case kInt16VFl:  r = v.u.i16;  break;
        case kUInt16VFl: r = v.u.u16;  break;
        case kBoolVFl:   r = v.u.b;    break;
        default:
          return cwLogError(kInvalidArgRC,"Invalid type conversion. Cannot convert '%s' to 'double'.", safeFlagsToLabel(v.flags) );
      }      
      return kOkRC;
    }    
    
    rc_t _get_uint32( const value_t& v, std::uint32_t& r )
    {
      switch( v.flags )
      {
        case kCharVFl:   r = v.u.c;    break;
        case kUCharVFl:  r = v.u.uc;   break;
        case kInt8VFl:   r = v.u.i8;   break;
        case kUInt8VFl:  r = v.u.u8;   break;
        case kInt16VFl:  r = v.u.i16;  break;
        case kUInt16VFl: r = v.u.u16;  break;
        case kInt32VFl:  r = v.u.i32;  break;
        case kUInt32VFl: r = v.u.u32;  break;
        case kBoolVFl:   r = v.u.b;    break;
        default:
          return cwLogError(kInvalidArgRC,"Invalid type conversion. Cannot convert '%s' to 'double'.", safeFlagsToLabel(v.flags) );
      }      
      return kOkRC;
    }
    
    
    rc_t _get_int32( const value_t& v, std::int32_t& r )
    {
      switch( v.flags )
      {
        case kCharVFl:   r = v.u.c;    break;
        case kUCharVFl:  r = v.u.uc;   break;
        case kInt8VFl:   r = v.u.i8;   break;
        case kUInt8VFl:  r = v.u.u8;   break;
        case kInt16VFl:  r = v.u.i16;  break;
        case kUInt16VFl: r = v.u.u16;  break;
        case kInt32VFl:  r = v.u.i32;  break;
        case kUInt32VFl: r = v.u.u32;  break;
        case kBoolVFl:   r = v.u.b;    break;
        default:
          return cwLogError(kInvalidArgRC,"Invalid type conversion. Cannot convert '%s' to 'double'.", safeFlagsToLabel(v.flags) );
      }      
      return kOkRC;
    }
    
    rc_t _get_uint64( const value_t& v, std::uint64_t& r )
    {
      switch( v.flags )
      {
        case kCharVFl:   r = v.u.c;    break;
        case kUCharVFl:  r = v.u.uc;   break;
        case kInt8VFl:   r = v.u.i8;   break;
        case kUInt8VFl:  r = v.u.u8;   break;
        case kInt16VFl:  r = v.u.i16;  break;
        case kUInt16VFl: r = v.u.u16;  break;
        case kInt32VFl:  r = v.u.i32;  break;
        case kUInt32VFl: r = v.u.u32;  break;
        case kInt64VFl:  r = v.u.i64;  break;
        case kUInt64VFl: r = v.u.u64;  break;
        case kBoolVFl:   r = v.u.b;    break;
        case kFloatVFl:  r = v.u.f;    break;
        case kDoubleVFl: r = v.u.d;    break;
        default:
          return cwLogError(kInvalidArgRC,"Invalid type conversion. Cannot convert '%s' to 'double'.", safeFlagsToLabel(v.flags) );
      }      
      return kOkRC;
    }
    
    rc_t _get_int64( const value_t& v, std::int64_t& r )
    {
      switch( v.flags )
      {
        case kCharVFl:   r = v.u.c;    break;
        case kUCharVFl:  r = v.u.uc;   break;
        case kInt8VFl:   r = v.u.i8;   break;
        case kUInt8VFl:  r = v.u.u8;   break;
        case kInt16VFl:  r = v.u.i16;  break;
        case kUInt16VFl: r = v.u.u16;  break;
        case kInt32VFl:  r = v.u.i32;  break;
        case kUInt32VFl: r = v.u.u32;  break;
        case kInt64VFl:  r = v.u.i64;  break;
        case kUInt64VFl: r = v.u.u64;  break;
        case kBoolVFl:   r = v.u.b;    break;
        case kFloatVFl:  r = v.u.f;    break;
        case kDoubleVFl: r = v.u.d;    break;
        default:
          return cwLogError(kInvalidArgRC,"Invalid type conversion. Cannot convert '%s' to 'double'.", safeFlagsToLabel(v.flags) );
      }      
      return kOkRC;
    }
    
    rc_t _get_bool( const value_t& v, bool& r )
    {
      switch( v.flags )
      {
        case kCharVFl:   r = v.u.c!=0;    break;
        case kUCharVFl:  r = v.u.uc!=0;   break;
        case kInt8VFl:   r = v.u.i8!=0;   break;
        case kUInt8VFl:  r = v.u.u8!=0;   break;
        case kInt16VFl:  r = v.u.i16!=0;  break;
        case kUInt16VFl: r = v.u.u16!=0;  break;
        case kInt32VFl:  r = v.u.i32!=0;  break;
        case kUInt32VFl: r = v.u.u32!=0;  break;
        case kInt64VFl:  r = v.u.i64!=0;  break;
        case kUInt64VFl: r = v.u.u64!=0;  break;
        case kBoolVFl:   r = v.u.b;       break;
        case kFloatVFl:  r = v.u.f!=0.0;  break;
        case kDoubleVFl: r = v.u.d!=0.0;  break;
        default:
          return cwLogError(kInvalidArgRC,"Invalid type conversion. Cannot convert '%s' to 'double'.", safeFlagsToLabel(v.flags) );
      }      
      return kOkRC;
    }
    
    rc_t _get_float( const value_t& v, float& r )
    {
      switch( v.flags )
      {
        case kCharVFl:   r = v.u.c;    break;
        case kUCharVFl:  r = v.u.uc;   break;
        case kInt8VFl:   r = v.u.i8;   break;
        case kUInt8VFl:  r = v.u.u8;   break;
        case kInt16VFl:  r = v.u.i16;  break;
        case kUInt16VFl: r = v.u.u16;  break;
        case kInt32VFl:  r = v.u.i32;  break;
        case kUInt32VFl: r = v.u.u32;  break;
        case kInt64VFl:  r = v.u.i64;  break;
        case kUInt64VFl: r = v.u.u64;  break;
        case kBoolVFl:   r = v.u.b;    break;
        case kFloatVFl:  r = v.u.f;    break;
        default:
          return cwLogError(kInvalidArgRC,"Invalid type conversion. Cannot convert '%s' to 'double'.", safeFlagsToLabel(v.flags) );
      }      
      return kOkRC;
    }
    
    rc_t _get_double( const value_t& v, double& r )
    {
      switch( v.flags )
      {
        case kCharVFl:   r = v.u.c;    break;
        case kUCharVFl:  r = v.u.uc;   break;
        case kInt8VFl:   r = v.u.i8;   break;
        case kUInt8VFl:  r = v.u.u8;   break;
        case kInt16VFl:  r = v.u.i16;  break;
        case kUInt16VFl: r = v.u.u16;  break;
        case kInt32VFl:  r = v.u.i32;  break;
        case kUInt32VFl: r = v.u.u32;  break;
        case kInt64VFl:  r = v.u.i64;  break;
        case kUInt64VFl: r = v.u.u64;  break;
        case kBoolVFl:   r = v.u.b;    break;
        case kFloatVFl:  r = v.u.f;    break;
        case kDoubleVFl: r = v.u.d;    break;
        default:
          return cwLogError(kInvalidArgRC,"Invalid type conversion. Cannot convert '%s' to 'double'.", safeFlagsToLabel(v.flags) );
      }      
      return kOkRC;
    }


    rc_t get( const value_t& v, std::int8_t& r )   { return _get_int8(v,r); }
    rc_t get( const value_t& v, std::uint8_t& r )  { return _get_uint8(v,r); }
    rc_t get( const value_t& v, std::int16_t& r )  { return _get_int16(v,r); }
    rc_t get( const value_t& v, std::uint16_t& r ) { return _get_uint16(v,r); }
    rc_t get( const value_t& v, std::int32_t& r )  { return _get_int32(v,r); }
    rc_t get( const value_t& v, std::uint32_t& r ) { return _get_uint32(v,r); }
    rc_t get( const value_t& v, std::int64_t& r )  { return _get_int64(v,r); }
    rc_t get( const value_t& v, std::uint64_t& r ) { return _get_uint64(v,r); }
    rc_t get( const value_t& v, bool& r )          { return _get_bool(v,r); }
    rc_t get( const value_t& v, float& r )         { return _get_float(v,r); }
    rc_t get( const value_t& v, double& r )        { return _get_double(v,r); }
    
    

  }
}

const char* cw::variant::flagsToLabel( unsigned flags )
{
  const variantDesc_t* v;
  if((v = _flagsToDesc(flags)) != nullptr )
    return v->label;

  return nullptr;
}
  
unsigned    cw::variant::flagsToBytes( unsigned flags )
{
  const variantDesc_t* v;
  if((v = _flagsToDesc(flags)) != nullptr )
    return v->byteN;

  return 0;
}


cw::rc_t cw::variant::print( const value_t& v, const char* fmt)
{
  rc_t                 rc = kOkRC;
  const variantDesc_t* d;
    
  if((d = _flagsToDesc(v.flags)) != nullptr )
  {
    char f[32+1];
    snprintf(f,32,"%s%s%s", "%", fmt==nullptr ? "":fmt, d->fmt);

    if( v.flags & kPtrVFl )
      printf(fmt,v.u.vp);
    else
    {
      switch( v.flags )
      {
        case kCharVFl:   printf(f,v.u.c);   break;
        case kUCharVFl:  printf(f,v.u.uc);  break;
        case kInt8VFl:   printf(f,v.u.i8);  break;
        case kUInt8VFl:  printf(f,v.u.u8);  break;
        case kInt16VFl:  printf(f,v.u.i16); break;
        case kUInt16VFl: printf(f,v.u.u16); break;
        case kInt32VFl:
          printf(f,v.u.i32);
          break;
          
        case kUInt32VFl: printf(f,v.u.u32); break;
        case kInt64VFl:  printf(f,v.u.i64); break;
        case kUInt64VFl: printf(f,v.u.u64); break;
        case kBoolVFl:   printf(f,v.u.b);   break;
        case kFloatVFl:  printf(f,v.u.f);   break;
        case kDoubleVFl: printf(f,v.u.d);   break;
        default:
          assert(0);
            
      }
    }
  }
    
  return rc;
}
  
  
cw::rc_t cw::variant::write( file::handle_t fH, const value_t& v )
{
  rc_t rc;
  if((rc = file::writeUInt( fH, &v.flags)) == kOkRC )
    rc = file::write( fH, &v.u, sizeof(v.u));
  return rc;
}
  
cw::rc_t cw::variant::read(  file::handle_t fH, value_t& v )
{
  rc_t rc;
  if((rc = file::readUInt( fH, &v.flags)) == kOkRC )
    rc = file::read( fH, &v.u, sizeof(v.u));
  return rc;    
}


