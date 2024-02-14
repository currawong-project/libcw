#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwMidi.h"
#include "cwTextBuf.h"

#include "cwMidiPort.h"

//===================================================================================================
//
//

namespace cw
{
  namespace midi
  {
    namespace parser
    {
      enum 
      { 
       kBufByteCnt = 1024, 

       kExpectStatusStId=0,       // 0
       kExpectDataStId,           // 1
       kExpectStatusOrDataStId,   // 2
       kExpectEOXStId             // 3
      };

      typedef struct cmMpParserCb_str
      {
        cbFunc_t                 cbFunc;
        void*                    cbDataPtr;
        struct cmMpParserCb_str* linkPtr;
      } cbRecd_t;

      typedef struct parser_str
      {
        cbRecd_t* cbChain;
  
        packet_t  pkt;

        unsigned state;         // parser state id
        unsigned errCnt;        // accumlated error count
        uint8_t  status;        // running status
        uint8_t  data0;         // data byte 0
        unsigned dataCnt;       // data byte cnt for current status 
        unsigned dataIdx;       // index (0 or 1) of next data byte
        uint8_t* buf;           // output buffer
        unsigned bufByteCnt;    // output buffer byte cnt
        unsigned bufIdx;        // next output buffer index
        unsigned msgCnt;        // count of channel messages in the buffer
      } parser_t;

      parser_t* _handleToPtr( handle_t h ) 
      {
        return handleToPtr<handle_t,parser_t>(h);
      }

      void _cmMpParserCb( parser_t* p, packet_t* pkt, unsigned pktCnt )
      {
        cbRecd_t* c = p->cbChain;
        for(; c!=NULL; c=c->linkPtr)
        {
          pkt->cbDataPtr = c->cbDataPtr;
          c->cbFunc( pkt, pktCnt );
        }
      }

      void _cmMpTransmitChMsgs( parser_t* p )
      {
        if( p->msgCnt > 0 )
        {
          p->pkt.msgArray = (msg_t*)p->buf;
          p->pkt.msgCnt   = p->msgCnt;
          p->pkt.sysExMsg = NULL;

          //p->cbFunc( &p->pkt, 1 );  
          _cmMpParserCb(p,&p->pkt,1);

          p->bufIdx = 0;
          p->msgCnt = 0;
        }
      }

      void _cmMpTransmitSysEx( parser_t* p )
      {
        p->pkt.msgArray = NULL;
        p->pkt.sysExMsg = p->buf;
        p->pkt.msgCnt   = p->bufIdx;
        //p->cbFunc( &p->pkt, 1 );
        _cmMpParserCb(p,&p->pkt,1);
        p->bufIdx = 0;

      }

      void _cmMpParserStoreChMsg( parser_t* p, const time::spec_t* timeStamp,  uint8_t d )
      {
        // if there is not enough room left in the buffer then transmit
        // the current messages
        if( p->bufByteCnt - p->bufIdx < sizeof(msg_t) )
          _cmMpTransmitChMsgs(p);


        assert( p->bufByteCnt - p->bufIdx >= sizeof(msg_t) );

        // get a pointer to the next msg in the buffer
        msg_t* msgPtr = (msg_t*)(p->buf + p->bufIdx);

        // fill the buffer msg
        msgPtr->timeStamp = *timeStamp;
        msgPtr->status  = p->status;

        switch( p->dataCnt )
        {
          case 0: 
            break;
          case 1:
            msgPtr->d0 = d;
            msgPtr->d1 = 0;
            break;

          case 2:
            msgPtr->d0 = p->data0;
            msgPtr->d1 = d;
            break;

          default:
            assert(0);
        }

        // update the msg count and next buffer 
        ++p->msgCnt;

        p->bufIdx += sizeof(msg_t);

      }
    
