#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFileSys.h"
#include "cwThread.h"
#include "cwObject.h"
#include "cwWebSock.h"
#include "cwWebSockSvr.h"
#include "cwText.h"
#include "cwNumericConvert.h"

#include "cwUi.h"

#define UI_CLICKABLE_LABEL "clickable"
#define UI_SELECT_LABEL    "select"
#define UI_VISIBLE_LABEL   "visible"
#define UI_ENABLE_LABEL    "enable"

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
      struct ele_str* parent;  // pointer to parent ele - or nullptr if this ele is the root ui ele
      unsigned        parentAppId; 
      unsigned        uuId;    // UI unique id - automatically generated and unique among all elements that are part of this ui_t object.
      unsigned        appId;   // application assigned id - application assigned id
      unsigned        chanId;  //
      char*           eleName; // javascript id
      object_t*       attr;    // attribute object
    } ele_t;
    
    typedef struct ui_str
    {
      unsigned             eleAllocN;   // size of eleA[]
      unsigned             eleN;        // count of ele's in use
      ele_t**              eleA;        // eleA[ eleAllocN ] 
      uiCallback_t         uiCbFunc;    // app. cb func
      void*                uiCbArg;     // app. cb func arg.
      sendCallback_t       sendCbFunc;
      void*                sendCbArg;
      appIdMapRecd_t*      appIdMap;    // map of application parent/child/js id's
      char*                buf;         // buf[bufN] output message formatting buffer
      unsigned             bufN;        //
      
      unsigned*            sessA;       // sessA[ sessN ] array of wsSessId's
      unsigned             sessN;
      unsigned             sessAllocN;
      
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
      
      for(unsigned i=0; i<p->eleN; ++i)
      {
        if( p->eleA[i]->attr != nullptr )
          p->eleA[i]->attr->free();
        
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

      mem::release(p->sessA);
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
      m->eleName     = eleName==nullptr ? nullptr : mem::duplStr(eleName);
      m->link        = p->appIdMap;
      p->appIdMap    = m;

      return rc;
    }

    rc_t _registerAppIdMap(  ui_t* p, const appIdMap_t* map, unsigned mapN )
    {
      rc_t  rc = kOkRC;
      
      if( map != nullptr )
        for(unsigned i=0; i<mapN; ++i)
          if((rc = _allocAppIdMap( p, map[i].parentAppId, map[i].appId, map[i].eleName )) != kOkRC )
            return rc;
  
      return rc;
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

    unsigned _findElementUuId( ui_t* p, const char* eleName )
    {
      for(unsigned i=0; i<p->eleN; ++i)
        if( textCompare(p->eleA[i]->eleName,eleName) == 0 )
          return p->eleA[i]->uuId;
  
      return kInvalidId;
    }

    unsigned _findElementUuId( ui_t* p, unsigned appId )
    {
      for(unsigned i=0; i<p->eleN; ++i)
        if( p->eleA[i]->appId == appId )
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
      rc_t rc = kOkRC;
      
      //return websock::send( websockSrv::websockHandle( p->wssH ), kUiProtocolId, wsSessId, msg, strlen(msg) );
      if( p->sendCbFunc != nullptr )
      {
        unsigned msgByteN = msg==nullptr ? 0 : strlen(msg);

        if( wsSessId != kInvalidId )
          rc = p->sendCbFunc( p->sendCbArg, wsSessId, msg, msgByteN );
        else
          {
            for(unsigned i=0; i<p->sessN; ++i)
              rc = p->sendCbFunc( p->sendCbArg, p->sessA[i], msg, msgByteN );
            
          }
      }
      
      return rc;
    }


    // terminating condition for format_attributes()
    void _create_attributes( ele_t* e )
    {  }
    
    template<typename T, typename... ARGS>
      void _create_attributes(ele_t* e, const char* label, T value, ARGS&&... args)
    {
      e->attr->insert_pair(label,value);
      
      _create_attributes(e,std::forward<ARGS>(args)...);      
    }

    bool _has_attribute( ele_t* e, const char* label )
    { return e->attr->find_child(label) != nullptr; }

    bool _get_attribute_bool( ele_t* e, const char* label )
    {
      bool value = false;
      const object_t* pair_value;
      if((pair_value = e->attr->find(label)) == nullptr )
        return false;

      pair_value->value(value);

      return value;        
    }

    rc_t _set_attribute_bool( ele_t* e, const char* label, bool value )
    {
      object_t* pair_value;
      if((pair_value = e->attr->find(label)) == nullptr )
        _create_attributes(e,label,value);
      else
        pair_value->set_value(value);

      return kOkRC;        
      
    }

    

    // Convert the ele_t 'attr' object into the attributes for a JSON message.
    unsigned _format_attributes( char* buf, unsigned n, unsigned i, ele_t* ele )
    {
      assert( ele->attr != nullptr );
      
      for(unsigned j=0; j<ele->attr->child_count() && i<n; ++j)
      {
        object_t* ch = ele->attr->child_ele(j);
        
        i += toText(buf+i, n-i, ",\"" );
        i += toText(buf+i, n-i, ch->pair_label()  );
        i += toText(buf+i, n-i, "\":" );
        i += ch->pair_value()->to_string( buf+i, n-i );        
      }

      return i;
    }
    
    rc_t _transmitOneEle( ui_t* p, unsigned wsSessId,  ele_t* ele )
    {
      rc_t rc = kOkRC;
      
      assert( ele != nullptr );      
      assert( ele->parent != nullptr );

      unsigned i = snprintf( p->buf, p->bufN,
                             "{ \"op\":\"create\", \"parentUuId\":\"%i\", \"eleName\":\"%s\", \"appId\":\"%i\", \"uuId\":%i ",
                             ele->parent->uuId,
                             ele->eleName==nullptr ? "" : ele->eleName,
                             ele->appId,
                             ele->uuId );
      

      // add the UI specific attributes
      i += _format_attributes(p->buf+i, p->bufN-i, 0, ele);

      // terminate the message
      i += toText(p->buf+i, p->bufN-i, "}");

      if( i >= p->bufN )
        return cwLogError(kBufTooSmallRC,"The UI message formatting buffer is too small. (size:%i bytes)", p->bufN);

      //printf("%s\n",p->buf);

      // send the message 
      rc =  _websockSend( p, wsSessId, p->buf );

      return rc;
      
    }

    rc_t _transmitTree( ui_t* p, unsigned wsSessId, ele_t* ele )
    {
      rc_t rc;
      
      // transmit the parent (unelss 'ele' is the root
      if(ele->uuId == kRootUuId || (rc = _transmitOneEle(p,wsSessId,ele)) == kOkRC )
      {
        // Transmit each of the children to the remote UI's.
        // Note that this requires going through all the nodes and picking out the ones whose
        // parent uuid matches the current ele's uuid 
        for(unsigned i=0; i<p->eleN; ++i)
          if( p->eleA[i]->uuId != kRootUuId && p->eleA[i]->uuId != ele->uuId && p->eleA[i]->parent->uuId == ele->uuId )
            if((rc = _transmitTree(p,wsSessId,p->eleA[i]))!=kOkRC )
              break;

      }
      
      return rc;
    }
    

    // Create the base element record.  The attributes mut be filled in by the calling function.
    // Note that if 'appId' is kInvalidId then this function will attempt to lookup the appId in p->appIdMap[].
    ele_t* _createBaseEle( ui_t* p, ele_t* parent, unsigned appId, unsigned chanId, const char* eleName, const char* eleTypeStr=nullptr, const char* eleClass=nullptr, const char* eleTitle=nullptr )
    {
      ele_t* e = mem::allocZ<ele_t>();
      unsigned parentAppId = kInvalidId;
      
      // Go up the tree looking for the first parent with a valid appId
      // This is useful to cover the situation where the parent is an unamed div (e.g. row, col, panel)
      // and the appIdMap gives the panel name as the parent.

      if( parent != nullptr )
      {
        parentAppId = parent->appId;
        for(const ele_t* par=parent; par!=nullptr; par=par->parent)
          if( par->appId != kInvalidId )
          {
            parentAppId = par->appId;
            break;
          }
      }
      
      
      e->parent      = parent;
      e->parentAppId = parentAppId;
      e->uuId        = p->eleN;
      e->appId       = appId;
      e->chanId      = chanId;
      e->eleName     = eleName==nullptr ? nullptr : mem::duplStr(eleName);
      e->attr        = newDictObject();
      
      if( eleTypeStr != nullptr )
        e->attr->insert_pair("type",eleTypeStr);
      
      if( eleClass != nullptr )
        e->attr->insert_pair("className",eleClass);

      if( eleTitle != nullptr )
        e->attr->insert_pair("title",eleTitle);

      // elements default to visible and enabled
      e->attr->insert_pair("visible",true);
      e->attr->insert_pair("enable",true);
      
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
        if((m = _findAppIdMap(p, e->parentAppId, eleName)) != nullptr )
          e->appId = m->appId;
      }

      //printf("uuid:%i appId:%i par-uuid:%i %s\n", e->uuId,e->appId,e->parent==nullptr ? -1 : e->parent->uuId, cwStringNullGuard(e->eleName));
       
      return e;
    }

    
    
    template< typename... ARGS>
    rc_t _createOneEle( ui_t* p, unsigned& uuIdRef, const char* eleTypeStr, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, ARGS&&... args )
    {
      rc_t           rc         = kOkRC;
      ele_t*         newEle     = nullptr;
      ele_t*         parentEle  = nullptr;
      
      uuIdRef = kInvalidId;

      if( parentUuId == kInvalidId )
        parentUuId = kRootUuId;
      
      // get the parent element
      if(( parentEle =  _uuIdToEle(p, parentUuId )) == nullptr )
        return cwLogError( kInvalidArgRC, "Unable to locate the parent element (id:%i).", parentUuId );

      // create the base element
      newEle = _createBaseEle(p, parentEle, appId, chanId, eleName, eleTypeStr, clas, title );

      // create the attributes
      _create_attributes(newEle, std::forward<ARGS>(args)...);

      uuIdRef = newEle->uuId;

      rc = _transmitOneEle(p, wsSessId, newEle);
      
      return rc;
    }


    bool _is_div_type( const char* eleType )
    {
      const char* divAliasA[] = { "div","row","col","panel",nullptr }; // all these types are div's
      bool divAliasFl         = false;

      // is this element a 'div' alias?
      for(unsigned i=0; divAliasA[i]!=nullptr; ++i)
        if( textCompare(divAliasA[i],eleType) == 0 )
        {
          divAliasFl = true;
          break;
        }

      return divAliasFl;      
    }

    rc_t _createElementsFromChildList( ui_t* p, const object_t* po, unsigned wsSessId, ele_t* parentEle, unsigned chanId );
    
    rc_t _createEleFromRsrsc( ui_t* p, ele_t* parentEle, const char* eleType, unsigned chanId, const object_t* srcObj, unsigned wsSessId )
    {
      rc_t        rc          = kOkRC;
      object_t*   co          = nullptr;
      ele_t*      ele         = nullptr;
      char*       eleName     = nullptr;
      object_t*   o           = srcObj->duplicate(); // duplicate the rsrc object so that we can modify it.
      bool        divAliasFl  = false;

      if( !o->is_dict() )
        return cwLogError(kSyntaxErrorRC,"All ui element resource records must be dictionaries.");
      
      // if this object has a 'children' list then unlink it and save it for later
      if((co = o->find("children", kOptionalFl)) != nullptr )
      {
        co = co->parent;
        co->unlink();
      }

      divAliasFl = _is_div_type(eleType);
            
      // get the ui ele name
      if((rc = o->get("name",eleName, cw::kOptionalFl)) != kOkRC )
      {
        // div's and titles don't need a 'name'
        if( rc == kLabelNotFoundRC && (divAliasFl || textCompare(eleType,"label")==0) )
          rc = kOkRC;
        else
        {
          rc = cwLogError(rc,"The UI element name could not be read.");
        
          goto errLabel;
        }
      }
      
      // get or create the ele record to associate with this ele
      if((ele = _createBaseEle(p, parentEle, kInvalidId, chanId, eleName, eleType, nullptr, nullptr)) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The local element '%s' could not be created.",cwStringNullGuard(eleName));
        goto errLabel;
      }

      
      if( !divAliasFl )
      {
        unsigned childN    = o->child_count();
        unsigned child_idx = 0;
        
        // transfer the attributes of this resource object to ele->attr
        for(unsigned i=0; i<childN; ++i)
        {
          object_t*   child      = o->child_ele(child_idx);
          const char* pair_label = child->pair_label();

          //if( textCompare(eleType,"list")==0 )
          //  printf("%i list: %s %i\n",i,pair_label,o->child_count());
        
          if( textCompare(pair_label,"name") != 0 && _is_div_type(pair_label)==false )
          {
            child->unlink();
            ele->attr->append_child(child);
          }
          else
          {
            child_idx += 1;
          }
        }
      }

      _transmitOneEle(p,wsSessId,ele);

      // if this element has a list of children then create them here
      if( co != nullptr || divAliasFl )
      {
        // Note that 'div's need not have an explicit 'children' node.
        // Any child node of a 'div' with a dictionary as a value is a child control.
        const object_t* childL = co!=nullptr ? co->pair_value() : srcObj;
        rc = _createElementsFromChildList(p, childL, wsSessId, ele, chanId );
      }

    errLabel:
      if( co != nullptr )
        co->free();

      if( o != nullptr )
        o->free();

      return rc;
    }
    
          
    
    // 'od' is an object dictionary where each pair in the dictionary has
    // the form: 'eleType':{ <object> }    
    rc_t _createElementsFromChildList( ui_t* p, const object_t* po, unsigned wsSessId, ele_t* parentEle, unsigned chanId )
    {
      rc_t rc = kOkRC;
      
      if( !po->is_dict() )
        return cwLogError(kSyntaxErrorRC,"All UI resource elements must be containers.");
            
      unsigned childN = po->child_count();

      for(unsigned i=0; i<childN; ++i)
      {
        const object_t* o = po->child_ele(i);
        
        if( !o->is_pair() )
          return cwLogError(kSyntaxErrorRC,"All object dictionary children must be pairs.");

        // skip pairs whose value is not a dict
        if( o->pair_value()->is_dict() )
        {
          if((rc = _createEleFromRsrsc(p, parentEle, o->pair_label(), chanId, o->pair_value(), wsSessId )) != kOkRC )
            return rc;
        }
      }
      
      return rc;
    }

    // This functions assumes that the cfg object 'o' contains a field named: 'parent'
    // which contains the element name of the parent node.
    rc_t _createFromObj( ui_t* p, const object_t* o, unsigned wsSessId, unsigned parentUuId, unsigned chanId )
    {
      rc_t            rc        = kOkRC;
      const object_t* po        = nullptr;
      ele_t*          parentEle = nullptr;
      char*           eleName   = nullptr;

      if( parentUuId == kInvalidId )
      {
        // locate the the 'parent' ele name value object
        if((po = o->find("parent",kOptionalFl)) == nullptr )
          return cwLogError(kSyntaxErrorRC,"UI resources must have a root 'parent' value.");
      
        // get the parent element name
        if((rc = po->value(eleName)) != kOkRC )
          return cwLogError(kOpFailRC,"The root 'parent' value could not be accessed.");
        
        // find the parent element
        parentEle = _eleNameToEle( p, eleName );
          
      }
      else
      {
        // find the parent element
        parentEle = _uuIdToEle(p,parentUuId);
      }


      if(parentEle == nullptr )
        return cwLogError(kSyntaxErrorRC,"A parent UI element named '%s' could not be found.",cwStringNullGuard(eleName));

      
      rc =  _createElementsFromChildList( p, o, wsSessId, parentEle, chanId );

      return rc;
    }



    // value message format: 'value' <uuid> <value_data_type> ':' <value>
    ele_t* _parse_value_msg( ui_t* p, value_t& valueRef, const char* msg )
    {
      rc_t     rc      = kOkRC;
      char     argType    = 0;
      ele_t*   ele     = nullptr;
      unsigned eleUuId = kInvalidId;
      
      valueRef.tid   = kInvalidTId;
      
      if( msg == nullptr )
      {
        cwLogWarning("Empty 'value' message received from UI.");
        return nullptr;
      }

      // locate the colon prior to the value
      const char* s = strchr(msg,':');
            
      if( s == nullptr || sscanf(msg, "value %i %c ",&eleUuId,&argType) != 2 )
      {
        cwLogError(kSyntaxErrorRC,"Invalid 'value' message from UI: '%s'.", msg );
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
          if((valueRef.u.s = nextNonWhiteChar(s)) != nullptr )
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

    ele_t* _parse_click_msg( ui_t* p, const char* msg )
    {
      ele_t*   ele     = nullptr;
      unsigned eleUuId = kInvalidId;
      
      if( msg == nullptr )
      {
        cwLogWarning("Empty click message received from UI.");
        return nullptr;
      }

      // 
      if( sscanf(msg, "click %i ",&eleUuId) != 1 )
      {
        cwLogError(kSyntaxErrorRC,"Invalid 'click' message from UI: '%s'.", msg );
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

    
    ele_t* _parse_echo_msg(  ui_t* p, const char* msg )
    {
      unsigned eleUuId = kInvalidId;
      ele_t* ele = nullptr;
      
      if( sscanf(msg, "echo %i",&eleUuId) != 1 )
      {
        cwLogError(kSyntaxErrorRC,"Invalid message from UI: '%s'.", msg );
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
         { kClickOpId,          "click" },
         { kEchoOpId,           "echo" },
         { kIdleOpId,           "idle" },
         { kDisconnectOpId,     "disconnect" },
         { kInvalidOpId,        "<invalid>" },       
        };

      for(unsigned i=0; mapA[i].id != kInvalidOpId; ++i)
        if( textCompare(label,mapA[i].label,strlen(mapA[i].label)) == 0 )
          return mapA[i].id;

      return kInvalidOpId;
    }

    template< typename T >
    rc_t _sendValue( ui_t* p, unsigned wsSessId, unsigned uuId, const char* vFmt, const T& value, const char* opStr="value", int vbufN=32 )
    {
      rc_t rc = kOkRC;
      
      if( p->sendCbFunc != nullptr )
      {
        const char* mFmt = "{ \"op\":\"%s\", \"uuId\":%i, \"value\":%s }";
        const int   mbufN = 128;
        char        vbuf[vbufN];
        char        mbuf[mbufN];
    
        if( snprintf(vbuf,vbufN,vFmt,value) >= vbufN-1 )
          return cwLogError(kBufTooSmallRC,"The value msg buffer is too small.");

        if( snprintf(mbuf,mbufN,mFmt,opStr,uuId,vbuf) >= mbufN-1 )
          return cwLogError(kBufTooSmallRC,"The msg buffer is too small.");

        //p->sendCbFunc(p->sendCbArg,wsSessId,mbuf,strlen(mbuf));
        _websockSend(p,wsSessId,mbuf);
      }

      return rc;
    }

    rc_t _onNewRemoteUi( ui_t* p, unsigned wsSessId )
    {
      rc_t rc = kOkRC;

      ele_t* rootEle;
      if((rootEle = _uuIdToEle(p, kRootUuId)) == nullptr )
      {
        rc = cwLogError(kInvalidStateRC,"Unable to locate the UI root element.");
        goto errLabel;
      }

      _transmitTree(p,wsSessId,rootEle);

    errLabel:
      return rc;
    }

    rc_t _setPropertyFlag( handle_t h, const char* propertyStr, unsigned uuId, bool enableFl )
    {
      ui_t* p = _handleToPtr(h);
      rc_t rc = kOkRC;
  
      ele_t* ele = nullptr;
      const char* mFmt = "{ \"op\":\"set\", \"type\":\"%s\", \"uuId\":%i, \"enableFl\":%i }";
      const int   mbufN = 256;
      char        mbuf[mbufN];
      
      if( snprintf(mbuf,mbufN,mFmt,propertyStr,uuId,enableFl?1:0) >= mbufN-1 )
      {
        rc = cwLogError(kBufTooSmallRC,"The msg buffer is too small.");
        goto errLabel;
      }
  
      if((ele = _uuIdToEle(p,uuId)) == nullptr )
      {
        rc = kInvalidIdRC;
        goto errLabel;
      }
      
      if((rc = _set_attribute_bool(ele,propertyStr,enableFl)) != kOkRC )
      {
        cwLogError(rc,"Property assignment failed.");
        goto errLabel;
      }

      if((rc = _websockSend(p,kInvalidId,mbuf)) != kOkRC )
      {
        cwLogError(rc,"'%s' msg transmit failed.",propertyStr);
        goto errLabel;
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Set '%s' failed.",propertyStr);
  
      return rc;
    }
    
    
  }
}

cw::rc_t cw::ui::create(
  handle_t&         h,
  sendCallback_t    sendCbFunc,
  void*             sendCbArg,
  uiCallback_t      uiCbFunc,
  void*             uiCbArg,
  const object_t*   uiRsrc,
  const appIdMap_t* appIdMapA,
  unsigned          appIdMapN,
  unsigned          fmtBufByteN )
{
  rc_t rc = kOkRC;
  ele_t* ele;
  
  if((rc = destroy(h)) != kOkRC )
    return rc;

  if( sendCbFunc == nullptr )
    return cwLogError(kInvalidArgRC,"The UI send callback function must be a valid pointer.");
  
  if( uiCbFunc == nullptr )
    return cwLogError(kInvalidArgRC,"The UI callback function must be a valid pointer.");

  ui_t* p      = mem::allocZ<ui_t>();


  p->eleAllocN  = 100;
  p->eleA       = mem::allocZ<ele_t*>( p->eleAllocN );
  p->eleN       = 0;
  p->uiCbFunc   = uiCbFunc;
  p->uiCbArg    = uiCbArg;
  p->sendCbFunc = sendCbFunc;
  p->sendCbArg  = sendCbArg;
  p->buf        = mem::allocZ<char>(fmtBufByteN);
  p->bufN       = fmtBufByteN;
  
  // create the root element
  if((ele = _createBaseEle(p, nullptr, kRootAppId, kInvalidId, "uiDivId" )) == nullptr || ele->uuId != kRootUuId )
  {
    cwLogError(kOpFailRC,"The UI root element creation failed.");
    goto errLabel;
  }

  // register any supplied appId maps
  if((rc = _registerAppIdMap(p,appIdMapA,appIdMapN)) != kOkRC )
    goto errLabel;

  if( uiRsrc != nullptr )
    if((rc = _createFromObj( p, uiRsrc, kInvalidId, kRootUuId, kInvalidId )) != kOkRC )
      rc = cwLogError(rc,"Create from UI resource failed.");

  
  h.set(p);


 errLabel:

  if( rc != kOkRC )
  {
    _destroy(p);
  }

  return rc;
}


cw::rc_t cw::ui::destroy( handle_t& h )
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


unsigned        cw::ui::sessionIdCount(handle_t h)
{
  ui_t* p = _handleToPtr(h);
  return p->sessN;
}

const unsigned* cw::ui::sessionIdArray(handle_t h)
{
  ui_t* p = _handleToPtr(h);
  return p->sessA;
}


cw::rc_t cw::ui::onConnect( handle_t h, unsigned wsSessId )
{
  ui_t* p = _handleToPtr(h);

  // if the session id array is full ...
  if( p->sessN == p->sessAllocN )
  {
    // ... then expand it
    p->sessAllocN += 16;
    p->sessA = mem::resizeZ<unsigned>(p->sessA,p->sessAllocN);
  }

  // append the new session id
  p->sessA[p->sessN++] = wsSessId;
  
  p->uiCbFunc( p->uiCbArg, wsSessId, kConnectOpId, kInvalidId, kInvalidId, kInvalidId, kInvalidId, nullptr );
  return kOkRC;
}

cw::rc_t cw::ui::onDisconnect( handle_t h, unsigned wsSessId )
{
  ui_t* p = _handleToPtr(h);

  p->uiCbFunc( p->uiCbArg, wsSessId, kDisconnectOpId, kInvalidId, kInvalidId, kInvalidId, kInvalidId, nullptr );

  // erase the disconnected session id by shrinking the array
  for(unsigned i=0; i<p->sessN; ++i)
    if( p->sessA[i] == wsSessId )
    {
      for(; i+1<p->sessN; ++i)
        p->sessA[i] = p->sessA[i+1];
      
      p->sessN -= 1;
      break;
    }
    
  
  return kOkRC;
}

cw::rc_t cw::ui::onReceive( handle_t h, unsigned wsSessId, const void* msg, unsigned msgByteN )
{
  rc_t    rc   = kOkRC;
  ui_t*   p    = _handleToPtr(h);
  opId_t  opId = _labelToOpId((const char*)msg);
  value_t value;
  ele_t*  ele;

  switch( opId )
  {
    case kInitOpId:
      // if the app cfg included a reference to a UI resource file then instantiate it here
      _onNewRemoteUi( p, wsSessId );
      
      // Pass on the 'init' msg to the app.
      p->uiCbFunc( p->uiCbArg, wsSessId, opId, kInvalidId, kInvalidId, kInvalidId, kInvalidId, nullptr );
      break;
                
    case kValueOpId:
      if((ele = _parse_value_msg(p, value, (const char*)msg )) == nullptr )
        cwLogError(kOpFailRC,"UI Value message parse failed.");
      else
      {
        p->uiCbFunc( p->uiCbArg, wsSessId, opId, ele->parentAppId, ele->uuId, ele->appId, ele->chanId, &value );
                  
      }
      break;

    case kClickOpId:
      if((ele = _parse_click_msg(p, (const char*)msg )) == nullptr )
        cwLogError(kOpFailRC,"UI Value message parse failed.");
      else
      {
        p->uiCbFunc( p->uiCbArg, wsSessId, opId, ele->parentAppId, ele->uuId, ele->appId, ele->chanId, &value );
      }
      break;
      
    case kEchoOpId:
      if((ele = _parse_echo_msg(p,(const char*)msg)) == nullptr )
        cwLogError(kOpFailRC,"UI Echo message parse failed.");
      else
      {
        p->uiCbFunc( p->uiCbArg, wsSessId, opId, ele->parentAppId, ele->uuId, ele->appId, ele->chanId,nullptr );               
      }
      break;

    case kIdleOpId:
      p->uiCbFunc( p->uiCbArg, kInvalidId, opId, kInvalidId, kInvalidId, kInvalidId, kInvalidId, nullptr );                     
      break;

    case kInvalidOpId:
      cwLogError(kInvalidIdRC,"The UI received a NULL op. id.");
      break;

    default:
      cwLogError(kInvalidIdRC,"The UI received an unknown op. id.");
      break;
                
  } // switch opId

  return rc;
}

unsigned cw::ui::findElementAppId(  handle_t h, unsigned parentAppId, const char* eleName )
{
  ui_t* p = _handleToPtr(h);
  
  for(unsigned i=0; i<p->eleN; ++i)
    if( p->eleA[i]->parentAppId==parentAppId && strcmp(p->eleA[i]->eleName,eleName) == 0 )
      return p->eleA[i]->appId;
  
  return kInvalidId;
}

unsigned cw::ui::parentAndNameToUuId( handle_t h, unsigned parentAppId, const char* eleName )
{
  ui_t*  p = _handleToPtr(h);

  for(unsigned i=0; i<p->eleN; ++i)
    if( p->eleA[i]->parentAppId==parentAppId && strcmp(p->eleA[i]->eleName,eleName) == 0 )
      return p->eleA[i]->uuId;
  
  return kInvalidId;
}

unsigned cw::ui::parentAndAppIdToUuId( handle_t h, unsigned parentAppId, unsigned appId )
{
  ui_t* p = _handleToPtr(h);
  
  for(unsigned i=0; i<p->eleN; ++i)
    if(((p->eleA[i]->parent==nullptr && parentAppId==kRootAppId) ||
        (p->eleA[i]->parentAppId==parentAppId))
      && p->eleA[i]->appId == appId )
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

unsigned cw::ui::findElementUuId( handle_t h, unsigned appId )
{
  ui_t* p = _handleToPtr(h);
  
  return _findElementUuId(p,appId);  
}


unsigned  cw::ui::findElementAppId(  handle_t h, unsigned uuId )
{
  ui_t* p = _handleToPtr(h);
  
  ele_t* ele = _uuIdToEle( p, uuId );

  return ele==nullptr ? kInvalidId : ele->uuId;
}


cw::rc_t cw::ui::createFromObject( handle_t  h, const object_t* o,  unsigned parentUuId,  unsigned chanId, const char* fieldName )
{
  ui_t*  p         = _handleToPtr(h);
  rc_t   rc        = kOkRC;
  
  //ele_t* parentEle = nullptr;
  
  if( fieldName != nullptr )
    if((o = o->find(fieldName)) == nullptr )
    {
      rc = cwLogError(kSyntaxErrorRC,"Unable to locate the '%s' sub-configuration.",cwStringNullGuard(fieldName));
      goto errLabel;
    }

  if((rc = _createFromObj( p, o, kInvalidId, parentUuId, chanId )) != kOkRC )
    goto errLabel;

 errLabel:
  if(rc != kOkRC )
    rc = cwLogError(rc,"UI instantiation from object failed.");

 return rc;
}


cw::rc_t cw::ui::createFromFile( handle_t  h, const char* fn,  unsigned parentUuId, unsigned chanId)
{
  ui_t*     p  = _handleToPtr(h);
  rc_t      rc = kOkRC;
  object_t* o  = nullptr;
  
  if((rc = objectFromFile( fn, o )) != kOkRC )
    goto errLabel;

  if((rc = _createFromObj( p, o, kInvalidId, parentUuId, chanId )) != kOkRC )
    goto errLabel;

 errLabel:
  if(rc != kOkRC )
    rc = cwLogError(rc,"UI instantiation from the configuration file '%s' failed.", cwStringNullGuard(fn));

  if( o != nullptr )
    o->free();
  
  return rc;
}

cw::rc_t cw::ui::createFromText( handle_t h, const char* text,  unsigned parentUuId, unsigned chanId )
{
  ui_t*     p  = _handleToPtr(h);
  rc_t      rc = kOkRC;
  object_t* o  = nullptr;
  
  if((rc = objectFromString( text, o )) != kOkRC )
    goto errLabel;
    
  if((rc = _createFromObj( p, o, kInvalidId, parentUuId, chanId )) != kOkRC )
    goto errLabel;

 errLabel:
  if(rc != kOkRC )
    rc = cwLogError(rc,"UI instantiation failed from the configuration from string: '%s'.", cwStringNullGuard(text));

  if( o != nullptr )
    o->free();
  
  return rc;
}
   
cw::rc_t cw::ui::createDiv( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "div", kInvalidId, parentUuId, eleName, appId, chanId, clas, title ); }

cw::rc_t cw::ui::createLabel( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "label", kInvalidId, parentUuId, eleName, appId, chanId, clas, title ); }

cw::rc_t cw::ui::createButton( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "button", kInvalidId, parentUuId, eleName, appId, chanId, clas, title ); }

cw::rc_t cw::ui::createCheck( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "check", kInvalidId, parentUuId, eleName, appId, chanId, clas, title ); }

cw::rc_t cw::ui::createCheck( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, bool value  )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "check", kInvalidId, parentUuId, eleName, appId, chanId, clas, title, "value", value ); }

