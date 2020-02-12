#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>
#include "dns_sd_print.h"
#include "dns_sd_const.h"


unsigned _print_name( const char* s, const char* buf )
{
  unsigned n = 0;     // track allocated length of the name in this record
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
        printf("%c",s[i+1]);
        if( incrFl )
          ++n;
        
      }
      
      s += s[0]+1;
      n += 1;
      
      if(*s)
      {
        printf(".");
      }
    }
  }
  return n;
}

void dns_sd_print( const void* buf, unsigned bufByteN )
{
  const uint16_t* u    = (uint16_t*)buf;
  const char*     b    = (const char*)(u+6);
  
  printf("%s ", ntohs(u[1]) & 0x8000 ? "Response " : "Question ");
  
  unsigned n = _print_name(b,(const char*)buf);
  
  printf(" slen:%i ",n);
  
  u = (uint16_t*)(b + n+1); // advance past name

  switch( ntohs(u[0]) )
  {
    case kA_DnsTId:   printf("A ");
      break;
      
    case kPTR_DnsTId: printf("PTR ");
      break;
      
    case kTXT_DnsTId: printf("TXT ");
      break;
      
    case kSRV_DnsTId: printf("SRV ");
      break;
      
    case kAAAA_DnsTId:printf("AAAA ");
      break;
    case kOPT_DnsTId: printf("OPT ");
      break;
    case kNSEC_DnsTId:printf("NSEC "); break;
    case kANY_DnsTId: printf("ANY "); break; 
   default:
      printf("<unk> 0x%2x",ntohs(u[0])); break;
  }

  if( ntohs(u[1]) & 0x80 )
    printf("flush ");

  printf("\n");
}


