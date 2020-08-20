#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwSvg.h"
#include "cwText.h"

namespace cw
{
  namespace svg
  {
    enum
    {
     kLineTId,
     kPLineTId,
     kRectTId,
     kTextTId
    };

    typedef struct attr_str
    {
      unsigned        strokeColor;
      unsigned        strokeWidth;
      double          strokeOpacity;
      unsigned        fillColor;
      double          fillOpacity;
    } attr_t;

    typedef struct css_str
    {
      unsigned        id;
      char*           label;
      attr_t          attr;
      struct css_str* link;
    } css_t;
    
    typedef struct ele_str
    {
      unsigned        typeId;
      double          x0;
      double          y0;
      double          x1;
      double          y1;
      char*           text;
      double*         xV;
      double*         yV;
      unsigned        yN;
      attr_t          attr;
      unsigned        cssClassId;
      unsigned        flags;      
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
      unsigned        strokeColorIdx;
      unsigned        strokeWidth;
      unsigned        fillColorIdx;

      css_t*       cssL;
      color_map_t* cmapL;
      ele_t*       eleL;

      double dx;
      double dy;

      ele_t* curEle;
    } svg_t;

    inline svg_t* _handleToPtr(handle_t h )
    { return handleToPtr<handle_t,svg_t>(h); }
    
