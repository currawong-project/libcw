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
#include "cwObject.h"

namespace cw
{
  namespace ui
  {
    typedef struct appIdMapRecd_str
    {
      struct appIdMapRecd_str* link;
      unsigned                 parentAppId;
      unsigned                 appId;
      char*                    jsId;
    } appIdMapRecd_t;
      
    
    typedef struct ele_str
    {
      struct ele_str* parent;  // pointer to parent ele - or nullptr if this ele is attached to the root ui ele
      unsigned        uuId;    // UI unique id - automatically generated and unique among all elements that are part of this ui_t object.
      unsigned        appId;   // application assigned id - application assigned id
      char*           jsId;    // javascript id
    } ele_t;
    
    typedef struct ui_str
    {
      websockSrv::handle_t wssH;      // websock server handle
      unsigned             eleAllocN; // size of eleA[]
      unsigned             eleN;      // count of ele's in use
      ele_t**              eleA;      // eleA[ eleAllocN ] 
      uiCallback_t         cbFunc;    // app. cb func
      void*                cbArg;     // app. cb func arg.
      appIdMapRecd_t*      appIdMap;  // map of application parent/child/js id's
      char*                buf;       // buf[bufN] output message formatting buffer
      unsigned             bufN;      //
    } ui_t;

    ui_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,ui_t>(h); }

    void _print_eles( ui_t* p )
    {
      for(unsigned i=0; i<p->eleN; ++i)
      {
        ele_t* e = p->eleA[i];
        printf("%15s u:%i : u:%i a:%i %s\n",e->parent==nullptr?"<null>" : e->parent->jsId,e->parent==nullptr? -1 :e->parent->uuId,e->uuId,e->appId,e->jsId);
      }
    }
    
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

      appIdMapRecd_t* m = p->appIdMap;
      while( m!=nullptr )
      {
        appIdMapRecd_t* m0 = m->link;
        mem::release(m->jsId);
        mem::release(m);
        m = m0;
      }

      mem::release(p->eleA);
      mem::release(p);

