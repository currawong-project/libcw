#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwText.h"
#include "cwTime.h"
#include "cwMidi.h"
#include "cwMidiFile.h"
#include "cwPianoScore.h"
#include "cwSvg.h"
#include "cwMidiState.h"
#include "cwSvgMidi.h"

#define PIX_PER_SEC 100.0
#define NOTE_HEIGHT 15.0
#define TIME_GRID_SECS 5.0
#define PITCH_LABEL_INTERVAL_SECS 10

namespace cw
{
  namespace svg_midi
  {

    enum {
      kBarTypeId,
      kSectionTypeId
    };
    
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

    rc_t _write_svg_line( svg::handle_t svgH, double sec0, double y0, double sec1, double y1, unsigned color )
    {
      rc_t rc = kOkRC;
      
      if((rc = line(svgH, sec0*PIX_PER_SEC,  y0,  sec1*PIX_PER_SEC, y1, "stroke", color, "rgb")) != kOkRC )
      {
        rc = cwLogError(rc,"Error writing SVG line.");
        goto errLabel;
          
      }

    errLabel:
      return rc;
    }
    

    const midi_state::event_t* _write_note_rect( svg::handle_t svgH, const midi_state::event_t* e0, const midi_state::event_t* e1, const midi_state::event_t* t0, unsigned minMidiPitch, unsigned maxMidiPitch )
    {
      cwAssert(  e0!=nullptr && e1!=nullptr && e0->msg != nullptr && e1->msg !=nullptr);
      
      const char*    sciPitch   = midi::midiToSciPitch( e0->msg->u.midi.d0 );
      const unsigned labelCharN = 127;
      char           label[ labelCharN+1 ];

      
      unsigned muid = e0->msg->u.midi.uid;
      if( t0!=nullptr && e0->secs - t0->secs < PITCH_LABEL_INTERVAL_SECS )
          snprintf(label,labelCharN,"%i",muid);
      else
      {
        snprintf(label,labelCharN,"%s - %i",sciPitch,muid);
        t0 = e1;
      }
      
      double y = -1.0 * (e0->msg->u.midi.d0 - minMidiPitch) + (maxMidiPitch - minMidiPitch);
      
      _write_svg_rect( svgH, e0->secs, e1->secs, y, label, 0xafafaf );

      return t0;
    }

    void _write_sound_line( svg::handle_t svgH, const midi_state::event_t* e0, const midi_state::event_t* e1, unsigned minMidiPitch, unsigned maxMidiPitch )
    {
      cwAssert(  e0!=nullptr && e1!=nullptr && e0->msg != nullptr && e1->msg !=nullptr);
      
      double y = -1.0 * (e0->msg->u.midi.d0 - minMidiPitch) + (maxMidiPitch - minMidiPitch);
      
      _write_svg_horz_line( svgH, e0->secs, e1->secs, y, 0xafafaf );
    }

    void _write_marker( svg::handle_t svgH, const midi_state::event_t* e, unsigned minMidiPitch, unsigned maxMidiPitch )
    {
      unsigned color = e->msg->u.marker.typeId == kBarTypeId ? 0x0000ff : 0xff0000;
      _write_svg_vert_line( svgH, e->secs, color, minMidiPitch, maxMidiPitch );
      unsigned labelCharN = 127;
      char label[labelCharN+1];
      snprintf(label,labelCharN,"%i",e->msg->u.marker.value);
               
      svg::text( svgH, e->secs*PIX_PER_SEC, -20, label );

    }
    
