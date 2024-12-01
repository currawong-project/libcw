//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwMidiDecls_H
#define cwMidiDecls_H

namespace cw
{
  namespace midi
  {
    typedef struct  msg_str
    {
      time::spec_t timeStamp;
      unsigned uid;     // application specified id
      uint8_t  ch;      // midi channel
      uint8_t  status;  // midi status byte (channel has been removed)
      uint8_t  d0;      // midi data byte 0
      uint8_t  d1;      // midi data byte 1
    } msg_t;
    
    typedef struct packet_str
    {
      //void*         cbArg; // Application supplied reference value
      unsigned      devIdx;    // The device the msg originated from
      unsigned      portIdx;   // The port index on the source device
      msg_t*        msgArray;  // Pointer to an array of 'msgCnt' mdMsg records or NULL if sysExMsg is non-NULL
      uint8_t*      sysExMsg;  // Pointer to a sys-ex msg or NULL if msgArray is non-NULL (see note below)
      unsigned      msgCnt;    // Count of mdMsg records or sys-ex bytes
    } packet_t;

    typedef void (*cbFunc_t)( void* cbArg, const packet_t* pktArray, unsigned pktCnt );

    typedef struct ch_msg_str
    {
      time::spec_t timeStamp;
      unsigned     devIdx;      // The device the msg originated from
      unsigned     portIdx;     // The port index on the source device
      unsigned     uid;         // application specified id
      uint8_t      ch;          // midi channel
      uint8_t      status;      // midi status byte (channel has been removed)
      uint8_t      d0;          // midi data byte 0
      uint8_t      d1;          // midi data byte 1
    } ch_msg_t;
  }
}
#endif
