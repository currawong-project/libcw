#ifndef cwUI_H
#define cwUI_H

#include "cwUiDecls.h"

namespace cw
{
  namespace ui
  {
    typedef handle<struct ui_str> handle_t;

    enum
    {
     kHttpProtocolId = 1,
     kUiProtocolId   = 2
    };
    
    typedef enum
    {
     kInvalidOpId,
     kConnectOpId,
     kInitOpId,
     kValueOpId,
     kEchoOpId,
     kDisconnectOpId
    } opId_t;

    typedef enum
    {
     kInvalidTId,
     kBoolTId,
     kIntTId,
     kUIntTId,
     kFloatTId,
     kDoubleTId,
     kStringTId
    } dtypeId_t;


    enum
    {
     kRootUuId = 0,
     kRootAppId,
    };

    typedef struct
    {
      dtypeId_t tid;
      union
      {
        bool        b;
        int         i;
        unsigned    u;
        float       f;
        double      d;
        const char* s;
      } u;
    } value_t;

    typedef rc_t (*uiCallback_t)(   void* cbArg, unsigned wsSessId, opId_t opId, unsigned parentAppId, unsigned uuId, unsigned appId, const value_t* value );
    typedef rc_t (*sendCallback_t)( void* cbArg, unsigned wsSessId, const void* msg, unsigned msgByteN );
      
    rc_t create(  handle_t& h,
      sendCallback_t sendCbFunc,
      void*          sendCbArg,
      uiCallback_t   uiCbFunc,
      void*          uiCbArg,
      unsigned       fmtBufByteN = 4096 );
    
    rc_t destroy( handle_t& h );

    rc_t onConnect(    handle_t h, unsigned wsSessId );
    rc_t onDisconnect( handle_t h, unsigned wsSessId );
    rc_t onReceive(    handle_t h, unsigned wsSessId, const void* msg, unsigned byteN );

    unsigned    findElementAppId(  handle_t h, unsigned parentUuId, const char* eleName );
    unsigned    findElementUuId(   handle_t h, unsigned parentUuId, const char* eleName );
    unsigned    findElementUuId(   handle_t h, unsigned parentUuId, unsigned appId );
    const char* findElementName(   handle_t h, unsigned uuId );
    
    // Return the uuid of the first matching 'eleName'.
    unsigned    findElementUuId( handle_t h, const char* eleName );

    rc_t createFromObject( handle_t h, const object_t* o, unsigned wsSessId, unsigned parentUuId=kInvalidId, const char* eleName=nullptr);
    rc_t createFromFile(   handle_t h, const char* fn,    unsigned wsSessId, unsigned parentUuId=kInvalidId);
    rc_t createFromText(   handle_t h, const char* text,  unsigned wsSessId, unsigned parentUuId=kInvalidId);
    rc_t createDiv(        handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createTitle(      handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createButton(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createCheck(      handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, bool value );
    rc_t createSelect(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createOption(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createString(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, const char* value );
    rc_t createNumber(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double value, double minValue, double maxValue, double stepValue, unsigned decPl );
    rc_t createProgress(   handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double value, double minValue, double maxValue );
    rc_t createText(       handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );

    typedef struct appIdMap_str
    {
      unsigned    parentAppId;
      unsigned    appId;
      const char* eleName;   
    } appIdMap_t;

    // Register parent/child/name app id's 
    rc_t registerAppIds(  handle_t h, const appIdMap_t* map, unsigned mapN );

    rc_t sendValueBool(   handle_t h, unsigned wsSessId, unsigned uuId, bool value );
    rc_t sendValueInt(    handle_t h, unsigned wsSessId, unsigned uuId, int value );
    rc_t sendValueUInt(   handle_t h, unsigned wsSessId, unsigned uuId, unsigned value );
    rc_t sendValueFloat(  handle_t h, unsigned wsSessId, unsigned uuId, float value );
    rc_t sendValueDouble( handle_t h, unsigned wsSessId, unsigned uuId, double value );
    rc_t sendValueString( handle_t h, unsigned wsSessId, unsigned uuId, const char* value );
    
    namespace ws
    {
      typedef handle<struct ui_ws_str> handle_t;

      rc_t create(  handle_t& h,
        unsigned          port,
        const char*       physRootDir,
        void*             cbArg,
        uiCallback_t      uiCbFunc, 
        websock::cbFunc_t wsCbFunc         = nullptr,
        const char*       dfltPageFn       = "index.html",        
        unsigned          websockTimeOutMs = 50,
        unsigned          rcvBufByteN      = 1024,
        unsigned          xmtBufByteN      = 1024,
        unsigned          fmtBufByteN      = 4096 );

      rc_t destroy( handle_t& h );

      // This function should be called periodically to send and receive
      // queued messages to and from the websocket.
      rc_t exec( handle_t h, unsigned timeOutMs );

      // This function executes the internal default websock callback function.
      // It is useful if the user provides a custom websock callback function
      // and wants to fallback to the default websock->ui interaction.
      rc_t  onReceive( handle_t h, unsigned protocolId, unsigned sessionId, websock::msgTypeId_t msg_type, const void* msg, unsigned byteN );
      
      websock::handle_t websockHandle( handle_t h );
      ui::handle_t      uiHandle( handle_t h );


      
    }

    namespace srv
    {

      typedef struct args_str
      {
        const char* physRootDir;
        const char* dfltHtmlPageFn;
        unsigned    port;
        unsigned    timeOutMs;
        unsigned    recvBufByteN;
        unsigned    xmitBufByteN;
        unsigned    fmtBufByteN;
      } args_t;
      
      typedef handle<struct ui_ws_srv_str> handle_t;

      rc_t create( handle_t& h,
        const args_t&     args,
        void*             cbArg,
        uiCallback_t      uiCbFunc,
        websock::cbFunc_t wsCbFunc = nullptr );
      
      rc_t create(  handle_t& h,
        unsigned          port,
        const char*       physRootDir,
        void*             cbArg,
        uiCallback_t      uiCbFunc,
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
