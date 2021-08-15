#ifndef cwNumericConvert_H
#define cwNumericConvert_H

namespace cw
{
  /*
  template< typename T >
    T minimum_value() { return 0; }

  template <> inline char  minimum_value<char>(){  return 0; }
  template <> inline int8_t  minimum_value<int8_t>(){  return INT8_MIN; }
  template <> inline int16_t minimum_value<int16_t>(){ return INT16_MIN; }
  template <> inline int32_t minimum_value<int32_t>(){ return INT32_MIN; }
  template <> inline int64_t minimum_value<int64_t>(){ return INT64_MIN; }
  template <> inline float   minimum_value<float>(){  return FLT_MIN; }
  template <> inline double  minimum_value<double>(){ return DBL_MIN; }

  template< typename T >
    T maximum_value() { cwAssert(0); }

  template <> inline char     maximum_value<char>(){  return 255; }
  template <> inline int8_t   maximum_value<int8_t>(){  return INT8_MAX; }
  template <> inline int16_t  maximum_value<int16_t>(){ return INT16_MAX; }
  template <> inline int32_t  maximum_value<int32_t>(){ return INT32_MAX; }
  template <> inline int64_t  maximum_value<int64_t>(){ return INT64_MAX; }
  template <> inline uint8_t  maximum_value<uint8_t>(){  return UINT8_MAX; }
  template <> inline uint16_t maximum_value<uint16_t>(){ return UINT16_MAX; }
  template <> inline uint32_t maximum_value<uint32_t>(){ return UINT32_MAX; }
  template <> inline uint64_t maximum_value<uint64_t>(){ return UINT64_MAX; }
  template <> inline bool     maximum_value<bool>(){ std::numeric_limits<bool>::max(); }
  template <> inline float    maximum_value<float>(){  return FLT_MAX; }
  template <> inline double   maximum_value<double>(){ return DBL_MAX; }
  */
    
    
  template< typename SRC_t, typename DST_t >
    rc_t numeric_convert( const SRC_t& src,  DST_t& dst )
  {
    // TODO: there is probably a way of using type_traits to make a more efficient comparison
    //       and avoid the double conversion
    double d_min = 0; // std::numeric_limits<DST_t>::min() return smallest positive number which then fails this test when 'src' is zero.
    double d_max = std::numeric_limits<DST_t>::max();
    if( (double)src <= d_max )
      dst = src;
    else
      return cwLogError(kInvalidArgRC,"Numeric conversion failed. The source value is outside the range of the destination value. min:%f max:%f src:%f",d_min,d_max,(double)src );
   

    return kOkRC;
  }
  
  template< typename SRC_t, typename DST_t >
    rc_t numeric_convert2( const SRC_t& src,  DST_t& dst,  const DST_t& minv,  const DST_t& maxv )
  {
    if( sizeof(SRC_t) < sizeof(DST_t) )
    {
      dst = src;
    }
    else
    {
      if( minv <= src && src <= maxv )
        dst = src;
      else
      {
        return cwLogError(kInvalidArgRC,"Numeric conversion failed. The source value is outside the range of the destination value." );
      }
    }

    return kOkRC;
  }


  
  template< typename T >
    rc_t string_to_number( const char* s, T& valueRef )
  {
    if( s == nullptr )
      valueRef = 0;  // BUG BUG BUG why is this not an error ????
    else
    {
      errno = 0;
      long v = strtol(s,nullptr,10);
      if( v == 0 and errno != 0)
        return cwLogError(kOpFailRC,"String to number conversion failed on '%s'.", cwStringNullGuard(s));
      
      return numeric_convert(v,valueRef);
      
    }
    return kOkRC;
  }

  template < > inline
    rc_t string_to_number<double>( const char* s, double& valueRef )
  {
    if( s == nullptr )
      valueRef = 0;    // BUG BUG BUG why is this not an error ????
    else
    {
      errno = 0;
      valueRef = strtod(s,nullptr);
      if( valueRef == 0 and errno != 0)
        return cwLogError(kOpFailRC,"String to number conversion failed on '%s'.", cwStringNullGuard(s));
            
    }
    return kOkRC;
  }

  
  template < > inline
    rc_t string_to_number<float>( const char* s, float& valueRef )
  {
    double d;
    rc_t rc;
    if((rc = string_to_number<double>(s,d)) != kOkRC )
      return rc;

    return numeric_convert(d,valueRef);
  }

  template < > inline
    rc_t string_to_number<bool>( const char* s, bool& valueRef )
  {
    s = nextNonWhiteChar(s);
    
    if( s == nullptr )
      valueRef = false;  // BUG BUG BUG why is this not an error ????
    else
    {
      if( strncmp( "true", s, 4) == 0 )
        valueRef = true;
      else
        if( strncmp( "false", s, 5) == 0 )
          valueRef = false;
        else
          return cwLogError(kOpFailRC,"String to number conversion failed on '%s'.", cwStringNullGuard(s));
    }
    return kOkRC;
  }

  template< typename T >
    int number_to_string( const T& v, char* buf, int bufN, const char* fmt=nullptr  )
  { return snprintf(buf,bufN,fmt,v); }

  template < > inline
  int number_to_string( const int&      v, char* buf, int bufN, const char* fmt ) { return snprintf(buf,bufN,fmt==nullptr ? "%i" : fmt, v);  }
  
  template < > inline
  int number_to_string( const unsigned& v, char* buf, int bufN, const char* fmt ) { return snprintf(buf,bufN,fmt==nullptr ? "%i" : fmt, v);  }
  
  template < > inline
  int number_to_string( const double&   v, char* buf, int bufN, const char* fmt ) { return snprintf(buf,bufN,fmt==nullptr ? "%f" : fmt, v);  }

  
  
}
#endif




