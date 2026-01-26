#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwVariant.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwTextBuf.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwB23Tree.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwThread.h"
#include "cwKeyboard.h"
#include "cwSpScBuf.h"
#include "cwSpScQueueTmpl.h"
#include "cwNbMpScQueue.h"
#include "cwMtQueueTester.h"
#include "cwThreadMach.h"
#include "cwSerialPort.h"
#include "cwSerialPortSrv.h"
#include "cwSocket.h"
#include "cwUtility.h"
#include "cwMath.h"
#include "cwDspTypes.h"
#include "cwDsp.h"
#include "cwAudioTransforms.h"
#include "cwAudioFileProc.h"
#include "cwPvAudioFileProc.h"

#include "cwTime.h"
#include "cwMidi.h"
#include "cwMidiDecls.h"

#include "cwDynRefTbl.h"
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwPerfMeas.h"

#include "cwFlowDecl.h"
#include "cwFlow.h"

#include "cwPianoScore.h"
#include "cwGutimReg.h"
//#include "cwIoPresetSelApp.h"


#include "cwMidiFile.h"
#include "cwAudioDevice.h"

#include "cwPresetSel.h"

#include "cwSfMatch.h"
#include "cwScoreTest.h"
#include "cwSfTrack.h"


#include "cwScoreFollowerPerf.h"
#include "cwScoreFollower.h"
#include "cwScoreFollowTest.h"

#include "cwScoreFollow2Test.h"
#include "cwMidiDetectors.h"

#include "cwCsv.h"

#if defined(cwWEBSOCK)
#include "cwWebSock.h"
#include "cwWebSockSvr.h"
#include "cwUi.h"
#include "cwUiTest.h"
#endif

#if(cwALSA)
#include "cwMidiDevice.h"
#include "cwMidiDeviceTest.h"
#include "cwMidiFileDev.h"
#include "cwAudioDeviceTest.h"
#include "cwAudioDeviceAlsa.h"
#endif

#include "cwAudioBuf.h"
#include "cwAudioDeviceFile.h"
#include "cwTcpSocket.h"
#include "cwTcpSocketSrv.h"
#include "cwTcpSocketTest.h"


//#include "cwMdns.h"
//#include "cwDnsSd.h"
//#include "cwEuCon.h"
#if defined(cwWEBSOCK)
#include "cwIo.h"
#include "cwIoTest.h"
#include "cwIoMinTest.h"
#include "cwIoAudioMidiApp.h"
#include "cwIoMidiRecordPlay.h"
#endif

#if !defined(cwWEB)
#include "cwDataSets.h"
#include "cwSvg.h"
#endif

#include "cwAudioFile.h"
#include "cwAudioFileOps.h"
#include "cwMidiState.h"
#include "cwSvgMidi.h"

#include <iostream>

unsigned calc( unsigned n )
{ return n; }

template<typename T0, typename T1, typename... ARGS>
  unsigned calc( T0 n, T1 i, ARGS&&... args)
{  
  return calc(n + i, std::forward<ARGS>(args)...);
}

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


template< typename T0 >
  unsigned fmt_data( char* buf, unsigned n, T0 t0 )
{
  return cw::toText(buf, n, t0);
}

template<>
  unsigned fmt_data( char* buf, unsigned n, const char* v )
{
  return cw::toText(buf,n,v);
}

unsigned to_text_base(char*, unsigned n, unsigned i )
{ return i; }

template<typename T0, typename T1, typename... ARGS>
  unsigned to_text_base( char* buf, unsigned  n, unsigned i, T0 t0, T1 t1, ARGS&&... args)
{
  i += fmt_data(buf+i, n-i, t0);
  i += fmt_data(buf+i, n-i, t1);
  
  if( i >= n )
    return i;

  return to_text_base(buf,n,i,std::forward<ARGS>(args)...);
}

template< typename... ARGS>
  unsigned to_text(const char* prefix, char* buf, unsigned  n, ARGS&&... args)
{
  unsigned i = cw::toText(buf, n, prefix );

  return to_text_base(buf,n,i,std::forward<ARGS>(args)...);
}



using namespace std;

