#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwMidi.h"

namespace cw {
  namespace midi {
    
    typedef struct statusDesc_str
    {
      uint8_t   status;
      uint8_t  byteCnt;
      const char*     label;
    } statusDesc_t;

    statusDesc_t _statusDescArray[] =
    {
     // channel messages
     { kNoteOffMdId,	2, "nof" },
     {	kNoteOnMdId,	2, "non" },
     {	kPolyPresMdId,2, "ppr" },
     {	kCtlMdId,		  2, "ctl" },
     {	kPgmMdId,		  1, "pgm" },
     {	kChPresMdId,	1, "cpr" },
     {	kPbendMdId,		2, "pb"  },


     {	kSysExMdId,		kInvalidMidiByte,"sex" },

     // system common
     { kSysComMtcMdId,    1, "mtc" },
     { kSysComSppMdId,    2, "spp" },
     { kSysComSelMdId,    1, "sel" },
     { kSysComUndef0MdId, 0, "cu0" },
     { kSysComUndef1MdId, 0, "cu1" },
     { kSysComTuneMdId,   0, "tun" },
     { kSysComEoxMdId,    0, "eox" },

     // system real-time
     { kSysRtClockMdId, 0, "clk" },
     { kSysRtUndef0MdId,0, "ud0" },
     { kSysRtStartMdId, 0, "beg" },
     { kSysRtContMdId,  0, "cnt" },
     { kSysRtStopMdId,  0, "end" },
     { kSysRtUndef1MdId,0, "ud1" },
     { kSysRtSenseMdId, 0, "sns" },
     { kSysRtResetMdId, 0, "rst" },

     { kInvalidStatusMdId,  kInvalidMidiByte, "ERR" }
    };

    statusDesc_t _metaStatusDescArray[] =
    {
     { kSeqNumbMdId,    2, "seqn"  },
     { kTextMdId,      0xff, "text"  },
     { kCopyMdId,      0xff, "copy"  },
     { kTrkNameMdId,   0xff, "name"  },
     { kInstrNameMdId, 0xff, "instr" },
     { kLyricsMdId,    0xff, "lyric" },
     { kMarkerMdId,    0xff, "mark"  },
     { kCuePointMdId,  0xff, "cue"   },
     { kMidiChMdId,     1, "chan"  },
     { kMidiPortMdId,   1, "port"  },
     { kEndOfTrkMdId,   0, "eot"   },
     { kTempoMdId,      3, "tempo" },
     { kSmpteMdId,      5, "smpte" },
     { kTimeSigMdId,    4, "tsig"  },
     { kKeySigMdId,     2, "ksig"  },
     { kSeqSpecMdId,   0xff, "seqs"  },
     { kInvalidMetaMdId, kInvalidMidiByte, "ERROR"}
    };

    statusDesc_t _pedalLabel[] = 
    {
     { kSustainCtlMdId,    0, "sustn" },
     { kPortamentoCtlMdId, 0, "porta" },
     { kSostenutoCtlMdId,  0, "sostn" },
     { kSoftPedalCtlMdId,  0, "soft"  },
     { kLegatoCtlMdId,     0, "legat" },
     { kInvalidMidiByte, kInvalidMidiByte, "ERROR"}
    };
  }
}

//====================================================================================================

const char* cw::midi::statusToLabel( uint8_t status )
{
  unsigned i;

  if( !isStatus(status) )
    return NULL;

  // remove the channel value from ch msg status bytes
  if( isChStatus(status) )
    status &= 0xf0;

  for(i=0; _statusDescArray[i].status != kInvalidStatusMdId; ++i)
    if( _statusDescArray[i].status == status )
      return _statusDescArray[i].label;

  return _statusDescArray[i].label; 
}

const char*   cw::midi::metaStatusToLabel( uint8_t metaStatus )
{
  int i;
  for(i=0; _metaStatusDescArray[i].status != kInvalidMetaMdId; ++i)
    if( _metaStatusDescArray[i].status == metaStatus )
      break;

  return _metaStatusDescArray[i].label; 
}

const char* cw::midi::pedalLabel( uint8_t d0 )
{
  int i;
  for(i=0; _pedalLabel[i].status != kInvalidMidiByte; ++i)
    if( _pedalLabel[i].status == d0 )
      break;

  return _pedalLabel[i].label;
}

