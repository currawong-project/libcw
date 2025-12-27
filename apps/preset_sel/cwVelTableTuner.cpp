//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwFile.h"
#include "cwFileSys.h"
#include "cwMidi.h"
#include "cwIo.h"
#include "cwScoreFollowerPerf.h"
#include "cwIoMidiRecordPlay.h"
#include "cwVelTableTuner.h"
#include "cwNumericConvert.h"
namespace cw
{
  namespace vtbl
  {
    ui::appIdMap_t mapA[] =
    {
      { kInvalidId, kVtDeviceSelectId,    "vtDeviceSelectId" },
      { kVtDeviceSelectId, kVtPianoDevId,        "vtPianoDevId" },
      { kVtDeviceSelectId, kVtSamplerDevId,      "vtSamplerDevId" },      
      { kInvalidId, kVtTableSelectId,     "vtTableSelectId" },
      { kInvalidId, kVtDefaultCheckId,    "vtDefaultCheckId" },
      { kInvalidId, kVtPlayVelSeqBtnId,   "vtPlayVelSeqBtnId" },
      { kInvalidId, kVtPitchId,           "vtPitchId" },
      { kInvalidId, kVtPlayPitchSeqBtnId, "vtPlayPitchSeqBtnId" },
      { kInvalidId, kVtVelocityId,        "vtVelocityId" },
      { kInvalidId, kVtMinPitchId,        "vtMinPitchId" },
      { kInvalidId, kVtMaxPitchId,        "vtMaxPitchId" },
      { kInvalidId, kVtIncPitchId,        "vtIncPitchId" },
      
      { kInvalidId, kVtApplyBtnId,        "vtApplyBtnId" },
      { kInvalidId, kVtSaveBtnId,         "vtSaveBtnId" },
      { kInvalidId, kVtDuplicateBtnId,    "vtDuplicateBtnId" },
      { kInvalidId, kVtNameStrId,         "vtNameStrId" },
      { kInvalidId, kVtStatusId,          "vtStatusId" },
      
      { kInvalidId, kVtEntry0,  "vtEntry0" },
      { kInvalidId, kVtEntry1,  "vtEntry1" },
      { kInvalidId, kVtEntry2,  "vtEntry2" },
      { kInvalidId, kVtEntry3,  "vtEntry3" },
      { kInvalidId, kVtEntry4,  "vtEntry4" },
      { kInvalidId, kVtEntry5,  "vtEntry5" },
      { kInvalidId, kVtEntry6,  "vtEntry6" },
      { kInvalidId, kVtEntry7,  "vtEntry7" },
      { kInvalidId, kVtEntry8,  "vtEntry8" },
      { kInvalidId, kVtEntry9,  "vtEntry9" },
      { kInvalidId, kVtEntry10, "vtEntry10" },
      { kInvalidId, kVtEntry11, "vtEntry11" },
      { kInvalidId, kVtEntry12, "vtEntry12" },
      { kInvalidId, kVtEntry13, "vtEntry13" },
      { kInvalidId, kVtEntry14, "vtEntry14" },
      { kInvalidId, kVtEntry15, "vtEntry15" },
      { kInvalidId, kVtEntry16, "vtEntry16" },
      { kInvalidId, kVtEntry17, "vtEntry17" },
      { kInvalidId, kVtEntry18, "vtEntry18" },
      { kInvalidId, kVtEntry19, "vtEntry19" },
      { kInvalidId, kVtEntry20, "vtEntry20" },
      { kInvalidId, kVtEntry21, "vtEntry21" },
      { kInvalidId, kVtEntry22, "vtEntry22" },
      { kInvalidId, kVtEntry23, "vtEntry23" },
      { kInvalidId, kVtEntry24, "vtEntry24" },      
    };

    typedef struct tbl_str
    {
      bool            defaultFl;
      bool            enableFl;
      uint8_t*        tableA;
      unsigned        tableN;
      char*           name;
      unsigned        mrpDevIdx;
      unsigned        appId;     // id associated with UI select option for this table
      struct tbl_str* link;
    } tbl_t;

    enum {
      kStoppedStateId,
      kNoteOnStateId,   // start a new now at 'nextTime'
      kNoteOffStateId,  // turn off the current note on 'nextTime'
    };

    enum {
      kVelSeqModeId,    // sequence through current vel table on 'vseqPitch'
      kPitchSeqModeId   // sequence through min-maxMIdiPitch on 'pseqVelocity'
    };
    
