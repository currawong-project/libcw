#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"

#include "cwThread.h"

#include "cwTcpSocket.h"
#include "cwTcpSocketSrv.h"
#include "cwMdns.h"
#include "cwTime.h"

namespace cw
{
  namespace net
  {
    namespace mdns
    {
      typedef struct msg_str
      {
        uint16_t transactionId;
        uint16_t flags;
        uint16_t questionN;
        uint16_t answerN;
        uint16_t nameServerN;
        uint16_t additionalN;
      } msg_t;

      typedef struct question_str
      {
        char*                name;
        uint16_t             type;
        uint16_t             clss;
        struct question_str* link;
      } question_t;

      typedef struct srv_rsrc_str
      {
        uint16_t priority;
        uint16_t weight;
        uint16_t port;
        char*    target;
      } srv_rsrc_t;

      typedef struct rsrc_str
      {
        char*    name;
        uint16_t type;
        uint16_t clss;
        uint32_t ttl;
        uint16_t dataByteN;
        union
        {
          char*      text;
          srv_rsrc_t srv;
          uint32_t   addr;
        };
        struct rsrc_str* link;
      } rsrc_t;

      
      typedef struct mdns_str
      {
        rsrc_t* rsrcL;
      } mdns_t;

      typedef struct mdns_app_str
      {
        srv::handle_t mdnsH;
        socket::handle_t tcpH;
        thread::handle_t tcpThreadH;
        unsigned      recvBufByteN;
        unsigned      cbN;
        mdns_t        mdns;
        unsigned      protocolState;
        time::spec_t  t0;
        unsigned      txtXmtN;
      } mdns_app_t;


      void errorv( mdns_t* p, const char* fmt, va_list vl )
      {
        printf("Error: ");
        vprintf(fmt,vl);
      }
      
      void logv( mdns_t* p, const char* fmt, va_list vl )
      {
        vprintf(fmt,vl);
        fflush(stdout);
      }
      
      void error( mdns_t* p, const char* fmt, ... )
      {
        va_list vl;
        va_start(vl,fmt);
        errorv(p,fmt,vl);
        va_end(vl);
      }
      
      void log( mdns_t* p, const char* fmt, ... )
      {
        va_list vl;
        va_start(vl,fmt);
        logv(p,fmt,vl);
        va_end(vl);
      }

      enum
      {
       kInvalidRecdTId,
       kQuestionRecdTId,
       kAnswerRecdTId,
       kNameServerRecdTId,
       kAdditionalRecdTId
      };

      enum
      {
       kA_DnsTId     = 1,
       kPTR_DnsTId   = 12,
       kTXT_DnsTId   = 16,
       kAAAA_DnsTId  = 28,
       kSRV_DnsTId   = 33,
       kOPT_DnsTId   = 41,
       kNSEC_DnsTId  = 47,
       kANY_DnsTId   = 255
       // REMEMBER: Add new type id's to dnsTypeIdToString()
      };

      enum
      {
       kHdrBodyByteN      = 12,
       kQuestionBodyByteN = 4,
       kRsrcBodyByteN     = 10,
       kABodyByteN        = 4,
       kSrvBodyByteN      = 6,
       kOptBodyByteN      = 4,
      };

      enum
      {
       kReplyHdrFl         = 0x8000,
       kAuthoritativeHdrFl = 0x0400,
       kFlushClassFl       = 0x8000,
       kInClassFl          = 0x0001
      };

      const char* dnsTypeIdToString( uint16_t id )
      {
        switch( id )
        {
          case kA_DnsTId:    return "A";            
          case kPTR_DnsTId:  return "PTR";
          case kTXT_DnsTId:  return "TXT";
          case kAAAA_DnsTId: return "AAAA";
          case kSRV_DnsTId:  return "SRV";
          case kOPT_DnsTId:  return "OPT";
          case kNSEC_DnsTId: return "NSEC";
          case kANY_DnsTId:  return "ANY";  
        }
        return "<unknown DNS type>";
      }

      unsigned calc_msg_buf_byte_count(
        unsigned    recdTId,
        const char* name,
        unsigned    dnsTId,
        unsigned    clss,
        unsigned    ttl,
        unsigned    numb0,
        const char* text,
        unsigned    nextRecdTId,
        va_list     vl )
      {
        unsigned msgByteN = kHdrBodyByteN; // msg header bytes
        unsigned recdN    = 0;
        
        while( true )
        {
          // unsigned n0 = msgByteN;

          // record name bytes
          if( name[0] == ((char)0xc0) )
            msgByteN += 2;
          else
            msgByteN += strlen(name) + 2;  // add 1 for initial segment length and 1 for terminating zero

          if( recdTId == kQuestionRecdTId )
          {
            msgByteN += kQuestionBodyByteN;
          }
          else
          {
            // resource record bytes
            msgByteN += kRsrcBodyByteN;

            switch( dnsTId )
            {
              case kA_DnsTId:
                msgByteN += kABodyByteN;
                break;
                
              case kPTR_DnsTId:
                if( numb0 == 0 )
                {
                  if( text[0] == ((char)0xc0) )
                    msgByteN += 2;
                  else
                    msgByteN += strlen(text) + 1;
                }
                else
                {
                  msgByteN += 2;
                }
                break;
                
              case kTXT_DnsTId:
                if( text[0] == ((char)0xc0) )
                  msgByteN += 2;
                else
                  msgByteN += strlen(text) + 2;
                break;
                
              case kSRV_DnsTId:
                msgByteN += kSrvBodyByteN;
                if( text[0] == ((char)0xc0) )
                  msgByteN += 2;
                else
                  msgByteN += strlen(text) + 1;
                break;
              default:
                assert(0);
            }

          }

          //printf("SIZE: %i %i\n", dnsTId, msgByteN-n0 );
          
          recdTId = recdN==0 ? nextRecdTId : va_arg(vl,unsigned);

          if( recdTId == kInvalidRecdTId )
            break;
          
          name   = va_arg(vl,const char*);
          dnsTId = va_arg(vl,unsigned);
          clss   = va_arg(vl,unsigned);
          ttl    = va_arg(vl,unsigned); // not used
          numb0  = va_arg(vl,unsigned); // not used
          text   = va_arg(vl,const char*);

          recdN  += 1;
        }
        
        return msgByteN;
      }

