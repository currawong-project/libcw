#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwSvg.h"
#include "cwText.h"
#include "cwNumericConvert.h"

namespace cw
{
  namespace svg
  {
    enum
    {
     kLineTId,
     kPLineTId,
     kRectTId,
     kTextTId,
     kPixelTId
    };

    // Attribute record used for CSS and SVG element attributes
    typedef struct attr_str
    {
      char*            label;
      char*            value;
      struct attr_str* link;
    } attr_t;

    // CSS selector record with attribute list
    typedef struct css_str
    {
      char*           selectorStr;
      attr_t*         attrL;
      struct css_str* link;
    } css_t;

    // SVG element record
    typedef struct ele_str
    {
      unsigned        typeId;  // kLineTId,kRectPId,...
      attr_t*         attrL;   // element attributes (e.g. id, class)
      css_t*          css;     // style attributes
      double          x0;      // left,top
      double          y0;     
      double          x1;      // right,bot
      double          y1;
      char*           text;
      double*         xV;      // poly line coords
      double*         yV;
      unsigned        yN;
      struct ele_str* link;
    } ele_t;

    typedef struct color_map_str
    {
      unsigned              id;
      unsigned              colorN;
      unsigned*             colorV;
      struct color_map_str* link;
    } color_map_t;

    typedef struct svg_str
    {
      css_t*       cssL;        // CSS selector records
      color_map_t* cmapL;       // Color maps
      ele_t*       eleL;        // SVG elements
      
      double       dx;          // Offset new elements by this amount
      double       dy;
      
      ele_t*       curEle;      // Last SVG element allocated
    } svg_t;

    inline svg_t* _handleToPtr(handle_t h )
    { return handleToPtr<handle_t,svg_t>(h); }

    void _destroy_attr_list( attr_t* a )
    {
      while( a != nullptr )
      {
        attr_t* a0 = a->link;
        mem::release(a->label);
        mem::release(a->value);
        mem::release(a);
        a = a0;
      }      
    }

    void _destroy_css( css_t* r )
    {
      if( r != nullptr )
      {
        mem::release(r->selectorStr);
      
        _destroy_attr_list(r->attrL);
        mem::release(r);
      }
    }
    
    rc_t _destroy( struct svg_str* p )
    {
      ele_t* e = p->eleL;
      while( e != nullptr )
      {
        ele_t* e0 = e->link;
        mem::release(e->text);
        mem::release(e->xV);
        mem::release(e->yV);
        _destroy_attr_list(e->attrL);
        _destroy_css(e->css);
        mem::release(e);
        e = e0;
      }

      color_map_t* c = p->cmapL;
      while( c != nullptr )
      {
        color_map_t* c0 = c->link;
        mem::release(c->colorV);
        mem::release(c);
        c = c0;
      }

      css_t* r = p->cssL;
      while( r != nullptr )
      {
        css_t* r0 = r->link;
        _destroy_css(r);
        r = r0;
      }

      mem::release(p);
      
      return kOkRC;
    }

    rc_t _install_cmap( svg_t* p, unsigned id, unsigned* cV, unsigned cN )
    {
      color_map_t* cmap = mem::allocZ<color_map_t>(1);
      cmap->id      = id;
      cmap->colorN  = cN;
      cmap->colorV  = cV;
      cmap->link    = p->cmapL;
      p->cmapL     = cmap;
      return kOkRC;
    }

    color_map_t* _cmap_from_id( svg_t* p, unsigned id )
    {
      color_map_t* cm;
      for(cm=p->cmapL; cm!=nullptr; cm=cm->link)
        if( cm->id == id )
          return cm;
      
      cwLogWarning("Unknown color map id:%i.", id );
      return nullptr;
    }


    
    rc_t _install_basic_eight_cmap( svg_t* p )
    {
      unsigned  colorN = 8;
      unsigned* colorV = mem::allocZ<unsigned>(colorN);      
      colorV[0] = 0x0000ff;
      colorV[1] = 0x00ff00;
      colorV[2] = 0xff0000;
      colorV[3] = 0x00ffff;
      colorV[4] = 0xffff00;
      colorV[5] = 0xff00ff;
      colorV[6] = 0x000000;
      colorV[7] = 0xffffff;
      return _install_cmap( p, kStandardColorMapId, colorV, colorN );
    }

