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
     kConnectOpId,   // A new remote user interface was connected
     kInitOpId,      // A remote user interface instance was created and is available. It needs to be updated with the current state of the UI from the server.
     kValueOpId,     // The value of a remote user interface control changed. Send this value to the application engine.
     kClickOpId,     // A element on a remote user interface was clicked.
     kSelectOpId,    // An element on a remote user interface was is 'selected' or 'deselected'.
     kEchoOpId,      // A remote user interface is requesting an application engine value. The the current value of a ui element must be sent to the remote UI.
     kIdleOpId,      // The application (UI server) is idle and waiting for the next event from a remote UI.
     kDisconnectOpId // A reemot user interface was disconnected. 
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