enum { kIsFilePathFl = 0x01, kIsDirPathFl=0x02, kPathMustExistFl=0x04, kVarOptionalFl=0x08, kCreateDirFl=0x010 };
char* instantiatePathVariable( const cw::object_t* args, const char* label, unsigned flags )
{
  cw::rc_t    rc;
  const char* fn         = nullptr;
  char*       expandedFn = nullptr;

  // locate the cfg field associated with 'label'
  if((rc = args->get(label,fn)) != cw::kOkRC )
  {
    if( !cwIsFlag(flags,kVarOptionalFl) )
    {
      rc = cwLogError(cw::kEleNotFoundRC,"The mandatory file '%s' was not found.",cwStringNullGuard(label));
      goto errLabel;
    }
    
    return nullptr;
  }

  // expand the path (replace ~ with home directory)
  if((expandedFn  = cw::filesys::expandPath(fn)) == nullptr )
  {
    rc = cwLogError(cw::kOpFailRC,"Path expansion failed on '%s'.",fn);
    goto errLabel;
  }

  // check if the path must exist
  if( cwIsFlag(flags,kPathMustExistFl) )
  {
    // if this is a file then the file must exist
    if( cwIsFlag(flags,kIsFilePathFl) && !cw::filesys::isFile(expandedFn) )
    {
      rc = cwLogError(cw::kFileNotFoundRC,"The path variable '%s' ('%s') does not identify an existing file.",label,expandedFn );
      goto errLabel;
    }

    // if this is a directory then the directory must exist
    if( cwIsFlag(flags,kIsDirPathFl ) && !cw::filesys::isDir(expandedFn))
    {

      // the dir. doesn't exist - is it ok to create it?
      if( cwIsFlag(flags,kCreateDirFl) )
      {
        if((rc = cw::filesys::makeDir( expandedFn)) != cw::kOkRC )
        {
          rc = cwLogError(rc,"Unable to create the directory for '%s' ('%s').",label,expandedFn);
          goto errLabel;
        }
        
      }
      else
      {    
        rc = cwLogError(cw::kFileNotFoundRC,"The path variable '%s' ('%s') does not identify an existing directory.",label,expandedFn );
        goto errLabel;
      }
    }
  }

  errLabel:
  if( rc != cw::kOkRC )
    cw::mem::release(expandedFn);

  return expandedFn;
}

char* requiredExistingDir( const cw::object_t* args, const char* label )
{ return instantiatePathVariable(args,label,kIsDirPathFl | kPathMustExistFl); }

char* optionalExistingDir( const cw::object_t* args, const char* label )
{ return instantiatePathVariable(args,label,kVarOptionalFl | kIsDirPathFl | kPathMustExistFl); }

char* requiredNewDir( const cw::object_t* args, const char* label )
{ return instantiatePathVariable(args,label,kIsDirPathFl | kPathMustExistFl | kCreateDirFl); }

char* optionalNewDir( const cw::object_t* args, const char* label )
{ return instantiatePathVariable(args,label,kVarOptionalFl | kIsDirPathFl | kPathMustExistFl | kCreateDirFl ); }

char* requiredExistingFile( const cw::object_t* args, const char* label )
{ return instantiatePathVariable(args,label,kIsFilePathFl | kPathMustExistFl); }

char* optionalExistingFile( const cw::object_t* args, const char* label )
{ return instantiatePathVariable(args,label,kVarOptionalFl | kIsFilePathFl | kPathMustExistFl); }

char* requiredNewFile( const cw::object_t* args, const char* label )
{ return instantiatePathVariable(args,label,0); }

char* optionalNewFile( const cw::object_t* args, const char* label )
{ return instantiatePathVariable(args,label,kVarOptionalFl); }




cw::rc_t variadicTplTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] )
{
  print("a", 1, "b", 3.14, "c",5L);
  
  int v0=0,v1=0,v2=0;
  get(0, "a", v0, "b", v1, "c", v2);
  printf("get: %i %i %i",v0,v1,v2);
  
  printf("\n");

  const int bufN = 32;
  char buf[bufN];
  buf[0] = '\0';
  unsigned n = to_text("prefix: ",buf,bufN,"a",1,"b",3.2,"hi","ho");
  printf("%i : %s\n",n,buf);


  unsigned m = calc(0,1,2,3);
  printf("Calc:%i\n",m);

  return cw::kOkRC;
}