    rc_t _install_gray_scale_cmap( svg_t* p )
    {
      unsigned  colorN = 256;
      unsigned* colorV = mem::allocZ<unsigned>(colorN);

      for(unsigned i=0; i<colorN; ++i)
        colorV[i] = (i<<16) + (i<<8) + i;
      
      return _install_cmap( p, kGrayScaleColorMapId, colorV, colorN );
    }

    rc_t _install_inv_gray_scale_cmap( svg_t* p )
    {
      color_map_t* cm     = _cmap_from_id(p, kGrayScaleColorMapId );
      unsigned*    colorV = mem::alloc<unsigned>(cm->colorN);
      for(unsigned i=0; i<cm->colorN; ++i)
        colorV[i] = 0xffffff - cm->colorV[i];
        
      return _install_cmap( p, kInvGrayScaleColorMapId, colorV, cm->colorN );
    }

    rc_t _install_heat_cmap( svg_t* p )
    {
      unsigned  colorN  = 256;
      unsigned* colorV  = mem::allocZ<unsigned>(colorN);
      double    rV[]    = { 0.0, 0.0, 1.0, 1.0 };
      double    gV[]    = { 1.0, 0.0, 0.0, 1.0 };
      double    bV[]    = { 1.0, 1.0, 0.0, 0.0 };
      const unsigned xN = cwCountOf(rV);

      for(unsigned i=1,k=0; i<xN; ++i)
      {
        unsigned c = colorN/xN; //64
        for(unsigned j=0; j<c && k<colorN; ++j,++k)
        {
          unsigned r = 255* (rV[i-1] + (j * rV[i]-rV[i-1])/c);
          unsigned g = 255* (gV[i-1] + (j * gV[i]-gV[i-1])/c);
          unsigned b = 255* (bV[i-1] + (j * bV[i]-bV[i-1])/c);
          colorV[k] = (r<<16) + (g<<8) + b;
        }
      }      
      return _install_cmap( p, kHeatColorMapId, colorV, colorN );      
    }

    
    rc_t _insert( svg_t* p, unsigned typeId, double x, double y, double w, double h, const char* text, const double* yV=nullptr, unsigned yN=0, const double* xV=nullptr  )
    {
      rc_t rc = kOkRC;
      
      ele_t* e = mem::allocZ<ele_t>(1);

      e->typeId         = typeId;
      e->x0             = x;
      e->y0             = y;
      e->x1             = x+w;
      e->y1             = y+h;
      e->text           = mem::duplStr(text);
      e->yV             = yN>0 && yV!=nullptr ? mem::allocDupl<double>(yV,yN) : nullptr;
      e->xV             = yN>0 && xV!=nullptr ? mem::allocDupl<double>(xV,yN) : nullptr;
      e->yN             = yN;

      if( yN == 0 )
      {
        e->x0 += p->dx;
        e->y0 += p->dy;
        e->x1 += p->dx;
        e->y1 += p->dy;        
      }
      else
      {
          
        if( e->xV == nullptr )
        {
          e->xV = mem::alloc<double>(e->yN);
          for(unsigned i=0; i<e->yN; ++i)
            e->xV[i] = i;
        }
        
        for(unsigned i=0; i<yN; ++i)
        {
          e->yV[i] += p->dy;
          e->xV[i] += p->dx;
        }
      }
      
      ele_t* e0 = nullptr;
      ele_t* e1 = p->eleL;
      while( e1!=nullptr)
      {
        e0 = e1;
        e1 = e1->link;
      }

      if( e0 == NULL )
        p->eleL = e;
      else
        e0->link = e;

      p->curEle = e;
      
      return rc;
    }