      char* format_name( char* b, unsigned bN, const char* name, bool zeroTermFl=true, const char sepChar='.' )
      {
        unsigned n = 0;
        unsigned j = 0;

        if( name[0] == ((char)0xc0) )
        {
          assert( bN >= 2 );
          b[0] = name[0];
          b[1] = name[1];
          return b + 1 + 1;
        }

        // for each input character
        for(unsigned i=0; true; ++i)
        {
          // if this char is a '.' or a '\0' then it is the end of a name segment
          if( name[i] == sepChar || name[i]==0 )
          {
            assert( j < bN);
            b[j] = n;    // write the length of the previous segment 
            j    = i+1;  // advance j to the length cell of the next segments
            n    = 0;

            // if this char is a '\0' then we are at the end of the input
            if( name[i] == 0 )
              break;
          }
          else
          {
            n += 1;            // advance the segment length counter
            assert( j+n < bN );
            b[j+n] = name[i];  // write  the current char to the output
          }          
        }

        // terminate the output string
        if( zeroTermFl )
        {
          assert( j < bN );
          b[j] = 0;
          j += 1;
        }
        
        return b + j; // return a pointer just past the end of the output string
      }

      char* format_question( char* b, unsigned bN, const char* name, unsigned dnsTypeId )
      {
        b = format_name(b,bN,name);
        uint16_t* u = (uint16_t*)b;
        u[0] = htons(dnsTypeId);
        u[1] = htons(kInClassFl);
        return b + kQuestionBodyByteN;
      }

      char* format_rsrc( char* b, unsigned bN, const char* name, unsigned typeId, unsigned clss, unsigned ttl, unsigned dataByteN )
      {
        // u[0] u[1]  u[2-3] u[4]
        // type class TTL    dlen
        char* b1    = format_name(b,bN,name);
        uint16_t* u = (uint16_t*)b1;
        uint32_t* l = (uint32_t*)(u + 2);
        u[0]        = htons(typeId);
        u[1]        = htons(clss);
        l[0]        = htonl(ttl);
        u[4]        = htons(dataByteN);
        return b1 + kRsrcBodyByteN;  
      }
      
      char* format_A_rsrc(   char* b, unsigned bN, const char* name, unsigned clss, unsigned ttl, unsigned addr )
      {
        char*     b1 = format_rsrc( b, bN, name, kA_DnsTId, clss, ttl, kABodyByteN );
        uint32_t* l  = (uint32_t*)b1;
        l[0]         = addr; //htonl(addr);
                  
        return b1 + kABodyByteN;
      }
      
      char* format_PTR_rsrc( char* b, unsigned bN, const char* name, unsigned clss, unsigned ttl, const char* text, unsigned offset=0 )
      {
        // u[0] u[1]  u[2-3] u[4] u[5 ... ]
        // type class TTL    dlen text

        unsigned dataByteN = strlen(text)+1;

        if( offset != 0 || text[0] == ((char)0xc0) )
          dataByteN = 2;
        
        char* b1 = format_rsrc( b, bN, name, kPTR_DnsTId, clss, ttl, dataByteN );

        assert( b < b1 );
        unsigned n = b1-b;
        
        if( offset == 0 )
        {
          b1 = format_name(b1,bN-n,text,false);
        }
        else
        {
          assert( bN - n >= 2 );
          b1[0] = ((char)0xc0);
          b1[1] = offset;
          b1 += dataByteN;
        }
        return b1;
      }
      
      char* format_TXT_rsrc( char* b, unsigned bN, const char* name, unsigned clss, unsigned ttl, const char* text )
      {
        // u[0] u[1]  u[2-3] u[4] u[5 ... ]
        // type class TTL    dlen text

        unsigned dataByteN = strlen(text)+1;
        char* b1 = format_rsrc( b, bN, name, kTXT_DnsTId, clss, ttl, dataByteN );

        b1 = format_name(b1,bN-(b1-b),text,false,'\n');
        return b1;
      }
      
      char* format_SRV_rsrc( char* b, unsigned bN, const char* name, unsigned clss, unsigned ttl, const char* text, unsigned port, unsigned priority=0, unsigned weight=0 )
      {
        // u[0] u[1]  u[2-3] u[4] u[5]  u[6]  u[7] u[8 ...]
        // type class TTL    dlen pri  weight port target
        unsigned dataByteN = kSrvBodyByteN;

        if( text[0] == ((char)0xc0))
          dataByteN = 2;
        else
          dataByteN += strlen(text)+1;

        
        char*     b1        = format_rsrc( b, bN, name, kSRV_DnsTId, clss, ttl, dataByteN+1 );
        uint16_t* u         = (uint16_t*)b1;
        u[0]                = htons(priority);
        u[1]                = htons(weight);
        u[2]                = htons(port);          
        b1                  = format_name(b1 + kSrvBodyByteN,bN-((b1-b)+kSrvBodyByteN),text,true);
        return b1;
      }

