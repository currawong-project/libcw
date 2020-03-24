#include "config.h"
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef OS_LINUX
#include <arpa/inet.h>
#endif
#ifdef ARDUINO
#include <Ethernet.h>
#include <utility/w5100.h>
#endif

#include "rpt.h"
#include "dns_sd.h"
#include "dns_sd_print.h"
#include "dns_sd_const.h"


int  _print_name( printCallback_t printCbFunc, const unsigned char* s, const unsigned char* buf )
{
  int n = 0;     // track allocated length of the name in this record
  bool incrFl = true; // do not incrmement 'n' if the name switches to a ptr segment
  
  while( *s )
  {
    if( (*s & 0xc0) == 0xc0 )
    {
      if( incrFl )
        n += 2;
      incrFl = false;
      s = buf + s[1];
    }
    else
    {
      for(char i=0; i<s[0]; ++i)
      {
        char x[2];
        x[0] = s[i+1];
        x[1] = 0;
        rpt(printCbFunc,"%s",x);
        if( incrFl )
          ++n;
        
      }
      
      s += s[0]+1;
      n += 1;
      
      if(*s)
      {
        rpt(printCbFunc,".");
      }
    }
  }
  return n;
}

void dns_sd_print( printCallback_t printCbFunc, const void* buf, unsigned bufByteN )
{
  (void)bufByteN;
  
  const uint16_t*      u = (uint16_t*)buf;
  const unsigned char* b = (const unsigned char*)(u+6);

  rpt(printCbFunc,"%s ", ntohs(u[1]) & 0x8000 ? "Response:" : "Question:");
  
  int n = _print_name(printCbFunc,b,(const unsigned char*)buf);

  rpt(printCbFunc," slen:%i ", n);
  
  u = (uint16_t*)(b + n + 1); // advance past name

  switch( ntohs(u[0]) )
  {
    case kA_DnsTId:   rpt(printCbFunc,"A ");
      break;
      
    case kPTR_DnsTId: rpt(printCbFunc,"PTR ");
      break;
      
    case kTXT_DnsTId: rpt(printCbFunc,"TXT ");
      break;
      
    case kSRV_DnsTId: rpt(printCbFunc,"SRV ");
      break;
      
    case kAAAA_DnsTId:rpt(printCbFunc,"AAAA ");
      break;
    case kOPT_DnsTId: rpt(printCbFunc,"OPT ");
      break;
    case kNSEC_DnsTId:rpt(printCbFunc,"NSEC "); break;
    case kANY_DnsTId: rpt(printCbFunc,"ANY "); break; 
   default:
      rpt(printCbFunc,"<unk> 0x%2x",ntohs(u[0])); break;
  }

  if( ntohs(u[1]) & 0x80 )
    rpt(printCbFunc,"flush ");

  rpt(printCbFunc,"\n");
}


