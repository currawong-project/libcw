//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/time.h>
#include "cwKeyboard.h"
namespace cw
{
  struct termios new_settings;
  struct termios stored_settings;

  void set_keypress(void) 
  {
    struct termios new_settings;

    tcgetattr(0,&stored_settings);
    new_settings             = stored_settings;
    new_settings.c_lflag    &= (~ICANON);
    new_settings.c_lflag    &= (~ECHO);
    new_settings.c_cc[VTIME] = 0;

    //int i;
    //for(i=0; i<NCCS; ++i)
    //  printf("%i ",new_settings.c_cc[i]);
    //printf("\n");
      

    tcgetattr(0,&stored_settings);

    new_settings.c_cc[VMIN] = 1;
    tcsetattr(0,TCSANOW,&new_settings);

  }

  void reset_keypress(void) 
  {
    tcsetattr(0,TCSANOW,&stored_settings);
  }

#define CM_KB_TBL_CNT (10)

  unsigned _cmKbTbl[][CM_KB_TBL_CNT] = 
  {

    //                           alt ctl code
    { 3, 27, 91, 68,   0,   0,   0,  0, 0, kLeftArrowKId },
    { 3, 27, 91, 67,   0,   0,   0,  0, 0, kRightArrowKId },
    { 3, 27, 91, 65,   0,   0,   0,  0, 0, kUpArrowKId },
    { 3, 27, 91, 66,   0,   0,   0,  0, 0, kDownArrowKId },
    { 3, 27, 79, 72,   0,   0,   0,  0, 0, kHomeKId },
    { 3, 27, 79, 70,   0,   0,   0,  0, 0, kEndKId },
    { 4, 27, 91, 53, 126,   0,   0,  0, 0, kPgUpKId },
    { 4, 27, 91, 54, 126,   0,   0,  0, 0, kPgDownKId },
    { 4, 27, 91, 50, 126,   0,   0,  0, 0, kInsertKId },
    { 4, 27, 91, 51, 126,   0,   0,  0, 0, kDeleteKId },
    { 6, 27, 91, 49,  59,  53,  68,  0, 1, kLeftArrowKId },
    { 6, 27, 91, 49,  59,  53,  67,  0, 1, kRightArrowKId },
    { 6, 27, 91, 49,  59,  53,  65,  0, 1, kUpArrowKId },
    { 6, 27, 91, 49,  59,  53,  66,  0, 1, kDownArrowKId },
    { 6, 27, 91, 53,  59,  53, 126,  0, 1, kPgUpKId }, 
    { 6, 27, 91, 54,  59,  53, 126,  0, 1, kPgDownKId },
    { 4, 27, 91, 51,  59,  53, 126,  0, 1, kDeleteKId }, 
    { 0,  0,  0,  0,   0,   0,   0,  0, 0, kInvalidKId }
  };
}


void cw::keyPress( cmKbRecd* p )
{
  const int bufN = 16;
  char      buf[bufN];
  int       n,j, k;
  int       rc;
  char      c;

  if( p != NULL )
  {
    p->code  = kInvalidKId;
    p->ch    = 0;
    p->ctlFl = false;
    p->altFl = false;
  }
  
  set_keypress();

  // block for the first character
  if((rc = read(0, &c, 1 )) == 1)
    buf[0]=c;

  // loop in non-blocking for successive characters
  new_settings.c_cc[VMIN] = 0;
  tcsetattr(0,TCSANOW,&new_settings);

  for(n=1; n<bufN; ++n)
    if(read(0,&c,1) == 1 )
      buf[n] = c;
    else
      break;

  new_settings.c_cc[VMIN] = 1;
  tcsetattr(0,TCSANOW,&new_settings);

  /*
  for(j=0; j<n; ++j)
    printf("{%c (%i)} ",buf[j],buf[j]);
  printf(" :%i\f\n",n);
  fflush(stdout);
  */

  if( p != NULL )
  {
    // translate the keypress
    if( n == 1)
    {
      p->code  = kAsciiKId;
      p->ch    = buf[0];
      p->ctlFl = buf[0] <= 31;    
    }
    else
    {
      for(j=0; _cmKbTbl[j][0] != 0; ++j)
        if( _cmKbTbl[j][0] == (unsigned)n )
        {
          for(k=1; k<=n; ++k)
            if( _cmKbTbl[j][k] != (unsigned)buf[k-1] )
              break;

          // if the key was found
          if( k==n+1 )
          {
            p->code  = _cmKbTbl[j][ CM_KB_TBL_CNT - 1 ];
            p->ctlFl = _cmKbTbl[j][ CM_KB_TBL_CNT - 2 ];
            break;
          }
        }
    }
  }
  reset_keypress();
}