cw::rc_t cw::ui::createSelect( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "select", kInvalidId, parentUuId, eleName, appId, chanId, clas, title ); }

cw::rc_t cw::ui::createOption( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "option", kInvalidId, parentUuId, eleName, appId, chanId, clas, title ); }

cw::rc_t cw::ui::createStr( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "string", kInvalidId, parentUuId, eleName, appId, chanId, clas, title ); }

cw::rc_t cw::ui::createStr( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, const char* value )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "string", kInvalidId, parentUuId, eleName, appId, chanId, clas, title, "value", value ); }

cw::rc_t cw::ui::createNumbDisplay( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, unsigned decpl )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "numb_disp", kInvalidId, parentUuId, eleName, appId, chanId, clas, title, "decpl", decpl ); }

cw::rc_t cw::ui::createNumbDisplay( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, unsigned decpl, double value )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "numb_disp", kInvalidId, parentUuId, eleName, appId, chanId, clas, title, "decpl", decpl, "value", value ); }

cw::rc_t cw::ui::createNumb( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, double minValue, double maxValue, double stepValue, unsigned decpl )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "number", kInvalidId, parentUuId, eleName, appId, chanId, clas, title, "min", minValue, "max", maxValue, "step", stepValue, "decpl", decpl ); }

cw::rc_t cw::ui::createNumb( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, double minValue, double maxValue, double stepValue, unsigned decpl, double value )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "number", kInvalidId, parentUuId, eleName, appId, chanId, clas, title, "min", minValue, "max", maxValue, "step", stepValue, "decpl", decpl, "value", value ); }

