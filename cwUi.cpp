#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwThread.h"
#include "cwWebSock.h"
#include "cwWebSockSvr.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwObject.h"

#include "cwUi.h"

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
      char*           eleName; // javascript id
    } ele_t;
    
    typedef struct ui_str
    {
      unsigned             eleAllocN; // size of eleA[]
      unsigned             eleN;      // count of ele's in use
      ele_t**              eleA;      // eleA[ eleAllocN ] 
      uiCallback_t         uiCbFunc;    // app. cb func
      void*                uiCbArg;     // app. cb func arg.
      sendCallback_t       sendCbFunc;
      void*                sendCbArg;
      appIdMapRecd_t*      appIdMap;  // map of application parent/child/js id's
      char*                buf;       // buf[bufN] output message formatting buffer
      unsigned             bufN;      //
      
      unsigned*            sessA;
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
        if( textCompare(p->eleA[i]->eleName,eleName) == 0 )
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
        return p->sendCbFunc( p->sendCbArg, wsSessId, msg, msgByteN );
      }
      
      return rc;
    }

    
    
    ele_t* _createEle( ui_t* p, ele_t* parent, unsigned appId, const char* eleName )
    {
      ele_t* e = mem::allocZ<ele_t>();

      // got up the tree looking for a parent with a valid appId
      ele_t* par = parent;
      while( par != nullptr && par->appId == kInvalidId )
        par = par->parent;

      if( par != nullptr )
        parent = par;
      
      e->parent  = parent;
      e->uuId    = p->eleN;
      e->appId   = appId;
      e->eleName    = eleName==nullptr ? nullptr : mem::duplStr(eleName);

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

        // Go up the tree looking for the first parent with a valid appId
        // This is useful to cover the situation where the parent is an unamed div (e.g. row, col, panel)
        // and the appIdMap gives the panel name as the parent.
        unsigned parentAppId = parent->appId;
        for(const ele_t* par=parent; par!=nullptr; par=par->parent)
          if( par->appId != kInvalidId )
          {
            parentAppId = par->appId;
            break;
          }

        // ... then try to look it up from the appIdMap.
        if((m = _findAppIdMap(p, parentAppId, eleName)) != nullptr )
          e->appId = m->appId;
      }

      //printf("uuid:%i appId:%i par-uuid:%i %s\n", e->uuId,e->appId,e->parent==nullptr ? -1 : e->parent->uuId, cwStringNullGuard(e->eleName));
       
      return e;
    }

    ele_t* _findOrCreateEle( ui_t* p, ele_t* parentEle, const char* eleName, unsigned appId=kInvalidId )
    {
      ele_t* ele = nullptr;

      // if an ele name was given
      if( eleName != nullptr )
      {
        // check for an existing child of parentEle with the same name
        ele = _parentUuId_EleName_ToEle( p, parentEle->uuId, eleName, false );

        // if a child with the same name does exist but has a different app id then
        // ignore the match and create a new element
        if( ele != nullptr && appId != kInvalidId && ele->appId != appId )
          ele = nullptr;
      }
      
      if(ele == nullptr )
        ele = _createEle(p, parentEle, appId, eleName );

      return ele;
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
      newEle = _findOrCreateEle( p, parentEle, eleName, appId );

      // form the create json message string
      //unsigned i = snprintf( p->buf, p->bufN, "{ \"op\":\"create\", \"parent\":\"%s\", \"children\":{ \"%s\":{ \"eleName\":\"%s\", \"appId\":%i, \"uuId\":%i, \"class\":\"%s\", \"title\":\"%s\" ", parentEleName, eleTypeStr, eleName, appId, newEle->uuId, clas, title );
      unsigned i = snprintf( p->buf, p->bufN, "{ \"op\":\"create\", \"parentUuId\":\"%i\", \"type\":\"%s\", \"eleName\":\"%s\", \"appId\":\"%i\", \"uuId\":%i, \"className\":\"%s\", \"title\":\"%s\" ", parentEle->uuId, eleTypeStr, eleName==nullptr ? "" : eleName, appId, newEle->uuId, clas==nullptr ? " " : clas, title==nullptr ? " " : title );
      

      // add the UI specific attributes
      i += format_attributes(p->buf+i, p->bufN-i, 0, std::forward<ARGS>(args)...);

      // terminate the message
      i += toText(p->buf+i, p->bufN-i, "}");

      if( i >= p->bufN )
        return cwLogError(kBufTooSmallRC,"The UI message formatting buffer is too small. (size:%i bytes)", p->bufN);

      //printf("%s\n",p->buf);

      // send the message 
      rc =  _websockSend( p, wsSessId, p->buf );

      uuIdRef = newEle->uuId;
      
      return rc;
    }


    rc_t _createElementsFromChildList( ui_t* p, const object_t* po, unsigned wsSessId, ele_t* parentEle );

    // 
    rc_t _createEleFromRsrsc( ui_t* p, ele_t* parentEle, const char* eleType, const object_t* srcObj, unsigned wsSessId )
    {
      rc_t        rc          = kOkRC;
      object_t*   co          = nullptr;
      ele_t*      ele         = nullptr;
      char*       eleName     = nullptr;
      object_t*   o           = srcObj->duplicate(); // duplicate the rsrc object so that we can modify it.
      const char* divAliasA[] = { "div","row","col","panel",nullptr };  // all these types are div's
      bool        divAliasFl  = false;

      if( !o->is_dict() )
        return cwLogError(kSyntaxErrorRC,"All ui element resource records must be dictionaries.");
      
      // if this object has a 'children' list then unlink it and save it for later
      if((co = o->find("children", kNoRecurseFl | kOptionalFl)) != nullptr )
      {
        co = co->parent;
        co->unlink();
      }

      // is this element a 'div' alias?
      for(unsigned i=0; divAliasA[i]!=nullptr; ++i)
        if( textCompare(divAliasA[i],eleType) == 0 )
        {
          divAliasFl = true;
          break;
        }
            
      // get the ui ele name
      if((rc = o->get("name",eleName, cw::kNoRecurseFl | cw::kOptionalFl)) != kOkRC )
      {
        // div's and titles don't need a 'name'
        if( rc == kLabelNotFoundRC && (divAliasFl || textCompare(eleType,"title")==0) )
          rc = kOkRC;
        else
        {
          rc = cwLogError(rc,"The UI element name could not be read.");
        
          goto errLabel;
        }
      }

      // get or create the ele record to associate with this ele
      if((ele = _findOrCreateEle( p, parentEle, eleName )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The local element '%s' could not be created.",cwStringNullGuard(eleName));
        goto errLabel;
      }

      // insert the UuId node
      if( o->insertPair("uuId",ele->uuId) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The 'uuid' node insertion failed on UI element '%s'.",cwStringNullGuard(eleName));
        goto errLabel;
      }

      // Insert the appId node
      if( ele->appId != kInvalidId )
      {
        if( o->insertPair("appId",ele->appId) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The 'appId' node insertion failed on UI element '%s'.",cwStringNullGuard(eleName));
          goto errLabel;
        }
      } 

      // Insert the parentId node
      if( o->insertPair("parentUuId",parentEle->uuId) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The 'parentUuId' node insertion failed on UI element '%s'.",cwStringNullGuard(eleName));
        goto errLabel;
      }

      // Insert the 'op':'create' operation node
      if( o->insertPair("op","create") == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The 'op' node insertion failed on UI element '%s'.",cwStringNullGuard(eleName));
        goto errLabel;
      }

      // Insert the 'type':'<ele_type>' node
      if( o->insertPair("type",eleType) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The 'eleType' node insertion failed on UI element '%s'.",cwStringNullGuard(eleName));
        goto errLabel;
      }

      // Convert the object to a JSON string
      if( o->to_string(p->buf,p->bufN) >= p->bufN )
      {
        rc = cwLogError(kOpFailRC,"Conversion to JSON string failed on UI element '%s'.",cwStringNullGuard(eleName));
        goto errLabel;
      }

      // Send the JSON msg to the browser
      if((rc =  _websockSend( p, wsSessId, p->buf )) != kOkRC )
      {
        rc = cwLogError(rc,"The creation request send failed on UI element '%s'.", cwStringNullGuard(eleName));
        goto errLabel;
      }

      
      // if this element has a list of children then create them here
      if( co != nullptr || divAliasFl )
      {
        // Note that 'div's need not have an explicit 'children' node.
        // Any child node of a 'div' with a dictionary as a value is a child control.
        const object_t* childL = co!=nullptr ? co->pair_value() : srcObj;
        rc = _createElementsFromChildList(p, childL, wsSessId, ele );
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
    rc_t _createElementsFromChildList( ui_t* p, const object_t* po, unsigned wsSessId, ele_t* parentEle )
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
          if((rc = _createEleFromRsrsc(p, parentEle, o->pair_label(), o->pair_value(), wsSessId )) != kOkRC )
            return rc;
        
      }
      
      return rc;
    }

    rc_t _createFromObj( ui_t* p, const object_t* o, unsigned wsSessId, unsigned parentUuId )
    {
      rc_t        rc        = kOkRC;
      const object_t*   po        = nullptr;
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
      //po = po->parent;
      
      //po->unlink();
      
      rc =  _createElementsFromChildList( p, o, wsSessId, parentEle );

      //po->free();

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
      rc_t _sendValue( ui_t* p, unsigned wsSessId, unsigned uuId, const char* vFmt, const T& value, int vbufN=32 )
    {
      rc_t rc = kOkRC;
      
      if( p->sendCbFunc != nullptr )
      {
        const char* mFmt = "{ \"op\":\"value\", \"uuId\":%i, \"value\":%s }";
        const int   mbufN = 128;
        char        vbuf[vbufN];
        char        mbuf[mbufN];
    
        if( snprintf(vbuf,vbufN,vFmt,value) >= vbufN-1 )
          return cwLogError(kBufTooSmallRC,"The value msg buffer is too small.");

        if( snprintf(mbuf,mbufN,mFmt,uuId,vbuf) >= mbufN-1 )
          return cwLogError(kBufTooSmallRC,"The msg buffer is too small.");

        p->sendCbFunc(p->sendCbArg,wsSessId,mbuf,strlen(mbuf));
      }

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
  if((ele = _createEle(p, nullptr, kRootAppId, "uiDivId" )) == nullptr || ele->uuId != kRootUuId )
  {
    cwLogError(kOpFailRC,"The UI root element creation failed.");
    goto errLabel;
  }

  // register any supplied appId maps
  if((rc = _registerAppIdMap(p,appIdMapA,appIdMapN)) != kOkRC )
    goto errLabel;
  
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
  
  p->uiCbFunc( p->uiCbArg, wsSessId, kConnectOpId, kInvalidId, kInvalidId, kInvalidId, nullptr );
  return kOkRC;
}

cw::rc_t cw::ui::onDisconnect( handle_t h, unsigned wsSessId )
{
  ui_t* p = _handleToPtr(h);

  p->uiCbFunc( p->uiCbArg, wsSessId, kDisconnectOpId, kInvalidId, kInvalidId, kInvalidId, nullptr );

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
      // Pass on the 'init' msg to the app.
      p->uiCbFunc( p->uiCbArg, wsSessId, opId, kInvalidId, kInvalidId, kInvalidId, nullptr );
      break;
                
    case kValueOpId:
      if((ele = _parse_value_msg(p, value, (const char*)msg )) == nullptr )
        cwLogError(kOpFailRC,"UI Value message parse failed.");
      else
      {
        unsigned parentEleAppId = ele->parent == nullptr ? kInvalidId : ele->parent->appId;

        p->uiCbFunc( p->uiCbArg, wsSessId, opId, parentEleAppId, ele->uuId, ele->appId, &value );
                  
      }
      break;

    case kEchoOpId:
      if((ele = _parse_echo_msg(p,(const char*)msg)) == nullptr )
        cwLogError(kOpFailRC,"UI Echo message parse failed.");
      else
      {
        unsigned parentEleAppId = ele->parent == nullptr ? kInvalidId : ele->parent->appId;

        p->uiCbFunc( p->uiCbArg, wsSessId, opId, parentEleAppId, ele->uuId, ele->appId, nullptr );               
      }
      break;

    case kIdleOpId:
      p->uiCbFunc( p->uiCbArg, kInvalidId, opId, kInvalidId, kInvalidId, kInvalidId, nullptr );                     
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
    if(((p->eleA[i]->parent==nullptr && parentUuId==kRootUuId) ||
        (p->eleA[i]->parent!=nullptr && p->eleA[i]->parent->uuId==parentUuId))
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

unsigned  cw::ui::findElementAppId(  handle_t h, unsigned uuId )
{
  ui_t* p = _handleToPtr(h);
  
  ele_t* ele = _uuIdToEle( p, uuId );

  return ele==nullptr ? kInvalidId : ele->uuId;
}


cw::rc_t cw::ui::createFromObject( handle_t  h, const object_t* o,  unsigned wsSessId,  unsigned parentUuId,  const char* eleName )
{
  ui_t*     p  = _handleToPtr(h);
  rc_t      rc = kOkRC;

  if( eleName != nullptr )
    if((o = o->find(eleName)) == nullptr )
    {
      rc = cwLogError(kSyntaxErrorRC,"Unable to locate the '%s' sub-configuration.",cwStringNullGuard(eleName));
      goto errLabel;
    }
  
  
 if((rc = _createFromObj( p, o, wsSessId, parentUuId )) != kOkRC )
    goto errLabel;

 errLabel:
  if(rc != kOkRC )
    rc = cwLogError(rc,"UI instantiation from object failed.");

 return rc;
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
{ return _createOneEle( _handleToPtr(h), uuIdRef, "title", wsSessId, parentUuId, eleName, appId, clas, title ); }

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



cw::rc_t cw::ui::registerAppIdMap(  handle_t h, const appIdMap_t* map, unsigned mapN )
{
  return _registerAppIdMap( _handleToPtr(h), map, mapN);
}

cw::rc_t cw::ui::sendValueBool( handle_t h, unsigned wsSessId, unsigned uuId, bool value )
{
  ui_t* p  = _handleToPtr(h);
  return _sendValue<int>(p,wsSessId,uuId,"%i",value?1:0);
}

cw::rc_t cw::ui::sendValueInt( handle_t h, unsigned wsSessId, unsigned uuId, int value )
{
  ui_t* p  = _handleToPtr(h);
  return _sendValue<int>(p,wsSessId,uuId,"%i",value);  
}

cw::rc_t cw::ui::sendValueUInt( handle_t h, unsigned wsSessId, unsigned uuId, unsigned value )
{
  ui_t* p  = _handleToPtr(h);
  return _sendValue<unsigned>(p,wsSessId,uuId,"%i",value);
}

cw::rc_t cw::ui::sendValueFloat( handle_t h, unsigned wsSessId, unsigned uuId, float value )
{
  ui_t* p  = _handleToPtr(h);
  return _sendValue<float>(p,wsSessId,uuId,"%f",value);
  
}

cw::rc_t cw::ui::sendValueDouble( handle_t h, unsigned wsSessId, unsigned uuId, double value )
{
  ui_t* p  = _handleToPtr(h);
  return _sendValue<double>(p,wsSessId,uuId,"%f",value);
}

cw::rc_t cw::ui::sendValueString( handle_t h, unsigned wsSessId, unsigned uuId, const char* value )
{
  ui_t* p  = _handleToPtr(h);
  // +10 allows for extra value buffer space for double quotes and slashed
  return _sendValue<const char*>(p,wsSessId,uuId,"\"%s\"",value,strlen(value)+10);
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
        return websock::send( p->wsH, kUiProtocolId, wsSessId, msg, msgByteN );
      }
      
    }
  }
}

cw::rc_t cw::ui::ws::create(  handle_t& h,
  unsigned          port,
  const char*       physRootDir,
  void*             cbArg,
  uiCallback_t      uiCbFunc,
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
  if((rc = ui::create(p->uiH, _webSockSend, p, uiCbFunc, cbArg, appIdMapA, appIdMapN, fmtBufByteN )) != kOkRC )
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
      
cw::rc_t cw::ui::ws::exec( handle_t h, unsigned timeOutMs )
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
        unsigned         wsTimeOutMs;
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
        
        if((rc = ws::exec(p->wsUiH,p->wsTimeOutMs)) != kOkRC )
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
  
  if((rc = ws::create(p->wsUiH, port, physRootDir, cbArg, uiCbFunc, appIdMapA, appIdMapN, wsCbFunc, dfltPageFn, websockTimeOutMs, rcvBufByteN, xmtBufByteN, fmtBufByteN )) != kOkRC )
  {
    cwLogError(rc,"The websock UI creationg failed.");
    goto errLabel;
  }

  if((rc = thread::create( p->thH, _threadCallback, p )) != kOkRC )
  {
    cwLogError(rc,"The websock UI server thread create failed.");
    goto errLabel;
  }

  p->wsTimeOutMs = websockTimeOutMs;

  h.set(p);
  
 errLabel:
  if( rc != kOkRC )
    _destroy(p);

  return rc;
}

cw::rc_t cw::ui::srv::create( handle_t& h,
  const args_t&     args,
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
    appIdMapA,
    appIdMapN,
    wsCbFunc,
    args.dfltHtmlPageFn,
    args.timeOutMs,
    args.recvBufByteN,
    args.xmitBufByteN,
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