      void _report( parser_t* p )
      {
        cwLogInfo("state:%i st:0x%x d0:%i dcnt:%i didx:%i buf[%i]->%i msg:%i err:%i\n",p->state,p->status,p->data0,p->dataCnt,p->dataIdx,p->bufByteCnt,p->bufIdx,p->msgCnt,p->errCnt);
      }

      void _destroy( parser_t* p )
      {
        mem::release(p->buf);

        cbRecd_t* c = p->cbChain;
        while(c != NULL)
        {
          cbRecd_t* nc = c->linkPtr;
          mem::release(c);
          c = nc;
        }

        mem::release(p);

      }

    } // parser
  } // midi
} // cw
  
cw::rc_t cw::midi::parser::create( handle_t& hRef, unsigned devIdx, unsigned portIdx, cbFunc_t cbFunc, void* cbDataPtr, unsigned bufByteCnt )
{
  rc_t      rc = kOkRC;
  parser_t* p  = mem::allocZ<parser_t>( 1 );


  p->pkt.devIdx        = devIdx;
  p->pkt.portIdx       = portIdx;

  //p->cbChain           = cmMemAllocZ( cbRecd_t, 1 );
  //p->cbChain->cbFunc    = cbFunc;
  //p->cbChain->cbDataPtr = cbDataPtr;
  //p->cbChain->linkPtr   = NULL;
  p->cbChain           = NULL;
  p->buf               = mem::allocZ<uint8_t>( bufByteCnt );
  p->bufByteCnt        = bufByteCnt;
  p->bufIdx            = 0;
  p->msgCnt            = 0;
  p->state             = kExpectStatusStId;
  p->dataIdx           = kInvalidIdx;
  p->dataCnt           = kInvalidCnt;
  p->status            = kInvalidStatusMdId;

  hRef.set(p);
        
  if( cbFunc != NULL )
    rc = installCallback(hRef, cbFunc, cbDataPtr );
    

  if( rc != kOkRC )
  {
    _destroy(p);
    hRef.clear();
  }
  

  return rc;
} 
  
  
cw::rc_t   cw::midi::parser::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  parser_t* p = _handleToPtr(hRef);

  _destroy(p);
  
  hRef.clear();

  return rc;
}

unsigned    cw::midi::parser::errorCount( handle_t h )
{
  parser_t* p = _handleToPtr(h);
  if( p == NULL )
    return 0;

  return p->errCnt;
}