      char* alloc_msgv(
        unsigned*   msgByteNRef, 
        uint16_t    transactionId,
        uint16_t    flags,
        unsigned    recdTId,
        const char* name,
        unsigned    dnsTId,
        unsigned    clss,
        unsigned    ttl,
        unsigned    numb0,
        const char* text,
        unsigned    nextRecdTId,
        va_list     vl0 )
      {
        va_list vl1;
        va_copy(vl1,vl0);
        unsigned byteN = calc_msg_buf_byte_count(recdTId,name,dnsTId,clss,ttl,numb0,text,nextRecdTId,vl1);
        va_end(vl1);

        if( msgByteNRef != nullptr )
          *msgByteNRef = 0;

        char*     buf    = (char*)calloc(1,byteN);
        char*     b0     = buf + kHdrBodyByteN;
        char*     b1     = nullptr;
        int       bN     = byteN;
        uint16_t* u      = (uint16_t*)buf;
        unsigned  recdN  = 0;


        // for each specified record
        while( true )
        {
          // track the type of record
          switch( recdTId )
          {
            case kQuestionRecdTId:   u[2] += 1; break;
            case kAnswerRecdTId:     u[3] += 1; break;
            case kNameServerRecdTId: u[4] += 1; break;
            case kAdditionalRecdTId: u[5] += 1; break;
          }

          // if this is a question record
          if( recdTId == kQuestionRecdTId )
          {
            b1 = format_question( b0, bN, name, dnsTId );
          }
          else
          {
            // select the resource record type to generate
            switch( dnsTId )
            {
              case kA_DnsTId:    b1 = format_A_rsrc(  b0, bN, name, clss, ttl, numb0 ); break;
              case kPTR_DnsTId:  b1 = format_PTR_rsrc(b0, bN, name, clss, ttl, text, numb0 );        break;
              case kTXT_DnsTId:  b1 = format_TXT_rsrc(b0, bN, name, clss, ttl, text );        break;
              case kSRV_DnsTId:  b1 = format_SRV_rsrc(b0, bN, name, clss, ttl, text, numb0 ); break;
              default:
                assert(0);
            }
          }


          //printf("FRMT: %i %li\n", dnsTId, b1-b0 );
          
          bN -= (b1 - b0);   // track the count of remaing bytes in the buffer
          assert(bN >= 0);   // assert the buffer is not already full
          b0 = b1;           // update the current buffer output pointer

          // get the next record type
          recdTId = recdN==0 ? nextRecdTId : va_arg(vl0,unsigned);

          // detect the end of records sentinel
          if( recdTId == kInvalidRecdTId )
            break;

          // get the arguments for the next record
          name   = va_arg(vl0,const char*);
          dnsTId = va_arg(vl0,unsigned);
          clss   = va_arg(vl0,unsigned);
          ttl    = va_arg(vl0,unsigned);
          numb0  = va_arg(vl0,unsigned); // not used
          text   = va_arg(vl0,const char*);

          recdN  += 1;
        }

        // Note that the buffer should be exactly full when all data is written.
        // If this is not true then either the buffer size calculation or
        // the buffer serialization code is incorrect.

        // BUG BUG BUG
        // BUG BUG BUG: see comment in send_txt() for reason that this check is turned off 
        // BUG BUG BUG
        
        //assert( bN == kHdrBodyByteN );  
        
        if( msgByteNRef != nullptr )
          *msgByteNRef = byteN;

        // convert the record counts to the network endianess
        u[0] = htons(transactionId);
        u[1] = htons(flags);        
        u[2] = htons(u[2]);
        u[3] = htons(u[3]);
        u[4] = htons(u[4]);
        u[5] = htons(u[5]);
        
        return buf;
      }
      
      char* alloc_msg(
        unsigned*   msgByteNRef, 
        uint16_t    transactionId,
        uint16_t    flags,
        unsigned    recdTId,
        const char* name,
        unsigned    dnsTId,
        unsigned    clss,
        unsigned    ttl,
        unsigned    numb0,
        const char* text,
        unsigned    nextRecdTId,
        ... )
      {
        va_list vl;
        va_start(vl,nextRecdTId);
        char* b = alloc_msgv( msgByteNRef, transactionId, flags, recdTId, name, dnsTId, clss, ttl, numb0, text, nextRecdTId, vl );
        va_end(vl);
        return b;        
      }

      unsigned calc_ptr_string_byte_count( const char* b, bool dotFl )
      {
        unsigned n = 0;
        unsigned i = 0;
        
        // terminate when a zero or another ptr string is encountered
        while( b[i] != 0 && (b[i] & 0xc0) != 0xc0)
        {
          // TODO: what if this is a 'ptr' ... getting the length of a pointer string may require a recursive function?
          n += b[i] + (dotFl ? 1 : 0);
          i += b[i] + 1;
          dotFl = true;
        }
        return n;
      }
      