    void _write_svg_ch_note( svg::handle_t svgH, const midi_state::event_t* e0, unsigned minMidiPitch, unsigned maxMidiPitch )
    {
      const midi_state::event_t* e = e0;
      const midi_state::event_t* n0 = nullptr;
      const midi_state::event_t* s0 = nullptr;
      const midi_state::event_t* t0 = nullptr;
      
      for(; e!=nullptr; e=e->link)
      {
        if( cwIsFlag(e->flags,midi_state::kMarkerEvtFl) )
        {
          _write_marker( svgH, e, minMidiPitch, maxMidiPitch );
        }
        
        if( cwIsFlag(e->flags,midi_state::kNoteOffFl) )
        {
          if( n0 == nullptr )
          {
            // consecutive note off msgs 
          }
          else
          {
            t0 = _write_note_rect( svgH, n0, e, t0, minMidiPitch, maxMidiPitch );
          }
          
          n0 = nullptr;
        }
        
        if( cwIsFlag(e->flags,midi_state::kNoteOnFl) )
        {
          // if note on without note-off 
          if( n0 != nullptr )
          {
            // TODO: check for reattack flag
            t0 = _write_note_rect( svgH, n0, e, t0, minMidiPitch, maxMidiPitch );
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

    void _write_svg_ch_pedal( svg::handle_t svgH, const midi_state::event_t* e, unsigned pedal_idx, unsigned minMidiPitch, unsigned maxMidiPitch, unsigned pedalCnt )
    {
      const midi_state::event_t* e0        = nullptr;
      const midi_state::event_t* e1        = nullptr;
      unsigned                   color     = 0;
      const char*                label     = nullptr;
      unsigned                   midiCtlId = midi_state::pedal_index_to_midi_ctl_id(pedal_idx);
      
      switch( midiCtlId )
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
        
        if( !cwIsFlag(e->flags,midi_state::kNoChangeFl) )
        {        
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
            
              double y = (maxMidiPitch - minMidiPitch) + 1 + pedal_idx;

              _write_svg_rect( svgH, e0->secs, e->secs, y, label, color );

              if( midiCtlId == midi::kSustainCtlMdId )
                _write_svg_vert_line( svgH, e->secs, color, minMidiPitch, maxMidiPitch );

              e0 = nullptr;
            }
          }
        }

        if( e1 != nullptr )
        {
          unsigned yOffs = ((maxMidiPitch - minMidiPitch) + pedalCnt) * NOTE_HEIGHT;
          _write_svg_line( svgH, e1->secs, yOffs + e1->msg->u.midi.d1, e->secs, yOffs + e->msg->u.midi.d1, color );
        }
        e1 = e;
          
        
      }
    }

    rc_t _load_from_piano_score( midi_state::handle_t msH, const char* piano_score_fname )
    {
      rc_t            rc = kOkRC;
      score::handle_t scH;
      unsigned n = 0;
      
      if((rc = score::create( scH, piano_score_fname )) != kOkRC )
      {
        rc = cwLogError(rc,"The piano score load failed.");
        goto errLabel;
      }
      else
      {
        const score::event_t* e = base_event(scH);
    
        for(; e!=nullptr; e=e->link)
        {
          uint8_t ch = 0;
          
          if( e->bar != 0 )
            if((rc = setMarker(msH, e->sec, e->uid, ch, kBarTypeId, e->bar )) != kOkRC )
            {
              rc = cwLogError(rc,"Error setting bar marker.");
              goto errLabel;
            }
      
          if( e->section != 0 )
            if((rc = setMarker(msH, e->sec, e->uid, ch, kSectionTypeId, e->section )) != kOkRC )
            {
              rc = cwLogError(rc,"Error setting section marker.");
              goto errLabel;
            }
      
          if( e->status != 0 )
          {
            if( e->status < 255 && e->d0 < 128 && e->d1 < 128 )
            {
              uint8_t status = (uint8_t)e->status & 0xf0;
              uint8_t ch = (uint8_t)e->status & 0x0f;
              uint8_t d0 = (uint8_t)e->d0;
              uint8_t d1 = (uint8_t)e->d1;

              //printf("%i : %i %i :  %i %x %i %i\n", n, e->uid, e->loc, ch, status, d0, d1 );
              
              if((rc = setMidiMsg(msH, e->sec, e->uid, ch, status, d0, d1 ) ) != kOkRC )
              {
                rc = cwLogError(rc,"Error on MIDI event insertion.");
                goto errLabel;
              }
              ++n;
            }
          }
        }
      }
  
    errLabel:
      destroy(scH);
    
      return rc;
    }
  }  
}


namespace cw
{
  namespace svg_midi
  {
    typedef struct svg_midi_str
    {
      midi_state::handle_t msH;
    } svg_midi_t;