    void _offsetEles(svg_t* p, double dx, double dy )
    {
      for(ele_t* e=p->eleL; e!=nullptr; e=e->link)
      {
        if( e->yN == 0 )
        {
          e->x0 += dx;
          e->x1 += dx;
          e->y0 += dy;
          e->y1 += dy;
        }
        else
        {                    
          for(unsigned i=0; i<e->yN; ++i)
          {
            e->yV[i] += dy;
            e->xV[i] += dx;
          }
        }
      }
    }
    
    void _ele_extents( ele_t* e, double& min_x, double& min_y, double& max_x, double& max_y )
    {
      if( e->yN==0 )
      {
        min_x = std::min(e->x0,e->x1);
        max_x = std::max(e->x0,e->x1);
        min_y = std::min(e->y0,e->y1);
        max_y = std::max(e->y0,e->y1);
      }
      else
      {
        min_y = e->yV[0];
        max_y = e->yV[0];
        min_x = e->xV[0];
        max_x = e->xV[0];

        for(unsigned i=0; i<e->yN; ++i)
        {
          min_x = std::min( min_x, e->yV[i] );
          max_x = std::max( max_x, e->yV[i] );
          min_y = std::min( min_y, e->xV[i] );
          max_y = std::max( max_y, e->xV[i] );
        }
      }
    }
    

    void _extents( svg_t* p, double& min_x, double& min_y, double& max_x, double& max_y )
    {
      if( p->eleL == NULL )
        return; 
  
      ele_t*      e     = p->eleL;
      _ele_extents(e,min_x,min_y,max_x,max_y);

      for(e=e->link; e!=NULL; e=e->link)
      {
        double e_min_x,e_min_y,e_max_x,e_max_y;
        _ele_extents(e,e_min_x,e_min_y,e_max_x,e_max_y);
        
        min_x = std::min(min_x,e_min_x);
        max_x = std::max(max_x,e_max_x);
        min_y = std::min(min_y,e_min_y);
        max_y = std::max(max_y,e_max_y);
      }
    }
    
    double _size( svg_t* p, double& widthRef, double& heightRef )
    {
      double min_x,min_y,max_x,max_y;
      widthRef  = 0;
      heightRef = 0;

      _extents( p, min_x, min_y, max_x, max_y );
      
      widthRef  = max_x - min_x;
      heightRef = max_y - min_y;

      return max_y;
    }

    void _flipY( svg_t* p, unsigned height )
    {
      ele_t* e = p->eleL;
      for(; e!=NULL; e=e->link)
      {
        if( e->yN == 0 )
        {
          e->y0 = (-e->y0) + height;
          e->y1 = (-e->y1) + height;

          if( e->typeId == kRectTId )
          {
            double t = e->y1;
            e->y1 = e->y0;
            e->y0 = t;
          }
        }
        else
        {
          for(unsigned i=0; i<e->yN; ++i)
            e->yV[i] =  (-e->yV[i]) + height;
        }
        
      }
    }

    // Write a HTML element attribute list
    char* _print_ele_attr_list( const attr_t* attrL, char*& s )
    {
      for(const attr_t* a=attrL; a!=nullptr; a=a->link)
        s = mem::printp(s,"%s=\"%s\" ",a->label,a->value);
      return s;
    }
    
    // Write a CSS record attribute list
    char* _print_css_attr_list( const attr_t* attrL, char*& s )
    {
      for(const attr_t* a=attrL; a!=nullptr; a=a->link)
        s = mem::printp(s,"%s: %s;",a->label,a->value);
      return s;
    }

    // Print a CSS record
    char* _print_css( const css_t* r, char*& s )
    {
      char* s0 = nullptr;
      s0 = _print_css_attr_list( r->attrL, s0 );
      s = mem::printp(s,"%s { %s }\n", r->selectorStr, s0 );
      mem::release(s0);
      return s;
    }
    
    char* _print_css_list( svg_t* p, char*& s )
    {
      for( const css_t* r=p->cssL; r!=nullptr; r=r->link)
        s = _print_css(r,s);

      return s;
    }