    typedef struct vtbl_str
    {
      io::handle_t                ioH; //
      midi_record_play::handle_t mrpH; //

      object_t*    cfg;                // 
      char*        cfg_fname;          //
      char*        cfg_backup_dir;     //

      tbl_t*       tableL;    // array of tables
      unsigned     nextTableAppId;
      
      tbl_t*       curTable;  // current table being edited
      
      bool         initUiFl;      // True if the UI has been initialized (UI initialization happens once at startup)
      bool         waitForStopFl; // The player was requested to stop but is waiting for the current note cycle to end 
      unsigned     state;         // player state id (See ???StateId above)
      unsigned     mode;          // sequence across pitch or velocity (See k???ModeId)
      time::spec_t nextTime;      // next note on/off time
      unsigned     nextPitch;     // next seq pitch (during vel sequencing)
      unsigned     nextVelIdx;    // next vel table index (during pitch sequencing)
      
      unsigned     noteOnDurMs;  //  Note on duration
      unsigned     noteOffDurMs; //  Note off duration
      
      unsigned     vseqPitch;    // Sequence across this pitch during velocity sequencing

      unsigned     pseqVelocity;   // Sequence from min to max pitch on this velocity
      unsigned     minPseqPitch;   // during pitch sequencing
      unsigned     maxPseqPitch;
      unsigned     incPseqPitch;
      
      
      char*        duplicateName; // table name to use with the duplicate 
      
    } vtbl_t;

    typedef struct dev_map_str
    {
      const char* label;      // device name (piano, sampler)
      unsigned    appId;      // device UI app id
      unsigned    mrpDevIdx;  // device MRP device index
    } dev_map_t;

    dev_map_t _devMapA[] = {
      { "piano",   kVtPianoDevId,   midi_record_play::kPiano_MRP_DevIdx   },
      { "sampler", kVtSamplerDevId, midi_record_play::kSampler_MRP_DevIdx }
    };

    vtbl_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,vtbl_t>(h); }

    unsigned _dev_map_count() {  return sizeof(_devMapA)/sizeof(_devMapA[0]); }
    
    const dev_map_t* _dev_map_from_label( const char* label )
    {
      for(unsigned i=0; i<_dev_map_count(); ++i)
        if( strcmp(_devMapA[i].label,label) == 0 )
          return _devMapA + i;
      
      return nullptr;
    }

    const dev_map_t* _dev_map_from_appId( unsigned appId )
    {
      for(unsigned i=0; i<_dev_map_count(); ++i)
        if( _devMapA[i].appId == appId )
          return _devMapA + i;
      
      return nullptr;
    }

    const dev_map_t* _dev_map_from_mrpDevIdx( unsigned mrpDevIdx )
    {
      for(unsigned i=0; i<_dev_map_count(); ++i)
        if( _devMapA[i].mrpDevIdx == mrpDevIdx )
          return _devMapA + i;
      
      return nullptr;
    }

    rc_t _destroy_tbl( tbl_t* t )
    {
      mem::release(t->tableA);
      mem::release(t->name);
      mem::release(t);
      return kOkRC;
    }
    
    rc_t _destroy( vtbl_t* p )
    {
      rc_t rc = kOkRC;

      tbl_t* t = p->tableL;
      while( t!=nullptr )
      {
        tbl_t* t0 = t->link;
        _destroy_tbl(t);
        t = t0;
      }

      mem::release(p->cfg_fname);
      mem::release(p->cfg_backup_dir);

      mem::release(p->duplicateName);
      
      mem::release(p);
      return rc;
    }

    rc_t _set_statusv( vtbl_t* p, rc_t rc, const char* fmt, va_list vl )
    {
      const int sN = 128;
      char      s[sN];
      vsnprintf(s,sN,fmt,vl);      
      uiSendValue( p->ioH, uiFindElementUuId(p->ioH,kVtStatusId), s );

      if( rc != kOkRC )
        rc = cwLogError(rc,s);
      
      return rc;
    }

    rc_t _set_status( vtbl_t* p, rc_t rc, const char* fmt, ... )
    {
      va_list vl;
      va_start(vl,fmt);
      rc = _set_statusv(p, rc, fmt, vl );
      va_end(vl);
      return rc;
    }

    unsigned _table_count( vtbl_t* p )
    {
      unsigned n = 0;
      for(tbl_t* t=p->tableL; t!=nullptr; t=t->link)
        ++n;

      return n;
    }

    const tbl_t* _table_from_name( vtbl_t* p, const char* name )
    {
      for(tbl_t* t=p->tableL; t!=nullptr; t=t->link)
        if( textCompare(t->name,name) == 0 )
          return t;
      return nullptr;
    }
    
