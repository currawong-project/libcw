#ifndef cwUI_H
#define cwUI_H

#include "cwUiDecls.h"

namespace cw
{
  namespace ui
  {
    typedef handle<struct ui_str> handle_t;


    // Callback for application notification.
    // (e.g. the GUI changed the value of a UI element)
    typedef rc_t (*uiCallback_t)(   void* cbArg, unsigned wsSessId, opId_t opId, unsigned parentAppId, unsigned uuId, unsigned appId, const value_t* value );

    // Callback with messages for the GUI as JSON strings
    // { "op":"create", "type":<>, "appId":<>, "parentUuId":<>, "name":<> (type specific fields) }
    // { "op":""value", "uuid":<> "value":<> }
    typedef rc_t (*sendCallback_t)( void* cbArg, unsigned wsSessId, const void* msg, unsigned msgByteN );
      
    rc_t create(
      handle_t&         h,
      sendCallback_t    sendCbFunc,
      void*             sendCbArg,
      uiCallback_t      uiCbFunc,
      void*             uiCbArg,
      const object_t*   uiRsrc      = nullptr,
      const appIdMap_t* appIdMapA   = nullptr,
      unsigned          appIdMapN   = 0,
      unsigned          fmtBufByteN = 4096 );
    
    rc_t destroy( handle_t& h );

    unsigned        sessionIdCount(handle_t h);  // Count of connected remote UI's
    const unsigned* sessionIdArray(handle_t h);  // Array of 'sessionIdCount()' remote UI id's

    // A new remote UI was connected
    rc_t onConnect(    handle_t h, unsigned wsSessId );
    
    // A UI was disconnected
    rc_t onDisconnect( handle_t h, unsigned wsSessId );

    // Receive a msg from a remote UI
    rc_t onReceive(    handle_t h, unsigned wsSessId, const void* msg, unsigned byteN );

    unsigned    findElementAppId(  handle_t h, unsigned parentUuId, const char* eleName );  
    unsigned    findElementUuId(   handle_t h, unsigned parentUuId, const char* eleName );
    unsigned    findElementUuId(   handle_t h, unsigned parentUuId, unsigned appId );
    const char* findElementName(   handle_t h, unsigned uuId );
    unsigned    findElementAppId(  handle_t h, unsigned uuId );
    
    // Return the uuid of the first matching 'eleName' or 'appId'.
    unsigned    findElementUuId( handle_t h, const char* eleName );
    unsigned    findElementUuId( handle_t h, unsigned appId );
    

    // Create multiple UI elements from an object_t representation.
    rc_t createFromObject( handle_t h, const object_t* o, unsigned wsSessId, unsigned parentUuId=kInvalidId, const char* eleName=nullptr);
    rc_t createFromFile(   handle_t h, const char* fn,    unsigned wsSessId, unsigned parentUuId=kInvalidId);
    rc_t createFromText(   handle_t h, const char* text,  unsigned wsSessId, unsigned parentUuId=kInvalidId);

    //
    // Create Sincgle UI elements on the UI instance identified by wsSessId.
    //
    // uuIdRef:    Returns the automatically generated id for this element.
    //             It is unique across all elements associated with this ui::handle.
    //             Note that this id is NOT generated per wsSession, it is only generated
    //             the first time a particular element of this ui::handle is created.
    //
    // wsSessId:   Identifies a particular instance (websock session) of the UI.
    //             Multiple instances of a UI may exist for a single application instance each will have a unique wsSessId.
    //             Set this value to kInvalidId to create this element on all existing websock sessions.  Note that this is
    //             an unusual use since UI elements are usually created when new sessions are connected and therefore
    //             a specific wsSessId is available.
    //
    // parentUuId: uuid of parent element that the new element will be a child of.
    //
    // eleName:    (optional) HTML ele id of the new element.
    //
    // appId:      (optional) Application id. This is the application variable which this UI element represents.
    //             This id is generally the same across all UI instances.
    //
    // clas:       (optional) HTML class of the new element
    //
    // title:      (optional) Visible Text label associated with this element.
    //

