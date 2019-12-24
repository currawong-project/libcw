#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFileSys.h"
#include "cwTextBuf.h"
#include "cwLex.h"
#include "cwNumericConvert.h"
#include "cwObject.h"
#include "cwThread.h"
#include "cwText.h"
#include "cwWebSock.h"
#include "cwWebSockSvr.h"
#include "cwSerialPort.h"
#include "cwSerialPortSrv.h"
#include "cwMidi.h"
#include "cwTime.h"
#include "cwMidiPort.h"
#include "cwAudioPort.h"
#include "cwAudioBuf.h"

#include <iostream>


void print()
{
  printf("\n");
}

template<typename T0, typename T1, typename ...ARGS>
  void print(T0 t0, T1 t1, ARGS&&... args)
{
  static const unsigned short int size = sizeof...(ARGS);
  std::cout << t0 << ":" << t1 << " (" << size << "), ";
  print(std::forward<ARGS>(args)...);
}

void get(int)
{
  printf("\n");
}

template<typename T0, typename T1, typename... ARGS>
  void get(int n, T0 t0, T1& t1, ARGS&&... args)
{
  std::cout << t0 << ":" " (" << n << "), ";
  t1 = n;
  get(n+1,std::forward<ARGS>(args)...);
}


using namespace std;


void variadicTplTest( cw::object_t* cfg, int argc, const char* argv[] )
{
  print("a", 1, "b", 3.14, "c",5L);
  
  int v0=0,v1=0,v2=0;
  get(0, "a", v0, "b", v1, "c", v2);
  printf("get: %i %i %i",v0,v1,v2);
  
  printf("\n");
}



void fileSysTest( cw::object_t* cfg, int argc, const char* argv[] )
{
  cw::fileSysPathPart_t* pp = cw::fileSysPathParts(__FILE__);
  
  cwLogInfo("dir:%s",pp->dirStr);
  cwLogInfo("fn: %s",pp->fnStr);
  cwLogInfo("ext:%s",pp->extStr);

  char* fn = cw::fileSysMakeFn( pp->dirStr, pp->fnStr, pp->extStr, nullptr );

  cwLogInfo("fn: %s",fn);

  cw::memRelease(pp);
  cw::memRelease(fn);
  
}

void numbCvtTest( cw::object_t* cfg, int argc, const char* argv[] )
{
  int8_t x0 = 3;
  int x1 = 127;
  
  cw::numeric_convert( x1, x0 );
  printf("%i %i\n",x0,x1);
    

  int v0;
  double v1;
  cw::string_to_number("123",v0);
  cw::string_to_number("3.4",v1);
  printf("%i %f\n",v0,v1 );
  
}

void objectTest( cw::object_t* cfg, int argc, const char* argv[] )
{
  cw::object_t* o;
  const char s [] = "{ a:1, b:2, c:[ 1.23, 4.56 ] }";
  cw::objectFromString(s,o);

  int v;
  o->get("b",v);
  printf("value:%i\n",v);
  
  o->print();

  int a = 0;
  int b = 0;

  o->getv("a",a,"b",b);
  printf("G: %i %i\n",a,b);
    
  o->free();
}

void threadTest(        cw::object_t* cfg, int argc, const char* argv[] ) { cw::threadTest(); }
void websockSrvTest(    cw::object_t* cfg, int argc, const char* argv[] ) { cw::websockSrvTest(); }
void serialPortSrvTest( cw::object_t* cfg, int argc, const char* argv[] ) { cw::serialPortSrvTest(); }
void midiDeviceTest(    cw::object_t* cfg, int argc, const char* argv[] ) { cw::midi::device::test();}
void textBufTest(       cw::object_t* cfg, int argc, const char* argv[] ) { cw::textBuf::test(); }
void audioBufTest(      cw::object_t* cfg, int argc, const char* argv[] ) { cw::audio::buf::test(); }
void audioPortTest(     cw::object_t* cfg, int argc, const char* argv[] )
{
  cw::audio::device::test( false, argc, argv );
}

void stubTest( cw::object_t* cfg, int argc, const char* argv[] )
{
  typedef struct v_str
  {
    int x = 1;
    int y = 2;
    void* z = nullptr;
  } v_t;


  v_t v;
  printf("%i %i %p\n",v.x,v.y,v.z);
}


int main( int argc, const char* argv[] )
{  

  typedef struct func_str
  {
    const char* label;
    void (*func)(cw::object_t* cfg, int argc, const char* argv[] );    
  } func_t;

  // function dispatch list
  func_t modeArray[] =
  {
   { "variadicTpl", variadicTplTest },
   { "fileSys", fileSysTest },
   { "numbCvt", numbCvtTest },
   { "object", objectTest },
   { "thread", threadTest },
   { "websockSrv", websockSrvTest },
   { "serialSrv", serialPortSrvTest },
   { "midiDevice", midiDeviceTest },
   { "textBuf", textBufTest },
   { "audioBuf", audioBufTest },
   { "audioPort",audioPortTest },
   { "stub", stubTest },
   { nullptr, nullptr }
  };

  // read the command line
  cw::object_t* cfg   = NULL;
  const char*   cfgFn = argc > 1 ? argv[1] : nullptr;
  const char*   mode  = argc > 2 ? argv[2] : nullptr;

  
  cw::logCreateGlobal();

  // if valid command line args were given and the cfg file was successfully read
  if( cfgFn != nullptr && mode != nullptr && objectFromFile( cfgFn, cfg ) == cw::kOkRC )
  {
    int i;
    // locate the requested function and call it
    for(i=0; modeArray[i].label!=nullptr; ++i)
      if( cw::textCompare(modeArray[i].label,mode)==0 )
      {
        modeArray[i].func( cfg, argc-2, argv + 2 );
        break;
      }

    // if the requested function was not found
    if( modeArray[i].label == nullptr )
      cwLogError(cw::kInvalidArgRC,"The mode selector: '%s' is not valid.", cwStringNullGuard(mode));

    cfg->free();
  }
  
  cw::logDestroyGlobal();

  return 0;
}


  