    rc_t _insert_menu_option( vtbl_t* p, unsigned appId, const char* name )
    {
      rc_t rc = kOkRC;
      unsigned uuId;
      unsigned selectUuId = io::uiFindElementUuId( p->ioH, kVtTableSelectId );
      
      cwAssert( selectUuId != kInvalidId );
      
      if((rc = uiCreateOption( p->ioH, uuId, selectUuId, nullptr, appId, kInvalidId, "optClass", name )) != kOkRC )
      {
        cwLogError(rc,"Create table selection option failed for '%s'.",cwStringNullGuard(name));
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    rc_t _link_in_table( vtbl_t* p, tbl_t* t )
    {
      rc_t rc = kOkRC;

      t->appId = p->nextTableAppId;
      
      // insert the table in the table selection menu 
      if((rc = _insert_menu_option( p, t->appId, t->name )) != kOkRC )
          goto errLabel;
      
      
      // link in the new table
      if( p->tableL == nullptr )
        p->tableL = t;
      else
      {
        t->link = p->tableL;
        p->tableL = t;
      }


      p->nextTableAppId += 1;
      
        
    errLabel:
      if( rc != kOkRC )
        rc = _set_status(p,rc,"Table insertion failed for '%s'.",cwStringNullGuard(t->name));
          
      return rc;      
    }

    rc_t _parseCfg( vtbl_t* p, const object_t* cfg )
    {
      rc_t rc;
      const object_t* tables_node = nullptr;
      tbl_t*          t           = nullptr;

      if((rc = cfg->getv("note_on_ms",p->noteOnDurMs,
                         "note_off_ms",p->noteOffDurMs,
                         "vseq_pitch",p->vseqPitch,
                         "pseq_velocity",p->pseqVelocity,
                         "min_pitch",p->minPseqPitch,
                         "max_pitch",p->maxPseqPitch,
                         "incr_pitch",p->incPseqPitch,
                         "tables",tables_node)) != kOkRC )
      {
        rc = cwLogError(rc,"Velocity table mgr. cfg. file parsing error.");
        goto errLabel;
      }

      // for each table
      for(unsigned i=0; i<tables_node->child_count(); ++i)
      {
        const object_t* tbl_hdr      = nullptr;
        const char*     tbl_name     = nullptr;
        const char*     tbl_device   = nullptr;
        bool            tbl_enableFl = false;
        bool            tbl_defaultFl= false;
        const object_t* tbl_node     = nullptr;
        const dev_map_t*devMap       = nullptr;

        // get the tbl hdr node
        if((tbl_hdr = tables_node->child_ele(i)) == nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"The velocity table at index %i was not found.",i);
          goto errLabel;
        }

        // parse the table record
        if((rc = tbl_hdr->getv("name",    tbl_name,
                               "device",  tbl_device,
                               "enableFl",tbl_enableFl,
                               "defaultFl", tbl_defaultFl,
                               "table",   tbl_node)) != kOkRC )
        {
          rc = cwLogError(rc,"Velocity table cfg. file parsing error.");
          goto errLabel;
        }

        if((devMap = _dev_map_from_label(tbl_device)) == nullptr )
        {
          cwLogError(kInvalidArgRC,"The MIDI device '%s' is not valid.", cwStringNullGuard(tbl_device) );
          goto errLabel;
        }
        
        t           = mem::allocZ<tbl_t>();
        t->name     = mem::duplStr(tbl_name);
        t->mrpDevIdx= devMap->mrpDevIdx;
        t->enableFl = tbl_enableFl;
        t->defaultFl= tbl_defaultFl;
        t->tableN   = tbl_node->child_count();
        t->tableA   = mem::allocZ<uint8_t>( t->tableN );

        // parse the table velocity values
        for(unsigned j=0; j<tbl_node->child_count(); ++j)
        {
          uint8_t vel;
          if((rc = tbl_node->child_ele(j)->value(vel)) != kOkRC )
          {
            rc = cwLogError(rc,"Parsing failed on velocity table '%s' index '%i'.", cwStringNullGuard(t->name), j);
            _destroy_tbl(t);
            goto errLabel;
          }
          
          t->tableA[j] = vel;
        }

        // link in the new table and assign it an app id
        _link_in_table(p, t );
        
        
        // prevent the list from being corrupted should the
        // parsing of the next table fail
        t = nullptr;
      }
      
    errLabel:
      if( rc != kOkRC )
        mem::release(t);
      return rc;
    }

    rc_t _backup( vtbl_t* p )
    {
      rc_t rc;
      
      if((rc = filesys::makeDir(p->cfg_backup_dir)) != kOkRC )
      {
        rc = cwLogError(rc,"The vel.table tuner backup directory '%s' could not be created.",cwStringNullGuard(p->cfg_backup_dir));
        goto errLabel;
      }

      if((rc = file::backup(p->cfg_fname, p->cfg_backup_dir)) != kOkRC )
      {
        rc = cwLogError(rc,"The vel.table tuner file backup failed.",cwStringNullGuard(p->cfg_backup_dir));
        goto errLabel;
      }

    errLabel:
      return rc;
    }
    
    rc_t _save( vtbl_t* p )
    {
      rc_t rc;
      object_t* cfg = nullptr;
      object_t* tables_node = nullptr;

      // backup the current vel table tuner file
      if((rc = _backup(p)) != kOkRC )
        goto errLabel;

      cfg = newDictObject(nullptr);

      newPairObject("note_on_ms",   p->noteOnDurMs,  cfg);
      newPairObject("note_off_ms",  p->noteOffDurMs, cfg);
      newPairObject("vseq_pitch",   p->vseqPitch,    cfg);
      newPairObject("pseq_velocity",p->pseqVelocity, cfg);
      newPairObject("min_pitch",    p->minPseqPitch, cfg);
      newPairObject("max_pitch",    p->maxPseqPitch, cfg);
      newPairObject("incr_pitch",   p->incPseqPitch, cfg);
      tables_node = newPairObject("tables",  newListObject(cfg), cfg );

      for(tbl_t* t=p->tableL; t!=nullptr; t=t->link)
      {
        const dev_map_t* devMap = _dev_map_from_mrpDevIdx( t->mrpDevIdx );
        cwAssert( devMap != nullptr );
        
        object_t* tbl = newDictObject(tables_node);

        tables_node->append_child(tbl);
        
        newPairObject("name",     t->name,      tbl);
        newPairObject("device",   devMap->label,tbl);
        newPairObject("enableFl", t->enableFl,  tbl);
        
        object_t* table_node = newPairObject("table", newListObject(nullptr), tbl);

        for(unsigned i=0; i<t->tableN; ++i)
          newObject( (unsigned)t->tableA[i], table_node );                              
      }
      
      if((rc = objectToFile(p->cfg_fname,cfg)) != kOkRC )
      {
        rc = cwLogError(rc,"Velocity table tuner write failed.");
        goto errLabel;
      }
      
    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"The velocity table tuner save failed.");

      if( cfg != nullptr )
        cfg->free();
      
      return rc;
    }