    rc_t createDiv(        handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createLabel(      handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createButton(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    
    // Create check: w/o value. The value will be read from the engine via the UI 'echo' event.
    rc_t createCheck(      handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );

    // Create check: w/ value. The value will be sent to the engine as the new value of the associated varaible.
    rc_t createCheck(      handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, bool value );
    
    rc_t createSelect(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createOption(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createStr(        handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createStr(        handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, const char* value );
    rc_t createNumbDisplay(handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, unsigned decPl );    
    rc_t createNumbDisplay(handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, unsigned decPl, double value );    
    rc_t createNumb(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double minValue, double maxValue, double stepValue, unsigned decPl );
    rc_t createNumb(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double minValue, double maxValue, double stepValue, unsigned decPl, double value );
    rc_t createProg(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double minValue, double maxValue );
    rc_t createProg(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double minValue, double maxValue, double value );
    rc_t createLog(        handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createList(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );

    rc_t setNumbRange( handle_t h, unsigned wsSessId, unsigned uuId, double minValue, double maxValue, double stepValue, unsigned decPl, double value );
    rc_t setProgRange( handle_t h, unsigned wsSessId, unsigned uuId, double minValue, double maxValue, double value );
    rc_t setLogLine(   handle_t h, unsigned wsSessId, unsigned uuId, const char* text );
    
    // Register parent/child/name app id's 
    rc_t registerAppIdMap(  handle_t h, const appIdMap_t* map, unsigned mapN );

    // Send a value from the application to the UI via a JSON messages.
    // Set wsSessId to kInvalidId to send to all sessions.
    rc_t sendValueBool(   handle_t h, unsigned wsSessId, unsigned uuId, bool value );
    rc_t sendValueInt(    handle_t h, unsigned wsSessId, unsigned uuId, int value );
    rc_t sendValueUInt(   handle_t h, unsigned wsSessId, unsigned uuId, unsigned value );
    rc_t sendValueFloat(  handle_t h, unsigned wsSessId, unsigned uuId, float value );
    rc_t sendValueDouble( handle_t h, unsigned wsSessId, unsigned uuId, double value );
    rc_t sendValueString( handle_t h, unsigned wsSessId, unsigned uuId, const char* value );

    void report( handle_t h );
    
    
    
    namespace ws
    {
      typedef handle<struct ui_ws_str> handle_t;

      typedef struct args_str
      {
        const char*     physRootDir;
        const char*     dfltPageFn;
        object_t*       uiRsrc;
        unsigned        port;
        unsigned        rcvBufByteN;
        unsigned        xmtBufByteN;
        unsigned        fmtBufByteN;
        unsigned        wsTimeOutMs;        
      } args_t;

      rc_t parseArgs( const object_t& o, args_t& args, const char* object_label=nullptr );
      rc_t releaseArgs( args_t& args );

      rc_t create( handle_t& h,
        const args_t&     args,
        void*             cbArg,
        uiCallback_t      uiCbFunc,
        const object_t*   uiRsrc           = nullptr,
        const appIdMap_t* appIdMapA        = nullptr,
        unsigned          appIdMapN        = 0,
        websock::cbFunc_t wsCbFunc         = nullptr );

      rc_t create(  handle_t& h,
        unsigned          port,
        const char*       physRootDir,
        void*             cbArg,
        uiCallback_t      uiCbFunc,
        const object_t*   uiRsrc           = nullptr,
        const appIdMap_t* appIdMapA        = nullptr,
        unsigned          appIdMapN        = 0,
        websock::cbFunc_t wsCbFunc         = nullptr,        
        const char*       dfltPageFn       = "index.html",        
        unsigned          websockTimeOutMs = 50,
        unsigned          rcvBufByteN      = 1024,
        unsigned          xmtBufByteN      = 1024,
        unsigned          fmtBufByteN      = 4096 );

      rc_t destroy( handle_t& h );

      // This function should be called periodically to send and receive
      // queued messages to and from the websocket.
      rc_t exec( handle_t h );

      // This function executes the internal default websock callback function.
      // It is useful if the user provides a custom websock callback function
      // and wants to fallback to the default websock->ui interaction.
      rc_t  onReceive( handle_t h, unsigned protocolId, unsigned sessionId, websock::msgTypeId_t msg_type, const void* msg, unsigned byteN );
      
      websock::handle_t websockHandle( handle_t h );
      ui::handle_t      uiHandle( handle_t h );
    }

    namespace srv
    {

      typedef handle<struct ui_ws_srv_str> handle_t;

      rc_t create( handle_t& h,
        const ws::args_t&     args,
        void*             cbArg,
        uiCallback_t      uiCbFunc,        
        const appIdMap_t* appIdMapA = nullptr,
        unsigned          appIdMapN = 0,        
        websock::cbFunc_t wsCbFunc  = nullptr );
      
      rc_t create(  handle_t& h,
        unsigned          port,
        const char*       physRootDir,
        void*             cbArg,
        uiCallback_t      uiCbFunc,
        const object_t*   uiRsrc           = nullptr,
        const appIdMap_t* appIdMapA        = nullptr,
        unsigned          appIdMapN        = 0,        
        websock::cbFunc_t wsCbFunc         = nullptr,
        const char*       dfltPageFn       = "index.html",
        unsigned          websockTimeOutMs = 50,
        unsigned          rcvBufByteN      = 1024,
        unsigned          xmtBufByteN      = 1024,
        unsigned          fmtBufByteN      = 4096 );


      rc_t destroy( handle_t& h );

      rc_t start( handle_t h );
      rc_t stop( handle_t h );

      thread::handle_t  threadHandle( handle_t h );
      websock::handle_t websockHandle( handle_t h );
      ui::handle_t      uiHandle( handle_t h );
      
      
    }
      

  }
  
}


#endif
