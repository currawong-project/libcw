#ifndef cwMidiDecls_H
#define cwMidiDecls_H

namespace cw
{
  namespace midi
  {
    typedef struct  msg_str
    {
      time::spec_t timeStamp;
      uint8_t status;  // midi status byte
      uint8_t d0;      // midi data byte 0
      uint8_t d1;      // midi data byte 1
      uint8_t pad;
    } msg_t;

    typedef struct packet_str
    {
      void*         cbDataPtr; // Application supplied reference value from mdParserCreate()
      unsigned      devIdx;    // The device the msg originated from
      unsigned      portIdx;   // The port index on the source device
      msg_t*        msgArray;  // Pointer to an array of 'msgCnt' mdMsg records or NULL if sysExMsg is non-NULL
      uint8_t*      sysExMsg;  // Pointer to a sys-ex msg or NULL if msgArray is non-NULL (see note below)
      unsigned      msgCnt;    // Count of mdMsg records or sys-ex bytes
    } packet_t;
  }
}
#endif
