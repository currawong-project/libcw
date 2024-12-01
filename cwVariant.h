//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwVariant_h
#define cwVariant_h

namespace cw
{
  namespace variant
  {
    enum {
      kCharVFl   = 0x00000001,
      kUCharVFl  = 0x00000002,
      kInt8VFl   = 0x00000004,
      kUInt8VFl  = 0x00000008,
      kInt16VFl  = 0x00000010,
      kUInt16VFl = 0x00000020,
      kInt32VFl  = 0x00000040,
      kUInt32VFl = 0x00000080,
      kInt64VFl  = 0x00000100,
      kUInt64VFl = 0x00000200,
      kBoolVFl   = 0x00000400,
      kIntVMask  = 0x000007ff,
      
      kFloatVFl  = 0x00000800,
      kDoubleVFl = 0x00001000,
      kRealVMask = 0x00001800,

      kNumberMask= kRealVMask | kIntVMask,
    
      kPtrVFl    = 0x80000000
    };

    const char* flagsToLabel( unsigned flags );
    unsigned    flagsToBytes( unsigned flags );
  
    typedef struct value_str
    {
      unsigned flags;
      union {
        char            c;
        unsigned char  uc;
        std::int8_t    i8;
        std::uint8_t   u8;
        std::int16_t   i16;
        std::uint16_t  u16;
        std::int32_t   i32;
        std::uint32_t  u32;
        std::int64_t   i64;
        std::uint64_t  u64;
        bool           b;
        float          f;
        double         d;

        char*           cp;
        unsigned char* ucp;
        std::int8_t*   i8p;
        std::uint8_t*  u8p;
        std::int16_t*  i16p;
        std::uint16_t* u16p;
        std::int32_t*  i32p;
        std::uint32_t* u32p;
        std::int64_t*  i64p;
        std::uint64_t* u64p;
        bool*          bp;
        float*         fp;
        double*        dp;
        void*          vp;
      
      } u;
    } value_t;

    

    inline void set( value_t& v, char x )          { v.u.c=x; v.flags=kCharVFl; }
    //inline void set( value_t& v, unsigned char x ) { v.u.uc=x; v.flags=kUCharVFl; }

    inline void set( value_t& v, std::int8_t x )   { v.u.i8=x; v.flags=kInt8VFl; }
    inline void set( value_t& v, std::uint8_t x )  { v.u.u8=x; v.flags=kUInt8VFl; }

    inline void set( value_t& v, std::int16_t x )  { v.u.i16=x; v.flags=kInt16VFl; }
    inline void set( value_t& v, std::uint16_t x ) { v.u.u16=x; v.flags=kUInt16VFl; }

    inline void set( value_t& v, std::int32_t x )  { v.u.i32=x; v.flags=kInt32VFl; }
    inline void set( value_t& v, std::uint32_t x ) { v.u.u32=x; v.flags=kUInt32VFl; }

    inline void set( value_t& v, std::int64_t x )  { v.u.i64=x; v.flags=kInt64VFl; }
    inline void set( value_t& v, std::uint64_t x ) { v.u.u64=x; v.flags=kUInt64VFl; }

    inline void set( value_t& v, bool x )          { v.u.b=x; v.flags=kBoolVFl; }
    inline void set( value_t& v, float x )         { v.u.f=x; v.flags=kFloatVFl; }
    inline void set( value_t& v, double x )        { v.u.d=x; v.flags=kDoubleVFl; }


    inline void set( value_t& v, char* x )          { v.u.cp=x;  v.flags=kPtrVFl | kCharVFl; }
    //inline void set( value_t& v, unsigned char* x ) { v.u.ucp=x; v.flags=kPtrVFl | kUCharVFl; }

    inline void set( value_t& v, std::int8_t* x )   { v.u.i8p=x; v.flags=kPtrVFl | kInt8VFl; }
    inline void set( value_t& v, std::uint8_t* x )  { v.u.u8p=x; v.flags=kPtrVFl | kUInt8VFl; }

    inline void set( value_t& v, std::int16_t* x )  { v.u.i16p=x; v.flags=kPtrVFl | kInt16VFl; }
    inline void set( value_t& v, std::uint16_t* x ) { v.u.u16p=x; v.flags=kPtrVFl | kUInt16VFl; }

    inline void set( value_t& v, std::int32_t* x )  { v.u.i32p=x; v.flags=kPtrVFl | kInt32VFl; }
    inline void set( value_t& v, std::uint32_t* x ) { v.u.u32p=x; v.flags=kPtrVFl | kUInt32VFl; }

    inline void set( value_t& v, std::int64_t* x )  { v.u.i64p=x; v.flags=kPtrVFl | kInt64VFl; }
    inline void set( value_t& v, std::uint64_t* x ) { v.u.u64p=x; v.flags=kPtrVFl | kUInt64VFl; }

    inline void set( value_t& v, bool* x )          { v.u.bp=x; v.flags=kPtrVFl | kBoolVFl; }
    inline void set( value_t& v, float* x )         { v.u.fp=x; v.flags=kPtrVFl | kFloatVFl; }
    inline void set( value_t& v, double* x )        { v.u.dp=x; v.flags=kPtrVFl | kDoubleVFl; }


    rc_t get( const value_t& v, std::int8_t& r );
    rc_t get( const value_t& v, std::uint8_t& r );
    rc_t get( const value_t& v, std::int16_t& r );
    rc_t get( const value_t& v, std::uint16_t& r );
    rc_t get( const value_t& v, std::int32_t& r );
    rc_t get( const value_t& v, std::uint32_t& r );
    rc_t get( const value_t& v, std::int64_t& r );
    rc_t get( const value_t& v, std::uint64_t& r );
    rc_t get( const value_t& v, bool& r );
    rc_t get( const value_t& v, float& r );
    rc_t get( const value_t& v, double& r );
    

    inline bool isInt( const value_t& v ) { return v.flags & kIntVMask; }
    inline bool isReal(const value_t& v ) { return v.flags & kRealVMask; }
    inline bool isPtr( const value_t& v ) { return v.flags & kPtrVFl; }
    
    rc_t print( const value_t& v, const char* fmt=nullptr );
    rc_t write( file::handle_t fH, const value_t& v );
    rc_t read(  file::handle_t fH,       value_t& v );
  }

}


#endif