    void _do_play(vtbl_t* p, unsigned mode )
    {
      if( p->state == kStoppedStateId )
      {
        p->mode          = mode;
        p->state         = kNoteOnStateId;
        p->nextPitch     = p->minPseqPitch;
        p->nextVelIdx    = 0;
        p->waitForStopFl = false;

        time::get(p->nextTime);
        time::advanceMs(p->nextTime,500); // turn on note in 500ms
      }
      else
      {
        p->waitForStopFl = true;
      }
    }

    cw::rc_t _play_vel_sequence(vtbl_t* p)
    {
      rc_t rc = kOkRC;
      _do_play(p,kVelSeqModeId);      
      return rc;
    }

    cw::rc_t _play_pitch_sequence(vtbl_t* p)
    {
      rc_t rc = kOkRC;
      _do_play(p,kPitchSeqModeId);      
      return rc;
    }

    cw::rc_t _apply(vtbl_t* p )
    {
      rc_t rc = kOkRC;
      
      const dev_map_t* dm;
      if((dm = _dev_map_from_mrpDevIdx(p->curTable->mrpDevIdx)) == nullptr )
      {
        rc = _set_status(p,kOpFailRC,"The current table has an invalid device index (%i).",p->curTable->mrpDevIdx );
        goto errLabel;
      }
     
      
      if((rc = vel_table_set( p->mrpH, p->curTable->mrpDevIdx, p->curTable->tableA, p->curTable->tableN )) != kOkRC )
        rc = _set_status(p,rc,"Velocity table apply failed.");
      else
        _set_status(p,kOkRC,"Velocity table applied to '%s'.", cwStringNullGuard(dm->label));
      
    errLabel:
      return rc;
    }

