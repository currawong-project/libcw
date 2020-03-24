#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwThread.h"
#include "cwWebSock.h"
#include "cwWebSockSvr.h"
#include "cwUi.h"
#include "cwText.h"
#include "cwNumericConvert.h"

namespace cw
{
  namespace ui
  {
    typedef struct ele_str
    {
      struct ele_str* parent;  // pointer to parent ele - or nullptr if this ele is attached to the root ui ele
      unsigned        uuId;    // UI unique id - automatically generated and unique among all elements that are part of this ui_t object.
      unsigned        appId;   // application assigned id - application assigned id
      char*           jsId;    // javascript id 
    } ele_t;
    
    typedef struct ui_str
    {
      websockSrv::handle_t wssH;
      unsigned             eleAllocN;
      unsigned             eleN;
      ele_t**              eleA;
      uiCallback_t         cbFunc;
      void*                cbArg;
    } ui_t;

    ui_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,ui_t>(h); }

    rc_t _destroy( ui_t* p )
    {
      rc_t rc = kOkRC;
      
      if( p->wssH.isValid() )
        if((rc = websockSrv::destroy(p->wssH)) != kOkRC )
          return rc;

      for(unsigned i=0; i<p->eleN; ++i)
      {
        mem::release(p->eleA[i]->jsId);
        mem::release(p->eleA[i]);
      }

      mem::release(p->eleA);
      mem::release(p);

      return rc;
    }

    ele_t* _createEle( ui_t* p, ele_t* parent, unsigned appId, const char* jsId )
    {
      ele_t* e = mem::allocZ<ele_t>();
      e->parent = parent;
      e->uuId    = p->eleN;
      e->appId = appId;
      e->jsId  = mem::duplStr(jsId);

      if( p->eleN == p->eleAllocN )
      {
        p->eleAllocN += 100;
        p->eleA       = mem::resizeZ<ele_t*>(p->eleA,p->eleAllocN);
      }

      p->eleA[ p->eleN ] = e;
      p->eleN += 1;
      
      return e;
    }

    ele_t* _uuIdToEle( ui_t* p, unsigned uuId, bool errorFl=true )
    {
      if( uuId >= p->eleN ) 
      {
        cwLogError(kInvalidIdRC,"The element uuid:%i is not valid.",uuId);
        return nullptr;
      }
    
      return p->eleA[ uuId ];
    }


    rc_t _websockSend( ui_t* p, const char* msg )
    {
      return websock::send( websockSrv::websockHandle( p->wssH ), kUiProtocolId, msg, strlen(msg) );
    }

    const char* _findEleJsId( ui_t* p, unsigned uuId )
    {
      for(unsigned i=0; i<p->eleN; ++i)
        if( p->eleA[i]->uuId == uuId )
          return p->eleA[i]->jsId;
      
      return nullptr;
    }

    // Print the attribute data value.
    template< typename T0 >
      unsigned format_attribute_data( char* buf, unsigned n, T0 t0 )
    {
      return toText(buf,n,t0);
    }
    
    // Override format_attribute_data() for char. string data so that strings are wrapped in quotes.
    template<>
    unsigned format_attribute_data( char* buf, unsigned n, const char* t )
    {
      unsigned i = 0;
      i += toText(buf+i, n-i, "\"" );
      i += toText(buf+i, n-i, t );
      i += toText(buf+i, n-i, "\"" );
      return i;
    }

    // terminating condition for format_attributes()
    unsigned format_attributes(char* buf, unsigned  n, unsigned i)
    { return i; }
    
    template<typename T0, typename T1, typename... ARGS>
      unsigned format_attributes(char* buf, unsigned  n, unsigned i, T0 t0, T1 t1, ARGS&&... args)
    {
      i += toText(buf+i, n-i, ",\"" );
      i += toText(buf+i, n-i, t0  );
      i += toText(buf+i, n-i, "\":" );
      i += format_attribute_data(buf+i, n-i,  t1 );
      
      return format_attributes(buf,n,i,std::forward<ARGS>(args)...);      
    }
    