      unsigned calc_name_byte_count( mdns_t* p, const char* base, const char* b, unsigned maxSrcByteN, unsigned* strLenRef=nullptr, bool logFl=true )
      {
        if( strLenRef != nullptr )
          *strLenRef = 0;

        // Number of bytes required to represent the uncompressed string
        // (including the segment size bytes but not the terminating zero)
        
        unsigned strByteN = 0;   
        unsigned segN     = 0;   // count of segments the name is formed from
          
        
        unsigned i = 0;
        while( maxSrcByteN ==0 || i < maxSrcByteN )
        {
          // if this a pointer
          if( (b[i] & 0xc0) == 0xc0 )
          {
            // TODO check for going past buffer before add 1 to index
            unsigned short offset = b[i] & 0x3f;
            offset = (offset<<8) + ((unsigned char)b[i+1]);

            strByteN += calc_ptr_string_byte_count( base + offset, i!=0) + 1;

            if( logFl )
              log(p,"%.*s.", base[offset], base + offset + 1 );

            
            i += 2;

            segN += 1;
            break; // ptr terminates the name
          }
          else
          {
            if( b[i] == 0 )
            {
              ++i;
              strByteN += 1;
              break; // zero terminates the name
            }

            if( logFl )
              log(p,"%.*s.", b[i], b+i+1 );
            
            strByteN += b[i] + (i==0 ? 0 : 1);
            i        += b[i] + 1;
            segN     += 1;
            
          }          
        }

        if( maxSrcByteN != 0 and i > maxSrcByteN )
        {
          // we came to the end of a name without a zero or ptr this is a malformed packet
          error(p,"Malformed name.");
          return -1;
        }

        if( strLenRef != nullptr )
          *strLenRef = strByteN;  // add one for terminating zero

        return i; // i is the count of byte used by the name in the packet buffer
      }

      // name[0] must be the length of the segment.
      // Returns the count of bytes written to buf.
      unsigned copy_out_segment( const char* name, char* buf, unsigned bufN,  bool dotFl )
      {
        // get segment length
        unsigned n = name[0];
        unsigned i = 0;
        
        if( dotFl )
        {
          assert( bufN > 0 );
          
          buf[0]  = '.';
          i      += 1;
          bufN   -= 1;
        }
        
        assert( n <= bufN );
        strncpy(buf + i, name+1, n );
       
        return i + n;
      }

      // Return the count of bytes written to buf[].
      unsigned  get_ptr_name( const char* name, char* buf, unsigned bufByteN, bool dotFl )
      {
        unsigned i = 0;
        unsigned N = 0;
        
        // terminate when a zero or another ptr string is encountered
        while( name[i] != 0 && (name[i] & 0xc0) != 0xc0)
        {
          unsigned n = copy_out_segment( name + i, buf, bufByteN, dotFl );
          buf      += n;
          bufByteN -= n;          
          dotFl     = true;
          i        += name[i] + 1;
          N        += n;
        }
        
        return N;
      }
      
      void get_name( const char* b, const char* base, char* buf, unsigned bufByteN )
      {        
        unsigned i = 0;
        
        while( true )
        {
          // if this a pointer
          if( (b[i] & 0xc0) == 0xc0 )
          {
            // TODO check for going past buffer before add 1 to index
            unsigned short offset = b[i] & 0x3f;
            offset = (offset<<8) + ((unsigned char)b[i+1]);

            unsigned n = get_ptr_name( base + offset, buf, bufByteN, i!=0 );
            bufByteN -= n;
            buf      += n;

            i += 2;

            break; // ptr terminates the name
          }
          else
          {
            if( b[i] == 0 )
            {
              break; // zero terminates the name
            }


            unsigned n = copy_out_segment( b + i, buf, bufByteN,  i!=0 );
            bufByteN -= n;
            buf      += n;
            i        += b[i] + 1;
            
          }          
        }

        assert( bufByteN >= 1 );
        buf[0]    = 0;
        bufByteN -= 1;
      }
      

      unsigned resource_recd_byte_count( mdns_t* p, const char* base, const char* b, unsigned bN )
      {
        unsigned nameN = calc_name_byte_count( p, base, b, bN, nullptr, false );
        

        
        uint16_t* u = (uint16_t*)(b + nameN);
        return nameN + 10 + ntohs(u[4]);        
      }

      const char* parse_A_recd( mdns_t* p, const char* base, const char* b, unsigned byteN )
      {
        assert( byteN >= kABodyByteN );
        unsigned addr = ntohl( *(unsigned *)b );
        log(p,"0x%04x inet addr", addr );
        return b + 4;
      }
      
      const char* parse_PTR_recd( mdns_t* p, const char* base, const char* b, unsigned byteN )
      {
        unsigned nameN = calc_name_byte_count( p, base, b, byteN );
        return b + nameN;
      }
      
      const char* parse_TXT_recd( mdns_t* p, const char* base, const char* b, unsigned byteN )
      {
        unsigned i =0;
        while( i<byteN )
        {
          log(p,"%.*s\n",b[i], b + i + 1 );
          i += b[i] + 1;
        }
        return b + i;
      }
      
      const char* parse_SRV_recd( mdns_t* p, const char* base, const char* b, unsigned byteN )
      {
        uint16_t*      u         = (uint16_t*)b;
        uint16_t       priority  = ntohs(u[0]);
        uint16_t       weight    = ntohs(u[1]);
        uint16_t       port      = ntohs(u[2]);
        
        log(p," priority:%i weight:%i port:%i ",priority,weight,port);

        const char* target = b + kSrvBodyByteN;
        unsigned    nameN  = calc_name_byte_count( p, base, target, byteN - kSrvBodyByteN );

        return b + kSrvBodyByteN + nameN + 1;
      }

      const char* parse_OPT_recd( mdns_t* p, const char* base, const char* b, unsigned byteN )
      {
        uint16_t*      u         = (uint16_t*)b;
        uint16_t       code      = ntohs(u[0]);
        uint16_t       optByteN  = ntohs(u[1]);

        log(p," code:0x%02x bN:0x02x ",code,optByteN);
        
        return b + kOptBodyByteN + optByteN;
      }