cw::rc_t cw::ui::createProg(  handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, double minValue, double maxValue )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "progress", kInvalidId, parentUuId, eleName, appId, chanId, clas, title, "min", minValue, "max", maxValue ); }

cw::rc_t cw::ui::createProg(  handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title, double minValue, double maxValue, double value )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "progress", kInvalidId, parentUuId, eleName, appId, chanId, clas, title, "value", value, "min", minValue, "max", maxValue ); }

cw::rc_t cw::ui::createLog(   handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "log", kInvalidId, parentUuId, eleName, appId, chanId, clas, title);  }

cw::rc_t cw::ui::createList( handle_t h, unsigned& uuIdRef, unsigned parentUuId, const char* eleName, unsigned appId, unsigned chanId, const char* clas, const char* title )
{ return _createOneEle( _handleToPtr(h), uuIdRef, "list", kInvalidId, parentUuId, eleName, appId, chanId, clas, title);  }


cw::rc_t cw::ui::setNumbRange( handle_t h, unsigned uuId, double minValue, double maxValue, double stepValue, unsigned decPl, double value )
{
  rc_t rc = kOkRC;
  ui_t* p = _handleToPtr(h);
  
  const char* mFmt = "{ \"op\":\"set\",  \"type\":\"number_range\", \"uuId\":%i, \"min\":%f, \"max\":%f, \"step\":%f, \"decpl\":%i, \"value\":%f }";
  const int   mbufN = 256;
  char        mbuf[mbufN];
  
  if( snprintf(mbuf,mbufN,mFmt,uuId,minValue,maxValue,stepValue,decPl,value) >= mbufN-1 )
    return cwLogError(kBufTooSmallRC,"The msg buffer is too small.");
  
  rc = _websockSend(p,kInvalidId,mbuf);

  return rc;
}