    rc_t _writeCssFile( svg_t* p, const char* fn )
    {
      rc_t           rc;
      file::handle_t fH;
      char*          s = nullptr;

      if( p->cssL == nullptr )
        return kOkRC;

      if((rc = file::open(fH,fn,file::kWriteFl)) != kOkRC )
        return cwLogError(rc,"CSS file create failed on '%s'.",cwStringNullGuard(fn));

      s = _print_css_list(p,s);

      file::printf(fH,"%s\n",s);
      
      mem::release(s);
      file::close(fH);
      return rc;
    }

    css_t* _cssSelectorToRecd( svg_t* p, const char* selectorStr )
    {
      css_t* r = p->cssL;
      for(; r!=nullptr; r=r->link)
        if( strcmp(selectorStr,r->selectorStr)==0 )
          return r;

      return nullptr;
    }

    css_t* _cssCreate( svg_t* p )
    {
      return  mem::allocZ<css_t>();
    }

    css_t* _cssFindOrCreate( svg_t* p, const char* selectorStr  )
    {
      css_t* r;
      if((r = _cssSelectorToRecd(p,selectorStr)) == nullptr )
      {
        r              = _cssCreate(p);
        r->selectorStr = mem::duplStr(selectorStr);
        r->link        = p->cssL;
        p->cssL        = r;
      }
      
      return r;
    }

    char*  _cssGenUniqueIdLabel( svg_t* p )
    {
      unsigned i = 0;
      const unsigned bufN = 64;
      char buf[ bufN + 1];
      do
        {
          snprintf(buf,bufN,"#id_%i",i);

          if( _cssSelectorToRecd(p,buf) == nullptr )
            return mem::duplStr(buf);

          i += 1;
        }while(1);

      return nullptr;
    }

    template< typename T >
    rc_t _set_attr_int( handle_t h, const char* selectorStr, const char* attrLabel, const T& value, const char* suffix )
    {
      const int bufN = 64;
      char buf[ bufN + 1 ];

      if( suffix == nullptr )
        suffix = "";
      else
      {
        if( strcmp(suffix,"rgb") == 0 )
        {
          snprintf(buf,bufN,"#%06x",value);
        }
        else
        {
          snprintf(buf,bufN,"%i%s",value,suffix);
        }
      }
      
      return _set_attr( h, selectorStr, attrLabel, buf, nullptr );
    }

    
    rc_t _write_pline(file::handle_t fH, ele_t* e, const char* styleStr)
    {
      rc_t rc = kOkRC;
      
      if( e->yN == 0 )
        return rc;
  
      file::printf(fH,"<polyline points=\"");
      for(unsigned i=0; i<e->yN; ++i)
        if((rc = file::printf(fH,"%f,%f ", e->xV[i], e->yV[i] )) != kOkRC)
          return rc;
          
  
      return file::printf(fH,"\" %s />\n", styleStr);
    }
  }
}

cw::rc_t cw::svg::create(  handle_t& h )
{
  rc_t rc;
  if((rc = destroy(h)) != kOkRC )
    return rc;

  svg_t* p = mem::allocZ<svg_t>();

  _install_basic_eight_cmap(p);
  _install_gray_scale_cmap(p);
  _install_inv_gray_scale_cmap(p);
  _install_heat_cmap(p);
  
  h.set(p);

  return rc;
}

cw::rc_t cw::svg::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  if( !h.isValid())
    return rc;

  svg_t* p = _handleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;  
}

cw::rc_t cw::svg::install_color_map( handle_t h, const unsigned* colorV, unsigned colorN, unsigned id )
{ return _install_cmap( _handleToPtr(h), id, mem::allocDupl<unsigned>(colorV,colorN), colorN ); }

void cw::svg::offset( handle_t h, double dx, double dy )
{
  svg_t* p = _handleToPtr(h);
  p->dx = dx;
  p->dy = dy;
}

