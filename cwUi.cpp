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
      char*                    eleName;
    } appIdMapRecd_t;
      
    
    typedef struct ele_str
    {
      struct ele_str* parent;  // pointer to parent ele - or nullptr if this ele is attached to the root ui ele
      unsigned        uuId;    // UI unique id - automatically generated and unique among all elements that are part of this ui_t object.
      unsigned        appId;   // application assigned id - application assigned id
      char*           eleName;    // javascript id
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
        printf("%15s u:%i : u:%i a:%i %s\n",e->parent==nullptr?"<null>" : e->parent->eleName,e->parent==nullptr? -1 :e->parent->uuId,e->uuId,e->appId,e->eleName);
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
        mem::release(p->eleA[i]->eleName);
        mem::release(p->eleA[i]);
      }

      appIdMapRecd_t* m = p->appIdMap;
      while( m!=nullptr )
      {
        appIdMapRecd_t* m0 = m->link;
        mem::release(m->eleName);
        mem::release(m);
        m = m0;
      }

      mem::release(p->eleA);
      mem::release(p->buf);      
      mem::release(p);

      return rc;
    }

    appIdMapRecd_t* _findAppIdMap( ui_t* p, unsigned parentAppId, const char* eleName )
    {
      appIdMapRecd_t* m = p->appIdMap;
      for(; m != nullptr; m=m->link)
        if( m->parentAppId==parentAppId && textCompare(eleName,m->eleName)==0 )
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
    
    rc_t _allocAppIdMap( ui_t* p, unsigned parentAppId, unsigned appId, const char* eleName )
    {
      rc_t rc = kOkRC;

      // The 'eleName' must be valid (or there is no reason to create the map.
      // (since it will ultimately be used to locate the appId give the parentAppId and eleName)
      if( eleName == nullptr || strlen(eleName) == 0 )
        return cwLogError(kInvalidIdRC,"Registered parent/child app id's must have a valid 'eleName'.");

      // verify that the parent/child pair is unique
      if( _findAppIdMap(p,parentAppId,appId) != nullptr )
        return cwLogError(kDuplicateRC,"An attempt was made to register a duplicate parent/child appid pair. parentId:%i appId:%i eleName:'%s'.",parentAppId,appId,cwStringNullGuard(eleName));

      // verify that the parent/js pair is unique
      if( _findAppIdMap(p,parentAppId,eleName) != nullptr )
        return cwLogError(kDuplicateRC,"An attempt was made to register a duplicate parent app id/js id pair. parentId:%i appId:%i eleName:'%s'.",parentAppId,appId,cwStringNullGuard(eleName));
              
      // allocate and link in a new appId map record
      appIdMapRecd_t* m = mem::allocZ<appIdMapRecd_t>();
      m->parentAppId = parentAppId;
      m->appId       = appId;
      m->eleName     = mem::duplStr(eleName);
      m->link        = p->appIdMap;
      p->appIdMap    = m;

      return rc;
    }

    ele_t* _createEle( ui_t* p, ele_t* parent, unsigned appId, const char* eleName )
    {
      ele_t* e = mem::allocZ<ele_t>();
      e->parent  = parent;
      e->uuId    = p->eleN;
      e->appId   = appId;
      e->eleName    = mem::duplStr(eleName);
      
      if( p->eleN == p->eleAllocN )
      {
        p->eleAllocN += 100;
        p->eleA       = mem::resizeZ<ele_t*>(p->eleA,p->eleAllocN);
      }

      p->eleA[ p->eleN ] = e;
      p->eleN += 1;

      // if the given appId was not valid ...
      if( appId == kInvalidId && parent != nullptr )
      {
        appIdMapRecd_t* m;
        // ... then try to look it up from the appIdMap.
        if((m = _findAppIdMap( p, parent->appId, eleName)) != nullptr )
          e->appId = m->appId;
      }
      
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

    ele_t* _eleNameToEle( ui_t* p, const char* eleName, bool errorFl=true )
    {
      for(unsigned i=0; i<p->eleN; ++i)
        if( textCompare(p->eleA[i]->eleName,eleName) == 0 )
          return p->eleA[i];
      
      if( errorFl )
        cwLogError(kInvalidIdRC,"The element with eleName:%s not found.",cwStringNullGuard(eleName));
      
      return nullptr;
    }

    // Given a parent UuId and a eleName find the associated ele
    ele_t* _parentUuId_EleName_ToEle( ui_t* p, unsigned parentUuId, const char* eleName, bool errorFl=true )
    {
      // if we are looking for the root 
      if( (parentUuId==kRootUuId || parentUuId == kInvalidId) && textCompare(eleName,"uiDivId")==0 )
      {
        for(unsigned i=0; i<p->eleN; ++i)
          if( p->eleA[i]->parent==nullptr && p->eleA[i]->uuId==kRootUuId)
            return p->eleA[i];
        
      }
      else // we are looking for an elment which is not the root
      {
        for(unsigned i=0; i<p->eleN; ++i)
          if( ((p->eleA[i]->parent==nullptr && parentUuId == kRootUuId) || (p->eleA[i]->parent != nullptr && parentUuId == p->eleA[i]->parent->uuId)) &&  (textCompare(p->eleA[i]->eleName,eleName) == 0))
            return p->eleA[i];
      }
        
          
      if( errorFl )
        cwLogError(kInvalidIdRC,"The element with parent uuid:%i and eleName:%s is not found.",parentUuId,eleName);
      
      return nullptr;
    }
    
    unsigned _findElementUuId( ui_t* p, const char* eleName )
    {
      for(unsigned i=0; i<p->eleN; ++i)
        if( strcmp(p->eleA[i]->eleName,eleName) == 0 )
          return p->eleA[i]->uuId;
  
      return kInvalidId;
    }

    const char* _findEleEleName( ui_t* p, unsigned uuId )
    {
      for(unsigned i=0; i<p->eleN; ++i)
        if( p->eleA[i]->uuId == uuId )
          return p->eleA[i]->eleName;
      
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
      rc_t _createOneEle( ui_t* p, unsigned& uuIdRef, const char* eleTypeStr, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, ARGS&&... args )
    {
      // { op:create, parent:my_parent_id, value:{ button:{ eleName:my_eleName, appId:appId, uuId:uuId, class:clas, title:'my title' } }
      rc_t           rc         = kOkRC;
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

      // create the local representation of the new element
      newEle = _createEle( p, parentEle, appId, eleName );

      // form the create json message string
      //unsigned i = snprintf( p->buf, p->bufN, "{ \"op\":\"create\", \"parent\":\"%s\", \"children\":{ \"%s\":{ \"eleName\":\"%s\", \"appId\":%i, \"uuId\":%i, \"class\":\"%s\", \"title\":\"%s\" ", parentEleName, eleTypeStr, eleName, appId, newEle->uuId, clas, title );
      unsigned i = snprintf( p->buf, p->bufN, "{ \"op\":\"create\", \"parentUuId\":\"%i\", \"type\":\"%s\", \"eleName\":\"%s\", \"appId\":\"%i\", \"uuId\":%i, \"class\":\"%s\", \"title\":\"%s\" ", parentEle->uuId, eleTypeStr, eleName, appId, newEle->uuId, clas, title );
      

      // add the UI specific attributes
      i += format_attributes(p->buf+i, p->bufN-i, 0, std::forward<ARGS>(args)...);

      // terminate the message
      i += toText(p->buf+i, p->bufN-i, "}");

      if( i >= p->bufN )
        return cwLogError(kBufTooSmallRC,"The UI message formatting buffer is too small. (size:%i bytes)", p->bufN);

      printf("%s\n",p->buf);

      // send the message 
      rc =  _websockSend( p, wsSessId, p->buf );

      uuIdRef = newEle->uuId;
      
      return rc;
    }

    ele_t* _findOrCreateEle( ui_t* p, ele_t* parentEle, const char* eleName )
    {
      ele_t* ele;
      if((ele = _parentUuId_EleName_ToEle( p, parentEle->uuId, eleName, false )) == nullptr )
        ele = _createEle(p, parentEle, kInvalidId, eleName );

      return ele;
    }

    rc_t _createElementsFromChildList( ui_t* p, object_t* po, unsigned wsSessId, ele_t* parentEle );

    // 
    rc_t _createEleFromRsrsc( ui_t* p, ele_t* parentEle, const char* eleType, object_t* o, unsigned wsSessId )
    {
      rc_t      rc      = kOkRC;
      object_t* co      = nullptr;
      ele_t*    ele     = nullptr;
      char*     eleName = nullptr;

      if( !o->is_dict() )
        return cwLogError(kSyntaxErrorRC,"All ui element resource records must be dictionaries.");
      
      // if this object has a 'children' list then unlink it an save it for later
      if((co = o->find("children", kNoRecurseFl | kOptionalFl)) != nullptr )
      {
        co = co->parent;
        co->unlink();
      }

      // get the ui ele name
      if((rc = o->get("name",eleName)) != kOkRC )
      {
        rc = cwLogError(rc,"The UI element name could not be read.");
        goto errLabel;
      }
      
      // get or create the ele record to associate with this ele
      if((ele = _findOrCreateEle( p, parentEle, eleName )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The local element '%s' could not be created.",cwStringNullGuard(eleName));
        goto errLabel;
      }

      if( o->insertPair("uuId",ele->uuId) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The 'uuid' node insertion failed on UI element '%s'.",cwStringNullGuard(eleName));
        goto errLabel;
      }

      if( ele->appId != kInvalidId )
      {
        if( o->insertPair("appId",ele->appId) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The 'appId' node insertion failed on UI element '%s'.",cwStringNullGuard(eleName));
          goto errLabel;
        }
      } 

      if( o->insertPair("parentUuId",parentEle->uuId) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The 'parentUuId' node insertion failed on UI element '%s'.",cwStringNullGuard(eleName));
        goto errLabel;
      }
          
      if( o->insertPair("op","create") == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The 'op' node insertion failed on UI element '%s'.",cwStringNullGuard(eleName));
        goto errLabel;
      }

      if( o->insertPair("type",eleType) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The 'eleType' node insertion failed on UI element '%s'.",cwStringNullGuard(eleName));
        goto errLabel;
      }

      if( o->to_string(p->buf,p->bufN) >= p->bufN )
      {
        rc = cwLogError(kOpFailRC,"Conversion to JSON string failed on UI element '%s'.",cwStringNullGuard(eleName));
        goto errLabel;
      }

      printf("%s\n",p->buf);
     
      if((rc =  _websockSend( p, wsSessId, p->buf )) != kOkRC )
      {
        rc = cwLogError(rc,"The creation request send failed on UI element '%s'.", cwStringNullGuard(eleName));
        goto errLabel;
      }
     
      
      // if this element has a list of children then create them here
      if( co != nullptr )
        rc = _createElementsFromChildList(p, co->pair_value(), wsSessId, ele );


    errLabel:
      if( co != nullptr )
        co->free();

      return rc;
    }
    
    // 'od' is an object dictionary where each pair in the dictionary has
    // the form: 'eleType':{ <object> }    
    rc_t _createElementsFromChildList( ui_t* p, object_t* po, unsigned wsSessId, ele_t* parentEle )
    {
      rc_t rc = kOkRC;
      
      if( !po->is_dict() )
        return cwLogError(kSyntaxErrorRC,"All UI resource elements must be containers.");
            
      unsigned childN = po->child_count();

      for(unsigned i=0; i<childN; ++i)
      {
        object_t* o = po->child_ele(i);
        
        if( !o->is_pair() )
          return cwLogError(kSyntaxErrorRC,"All object dictionary children must be pairs.");

        if((rc = _createEleFromRsrsc(p, parentEle, o->pair_label(), o->pair_value(), wsSessId )) != kOkRC )
          return rc;
        
      }
      
      return rc;
    }

    rc_t _createFromObj( ui_t* p, object_t* o, unsigned wsSessId, unsigned parentUuId )
    {
      rc_t        rc        = kOkRC;
      object_t*   po        = nullptr;
      ele_t*      parentEle = nullptr;
      char*       eleName   = nullptr;

      // locate the the 'parent' ele name value object
      if((po = o->find("parent",kNoRecurseFl | kOptionalFl)) == nullptr )
        return cwLogError(kSyntaxErrorRC,"UI resources must have a root 'parent' value.");
      

      // get the parent element name
      if((rc = po->value(eleName)) != kOkRC )
        return cwLogError(kOpFailRC,"The root 'parent' value could not be accessed.");
        
      // find the parent element
      if((parentEle = _parentUuId_EleName_ToEle( p, parentUuId, eleName )) == nullptr )
        return cwLogError(kSyntaxErrorRC,"A parent UI element named '%s' could not be found.",cwStringNullGuard(eleName));
      
      // unlink the 'parent' pair
      po = po->parent;
      
      po->unlink();
      
      rc =  _createElementsFromChildList( p, o, wsSessId, parentEle );

      po->free();

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
      
      unsigned i = snprintf(p->buf,p->bufN,"{ \"op\":\"set_app_id\", \"parentUuId\":%i, \"eleName\":\"%s\", \"appId\":%i, \"uuId\":%i }", ele->parent->uuId, ele->eleName, ele->parent->appId, ele->appId );

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
      
      const char* eleName = nextNonWhiteChar(nextWhiteChar(s0));

      // verifity the message tokens
      if( s0 == nullptr || eleName == nullptr )
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
      if(( ele = _parentUuId_EleName_ToEle( p, parentUuId, eleName, false )) == nullptr )
      {
        // look up the parent/eleName pair map
        appIdMapRecd_t* m = _findAppIdMap( p, parentEle->appId, eleName );

        // create the ele 
        ele = _createEle( p, parentEle, m==nullptr ? kInvalidId : m->appId, eleName );

        printf("creating: parent uuid:%i js:%s \n", parentUuId,eleName);

        // notify the app of the new ele's uuid and appId
        if( m != nullptr )
          _send_app_id_msg( p, wsSessId, ele );
        
      }
      else
      {
        printf("parent uuid:%i js:%s already exists.\n", parentUuId,eleName);
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
  if((ele = _createEle(p, nullptr, kRootAppId, "uiDivId" )) == nullptr || ele->uuId != kRootUuId )
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

unsigned cw::ui::findElementAppId(  handle_t h, unsigned parentUuId, const char* eleName )
{
  ui_t* p = _handleToPtr(h);
  
  for(unsigned i=0; i<p->eleN; ++i)
    if( p->eleA[i]->parent->uuId==parentUuId && strcmp(p->eleA[i]->eleName,eleName) == 0 )
      return p->eleA[i]->appId;
  return kInvalidId;
}

unsigned cw::ui::findElementUuId( handle_t h, unsigned parentUuId, const char* eleName )
{
  ui_t*  p = _handleToPtr(h);
  ele_t* ele;

  if((ele = _parentUuId_EleName_ToEle(p, parentUuId, eleName )) != nullptr )
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

const char* cw::ui::findElementName( handle_t h, unsigned uuId )
{
  ui_t* p = _handleToPtr(h);
  return _findEleEleName(p,uuId);
}

unsigned cw::ui::findElementUuId( handle_t h, const char* eleName )
{
  ui_t* p = _handleToPtr(h);
  
  return _findElementUuId(p,eleName);
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
   
cw::rc_t cw::ui::createDiv( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "div", wsSessId, parentUuId, eleName, appId, clas, title ); }

cw::rc_t cw::ui::createTitle( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "option", wsSessId, parentUuId, eleName, appId, clas, title ); }

cw::rc_t cw::ui::createButton( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "button", wsSessId, parentUuId, eleName, appId, clas, title ); }

cw::rc_t cw::ui::createCheck( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, bool value  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "check", wsSessId, parentUuId, eleName, appId, clas, title, "value", value ); }

cw::rc_t cw::ui::createSelect( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "select", wsSessId, parentUuId, eleName, appId, clas, title ); }

cw::rc_t cw::ui::createOption( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "option", wsSessId, parentUuId, eleName, appId, clas, title ); }

cw::rc_t cw::ui::createString( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, const char* value )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "string", wsSessId, parentUuId, eleName, appId, clas, title, "value", value ); }

cw::rc_t cw::ui::createNumber( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double value, double minValue, double maxValue, double stepValue, unsigned decpl )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "number", wsSessId, parentUuId, eleName, appId, clas, title, "value", value, "min", minValue, "max", maxValue, "step", stepValue, "decpl", decpl ); }

cw::rc_t cw::ui::createProgress(  handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double value, double minValue, double maxValue )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "progress", wsSessId, parentUuId, eleName, appId, clas, title, "value", value, "min", minValue, "max", maxValue ); }

cw::rc_t cw::ui::createText(   handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title )
{
  rc_t rc= kOkRC;
  return rc;
}



cw::rc_t cw::ui::registerAppIds(  handle_t h, const appIdMap_t* map, unsigned mapN )
{
  ui_t* p  = _handleToPtr(h);
  rc_t  rc = kOkRC;

  for(unsigned i=0; i<mapN; ++i)
    if((rc = _allocAppIdMap( p, map[i].parentAppId, map[i].appId, map[i].eleName )) != kOkRC )
      return rc;
  
  return rc;
}




 