cw::rc_t cw::ui::setProgRange( handle_t h, unsigned uuId, double minValue, double maxValue, double value )
{
  rc_t rc = kOkRC;  
  ui_t* p = _handleToPtr(h);
  
  const char* mFmt = "{ \"op\":\"set\",  \"type\":\"progress_range\", \"uuId\":%i, \"min\":%f, \"max\":%f, \"value\":%f }";
  const int   mbufN = 256;
  char        mbuf[mbufN];
      
  if( snprintf(mbuf,mbufN,mFmt,uuId,minValue,maxValue,value) >= mbufN-1 )
    return cwLogError(kBufTooSmallRC,"The msg buffer is too small.");
  
  rc = _websockSend(p,kInvalidId,mbuf);

  return rc;
}

cw::rc_t cw::ui::setLogLine(   handle_t h, unsigned uuId, const char* text )
{
  rc_t rc = kOkRC;

  unsigned    n = 0;
  const char* c = text;
  for(; *c; ++c )
    if( *c == '\n')
      ++n;

  if( n == 0 )
    rc = sendValueString(h,uuId,text);
  else
  {
    int sn = textLength(text);
    sn += n + 1;
    
    char s[ sn ];
    unsigned i,j;
    for( i=0,j=0; text[i]; ++i,++j)
    {
      char ch        = text[i];
      bool escape_fl = true;
      
      switch( ch )
      {
      case '\\': ch='\\'; break;
      case '\b': ch='b'; break;
      case '\f': ch='f'; break;
      case '\n': ch='n'; break;
      case '\r': ch='r'; break;
      case '\t': ch='t'; break;
      default:
        escape_fl = false;
        break;
      }

      if( escape_fl )
        s[j++] = '\\';
      
      s[j] = ch;
    }
    
    s[sn-1] = 0;

    printf("%s %s\n",text,s);
    
    rc = sendValueString(h,uuId,s);

  }
  
  return rc;
}

