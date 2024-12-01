//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwSvg_h
#define cwSvg_h

namespace cw
{
  namespace svg
  {
    typedef handle<struct svg_str> handle_t;

    enum
    {
     kStandardColorMapId,
     kGrayScaleColorMapId,
     kInvGrayScaleColorMapId,
     kHeatColorMapId,
     kBaseUserColorMapId
    };

    rc_t create(  handle_t& h );
    rc_t destroy( handle_t& h );

    rc_t install_color_map( handle_t h, const unsigned* colorV, unsigned colorN, unsigned colorId=kBaseUserColorMapId );

    void     offset( handle_t h, double dx, double dy );
    unsigned color( handle_t h, unsigned colorMapId, double colorMin, double colorMax, double colorValue );
    unsigned color( handle_t h, unsigned colorMapId, unsigned colorIdx );
    
    rc_t _set_attr( handle_t h, const char* selectorStr, const char* attrLabel, const char*     value, const char* suffix );
    rc_t _set_attr( handle_t h, const char* selectorStr, const char* attrLabel, const unsigned& value, const char* suffix );
    rc_t _set_attr( handle_t h, const char* selectorStr, const char* attrLabel, const int&      value, const char* suffix );
    rc_t _set_attr( handle_t h, const char* selectorStr, const char* attrLabel, const double&   value, const char* suffix );
    
    inline rc_t _parse_attr( handle_t h, const char* selectorStr ){ return kOkRC; }
    
    template<typename T, typename ...ARGS>
      rc_t _parse_attr( handle_t h, const char* selectorStr, const char* attrLabel, const T& val, const char* suffix, ARGS&&... args )
    {
      rc_t rc;
      if((rc = _set_attr(h, selectorStr, attrLabel, val, suffix)) == kOkRC )        
        rc =_parse_attr( h, selectorStr, std::forward<ARGS>(args)...);
      
      return rc;
    }

    // Install a CSS selector record.
    // Style attributes are encoded as triples "<label>" <value> "<suffix>".
    // Color values are encoded with the <suffix> = "rgb"
    template< typename ...ARGS>
    rc_t install_css( handle_t h, const char* selectorStr, ARGS&&... args )
    { return _parse_attr( h, selectorStr, std::forward<ARGS>(args)...); }

    rc_t _rect( handle_t h, double x,  double y,  double ww, double hh );

    // Draw a rectangle.  The variable arg. list must be <argId>,<value> pairs.
    // All attributes assigned here will be encoded inside the SVG element tag.
    // Attributes are encoded as triples "<label>" <value> "<suffix>".
    // Color values are encoded with the <suffix> = "rgb"
    template<typename ...ARGS>
    rc_t rect(  handle_t h, double x,  double y,  double ww, double hh, ARGS&&... args  )
    {
      _rect( h, x, y, ww, hh );
      return _parse_attr( h, nullptr, std::forward<ARGS>(args)...);
    }

    rc_t _line( handle_t h, double x0,  double y0,  double x1, double y1 );
    

    // Draw a line.  The variable arg. list must be <argId>,<value> pairs.
    template<typename ...ARGS>
    rc_t line(  handle_t h, double x0,  double y0,  double x1, double y1, ARGS&&... args  )
    {
      _line( h, x0, y0, x1, y1 );
      return _parse_attr( h, nullptr, std::forward<ARGS>(args)...);
    }

    rc_t _pline( handle_t h, const double* yV,  unsigned n,  const double* xV );
    
    // Draw a poly-line.  The variable arg. list must be <argId>,<value> pairs. Set xV to nullptr to use index as x.
    template<typename ...ARGS>
    rc_t pline(  handle_t h, const double* yV,  unsigned n,  const double* xV, ARGS&&... args  )
    {
      _pline( h, yV, n, xV );
      return _parse_attr( h, nullptr, std::forward<ARGS>(args)...);
    }
    
    rc_t _text( handle_t h, double x, double y, const char* text );      
    
    // Draw text.  The variable arg. list must be <argId>,<value> pairs.
    template<typename ...ARGS>
    rc_t text(  handle_t h, double x,  double y, const char* text, ARGS&&... args )
    {
      _text( h, x, y, text );
      return _parse_attr( h, nullptr, std::forward<ARGS>(args)...);
    }

    
    rc_t image( handle_t h, const float* xM, unsigned rowN, unsigned colN, unsigned pixSize, unsigned cmapId );

    // Write the SVG file.
    enum { kStandAloneFl=0x01, kPanZoomFl=0x02, kGenCssFileFl=0x04 };
    rc_t write( handle_t h, const char* outFn, const char* cssFn, unsigned flags, double bordL=5, double bordT=5, double bordR=5, double bordB=5 );

    rc_t test( const char* outFn, const char* cssFn );

  }
}


#endif