    svg_midi_t* _handleToPtr( handle_t h )
    {  return handleToPtr<handle_t,svg_midi_t>(h); }
      
    rc_t _destroy( svg_midi_t* p )
    {
      midi_state::destroy(p->msH);
      mem::release(p);
      return kOkRC;
    }
  }
}

cw::rc_t cw::svg_midi::create( handle_t& hRef )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  svg_midi_t* p = mem::allocZ<svg_midi_t>();

  if((rc = midi_state::create(p->msH, nullptr, nullptr, &midi_state::default_config())) != kOkRC )
  {
    rc = cwLogError(rc,"midi_state create failed.");
    goto errLabel;
  }

  hRef.set(p);
  
 errLabel:
  return rc;
}

cw::rc_t cw::svg_midi::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if(!hRef.isValid())
    return rc;

  svg_midi_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  
  return rc;
}

cw::rc_t cw::svg_midi::setMidiMsg( handle_t h, double secs, unsigned uid, unsigned ch, unsigned status, unsigned d0,  unsigned d1 )
{
  rc_t rc;
  svg_midi_t* p = _handleToPtr(h);

  if( ch >= midi::kMidiChCnt )
  {
    rc = cwLogError(kInvalidArgRC,"Invalid MIDI channel value: %i.",ch);
    goto errLabel;
  }
  
  if( !midi::isStatus(status))
  {
    rc = cwLogError(kInvalidArgRC,"Invalid MIDI status value: %i.",status);
    goto errLabel;
  }
  
  if( d0>127 )
  {
    rc = cwLogError(kInvalidArgRC,"Invalid MIDI d0 value: %i.",d0);
    goto errLabel;
  }
  
  if( d1>127 )
  {
    rc = cwLogError(kInvalidArgRC,"Invalid MIDI d1 value: %i.",d1);
    goto errLabel;
  }
  
  if((rc = midi_state::setMidiMsg( p->msH, secs, uid, (uint8_t)ch, (uint8_t)status, (uint8_t)d0, (uint8_t)d1 )) != kOkRC )
  {
    rc = cwLogError(rc,"midi_state MIDI msg update failed.");
    goto errLabel;
  }

 errLabel:
  return rc;  
}

cw::rc_t cw::svg_midi::setMarker(  handle_t h, double secs, unsigned uid, unsigned ch, unsigned markId, unsigned markValue )
{
  rc_t rc;
  svg_midi_t* p = _handleToPtr(h);

  if( ch >= midi::kMidiChCnt )
  {
    rc = cwLogError(kInvalidArgRC,"Invalid MIDI channel value: %i.",ch);
    goto errLabel;
  }
  
  if((rc = midi_state::setMarker(  p->msH, secs, uid, (uint8_t)ch, markId, markValue )) != kOkRC )
  {
    rc = cwLogError(rc,"midi_state MIDI set marker failed.");
    goto errLabel;
  }

 errLabel:
  return rc;
}

