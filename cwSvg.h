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

    typedef enum
    {
     kStrokeColorArgId   = 0x01,
     kStrokeWidthArgId   = 0x02,
     kStrokeOpacityArgId = 0x04,
     kFillColorArgId     = 0x08,
     kFillOpacityArgId   = 0x10
    } argId_t;

    enum
    {
     kRectCssId,
     kLineCssId,
     kPLineCssId,
     kTextCssId,
     kBaseCssId
    };


    rc_t create(  handle_t& h );
    rc_t destroy( handle_t& h );

    rc_t install_color_map( handle_t h, const unsigned* colorV, unsigned colorN, unsigned colorId=kBaseUserColorMapId );

    rc_t install_css(
      handle_t    h,
      unsigned    cssClassId,
      const char* cssClass       = nullptr,
      unsigned    strokeColor    = 0,
      unsigned    strokeWidth    = 1,
      unsigned    fillColor      = 0xffffff,      
      unsigned    strokeOpacity  = 1.0,
      double      fillOpacity    = 1.0 );

    void     offset( handle_t h, double dx, double dy );
    unsigned color( handle_t h, unsigned colorMapId, double colorMin, double colorMax, double colorValue );
    unsigned color( handle_t h, unsigned colorMapId, unsigned colorIdx );
    
    rc_t _set_attr( handle_t h, argId_t id, const int& value );
    rc_t _set_attr( handle_t h, argId_t id, double value );
    
    inline rc_t _parse_attr( handle_t h ){ return kOkRC; }
    
    template<typename T0, typename T1, typename ...ARGS>
      rc_t _parse_attr( handle_t h, T0 id, T1 val, ARGS&&... args )
    {
      rc_t rc;
      if((rc = _set_attr(h, id,val)) == kOkRC )        
        rc =_parse_attr( h, std::forward<ARGS>(args)...);
      
      return rc;
    }

    rc_t _rect( handle_t h, double x,  double y,  double ww, double hh, unsigned cssClassId );

    // Draw a rectangle.  The variable arg. list must be <argId>,<value> pairs.
    template<typename ...ARGS>
      rc_t rect(  handle_t h, double x,  double y,  double ww, double hh, unsigned cssClassId, ARGS&&... args  )
    {
      _rect( h, x, y, ww, hh, cssClassId );
      return _parse_attr( h, std::forward<ARGS>(args)...);
    }

    rc_t _line( handle_t h, double x0,  double y0,  double x1, double y1, unsigned cssClassId );
    
    // Draw a line.  The variable arg. list must be <argId>,<value> pairs.
    template<typename ...ARGS>
      rc_t line(  handle_t h, double x0,  double y0,  double x1, double y1, unsigned cssClassId=kLineCssId, ARGS&&... args  )
    {
      _line( h, x0, y0, x1, y1, cssClassId );
      return _parse_attr( h, std::forward<ARGS>(args)...);
    }

    rc_t _pline( handle_t h, const double* yV,  unsigned n,  const double* xV, unsigned cssClassId );
    
    // Draw a poly-line.  The variable arg. list must be <argId>,<value> pairs.
    template<typename ...ARGS>
      rc_t pline(  handle_t h, const double* yV,  unsigned n,  const double* xV=nullptr, unsigned cssClassId=kPLineCssId, ARGS&&... args  )
    {
      _pline( h, yV, n, xV, cssClassId );
      return _parse_attr( h, std::forward<ARGS>(args)...);
    }
    
    rc_t _text( handle_t h, double x, double y, const char* text, unsigned cssClassId );      
    
    // Draw text.  The variable arg. list must be <argId>,<value> pairs.
    template<typename ...ARGS>
      rc_t text(  handle_t h, double x,  double y, const char* text, unsigned cssClassId=kTextCssId, ARGS&&... args )
    {
      _text( h, x, y, text, cssClassId );
      return _parse_attr( h, std::forward<ARGS>(args)...);
    }


    rc_t image( handle_t h, const float* xM, unsigned rowN, unsigned colN, unsigned pixSize, unsigned cmapId );

    // Write the SVG file.
    enum { kStandAloneFl=0x01, kPanZoomFl=0x02, kGenCssFileFl=0x04, kGenInlineStyleFl=0x08, kDrawFrameFl=0x10 };
    rc_t write( handle_t h, const char* outFn, const char* cssFn, unsigned flags, double bordL=5, double bordT=5, double bordR=5, double bordB=5 );

    rc_t test( const char* outFn, const char* cssFn );

  }
}


#endif