    rc_t _destroy( struct svg_str* p )
    {
      ele_t* e = p->eleL;
      while( e != nullptr )
      {
        ele_t* e0 = e->link;
        mem::release(e->text);
        mem::release(e->xV);
        mem::release(e->yV);
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
        mem::release(r->label);
        mem::release(r);
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
      unsigned  colorN = 255;
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
      unsigned  colorN  = 255;
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


    rc_t _install_css(
      svg_t*      p,
      unsigned    cssClassId,
      const char* cssClassLabel,
      unsigned    strokeColor   = 0,
      unsigned    strokeWidth   = 1,
      unsigned    fillColor     = 0xffffff,      
      unsigned    strokeOpacity = 1.0,
      double      fillOpacity   = 1.0 )
    {
      css_t* r = mem::allocZ<css_t>(1);

      r->id                 = cssClassId;
      r->label              = mem::duplStr(cssClassLabel==nullptr?" ":cssClassLabel);
      r->attr.strokeColor   = strokeColor;
      r->attr.strokeWidth   = strokeWidth;
      r->attr.strokeOpacity = strokeOpacity;
      r->attr.fillColor     = fillColor;
      r->attr.fillOpacity   = fillOpacity;

      r->link = p->cssL;
      p->cssL = r;

      return kOkRC;
    }
    
    rc_t _insert( svg_t* p, unsigned typeId, double x, double y, double w, double h, unsigned cssClassId, const char* text, const double* yV=nullptr, unsigned yN=0, const double* xV=nullptr  )
    {
      rc_t rc = kOkRC;
      
      ele_t* e = mem::allocZ<ele_t>(1);

      e->typeId         = typeId;
      e->x0             = x;
      e->y0             = y;
      e->x1             = x+w;
      e->y1             = y+h;
      e->text           = mem::duplStr(text);
      e->cssClassId     = cssClassId;
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

    rc_t _writeCssFile( svg_t* p, const char* fn )
    {
      rc_t           rc;
      file::handle_t fH;
      css_t*         r;
      
      if((rc = file::open(fH,fn,file::kWriteFl)) != kOkRC )
        return cwLogError(rc,"CSS file create failed on '%s'.",cwStringNullGuard(fn));

      for(r=p->cssL; r!=nullptr; r=r->link)
      {
        file::printf(fH,".%s {\nstroke:#%06x;\nfill:#%06x;\nstroke-width:%i;\nstroke-opacity:%f;\nfill-opacity:%f\n}\n",
          r->label,r->attr.strokeColor,r->attr.fillColor,r->attr.strokeWidth,r->attr.strokeOpacity,r->attr.fillOpacity);
      }

      file::close(fH);
      return rc;
    }

    css_t* _cssIdToRecd( svg_t* p, unsigned cssClassId )
    {
      css_t* r = p->cssL;
      for(; r!=nullptr; r=r->link)
        if( r->id == cssClassId )
          return r;

      return nullptr;
    }

    rc_t _allocStyleString( svg_t* p, const ele_t* e, bool genInlineFl, char*& s )
    {
      s = nullptr;
      
      css_t* r;
      if((r = _cssIdToRecd(p,e->cssClassId)) == nullptr )
        return cwLogError(kGetAttrFailRC,"Unable to locate SVG class id %i.", e->cssClassId );

      if( genInlineFl || e->flags != 0 )
      {
        attr_t a;
        
        a.strokeColor   =  cwIsFlag(e->flags,kStrokeColorArgId)     ? e->attr.strokeColor   : r->attr.strokeColor;
        a.strokeOpacity =  cwIsFlag(e->flags,kStrokeOpacityArgId)   ? e->attr.strokeOpacity : r->attr.strokeOpacity;
        a.strokeWidth   =  cwIsFlag(e->flags,kStrokeWidthArgId)     ? e->attr.strokeWidth   : r->attr.strokeWidth;
        a.fillColor     =  cwIsFlag(e->flags,kFillColorArgId)       ? e->attr.fillColor     : r->attr.fillColor;
        a.fillOpacity   =  cwIsFlag(e->flags,kFillOpacityArgId)     ? e->attr.fillOpacity   : r->attr.fillOpacity;
        
        s = mem::printf(s,"style=\"stroke:#%06x;fill:#%06x;stroke-width:%i;stroke-opacity:%f;fill-opacity:%f\"",a.strokeColor,a.fillColor,a.strokeWidth,a.strokeOpacity,a.fillOpacity);
      }
      else
      {
        s = mem::printf(s,"class=\"%s\"",r->label);
      }

      return kOkRC;
    }

    char* _write_pline(char* bodyStr, ele_t* e, const char* styleStr)
    {
      if( e->yN == 0 )
        return bodyStr;
  
      bodyStr = mem::printp(bodyStr,"<polyline points=\"");
      for(unsigned i=0; i<e->yN; ++i)
        bodyStr = mem::printp(bodyStr,"%f,%f ", e->xV[i], e->yV[i] );
  
      return mem::printp(bodyStr,"\" %s />",styleStr);
    }

    
  }
}

cw::rc_t cw::svg::create(  handle_t& h )
{
  rc_t rc;
  if((rc = destroy(h)) != kOkRC )
    return rc;

  svg_t* p = mem::allocZ<svg_t>(1);

  p->strokeColorIdx = kInvalidIdx;
  p->strokeWidth    = 1;
  p->fillColorIdx   = kInvalidIdx;

  _install_basic_eight_cmap(p);
  _install_gray_scale_cmap(p);
  _install_inv_gray_scale_cmap(p);
  _install_heat_cmap(p);

  _install_css(p, kRectCssId, "rect" );
  _install_css(p, kLineCssId, "line" );
  _install_css(p, kPLineCssId,"pline" );
  _install_css(p, kTextCssId, "text",0,1,0 );
  
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

cw::rc_t cw::svg::install_css(
      handle_t    h,
      unsigned    cssClassId,
      const char* cssClassLabel,
      unsigned    strokeColor,
      unsigned    strokeWidth,
      unsigned    fillColor,      
      unsigned    strokeOpacity,
      double      fillOpacity )
{
  svg_t* p = _handleToPtr(h);
  return _install_css(p, cssClassId, cssClassLabel, strokeColor, strokeWidth, fillColor, strokeOpacity, fillOpacity );
}

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
  unsigned idx = cm->colorN * (c - colorMin)/(colorMax - colorMin);

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

cw::rc_t cw::svg::_set_attr( handle_t h, argId_t id, const int& value )
{
  rc_t   rc = kOkRC;
  svg_t* p  = _handleToPtr(h);
  
  switch( id )
  {
    case kStrokeColorArgId:   p->curEle->attr.strokeColor = value; break;
    case kStrokeWidthArgId:   p->curEle->attr.strokeWidth = value; break;
    case kFillColorArgId:     p->curEle->attr.fillColor   = value; break;
    default:
      rc = cwLogError(kSetAttrFailRC,"Unknown SVG attribute id: %i", id);
  }

  if( rc == kOkRC )
    p->curEle->flags |= id;
  
  return rc;
}

cw::rc_t cw::svg::_set_attr( handle_t h, argId_t id, double value )
{
  rc_t   rc = kOkRC;
  svg_t* p  = _handleToPtr(h);
  
  switch( id )
  {
    case kStrokeOpacityArgId: p->curEle->attr.strokeOpacity = value; break;
    case kFillOpacityArgId: p->curEle->attr.fillOpacity = value; break;
    default:
      rc = cwLogError(kSetAttrFailRC,"Unknown SVG attribute id: %i", id);
  }
  
  if( rc == kOkRC )
    p->curEle->flags |= id;
  
  return rc;
}

cw::rc_t cw::svg::_rect( handle_t h, double x,  double y,  double ww, double hh, unsigned cssClassId )
{
  svg_t* p = _handleToPtr(h);
  return _insert( p, kRectTId, x, y, ww, hh, cssClassId, nullptr);  
}

cw::rc_t cw::svg::_line( handle_t h, double x0,  double y0,  double x1, double y1, unsigned cssClassId )
{
  svg_t* p = _handleToPtr(h);
  return _insert( p, kLineTId, x0, y0, x1-x0, y1-y0, cssClassId, nullptr);  
}

cw::rc_t cw::svg::_pline( handle_t h, const double* yV,  unsigned n,  const double* xV, unsigned cssClassId )
{
  svg_t* p = _handleToPtr(h);
  return _insert( p, kPLineTId, 0,0,0,0, cssClassId, nullptr, yV, n, xV);    
}

cw::rc_t cw::svg::_text( handle_t h, double x, double y, const char* text, unsigned cssClassId )
{
  svg_t* p = _handleToPtr(h);
  return _insert( p, kTextTId, x, y, 0, 0, cssClassId, text);  
}

cw::rc_t cw::svg::image( handle_t h, const float* xM, unsigned rowN, unsigned colN, unsigned pixSize, unsigned cmapId )
{
  for(unsigned i=0; i<rowN; ++i)
    for(unsigned j=0; j<colN; ++j)
    {
      int c = color( h, cmapId, 0, 1, xM[j*colN + i + 1] );
      rect(h, i*pixSize, j*pixSize, pixSize, pixSize, kRectCssId, kFillColorArgId, c, kStrokeColorArgId, c );
    }

  return kOkRC;
}
  
cw::rc_t cw::svg::write( handle_t h, const char* outFn, const char* cssFn, unsigned flags, double bordL, double bordT, double bordR, double bordB)
{
  rc_t   rc               = kOkRC;
  svg_t* p                = _handleToPtr(h);
  double svgWidth         = 0;
  double svgHeight        = 0;
  char*  cssStr           = nullptr;
  char*  fileHdr          = nullptr;
  char*  svgHdr           = nullptr;
  char*  bodyStr          = nullptr;
  ele_t* e                = p->eleL;
  bool   standAloneFl     = cwIsFlag(flags,kStandAloneFl); 
  bool   panZoomFl        = cwIsFlag(flags,kPanZoomFl);
  bool   genInlineStyleFl = cwIsFlag(flags,kGenInlineStyleFl);
  bool   genCssFileFl     = cwIsFlag(flags,kGenCssFileFl) && cssFn!=nullptr;
  //bool   drawFrameFl      = cwIsFlag(flags,kDrawFrameFl);

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
    "%s\n"
    "</head>\n"
    "<body onload=\"doOnLoad()\">\n";

  char svgFmt[] = "<svg id=\"mysvg\" width=\"%f\" height=\"%f\">\n";
  char cssFmt[] = "<link rel=\"stylesheet\" type=\"text/css\" href=\"%s\">\n";

  double max_y = _size(p, svgWidth, svgHeight );

  _flipY( p, max_y );

  _offsetEles(p, bordR, bordT );
  
  svgWidth  += bordR + bordL;
  svgHeight += bordT + bordB;

  if( genCssFileFl )
    _writeCssFile( p, cssFn );

  cssStr  = mem::printf(cssStr,cssFn==nullptr ? "%s" : cssFmt, cssFn==nullptr ? " " : cssFn);

  fileHdr = mem::printf(fileHdr, standAloneFmt, cssStr, panZoomFl ? panZoomHdr : "");
  
  svgHdr  = mem::printf(svgHdr,"%s%s", standAloneFl ? fileHdr : "", svgFmt);

  fileHdr = mem::printf(fileHdr,svgHdr,svgWidth,svgHeight);

  mem::release(svgHdr);
  mem::release(cssStr);

  for(; e!=NULL; e=e->link)
  {
    char* styleStr = nullptr;
    
    if((rc = _allocStyleString( p, e, genInlineStyleFl, styleStr )) != kOkRC )
      goto errLabel;
    
    switch( e->typeId )
    {
      case kRectTId:
        bodyStr = mem::printp(bodyStr,"<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\" %s/>\n",e->x0,e->y0,e->x1-e->x0,e->y1-e->y0,styleStr);
        break;
        
      case kLineTId:
        bodyStr = mem::printp(bodyStr,"<line x1=\"%f\" y1=\"%f\" x2=\"%f\" y2=\"%f\"  %s/>\n",e->x0,e->y0,e->x1,e->y1,styleStr);
        break;

      case kPLineTId:
        bodyStr = _write_pline(bodyStr,e,styleStr);
        break;
        
      case kTextTId:
        bodyStr = mem::printp(bodyStr,"<text x=\"%f\" y=\"%f\"  %s>%s</text>\n",e->x0,e->y0, styleStr,e->text);
        break;
    }
    
    mem::release(styleStr);
  }

  if( (bodyStr = textAppend(bodyStr,"</svg>\n")) == NULL )
  {
    rc = cwLogError(kMemAllocFailRC,"File suffix write failed.");
    goto errLabel;
  }
  
  if( standAloneFl )
    bodyStr = textAppend(bodyStr,"</body>\n</html>\n");
  
  if((rc = file::open(fH,outFn,file::kWriteFl)) != kOkRC )
  {
    rc = cwLogError(rc,"SVG file create failed for '%s'.",cwStringNullGuard(outFn));
    goto errLabel;
  }

  fileHdr = textAppend(fileHdr,bodyStr);
  
  if((rc = file::printf(fH,fileHdr)) != kOkRC )
  {    
    rc = cwLogError(rc,"SVG file write failed on '%s'.",outFn);
    goto errLabel;
  }

 errLabel:
    file::close(fH);

    mem::release(fileHdr);
    mem::release(bodyStr);
  
    return rc;  
}

cw::rc_t cw::svg::test( const char* outFn, const char* cssFn )
{

  rc_t rc = kOkRC;

  handle_t h;
  if((rc = create(h)) != kOkRC )
    cwLogError(rc,"SVG Test failed on create.");


  double yV[] = { 0, 10, 30, 60, 90 };
  unsigned yN = cwCountOf(yV);
  
  rect(h,  0,  0, 100, 100, kRectCssId, kFillColorArgId, 0x7f7f7f );
  line(h,  0,  0, 100, 100, kLineCssId, kStrokeColorArgId, 0xff0000 );
  line(h,  0,100, 100,   0, kLineCssId, kStrokeColorArgId, 0x00ff00, kStrokeWidthArgId, 3, kStrokeOpacityArgId, 0.5 );
  pline(h, yV, yN );
  text(h, 10, 10, "foo");

  write(h,outFn, cssFn, kStandAloneFl | kGenCssFileFl, 10,10,10,10);
  if((rc = destroy(h)) != kOkRC )
    cwLogError(rc,"SVG destroy failed.");
  
  return rc;
  
}