      const char* parse_NSEC_recd( mdns_t* p, const char* base, const char* b, unsigned byteN )
      {
        // TODO: add parser here
        return nullptr;
      }
      
      const char* parse_resource_recd( mdns_t* p, const char* base, const char* b, unsigned byteN )
      {
        unsigned       nameStrByteN = 0;
        unsigned       nameByteN = calc_name_byte_count( p, base, b, byteN-kRsrcBodyByteN, &nameStrByteN );
        uint16_t*      u         = (uint16_t*)(b + nameByteN);
        uint16_t       type      = ntohs(u[0]);
        uint16_t       clss      = ntohs(u[1]);
        uint32_t       ttl       = ntohl(*((uint32_t*)(u+2)));
        uint16_t       dataN     = ntohs(u[4]);
        const char*    b0        = b;
        
        log(p," nameN:%i type:%s (0x%02x) class:0x%02x ttl:%i dataN:%i ",nameByteN,dnsTypeIdToString(type),type,clss,ttl,dataN );

        char nameBuf[ nameStrByteN ];
        get_name( b, base, nameBuf, nameStrByteN );
        
        b += nameByteN + kRsrcBodyByteN; // advance to the record data
        
        assert( nameByteN + kRsrcBodyByteN + dataN <= byteN);
        
        switch( type )
        {
          case kA_DnsTId:    parse_A_recd(  p,base,b,dataN); break;
          case kPTR_DnsTId:  parse_PTR_recd(p,base,b,dataN); break;
          case kTXT_DnsTId:  parse_TXT_recd(p,base,b,dataN); break;
          case kSRV_DnsTId:  parse_SRV_recd(p,base,b,dataN); break;
          case kOPT_DnsTId:  parse_OPT_recd(p,base,b,dataN); break;
          case kNSEC_DnsTId: parse_NSEC_recd(p,base,b,dataN); break;
          default:
            error(p,"Unhandled DNS type id: %i (0x%x)",type);
        }

        log(p,"\n");

        printf("Extracted name:%s\n",nameBuf);
        
        return b0 + resource_recd_byte_count(p,base,b0,byteN);

      }
      
      const char* parse_question( mdns_t* p, const char* base, const char* b, unsigned byteN )
      {
        log(p,"Question: ");
        unsigned  nameByteN = calc_name_byte_count( p, base, b, byteN-kQuestionBodyByteN );
        uint16_t* u         = (uint16_t*)(b + nameByteN);
        uint16_t  type      = ntohs(u[0]);
        uint16_t  clss      = ntohs(u[1]);
        log(p,"nameN:%i type:%s (0x%02x) class:0x%02x\n", nameByteN,dnsTypeIdToString(type),type,clss );
        
        return b + nameByteN + kQuestionBodyByteN;
      }

      const char* parse_answer( mdns_t* p, const char* base, const char* b, unsigned byteN )
      {
        log(p,"Answer:");
        return parse_resource_recd(p,base,b,byteN);
      }

      const char* parse_name_server( mdns_t* p, const char* base, const char* b, unsigned byteN )
      {
        log(p,"Name Server:");
        return parse_resource_recd(p,base,b,byteN);
      }

      const char* parse_additional( mdns_t* p, const char* base, const char* b, unsigned byteN )
      {
        log(p,"Additional:");
        return parse_resource_recd(p,base,b,byteN);
      }

      const char* parse_msg_segment(
        mdns_t*     p,
        const char* (*parse_func)(mdns_t* p, const char* base, const char* b, unsigned byteN),
        unsigned    msgN,
        const char* base,
        const char* b0,
        int         bN )
      {

        const char* b1;
        for(unsigned i=0; i<msgN; ++i)
        {
          b1 = parse_func(p,base,b0,bN);
          bN -= b1 - b0;
          if( bN < 0 )
          {
            error(p,"Message boundary error.");
            return nullptr;
          }

          b0 = b1;
        }
        
        return b0;
      }
      
      int parse_msg( mdns_t* p, const void* buf, unsigned byteN )
      {
        const char*     base      = static_cast<const char*>(buf);
        const uint16_t* hdr       = static_cast<const uint16_t*>(buf);
        uint16_t        transId   = ntohs(hdr[0]);
        uint16_t        flags     = ntohs(hdr[1]);
        uint16_t        questionN = ntohs(hdr[2]);
        uint16_t        answerN   = ntohs(hdr[3]);
        uint16_t        nameSrvN  = ntohs(hdr[4]);
        uint16_t        addN      = ntohs(hdr[5]);

        log(p,"*** Msg: id:0x%04x flags:0x%04x qN:%i aN:%i nsN:%i addN:%i\n", transId, flags, questionN, answerN, nameSrvN,addN);

        const char* b0 = (const char*)(hdr + 6);
        const char* b1 = nullptr;
        int   bN       = byteN - (b0 - base);

        if((b1 = parse_msg_segment( p, parse_question, questionN, base, b0, bN )) == nullptr )
          goto errLabel;

        bN -= b1 - b0;
        b0 = b1;
        if((b1 = parse_msg_segment( p, parse_answer, answerN, base, b0, bN )) == nullptr )
          goto errLabel;
        
        bN -= b1 - b0;
        b0 = b1;
        if((b1 = parse_msg_segment( p, parse_name_server, nameSrvN, base, b0, bN )) == nullptr )
          goto errLabel;
        
        bN -= b1 - b0;
        b0 = b1;
        if((b1 = parse_msg_segment( p, parse_additional, addN, base, b0, bN )) == nullptr )
          goto errLabel;

      errLabel:
        
        return 0;
      }