cw::rc_t cw::ui::setClickable( handle_t h, unsigned uuId, bool clickableFl )
{ return _setPropertyFlag( h, UI_CLICKABLE_LABEL, uuId, clickableFl ); }

cw::rc_t cw::ui::clearClickable( handle_t h, unsigned uuId )
{ return setClickable(h,uuId,false); }

bool cw::ui::isClickable( handle_t h, unsigned uuId )
{
  ui_t*  p           = _handleToPtr(h);
  ele_t* ele         = nullptr;
  bool   clickableFl = false;

  if((ele = _uuIdToEle(p,uuId)) != nullptr )
    clickableFl = _get_attribute_bool(ele,UI_CLICKABLE_LABEL);
  
  return clickableFl;   
}


cw::rc_t cw::ui::setSelect( handle_t h, unsigned uuId, bool enableFl )
{ return _setPropertyFlag( h, UI_SELECT_LABEL, uuId, enableFl );   }

cw::rc_t cw::ui::clearSelect( handle_t h, unsigned uuId )
{ return setSelect(h,uuId,false); }

bool cw::ui::isSelected( handle_t h, unsigned uuId )
{
  ui_t*  p        = _handleToPtr(h);
  ele_t* ele      = nullptr;
  bool   selectFl = false;

  if((ele = _uuIdToEle(p,uuId)) != nullptr )
    selectFl = _get_attribute_bool(ele,UI_SELECT_LABEL);
  
  return selectFl;   
}