unsigned cw::svg::color( handle_t h, unsigned colorMapId, double colorMin, double colorMax, double colorValue )
{
  color_map_t* cm;
  svg_t* p = _handleToPtr(h);
  if((cm = _cmap_from_id(p, colorMapId )) == nullptr )
  {
    cwLogWarning("Unknown color map id:%i.", colorMapId );
    return 0;
  }

  double   c   =  std::min( colorMax, std::max( colorMin, colorValue ) );
  unsigned idx = (cm->colorN-1) * (c - colorMin)/(colorMax - colorMin);

  assert(idx<cm->colorN);
  
  return cm->colorV[idx];
}

unsigned cw::svg::color( handle_t h, unsigned colorMapId, unsigned colorIdx )
{
  color_map_t* cm;
  svg_t* p = _handleToPtr(h);
  if((cm = _cmap_from_id(p, colorMapId )) == nullptr )
    return 0;

  colorIdx = colorIdx % cm->colorN;
  
  return cm->colorV[colorIdx];
}

cw::rc_t cw::svg::_set_attr( handle_t h, const char* selectorStr, const char* attrLabel, const char* value, const char* suffix )
{
  svg_t* p    = _handleToPtr(h);
  css_t* r    = nullptr;
  int    bufN = 64;
  char   buf[ bufN + 1 ];

  if( suffix != nullptr )
  {
    snprintf(buf,bufN,"%s%s",value,suffix);
    value = buf;
  }
  
  // allocate and fill the CSS attribute record
  attr_t* a = mem::allocZ<attr_t>();
  a->label  = mem::duplStr(attrLabel);
  a->value  = mem::duplStr(value);

  // if a selector is given then find or create a CSS selector record
  if( selectorStr != nullptr )    
    r = _cssFindOrCreate(p,selectorStr);
  else
  {  
    // 'id' and 'class' attributes are always added to the ele attribute list ...
    if( strcmp(attrLabel,"id")!=0 && strcmp(attrLabel,"class")!=0 )
    {
      // ... otherwise the attributes are added to the ele style list
      if( p->curEle->css == nullptr )
        p->curEle->css = _cssCreate(p);
      r = p->curEle->css;
    }
  }

  if( r != nullptr )
  {
    a->link   = r->attrL;
    r->attrL  = a;
  }
  else
  {
    a->link          = p->curEle->attrL;
    p->curEle->attrL = a;      
  }
  return kOkRC;    
}



cw::rc_t cw::svg::_set_attr( handle_t h, const char* selectorStr, const char* attrLabel, const unsigned& value, const char* suffix )
{
  return _set_attr_int(h,selectorStr,attrLabel,value,suffix);
}

cw::rc_t cw::svg::_set_attr( handle_t h, const char* selectorStr, const char* attrLabel, const int& value, const char* suffix )
{
  return _set_attr_int(h,selectorStr,attrLabel,value,suffix);
}

cw::rc_t cw::svg::_set_attr( handle_t h, const char* selectorStr, const char* attrLabel, const double& value, const char* suffix )
{
  const int bufN = 32;
  char buf[ bufN+1 ];
  number_to_string(value,buf,bufN);
  return _set_attr(h,selectorStr,attrLabel,buf,suffix);
}

cw::rc_t cw::svg::_rect( handle_t h, double x,  double y,  double ww, double hh )
{
  svg_t* p = _handleToPtr(h);
  return _insert( p, kRectTId, x, y, ww, hh, nullptr);  
}

cw::rc_t cw::svg::_line( handle_t h, double x0,  double y0,  double x1, double y1 )
{
  svg_t* p = _handleToPtr(h);
  return _insert( p, kLineTId, x0, y0, x1-x0, y1-y0, nullptr);  
}

cw::rc_t cw::svg::_pline( handle_t h, const double* yV,  unsigned n,  const double* xV )
{
  svg_t* p = _handleToPtr(h);
  return _insert( p, kPLineTId, 0,0,0,0, nullptr, yV, n, xV);    
}

cw::rc_t cw::svg::_text( handle_t h, double x, double y, const char* text )
{
  svg_t* p = _handleToPtr(h);
  return _insert( p, kTextTId, x, y, 0, 0, text);  
}