      void print_hex( const char* buf, unsigned dataByteCnt )
      {
        unsigned char* data = (unsigned char*)buf;
        const unsigned colN = 8;
        unsigned ci = 0;
        
        for(unsigned i=0; i<dataByteCnt; ++i)
        {
          printf("%02x ", data[i] );

          ++ci;
          if( ci == colN || i+1 == dataByteCnt )
          {
            unsigned n = ci==colN ? colN-1 : ci-1;

            for(unsigned j=0; j<(colN-n)*3; ++j)
              printf(" ");

            
            for(unsigned j=i-n; j<=i; ++j)
              if( 32<= data[j] && data[j] < 127 )
                printf("%c",data[j]);
              else
                printf(".");
            
            printf("\n");
            ci = 0;
          }
        }
      }

      rc_t send_txt( mdns_app_t* p )
      {
        rc_t rc = kOkRC;
        unsigned bufByteN = 0;
        unsigned transId = 0;
        char*    buf      = alloc_msg( &bufByteN, transId, 0x8400,  // 30-23-03-1b-b6-f9
          kAnswerRecdTId,    "MC Mix - 1._EuConProxy._tcp.local",  kTXT_DnsTId,  kFlushClassFl | kInClassFl, 4500,  0,  "lmac=38-C9-86-37-44-E7\nhost=mbp19\nhmac=BE-BD-EA-31-F9-88\ndummy=1",
          kInvalidRecdTId );
                    
        //print_hex(buf,bufByteN);
        //parse_msg( nullptr, buf, bufByteN );

        // BUG BUG BUG BUG
        // BUG BUG BUG BUG: if all was well should not need to subtract 1 from bufByteN - this is related to turning off the final size assert() in alloc_msgv
        // BUG BUG BUG BUG
        
        send( srv::socketHandle(p->mdnsH), buf, bufByteN-1, "224.0.0.251", 5353 );
          
        free(buf);
        
        return rc;
      }
      
      void udpReceiveCallback( void* arg, const void* data, unsigned dataByteCnt, const struct sockaddr_in* fromAddr )
      {
        mdns_app_t* p = static_cast<mdns_app_t*>(arg);
        char addrBuf[ INET_ADDRSTRLEN ];
        socket::addrToString( fromAddr, addrBuf, INET_ADDRSTRLEN );
        p->cbN += 1;

        
        if( false )
        {
          printf("%i bytes:%i %s\n", p->cbN, dataByteCnt, addrBuf );
          print_hex( (const char*)data, dataByteCnt );
          parse_msg(&p->mdns,data,dataByteCnt);
        }

        if( dataByteCnt > 0 )
        {
          uint16_t* u = (uint16_t*)data;
          uint8_t*  b = (uint8_t*)data;
          if( u[1]==0 && u[2] == 1 && b[12] == 0x0a )
          {
            printf("dataByteCnt:%i\n",dataByteCnt);
            if( dataByteCnt == 51 )
              printf("MATCH!\n");
          }
        }
        
        

      }

      rc_t send_response1( mdns_app_t* app, socket::handle_t sockH )
      {
        rc_t rc = kOkRC;

        //              send_response( app, sockH );
 
        // wifi: 98 5A EB 89 BA AA
        // enet: 38 C9 86 37 44 E7
          
        unsigned char buf[] =
          { 0x0b,0x00,0x00,0x00,0x00,0x00,0x00,0x50,0x00,0x02,0x03,0xfc,0x01,0x05,
            0x06,0x00,
            0x38,0xc9,0x86,0x37,0x44,0xe7,
            0x01,0x00,
            0xc0,0xa8,0x00,0x44,
            0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x03,0xff,0x00,0x30,0x08,0x00,0x00,0x80,0x00,0x40,0x01,0x01,0x00,0x00,0x00,0x00,
            0x00,0x00
          };

        unsigned bufByteN = sizeof(buf);
        if((rc = socket::send( sockH, buf, bufByteN )) != kOkRC )
        {
          error(&app->mdns,"Send failed.");
        }

        return rc;
      }

      rc_t send_response2( mdns_app_t* app, socket::handle_t sockH )
      {
        rc_t rc = kOkRC;

        unsigned char buf[] =
          { 0x0d,0x00,0x00,0x00,0x00,0x00,0x00,0x08 };

        unsigned bufByteN = sizeof(buf);
        if((rc = socket::send( sockH, buf, bufByteN )) != kOkRC )
        {
          error(&app->mdns,"Send failed.");
        }

        return rc;
      }
      