cw::rc_t cw::ui::setVisible( handle_t h, unsigned uuId, bool enableFl )
{ return _setPropertyFlag( h, UI_VISIBLE_LABEL, uuId, enableFl );   }

cw::rc_t cw::ui::clearVisible( handle_t h, unsigned uuId )
{ return setVisible(h,uuId,false); }

bool cw::ui::isVisible( handle_t h, unsigned uuId )
{
  ui_t*  p         = _handleToPtr(h);
  ele_t* ele       = nullptr;
  bool   visibleFl = false;

  if((ele = _uuIdToEle(p,uuId)) != nullptr )
    visibleFl = _get_attribute_bool(ele,UI_VISIBLE_LABEL);
  
  return visibleFl;   
}

cw::rc_t cw::ui::setEnable( handle_t h, unsigned uuId, bool enableFl )
{ return _setPropertyFlag( h, UI_ENABLE_LABEL, uuId, enableFl );   }

cw::rc_t cw::ui::clearEnable( handle_t h, unsigned uuId )
{ return setEnable(h,uuId,false); }

bool cw::ui::isEnabled( handle_t h, unsigned uuId )
{
  ui_t*  p        = _handleToPtr(h);
  ele_t* ele      = nullptr;
  bool   enableFl = false;

  if((ele = _uuIdToEle(p,uuId)) != nullptr )
    enableFl = _get_attribute_bool(ele,UI_ENABLE_LABEL);
  
  return enableFl;   
}



cw::rc_t cw::ui::registerAppIdMap(  handle_t h, const appIdMap_t* map, unsigned mapN )
{
  return _registerAppIdMap( _handleToPtr(h), map, mapN);
}

cw::rc_t cw::ui::sendValueBool( handle_t h, unsigned uuId, bool value )
{
  ui_t* p  = _handleToPtr(h);
  return _sendValue<int>(p,kInvalidId,uuId,"%i",value?1:0);
}

cw::rc_t cw::ui::sendValueInt( handle_t h, unsigned uuId, int value )
{
  ui_t* p  = _handleToPtr(h);
  return _sendValue<int>(p,kInvalidId,uuId,"%i",value);  
}

cw::rc_t cw::ui::sendValueUInt( handle_t h, unsigned uuId, unsigned value )
{
  ui_t* p  = _handleToPtr(h);
  return _sendValue<unsigned>(p,kInvalidId,uuId,"%i",value);
}

cw::rc_t cw::ui::sendValueFloat( handle_t h, unsigned uuId, float value )
{
  ui_t* p  = _handleToPtr(h);
  return _sendValue<float>(p,kInvalidId,uuId,"%f",value);
  
}