    // Duplicate the source table 't' into the destination table 'dst_tbl'.
    // If newTablename is non-null then use it to name the 'dst_tbl' otherwise
    // name the table with the source table name.
    void _duplicateTable( vtbl_t* p, tbl_t& dst_tbl, const tbl_t* t, const char* newTableName=nullptr )
    {
      // allocate a new velocity table
      if( dst_tbl.tableN != t->tableN )
      {
        dst_tbl.tableA = mem::resize<uint8_t>(dst_tbl.tableA,t->tableN);
        dst_tbl.tableN = t->tableN;
      }

      // copy the src velocities into the destination table
      for(unsigned i=0; i<t->tableN; ++i)
        dst_tbl.tableA[i] = t->tableA[i];

      if( newTableName == nullptr )
        newTableName = t->name;
      
      dst_tbl.defaultFl = t->defaultFl;
      dst_tbl.enableFl  = t->enableFl;
      dst_tbl.name      = mem::reallocStr(dst_tbl.name,newTableName);
      dst_tbl.mrpDevIdx = t->mrpDevIdx;
      dst_tbl.appId     = t->appId;
      dst_tbl.link      = nullptr;
    }

    void _form_versioned_name( const char* name, char* buf, unsigned bufN, int version )
    {
      int i;

      // set 'j' to the  index of the final '_' just prior to numeric suffix
      for(i=textLength(name)-1; i>=0; --i)
        if( !isdigit(name[i]) )
          break;

      // if name is of the form ????_### then name+i is the end of the prefix
      if( i>0 && i < (int)textLength(name) && name[i]=='_' )
      {
        snprintf(buf,bufN,"%.*s_%i",i,name,version);
      }
      else // otherwise append a sufix to name
      {
        snprintf(buf,bufN,"%s_%i",name,version);
      }
      
    }
    
    cw::rc_t _set_duplicate_name( vtbl_t* p, const char* name )
    {
      rc_t rc = kOkRC;
      
      const tbl_t* t;
      unsigned     i = 0;
      unsigned bufN  = textLength(name) + 32;
      char buf[ bufN ];

      strcpy(buf,name);

      // mutate 'name' until it is unique among other tables
      while((t = _table_from_name( p, buf )) != nullptr )
      {        
        _form_versioned_name(name,buf,bufN,i);

        if( i == 999 )
        {
          rc = cwLogError(kInvalidOpRC,"The 'new' name could not be formed.");
          goto errLabel;
        }

        ++i;        
      }

      // store the new name
      p->duplicateName = mem::reallocStr(p->duplicateName,buf);

      // update the UI
      uiSendValue(p->ioH, uiFindElementUuId( p->ioH, kVtNameStrId), p->duplicateName );
      

    errLabel:
      
      return rc;
    }

    cw::rc_t _load( vtbl_t* p, unsigned tableOptionAppId )
    {
      rc_t             rc = kOkRC;
      tbl_t*           t  = p->tableL;
      const dev_map_t* dm = nullptr;
      
      // locate the selected table
      for(; t!=nullptr; t=t->link)
        if( t->appId == tableOptionAppId )
          break;

      // verify that a table was found
      if( t == nullptr )
      {
        cwLogError(kOpFailRC,"The table associated with appId %i was not found.",tableOptionAppId );
        goto errLabel;
      }

      // duplicate the selected table into 'curTable'
      //_duplicateTable(p,p->curTable,t);
      p->curTable = t;

      // update the UI with the new velocity table values
      for(unsigned i = 0; i<t->tableN; ++i)
        uiSendValue( p->ioH, uiFindElementUuId( p->ioH, kVtEntry0 + i), (unsigned)t->tableA[i] );


      // set the device menu
      dm = _dev_map_from_mrpDevIdx(t->mrpDevIdx);      
      assert( dm != nullptr ); // device labels were tested at parse time - so dm must be non-null
      uiSendValue( p->ioH, uiFindElementUuId( p->ioH, kVtDeviceSelectId), dm->appId );

      // set the table menu
      uiSendValue( p->ioH, io::uiFindElementUuId( p->ioH, kVtTableSelectId ), t->appId );

      // set the 'default' check box
      uiSendValue( p->ioH, io::uiFindElementUuId( p->ioH, kVtDefaultCheckId ), t->defaultFl );

      // Set the 'duplicate name' based on the new table
      _set_duplicate_name(p,t->name);

      _set_status(p,kOkRC,"'%s' loaded.",t->name);

    errLabel:
      return rc;
    }