void cw::midi::parser::parseMidiData( handle_t h, const time::spec_t* timeStamp, const uint8_t* iBuf, unsigned iByteCnt )
{
  
  parser_t* p = _handleToPtr(h);

  if( p == NULL )
    return;
  
  const uint8_t* ip = iBuf;
  const uint8_t* ep  = iBuf + iByteCnt;

  for(; ip < ep; ++ip )
  {
    // if this byte is a status byte
    if( isStatus(*ip) )
    {
      if( p->state != kExpectStatusStId && p->state != kExpectStatusOrDataStId )
        ++p->errCnt;

      p->status  = *ip;
      p->dataCnt = statusToByteCount(*ip);

      switch( p->status )
      {
        case kSysExMdId: // if this is the start of a sys-ex msg ...
          // ... clear the buffer to prepare from sys-ex data
          _cmMpTransmitChMsgs(p); 

          p->state   = kExpectEOXStId;
          p->dataCnt = kInvalidCnt;
          p->dataIdx = kInvalidIdx;
          p->buf[ p->bufIdx++ ] =  kSysExMdId;
          break;

        case kSysComEoxMdId: // if this is the end of a sys-ex msg
          assert( p->bufIdx < p->bufByteCnt );
          p->buf[p->bufIdx++] = *ip; 
          _cmMpTransmitSysEx(p);
          p->state = kExpectStatusStId;
          break;

        default: // ... otherwise it is a 1,2, or 3 byte msg status
          if( p->dataCnt > 0 )
          {
            p->state   = kExpectDataStId;
            p->dataIdx = 0;
          }
          else
          {
            // this is a status only msg - store it
            _cmMpParserStoreChMsg(p,timeStamp,*ip);

            p->state   = kExpectStatusStId;
            p->dataIdx = kInvalidIdx;
            p->dataCnt = kInvalidCnt;
          }

      }

      continue;
    }

    // at this point the current byte (*ip) is a data byte

    switch(p->state)
    {

      case kExpectStatusOrDataStId:
        assert( p->dataIdx == 0 );
          
      case kExpectDataStId:
        switch( p->dataIdx )
        {
          case 0: // expecting data byte 0 ...
            
            switch( p->dataCnt )
            {
              case 1: // ... of a 1 byte msg - the msg is complete
                _cmMpParserStoreChMsg(p,timeStamp,*ip);
                p->state = kExpectStatusOrDataStId; 
                break;

              case 2: // ... of a 2 byte msg - prepare to recv the second data byte
                p->state   = kExpectDataStId;
                p->dataIdx = 1;
                p->data0   = *ip;
                break;

              default:
                assert(0);
            }
            break;

          case 1: // expecting data byte 1 of a two byte msg
            assert( p->dataCnt == 2 );
            assert( p->state == kExpectDataStId );

            _cmMpParserStoreChMsg(p,timeStamp,*ip);
            p->state   = kExpectStatusOrDataStId;
            p->dataIdx = 0;
            break;

          default:
            assert(0);            
            
        }
        break;

      case kExpectEOXStId:
        assert( p->bufIdx < p->bufByteCnt );

        p->buf[p->bufIdx++] = *ip;

        // if the buffer is full - then transmit it
        if( p->bufIdx == p->bufByteCnt )
          _cmMpTransmitSysEx(p);

        break;

    }

  } // ip loop

  _cmMpTransmitChMsgs(p);
 
}

cw::rc_t  cw::midi::parser::midiTriple(   handle_t h, const time::spec_t* timeStamp, uint8_t status, uint8_t d0, uint8_t d1 )
{
  rc_t rc = kOkRC;
  parser_t* p = _handleToPtr(h);
  uint8_t mb = 0xff; // a midi triple may never have a status of 0xff

  if( d0 == 0xff )
    p->dataCnt = 0;
  else
    if( d1 == 0xff )
      p->dataCnt = 1;
    else
      p->dataCnt = 2;

  p->status  = status;
  switch( p->dataCnt )
  {
    case 0:      
      mb = status;
      break;

    case 1:
      mb = d0;
      break;

    case 2:
      p->data0 = d0;
      mb = d1;
      break;

    default:
      rc = cwLogError(kInvalidArgRC,"An invalid MIDI status byte (0x%x) was encountered by the MIDI data parser.");
      goto errLabel;
      break;
  }

  if( mb != 0xff )
    _cmMpParserStoreChMsg(p,timeStamp,mb);
  
  p->dataCnt = kInvalidCnt;

 errLabel:
  return rc;
}

cw::rc_t  cw::midi::parser::transmit( handle_t h )
{
  parser_t* p = _handleToPtr(h);
  _cmMpTransmitChMsgs(p);
  return kOkRC;
}

cw::rc_t      cw::midi::parser::installCallback( handle_t h, cbFunc_t  cbFunc, void* cbDataPtr )
{
  parser_t*   p        = _handleToPtr(h);
  cbRecd_t* newCbPtr = mem::allocZ<cbRecd_t>( 1 );
  cbRecd_t* c        = p->cbChain;
  
  newCbPtr->cbFunc    = cbFunc;
  newCbPtr->cbDataPtr = cbDataPtr;
  newCbPtr->linkPtr   = NULL;

  if( p->cbChain == NULL )
    p->cbChain = newCbPtr;
  else  
  {
    while( c->linkPtr != NULL )
      c = c->linkPtr;

    c->linkPtr = newCbPtr;
  }
  
  return kOkRC;
}