    template< typename... ARGS>
      rc_t _createOneEle( ui_t* p, unsigned& uuIdRef, const char* eleTypeStr, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title, ARGS&&... args )
    {
      // { op:create, parent:my_parent_id, value:{ button:{ jsId:my_jsId, appId:appId, uuId:uuId, class:clas, title:'my title' } }
      rc_t           rc         = kOkRC;
      const char*    parentJsId = "";
      ele_t*         newEle     = nullptr;
      ele_t*         parentEle  = nullptr;
      const unsigned bufN       = 1024;
      char           buf[ bufN ];
      
      uuIdRef = kInvalidId;
      
      // 
      if( parentUuId != kInvalidId )
      {
        if(( parentEle =  _uuIdToEle(p, parentUuId )) == nullptr )
          return cwLogError( kInvalidArgRC, "Unable to locate the parent element (id:%i).", parentUuId );
        
        parentJsId = parentEle->jsId;
      }
      
      newEle = _createEle( p, parentEle, appId, jsId );

      unsigned i = snprintf( buf, bufN, "{ \"op\":\"create\", \"parent\":\"%s\", \"children\":{ \"%s\":{ \"jsId\":\"%s\", \"appId\":%i, \"uuId\":%i, \"class\":\"%s\", \"title\":\"%s\" ", parentJsId, eleTypeStr, jsId, appId, newEle->uuId, clas, title );

      i = format_attributes(buf, bufN, i, std::forward<ARGS>(args)...);

      toText(buf+i, bufN-i, "}}}");

      printf("%s\n",buf);
      
      rc =  _websockSend( p, buf );

      uuIdRef = newEle->uuId;

      
      return rc;
    }

    
    ele_t* _parse_value_msg( ui_t* p, value_t& valueRef, const char* msg )
    {
      rc_t     rc      = kOkRC;
      char     argType    = 0;
      ele_t*   ele     = nullptr;
      unsigned eleUuId = kInvalidId;
      
      valueRef.tid   = kInvalidTId;
      
      if( msg == nullptr )
      {
        cwLogWarning("Empty message received from UI.");
        return nullptr;
      }

      // locate the colon prior to the value
      const char* s = strchr(msg,':');
            
      if( s == nullptr || sscanf(msg, "value %i %c ",&eleUuId,&argType) != 2 )
      {
        cwLogError(kSyntaxErrorRC,"Invalid message from UI: %s.", msg );
        goto errLabel;
      }
      
      // advance s past the colon
      s += 1;

      // parse the argument
      switch( argType )
      {
        case 'b':
          if((rc = string_to_number<bool>(s,valueRef.u.b)) == kOkRC )
            valueRef.tid = kBoolTId;
          break;
          
        case 'i':
          if((rc = string_to_number<int>(s,valueRef.u.i)) == kOkRC )
            valueRef.tid = kIntTId;
          break;
          
        case 'u':
          if((rc = string_to_number<unsigned>(s,valueRef.u.u)) == kOkRC )
            valueRef.tid = kUIntTId;
          break;
          
        case 'f':
          if((rc = string_to_number<float>(s,valueRef.u.f)) == kOkRC )
            valueRef.tid = kFloatTId;
          break;
          
        case 'd':
          if((rc = string_to_number<double>(s,valueRef.u.d)) == kOkRC )
            valueRef.tid = kDoubleTId;
          break;

        case 's':
          if((valueRef.u.s = nextNonWhiteChar(s)) == nullptr )
            valueRef.tid = kStringTId;
          break;
          
        default:
          rc = cwLogError(kInvalidIdRC,"Unknown value type '%c' in message from UI.", argType );
          goto errLabel;
      }

      // locate the element record
      if((ele = _uuIdToEle( p, eleUuId )) == nullptr )
      {
        cwLogError(kInvalidIdRC,"UI message elment not found.");
        goto errLabel;
      }
      
    errLabel:

      return ele;
    }
    
    void _websockCb( void* cbArg, unsigned protocolId, unsigned connectionId, websock::msgTypeId_t msg_type, const void* msg, unsigned byteN )
    {
      ui_t*    p              = (ui_t*)cbArg;
      opId_t   opId           = kInvalidOpId;
      unsigned eleUuId        = kInvalidId;
      unsigned eleAppId       = kInvalidId;
      unsigned parentEleAppId = kInvalidId;
      value_t  value;
      
      switch( msg_type )
      {
        case websock::kConnectTId:
          opId = kConnectOpId;
          break;
          
        case websock::kDisconnectTId:
          opId = kDisconnectOpId;
          break;
          
        case websock::kMessageTId:
          {
            ele_t* ele;

            if( textCompare((const char*)msg,"init",strlen("init")) == 0 )
            {
              opId = kInitOpId;
            }
            else
            {
              opId = kValueOpId;
              if((ele = _parse_value_msg(p, value, (const char*)msg )) == nullptr )
                cwLogError(kOpFailRC,"UI Value message parse failed.");
              else
              {
                eleUuId        = ele->uuId;
                eleAppId       = ele->appId;
                parentEleAppId = ele->parent == nullptr ? kInvalidId : ele->parent->appId;
              }
            }
          }
          break;

        default:
          cwLogError(kInvalidOpRC,"Unknown websock message type:%i.", msg_type );
          return;
      }

      if( p->cbFunc != nullptr )
        p->cbFunc( p->cbArg, connectionId, opId, parentEleAppId, eleUuId, eleAppId, &value );
    }
  }
}