      bool tcpReceiveCallback( void* arg )
      {
        mdns_app_t*      app       = static_cast<mdns_app_t*>(arg);
        socket::handle_t sockH     = app->tcpH;
        char             buf[ app->recvBufByteN ];
        unsigned         readByteN = 0;
        rc_t             rc        = kOkRC;

        if( !socket::isConnected(sockH) )
        {
          if((rc = socket::accept( sockH )) == kOkRC )
          {
            log(&app->mdns,"TCP connected.\n");
          }
        }
        else
        {
          if((rc = socket::recieve( sockH, buf, app->recvBufByteN, &readByteN, nullptr )) == kOkRC || rc == kTimeOutRC )
          {

            //printf(".");
            //fflush(stdout);
            
            //printf("msg: %i\n",msg_idx++);
            //print_hex(buf,readByteN);
            if( readByteN > 0 && app->protocolState == 0)
            {
              if( app->protocolState == 0 )
                send_response1( app, sockH );
              app->protocolState += 1;
              printf("PROTO:%i\n",app->protocolState);
            }
            else
            {
              if( app->protocolState == 1 )
              {
                send_response2( app, sockH );
                app->protocolState += 1;
                printf("PROTO:%i\n",app->protocolState);
              }
              else
              {
                if( app->protocolState == 2 )
                {
                  time::get(app->t0);
                  //send_txt(app);
                  app->protocolState+=1;
                  printf("PROTO:%i\n",app->protocolState);
                }
                else
                {
                  if( app->protocolState > 2 && app->txtXmtN < 20 )
                  {
            
                    time::spec_t t1;
                    time::get(t1);
                    if( time::elapsedMs( &app->t0, &t1 ) >= 2500 )
                    {
                      //send_txt(app);
                      app->txtXmtN+=1;
                      printf("TXT:%i\n",app->txtXmtN);
                      app->t0 = t1;
                    }
                  }            
                }
              }
            }
          
            
            
            // if the server disconnects then recvBufByteN 
            if( isConnected( sockH ) )
            {
              //log(&app->mdns,"TCP disconnected.");
            }
            else
            {
              // handle recv'd TCP messages here.
              //send_response( app, sockH );
            }
          }
                  
        }
        return true;
      }

      rc_t sendMsg1( mdns_app_t* p )
      {
        rc_t     rc       = kOkRC;
        unsigned transId  = 0;
        unsigned bufByteN = 0;
        unsigned flags    = 0;
        unsigned ttl      = 120;
        socket::handle_t sockH    = srv::socketHandle(p->mdnsH);
        
        struct sockaddr_in addr;
        
        if((rc = socket::initAddr( sockH, "192.168.0.68", 4325, &addr )) != kOkRC )
        {
          error(&p->mdns,"Get inet address failed.");
          goto errLabel;
        }
        else
        {
          // wifi: 98 5A EB 89 BA AA  "985AEB89BAAA" 98-5A-EB-89-BA-AA
          // enet: 38 C9 86 37 44 E7  "38C9863744E7" 38-C9-86-37-44-E7
          
          char*    buf      = alloc_msg( &bufByteN, transId, flags,
            kQuestionRecdTId,    "68.0.168.192.in-addr.arpa",     kANY_DnsTId,  kInClassFl, 0,   0,     nullptr,
            kQuestionRecdTId,    "Euphonix-MC-38C9863744E7.local", kANY_DnsTId,  kInClassFl, 0,   0,     nullptr,
            kNameServerRecdTId,  "Euphonix-MC-38C9863744E7.local", kA_DnsTId,    kInClassFl, ttl, addr.sin_addr, nullptr,
            kNameServerRecdTId,  "68.0.168.192.in-addr.arpa",      kPTR_DnsTId,  kInClassFl, ttl, 43,    "Euphonix-MC-38C9863744E7.local",
            kInvalidRecdTId );

          //print_hex(buf,bufByteN);
          //parse_msg( nullptr, buf, bufByteN );

          send( sockH, buf, bufByteN, "224.0.0.251", 5353 );
          
          free(buf);
        }
        
      errLabel:
        
        return rc;
      }

      rc_t sendMsg2( mdns_app_t* p )
      {
        rc_t     rc       = kOkRC;
        unsigned transId  = 0;
        unsigned bufByteN = 0;
        unsigned flags    = 0x8400;
        socket::handle_t sockH    = srv::socketHandle(p->mdnsH);
        
        struct sockaddr_in addr;
        
        if((rc = socket::initAddr( sockH, "192.168.0.68", 4325, &addr )) != kOkRC )
        {
          error(&p->mdns,"Get inet address failed.");
          goto errLabel;
        }
        else
        {

          char*    buf0  = alloc_msg( &bufByteN, transId, 0,
            kQuestionRecdTId,    "MC Mix - 1._EuConProxy._tcp.local", kANY_DnsTId, kInClassFl, 0,        0, nullptr,
            kNameServerRecdTId,  "\xc0\x0c",                          kSRV_DnsTId, kInClassFl, 120,  49168, "Euphonix-MC-38C9863744E7.local",
            kNameServerRecdTId,  "\xc0\x0c",                          kTXT_DnsTId, kInClassFl, 4500,     0, "lmac=38-C9-86-37-44-E7\ndummy=0",            
            kInvalidRecdTId );

          //print_hex(buf0,bufByteN);
          send( sockH, buf0, bufByteN, "224.0.0.251", 5353 );
          free(buf0);

          sleepMs(500);
          
          bufByteN = 0;
          
          char*    buf      = alloc_msg( &bufByteN, transId, flags,
            kAnswerRecdTId,    "MC Mix - 1._EuConProxy._tcp.local",  kSRV_DnsTId,  kFlushClassFl | kInClassFl,  120,         49168,  "Euphonix-MC-38C9863744E7.local",
            kAnswerRecdTId,    "\xc0\x3f",                           kA_DnsTId,    kFlushClassFl | kInClassFl,  120, addr.sin_addr,  nullptr,
            kAnswerRecdTId,    "\xc0\x17",                           kPTR_DnsTId,                  kInClassFl, 4500,             0,  "\xc0\x0c",
            kAnswerRecdTId,    "\xc0\x0c",                           kTXT_DnsTId,  kFlushClassFl | kInClassFl, 4500,             0,  "lmac=38-C9-86-37-44-E7\ndummy=1",
            kAnswerRecdTId,     "_services._dns-sd._udp.local",      kPTR_DnsTId,                  kInClassFl, 4500,             0,   "\xc0\x17",
            kInvalidRecdTId );
                    
          //print_hex(buf,bufByteN);
          //parse_msg( nullptr, buf, bufByteN );

          send( sockH, buf, bufByteN, "224.0.0.251", 5353 );
          
          free(buf);
        }
        
      errLabel:
        
        return rc;
      }