cw::rc_t      cw::midi::parser::removeCallback(  handle_t h, cbFunc_t cbFunc, void* cbDataPtr )
{
  parser_t*   p = _handleToPtr(h);
  cbRecd_t* c1 = p->cbChain;  // target link
  cbRecd_t* c0 = NULL;        // link pointing to target

  // search for the cbFunc to remove
  for(; c1!=NULL; c1=c1->linkPtr)
  {
    if( c1->cbFunc == cbFunc && c1->cbDataPtr == cbDataPtr)
      break;

    c0 = c1;
  }

  // if the cbFunc was not found
  if( c1 == NULL )
    return cwLogError(kInvalidArgRC,"Unable to locate the callback function %p for removal.",cbFunc);

  // the cbFunc to remove was found

  // if it was the first cb in the chain
  if( c0 == NULL )
    p->cbChain = c1->linkPtr;
  else
    c0->linkPtr = c1->linkPtr;

  mem::release(c1);
  
  return kOkRC;
}

bool cw::midi::parser::hasCallback( handle_t h, cbFunc_t cbFunc, void* cbArg )
{
  parser_t* p = _handleToPtr(h);
  cbRecd_t* c = p->cbChain; // target link

  // search for the cbFunc to remove
  for(; c!=NULL; c=c->linkPtr)
    if( c->cbFunc == cbFunc && c->cbDataPtr == cbArg )
      return true;

  return false;
}

//====================================================================================================
//
//

unsigned    cw::midi::device::nameToIndex(handle_t h, const char* deviceName)
{
  assert(deviceName!=NULL);
  unsigned i;
  unsigned n = count(h);
  for(i=0; i<n; ++i)
    if( strcmp(name(h,i),deviceName)==0)
      return i;
  return kInvalidIdx;
}

unsigned    cw::midi::device::portNameToIndex( handle_t h, unsigned devIdx, unsigned flags, const char* portNameStr )
{
  unsigned i;
  unsigned n = portCount(h,devIdx,flags);
  for(i=0; i<n; ++i)
    if( strcmp(portName(h,devIdx,flags,i),portNameStr)==0)
      return i;

  return kInvalidIdx;
}

//====================================================================================================
//
//

namespace cw
{
  namespace midi
  {
    namespace device
    {

      void testCallback( const packet_t* pktArray, unsigned pktCnt )
      {
        unsigned i,j;
        for(i=0; i<pktCnt; ++i)
        {
          const packet_t* p = pktArray + i;

          for(j=0; j<p->msgCnt; ++j)
            if( p->msgArray != NULL )
            {
              if( ((p->msgArray[j].status & 0xf0) == kNoteOnMdId) && (p->msgArray[j].d1>0))
                printf("%ld %ld 0x%x %i %i\n", p->msgArray[j].timeStamp.tv_sec, p->msgArray[j].timeStamp.tv_nsec, p->msgArray[j].status,p->msgArray[j].d0, p->msgArray[j].d1);
            }
            else
            {
              printf("0x%x ",p->sysExMsg[j]);
            }
        }
      }
    } // device
  } // midi
} // cw
    
cw::rc_t cw::midi::device::test()
{
  rc_t              rc               = kOkRC;
  char              ch;
  unsigned          parserBufByteCnt = 1024;
  textBuf::handle_t tbH;
  handle_t          h;

  // initialie the MIDI system
  if((rc = create(h,testCallback,NULL,parserBufByteCnt,"app")) != kOkRC )
    return rc;

  // create a text buffer to hold the MIDI system report text
  if((rc = textBuf::create(tbH)) != kOkRC )
    goto errLabel;

  // generate and print the MIDI system report
  report(h,tbH);
  cwLogInfo("%s",textBuf::text(tbH));
  
  cwLogInfo("any key to send note-on (<q>=quit)\n");

  while((ch = getchar()) != 'q')
  {
    send(h,2,0,0x90,60,60);
  } 

 errLabel:
  textBuf::destroy(tbH);
  destroy(h);
  return rc;
}
