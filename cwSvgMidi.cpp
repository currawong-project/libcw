#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwMidi.h"
#include "cwMidiFile.h"
#include "cwPianoScore.h"
#include "cwSvg.h"
#include "cwMidiState.h"
#include "cwSvgMidi.h"

#define PEDAL_COUNT 3
#define SUST_PEDAL_IDX 0
#define SOST_PEDAL_IDX 1
#define SOFT_PEDAL_IDX 2

#define PIX_PER_SEC 100.0
#define NOTE_HEIGHT 15.0

namespace cw
{
  namespace svg_midi
  {

    rc_t _write_svg_rect( svg::handle_t svgH, double secs0, double secs1, double y, const char* label, unsigned color )
    {
      rc_t rc;
      double x =  secs0 * PIX_PER_SEC;
      double ww = secs1 * PIX_PER_SEC - x;
      double hh = NOTE_HEIGHT;
      
      if((rc = svg::rect( svgH, x, y*NOTE_HEIGHT,  ww, hh, "fill",   color, "rgb"  )) != kOkRC )
      {
        rc = cwLogError(rc,"Error creating SVG rect.");
        goto errLabel;
      }

      if((rc = svg::text( svgH, x, y*NOTE_HEIGHT+NOTE_HEIGHT, label )) != kOkRC )
      {
        rc = cwLogError(rc,"Error writing SVG rect label.");
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    rc_t _write_svg_vert_line( svg::handle_t svgH, double sec, unsigned color, unsigned minMidiPitch, unsigned maxMidiPitch )
    {
      rc_t rc = kOkRC;
      
      if((rc = line(svgH, sec*PIX_PER_SEC,  0,  sec*PIX_PER_SEC, (maxMidiPitch-minMidiPitch)*NOTE_HEIGHT, "stroke", color, "rgb")) != kOkRC )
      {
        rc = cwLogError(rc,"Error writing SVG line.");
        goto errLabel;
          
      }

    errLabel:
      return rc;
    }

    rc_t _write_svg_horz_line( svg::handle_t svgH, double sec0, double sec1, double y, unsigned color )
    {
      rc_t rc = kOkRC;
      
      if((rc = line(svgH, sec0*PIX_PER_SEC,  y*NOTE_HEIGHT,  sec1*PIX_PER_SEC, y*NOTE_HEIGHT, "stroke", color, "rgb")) != kOkRC )
      {
        rc = cwLogError(rc,"Error writing SVG line.");
        goto errLabel;
          
      }

    errLabel:
      return rc;
    }
    

    void _write_note_rect( svg::handle_t svgH, const midi_state::event_t* e0, const midi_state::event_t* e1, unsigned minMidiPitch, unsigned maxMidiPitch )
    {
      cwAssert(  e0!=nullptr && e1!=nullptr && e0->msg != nullptr && e1->msg !=nullptr);
      
      const char*    sciPitch   = midi::midiToSciPitch( e0->msg->u.midi.d0 );
      const unsigned labelCharN = 127;
      char           label[ labelCharN+1 ];

      
      unsigned muid = e0->msg->u.midi.uid;
      snprintf(label,labelCharN,"%s - %i",sciPitch,muid);
      
      double y = -1.0 * (e0->msg->u.midi.d0 - minMidiPitch) + (maxMidiPitch - minMidiPitch);
      
      _write_svg_rect( svgH, e0->secs, e1->secs, y, label, 0xafafaf );
      
    }

    void _write_sound_line( svg::handle_t svgH, const midi_state::event_t* e0, const midi_state::event_t* e1, unsigned minMidiPitch, unsigned maxMidiPitch )
    {

      cwAssert(  e0!=nullptr && e1!=nullptr && e0->msg != nullptr && e1->msg !=nullptr);
      
      double y = -1.0 * (e0->msg->u.midi.d0 - minMidiPitch) + (maxMidiPitch - minMidiPitch);
      
      _write_svg_horz_line( svgH, e0->secs, e1->secs, y, 0xafafaf );
    }

    
    void _write_svg_ch_note( svg::handle_t svgH, const midi_state::event_t* e0, unsigned minMidiPitch, unsigned maxMidiPitch )
    {
      const midi_state::event_t* e = e0;
      const midi_state::event_t* n0 = nullptr;
      const midi_state::event_t* s0 = nullptr;
      
      for(; e!=nullptr; e=e->link)
      {
        if( cwIsFlag(e->flags,midi_state::kNoteOffFl) )
        {
          if( n0 == nullptr )
          {
            // consecutive note off msgs 
          }
          else
          {
            _write_note_rect( svgH, n0, e, minMidiPitch, maxMidiPitch );
          }
          
          n0 = nullptr;
        }
        
        if( cwIsFlag(e->flags,midi_state::kNoteOnFl) )
        {
          // if note on without note-off 
          if( n0 != nullptr )
          {
            // TODO: check for reattack flag
            _write_note_rect( svgH, n0, e, minMidiPitch, maxMidiPitch );
          }
          
          n0 = e;
        }
        

        if( cwIsFlag(e->flags,midi_state::kSoundOnFl) )
        {
          if( s0 != nullptr )
          {
            // consecutive sound on msgs
            _write_sound_line( svgH, s0, e, minMidiPitch, maxMidiPitch );
          }
            
          s0 = e;
        }

        if( cwIsFlag(e->flags,midi_state::kSoundOffFl) )
        {
          if( s0 == nullptr )
          {
            // consecutive  off msgs
          }
          else
          {
            _write_sound_line( svgH, s0, e, minMidiPitch, maxMidiPitch );
          }

          s0 = nullptr;
        }
      }
      
    }

    /*
    rc_t _write_svg_pedal( svg_midi_t* p, svg::handle_t svgH, const graphic_evt_t* ge, unsigned minMidiPitch, unsigned maxMidiPitch )
    {
      rc_t        rc       = kOkRC;
      const char* label    = nullptr;
      unsigned    pedal_id = 0;
      unsigned    color;
      
      switch( ge->beg_evt->d0 & 0xf0 )
      {
        case midi::kSustainCtlMdId:
          label = "damp";
          pedal_id = 0;
          color = 0xf4a460;
          break;
          
        case midi::kSostenutoCtlMdId:
          label = "sost";
          pedal_id = 1;
          color = 0x7fffd4;
          break;
          
        case midi::kSoftPedalCtlMdId:          
          label = "soft";
          pedal_id = 2;
          color = 0x98fb98;
          break;
      }

      
      double y = (maxMidiPitch - minMidiPitch) + 1 + pedal_id;

      if((rc = _write_svg_rect( p, svgH, ge, y, label, color )) != kOkRC )
      {
        rc = cwLogError(rc,"Error writing SVG pedal rect.");
        goto errLabel;
      }

      if((rc = _write_svg_line( p, svgH, ge->beg_evt->time, color, minMidiPitch, maxMidiPitch )) != kOkRC )
      {
        rc = cwLogError(rc,"Error writing SVG pedal begin line.");
        goto errLabel;
      }

      if((rc = _write_svg_line( p, svgH, ge->end_evt->time, color, minMidiPitch, maxMidiPitch )) != kOkRC )
      {
        rc = cwLogError(rc,"Error writing SVG pedal end line.");
        goto errLabel;
      }
      
    errLabel:
      return rc;
    }
    */
    
    void _write_svg_ch_pedal( svg::handle_t svgH, const midi_state::event_t* e, unsigned pedal_idx, unsigned minMidiPitch, unsigned maxMidiPitch )
    {
      const midi_state::event_t* e0 = nullptr;
      unsigned color = 0;
      const char* label = nullptr;
      switch( midi_state::pedal_index_to_midi_ctl_id(pedal_idx) )
      {
        case midi::kSustainCtlMdId:
          label = "damp";
          color = 0xf4a460;
          break;
          
        case midi::kSostenutoCtlMdId:
          label = "sost";
          color = 0x7fffd4;
          break;
          
        case midi::kSoftPedalCtlMdId:          
          label = "soft";
          color = 0x98fb98;
          break;
        default:
          assert(0);
      }
      
      for(; e!=nullptr; e=e->link)
      {
        if( cwIsFlag(e->flags,midi_state::kNoChangeFl) )
          continue;
        
        if( cwIsFlag(e->flags,midi_state::kDownPedalFl) )
        {
          if( e0 != nullptr )
          {
            // two consecutive pedal downd - this shouldn't be possible
          }
          else
          {
            e0 = e;
          }
        }

        if( cwIsFlag(e->flags,midi_state::kUpPedalFl))
        {
          if( e0 == nullptr )
          {
            // two consecutive pedal up's
          }
          else
          {
            
            // two consecutve pedal ups - this shouldn't be possible
            double y = (maxMidiPitch - minMidiPitch) + 1 + pedal_idx;

            _write_svg_rect( svgH, e0->secs, e->secs, y, label, color );
            e0 = nullptr;
          }
        }
      }
    }
    
    
  }  
}




cw::rc_t cw::svg_midi::write( const char* fname, midi_state::handle_t msH )
{
  rc_t          rc           = kOkRC;
  uint8_t      minMidiPitch = midi::kMidiNoteCnt;
  uint8_t      maxMidiPitch = 0;
  const midi_state::event_t* evt = nullptr;

  double minSec = 0.0;
  double maxSec = 0.0;
  
  svg::handle_t svgH;
  
  get_note_extents(  msH, minMidiPitch, maxMidiPitch, minSec, maxSec );

  // create the SVG file object
  if((rc = svg::create(svgH)) != kOkRC )
  {
    rc = cwLogError(rc,"SVG file object create failed.");
    goto errLabel;
  }

  // create the note graphics
  for(uint8_t i=0; i<midi::kMidiChCnt; ++i)
    for(uint8_t j=0; j<midi::kMidiNoteCnt; ++j)
      if((evt = note_event_list(msH,i,j)) != nullptr )
        _write_svg_ch_note(svgH, evt, minMidiPitch, maxMidiPitch );

  // create the pedal graphics
  for(uint8_t i=0; i<midi::kMidiChCnt; ++i)
    for(uint8_t j=0; j<midi_state::pedal_count(msH); ++j)
      if((evt = pedal_event_list(msH,i,j)) != nullptr )
        _write_svg_ch_pedal(svgH, evt, j, minMidiPitch, maxMidiPitch );

  
  // write the SVG file
  if((rc = svg::write( svgH, fname, nullptr, svg::kStandAloneFl )) != kOkRC )
  {
    rc = cwLogError(rc,"SVG-MIDI file write failed.");
    goto errLabel;
  }

 errLabel:
  destroy(svgH);
  return rc;  
}

/*
cw::rc_t cw::svg_midi::load_from_piano_score( handle_t h, const char* piano_score_fname )
{
  rc_t            rc = kOkRC;
  svg_midi_t*     p  = _handleToPtr(h);
  score::handle_t scH;
  
  
  if((rc = score::create( scH, piano_score_fname )) != kOkRC )
  {
    rc = cwLogError(rc,"The piano score load failed.");
    goto errLabel;
  }
  else
  {
    unsigned       evtN = event_count(scH);
    const score::event_t* evtA = base_event(scH);
    time::spec_t  timestamp;
    
    for(unsigned i=0; i<evtN; ++i)
    {
      const score::event_t* e = evtA + i;

      time::secondsToSpec(timestamp,e->sec);
      
      if( e->bar != 0 )
        _setEvent(p, kBarTypeId, e->bar, timestamp, i, 0,0,0 );
      
      if( e->section != 0 )
        _setEvent(p, kSectionTypeId, e->section, timestamp, i, 0,0,0 );
      
      if( e->status != 0 )
        _setEvent(p, kMidiTypeId, 0, timestamp, i, 0,0,0 );
    }
  }
  
 errLabel:
  destroy(scH);
    
  return rc;
}
*/

cw::rc_t cw::svg_midi::midi_to_svg_file( const char* midi_fname, const char* out_fname, const object_t* midi_state_args )
{
  rc_t     rc = kOkRC;
  midi_state::handle_t msH;


  // create the MIDI state object - with caching turned on
  if((rc = midi_state::create( msH, nullptr, nullptr, true, midi_state_args )) != kOkRC )
  {
    rc = cwLogError(rc,"Error creating the midi_state object.");
    goto errLabel;
  }

  // load the MIDI file
  if((rc = midi_state::load_from_midi_file( msH, midi_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"Error loading midi file into midi_state object.");
    goto errLabel;
  }

  // write the SVG file
  if((rc = write(out_fname,msH)) != kOkRC )
  {
    rc = cwLogError(rc,"Error write the SVG-MIDI file.");
    goto errLabel;    
  }
  
 errLabel:
  destroy(msH);
  return rc;
}
/*
cw::rc_t cw::svg_midi::piano_score_to_svg_file( const char* piano_score_fname, const char* out_fname, unsigned midiMsgCacheCnt, unsigned pedalUpMidiValue )
{
  rc_t rc = kOkRC;
  handle_t h;

  // create the SVG-MIDI object
  if((rc = create(h,midiMsgCacheCnt,pedalUpMidiValue)) != kOkRC )
  {
    rc = cwLogError(rc,"Error creating the SVG-MIDI object.");
    goto errLabel;
  }

  // load the MIDI file msg events into the svg-midi cache
  if((rc = load_from_piano_score(h, piano_score_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"Error loading the piano score file.");
    goto errLabel;
  }

  // write the SVG file
  if((rc = write(h,out_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"Error write the SVG-MIDI file.");
    goto errLabel;    
  }
  
 errLabel:
  destroy(h);
  
  return rc;
}
*/

cw::rc_t cw::svg_midi::test_midi_file( const object_t* cfg )
{
  rc_t rc;
  const char* midi_fname = nullptr;
  const char* out_fname  = nullptr;
  const object_t* midi_state_args = nullptr;
  
  if((rc = cfg->getv( "midi_fname", midi_fname,
                      "out_fname", out_fname,
                      "midi_state_args",midi_state_args)) != kOkRC )
  {
    rc = cwLogError(rc,"Error parsing svg_midi::test_midi_file() arguments.");
    goto errLabel;
  }

  rc = midi_to_svg_file( midi_fname, out_fname, midi_state_args );
  
 errLabel:
  return rc;
}