    cw::rc_t _duplicate( vtbl_t* p )
    {
      rc_t rc = kOkRC;

      // a name for the new table must have been given
      if( textLength(p->duplicateName) == 0 )
      {
        rc = _set_status(p,kInvalidArgRC, "Enter a 'name' for the new table.");
      }
      else
      {
        // the name of the table must be unique
        if( _table_from_name(p,p->duplicateName) != nullptr )
          rc = _set_status(p,kInvalidArgRC,"'%s' is not a unique table name.",p->duplicateName);
        else
        {
          // create a new table
          tbl_t* new_tbl = mem::allocZ<tbl_t>();

          // duplicate 'curTable' into the new table
          //_duplicateTable(p, *new_tbl, &p->curTable, p->duplicateName);
          p->curTable = new_tbl;

          // link in the new table
          _link_in_table(p, new_tbl );

          // load the new table
          _load(p,new_tbl->appId);
        }
      }
      return rc;
    }
    
    cw::rc_t _set_device( vtbl_t* p, unsigned devAppId )
    {
      rc_t rc = kOkRC;

      const dev_map_t* dm = _dev_map_from_appId(devAppId);

      cwAssert( dm != nullptr );
      
      p->curTable->mrpDevIdx = dm->mrpDevIdx;
      
      return rc;
    }

    cw::rc_t _set_default_check( vtbl_t* p, unsigned defaultFl )
    {
      cw::rc_t rc = kOkRC;

      if( defaultFl )
      {
        tbl_t* t;
        for(t=p->tableL; t!=nullptr; t=t->link)
          if( t->mrpDevIdx == p->curTable->mrpDevIdx )
            t->defaultFl = false;
      }

      p->curTable->defaultFl = defaultFl;
      
      return rc;
    }

    cw::rc_t _set_pitch( vtbl_t* p, unsigned vseqPitch )
    {
      rc_t rc = kOkRC;
      if( vseqPitch < 128)
        p->vseqPitch = vseqPitch;
      else
      {
        rc = _set_status(p,kInvalidArgRC,"%i is not a valid MIDI pitch.",vseqPitch);
      }
      return rc;
    }

    cw::rc_t _validate_midi_value( vtbl_t* p, unsigned midiValue )
    {
      if( midiValue < 128 )
        return kOkRC;

      return _set_status(p,kInvalidArgRC,"%i is an invalid 8 bit MIDI value.",midiValue);
    }

    uint8_t _cast_int_to_8bits( vtbl_t* p, unsigned value )
    {
      uint8_t v = 0;
      
      if( _validate_midi_value(p,value) == kOkRC )
      {
        v = (uint8_t)value;
      }
      else
      {
        v = 127;
      }

      return v;
    }

    cw::rc_t _set_velocity( vtbl_t* p, unsigned midiVel )
    {
      rc_t rc = kOkRC;
      if((rc = _validate_midi_value(p,midiVel)) == kOkRC )
      {
        p->pseqVelocity = midiVel;
      }
      return rc;
    }

    cw::rc_t _set_min_pitch( vtbl_t* p, unsigned midiPitch )
    {
      rc_t rc = kOkRC;
      if((rc = _validate_midi_value(p,midiPitch)) == kOkRC )
      {
        p->minPseqPitch = midiPitch;
      }
      return rc;
    }

    cw::rc_t _set_max_pitch( vtbl_t* p, unsigned midiPitch )
    {
      rc_t rc = kOkRC;
      if((rc = _validate_midi_value(p,midiPitch)) == kOkRC )
      {
        p->maxPseqPitch = midiPitch;
      }
      return rc;
    }

    cw::rc_t _set_inc_pitch( vtbl_t* p, unsigned midiPitch )
    {
      rc_t rc = kOkRC;
      if((rc = _validate_midi_value(p,midiPitch)) == kOkRC )
      {      
        p->incPseqPitch = midiPitch;
      }
      return rc;
    }

    cw::rc_t _set_table_entry( vtbl_t* p, unsigned table_idx, unsigned value )
    {
      rc_t rc = kOkRC;
      if( table_idx >= p->curTable->tableN )
      {
        rc = _set_status(p,kInvalidArgRC,"The table index %i is not valid.",table_idx);
      }
      else
      {
        if((rc = _validate_midi_value(p,value)) == kOkRC )
        {
          p->curTable->tableA[ table_idx ] = value;
          _set_status(p,kOkRC,"The table index '%i' was set to the value '%i'.",table_idx,value);
        }
      }
      
      return rc;
    }
  }
}

unsigned cw::vtbl::get_ui_id_map_count()
{ return sizeof(mapA)/sizeof(mapA[0]); };

