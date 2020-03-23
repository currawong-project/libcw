#ifndef cwMidiDecls_H
#define cwMidiDecls_H

namespace cw
{
  namespace midi
  {
    typedef struct  msg_str
    {
      //unsigned     deltaUs; // time since last MIDI msg in microseconds
      time::spec_t timeStamp;
      uint8_t status;  // midi status byte
      uint8_t d0;      // midi data byte 0
      uint8_t d1;      // midi data byte 1
      uint8_t pad;
    } msg_t;

    typedef struct packet_str
    {
      void*         cbDataPtr; // application supplied reference value from mdParserCreate()
      unsigned      devIdx;    // the device the msg originated from
      unsigned      portIdx;   // the port index on the source device
      msg_t*        msgArray;  // pointer to an array of 'msgCnt' mdMsg records or NULL if sysExMsg is non-NULL
      uint8_t*       sysExMsg;  // pointer to a sys-ex msg or NULL if msgArray is non-NULL (see note below)
      unsigned      msgCnt;    // count of mdMsg records or sys-ex bytes
    } packet_t;
  }
}
#endif