cw::rc_t cw::svg::image( handle_t h, const float* xM, unsigned rowN, unsigned colN, unsigned pixSize, unsigned cmapId )
{
  svg_t* p       = _handleToPtr(h);
  char*  idLabel = _cssGenUniqueIdLabel(p);

  _parse_attr( h, idLabel,
    "width",          pixSize, "px",
    "height",         pixSize, "px",
    "stroke-width",   1,       "px",
    "stroke-opacity", 1.0,     nullptr,
    "fill-opacity",   1.0,     nullptr );
  
  for(unsigned i=0; i<rowN; ++i)
    for(unsigned j=0; j<colN; ++j)
    {
      int       c    = color( h, cmapId, 0, 1, xM[j*colN + i + 1] );

      _insert(p, kPixelTId, i*pixSize, j*pixSize, pixSize, pixSize, nullptr  );
      _set_attr( h, nullptr, "stroke", c, "rgb" );
      _set_attr( h, nullptr, "fill",   c, "rgb" );
      _set_attr( h, nullptr, "id", idLabel+1, nullptr );
      
    }

  mem::release(idLabel);
  
  return kOkRC;
}
  
cw::rc_t cw::svg::write( handle_t h, const char* outFn, const char* cssFn, unsigned flags, double bordL, double bordT, double bordR, double bordB)
{
  rc_t   rc               = kOkRC;
  svg_t* p                = _handleToPtr(h);
  double svgWidth         = 0;
  double svgHeight        = 0;
  char*  cssStr           = nullptr;
  char*  styleStr        = nullptr;
  char*  fileHdr          = nullptr;
  char*  svgHdr           = nullptr;
  ele_t* e                = p->eleL;
  bool   standAloneFl     = cwIsFlag(flags,kStandAloneFl); 
  bool   panZoomFl        = cwIsFlag(flags,kPanZoomFl);
  bool   genCssFileFl     = cwIsFlag(flags,kGenCssFileFl) && cssFn!=nullptr;

  file::handle_t fH;

  char panZoomHdr[] = 
    "<script type=\"text/javascript\" src=\"svg-pan-zoom/dist/svg-pan-zoom.js\"></script>\n"
    "<script>\n"
    " var panZoom = null;\n"
    "  function doOnLoad() { panZoom = svgPanZoom(document.querySelector('#mysvg'), { controlIconsEnabled:true } ) }\n"
    "</script>\n";

  
  char standAloneFmt[] =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "<meta charset=\"utf-8\">\n"
    "%s"
    "<style>\n%s</style>\n"
    "%s\n"
    "</head>\n"
    "<body onload=\"doOnLoad()\">\n";

  char svgFmt[] = "<svg id=\"mysvg\" width=\"%f\" height=\"%f\">\n";
  char cssFmt[] = "<link rel=\"stylesheet\" type=\"text/css\" href=\"%s\">\n";

  //double max_y = _size(p, svgWidth, svgHeight );
  //_flipY( p, max_y );
  _size(p,svgWidth,svgHeight);

  _offsetEles(p, bordR, bordT );
  
  svgWidth  += bordR + bordL;
  svgHeight += bordT + bordB;


  if( p->cssL != nullptr )
  {
    if( genCssFileFl )
      _writeCssFile( p, cssFn );
    else
      styleStr = _print_css_list(p,styleStr);
  }

  cssStr  = mem::printf(cssStr,cssFn==nullptr ? "%s" : cssFmt, cssFn==nullptr ? " " : cssFn);

  fileHdr = mem::printf(fileHdr,standAloneFmt, cssStr, styleStr==nullptr ? "" : styleStr, panZoomFl ? panZoomHdr : "");
  
  svgHdr  = mem::printf(svgHdr,"%s%s", standAloneFl ? fileHdr : "", svgFmt);

  fileHdr = mem::printf(fileHdr,svgHdr,svgWidth,svgHeight);

  mem::release(svgHdr);
  mem::release(cssStr);
  mem::release(styleStr);


  if((rc = file::open(fH,outFn,file::kWriteFl)) != kOkRC )
  {
    rc = cwLogError(rc,"SVG file create failed for '%s'.",cwStringNullGuard(outFn));
    goto errLabel;
  }

  if((rc = file::printf(fH,"%s",fileHdr)) != kOkRC )
    goto errLabel;

  for(; e!=NULL; e=e->link)
  {
    char* dStyleStr = nullptr;

    if( e->css != nullptr )
      dStyleStr = mem::printf(dStyleStr, "style=\"%s\" ",_print_css_attr_list(e->css->attrL,dStyleStr));

    if( e->attrL != nullptr )
      dStyleStr = _print_ele_attr_list(e->attrL,dStyleStr);
    
    const char* styleStr  = dStyleStr==nullptr ? "" : dStyleStr;
        
    switch( e->typeId )
    {
      case kRectTId:
        rc = file::printf(fH,"<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\" %s/>\n",e->x0,e->y0,e->x1-e->x0,e->y1-e->y0,styleStr);
        break;

      case kPixelTId:
        rc = file::printf(fH,"<rect x=\"%f\" y=\"%f\" %s/>\n",e->x0,e->y0,e->x1-e->x0,e->y1-e->y0,styleStr);
      break;
      
      case kLineTId:
        rc = file::printf(fH,"<line x1=\"%f\" y1=\"%f\" x2=\"%f\" y2=\"%f\"  %s/>\n",e->x0,e->y0,e->x1,e->y1,styleStr);
        break;

      case kPLineTId:
        rc = _write_pline(fH,e,styleStr);
        break;
        
      case kTextTId:
        rc   = file::printf(fH,"<text x=\"%f\" y=\"%f\"  %s>%s</text>\n",e->x0,e->y0, styleStr,e->text);
        break;
    }

    mem::release(dStyleStr);

    if( rc != kOkRC )
      goto errLabel;
  }

  if( (rc = file::printf(fH,"</svg>\n")) != kOkRC )
  {
    rc = cwLogError(kMemAllocFailRC,"File suffix write failed.");
    goto errLabel;
  }
  
  if( standAloneFl )
    rc = file::printf(fH,"</body>\n</html>\n");
  

 errLabel:

  if( rc != kOkRC )
    rc = cwLogError(rc,"SVG file write failed.");
  file::close(fH);

    mem::release(fileHdr);
  
    return rc;  
}