cw::rc_t testTest(             const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::test::test(args,argc,argv); }
cw::rc_t threadTest(           const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::threadTest(); }
cw::rc_t kbTest1Test(          const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { cw::kbTest1(); return cw::kOkRC; }
cw::rc_t kbTest2Test(          const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { cw::kbTest2(); return cw::kOkRC; }
cw::rc_t kbTest3Test(          const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { cw::kbTest3(); return cw::kOkRC; }
cw::rc_t spscBuf(              const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::spsc_buf::test(); }
cw::rc_t spscQueueTmpl(        const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::testSpScQueueTmpl(); }
cw::rc_t nbMpScQueue(          const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::mt_queue_tester::test(args); }
cw::rc_t serialPortSrvTest(    const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::serialPortSrvTest(); }
cw::rc_t audioDevFileTest(     const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::audio::device::file::test(args); }
cw::rc_t b23TreeTest(          const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::b23::test(args); }
cw::rc_t midiFileTest(         const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::midi::file::test(args); }
cw::rc_t audioFileTest(        const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::audiofile::test(args); }
cw::rc_t audioFileOp(          const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::afop::test(args); }
cw::rc_t pianoScoreTest(       const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::perf_score::test(args); }
cw::rc_t gutimRegTest(         const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::gutim::reg::test(args); }
cw::rc_t amToMidiFile(         const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::midi_record_play::am_to_midi_file(args); }
cw::rc_t audioFileProc(        const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::afop::file_processor(args); }
cw::rc_t pvocFileProc(         const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::afop::pvoc_file_processor(args); }
//cw::rc_t socketMdnsTest(       const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::net::mdns::test(); }
//cw::rc_t dnsSdTest(            const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::net::dnssd::test(); }
//cw::rc_t euConTest(            const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::eucon::test(); }

cw::rc_t scoreFollowTest(    const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::score_follow_test::test(args); }
cw::rc_t svgMidiFileTest(    const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::svg_midi::test_midi_file(args); }
cw::rc_t midiStateTest(      const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::midi_state::test(args); }
cw::rc_t csvTest(            const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::csv::test(args); }
cw::rc_t scoreTest(          const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::score_test::test(args); }
cw::rc_t translateFrags(     const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::preset_sel::translate_frags(args); }
cw::rc_t scoreFollow2(       const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::score_follow_2::test(args); }
cw::rc_t midiDetect(         const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::midi_detect::test(args); }

#if defined(cwWEBSOCK)
cw::rc_t websockSrvTest(    const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::websockSrvTest(args); }
cw::rc_t uiTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] )         { return cw::ui::test(args); }
#if defined(cwALSA)
cw::rc_t ioTest(            const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::io::test(args); }
cw::rc_t ioMinTest(         const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::min_test(args); }
cw::rc_t ioAudioMidiTest(   const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::audio_midi_app::main(args); }
cw::rc_t ioPresetSelTest(   const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] )
{
  //return cw::preset_sel_app::main(args,argc-1,argv+1);
  return cw::kOkRC;
}
#else
cw::rc_t _no_alsa_websock() { return cwLogError(cw::kResourceNotAvailableRC,"Websock or ALSA functionality not included in this build."); } 
cw::rc_t ioTest(            const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_alsa_websock(); }
cw::rc_t ioMinTest(         const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_alsa_websock(); }
cw::rc_t ioAudioMidiTest(   const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_alsa_websock(); }
cw::rc_t ioPresetSelTest(   const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_alsa_websock(); }
#endif
#else
cw::rc_t _no_websock() { return cwLogError(cw::kResourceNotAvailableRC,"Websocket functionality not included in this build."); } 
cw::rc_t websockSrvTest(    const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_websock(); }
cw::rc_t uiTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] )         { return _no_websock(); }
cw::rc_t ioTest(            const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_websock(); }
cw::rc_t ioMinTest(         const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_websock(); }
cw::rc_t ioAudioMidiTest(   const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_websock(); }
cw::rc_t ioPresetSelTest(   const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_websock(); }
#endif


#if defined(cwALSA)
cw::rc_t midiDeviceReport(     const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::midi::device::testReport();}
cw::rc_t midiDeviceTest(       const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::midi::device::test(args);}
cw::rc_t audioDevTest(         const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::audio::device::test( args ); }
cw::rc_t audioDevTestTone(     const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::audio::device::test_tone( args ); }
cw::rc_t audioDevAlsaTest(     const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::audio::device::alsa::report(); }
cw::rc_t audioDevRpt(          const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::audio::device::report(); }
#else
cw::rc_t _no_alsa() { return cwLogError(cw::kResourceNotAvailableRC,"ALSA based functionality not included in this build."); } 
cw::rc_t midiDeviceReport(     const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_alsa();}
cw::rc_t midiDeviceTest(       const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_alsa();}
cw::rc_t audioDevTest(         const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_alsa(); }
cw::rc_t audioDevTestTone(     const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_alsa(); }
cw::rc_t audioDevAlsaTest(     const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_alsa(); }
cw::rc_t audioDevRpt(          const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_alsa(); }
#endif