      void testAllocMsg( const char* tag )
      {
        unsigned bufByteN = 0;
        unsigned transId  = 0;
        unsigned flags    = 0;
        unsigned ttl      = 120;
        char*    buf      = alloc_msg( &bufByteN, transId, flags,
          kQuestionRecdTId,    "80.0.168.192.in-addr.arpa",      kANY_DnsTId,  kInClassFl, 0,   0,     nullptr,
          kQuestionRecdTId,    "Euphonix-MC-0090D580F4DE.local", kANY_DnsTId,  kInClassFl, 0,   0,     nullptr,
          kNameServerRecdTId,  "Euphonix-MC-0090D580F4DE.local", kA_DnsTId,    kInClassFl, ttl, 49168, nullptr,
          kNameServerRecdTId,  "80.0.168.192.in-addr.arpa",      kPTR_DnsTId,  kInClassFl, ttl, 0,     "in-addr.arpa",
          kInvalidRecdTId );

        print_hex( buf, bufByteN );
        parse_msg( nullptr, buf, bufByteN );
        free(buf);

        buf = alloc_msg( &bufByteN, transId, flags,
          kQuestionRecdTId,  "MC Mix._EuConProxy._tcp.local",  kANY_DnsTId,  kInClassFl, 0,     0,      nullptr,
          kNameServerRecdTId,"Euphonix-MC-0090D580F4DE.local", kSRV_DnsTId,  kInClassFl, 120,   49168,  "local",
          kInvalidRecdTId );

        print_hex( buf, bufByteN );
        parse_msg( nullptr, buf, bufByteN );
        free(buf);

        buf = alloc_msg( &bufByteN, transId, kReplyHdrFl | kAuthoritativeHdrFl,
          kAnswerRecdTId,  "MC Mix - 1._EuConProxy._tcp.local",  kTXT_DnsTId,  kFlushClassFl | kInClassFl, 0, 0, "lmac=00-90-D5-80-F4-DE\ndummy=0",
          kInvalidRecdTId );

        print_hex( buf, bufByteN );
        parse_msg( nullptr, buf, bufByteN );
        free(buf);
        
      }
      
    }
  }
}



cw::rc_t cw::net::mdns::test()
{
  rc_t                 rc;
  socket::portNumber_t mdnsPort       = 5353;
  socket::portNumber_t tcpPort        = 49168;
  unsigned             udpTimeOutMs   = 0;  // if timeOutMs==0 server uses recv_from()
  unsigned             tcpTimeOutMs  = 50;
  const unsigned       sbufN          = 31;
  char                 sbuf[ sbufN+1 ];
  mdns_app_t           app;
 
  app.cbN          = 0;
  app.recvBufByteN = 4096;
  app.protocolState = 0;
  app.txtXmtN = 0;

  // create the mDNS UDP socket server
  if((rc = srv::create(
        app.mdnsH,
        mdnsPort,
        socket::kNonBlockingFl | socket::kReuseAddrFl | socket::kReusePortFl | socket::kMultiCastTtlFl | socket::kMultiCastLoopFl,
        udpReceiveCallback,
        &app,
        app.recvBufByteN,
        udpTimeOutMs,
        NULL,
        socket::kInvalidPortNumber )) != kOkRC )
  {    
    return cwLogError(rc,"mDNS UDP socket create failed.");
  }


  // add the mDNS socket to the multicast group
  if((rc =  join_multicast_group( socketHandle(app.mdnsH), "224.0.0.251" )) != kOkRC )
    goto errLabel;

  // set the TTL for multicast 
  if((rc = set_multicast_time_to_live( socketHandle(app.mdnsH), 255 )) != kOkRC )
      goto errLabel;

  // create the  TCP socket
  if((rc = socket::create(
        app.tcpH,
        tcpPort,
        socket::kTcpFl | socket::kBlockingFl | socket::kStreamFl | socket::kListenFl,
        tcpTimeOutMs,
        NULL,
        socket::kInvalidPortNumber )) != kOkRC )
  {    
    rc = cwLogError(rc,"mDNS TCP socket create failed.");
    goto errLabel;
  }

    // create the TCP listening thread
  if((rc = thread::create( app.tcpThreadH, tcpReceiveCallback, &app )) != kOkRC )
    goto errLabel;

  
  // start the mDNS socket server
  if((rc = srv::start( app.mdnsH )) != kOkRC )
    goto errLabel;

  // start the tcp thread
  if((rc = thread::unpause( app.tcpThreadH )) != kOkRC )
    goto errLabel;
  
  while( true )
  {
    printf("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {
      if( strcmp(sbuf,"msg0\n") == 0 )
      {
        //testAllocMsg(sbuf);
        send_txt(&app);
        break;
      }

      if( strcmp(sbuf,"msg1\n") == 0 )
      {
        sendMsg1( &app );
      }

      if( strcmp(sbuf,"msg2\n") == 0 )
      {
        sendMsg2( &app );
      }
      
      if( strcmp(sbuf,"quit\n") == 0)
        break;
    }
  }

 errLabel:
  // close the mDNS server
  rc_t rc0 = destroy(app.mdnsH);
  rc_t rc1 = thread::destroy(app.tcpThreadH);
  rc_t rc2 = socket::destroy(app.tcpH);

  return rcSelect(rc,rc0,rc1,rc2);  
}
