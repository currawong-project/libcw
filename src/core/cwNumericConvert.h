//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwNumericConvert_H
#define cwNumericConvert_H

namespace cw
{    
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
    rc_t rc = kOkRC;
    
    valueRef = 0;
    
    if( s == nullptr )
    {
      rc = cwLogError(kInvalidArgRC,"string_to_number<T>() failed on null input.");
    }
    else
    {
      int base = 10;
      errno = 0;

      if( strlen(s) >= 2 && s[0]=='0' && s[1]=='x' )
        base = 16;
      
      long v = strtol(s,nullptr,base);
      
      if( v == 0 && errno != 0)
      {
        rc =  cwLogError(kOpFailRC,"String to number conversion failed on '%s'.", cwStringNullGuard(s));
      }
      else
      {
        rc = numeric_convert(v,valueRef);
      }
      
    }
    return rc;
  }

  template < > inline
  rc_t string_to_number<double>( const char* s, double& valueRef )
  {
    rc_t rc = kOkRC;
    
    valueRef = 0;
    
    if( s == nullptr )
    {
      rc = cwLogError(kInvalidArgRC,"string_to_number<double>() failed on null input.");
    }
    else
    {
      errno = 0;
      
      valueRef = strtod(s,nullptr);
      
      if( valueRef == 0 && errno != 0)
        rc = cwLogError(kOpFailRC,"String to number conversion failed on '%s'.", cwStringNullGuard(s));
            
    }
    return rc;
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
    rc_t rc = kOkRC;

    valueRef = false;
    
    s = nextNonWhiteChar(s);
    
    if( s == nullptr )
    {
      rc = cwLogError(kInvalidArgRC,"string_to_number<bool>() failed on null input.");
    }
    else
    {
      if( strncmp( "true", s, 4) == 0 )
        valueRef = true;
      else
      {
        if( strncmp( "false", s, 5) == 0 )
          valueRef = false;
        else
          rc = cwLogError(kOpFailRC,"String to bool conversion failed on '%s'.", cwStringNullGuard(s));
      }
    }
    return rc;
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

  rc_t numericConvertTest( const test::test_args_t& args );

  
}
#endif