cw::rc_t cw::ui::sendValueDouble( handle_t h, unsigned uuId, double value )
{
  ui_t* p  = _handleToPtr(h);
  return _sendValue<double>(p,kInvalidId,uuId,"%f",value);
}

cw::rc_t cw::ui::sendValueString( handle_t h, unsigned uuId, const char* value )
{
  ui_t* p  = _handleToPtr(h);
  // +10 allows for extra value buffer space for double quotes and slashed
  return _sendValue<const char*>(p,kInvalidId,uuId,"\"%s\"",value,"value",strlen(value)+10);
}

void cw::ui::report( handle_t h )
{
  ui_t* p  = _handleToPtr(h);

  for(unsigned i=0; i<p->eleN; ++i)
  {
    const ele_t* e = p->eleA[i];
    
    unsigned    parUuId    = e->parent==NULL ? kInvalidId : e->parent->uuId;
    const char* parEleName = e->parent==NULL || e->parent->eleName == NULL ? "" : e->parent->eleName;
    
    printf("uu:%5i app:%5i %20s : parent uu:%5i app:%5i %20s ", e->uuId, e->appId, e->eleName == NULL ? "" : e->eleName, parUuId, e->parentAppId, parEleName );

    for(unsigned i=0; i<e->attr->child_count(); ++i)
    {
      e->attr->child_ele(i)->to_string(p->buf,p->bufN);
      printf("%s, ", p->buf );
    }
    printf("\n");
    
  }
  
}



namespace cw
{
  namespace ui
  {
    namespace ws
    {
      typedef struct ui_ws_str
      {
        websock::handle_t wsH;
        ui::handle_t      uiH;
        void*             cbArg;
        uiCallback_t      uiCbFunc;
        websock::cbFunc_t wsCbFunc;
        unsigned          wsTimeOutMs;
      } ui_ws_t;

      ui_ws_t* _handleToPtr( handle_t h )
      { return handleToPtr<handle_t,ui_ws_t>(h); }
      
      rc_t _destroy( ui_ws_t* p )
      {
        rc_t rc;

        if((rc = websock::destroy(p->wsH)) != kOkRC )
          return rc;
        
        if((rc = ui::destroy(p->uiH)) != kOkRC )
          return rc;

        mem::release(p);

        return rc;
      }

      void _webSockCb( void* cbArg, unsigned protocolId, unsigned sessionId, websock::msgTypeId_t msg_type, const void* msg, unsigned byteN )
      {
        ui_ws_t* p = static_cast<ui_ws_t*>(cbArg);

        switch( msg_type )
        {
          case websock::kConnectTId:
            ui::onConnect(p->uiH,sessionId);
            break;
            
          case websock::kDisconnectTId:
            ui::onDisconnect(p->uiH,sessionId);
            break;
          
          case websock::kMessageTId:
            ui::onReceive(p->uiH,sessionId,msg,byteN);
            break;
            
          default:
            cwLogError(kInvalidIdRC,"An invalid websock msgTypeId (%i) was encountered",msg_type);
        }
      }

      rc_t _webSockSend( void* cbArg, unsigned wsSessId, const void* msg, unsigned msgByteN )
      {
        ui_ws_t* p = static_cast<ui_ws_t*>(cbArg);
        return websock::send( p->wsH, kUiProtocolId, kInvalidId, msg, msgByteN );
      }
      
    }
  }
}

cw::rc_t cw::ui::ws::parseArgs(  const object_t& o, args_t& args, const char* object_label )
{
  rc_t rc = kOkRC;
  const object_t* op = &o;
  char* uiCfgFn = nullptr;
  
  memset(&args,0,sizeof(args));

  // if no 'ui' cfg record was given then skip
  if( object_label == nullptr )
    op = &o;
  else
    if((op = o.find(object_label)) == nullptr )
      return cwLogError(kLabelNotFoundRC,"The ui configuration label '%s' was not found.", cwStringNullGuard(object_label));
  
  if((rc = op->getv(
        "physRootDir", args.physRootDir, "dfltPageFn", args.dfltPageFn, "port", args.port, "rcvBufByteN", args.rcvBufByteN,
        "xmtBufByteN", args.xmtBufByteN, "fmtBufByteN", args.fmtBufByteN, "websockTimeOutMs", args.wsTimeOutMs, "uiCfgFn", uiCfgFn )) != kOkRC )
  {
    rc = cwLogError(rc,"'ui' cfg. parse failed.");
  }

  // if a default UI resource script was given then convert it into an object
  if( uiCfgFn != nullptr )
  {
    char* fn = filesys::makeFn(  args.physRootDir, uiCfgFn, nullptr, nullptr );

    if((rc = objectFromFile(fn,args.uiRsrc)) != kOkRC )
      rc = cwLogError(rc,"An error occurred while parsing the UI resource script in '%s'.", cwStringNullGuard(uiCfgFn));

    mem::release(fn);
  }
  
  return rc;
}

cw::rc_t cw::ui::ws::releaseArgs( args_t& args )
{
  if( args.uiRsrc != nullptr )
    args.uiRsrc->free();
  return kOkRC;
}

cw::rc_t cw::ui::ws::create( handle_t& h,
  const args_t&     args,
  void*             cbArg,
  uiCallback_t      uiCbFunc,
  const object_t*   uiRsrc,
  const appIdMap_t* appIdMapA,
  unsigned          appIdMapN,
  websock::cbFunc_t wsCbFunc  )
{
  return create(h,
    args.port,
    args.physRootDir,
    cbArg,
    uiCbFunc,
    uiRsrc,
    appIdMapA,
    appIdMapN,
    wsCbFunc,
    args.dfltPageFn,
    args.wsTimeOutMs,
    args.rcvBufByteN,
    args.xmtBufByteN,
    args.fmtBufByteN );
}

  
cw::rc_t cw::ui::ws::create(  handle_t& h,
  unsigned          port,
  const char*       physRootDir,
  void*             cbArg,
  uiCallback_t      uiCbFunc,
  const object_t*   uiRsrc,
  const appIdMap_t* appIdMapA,
  unsigned          appIdMapN,
  websock::cbFunc_t wsCbFunc,
  const char*       dfltPageFn,
  unsigned          websockTimeOutMs,
  unsigned          rcvBufByteN,
  unsigned          xmtBufByteN,
  unsigned          fmtBufByteN )
{
  rc_t rc = kOkRC;

  if((rc = destroy(h)) != kOkRC )
    return rc;

  ui_ws_t* p = mem::allocZ<ui_ws_t>();

  websock::protocol_t protocolA[] =
    {
     { "http",        kHttpProtocolId,          0,           0 },
     { "ui_protocol", kUiProtocolId,  rcvBufByteN, xmtBufByteN }
    };

  unsigned          protocolN = sizeof(protocolA)/sizeof(protocolA[0]);
  websock::cbFunc_t wsCbF     = wsCbFunc==nullptr ? _webSockCb : wsCbFunc;
  void*             wsCbA     = wsCbFunc==nullptr ? p          : cbArg;
  
  // create the websocket
  if((rc = websock::create(p->wsH, wsCbF, wsCbA, physRootDir, dfltPageFn, port, protocolA, protocolN )) != kOkRC )
  {
    cwLogError(rc,"UI Websock create failed.");
    goto errLabel;
  }

  // create the ui
  if((rc = ui::create(p->uiH, _webSockSend, p, uiCbFunc, cbArg, uiRsrc, appIdMapA, appIdMapN, fmtBufByteN )) != kOkRC )
  {
    cwLogError(rc,"UI object create failed.");
    goto errLabel;
  }

  p->cbArg       = cbArg;
  p->uiCbFunc    = uiCbFunc;
  p->wsCbFunc    = wsCbFunc;
  p->wsTimeOutMs = websockTimeOutMs;

  h.set(p);
  
 errLabel:
  if( rc != kOkRC )
    _destroy(p);
    
  return rc;
}


