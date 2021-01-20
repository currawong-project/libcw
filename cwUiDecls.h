#ifndef cwUiDecls_H
#define cwUiDecls_H

namespace cw
{
  namespace ui
  {
    enum
    {
     kHttpProtocolId = 1,
     kUiProtocolId   = 2
    };
    
    typedef enum
    {
     kInvalidOpId,
     kConnectOpId,   // A new user interface instance was connected
     kInitOpId,      // A user interface instance was created and is available (new ui elements can now be added)
     kValueOpId,     // Used by the user interface instance to send a value of a ui element to the application.
     kEchoOpId,      // Used by the user interface instance to request the current value of a ui element from the application.
     kIdleOpId,      // The application is idle and waiting for the next event from the ui instance.
     kDisconnectOpId // A user interface instance was disconnected
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

    typedef struct appIdMap_str
    {
      unsigned    parentAppId;
      unsigned    appId;
      const char* eleName;   
    } appIdMap_t;
    
  }
}

#endif