const cw::ui::appIdMap_t* cw::vtbl::get_ui_id_map( unsigned panelAppId )
{
  unsigned mapN = get_ui_id_map_count();
  for(unsigned i=0; i<mapN; ++i)
    if( mapA[i].parentAppId == kInvalidId )
      mapA[i].parentAppId = panelAppId;
  
  return mapA;
}

cw::rc_t cw::vtbl::create( handle_t&                  hRef,
                           io::handle_t               ioH,
                           midi_record_play::handle_t mrpH,
                           const char*                cfg_fname,
                           const char*                cfg_backup_dir)
{
  rc_t      rc  = kOkRC;
  vtbl_t*   p   = nullptr;
  object_t* cfg = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  p = mem::allocZ<vtbl_t>();

  if((rc = objectFromFile( cfg_fname, cfg )) != kOkRC )
  {
    rc = cwLogError(rc,"The velocity table tuner cfg. file open failed on '%s'.",cwStringNullGuard(cfg_fname));
    goto errLabel;
  }
  
  p->mrpH                = mrpH;
  p->ioH                 = ioH;
  p->cfg_fname           = mem::duplStr(cfg_fname);
  p->cfg_backup_dir      = mem::duplStr(cfg_backup_dir);
  p->nextTableAppId      = kLoadOptionBaseId;

  if((rc = _parseCfg(p, cfg )) != kOkRC )
  {
    rc = cwLogError(rc,"Velocity table cfg. parsing failed.");
    goto errLabel;
  }
  
  hRef.set(p);
  
 errLabel:
  if( rc != kOkRC )
    _destroy(p);

  if( cfg != nullptr )
    cfg->free();

  return rc;
}

cw::rc_t cw::vtbl::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  vtbl_t* p = _handleToPtr(hRef);
  
  if((rc = _destroy(p)) != kOkRC )
    goto errLabel;

  hRef.clear();

 errLabel:
  return rc;
}

cw::rc_t cw::vtbl::on_ui_value( handle_t h, const io::ui_msg_t& m )
{
  rc_t rc = kOkRC;

  vtbl_t* p = _handleToPtr(h);

  switch( m.appId )
  {
    case kVtTableSelectId:
      rc = vtbl::_load(p,m.value->u.u);
      break;

    case kVtDeviceSelectId:
      rc = vtbl::_set_device(p, m.value->u.u );
      break;

    case kVtDefaultCheckId:
      rc = vtbl::_set_default_check(p, m.value->u.b );
      break;
          
    case kVtPianoDevId:
      rc = vtbl::_set_device(p,midi_record_play::kPiano_MRP_DevIdx );
      break;

    case kVtSamplerDevId:
      rc = vtbl::_set_device(p,midi_record_play::kSampler_MRP_DevIdx );
      break;
          
    case kVtPlayVelSeqBtnId:
      rc = vtbl::_play_vel_sequence(p);
      break;

    case kVtPitchId:
      rc = vtbl::_set_pitch(p,m.value->u.u);
      break;
          
    case kVtPlayPitchSeqBtnId:
      rc = vtbl::_play_pitch_sequence(p);
      break;

    case kVtVelocityId:
      rc = vtbl::_set_velocity(p,m.value->u.u);
      break;

    case kVtMinPitchId:
      rc = vtbl::_set_min_pitch(p,m.value->u.u);
      break;
      
    case kVtMaxPitchId:
      rc = vtbl::_set_max_pitch(p,m.value->u.u);
      break;
      
    case kVtIncPitchId:
      rc =  vtbl::_set_inc_pitch(p,m.value->u.u);
      break;
          
    case kVtApplyBtnId:
      rc = vtbl::_apply(p);
      break;
          
    case kVtSaveBtnId:
      rc = vtbl::_save(p);
      break;

    case kVtDuplicateBtnId:
      rc = vtbl::_duplicate(p);
      break;

    case kVtNameStrId:
      rc = vtbl::_set_duplicate_name(p,m.value->u.s);
      break;
      
    default:
      if( kVtEntry0 <= m.appId &&  m.appId <= kVtEntry24 )
      {
        rc = vtbl::_set_table_entry(p, m.appId - kVtEntry0, m.value->u.u );
        break;
      }

      /*
      if( kLoadOptionBaseId <= m.appId && m.appId < _table_count(p) )
      {
        printf("loader:%i\n", m.appId - kLoadOptionBaseId );
      }
      */
  }

  return rc;
}

