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
     kRegisterOpId,
     kEndAppIdUpdateOpId,
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

    typedef rc_t (*uiCallback_t)( void* cbArg, unsigned websockSessionId, opId_t opId, unsigned parentAppId, unsigned uuId, unsigned appId, const value_t* value );
    
    rc_t createUi(  handle_t& h,
      unsigned     port,
      uiCallback_t cbFunc,
      void*        cbArg,
      const char*  physRootDir,
      const char*  dfltPageFn       = "index.html",
      unsigned     websockTimeOutMs = 50,
      unsigned     rcvBufByteN      = 1024,
      unsigned     xmtBufByteN      = 1024,
      unsigned     fmtBufByteN      = 4096 );
    
    rc_t destroyUi( handle_t& h );

    rc_t start( handle_t h );
    rc_t stop(  handle_t h );

    unsigned    findElementAppId(  handle_t h, unsigned parentUuId, const char* eleName );
    unsigned    findElementUuId(   handle_t h, unsigned parentUuId, const char* eleName );
    unsigned    findElementUuId(   handle_t h, unsigned parentUuId, unsigned appId );
    const char* findElementName(   handle_t h, unsigned uuId );
    
    // Return the uuid of the first matching 'eleName'.
    unsigned    findElementUuId( handle_t h, const char* eleName );

    rc_t createFromFile( handle_t h, const char* fn,    unsigned wsSessId, unsigned parentUuId=kInvalidId);
    rc_t createFromText( handle_t h, const char* text,  unsigned wsSessId, unsigned parentUuId=kInvalidId);
    rc_t createDiv(      handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createTitle(    handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createButton(   handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createCheck(    handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, bool value );
    rc_t createSelect(   handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createOption(   handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );
    rc_t createString(   handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, const char* value );
    rc_t createNumber(   handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double value, double minValue, double maxValue, double stepValue, unsigned decPl );
    rc_t createProgress( handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title, double value, double minValue, double maxValue );
    rc_t createText(     handle_t h, unsigned& uuIdRef, unsigned wsSessId, unsigned parentUuId, const char* eleName, unsigned appId, const char* clas, const char* title );

    typedef struct appIdMap_str
    {
      unsigned    parentAppId;
      unsigned    appId;
      const char* eleName;   
    } appIdMap_t;

    // Register parent/child/name app id's 
    rc_t registerAppIds(  handle_t h, const appIdMap_t* map, unsigned mapN );

  }
}


#endif