uint8_t cw::midi::statusToByteCount( uint8_t status )
{
  unsigned i;

  if( !isStatus(status) )
    return kInvalidMidiByte;

  // remove the channel value from ch msg status bytes
  if( isChStatus(status) )
    status &= 0xf0;

  for(i=0; _statusDescArray[i].status != kInvalidStatusMdId; ++i)
    if( _statusDescArray[i].status == status )
      return _statusDescArray[i].byteCnt;

  assert(0);

  return 0; 
}

unsigned      cw::midi::to14Bits( uint8_t d0, uint8_t d1 )
{
  unsigned val = d0;
  val <<= 7;
  val += d1;
  return val;
}

void          cw::midi::split14Bits( unsigned v, uint8_t& d0Ref, uint8_t& d1Ref )
{
  d0Ref = (v & 0x3f80) >> 7;
  d1Ref = v & 0x7f;
}

int           cw::midi::toPbend(  uint8_t d0, uint8_t d1 )
{
  int v = to14Bits(d0,d1);
  return v - 8192;
}

void          cw::midi::splitPbend( int v, uint8_t& d0Ref, uint8_t& d1Ref )
{
  unsigned uv = v + 8192;
  split14Bits(uv,d0Ref,d1Ref);
}

//====================================================================================================
const char*     cw::midi::midiToSciPitch( uint8_t pitch, char* label, unsigned labelCharCnt )
{
  static char buf[ kMidiSciPitchCharCnt ];

  if( label == NULL || labelCharCnt == 0 )
  {
    label = buf;
    labelCharCnt = kMidiSciPitchCharCnt;
  }

  assert( labelCharCnt >= kMidiSciPitchCharCnt );

  if( /*pitch < 0 ||*/ pitch > 127 )
  {
    label[0] = 0;
    return label;
  }

  assert( labelCharCnt >= 5 && /*pitch >= 0 &&*/ pitch <= 127 );

  char     noteV[]      =  { 'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B' };
  char     shrpV[]      =  { ' ', '#', ' ', '#', ' ', ' ', '#', ' ', '#', ' ', '#', ' ' };
  int      octave       =  (pitch / 12)-1;
  unsigned noteIdx      =  pitch % 12;
  char     noteCh       =  noteV[ noteIdx ];
  char     sharpCh      =  shrpV[ noteIdx ];
  unsigned idx          =  1;

  label[labelCharCnt-1] = 0;
  label[0]              = noteCh;

  if( sharpCh != ' ' )
  {
    label[1] = sharpCh;
    idx      = 2;
  }

  assert( -1 <= octave && octave <= 9);

  snprintf(label+idx,kMidiSciPitchCharCnt-idx-1,"%i",octave);

  return label;
}


uint8_t    cw::midi::sciPitchToMidiPitch( char pitch, int acc, int octave )
{
  int idx = -1;
  
  switch(tolower(pitch))
  {
    case 'a': idx = 9;  break;
    case 'b': idx = 11; break;
    case 'c': idx = 0;  break;
    case 'd': idx = 2;  break;
    case 'e': idx = 4;  break;
    case 'f': idx = 5;  break;
    case 'g': idx = 7;  break;
    default:
      return kInvalidMidiPitch;
  }

  unsigned rv =  (octave*12) + idx + acc + 12;

  if( rv <= 127 )
    return rv;

  return kInvalidMidiPitch;

}

uint8_t    cw::midi::sciPitchToMidi( const char* sciPitchStr )
{
  const char* cp      = sciPitchStr;
  bool        sharpFl = false;
  bool        flatFl  = false;
  int         octave;
  int         acc     = 0;

  if( sciPitchStr==NULL || strlen(sciPitchStr) > 5 )
    return kInvalidMidiPitch;

  // skip over leading letter
  ++cp;

  if( !(*cp) )
    return kInvalidMidiPitch;

  
  if((sharpFl = *cp=='#') == true )
    acc = 1;
  else
    if((flatFl  = *cp=='b') == true )
      acc = -1;

  if( sharpFl || flatFl )
  {
    ++cp;

    if( !(*cp) )
      return kInvalidMidiPitch;
  }
  
  if( isdigit(*cp) == false && *cp!='-' )
    return kInvalidMidiPitch;

  octave = atoi(cp);


  return sciPitchToMidiPitch( *sciPitchStr, acc, octave );
  
}