      return rc;
    }

    appIdMapRecd_t* _findAppIdMap( ui_t* p, unsigned parentAppId, const char* jsId )
    {
      appIdMapRecd_t* m = p->appIdMap;
      for(; m != nullptr; m=m->link)
        if( m->parentAppId==parentAppId && textCompare(jsId,m->jsId)==0 )
          return m;
      return nullptr;
    }
    
    appIdMapRecd_t* _findAppIdMap( ui_t* p, unsigned parentAppId, unsigned appId )
    {
      appIdMapRecd_t* m = p->appIdMap;
      for(; m != nullptr; m=m->link)
        if( m->parentAppId==parentAppId && m->appId==appId )
          return m;
      return nullptr;
    }
    
    rc_t _allocAppIdMap( ui_t* p, unsigned parentAppId, unsigned appId, const char* jsId )
    {
      rc_t rc = kOkRC;

      // The 'jsId' must be valid (or there is no reason to create the map.
      // (since it will ultimately be used to locate the appId give the parentAppId and jsId)
      if( jsId == nullptr || strlen(jsId) == 0 )
        return cwLogError(kInvalidIdRC,"Registered parent/child app id's must have a valid 'jsId'.");

      // verify that the parent/child pair is unique
      if( _findAppIdMap(p,parentAppId,appId) != nullptr )
        return cwLogError(kDuplicateRC,"An attempt was made to register a duplicate parent/child appid pair. parentId:%i appId:%i jsId:'%s'.",parentAppId,appId,cwStringNullGuard(jsId));

      // verify that the parent/js pair is unique
      if( _findAppIdMap(p,parentAppId,jsId) != nullptr )
        return cwLogError(kDuplicateRC,"An attempt was made to register a duplicate parent app id/js id pair. parentId:%i appId:%i jsId:'%s'.",parentAppId,appId,cwStringNullGuard(jsId));
              
      // allocate and link in a new appId map record
      appIdMapRecd_t* m = mem::allocZ<appIdMapRecd_t>();
      m->parentAppId = parentAppId;
      m->appId       = appId;
      m->jsId        = mem::duplStr(jsId);
      m->link        = p->appIdMap;
      p->appIdMap    = m;

      return rc;
    }

    ele_t* _createEle( ui_t* p, ele_t* parent, unsigned appId, const char* jsId )
    {
      ele_t* e = mem::allocZ<ele_t>();
      e->parent  = parent;
      e->uuId    = p->eleN;
      e->appId   = appId;
      e->jsId    = mem::duplStr(jsId);

      if( p->eleN == p->eleAllocN )
      {
        p->eleAllocN += 100;
        p->eleA       = mem::resizeZ<ele_t*>(p->eleA,p->eleAllocN);
      }

      p->eleA[ p->eleN ] = e;
      p->eleN += 1;
      
      return e;
    }

    // Given a uuId return a pointer to the associated element.
    ele_t* _uuIdToEle( ui_t* p, unsigned uuId, bool errorFl=true )
    {
      if( uuId >= p->eleN ) 
      {
        cwLogError(kInvalidIdRC,"The element uuid:%i is not valid.",uuId);
        return nullptr;
      }
    
      return p->eleA[ uuId ];
    }

    // Given a parent UuId and a javascript id find the associated ele
    ele_t* _parentUuId_JsId_ToEle( ui_t* p, unsigned parentUuId, const char* jsId, bool errorFl=true )
    {
      for(unsigned i=0; i<p->eleN; ++i)
        if( ((p->eleA[i]->parent==nullptr && parentUuId == kRootUuId) || (p->eleA[i]->parent != nullptr && parentUuId == p->eleA[i]->parent->uuId)) &&  (strcmp(p->eleA[i]->jsId,jsId) == 0))
          return p->eleA[i];
          
      if( errorFl )
        cwLogError(kInvalidIdRC,"The element with parent uuid:%i and jsId:%s is not found.",parentUuId,jsId);
      
      return nullptr;
    }
    
    unsigned _findElementUuId( ui_t* p, const char* jsId )
    {
      for(unsigned i=0; i<p->eleN; ++i)
        if( strcmp(p->eleA[i]->jsId,jsId) == 0 )
          return p->eleA[i]->uuId;
  
      return kInvalidId;
    }

    const char* _findEleJsId( ui_t* p, unsigned uuId )
    {
      for(unsigned i=0; i<p->eleN; ++i)
        if( p->eleA[i]->uuId == uuId )
          return p->eleA[i]->jsId;
      
      return nullptr;
    }

    rc_t _websockSend( ui_t* p, unsigned wsSessId, const char* msg )
    {
      return websock::send( websockSrv::websockHandle( p->wssH ), kUiProtocolId, wsSessId, msg, strlen(msg) );
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
      rc_t _createOneEle( ui_t* p, unsigned& uuIdRef, const char* eleTypeStr, unsigned wsSessId, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title, ARGS&&... args )
    {
      // { op:create, parent:my_parent_id, value:{ button:{ jsId:my_jsId, appId:appId, uuId:uuId, class:clas, title:'my title' } }
      rc_t           rc         = kOkRC;
      const char*    parentJsId = "";
      ele_t*         newEle     = nullptr;
      ele_t*         parentEle  = nullptr;
      //const unsigned bufN       = 1024;   // TODO: use preallocated buffer 
      //char           buf[ bufN ];
      
      uuIdRef = kInvalidId;

      if( parentUuId == kInvalidId )
        parentUuId = kRootUuId;
          
      // get the parent element
      if(( parentEle =  _uuIdToEle(p, parentUuId )) == nullptr )
        return cwLogError( kInvalidArgRC, "Unable to locate the parent element (id:%i).", parentUuId );

      // get the parent jsId
      parentJsId = parentEle->jsId;
      
      // create the local representation of the new element
      newEle = _createEle( p, parentEle, appId, jsId );

      // form the create json message string
      unsigned i = snprintf( p->buf, p->bufN, "{ \"op\":\"create\", \"parent\":\"%s\", \"children\":{ \"%s\":{ \"jsId\":\"%s\", \"appId\":%i, \"uuId\":%i, \"class\":\"%s\", \"title\":\"%s\" ", parentJsId, eleTypeStr, jsId, appId, newEle->uuId, clas, title );

      // add the UI specific attributes
      i += format_attributes(p->buf+i, p->bufN-i, 0, std::forward<ARGS>(args)...);

      // terminate the message
      i += toText(p->buf+i, p->bufN-i, "}}}");

      if( i >= p->bufN )
        return cwLogError(kBufTooSmallRC,"The UI message formatting buffer is too small. (size:%i bytes)", p->bufN);

      printf("%s\n",p->buf);

      // send the message 
      rc =  _websockSend( p, wsSessId, p->buf );

      uuIdRef = newEle->uuId;
      
      return rc;
    }

    rc_t _decorateObj( ui_t* p, object_t* o )
    {
      rc_t            rc = kOkRC;
      const object_t* oo;
      //ele_t*          parent_ele;
      const char*     jsId;

      // find the parent pair
      if((oo = o->find( "parent", kNoRecurseFl | kOptionalFl)) != nullptr )
      {
      }

      // find the parent JsId
      if((rc = oo->value(jsId)) != kOkRC )
      {
      }

      // find the parent element
      //if((parent_ele = _jsIdToEle( p, jsId )) == nullptr )
      //{
      //}

      
      return rc;
 
    }


    rc_t  _createFromObj( ui_t* p, object_t* o, unsigned wsSessId, unsigned parentUuId )     
    {
      rc_t        rc         = kOkRC;
      const char* parentJsId = "";
      const int   kBufN      = 512; // TODO: preallocate this buffer as part of ui_t.
      char        buf0[ kBufN ];
      char        buf1[ kBufN ];

      // if a parentUuid was given ...
      if( parentUuId != kInvalidId )
      {
        // ... then find the associated JS id
        if((parentJsId = _findEleJsId( p, parentUuId )) == nullptr )
          return cwLogError(kInvalidIdRC, "The JS id associated with the uuid '%i' could not be found for an resource object.", parentUuId );
      }
      else // if no parentUuid was given then look for one in the resource
      {
        // get the parent JS id from the cfg object
        rc = o->get("parent",parentJsId,kNoRecurseFl | kOptionalFl);
        
        switch(rc)
        {
          case kOkRC:
            // get a pointer to the jsId from the local list (the copy in the object is about to be deleted)
            parentJsId = _findEleJsId( p, _findElementUuId(p,parentJsId));
            
            //remove the parent link
            o->find("parent")->parent->free();
            break;
            
          case kLabelNotFoundRC:
            parentJsId = _findEleJsId( p, kRootUuId );
            break;
            
          default:
            rc = cwLogError(rc,"The resource object parent id '%s' could not be found.", parentJsId );
            goto errLabel;
        }
      }

      // form the msg string from the resource
      if( o->to_string( buf0, kBufN ) >= kBufN )
        return cwLogError(kBufTooSmallRC,"The resource object string buffer is too small (buf bytes:%i).",kBufN);

      printf("buf0: %s\n",buf0);
      if( snprintf( buf1, kBufN, "{ \"op\":\"create\", \"parent\":\"%s\", \"children\":%s }", parentJsId, buf0 ) >= kBufN )
        return cwLogError(kBufTooSmallRC,"The resource object string buffer is too small (buf bytes:%i).",kBufN);
        
        
      // send the msg string
      printf("buf1: %s\n",buf1);

      rc =  _websockSend( p, wsSessId, buf1 );

      
    errLabel:
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
        cwLogError(kSyntaxErrorRC,"Invalid message from UI: '%s'.", msg );
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

    rc_t _send_app_id_msg( ui_t* p, unsigned wsSessId, ele_t* ele )
    {
      rc_t rc = kOkRC;
      
      unsigned i = snprintf(p->buf,p->bufN,"{ \"op\":\"set_app_id\", \"parentUuId\":%i, \"jsId\":\"%s\", \"appId\":%i, \"uuId\":%i }", ele->parent->uuId, ele->jsId, ele->parent->appId, ele->appId );

      if( i >= p->bufN )
        return cwLogError(kBufTooSmallRC,"The 'app_id' msg formatting buffer is too small (%i bytes).", p->bufN);

      if((rc =  _websockSend( p, wsSessId, p->buf )) != kOkRC )
        return cwLogError(rc,"'app_id' msg transmission failed.");
     
      return rc;
    }

    ele_t* _handle_register_msg( ui_t* p, unsigned wsSessId, const char* msg )
    {
      printf("%s\n",msg);
      return nullptr;
    }
    
    ele_t* _handle_register_msg0( ui_t* p, unsigned wsSessId, const char* msg )
    {
      rc_t        rc           = kOkRC;
      unsigned    parentUuId   = kInvalidId;
      ele_t*      parentEle    = nullptr;
      ele_t*      ele          = nullptr;

      const char* s0  = nextNonWhiteChar(msg + strlen("register"));
      
      const char* jsId = nextNonWhiteChar(nextWhiteChar(s0));

      // verifity the message tokens
      if( s0 == nullptr || jsId == nullptr )
      {
        cwLogError(kSyntaxErrorRC, "'register' msg format error: '%s' is not a valid message.", cwStringNullGuard(msg) );
        goto errLabel;
      }

      // verify the parentUuId parsing
      if((rc = string_to_number<unsigned>(s0,parentUuId)) != kOkRC )
      {
        cwLogError(kSyntaxErrorRC, "'register' msg parentUuId format error: '%s' does not contain a valid parentUuId.", cwStringNullGuard(msg) );
        goto errLabel;
      }
      
      // get the parent ele
      if((parentEle = _uuIdToEle( p, parentUuId)) == nullptr )
      {
        cwLogError(kInvalidIdRC,"UI register msg parent element not found.");
        goto errLabel;
      }

      // if the child element does not already exist
      if(( ele = _parentUuId_JsId_ToEle( p, parentUuId, jsId, false )) == nullptr )
      {
        // look up the parent/jsId pair map
        appIdMapRecd_t* m = _findAppIdMap( p, parentEle->appId, jsId );

        // create the ele 
        ele = _createEle( p, parentEle, m==nullptr ? kInvalidId : m->appId, jsId );

        printf("creating: parent uuid:%i js:%s \n", parentUuId,jsId);

        // notify the app of the new ele's uuid and appId
        if( m != nullptr )
          _send_app_id_msg( p, wsSessId, ele );
        
      }
      else
      {
        printf("parent uuid:%i js:%s already exists.\n", parentUuId,jsId);
      }

      if( ele != nullptr )
        _send_app_id_msg( p, wsSessId, ele );
      
      return ele;
      
    errLabel:
      return nullptr;
    }
      

    opId_t _labelToOpId( const char* label )
    {
      typedef struct
      {
        opId_t      id;
        const char* label;
      } map_t;

      map_t mapA[] = 
      {
       { kConnectOpId,        "connect" },
       { kInitOpId,           "init" },
       { kValueOpId,          "value" },
       { kRegisterOpId,       "register" },       
       { kDisconnectOpId,     "disconnect" },
       { kEndAppIdUpdateOpId, "end_app_id_update" },
       { kInvalidOpId,        "<invalid>" },       
      };

      for(unsigned i=0; mapA[i].id != kInvalidOpId; ++i)
        if( textCompare(label,mapA[i].label,strlen(mapA[i].label)) == 0 )
          return mapA[i].id;

      return kInvalidOpId;

    
    }
    
    void _websockCb( void* cbArg, unsigned protocolId, unsigned wsSessId, websock::msgTypeId_t msg_type, const void* msg, unsigned byteN )
    {
      ui_t*    p              = (ui_t*)cbArg;
      opId_t   opId           = kInvalidOpId;
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

            opId = _labelToOpId((const char*)msg);

            switch( opId )
            {
              case kInitOpId:
                // Pass on the 'init' msg to the app.
                p->cbFunc( p->cbArg, wsSessId, opId, kInvalidId, kInvalidId, kInvalidId, nullptr );

                // The UI is initialized - begin the id update process
                if(  _websockSend( p, wsSessId, "{ \"op\":\"begin_app_id_update\" }" ) != kOkRC )
                  cwLogError(kOpFailRC,"'begin_app_id_update' transmit failed.");
                
                break;
                
              case kValueOpId:
                if((ele = _parse_value_msg(p, value, (const char*)msg )) == nullptr )
                  cwLogError(kOpFailRC,"UI Value message parse failed.");
                else
                {
                  unsigned parentEleAppId = ele->parent == nullptr ? kInvalidId : ele->parent->appId;

                  p->cbFunc( p->cbArg, wsSessId, opId, parentEleAppId, ele->uuId, ele->appId, &value );
                  
                }
                break;

                
              case kRegisterOpId:
                _handle_register_msg(p, wsSessId, (const char*)msg );
                break;

              case kEndAppIdUpdateOpId:
                _print_eles( p );
                cwLogInfo("App Id Update Complete.");
                break;
                

              case kInvalidOpId:
                cwLogError(kInvalidIdRC,"The UI received a NULL op. id.");
                break;

              default:
                cwLogError(kInvalidIdRC,"The UI received an unknown op. id.");
                break;
                
            } // switch opId
            
          } // kMessageTId
          break;

        default:
          cwLogError(kInvalidOpRC,"Unknown websock message type:%i.", msg_type );
          return;
      }


        
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
  unsigned     xmtBufByteN,
  unsigned     fmtBufByteN)
{
  rc_t rc = kOkRC;
  ele_t* ele;
  
  websock::protocol_t protocolA[] =
    {
     { "http",        kHttpProtocolId,          0,           0 },
     { "ui_protocol", kUiProtocolId,  rcvBufByteN, xmtBufByteN }
    };

  unsigned protocolN = sizeof(protocolA)/sizeof(protocolA[0]);
  
  if((rc = destroyUi(h)) != kOkRC )
    return rc;

  if( cbFunc == nullptr )
    return cwLogError(kInvalidArgRC,"The UI callback function must be a valid pointer.");

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
  p->buf       = mem::allocZ<char>(fmtBufByteN);
  p->bufN      = fmtBufByteN;

  // create the root element
  if((ele = _createEle(p, nullptr, kRootEleAppId, "uiDivId" )) == nullptr || ele->uuId != kRootUuId )
  {
    cwLogError(kOpFailRC,"The UI root element creation failed.");
    goto errLabel;
  }

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
  ui_t*  p = _handleToPtr(h);
  ele_t* ele;

  if((ele = _parentUuId_JsId_ToEle(p, parentUuId, jsId )) != nullptr )
    return ele->uuId;
  
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

unsigned cw::ui::findElementUuId( handle_t h, const char* jsId )
{
  ui_t* p = _handleToPtr(h);
  
  return _findElementUuId(p,jsId);
}


cw::rc_t cw::ui::createFromFile( handle_t  h, const char* fn,  unsigned wsSessId,  unsigned parentUuId)
{
  ui_t*     p  = _handleToPtr(h);
  rc_t      rc = kOkRC;
  object_t* o  = nullptr;
  
  if((rc = objectFromFile( fn, o )) != kOkRC )
    goto errLabel;

  //o->print();
  
  if((rc = _createFromObj( p, o, wsSessId, parentUuId )) != kOkRC )
    goto errLabel;

 errLabel:
  if(rc != kOkRC )
    rc = cwLogError(rc,"UI instantiation from the configuration file '%s' failed.", cwStringNullGuard(fn));

  if( o != nullptr )
    o->free();
  
  return rc;
}

cw::rc_t cw::ui::createFromText( handle_t h, const char* text, unsigned wsSessId,  unsigned parentUuId)
{
  ui_t*     p  = _handleToPtr(h);
  rc_t      rc = kOkRC;
  object_t* o  = nullptr;
  
  if((rc = objectFromString( text, o )) != kOkRC )
    goto errLabel;
    
  if((rc = _createFromObj( p, o, wsSessId, parentUuId )) != kOkRC )
    goto errLabel;

 errLabel:
  if(rc != kOkRC )
    rc = cwLogError(rc,"UI instantiation failed from the configuration from string: '%s'.", cwStringNullGuard(text));

  if( o != nullptr )
    o->free();
  
  return rc;
}
   
cw::rc_t cw::ui::createDiv( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "div", wsSessId, parentUuId, jsId, appId, clas, title ); }