cw::rc_t cw::svg_midi::write( handle_t h, const char* fname )
{
  rc_t rc;
  svg_midi_t* p = _handleToPtr(h);
  if((rc = write(fname,p->msH)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI SVG write failed.");
    goto errLabel;
  }
  
 errLabel:
  return rc;
}


cw::rc_t cw::svg_midi::write( const char* fname, midi_state::handle_t msH )
{
  rc_t     rc           = kOkRC;
  uint8_t  minMidiPitch = midi::kMidiNoteCnt;
  uint8_t  maxMidiPitch = 0;
  unsigned pedal_cnt    = midi_state::pedal_count( msH );
  double   minSec       = 0.0;
  double   maxSec       = 0.0;
  
  const midi_state::event_t* evt = nullptr;  
  svg::handle_t              svgH;

  
  get_note_extents(  msH, minMidiPitch, maxMidiPitch, minSec, maxSec );

  printf("pitch - min:%i max:%i sec - min:%f max:%f\n",minMidiPitch,maxMidiPitch,minSec,maxSec);

  // create the SVG file object
  if((rc = svg::create(svgH)) != kOkRC )
  {
    rc = cwLogError(rc,"SVG file object create failed.");
    goto errLabel;
  }

  // create the time grid
  for(double sec = 0.0; sec<=maxSec; sec+=TIME_GRID_SECS)
    _write_svg_vert_line(svgH, sec, 0xefefef, minMidiPitch, maxMidiPitch );

  // create the note graphics
  for(uint8_t i=0; i<midi::kMidiChCnt; ++i)
    for(uint8_t j=0; j<midi::kMidiNoteCnt; ++j)
      if((evt = note_event_list(msH,i,j)) != nullptr )
        _write_svg_ch_note(svgH, evt, minMidiPitch, maxMidiPitch );

  // create the pedal graphics
  for(uint8_t i=0; i<midi::kMidiChCnt; ++i)
    for(uint8_t j=0; j<midi_state::pedal_count(msH); ++j)
      if((evt = pedal_event_list(msH,i,j)) != nullptr )
        _write_svg_ch_pedal(svgH, evt, j, minMidiPitch, maxMidiPitch, pedal_cnt );

  
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

cw::rc_t cw::svg_midi::midi_to_svg_file( const char* midi_fname, const char* out_fname, const object_t* midi_state_args )
{
  rc_t     rc = kOkRC;
  midi_state::handle_t msH;


  // create the MIDI state object - with caching turned on
  if((rc = midi_state::create( msH, nullptr, nullptr, midi_state_args )) != kOkRC )
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

cw::rc_t cw::svg_midi::piano_score_to_svg_file( const char* piano_score_fname, const char* out_fname, const object_t* midi_state_args )
{
  rc_t     rc = kOkRC;
  midi_state::handle_t msH;


  // create the MIDI state object - with caching turned on
  if((rc = midi_state::create( msH, nullptr, nullptr, midi_state_args )) != kOkRC )
  {
    rc = cwLogError(rc,"Error creating the midi_state object.");
    goto errLabel;
  }

  // load the MIDI file
  if((rc = _load_from_piano_score( msH, piano_score_fname)) != kOkRC )
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


cw::rc_t cw::svg_midi::test_midi_file( const object_t* cfg )
{
  rc_t rc;
  const char* src_file_type = nullptr;
  const char* src_fname = nullptr;
  const char* out_fname  = nullptr;
  const object_t* midi_state_args = nullptr;
  
  if((rc = cfg->getv(
                     "src_file_type", src_file_type,
                     "src_fname", src_fname,
                     "out_fname", out_fname,
                     "midi_state_args",midi_state_args)) != kOkRC )
  {
    rc = cwLogError(rc,"Error parsing svg_midi::test_midi_file() arguments.");
    goto errLabel;
  }

  if( textCompare( src_file_type, "midi" ) == 0 )
  {
    rc = midi_to_svg_file( src_fname, out_fname, midi_state_args );
  }
  else
  if( textCompare( src_file_type, "piano_score" ) == 0 )
  {
    rc = piano_score_to_svg_file( src_fname, out_fname, midi_state_args );
  }
  else
  {
    rc = cwLogError(kInvalidArgRC,"Invalid file type:'%s'.",cwStringNullGuard(src_file_type));
    goto errLabel;
  }

  if( rc != kOkRC )
    cwLogError(rc,"The SVG file create failed.");
  
 errLabel:
  return rc;
}