cw::rc_t cw::svg::test( const char* outFn, const char* cssFn )
{
  rc_t     rc = kOkRC;
  handle_t h;
  
  if((rc = create(h)) != kOkRC )
    cwLogError(rc,"SVG Test failed on create.");


  double yV[] = { 0, 10, 30, 60, 90 };
  double xV[] = { 0, 40, 60, 40, 10 };
  unsigned yN = cwCountOf(yV);

  install_css(h,"#my_rect","fill-opacity",0.25,nullptr);

  rect(h,  0,  0, 100, 100, "fill",   0x7f7f7f, "rgb", "id", "my_rect", nullptr );
  line(h,  0,  0, 100, 100, "stroke", 0xff0000, "rgb" );
  line(h,  0,100, 100,   0, "stroke", 0x00ff00, "rgb", "stroke-width", 3, "px", "stroke-opacity", 0.5, nullptr );
  pline(h, yV, yN,  xV,  "stroke", 0x0, "rgb", "fill-opacity", 0.25, nullptr );
  text(h, 10, 10, "foo" );
  
  float imgM[] = {
    0.0f, 0.5f, 1.0f,
    0.5f, 0.0f, 0.5f,
    1.0f, 1.0f, 0.0f,
    0.5f, 0.0f, 1.0f };

  offset( h, 10, 200 );
  image(h, imgM, 4, 3, 20, kInvGrayScaleColorMapId );
  

  write(h,outFn, cssFn, kStandAloneFl, 10,10,10,10);
  
  if((rc = destroy(h)) != kOkRC )
    cwLogError(rc,"SVG destroy failed.");
  
  return rc;
  
}