cw::rc_t cw::ui::createTitle( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "option", wsSessId, parentUuId, jsId, appId, clas, title ); }

cw::rc_t cw::ui::createButton( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "button", wsSessId, parentUuId, jsId, appId, clas, title ); }

cw::rc_t cw::ui::createCheck( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title, bool value  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "check", wsSessId, parentUuId, jsId, appId, clas, title, "value", value ); }

cw::rc_t cw::ui::createSelect( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "select", wsSessId, parentUuId, jsId, appId, clas, title ); }

cw::rc_t cw::ui::createOption( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "option", wsSessId, parentUuId, jsId, appId, clas, title ); }

cw::rc_t cw::ui::createString( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title, const char* value )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "string", wsSessId, parentUuId, jsId, appId, clas, title, "value", value ); }

cw::rc_t cw::ui::createNumber( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title, double value, double minValue, double maxValue, double stepValue, unsigned decpl )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "number", wsSessId, parentUuId, jsId, appId, clas, title, "value", value, "min", minValue, "max", maxValue, "step", stepValue, "decpl", decpl ); }

cw::rc_t cw::ui::createProgress(  handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title, double value, double minValue, double maxValue )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "progress", wsSessId, parentUuId, jsId, appId, clas, title, "value", value, "min", minValue, "max", maxValue ); }

cw::rc_t cw::ui::createText(   handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* jsId, unsigned appId, const char* clas, const char* title )
{
  rc_t rc= kOkRC;
  return rc;
}



cw::rc_t cw::ui::registerAppIds(  handle_t h, const appIdMap_t* map, unsigned mapN )
{
  ui_t* p  = _handleToPtr(h);
  rc_t  rc = kOkRC;

  for(unsigned i=0; i<mapN; ++i)
    if((rc = _allocAppIdMap( p, map[i].parentAppId, map[i].appId, map[i].jsId )) != kOkRC )
      return rc;
  
  return rc;
}




 
