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
#include "cwAudioDevice.h"
#include "cwAudioDeviceTest.h"
#include "cwAudioDeviceAlsa.h"
#include "cwAudioBuf.h"
#include "cwTcpSocket.h"
#include "cwTcpSocketSrv.h"
#include "cwTcpSocketTest.h"
#include "cwMdns.h"
//#include "cwNbMem.h"

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
  cw::filesys::pathPart_t* pp = cw::filesys::pathParts(__FILE__);
  
  cwLogInfo("dir:%s",pp->dirStr);
  cwLogInfo("fn: %s",pp->fnStr);
  cwLogInfo("ext:%s",pp->extStr);

  char* fn = cw::filesys::makeFn( pp->dirStr, pp->fnStr, pp->extStr, nullptr );

  cwLogInfo("fn: %s",fn);

  cw::mem::release(pp);
  cw::mem::release(fn);
  
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
void audioDevTest(      cw::object_t* cfg, int argc, const char* argv[] ) { cw::audio::device::test( argc, argv ); }
void audioDevAlsaTest(  cw::object_t* cfg, int argc, const char* argv[] ) { cw::audio::device::alsa::report(); }
//void nbmemTest(         cw::object_t* cfg, int argc, const char* argv[] ) { cw::nbmem::test(); }

void socketTest( cw::object_t* cfg, int argc, const char* argv[] )
{
  if( argc >= 3 )
  {
    unsigned short localPort  = atoi(argv[1]);
    unsigned short remotePort = atoi(argv[2]);
    const char* remoteAddr = "127.0.0.1"; //"224.0.0.251"; //"127.0.0.1";
    printf("local:%i remote:%i\n", localPort, remotePort);
    
    cw::net::socket::test( localPort, remoteAddr, remotePort );
  }
}

void socketTestTcp( cw::object_t* cfg, int argc, const char* argv[] )
{
  // server: ./cw_rt main.cfg socketTcp 5434 5435 dgram/stream server
  // client: ./cw_rt main.cfg socketTcp 5435 5434 dgram/stream
  
  if( argc >= 4 )
  {
    unsigned short localPort  = atoi(argv[1]);
    unsigned short remotePort = atoi(argv[2]);
    bool           dgramFl    = strcmp(argv[3],"dgram") == 0;
    bool           serverFl   = false;
    
    if( argc >= 5 )
      serverFl = strcmp(argv[4],"server") == 0;

    printf("local:%i remote:%i %s %s\n", localPort, remotePort, dgramFl ? "dgram":"stream", serverFl?"server":"client");
    
    cw::net::socket::test_tcp( localPort, "127.0.0.1", remotePort, dgramFl, serverFl );
  }
}

void socketSrvTest( cw::object_t* cfg, int argc, const char* argv[] )
{
  if( argc >= 3 )
  {
    unsigned short localPort  = atoi(argv[1]);
    unsigned short remotePort = atoi(argv[2]);

    printf("local:%i remote:%i\n", localPort, remotePort);
    
    cw::net::srv::test( localPort, "127.0.0.1", remotePort );
  }
}

void socketMdnsTest( cw::object_t* cfg, int argc, const char* argv[] )
{
  cw::net::mdns::test();
}


void dirEntryTest( cw::object_t* cfg, int argc, const char* argv[] )
{
  if( argc >= 2 )
  {
    const char*              path         = argv[1];
    unsigned                 dirEntryN    = 0;
    unsigned                 includeFlags = cw::filesys::kFileFsFl | cw::filesys::kDirFsFl | cw::filesys::kFullPathFsFl | cw::filesys::kRecurseFsFl;
    cw::filesys::dirEntry_t* de           = cw::filesys::dirEntries( path,includeFlags, &dirEntryN );
    for(unsigned i=0; i<dirEntryN; ++i)
      cwLogInfo("%s",de[i].name);

    cw::mem::release(de);
  }
}

void stubTest( cw::object_t* cfg, int argc, const char* argv[] )
{
  /*
  typedef struct v_str
  {
    int x = 1;
    int y = 2;
    void* z = nullptr;
  } v_t;


  v_t v;
  printf("%i %i %p\n",v.x,v.y,v.z);
  */

  const char* s = "\x16lmac=00-90-D5-80-F4-DE\x7dummy=0";
  printf("len:%li\n",strlen(s));
    
  
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
   { "audioDev",audioDevTest },
   { "audioDevAlsa", audioDevAlsaTest },
   //{ "nbmem", nbmemTest },
   { "socket", socketTest },
   { "socketTcp", socketTestTcp },
   { "socketSrv", socketSrvTest },
   { "socketMdns", socketMdnsTest },
   { "dirEntry", dirEntryTest },
   { "stub", stubTest },
   { nullptr, nullptr }
  };

  // read the command line
  cw::object_t* cfg   = NULL;
  const char*   cfgFn = argc > 1 ? argv[1] : nullptr;
  const char*   mode  = argc > 2 ? argv[2] : nullptr;

  
  cw::log::createGlobal();

  // if valid command line args were given and the cfg file was successfully read
  if( cfgFn != nullptr && mode != nullptr && objectFromFile( cfgFn, cfg ) == cw::kOkRC )
  {
    int i;
    // locate the requested function and call it
    for(i=0; modeArray[i].label!=nullptr; ++i)
    {
      //printf("'%s' '%s'\n",modeArray[i].label,mode);
      
      if( cw::textCompare(modeArray[i].label,mode)==0 )
      {
        modeArray[i].func( cfg, argc-2, argv + 2 );
        break;
      }
    }
    // if the requested function was not found
    if( modeArray[i].label == nullptr )
      cwLogError(cw::kInvalidArgRC,"The mode selector: '%s' is not valid.", cwStringNullGuard(mode));

    cfg->free();
  }
  
  cw::log::destroyGlobal();

  return 0;
}


  