cw::rc_t cw::vtbl::on_ui_echo( handle_t h, const io::ui_msg_t& m )
{
  rc_t rc = kOkRC;
  vtbl_t* p = _handleToPtr(h);
  
  switch( m.appId )
  {
    case kVtTableSelectId:
      if( !p->initUiFl )
      {
        // BUG BUG BUG: if multiple UI's are connected this is not the appropriate
        // response - echo should simply send the value, not change the state
        // of the vtbl
        
        if( _table_count(p) > 0 )
          if(_load( p, kLoadOptionBaseId ) == kOkRC )
            p->initUiFl = true;
      }
      break;

    case kVtDefaultCheckId:      
      rc = io::uiSendValue(p->ioH, m.uuId, p->curTable->defaultFl );      
      break;
      
    case kVtPitchId:
      rc = io::uiSendValue(p->ioH, m.uuId, p->vseqPitch );      
      break;      
    case kVtVelocityId:
      rc = io::uiSendValue(p->ioH, m.uuId, p->pseqVelocity );
      break;
    case kVtMinPitchId:
      rc = io::uiSendValue(p->ioH, m.uuId, p->minPseqPitch );
      break;
    case kVtMaxPitchId:
      rc = io::uiSendValue(p->ioH, m.uuId, p->maxPseqPitch );
      break;
    case kVtIncPitchId:
      rc = io::uiSendValue(p->ioH, m.uuId, p->incPseqPitch );
      break;
  }

  return rc;
}

cw::rc_t cw::vtbl::exec( handle_t h )
{
  rc_t rc = kOkRC;

  vtbl_t* p = _handleToPtr(h);
  time::spec_t t0;

  if( p->state != kStoppedStateId  )
  {
    time::get(t0);
    if( time::isGTE(t0,p->nextTime) )
    {
      unsigned pitch = 0;
      unsigned vel   = 0;
      
      switch( p->state )
      {
        case kNoteOnStateId:
          {
            p->state = kNoteOffStateId;

            time::advanceMs(p->nextTime,p->noteOnDurMs); 
            
            switch( p->mode )
            {
              case kVelSeqModeId:
                pitch = p->vseqPitch;
                cwAssert( p->nextVelIdx < p->curTable->tableN );
                vel = p->curTable->tableA[ p->nextVelIdx ];
                break;
                
              case kPitchSeqModeId:
                vel   = p->pseqVelocity;
                pitch = p->nextPitch;
                break;
                
              default:
                cwAssert(0);
            }
          }
          break;
      
        case kNoteOffStateId:
          {
            vel      = 0;
            p->state = kNoteOnStateId;

            time::advanceMs(p->nextTime,p->noteOffDurMs); 
            
            switch( p->mode )
            {
              case kVelSeqModeId:
                {
                  pitch    = p->vseqPitch;

                  if( p->nextVelIdx + 1 >= p->curTable->tableN || p->waitForStopFl )
                    p->state = kStoppedStateId;
                  else
                    p->nextVelIdx += 1;
                }                
                break;
                
              case kPitchSeqModeId:
                {
                  pitch = p->nextPitch;
                  
                  if( p->nextPitch + p->incPseqPitch > p->maxPseqPitch || p->waitForStopFl )
                    p->state = kStoppedStateId;
                  else
                    p->nextPitch += p->incPseqPitch;
                }
                break;

              default:
                cwAssert(0);
                
            }
          }
          break;
      
        default:
          cwAssert(0);
      }

      _set_status(p,kOkRC,"state:%i mode:%i wfs_fl:%i : dev:%i : pitch:%i vel:%i : nxt %i %i",
                  p->state,p->mode,p->waitForStopFl,
                  p->curTable->mrpDevIdx,
                  pitch,vel,
                  p->nextPitch,p->nextVelIdx);

      send_midi_msg( p->mrpH,
                     p->curTable->mrpDevIdx,
                     0,
                     midi::kNoteOnMdId,
                     _cast_int_to_8bits(p,pitch),
                     _cast_int_to_8bits(p,vel) );
      
    }
  }
  return rc;
}

const uint8_t* cw::vtbl::get_vel_table( handle_t h, const char* label, unsigned& velTblN_Ref )
{
  vtbl_t* p = _handleToPtr(h);

  const tbl_t* t= nullptr;

  velTblN_Ref = 0;
  
  if((t = _table_from_name( p, label )) == nullptr )
  {
    cwLogError(kInvalidArgRC,"The velocity table named:'%s' could not be found.",cwStringNullGuard(label));
    return nullptr;
  }

  velTblN_Ref = t->tableN;
  
  return t->tableA;
  
}