// Based on: // From: http://www.flipcode.com/archives/_kbhit_for_Linux.shtml

int cw::isKeyWaiting()
{
  static const int STDIN       = 0;
  static bool      initialized = false;
  struct timeval   timeout;
  fd_set           rdset;

  if( !initialized )
  {
    // Use termios to turn off line buffering
    struct termios term;
    tcgetattr(STDIN, &term);
    term.c_lflag &= ~ICANON;
    tcsetattr(STDIN, TCSANOW, &term);
    setbuf(stdin, NULL);
    initialized = true;
  }

  if(0)
  {

    FD_ZERO(&rdset);
    FD_SET(STDIN, &rdset);
    timeout.tv_sec  = 0;
    timeout.tv_usec = 0;

    // time out immediately if STDIN is not ready.
    return select(STDIN + 1, &rdset, NULL, NULL, &timeout);
  }
  else
  {
    int bytesWaiting;
    ioctl(STDIN, FIONREAD, &bytesWaiting);
    return bytesWaiting;
  }

}


void cw::kbTest1()
{
  set_keypress();
  
  int c = 0;
  int r;
  printf("'q' to quit\n");
  while( c != 'q' )
  {

    printf("0>"); fflush(stdout);
    r = read(0, &c, 1 );
    printf("0: %c (%i)\r\n",(char)c,c);


    new_settings.c_cc[VMIN] = 0;
    tcsetattr(0,TCSANOW,&new_settings);

    if( r == 1 && c == 27 )
    {
      r = read(0, &c, 1 );
      printf("1: %c (%i)\n",(char)c,c);

      if( r == 1 && c == '[' )
      {
        r = read(0, &c, 1 );
        printf("2: %c (%i)\n",(char)c,c);

      }

    }

    new_settings.c_cc[VMIN] = 1;
    tcsetattr(0,TCSANOW,&new_settings);

  }

  reset_keypress();
}

void cw::kbTest2()
{
  set_keypress();

  fd_set         rfds;
  struct timeval tv;
  int            retval;
  int            c=0;
    
  printf("'q' to quit\n");
  
  while( c != 'q' )
  {
    int i = 0;

    printf(">");

    do
    {      
      // Watch stdin (fd 0) to see when it has input.
      FD_ZERO(&rfds);
      FD_SET(0, &rfds);

      // don't wait
      tv.tv_sec =  0;
      tv.tv_usec = 0;

      retval = select(1, &rfds, NULL, NULL, i==0 ? NULL : &tv);
      // Don't rely on the value of tv now - it may have been overwritten by select

      // if an error occurred
      if (retval == -1)
        perror("select()");
      else 
      {
        // if data is waiting
        if (retval)
        {
          c = getchar();
          printf("%i %c (%i) ",i,(char)c,c);
          ++i;

        }
        else
        {
          printf("\n");
          break; // no data available
        }
      }

    } while( 1 );
  }

  reset_keypress();
}

void cw::kbTest3()
{
  set_keypress();

  int i =0;

  printf("<enter> to quit");
  
  while(1)
  {
    cw::sleepMs(500); // sleep milliseconds

    printf("%i\n",i);
    
    i += 1;

    if( isKeyWaiting() )
      break;

  }

  reset_keypress();
  
}