cw::rc_t socketTestUdp(    const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::net::socket::test_udp(args); }
cw::rc_t socketTestTcp(    const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::net::socket::test_tcp(args); }
cw::rc_t socketSrvUdpTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::net::srv::test_udp_srv(args); }
cw::rc_t socketSrvTcpTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::net::srv::test_tcp_srv(args); }

cw::rc_t sockMgrSrvTest(    const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::socksrv::testMain(args); }
cw::rc_t sockMgrClientTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::socksrv::testMain(args); }

#if defined(cwWEB)
cw::rc_t _no_c11() { return cwLogError(cw::kResourceNotAvailableRC,"C++11 functionality not included in this build."); } 

cw::rc_t mnistTest(   const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_c11(); }
cw::rc_t datasetTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_c11(); }
cw::rc_t svgTest(     const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return _no_c11(); }
cw::rc_t datasetWtrTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] )  { return _no_c11(); }
cw::rc_t datasetRdrTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] )  { return _no_c11(); }
cw::rc_t datasetAdapterTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] )  { return _no_c11(); }

#else

cw::rc_t mnistTest(          const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::dataset::mnist::test(cfg); }
cw::rc_t datasetTest(        const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::dataset::test(args); }
cw::rc_t datasetWtrTest(     const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::dataset::wtr::test(args); }
cw::rc_t datasetRdrTest(     const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::dataset::rdr::test(args); }
cw::rc_t datasetAdapterTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::dataset::adapter::test(args); }

cw::rc_t svgTest(   const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) { return cw::svg::test(args); }
#endif

cw::rc_t dirEntryTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] ) {  return cw::filesys::dirEntryTest(args); }

cw::rc_t stubTest( const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] )
{
  cw::rc_t rc = cw::kOkRC;
  return rc;
}

const cw::object_t* _locateArgsRecd( const cw::object_t* cfg, const char*& cfgLabel )
{
  const cw::object_t* o;

  if((cfg = cfg->find_child("test")) == nullptr )
  {
    cwLogError(cw::kSyntaxErrorRC,"The cli cfg. file does not have a 'test' record.");
    return nullptr;
  }

  if((o = cfg->find_child(cfgLabel)) == nullptr )
      cwLogError(cw::kEleNotFoundRC,"The test selector: '%s' was not found in the configuration file.", cwStringNullGuard(cfgLabel));
  else
  {
  
    const struct cw::object_str* oo     = nullptr;

    // if the cfg record label does not match the test mode label - then get the test mode label
    if((oo = o->find_child( "test_label" )) != nullptr )
    {
      const char* test_label = nullptr;
      if( oo->value(test_label) == cw::kOkRC )
        cfgLabel = test_label;
    }
  }

  return o;
}

