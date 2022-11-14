#ifndef cwKeyboard_H
#define cwKeyboard_H


namespace cw
{

  enum
  {
    kInvalidKId,
    kAsciiKId,
    kLeftArrowKId,
    kRightArrowKId,
    kUpArrowKId,
    kDownArrowKId,
    kHomeKId,
    kEndKId,
    kPgUpKId,
    kPgDownKId,
    kInsertKId,
    kDeleteKId,  
  };

  typedef struct
  {
    unsigned code;
    char     ch;
    bool     ctlFl;
    bool     altFl;
  } cmKbRecd;

  // Set 'p' to NULL if the value of the key is not required.
  void keyPress( cmKbRecd* p );


  // Return non-zero if a key is waiting to be read otherwise return 0.
  // Use getchar() to pick up the key.
  // 
  // Example:
  // while( 1 )
  // {
  //    if( cmIsKeyWaiting() == 0 )
  //       usleep(20000);
  //    else
  //    {
  //      char c = getchar();
  //      switch(c)
  //      {
  //        ....
  //      } 
  //    }
  //
  // }
  //
  // TODO: Note that this function turns off line-buffering on stdin.
  // It should be changed to a three function sequence.
  // bool org_state =  cmSetStdinLineBuffering(false);
  // ....
  // isKeyWaiting()
  // ....
  // setStdinLineBuffering(org_state)
  int isKeyWaiting();

  void kbTest1();
  void kbTest2();
  void kbTest3();
  
}

#endif