cw::rc_t cw::ui::createUi(
  handle_t&    h,
  unsigned     port,
  uiCallback_t cbFunc,
  void*        cbArg,
  const char*  physRootDir,
  const char*  dfltPageFn,
  unsigned     websockTimeOutMs,  
  unsigned     rcvBufByteN,
  unsigned     xmtBufByteN )
{
  rc_t rc = kOkRC;
    
  websock::protocol_t protocolA[] =
    {
     { "http",        kHttpProtocolId,          0,           0 },
     { "ui_protocol", kUiProtocolId,  rcvBufByteN, xmtBufByteN }
    };

  unsigned protocolN = sizeof(protocolA)/sizeof(protocolA[0]);
  
  if((rc = destroyUi(h)) != kOkRC )
    return rc;

  ui_t* p      = mem::allocZ<ui_t>();

  if((rc = websockSrv::create(p->wssH, _websockCb, p, physRootDir, dfltPageFn, port, protocolA, protocolN, websockTimeOutMs )) != kOkRC )
  {
    cwLogError(rc,"Internal websock server creation failed.");
    goto errLabel;
  }

  p->eleAllocN = 100;
  p->eleA      = mem::allocZ<ele_t*>( p->eleAllocN );
  p->eleN      = 0;
  p->cbFunc    = cbFunc;
  p->cbArg     = cbArg;

  h.set(p);

 errLabel:

  if( rc != kOkRC )
  {
    _destroy(p);
  }

  return rc;
}

cw::rc_t cw::ui::start( handle_t h )
{
  rc_t  rc = kOkRC;
  ui_t* p  = _handleToPtr(h);

  if((rc = websockSrv::start(p->wssH)) != kOkRC )
    rc = cwLogError(rc,"Internal websock server start failed.");

  return rc;  
}

cw::rc_t cw::ui::stop( handle_t h )
{
  rc_t  rc = kOkRC;
  ui_t* p  = _handleToPtr(h);

  if((rc = websockSrv::pause(p->wssH)) != kOkRC )
    rc = cwLogError(rc,"Internal websock server stop failed.");

  return rc;
}

cw::rc_t cw::ui::destroyUi( handle_t& h )
{
  rc_t rc = kOkRC;
  if( !h.isValid() )
    return rc;

  ui_t* p = _handleToPtr(h);
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;
}

unsigned cw::ui::findElementAppId(  handle_t h, unsigned parentUuId, const char* jsId )
{
  ui_t* p = _handleToPtr(h);
  
  for(unsigned i=0; i<p->eleN; ++i)
    if( p->eleA[i]->parent->uuId==parentUuId && strcmp(p->eleA[i]->jsId,jsId) == 0 )
      return p->eleA[i]->appId;
  return kInvalidId;
}

unsigned cw::ui::findElementUuId( handle_t h, unsigned parentUuId, const char* jsId )
{
  ui_t* p = _handleToPtr(h);
  
  for(unsigned i=0; i<p->eleN; ++i)
    if( p->eleA[i]->parent->uuId==parentUuId && strcmp(p->eleA[i]->jsId,jsId) == 0 )
      return p->eleA[i]->uuId;
  return kInvalidId;
}

unsigned cw::ui::findElementUuId( handle_t h, unsigned parentUuId, unsigned appId )
{
  ui_t* p = _handleToPtr(h);
  
  for(unsigned i=0; i<p->eleN; ++i)
    if( p->eleA[i]->parent->uuId==parentUuId && p->eleA[i]->appId == appId )
      return p->eleA[i]->uuId;
  return kInvalidId;
}

const char* cw::ui::findElementJsId( handle_t h, unsigned uuId )
{
  ui_t* p = _handleToPtr(h);
  return _findEleJsId(p,uuId);
}

    
cw::rc_t cw::ui::createDiv( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "div", parentUuId, jsId, appId, clas, title ); }

cw::rc_t cw::ui::createTitle( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "option", parentUuId, jsId, appId, clas, title ); }

cw::rc_t cw::ui::createButton( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "button", parentUuId, jsId, appId, clas, title ); }

cw::rc_t cw::ui::createCheck( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title, bool value  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "check", parentUuId, jsId, appId, clas, title, "value", value ); }

cw::rc_t cw::ui::createSelect( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "select", parentUuId, jsId, appId, clas, title ); }

cw::rc_t cw::ui::createOption( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "option", parentUuId, jsId, appId, clas, title ); }

cw::rc_t cw::ui::createString( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title, const char* value )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "string", parentUuId, jsId, appId, clas, title, "value", value ); }

cw::rc_t cw::ui::createNumber( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title, double value, double minValue, double maxValue, double stepValue, unsigned decpl )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "number", parentUuId, jsId, appId, clas, title, "value", value, "min", minValue, "max", maxValue, "step", stepValue, "decpl", decpl ); }

cw::rc_t cw::ui::createProgress(  handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title, double value, double minValue, double maxValue )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "progress", parentUuId, jsId, appId, clas, title, "value", value, "min", minValue, "max", maxValue ); }

cw::rc_t cw::ui::createText(   handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title )
{
  rc_t rc= kOkRC;
  return rc;
}



 