cw::rc_t cw::ui::ws::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  ui_ws_t* p = nullptr;
  
  if( !h.isValid() )
    return rc;

  p = _handleToPtr(h);
  
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;
}
      
cw::rc_t cw::ui::ws::exec( handle_t h )
{
  rc_t     rc = kOkRC;
  ui_ws_t* p  = _handleToPtr(h);

  if((rc = websock::exec( p->wsH, p->wsTimeOutMs )) != kOkRC)
    cwLogError(rc,"The UI websock execution failed.");

  // make the idle callback
  ui::onReceive( p->uiH, kInvalidId, "idle", strlen("idle") );
        
  return rc;
}

cw::rc_t cw::ui::ws::onReceive( handle_t h, unsigned protocolId, unsigned sessionId, websock::msgTypeId_t msg_type, const void* msg, unsigned byteN )
{
  ui_ws_t* p = _handleToPtr(h);
  _webSockCb( p, protocolId, sessionId, msg_type, msg, byteN );
  return kOkRC;
}

cw::websock::handle_t cw::ui::ws::websockHandle( handle_t h )
{
  ui_ws_t* p  = _handleToPtr(h);
  return p->wsH;
}

cw::ui::handle_t cw::ui::ws::uiHandle( handle_t h )
{
  ui_ws_t* p  = _handleToPtr(h);
  return p->uiH;
}


 
namespace cw
{
  namespace ui
  {
    namespace srv
    {
      typedef struct ui_ws_srv_str
      {
        ws::handle_t     wsUiH;
        thread::handle_t thH;
      } ui_ws_srv_t;

      ui_ws_srv_t* _handleToPtr(handle_t h )
      { return handleToPtr<handle_t,ui_ws_srv_t>(h); }

      rc_t _destroy( ui_ws_srv_t* p )
      {
        rc_t rc;
        if((rc = thread::destroy(p->thH)) != kOkRC )
          return rc;
        
        if((rc = ws::destroy(p->wsUiH)) != kOkRC )
          return rc;

        mem::release(p);

        return rc;
      }

      bool _threadCallback( void* arg )
      {
        ui_ws_srv_t* p = static_cast<ui_ws_srv_t*>(arg);
        rc_t rc;
        
        if((rc = ws::exec(p->wsUiH)) != kOkRC )
        {
          cwLogError(rc,"Websocket UI exec failed.");
        }

        return true;
      }
    }
  }
}

cw::rc_t cw::ui::srv::create(  handle_t& h,
  unsigned          port,
  const char*       physRootDir,
  void*             cbArg,
  uiCallback_t      uiCbFunc,
  const object_t*   uiRsrc,
  const appIdMap_t* appIdMapA,
  unsigned          appIdMapN,
  websock::cbFunc_t wsCbFunc,
  const char*       dfltPageFn,
  unsigned          websockTimeOutMs,
  unsigned          rcvBufByteN,
  unsigned          xmtBufByteN,
  unsigned          fmtBufByteN )
{
  rc_t rc = kOkRC;
  if((rc = destroy(h)) != kOkRC )
    return rc;

  ui_ws_srv_t* p = mem::allocZ<ui_ws_srv_t>();
  
  if((rc = ws::create(p->wsUiH, port, physRootDir, cbArg, uiCbFunc, uiRsrc,appIdMapA, appIdMapN, wsCbFunc, dfltPageFn, websockTimeOutMs, rcvBufByteN, xmtBufByteN, fmtBufByteN )) != kOkRC )
  {
    cwLogError(rc,"The websock UI creationg failed.");
    goto errLabel;
  }

  if((rc = thread::create( p->thH, _threadCallback, p )) != kOkRC )
  {
    cwLogError(rc,"The websock UI server thread create failed.");
    goto errLabel;
  }

  h.set(p);
  
 errLabel:
  if( rc != kOkRC )
    _destroy(p);

  return rc;
}

cw::rc_t cw::ui::srv::create( handle_t& h,
  const ws::args_t&     args,
  void*             cbArg,
  uiCallback_t      uiCbFunc,
  const appIdMap_t* appIdMapA,
  unsigned          appIdMapN,        
  websock::cbFunc_t wsCbFunc )
{
  return create(h,
    args.port,
    args.physRootDir,
    cbArg,
    uiCbFunc,
    args.uiRsrc,
    appIdMapA,
    appIdMapN,
    wsCbFunc,
    args.dfltPageFn,
    args.wsTimeOutMs,
    args.rcvBufByteN,
    args.xmtBufByteN,
    args.fmtBufByteN );
}
      


cw::rc_t cw::ui::srv::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  
  if( !h.isValid() )
    return rc;

  ui_ws_srv_t* p = _handleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();

  return rc;
}

cw::rc_t cw::ui::srv::start( handle_t h )
{
  ui_ws_srv_t* p = _handleToPtr(h);
  rc_t         rc;
  
  if((rc = thread::unpause(p->thH)) != kOkRC )
    cwLogError(rc,"WebockUI server thread start failed.");
  return rc;
}

cw::rc_t cw::ui::srv::stop( handle_t h )
{
  ui_ws_srv_t* p = _handleToPtr(h);
  rc_t         rc;
  
  if((rc = thread::pause(p->thH, thread::kPauseFl | thread::kWaitFl )) != kOkRC )
    cwLogError(rc,"WebockUI server thread stop failed.");
  
  return rc;
}

cw::thread::handle_t  cw::ui::srv::threadHandle( handle_t h )
{
  ui_ws_srv_t* p = _handleToPtr(h);
  return p->thH;
}

cw::websock::handle_t cw::ui::srv::websockHandle( handle_t h )
{
  ui_ws_srv_t* p = _handleToPtr(h);
  return ws::websockHandle(p->wsUiH);
}

cw::ui::handle_t      cw::ui::srv::uiHandle( handle_t h )
{
  ui_ws_srv_t* p = _handleToPtr(h);
  return ws::uiHandle(p->wsUiH);
}
