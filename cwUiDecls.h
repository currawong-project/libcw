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
      kInvalidOpId,   // 0
      kConnectOpId,   // 1 A new remote user interface was connected
      kInitOpId,      // 2 A remote user interface instance was created and is available. It needs to be updated with the current state of the UI from the server.
      kValueOpId,     // 3 The value of a remote user interface control changed. Send this value to the application engine.
      kCorruptOpId,   // 4 The value of the remote user interface is invalid or corrupt.
      kClickOpId,     // 5 A element on a remote user interface was clicked.
      kSelectOpId,    // 6 An element on a remote user interface was is 'selected' or 'deselected'.
      kEchoOpId,      // 7 A remote user interface is requesting an application engine value. The the current value of a ui element must be sent to the remote UI.
      kIdleOpId,      // 8 The application (UI server) is idle and waiting for the next event from a remote UI.
      kDisconnectOpId // 9 A remote user interface was disconnected. 
    } opId_t;

    typedef enum
    {
      kInvalidTId,  // 0
      kBoolTId,     // 1
      kIntTId,      // 2
      kUIntTId,     // 3 
      kFloatTId,    // 4
      kDoubleTId,   // 5
      kStringTId    // 6
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