int main( int argc, const char* argv[] )
{  
  cw::rc_t      rc    = cw::kOkRC;
  cw::object_t* cfg   = nullptr;
  const char*   cfgFn = nullptr;
  const char*   mode  = nullptr;
  cw::log::log_args_t log_args;

  
  typedef struct func_str
  {
    const char* label;
    cw::rc_t (*func)(const cw::object_t* cfg, const cw::object_t* args, int argc, const char* argv[] );    
  } func_t;

  // function dispatch list
  func_t modeArray[] =
  {
   { "variadicTpl", variadicTplTest },
    { "test", testTest },
   { "thread", threadTest },
   { "kbTest1", kbTest1Test },
   { "kbTest2", kbTest2Test },
   { "kbTest3", kbTest3Test },
   { "spscBuf", spscBuf },
   { "spscQueueTmpl", spscQueueTmpl },
   { "nbmpscQueue", nbMpScQueue },
   { "websockSrv", websockSrvTest },
   { "serialSrv", serialPortSrvTest },
   { "midiDeviceReport", midiDeviceReport },
   { "midiDevice", midiDeviceTest },
   { "audioDevFileTest", audioDevFileTest },
   { "audioDevTest",audioDevTest },
   { "audioDevTone", audioDevTestTone },
   { "audioDevAlsa", audioDevAlsaTest },
   { "audioDevRpt", audioDevRpt },
   //{ "nbmem", nbmemTest },
   { "socketUdp", socketTestUdp },
   { "socketTcpClient", socketTestTcp },
   { "socketTcpServer", socketTestTcp },
   { "socketSrvUdp", socketSrvUdpTest },
   { "socketSrvTcp", socketSrvTcpTest },
   { "sockMgrSrvTest", sockMgrSrvTest },
   { "sockMgrClientTest", sockMgrClientTest },
   { "uiTest", uiTest },
   //{ "socketMdns", socketMdnsTest },
   //{ "dnssd",  dnsSdTest },
   //{ "eucon",  euConTest },
   { "dirEntry", dirEntryTest },
   { "io", ioTest },
   { "io_minimal", ioMinTest },
   { "audio_midi", ioAudioMidiTest },
   { "mnist", mnistTest },
   { "dataset", datasetTest },
   { "dataset_wtr", datasetWtrTest },
   { "dataset_rdr", datasetRdrTest },
   { "dataset_adapter", datasetAdapterTest },
   { "svg",   svgTest },
   { "b23Tree",   b23TreeTest },
   { "midifile", midiFileTest },
   { "audiofile", audioFileTest },
   { "afop",      audioFileOp },
   { "piano_score", pianoScoreTest },
   { "gutim_reg", gutimRegTest },
   { "am_to_midi_file", amToMidiFile },
   { "audio_file_proc", audioFileProc },
   { "pvoc_file_proc",  pvocFileProc },
   { "preset_sel", ioPresetSelTest },
   { "score_follow", scoreFollowTest },
   { "svg_midi_file", svgMidiFileTest },
   { "midi_state", midiStateTest },
   { "csv", csvTest },
   { "score_test", scoreTest },
   { "translate_frags", translateFrags },
   { "sf2", scoreFollow2 },
   { "midi_detect",midiDetect},
   { "stub", stubTest },
   { nullptr, nullptr }
  };

  
  // read the command line
  cfgFn = argc > 1 ? argv[1] : nullptr;
  mode  = argc > 2 ? argv[2] : nullptr;

  init_minimum_args( log_args );
  cw::log::createGlobal(log_args);

  if( argc < 3 )
  {
    cwLogInfo("cli <config_filename> <mode>");
    goto errLabel;
  }

  if( cw::textLength(cfgFn) == 0 )
  {
    rc = cwLogError(cw::kInvalidArgRC,"The configuration file name is empty.");
    goto errLabel;
  }

  if( cw::textLength(mode) == 0 )
  {
    rc = cwLogError(cw::kInvalidArgRC,"The mode selector label is empty.");
    goto errLabel;
  }

  // if valid command line args were given and the cfg file was successfully read
  if((rc = objectFromFile( cfgFn, cfg )) != cw::kOkRC )
  {
    rc = cwLogError(rc,"The main configuraiton file parse failed.");
    goto errLabel;
  }
  else
  {

    cw::rc_t rc = cw::kOkRC;
    const cw::object_t* test_cfg;
    const cw::object_t* args;
    int  i = 0;

    // get the dict. of test cfg. records
    if((rc = cfg->getv("test", test_cfg)) != cw::kOkRC )
    {
      cwLogError(rc,"The 'test' dictionary was not found.");
      goto errLabel;
    }

    // get the requested cfg. records
    if((rc = test_cfg->getv(mode, args)) != cw::kOkRC )
    {
      cwLogError(rc,"The requested test configuration record: '%s' was not found.",cwStringNullGuard(mode));
      goto errLabel;
    }
    
    // locate the requested function and call it
    for(i=0; modeArray[i].label!=nullptr; ++i)
      if( cw::textIsEqual(modeArray[i].label,mode) )
      {
        rc = modeArray[i].func( test_cfg, args, argc-2, argv + 2 );
        break;
      }
  
    // if the requested function was not found
    if( modeArray[i].label == nullptr )
      rc = cwLogError(cw::kInvalidArgRC,"The mode selector: '%s' is not valid.", cwStringNullGuard(mode));
    
  }

 errLabel:
  if( cfg != nullptr )
      cfg->free();

  cw::log::destroyGlobal();

  return (int)rc;
}

