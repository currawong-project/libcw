#ifndef cwEuConDecls_H
#define cwEuConDecls_H

namespace cw
{
  namespace eucon
  {

    enum
    {
     kHs0_a_EuProtoId = 0x0a,   // Handshake 0 : Eucon   -> Channel
     kHs1_b_EuProtoId = 0x0b,   // Handshake 1 : Channel -> EuCon
     kHs2_c_EuProtoId = 0x0c,   // Handshake 2 : Eucon   -> Channel
     kHs3_d_EuProtoId = 0x0d,   // Handshake 3 : Channel -> EuCon
     kChMsg_EuProtoId = 0x00,   // 8 byte channel message
     k0x19_EuProtoId  = 0x19,   // variable length messages (display bitmaps?)
     kChHb_EuProtoId  = 0x03,   // Heartbeat   : Channel -> EuCon
     kEuHb_EuProtoId  = 0x04    // Heartbeat   : Eucon   -> Channel
    };
    
    enum
    {
     
     kFPosnEuconId = 0x0000,
     kTouchEuconId = 0x0001,
     kMuteEuconId  = 0x0200,
     kPingEuconId  = 0x0800
    };
  }
}

#endif
