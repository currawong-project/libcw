//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwMidi.h"
#include "cwMidiFile.h"
#include "cwText.h"
#include "cwCsv.h"

#ifdef cwBIG_ENDIAN
#define mfSwap16(v)  (v)
#define mfSwap32(v)  (v)
#else
#define mfSwap16(v)  cwSwap16(v)
#define mfSwap32(v)  cwSwap32(v)
#endif

namespace cw
{
  namespace midi
  {
    namespace file
    {
      
      typedef struct
      {
        unsigned    cnt;   // count of track records
        trackMsg_t* base;  // pointer to first track recd
        trackMsg_t* last;  // pointer to last track recd
      } track_t;

      typedef struct file_str
      {
        cw::file::handle_t fh;                 // file handle (only used in open() and write())
        unsigned short     fmtId;              // midi file type id: 0,1,2
        unsigned short     ticksPerQN;         // ticks per quarter note or 0 if smpteFmtId is valid
        uint8_t            smpteFmtId;         // smpte format or 0 if ticksPerQN is valid
        uint8_t            smpteTicksPerFrame; // smpte ticks per frame or 0 if ticksPerQN is valid
        unsigned short     trkN;               // track count
        track_t*           trkV;               // track vector
        char*              fn;                 // file name or NULL if this object did not originate from a file
        unsigned           msgN;               // count of msg's in msgV[]
        trackMsg_t**       msgV;               // sorted msg list
        bool               msgVDirtyFl;        // msgV[] needs to be refreshed from trkV[] because new msg's were inserted.
        unsigned           nextUid;            // next available msg uid
      } file_t;

      const trackMsg_t** _msgArray( file_t* p  );

      file_t* _handleToPtr( handle_t h )
      { return handleToPtr<handle_t,file_t>(h); }

      rc_t _read8( file_t* mfp, uint8_t* p )
      {
        rc_t rc;
        if((rc = cw::file::readUChar(mfp->fh,p,1)) != kOkRC )  
          return cwLogError(rc,"MIDI byte read failed.");
        return kOkRC;
      }

      rc_t _read16( file_t* mfp, unsigned short* p )
      {
        rc_t rc;
        if((rc = cw::file::readUShort(mfp->fh,p,1)) != kOkRC )
          return cwLogError(rc,"MIDI short read failed.");

        *p = mfSwap16(*p);

        return kOkRC;
      }

      rc_t _read24( file_t* mfp, unsigned* p )
      {
        rc_t rc = kOkRC;
        
        *p = 0;
        int i = 0;
        for(; i<3; ++i)
        {
          unsigned char c;
          if((rc = cw::file::readUChar(mfp->fh,&c,1)) != kOkRC )  
            return cwLogError(rc,"MIDI 24 bit integer read failed.");
          *p = (*p << 8) + c;
        }

        //*p =mfSwap32(*p);

        return rc;
      }

      rc_t _read32( file_t* mfp, unsigned* p )
      {
        rc_t rc = kOkRC;
        if((rc = cw::file::readUInt(mfp->fh,p,1)) != kOkRC )  
          return cwLogError(rc,"MIDI integer read failed.");

        *p = mfSwap32(*p);

        return rc;
      }

      rc_t _readText( file_t* mfp, trackMsg_t* tmp, unsigned byteN )
      {
        rc_t rc = kOkRC;
        if( byteN == 0 )
          return kOkRC;

        char*  t = mem::allocZ<char>(byteN+1);
        t[byteN] = 0;
  
        if((rc = cw::file::readChar(mfp->fh,t,byteN)) != kOkRC )
        {
           rc = cwLogError(rc,"MIDI read text failed.");
           goto errLabel;
        }
        
        tmp->u.text  = t;
        tmp->byteCnt = byteN;
        tmp->flags |= kReleaseFl;
        
      errLabel:
        if( rc != kOkRC )
          mem::release(t);
            
        return rc;
      }

      rc_t _readRecd( file_t* mfp, trackMsg_t* tmp, unsigned byteN )
      {
        rc_t rc = kOkRC;
        char*  t = mem::allocZ<char>(byteN);
  
        if((rc = cw::file::readChar(mfp->fh,t,byteN)) != kOkRC )
        {
          rc = cwLogError(rc,"MIDI read record failed.");
          goto errLabel;          
        }
        
        tmp->byteCnt = byteN;
        tmp->u.voidPtr = t;
        tmp->flags |= kReleaseFl;

      errLabel:
        if( rc != kOkRC )
          mem::release(t);
        
        return rc;
      }

      rc_t _readVarLen( file_t* mfp, unsigned* p )
      {
        rc_t rc = kOkRC;
        unsigned char c;

        if((rc = cw::file::readUChar(mfp->fh,&c,1)) != kOkRC )  
          return cwLogError(rc,"MIDI read variable length integer failed.");
  
        if( !(c & 0x80) )
          *p = c;
        else
        {
          *p = c & 0x7f;

          do 
          {
            if((rc = cw::file::readUChar(mfp->fh,&c,1)) != kOkRC )  
              return cwLogError(rc,"MIDI read variable length integer failed.");
            
            *p = (*p << 7) + (c & 0x7f);
    
          }while( c & 0x80 );
        }

        return rc; 
      }

      trackMsg_t* _allocMsg( file_t* mfp, unsigned short trkIdx, unsigned dtick, uint8_t status )
      {
        trackMsg_t* tmp = mem::allocZ<trackMsg_t>();

        // set the generic track record fields
        tmp->dtick    = dtick;
        tmp->status   = status;
        tmp->metaId   = kInvalidMetaMdId;
        tmp->trkIdx   = trkIdx;
        tmp->byteCnt  = 0;
        tmp->uid      = mfp->nextUid++;
        
        return tmp;
      }

      rc_t _appendTrackMsg( file_t* mfp, unsigned short trkIdx, unsigned dtick, uint8_t status, trackMsg_t** trkMsgPtrPtr )
      {
        trackMsg_t* tmp = _allocMsg( mfp, trkIdx, dtick, status );
    
        // link new record onto track record chain
        if( mfp->trkV[trkIdx].base == NULL )
          mfp->trkV[trkIdx].base = tmp;
        else
          mfp->trkV[trkIdx].last->link = tmp;

        mfp->trkV[trkIdx].last = tmp;
        mfp->trkV[trkIdx].cnt++;


        *trkMsgPtrPtr = tmp;

        return kOkRC;
      }

      rc_t _readSysEx( file_t* mfp, trackMsg_t* tmp, unsigned byteN )
      {
        rc_t     rc = kOkRC;
        uint8_t b  = 0;

        if( byteN == kInvalidCnt )
        {

          long offs;
          if( (rc = cw::file::tell(mfp->fh,&offs)) != kOkRC )
            return cwLogError(rc,"MIDI File 'tell' failed.");

          byteN = 0;

          // get the length of the sys-ex msg
          while( !cw::file::eof(mfp->fh) && (b != kSysComEoxMdId) )
          {
            if((rc = _read8(mfp,&b)) != kOkRC )
              return rc;
   
            ++byteN;
          }

          // verify that the EOX byte was found
          if( b != kSysComEoxMdId )
            return cwLogError(kSyntaxErrorRC,"MIDI file missing 'end-of-sys-ex'.");

          // rewind to the beginning of the msg
          if((rc = cw::file::seek(mfp->fh,cw::file::kBeginFl,offs)) != kOkRC )
            return cwLogError(rc,"MIDI file seek failed on sys-ex read.");

        }

        // allocate memory to hold the sys-ex msg
        uint8_t* mp = mem::allocZ<uint8_t>( byteN );

        // read the sys-ex msg from the file into msg memory
        if((rc = cw::file::readUChar(mfp->fh,mp,byteN)) != kOkRC )
        {
          rc = cwLogError(rc,"MIDI sys-ex read failed.");
          goto errLabel;
        }
        
        tmp->byteCnt     = byteN;
        tmp->u.sysExPtr  = mp;
        tmp->flags      |= kReleaseFl;

      errLabel:
        if( rc != kOkRC )
          mem::release(mp);
  
        return rc;
      }

      rc_t _readChannelMsg( file_t* mfp, uint8_t* rsPtr, uint8_t status, trackMsg_t* tmp )
      {
        rc_t     rc       = kOkRC;
        chMsg_t* p        = mem::allocZ<chMsg_t>();
        unsigned useRsFl  = status <= 0x7f;
        uint8_t  statusCh = useRsFl ? *rsPtr : status;
  
        if( useRsFl )
          p->d0  = status;    
        else
          *rsPtr = status;

        tmp->byteCnt = sizeof(chMsg_t);
        tmp->status  = statusCh & 0xf0;
        p->ch        = statusCh & 0x0f;
        p->durMicros = 0;

        unsigned byteN = statusToByteCount(tmp->status);
  
        if( byteN==kInvalidMidiByte || byteN > 2 )
        {
          rc = cwLogError(kSyntaxErrorRC,"Invalid status:0x%x %i byte cnt:%i.",tmp->status,tmp->status,byteN);
          goto errLabel;
        }
        
        unsigned i;
        for(i=useRsFl; i<byteN; ++i)
        {
          uint8_t* b = i==0 ? &p->d0 : &p->d1;
  
          if((rc = _read8(mfp,b)) != kOkRC )
            goto errLabel;
        }

        // convert note-on velocity=0 to note off
        if( tmp->status == kNoteOnMdId && p->d1==0 )
          tmp->status = kNoteOffMdId;

        tmp->u.chMsgPtr = p;
        tmp->flags |= kReleaseFl;
        
      errLabel:
        if( rc != kOkRC )
          mem::release(p);
          
        return rc;
      }

      rc_t _readMetaMsg( file_t* mfp, trackMsg_t* tmp )
      {
        uint8_t metaId;
        rc_t     rc;
        unsigned   byteN = 0;

        if((rc = _read8(mfp,&metaId)) != kOkRC )
          return rc;

        if((rc = _readVarLen(mfp,&byteN)) != kOkRC )
          return rc;

        //printf("mt: %i 0x%x n:%i\n",metaId,metaId,byteN);

        switch( metaId )
        {
          case kSeqNumbMdId:   rc = _read16(mfp,&tmp->u.sVal); break;
          case kTextMdId:      rc = _readText(mfp,tmp,byteN);  break;
          case kCopyMdId:      rc = _readText(mfp,tmp,byteN);  break;
          case kTrkNameMdId:   rc = _readText(mfp,tmp,byteN);  break;
          case kInstrNameMdId: rc = _readText(mfp,tmp,byteN);  break;
          case kLyricsMdId:    rc = _readText(mfp,tmp,byteN);  break;
          case kMarkerMdId:    rc = _readText(mfp,tmp,byteN);  break;
          case kCuePointMdId:  rc = _readText(mfp,tmp,byteN);  break;
          case kMidiChMdId:    rc = _read8(mfp,&tmp->u.bVal);  break;
          case kMidiPortMdId:  rc = _read8(mfp,&tmp->u.bVal);  break;
          case kEndOfTrkMdId:  break;
          case kTempoMdId:     rc = _read24(mfp,&tmp->u.iVal); break;
          case kSmpteMdId:     rc = _readRecd(mfp,tmp,sizeof(smpte_t));   break;
          case kTimeSigMdId:   rc = _readRecd(mfp,tmp,sizeof(timeSig_t)); break;
          case kKeySigMdId:    rc = _readRecd(mfp,tmp,sizeof(keySig_t));  break;
          case kSeqSpecMdId:   rc = _readSysEx(mfp,tmp,byteN); break;

          default:
            cw::file::seek(mfp->fh,cw::file::kCurFl,byteN);
            rc = cwLogError(kProtocolErrorRC,"Unknown meta status:0x%x %i.",metaId,metaId);
        }

        tmp->metaId = metaId;

        return rc;
      }


      rc_t _readTrack( file_t* mfp, unsigned short trkIdx )
      {
        rc_t     rc        = kOkRC;
        unsigned dticks    = 0;
        uint8_t  status;
        uint8_t  runstatus = 0;
        bool     contFl    = true;

        while( contFl && (rc==kOkRC))
        { 
          trackMsg_t* tmp = NULL;

          // read the tick count
          if((rc = _readVarLen(mfp,&dticks)) != kOkRC )
            return rc;

          // read the status byte
          if((rc = _read8(mfp,&status)) != kOkRC )
            return rc;

          //printf("%5i st:%i 0x%x\n",dticks,status,status);

          // append a track msg
          if((rc = _appendTrackMsg( mfp, trkIdx, dticks, status, &tmp )) != kOkRC )
            return rc;

          // switch on status
          switch( status )
          {
            // handle sys-ex msg
            case kSysExMdId:
              rc = _readSysEx(mfp,tmp,kInvalidCnt);
              break;

              // handle meta msg
            case kMetaStId:
              rc = _readMetaMsg(mfp,tmp);

              // ignore unknown meta messages
              if( rc == kProtocolErrorRC )
                rc = kOkRC;

              contFl = tmp->metaId != kEndOfTrkMdId;        
              break;

            default:
              // handle channel msg
              rc = _readChannelMsg(mfp,&runstatus,status,tmp);

          }
        }
        return rc;
      }


      rc_t _readHdr( file_t* mfp )
      {
        rc_t rc;
        unsigned fileId;
        unsigned chunkByteN;

        // read the file id 
        if((rc = _read32(mfp,&fileId)) != kOkRC )
          return rc;
  
        // verify the file id
        if( fileId != 'MThd' )
          return cwLogError(kInvalidDataTypeRC,"Not a MIDI file.");

        // read the file chunk byte count
        if((rc = _read32(mfp,&chunkByteN)) != kOkRC )
          return  rc;

        // read the format id
        if((rc = _read16(mfp,&mfp->fmtId)) != kOkRC )
          return rc;

        // read the track count
        if((rc = _read16(mfp,&mfp->trkN)) != kOkRC )
          return rc;

        // read the ticks per quarter note
        if((rc = _read16(mfp,&mfp->ticksPerQN)) != kOkRC )
          return rc;

        // if the division field was given in smpte
        if( mfp->ticksPerQN & 0x8000 )
        {
          mfp->smpteFmtId         = (mfp->ticksPerQN & 0x7f00) >> 8;
          mfp->smpteTicksPerFrame = (mfp->ticksPerQN & 0xFF);
          mfp->ticksPerQN         = 0;
        }

        // allocate and zero the track array
        if( mfp->trkN )
          mfp->trkV = mem::allocZ<track_t>(mfp->trkN);
  
        return rc;
      }

      void _drop( file_t* p )
      {
        unsigned i;
        unsigned n = 0;
        for(i=0; i<p->trkN; ++i)
        {
          track_t*   trk = p->trkV + i;
          trackMsg_t* m0  = NULL;
          trackMsg_t* m   = trk->base;
    
          for(; m!=NULL; m=m->link)
          {
            if( cwIsFlag(m->flags,kDropTrkMsgFl) )
            {
              ++n;
              if( m0 == NULL )
                trk->base = m->link;
              else
                m0->link = m->link;
            }
            else
            {
              m0 = m;
            }      
          }
        }
      }

      int _sortFunc( const void *p0, const void* p1 )
      {  
        if( (*(trackMsg_t**)p0)->atick == (*(trackMsg_t**)p1)->atick )
          return 0;

        return (*(trackMsg_t**)p0)->atick < (*(trackMsg_t**)p1)->atick ? -1 : 1;  
      }

      // Set the absolute accumulated ticks (atick) value of each track message.
      // The absolute accumulated ticks gives a global time ordering for all
      // messages in the file.
      void _setAccumulateTicks( file_t* p )
      {
        trackMsg_t* nextTrkMsg[ p->trkN ]; // next msg in each track
        unsigned long long atick = 0;
        unsigned          i;
  
        // iniitalize nextTrkMsg[] to the first msg in each track
        for(i=0; i<p->trkN; ++i)
          if((nextTrkMsg[i] =  p->trkV[i].base) != NULL )
            nextTrkMsg[i]->atick = nextTrkMsg[i]->dtick;

        while(1)
        {
          unsigned k = kInvalidIdx;

          // find the index of the track in nextTrkMsg[] which has the min atick
          for(i=0; i<p->trkN; ++i)
            if( nextTrkMsg[i]!=NULL && (k==kInvalidIdx || nextTrkMsg[i]->atick < nextTrkMsg[k]->atick) )
              k = i;

          // no next msg was found - we're done
          if( k == kInvalidIdx )
            break;

          // store the current atick
          atick = nextTrkMsg[k]->atick;

          // advance the selected track to it's next message
          nextTrkMsg[k] = nextTrkMsg[k]->link;

          // set the selected tracks next atick time 
          if( nextTrkMsg[k] != NULL )
            nextTrkMsg[k]->atick = atick + nextTrkMsg[k]->dtick;      
        }  
      }

      void _setAbsoluteTime( file_t* mfp )
      {
        const trackMsg_t** msgV          = _msgArray(mfp);
        double             microsPerQN   = 60000000.0/120.0; // default tempo;
        double             microsPerTick = microsPerQN / mfp->ticksPerQN;
        unsigned long long amicro        = 0;
        unsigned           i;


        for(i=0; i<mfp->msgN; ++i)
        {
          trackMsg_t* mp    = (trackMsg_t*)msgV[i]; // cast away const
          unsigned    dtick = 0;
    
          if( i > 0 )
          {
            // atick must have already been set and sortedh
            assert( mp->atick >= msgV[i-1]->atick );
            dtick = mp->atick -  msgV[i-1]->atick;
          }

          amicro     += microsPerTick * dtick;
          mp->amicro  = amicro;
    
    
          // track tempo changes
          if( mp->status == kMetaStId && mp->metaId == kTempoMdId )
            microsPerTick = (double)mp->u.iVal / mfp->ticksPerQN;
        }
  
      }

      rc_t _close( file_t* mfp )
      {
        rc_t rc = kOkRC;

        if( mfp == NULL )
          return rc;

        for(unsigned i=0; i<mfp->trkN; ++i)
        {
          trackMsg_t* t = mfp->trkV[i].base; 
          while( t!=nullptr )
          {
            trackMsg_t* t0 = t->link;
            
            if( cwIsFlag(t->flags,kReleaseFl) )
              mem::release((void*&)(t->u.voidPtr));
            
            mem::release(t);
            t = t0;
          }          
        }
        mem::release(mfp->trkV);
        mem::release(mfp->msgV);

        if( mfp->fh.isValid() )
          if((rc = cw::file::close( mfp->fh )) != kOkRC )
            rc = cwLogError(rc,"MIDI file close failed.");

        mem::release(mfp->fn);
        mem::release(mfp);

        return rc;
  
      }

      void _linearize_partial( file_t* mfp )
      {
        mfp->msgVDirtyFl = false;
        
        // get the total trk msg count
        mfp->msgN = 0;
        for(unsigned trkIdx=0; trkIdx<mfp->trkN; ++trkIdx)
          mfp->msgN += mfp->trkV[ trkIdx ].cnt;

        // allocate the trk msg index vector: msgV[]
        mfp->msgV = mem::resizeZ<trackMsg_t*>( mfp->msgV, mfp->msgN);

        // store a pointer to every trk msg in msgV[]
        for(unsigned i=0,j=0; i<mfp->trkN; ++i)
        {
          trackMsg_t* m = mfp->trkV[i].base;
    
          for(; m!=NULL; m=m->link)
          {
            assert( j < mfp->msgN );
      
            mfp->msgV[j++] = m;
          }
        }
      }

      void _linearize( file_t* mfp )
      {
        if( mfp->msgVDirtyFl == false )
          return;

        mfp->msgVDirtyFl = false;

        _linearize_partial(mfp);
        
        // set the atick value in each msg
        _setAccumulateTicks(mfp);
    
        // sort msgV[] in ascending order on atick
        qsort( mfp->msgV, mfp->msgN, sizeof(trackMsg_t*), _sortFunc );

        // set the amicro value in each msg
        _setAbsoluteTime(mfp);

  
      }

      // Note that p->msgV[] should always be accessed through this function
      // to guarantee that the p->msgVDirtyFl is checked and msgV[] is updated
      // in case msgV[] is out of sync (due to inserted msgs (see insertTrackMsg())
      // with trkV[].
      const trackMsg_t** _msgArray( file_t* p  )
      {
        _linearize(p);
  
        // this cast is needed to eliminate an apparently needless 'incompatible type' warning
        return (const trackMsg_t**)p->msgV;
      }

      rc_t _create( handle_t& hRef )
      {
        rc_t    rc = kOkRC;
        file_t* p  = NULL;
  
        if((rc = close(hRef)) != kOkRC )
          return rc;
  
        // allocate the midi file object 
        if(( p = mem::allocZ<file_t>()) == NULL )
          return rc = cwLogError(kObjAllocFailRC,"MIDI file memory allocation failed.");
    
        if( rc != kOkRC )
          _close(p);
        else
          hRef.set(p);
        
        return rc;
  
      }

      void _init( file_t* p, unsigned trkN, unsigned ticksPerQN )
      {
        p->ticksPerQN = ticksPerQN;
        p->fmtId      = 1;
        p->trkN       = trkN;
        p->trkV       = mem::allocZ<track_t>(p->trkN);
      }
      
      rc_t _write8( file_t* mfp, unsigned char v )
      {
        rc_t rc = kOkRC;

        if( (rc = cw::file::writeUChar(mfp->fh,&v,1)) != kOkRC )
          rc = cwLogError(rc,"MIDI file byte write failed.");

        return rc;  
      }

      rc_t _write16( file_t* mfp, unsigned short v )
      {
        rc_t rc = kOkRC;

        v = mfSwap16(v);

        if((rc = cw::file::writeUShort(mfp->fh,&v,1)) != kOkRC )
          rc = cwLogError(rc,"MIDI file short integer write failed.");

        return rc;
      }

      rc_t _write24( file_t* mfp, unsigned v )
      {
        rc_t rc = kOkRC;
        unsigned mask = 0xff0000;
        int      i;

        for(i = 2; i>=0; --i)
        {
          unsigned char c = (v & mask) >> (i*8);
          mask >>= 8;

          if((rc = cw::file::writeUChar(mfp->fh,&c,1)) != kOkRC )
          {
            rc = cwLogError(rc,"MIDI file 24 bit integer write failed.");
            goto errLabel;
          }
    
        }

      errLabel:
        return rc;
      }

      rc_t _write32( file_t* mfp, unsigned v )
      {
        rc_t rc = kOkRC;

        v = mfSwap32(v);

        if((rc = cw::file::writeUInt(mfp->fh,&v,1)) != kOkRC )
          rc = cwLogError(rc,"MIDI file integer write failed.");

        return rc;
      }

      rc_t _writeRecd( file_t* mfp, const void* v, unsigned byteCnt )
      {
        rc_t rc = kOkRC;

        if((rc = cw::file::writeChar(mfp->fh,(const char*)v,byteCnt)) != kOkRC )  
          rc = cwLogError(rc,"MIDI file write record failed.");

        return rc;
      }

      rc_t _writeVarLen( file_t* mfp, unsigned v )
      {
        rc_t rc  = kOkRC;
        unsigned buf = v & 0x7f;
 
        while((v >>= 7) > 0 )
        {
          buf <<= 8;          
          buf |= 0x80;
          buf += (v & 0x7f);
        }

        while(1)
        {
          unsigned char c = (unsigned char)(buf & 0xff);
          if((rc = cw::file::writeUChar(mfp->fh,&c,1)) != kOkRC )
          {
            rc = cwLogError(rc,"MIDI file variable length integer write failed.");
            goto errLabel;
          }

          if( buf & 0x80 )
            buf >>= 8;
          else
            break;
        }

      errLabel:
        return rc;
      }

      rc_t _writeHdr( file_t* mfp )
      {
        rc_t rc;
        unsigned fileId = 'MThd';
        unsigned chunkByteN = 6;

        // write the file id ('MThd')
        if((rc = _write32(mfp,fileId)) != kOkRC )
          return rc;
  
        // write the file chunk byte count (always 6)
        if((rc = _write32(mfp,chunkByteN)) != kOkRC )
          return  rc;

        // write the MIDI file format id (0,1,2)
        if((rc = _write16(mfp,mfp->fmtId)) != kOkRC )
          return rc;

        // write the track count
        if((rc = _write16(mfp,mfp->trkN)) != kOkRC )
          return rc;

        unsigned short v = 0;

        // if the ticks per quarter note field is valid ...
        if( mfp->ticksPerQN )
          v = mfp->ticksPerQN;
        else
        {
          // ... otherwise the division field was given in smpte
          v = mfp->smpteFmtId << 8;
          v += mfp->smpteTicksPerFrame;    
        }

        if((rc = _write16(mfp,v)) != kOkRC )
          return rc;

        return rc;

      }


      rc_t _writeSysEx( file_t* mfp, trackMsg_t* tmp )
      {
        rc_t rc = kOkRC;

        if((rc = _write8(mfp,kSysExMdId)) != kOkRC )
          goto errLabel;

        if((rc = cw::file::writeUChar(mfp->fh,tmp->u.sysExPtr,tmp->byteCnt)) != kOkRC )
          rc = cwLogError(rc,"Sys-ex msg write failed.");

      errLabel:
        return rc;
      }

      rc_t _writeChannelMsg( file_t* mfp, const trackMsg_t* tmp, uint8_t* runStatus )
      {
        rc_t     rc    = kOkRC;
        unsigned     byteN = statusToByteCount(tmp->status);
        uint8_t status = tmp->status + tmp->u.chMsgPtr->ch;

        if( status != *runStatus )
        {
          *runStatus = status;
          if((rc = _write8(mfp,status)) != kOkRC )
            goto errLabel;
        }

        if(byteN>=1)
          if((rc = _write8(mfp,tmp->u.chMsgPtr->d0)) != kOkRC )
            goto errLabel;

        if(byteN>=2)
          if((rc = _write8(mfp,tmp->u.chMsgPtr->d1)) != kOkRC )
            goto errLabel;

      errLabel:
        return rc;
      }

      rc_t _writeMetaMsg( file_t* mfp, const trackMsg_t* tmp )
      {
        rc_t rc;

        if((rc = _write8(mfp,kMetaStId)) != kOkRC )
          return rc;

        if((rc = _write8(mfp,tmp->metaId)) != kOkRC )
          return rc;


        switch( tmp->metaId )
        {
          case kSeqNumbMdId:
            if((rc = _write8(mfp,sizeof(tmp->u.sVal))) == kOkRC )
              rc                                                  = _write16(mfp,tmp->u.sVal);
            break;

          case kTempoMdId:
            if((rc = _write8(mfp,3)) == kOkRC )
              rc = _write24(mfp,tmp->u.iVal); 
            break;

          case kSmpteMdId:
            if((rc = _write8(mfp,sizeof(smpte_t))) == kOkRC )
              rc   = _writeRecd(mfp,tmp->u.smptePtr,sizeof(smpte_t));
            break;
          
          case kTimeSigMdId:
            if((rc = _write8(mfp,sizeof(timeSig_t))) == kOkRC )
              rc   = _writeRecd(mfp,tmp->u.timeSigPtr,sizeof(timeSig_t));
            break;

          case kKeySigMdId:
            if((rc = _write8(mfp,sizeof(keySig_t))) == kOkRC )
              rc   = _writeRecd(mfp,tmp->u.keySigPtr,sizeof(keySig_t));
            break;

          case kSeqSpecMdId:
            if((rc = _writeVarLen(mfp,sizeof(tmp->byteCnt))) == kOkRC )
              rc   = _writeRecd(mfp,tmp->u.sysExPtr,tmp->byteCnt);
            break;

          case kMidiChMdId: 
            if((rc = _write8(mfp,sizeof(tmp->u.bVal))) == kOkRC )
              rc  = _write8(mfp,tmp->u.bVal);
            break;

          case kMidiPortMdId: 
            if((rc = _write8(mfp,sizeof(tmp->u.bVal))) == kOkRC )
              rc  = _write8(mfp,tmp->u.bVal);
            break;
            
          case kEndOfTrkMdId:  
            rc = _write8(mfp,0);
            break;

          case kTextMdId:      
          case kCopyMdId:      
          case kTrkNameMdId:   
          case kInstrNameMdId: 
          case kLyricsMdId:    
          case kMarkerMdId:    
          case kCuePointMdId:  
            {
              unsigned n = tmp->u.text==NULL ? 0 : strlen(tmp->u.text);
              if((rc     = _writeVarLen(mfp,n)) == kOkRC && n>0 )
                rc       = _writeRecd(mfp,tmp->u.text,n);
            }
            break;

          default:
            {
              // ignore unknown meta messages
            }

        }

        return rc;
      }

      rc_t _insertEotMsg( file_t* p, unsigned trkIdx )
      {
        track_t* trk = p->trkV + trkIdx;
        trackMsg_t* m0 = NULL;
        trackMsg_t* m = trk->base;

        // locate the current EOT msg on this track
        for(; m!=NULL; m=m->link)
        {
          if( m->status == kMetaStId && m->metaId == kEndOfTrkMdId )
          {
            // If this EOT msg is the last msg in the track  ...
            if( m->link == NULL )
            {
              assert( m == trk->last );
              return kOkRC; // ... then there is nothing else to do
            }

            // If this EOT msg is not the last in the track ...
            if( m0 != NULL )
              m0->link = m->link;  // ... then unlink it

            break;
          }

          m0 = m;
        }

        // if we get here then the last msg in the track was not an EOT msg

        // if there was no previously allocated EOT msg
        if( m == NULL )
        {
          m   = _allocMsg(p, trkIdx, 1, kMetaStId );
          m->metaId = kEndOfTrkMdId;
          trk->cnt += 1;
    
        }

        // link an EOT msg as the last msg on the track

        // if the track is currently empty
        if( m0 == NULL )
        {
          trk->base = m;
          trk->last = m;
        }
        else // link the msg as the last on on the track
        {
          assert( m0 == trk->last);
          m0->link = m;
          m->link  = NULL;
          trk->last = m;
        }

        return kOkRC;

      }

      rc_t _writeTrack( file_t* mfp, unsigned trkIdx )
      {
        rc_t          rc        = kOkRC;
        trackMsg_t* tmp       = mfp->trkV[trkIdx].base;
        uint8_t      runStatus = 0;

        // be sure there is a EOT msg at the end of this track
        if((rc = _insertEotMsg(mfp, trkIdx )) != kOkRC )
          return rc;
  
        for(; tmp != NULL; tmp=tmp->link)
        {
          // write the msg tick count
          if((rc = _writeVarLen(mfp,tmp->dtick)) != kOkRC )
            return rc;

          // switch on status
          switch( tmp->status )
          {
            // handle sys-ex msg
            case kSysExMdId:
              rc = _writeSysEx(mfp,tmp);
              break;

              // handle meta msg
            case kMetaStId:
              rc = _writeMetaMsg(mfp,tmp);
              break;

            default:
              // handle channel msg
              rc = _writeChannelMsg(mfp,tmp,&runStatus);

          }

        }

        return rc;
      }

      trackMsg_t*  _uidToMsg( file_t* mfp, unsigned uid )
      {
        unsigned i;
        const trackMsg_t** msgV = _msgArray(mfp);

        for(i=0; i<mfp->msgN; ++i)
          if( msgV[i]->uid == uid )
            return (trackMsg_t*)msgV[i];

        return NULL;
      }
      
      // Returns NULL if uid is not found or if it the first msg on the track.
      trackMsg_t*  _msgBeforeUid( file_t* p, unsigned uid )
      {
        trackMsg_t* m;
  
        if((m = _uidToMsg(p,uid)) == NULL )
          return NULL;

        assert( m->trkIdx < p->trkN );

        trackMsg_t* m0 = NULL;
        trackMsg_t* m1 = p->trkV[ m->trkIdx ].base;
        for(; m1!=NULL; m1 = m1->link)
        {
          if( m1->uid == uid )
            break;
          m0 = m1;
        }

        return m0;
      }

      unsigned _isMsgFirstOnTrack( file_t* p, unsigned uid )
      {
        unsigned i;
        for(i=0; i<p->trkN; ++i)
          if( p->trkV[i].base!=NULL && p->trkV[i].base->uid == uid )
            return i;
  
        return kInvalidIdx;   
      }

      unsigned _isEndMsg( trackMsg_t* m, trackMsg_t** endMsgArray, unsigned n )
      {
        unsigned i = 0;
        for(; i<n; ++i)
          if( endMsgArray[i] == m )
            return i;

        return kInvalidIdx;
      }

      bool _allEndMsgFound( trackMsg_t** noteMsgArray, unsigned n0, trackMsg_t** pedalMsgArray, unsigned n1 )
      {
        unsigned i=0;
        for(; i<n0; ++i)
          if( noteMsgArray[i] != NULL )
            return false;

        for(i=0; i<n1; ++i)
          if( pedalMsgArray[i] != NULL )
            return false;

        return true;
      }

      void _setDur( trackMsg_t* m0, trackMsg_t* m1 )
      {
        // calculate the duration of the sounding note
        ((chMsg_t*)m0->u.chMsgPtr)->durMicros = m1->amicro - m0->amicro;

        // set the note-off msg pointer
        ((chMsg_t*)m0->u.chMsgPtr)->end       = m1;
      }

      bool _calcNoteDur( trackMsg_t* m0, trackMsg_t* m1, int noteGateFl, int sustainGateFl, bool sostGateFl )
      {
        // if the note is being kept sounding because the key is still depressed,
        //    the sustain pedal is down or it is being held by the sostenuto pedal ....
        if( noteGateFl>0 || sustainGateFl>0 || sostGateFl )
          return false;  // ... do nothing

        _setDur(m0,m1);
  
        return true;
      }

      rc_t _testReport( const object_t* cfg )
      {
        rc_t        rc     = kOkRC;
        const char* fn     = nullptr;
      
        if((rc = cfg->getv("midiFn",fn)) != kOkRC )
          return cwLogError(kSyntaxErrorRC,"Invalid parameter to MIDI file report test.");
      
        char* mfn = filesys::expandPath(fn);
        rc = report(mfn,log::globalHandle() );
        mem::release(mfn);
      
        return rc;
      }

      rc_t _testGenCsv( const object_t* cfg )
      {
        rc_t        rc     = kOkRC;
        const char* midiFn = nullptr;
        const char* csvFn  = nullptr;
      
        if((rc = cfg->getv("midiFn",midiFn,"csvFn",csvFn)) != kOkRC )
          return cwLogError(kSyntaxErrorRC,"Invalid parameter to MIDI to CSV file conversion.");
      
        char* mfn = filesys::expandPath(midiFn);
        char* cfn = filesys::expandPath(csvFn);
        rc = genCsvFile(mfn,cfn );
        mem::release(mfn);
        mem::release(cfn);
      
        return rc;
      }

      rc_t _testOpenCsv( const object_t* cfg )
      {
        rc_t rc = kOkRC;
        rc_t rc1 = kOkRC;
        midi::file::handle_t mfH;
        const char* csvFn = nullptr;
        
        if((rc = cfg->getv("csvFn",csvFn)) != kOkRC )
          return cwLogError(kSyntaxErrorRC,"Invalid parameter to MIDI to CSV file conversion.");
        
        if(( rc = midi::file::open_csv(mfH,csvFn)) != kOkRC )
          goto errLabel;

        midi::file::printMsgs(mfH,log::globalHandle());

          
      errLabel:

        if((rc1 = close(mfH)) != kOkRC )
          rc1 = cwLogError(rc1,"MIDI file close failed on '%s'.",cwStringNullGuard(csvFn));
        
        return rcSelect(rc,rc1);
      }


      rc_t _testBatchConvert( const object_t* cfg )
      {
        rc_t rc;
        const char* io_dir = nullptr;
        const char* session_dir = nullptr;
        unsigned take_begin = 0;
        unsigned take_end = 0;
        const object_t* takeL = nullptr;
        const char* src_midi_fname = "midi.mid";
        const char* out_csv_fname = "midi.csv";
        unsigned take_cnt = 0;
        bool printWarningsFl = true;
        
        if((rc = cfg->getv("io_dir",io_dir,
                           "session_dir",session_dir,
                           "print_warnings_flag",printWarningsFl)) != kOkRC )
        {
          rc = cwLogError(rc,"Non-optional argument parse failed.");
          goto errLabel;
        }

        
        if((rc = cfg->getv_opt("src_midi_fname",src_midi_fname,
                               "out_csv_fname",out_csv_fname,
                               "take_begin",take_begin,
                               "take_end",take_end,
                               "takeL",takeL)) != kOkRC )
        {
          rc = cwLogError(rc,"Optional argument parse failed.");
          goto errLabel;
        }


        if( takeL == nullptr && take_begin == 0 && take_end == 0)
        {
          cwLogError(kInvalidArgRC,"No 'takeL' or 'take_begin/take_end' fields were provided.");
          goto errLabel;
        }

        if( takeL != nullptr )
        {
          take_cnt = takeL->child_count();
        }
        else
        {
          if( take_begin >= take_end )
          {
            rc = cwLogError(kInvalidArgRC,"The 'take_begin' value is >= 'take_end' value.");
            goto errLabel;
          }

          take_cnt = (take_end - take_begin) + 1;          
        }

        if( take_cnt == 0 )
        {
          rc = cwLogError(kInvalidArgRC,"The take number range is empty.");
          goto errLabel;
        }

        for(unsigned i=0; i<take_cnt; ++i)
        {
          unsigned take_numb;
          
          if( takeL != nullptr )
          {
            if((rc = takeL->child_ele(i)->value(take_numb)) != kOkRC )
            {
              rc = cwLogError(rc,"Invalid take number encountered.");
              goto errLabel;
            }
          }
          else
          {
            take_numb = take_begin + i;
          }
          
          char take_dir[32];
          snprintf(take_dir,32,"record_%i",take_numb);
          char* src_midi_fn =  filesys::makeFn(  io_dir, src_midi_fname, nullptr, session_dir, take_dir, nullptr );
          char* dst_csv_fn  =  filesys::makeFn(  io_dir, out_csv_fname, nullptr, session_dir, take_dir, nullptr );

          char* sm_fn = filesys::expandPath( src_midi_fn );
          char* dm_fn = filesys::expandPath( dst_csv_fn );
          
          //rc = genCsvFile(mfn,cfn );
          cwLogInfo("Midi to CSV: src:%s dst:%s", sm_fn,dm_fn);
          
          if((rc = genCsvFile(sm_fn, dm_fn, printWarningsFl )) != kOkRC )
            cwLogError(rc,"MIDI to CSV Conversion failed on %s to %s.",sm_fn,dm_fn);

          mem::release(sm_fn);
          mem::release(dm_fn);
        
          mem::release(src_midi_fn);
          mem::release(dst_csv_fn);
        }

      errLabel:
        if(rc != kOkRC )
          rc = cwLogError(rc,"MIDI batch convert to CSV failed.");
        return rc;
      }

      rc_t _testRptBeginEnd( const object_t* cfg )
      {
        rc_t rc;
        const char* midi_fname = nullptr;
        unsigned msg_cnt = 0;
        bool noteon_only_fl;
        if((rc = cfg->getv("midi_fname",midi_fname,
                           "note_on_only_fl", noteon_only_fl,
                           "msg_cnt",msg_cnt)) != kOkRC )
        {
          rc = cwLogError(rc,"MIDI file rpt beg/end arg. parse failed.");
          goto errLabel;
        }
        
        if((rc = report_begin_end(midi_fname,msg_cnt,noteon_only_fl)) != kOkRC )
        {
          rc = cwLogError(rc,"MIDI file rpt beg/end failed.");
          goto errLabel;
          
        }
      errLabel:
        return rc;
      }

      
      
    }
  }
}

cw::rc_t cw::midi::file::open( handle_t& hRef, const char* fn ){
  rc_t           rc     = kOkRC;  
  unsigned short trkIdx = 0;

  if((rc = _create(hRef)) != kOkRC )
    return rc;

  file_t* p    = _handleToPtr(hRef);

  // open the file
  if((rc = cw::file::open(p->fh,fn, cw::file::kReadFl | cw::file::kBinaryFl)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI file open failed.");
    goto errLabel;
  }

  // read header and setup track array
  if(( rc = _readHdr(p)) != kOkRC )
    goto errLabel;
  
  while( !cw::file::eof(p->fh) && trkIdx < p->trkN )
  {
    unsigned chkId = 0,chkN=0;

    // read the chunk id
    if((rc = _read32(p,&chkId)) != kOkRC )
      goto errLabel;

    // read the chunk size
    if((rc = _read32(p,&chkN)) != kOkRC )
      goto errLabel;

    // if this is not a trk chunk then skip it
    if( chkId != (unsigned)'MTrk')
    {
      //if( fseek( p->fp, chkN, SEEK_CUR) != 0 )
      if((rc = cw::file::seek(p->fh,cw::file::kCurFl,chkN)) != kOkRC )
      {
        rc = cwLogError(rc,"MIDI file seek failed.");
        goto errLabel;
      }
    }  
    else
    {
      if((rc = _readTrack(p,trkIdx)) != kOkRC )
        goto errLabel;

      ++trkIdx;
    }
  }

  // store the file name
  p->fn = mem::duplStr(fn);

  p->msgVDirtyFl = true;
  _linearize(p);
  
 errLabel:

  if((rc = cw::file::close(p->fh)) != kOkRC )
    rc = cwLogError(rc,"MIDI file close failed.");

  if( rc != kOkRC )
  {
    _close(p);
    hRef.clear();
  }
  
  return rc;
}

cw::rc_t cw::midi::file::open_csv( handle_t& hRef, const char* csv_fname )
{
  rc_t rc = kOkRC;
  csv::handle_t csvH;
  
  const char* titleA[] = { "uid","tpQN","bpm","dticks","ch","status","d0","d1" };
  unsigned    titleN   = sizeof(titleA)/sizeof(titleA[0]);
  
  unsigned    TpQN     = 1260;
  unsigned    BpM      = 60;
  unsigned    lineN    = 0;
  unsigned    line_idx = 0;
  
  //double      asecs    = 0;
  
  unsigned    uid      = kInvalidId;
  unsigned    dtick    = 0;
  unsigned    ch       = 0;
  unsigned    status   = 0;
  unsigned    d0       = 0;
  unsigned    d1       = 0;
  file_t*     p        = nullptr;

  unsigned aticks = 0;
  
  if((rc = _create(hRef)) != kOkRC )
    goto errLabel;

  if((p = _handleToPtr(hRef)) == nullptr )
    goto errLabel;
    
  if((rc = csv::create(csvH,csv_fname,titleA,titleN)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI CSV file open failed.");
    goto errLabel;
  }

  if((rc = line_count(csvH,lineN)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI CSV line count access failed.");
    goto errLabel;
  }

  
  for(; (rc = next_line(csvH)) == kOkRC; ++line_idx )
  {    
    if((rc = getv(csvH,"uid",uid,"tpQN",TpQN,"bpm",BpM,"dticks",dtick,"ch",ch,"status",status,"d0",d0,"d1",d1)) != kOkRC )
    {
      cwLogError(rc,"Error reading CSV line %i.",line_idx+1);
      goto errLabel;
    }

    //printf("%i %i tpqn:%i bpm:%i dtick:%i ch:%i st:%i d0:%i d1:%i\n",line_idx,uid,TpQN,BpM,dtick,ch,status,d0,d1);
    
    //double ticks_per_sec = TpQN * BpM / 60;
    //double dsecs         = dtick * ticks_per_sec;

    //asecs += dsecs;
    
    if( line_idx == 0 )
      _init(p,1,TpQN);

    aticks += dtick;

    if( BpM != 0 )
    {
      if((rc = insertTrackTempoMsg(hRef, 0, aticks, BpM )) != kOkRC )
      {
        rc = cwLogError(rc,"BPM insert failed.");
        goto errLabel;
      }
    }

    if( status != 0 )
    {
      if((rc = insertTrackChMsg(hRef, 0, aticks, ch+status, d0, d1 )) != kOkRC )
      {
        rc = cwLogError(rc,"Channel msg insert failed.");
        goto errLabel;
      }
    }
    
    TpQN   = 0;
    BpM    = 0;
    status = 0;
    dtick  = 0;
  }
  
  if( rc == kEofRC )
    rc = kOkRC;
  
errLabel:
    
  if( rc != kOkRC )
    close(hRef);
    
  destroy(csvH);
  return rc;
}

cw::rc_t cw::midi::file::open_csv_2( handle_t& hRef, const char* midi_csv_fname )
{
  rc_t          rc       = kOkRC;
  file_t*     p        = nullptr;
  csv::handle_t csvH;      
  const char*   titleA[] = { "UID","trk","dtick","atick","amicro","type","ch","D0","D1" };
  unsigned      titleN   = sizeof(titleA)/sizeof(titleA[0]);
  unsigned      line_idx = 0;
  unsigned      lineN    = 0;
  unsigned      trkN = 1;
  unsigned      trkIdx = 0;
  unsigned      TpQN     = 1260;

  if((rc = _create(hRef)) != kOkRC )
    goto errLabel;

  if((p = _handleToPtr(hRef)) == nullptr )
    goto errLabel;

  _init( p, trkN, TpQN );

  
  if((rc = csv::create(csvH,midi_csv_fname,titleA,titleN)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI CSV file open failed.");
    goto errLabel;
  }

  if((rc = line_count(csvH,lineN)) != kOkRC )
  {
    rc = cwLogError(rc,"MIDI CSV line count access failed.");
    goto errLabel;
  }
  
  for(; (rc = next_line(csvH)) == kOkRC; ++line_idx )
  {
    unsigned    uid;
    unsigned    amicro;
    unsigned    dtick;
    unsigned    atick;
    const char* type   = nullptr;
    unsigned    ch;
    unsigned    d0;
    unsigned    d1;
    uint8_t     status = 0;
    if((rc = getv(csvH,"UID",uid,"dtick",dtick,"atick",atick,"amicro",amicro,"type",type,"ch",ch,"D0",d0,"D1",d1)) != kOkRC )
    {
      cwLogError(rc,"Error reading CSV line %i.",line_idx+1);
      goto errLabel;
    }

    if(textIsEqual(type,"non"))
      status = midi::kNoteOnMdId;
    else
      if(textIsEqual(type,"nof"))
        status = midi::kNoteOffMdId;
      else
        if(textIsEqual(type,"ctl"))
          status = midi::kCtlMdId;
        else
          if(textIsEqual(type,"tempo"))
          {
            // note the tempo is taken from the 'ch' column
            if((rc = insertTrackTempoMsg(hRef, trkIdx, atick, ch )) != kOkRC )
            {
              rc = cwLogError(rc,"BPM insert failed.");
              goto errLabel;
            }

            continue;
            
          }
          else
            if(textIsEqual(type,"eot"))
            {
            }
            else
            {
              rc = cwLogError(kSyntaxErrorRC,"Unknown message type:'%s' on line index:%i.",cwStringNullGuard(type),line_idx);
              goto errLabel;
            }


    if( status != 0 )
    {
      assert( ch<=15 && d0 <= 127 && d1 <= 127 );

      if((rc = insertTrackChMsg(hRef, trkIdx, atick, status+ch, d0, d1 ) ) != kOkRC )
      {
        rc = cwLogError(rc,"Channel msg insert failed.");
        goto errLabel;
      }

      assert( p->trkV[trkIdx].last->atick == atick );
      
      // insert the time directly
      p->trkV[trkIdx].last->amicro = amicro;
      
    }
  }

  _linearize_partial(p);
  

errLabel:
  destroy(csvH);
      
  if( rc == kEofRC )
    rc = kOkRC;

  if( rc != kOkRC )
    rc = cwLogError(rc,"MIDI csv file parse failed on '%s'.",cwStringNullGuard(midi_csv_fname));
  return rc;      
}

cw::rc_t cw::midi::file::create( handle_t& hRef, unsigned trkN, unsigned ticksPerQN )
{
  rc_t       rc     = kOkRC;  

  if((rc = _create(hRef)) != kOkRC )
    return rc;

  file_t* p    = _handleToPtr(hRef);

  _init(p,trkN,ticksPerQN);
  
  return rc;
}


cw::rc_t  cw::midi::file::close( handle_t& hRef )
{
  rc_t rc = kOkRC;

  if( !hRef.isValid() )
    return kOkRC;
  
  file_t* p = _handleToPtr(hRef);

  if((rc = _close(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;
}

cw::rc_t   cw::midi::file::write( handle_t h, const char* fn )
{
  rc_t     rc  = kOkRC;
  file_t*  mfp = _handleToPtr(h);
  unsigned i;

  // create the output file
  if( (rc = cw::file::open(mfp->fh,fn,cw::file::kWriteFl)) != kOkRC )
    return cwLogError(rc,"The MIDI file '%s' could not be created.",cwStringNullGuard(fn));

  // write the file header
  if((rc = _writeHdr(mfp)) != kOkRC )
  {
    rc = cwLogError(rc,"The file header write failed on the MIDI file '%s'.",cwStringNullGuard(fn));
    goto errLabel;
  }

  for(i=0; i < mfp->trkN; ++i )
  {
    unsigned chkId = 'MTrk';
    long     offs0,offs1;

    // write the track chunk id ('MTrk')
    if((rc = _write32(mfp,chkId)) != kOkRC )
      goto errLabel;

    cw::file::tell(mfp->fh,&offs0);

    // write the track chunk size as zero
    if((rc = _write32(mfp,0)) != kOkRC )
      goto errLabel;

    if((rc = _writeTrack(mfp,i)) != kOkRC )
      goto errLabel;

    cw::file::tell(mfp->fh,&offs1);

    cw::file::seek(mfp->fh,cw::file::kBeginFl,offs0);

    _write32(mfp,offs1-offs0-4);

    cw::file::seek(mfp->fh,cw::file::kBeginFl,offs1);
    
  }

 errLabel:
  cw::file::close(mfp->fh);
  return rc;
}


unsigned    cw::midi::file::trackCount( handle_t h )
{
  file_t* mfp;

  if((mfp = _handleToPtr(h)) == NULL )
    return kInvalidCnt;

  return mfp->trkN;
}

unsigned    cw::midi::file::fileType( handle_t h )
{
  file_t* mfp;

  if((mfp = _handleToPtr(h)) == NULL )
    return kInvalidId;

  return mfp->fmtId;
}

const char*  cw::midi::file::filename( handle_t h )
{
  file_t* mfp;
  if((mfp = _handleToPtr(h)) == NULL )
    return NULL;
  return mfp->fn;
}

unsigned    cw::midi::file::ticksPerQN( handle_t h )
{
  file_t* mfp;

  if((mfp = _handleToPtr(h)) == NULL )
    return kInvalidCnt;

  return mfp->ticksPerQN;
}

uint8_t  cw::midi::file::ticksPerSmpteFrame( handle_t h )
{
  file_t* mfp;

  if((mfp = _handleToPtr(h)) == NULL )
    return kInvalidMidiByte;

  if( mfp->ticksPerQN != 0 )
    return 0;
 
  return mfp->smpteTicksPerFrame;
} 

uint8_t  cw::midi::file::smpteFormatId( handle_t h )
{ 
  file_t* mfp;

  if((mfp = _handleToPtr(h)) == NULL )
    return kInvalidMidiByte;

  if( mfp->ticksPerQN != 0 )
    return 0;
 
  return mfp->smpteFmtId;
}
    
unsigned    cw::midi::file::trackMsgCount( handle_t h, unsigned trackIdx )
{
  file_t* mfp;

  if((mfp = _handleToPtr(h)) == NULL )
    return kInvalidCnt;

  return mfp->trkV[trackIdx].cnt;
}


const cw::midi::file::trackMsg_t* cw::midi::file::trackMsg( handle_t h, unsigned trackIdx )
{
  file_t* mfp;

  if((mfp = _handleToPtr(h)) == NULL )
    return NULL;

  return mfp->trkV[trackIdx].base;
}

unsigned              cw::midi::file::msgCount( handle_t h )
{
  file_t* mfp;

  if((mfp = _handleToPtr(h)) == NULL )
    return kInvalidCnt;

  return mfp->msgN;
}


const cw::midi::file::trackMsg_t** cw::midi::file::msgArray(    handle_t h )
{
  file_t* mfp;

  if((mfp = _handleToPtr(h)) == NULL )
    return NULL;

  return _msgArray(mfp); 
}


cw::rc_t cw::midi::file::setVelocity( handle_t h, unsigned uid, uint8_t vel )
{
  trackMsg_t* r;
  file_t*    mfp = _handleToPtr(h);

  assert( mfp != NULL );

  if((r = _uidToMsg(mfp,uid)) == NULL )
    return cwLogError(kInvalidArgRC,"The MIDI file uid %i could not be found.",uid);

  if( midi::isNoteOnStatus(r->status) == false && midi::isNoteOff(r->status,(uint8_t)0)==false )
    return cwLogError(kInvalidArgRC,"Cannot set velocity on a non-Note-On/Off msg.");
  
  chMsg_t* chm = (chMsg_t*)r->u.chMsgPtr;

  chm->d1 = vel;

  return kOkRC;
}

cw::rc_t cw::midi::file::insertMsg( handle_t h, unsigned uid, int dtick, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 )
{
  file_t*    mfp    = _handleToPtr(h);
  assert( mfp != NULL );
  trackMsg_t* ref    = NULL;
  unsigned          trkIdx = kInvalidIdx;

  // if dtick is positive ...
  if( dtick >= 0 )
  {
    ref    = _uidToMsg(mfp,uid); // ... then get the ref. msg.
    trkIdx = ref->trkIdx;
  }
  else // if dtick is negative ...
  {
    // ... get get the msg before the ref. msg.
    if((ref = _msgBeforeUid(mfp,uid)) != NULL )
      trkIdx = ref->trkIdx;
    else
    {
      // ... the ref. msg was first in the track so there is no msg before it
      trkIdx = _isMsgFirstOnTrack(mfp,uid);
    }
  }

  // verify that the reference msg was found
  if( trkIdx == kInvalidIdx )
    return cwLogError(kInvalidArgRC,"The UID (%i) reference note could not be located.",uid);

  assert( trkIdx < mfp->trkN );

  // complete the msg setup
  track_t* trk   = mfp->trkV + trkIdx;
  trackMsg_t* m   = _allocMsg(mfp, trkIdx, abs(dtick), status );
  chMsg_t*    c   = mem::allocZ<chMsg_t>();

  m->u.chMsgPtr = c;
  m->flags |= kReleaseFl;
  
  c->ch   = ch;
  c->d0   = d0;
  c->d1   = d1;

  // if 'm' is prior to the first msg in the track
  if( ref == NULL )
  {
    // ... then make 'm' the first msg in the first msg
    m->link = trk->base;
    trk->base = m;
    // 'm' is before ref and the track cannot be empty (because ref is in it) 'm'
    // can never be the last msg in the list
  } 
  else // ref is the msg before 'm'
  {
    m->link   = ref->link;
    ref->link = m;
    
    // if ref was the last msg in the trk ...
    if( trk->last == ref )
      trk->last = m;  //... then 'm' is now the last msg in the trk
  }

  trk->cnt += 1;

  mfp->msgVDirtyFl = true;

  return kOkRC;
}

cw::rc_t  cw::midi::file::insertTrackMsg( handle_t h, unsigned trkIdx, const trackMsg_t* msg )
{
  file_t* p = _handleToPtr(h);

  // validate the track index
  if( trkIdx >= p->trkN )
    return cwLogError(kInvalidArgRC,"The track index (%i) is invalid.",trkIdx);

  // allocate a new track record
  trackMsg_t* m = (trackMsg_t*)mem::allocZ<char>(sizeof(trackMsg_t)+msg->byteCnt);

  // fill the track record
  m->uid     = p->nextUid++;
  m->atick   = msg->atick;
  m->amicro  = msg->amicro;
  m->status  = msg->status;
  m->metaId  = msg->metaId;
  m->trkIdx  = trkIdx;
  m->byteCnt = msg->byteCnt;
  memcpy(&m->u,&msg->u,sizeof(msg->u));

  // copy the exernal data
  if( msg->byteCnt > 0 )
  {
    m->u.voidPtr = (m+1);
    memcpy((void*)m->u.voidPtr,msg->u.voidPtr,msg->byteCnt);
  }

  trackMsg_t* m0 = NULL;                  // msg before insertion
  trackMsg_t* m1 = p->trkV[trkIdx].base;  // msg after insertion

  // locate the track record before and after the new msg based on 'atick' value
  for(; m1!=NULL; m1=m1->link)
  {
    if( m1->atick > m->atick )
    {      
      if( m0 == NULL )
        p->trkV[trkIdx].base = m;
      else
        m0->link = m;
      
      m->link = m1;
      break;
    }
    
    m0 = m1;    
  }

  // if the new track record was not inserted then it is the last msg
  if( m1 == NULL )
  {
    assert(m0 == p->trkV[trkIdx].last);
    
    // link in the new msg
    if( m0 != NULL )
      m0->link = m;

    // the new msg always becomes the last msg
    p->trkV[trkIdx].last = m;
    
    // if the new msg is the first msg inserted in this track
    if( p->trkV[trkIdx].base == NULL )
      p->trkV[trkIdx].base = m;    
  }

  // set the dtick field of the new msg
  if( m0 != NULL )
  {
    assert( m->atick >= m0->atick );
    m->dtick = m->atick - m0->atick;
  }

  // update the dtick field of the msg following the new msg
  if( m1 != NULL )
  {
    assert( m1->atick >= m->atick );
    m1->dtick = m1->atick - m->atick;
  }

  p->trkV[trkIdx].cnt += 1;  
  p->msgVDirtyFl = true;


  
  return kOkRC;
   
}

cw::rc_t  cw::midi::file::insertTrackChMsg( handle_t h, unsigned trkIdx, unsigned atick, uint8_t status, uint8_t d0, uint8_t d1 )
{
  trackMsg_t m;
  chMsg_t   cm;

  memset(&m,0,sizeof(m));
  memset(&cm,0,sizeof(cm));

  cm.ch = status & 0x0f;
  cm.d0 = d0;
  cm.d1 = d1;
  
  m.atick      = atick;
  m.status     = status & 0xf0;
  m.byteCnt    = sizeof(cm);
  m.u.chMsgPtr = &cm;

  assert( m.status >= kNoteOffMdId && m.status <= kPbendMdId );
  
  return insertTrackMsg(h,trkIdx,&m);
}

cw::rc_t  cw::midi::file::insertTrackTempoMsg( handle_t h, unsigned trkIdx, unsigned atick, unsigned bpm )
{
  trackMsg_t m;

  memset(&m,0,sizeof(m));

  m.atick      = atick;
  m.status     = kMetaStId;
  m.metaId     = kTempoMdId;
  m.u.iVal     = 60000000/bpm;  // convert BPM to microsPerQN
  
  return insertTrackMsg(h,trkIdx,&m);
}


unsigned  cw::midi::file::seekUsecs( handle_t h, unsigned long long offsUSecs, unsigned* msgUsecsPtr, unsigned* microsPerTickPtr )
{
  file_t* p;

  if((p = _handleToPtr(h)) == NULL )
    return kInvalidIdx;

  if( p->msgN == 0 )
    return kInvalidIdx;

  unsigned                 mi;
  double                   microsPerQN   = 60000000.0/120.0;
  double                   microsPerTick = microsPerQN / p->ticksPerQN;
  double                   accUSecs      = 0;
  const trackMsg_t** msgV          = _msgArray(p);
  
  for(mi=0; mi<p->msgN; ++mi)
  {
    const trackMsg_t* mp = msgV[mi];

    if( mp->amicro >= offsUSecs )
      break;
  }
  
  if( mi == p->msgN )
    return kInvalidIdx;

  if( msgUsecsPtr != NULL )
    *msgUsecsPtr = round(accUSecs - offsUSecs);

  if( microsPerTickPtr != NULL )
    *microsPerTickPtr = round(microsPerTick);

  return mi;
}

/*
  1.Move closest previous tempo msg to begin.
  2.The first msg in each track must be the first msg >= begin.time
  3.Remove all msgs > end.time - except the 'endMsg' for each note/pedal that is active at end time.


*/


double  cw::midi::file::durSecs( handle_t h )
{
  file_t* mfp = _handleToPtr(h);

  if( mfp->msgN == 0 )
    return 0;

  const trackMsg_t** msgV = _msgArray(mfp);
  
  return msgV[ mfp->msgN-1 ]->amicro / 1000000.0;
}

void cw::midi::file::calcNoteDurations( handle_t h, unsigned flags )
{
  file_t* p;
  bool warningFl = cwIsFlag(flags,kWarningsMfFl);
  
  if((p = _handleToPtr(h)) == NULL )
    return;

  if( p->msgN == 0 )
    return;

  unsigned    mi = kInvalidId;
  trackMsg_t* noteM[     kMidiNoteCnt * kMidiChCnt ]; // ptr to note-on or NULL if the note is not sounding
  trackMsg_t* sustV[                    kMidiChCnt ]; // ptr to last sustain pedal down msg or NULL if susteain pedal is not down
  trackMsg_t* sostV[                    kMidiChCnt ]; // ptr to last sost. pedal down msg or NULL if sost. pedal is not down
  int         noteGateM[ kMidiNoteCnt * kMidiChCnt ]; // true if the associated note key is depressed
  bool        sostGateM[ kMidiNoteCnt * kMidiChCnt ]; // true if the associated note was active when the sost. pedal went down
  int         sustGateV[ kMidiChCnt]; // true if the associated sustain pedal is down
  int         sostGateV[ kMidiChCnt]; // true if the associated sostenuto pedal is down
  unsigned    i,j;
  unsigned    n  = 0;
  
  const trackMsg_t** msgV = _msgArray(p);
  
  // initialize the state tracking variables
  for(i=0; i<kMidiChCnt; ++i)
  {
    sustV[i]     = NULL;
    sustGateV[i] = 0;
    
    sostV[i]     = NULL;
    sostGateV[i] = 0;
    
    for(j=0; j<kMidiNoteCnt; ++j)
    {
      noteM[     i*kMidiNoteCnt + j ] = NULL;
      noteGateM[ i*kMidiNoteCnt + j ] = 0;
      sostGateM[ i*kMidiNoteCnt + j ] = false;
    }
  }

  // for each midi event
  for(mi=0; mi<p->msgN; ++mi)
  {
    trackMsg_t* m = (trackMsg_t*)msgV[mi]; // cast away const

    // verify that time is also incrementing
    assert(  mi==0 || (mi>0 &&  m->amicro >= msgV[mi-1]->amicro) );

    // ignore all non-channel messages
    if(  !isChStatus( m->status ) )
      continue;

    uint8_t ch = m->u.chMsgPtr->ch; // get the midi msg channel
    uint8_t d0 = m->u.chMsgPtr->d0; // get the midi msg data value 

    // if this is a note-on msg
    if( isNoteOn(m) )
    {
      unsigned  k = ch*kMidiNoteCnt + d0;

      // if the note gate is not activated (i.e. key is up) but the note is still sounding (e.g. held by pedal)
      if( noteGateM[k] == 0 && noteM[k] != NULL )
      {
        //if( warningFl )
        //  cwLogWarning("%i : Missing note-off instance for note on:%s",m->uid,midi::midiToSciPitch(d0,NULL,0));

        if( cwIsFlag(flags,kDropReattacksMfFl) )
        {
          m->flags |= kDropTrkMsgFl;
          n += 1;
        }
          
      }
      // if this is a re-attack 
      if( noteM[k] != NULL )
        noteGateM[k] += 1;
      else // this is a new attack
      {
        noteM[k]     = m;
        noteGateM[k] = 1;
      }
    }
    else
      
      // if this is a note-off msg
      if( isNoteOff(m) )
      {
        unsigned            k = ch*kMidiNoteCnt + d0;
        trackMsg_t*  m0 = noteM[k];

        if( m0 == NULL )
        {
          if( warningFl )
            cwLogWarning("%i : Missing note-on instance for note-off:%s",m->uid,midi::midiToSciPitch(d0,NULL,0));
        }
        else
        {
          // a key was released - so it should not already be up
          if( noteGateM[k]==0 )
          {
            if( warningFl )
              cwLogWarning("%i : Missing note-on for note-off:%s",m->uid,midi::midiToSciPitch(d0,NULL,0));
          }
          else
          {
            noteGateM[k] -= 1; // update the note gate state

            // update the sounding note status
            if( _calcNoteDur(m0, m, noteGateM[k], sustGateV[ch], sostGateM[k]) )
              noteM[k] = NULL;
          }
        }
      }
      else
        
        // This is a sustain-pedal down msg
        if( isSustainPedalDown(m) )
        {
          // if the sustain channel is already down
          if( warningFl && sustGateV[ch] )
            cwLogWarning("%i : The sustain pedal went down twice with no intervening release.",m->uid);

          sustGateV[ch] += 1;

          if( sustV[ch] == NULL )
            sustV[ch] = m;
          
        }
        else

          // This is a sustain-pedal up msg
          if( isSustainPedalUp(m) )
          {
            // if the sustain channel is already up
            if( warningFl && sustGateV[ch]==0 )
              cwLogWarning("%i : The sustain pedal release message was received with no previous pedal down.",m->uid);

            if( sustGateV[ch] >= 1 )
            {
              sustGateV[ch] -= 1;

              if( sustGateV[ch] == 0 )
              {
                int k = ch*kMidiNoteCnt;
                
                // for each sounding note on this channel
                for(; k<ch*kMidiNoteCnt+kMidiNoteCnt; ++k)
                  if( noteM[k]!=NULL && _calcNoteDur(noteM[k], m, noteGateM[k], sustGateV[ch], sostGateM[k]) )
                    noteM[k] = NULL;

                if( sustV[ch] != NULL )
                {
                  _setDur(sustV[ch],m);
                  ((chMsg_t*)sustV[ch]->u.chMsgPtr)->end = m; // set the pedal-up msg ptr. in the pedal-down msg.
                  sustV[ch] = NULL;
                }
              }
            }
          }
          else

            // This is a sostenuto pedal-down msg
            if( isSostenutoPedalDown(m) )
            {
              // if the sustain channel is already down
              if( warningFl && sostGateV[ch] )
                cwLogWarning("%i : The sostenuto pedal went down twice with no intervening release.",m->uid);

              // record the notes that are active when the sostenuto pedal went down
              unsigned k = ch * kMidiNoteCnt;
              for(i=0; i<kMidiNoteCnt; ++i)
                sostGateM[k+i] = noteGateM[k+i] > 0;
              
              sostGateV[ch] += 1;          
            }
            else

              // This is a sostenuto pedal-up msg
              if( isSostenutoPedalUp(m) )
              {
                // if the sustain channel is already up
                if( warningFl && sostGateV[ch]==0 )
                  cwLogWarning("%i : The sostenuto pedal release message was received with no previous pedal down.",m->uid);

                if( sostGateV[ch] >= 1 )
                {
                  sostGateV[ch] -= 1;

                  if( sostGateV[ch] == 0 )
                  {
                    int k = ch*kMidiNoteCnt;
                    
                    // for each note on this channel
                    for(; k<ch*kMidiNoteCnt+kMidiNoteCnt; ++k)
                    {
                      sostGateM[k] = false;
                      
                      if( noteM[k]!=NULL && _calcNoteDur(noteM[k], m, noteGateM[k], sustGateV[ch], sostGateM[k]) )
                        noteM[k] = NULL;
                    }
                    
                    if( sostV[ch] != NULL )
                    {
                      _setDur(sostV[ch],m);                      
                      ((chMsg_t*)sostV[ch]->u.chMsgPtr)->end = m; // set the pedal-up msg ptr. in the pedal-down msg.
                      sostV[ch] = NULL;
                    }

                  }
                }
              }
    
  } // for each midi file event


  if( warningFl )
  {
    unsigned sustChN   = 0; // count of channels where the sustain pedal was left on at the end of the file
    unsigned sostChN   = 0; //                             sostenuto
    unsigned sustInstN = 0; // count of sustain   on with no previous sustain off
    unsigned sostInstN = 0; //          sostenuto on   
    unsigned noteN     = 0; // count of notes left on at the end of the file
    unsigned noteInstN = 0; // count of reattacks
    
    // initialize the state tracking variables
    for(i=0; i<kMidiChCnt; ++i)
    {
      if( sustV[i]!=NULL )
        sustChN += 1;
      
      sustInstN += sustGateV[i]; 
      
      if( sostV[i] != NULL )
        sostChN += 1;
      
      sostInstN += sostGateV[i] = 0;
      
      for(j=0; j<kMidiNoteCnt; ++j)
      {
        noteN     += noteM[ i*kMidiNoteCnt + j ] != NULL;
        noteInstN += noteGateM[ i*kMidiNoteCnt + j ];
      }
    }

    cwLogWarning("note:%i inst:%i sustain: %i inst: %i sost: %i inst: %i",noteN,noteInstN,sustChN,sustInstN,sostChN,sostInstN);
  }

  // drop 
  if( cwIsFlag(flags,kDropReattacksMfFl) )
    _drop(p);
  


}

void cw::midi::file::setDelay( handle_t h, unsigned ticks )
{
  file_t* p;
  unsigned mi;

  if((p = _handleToPtr(h)) == NULL )
    return;

  const trackMsg_t** msgV = _msgArray(p);

  if( p->msgN == 0 )
    return;

  for(mi=0; mi<p->msgN; ++mi)
  {
    trackMsg_t* mp = (trackMsg_t*)msgV[mi]; // cast away const
    
    // locate the first msg which has a non-zero delta tick
    if( mp->dtick > 0 )
    {
      mp->dtick = ticks;
      break;
    }
  }
}

namespace cw
{
  namespace midi
  {
    namespace file
    {
      void _printHdr( const file_t* mfp, log::handle_t logH )
      {
        if( mfp->fn != NULL )
          cwLogPrintH(logH,"%s\n",mfp->fn);

        cwLogPrintH(logH,"fmt:%i ticksPerQN:%i tracks:%i\n",mfp->fmtId,mfp->ticksPerQN,mfp->trkN);

        cwLogPrintH(logH," UID  trk    dtick     atick      amicro     type  ch  D0  D1\n");
        cwLogPrintH(logH,"----- --- ---------- ---------- ---------- : ---- --- --- ---\n");
  
      }

      void _printMsg( log::handle_t logH, const trackMsg_t* tmp )
      {
        cwLogPrintH(logH,"%5i %3i %10u %10llu %10llu : ",
                    tmp->uid,
                    tmp->trkIdx,
                    tmp->dtick,
                    tmp->atick,
                    tmp->amicro );

        if( tmp->status == kMetaStId )
        {

          switch( tmp->metaId )
          {
            case kTempoMdId:
              cwLogPrintH(logH,"%s bpm %i", metaStatusToLabel(tmp->metaId),60000000 / tmp->u.iVal);        
              break;

            case kTimeSigMdId:
              cwLogPrintH(logH,"%s %i %i", metaStatusToLabel(tmp->metaId), tmp->u.timeSigPtr->num,tmp->u.timeSigPtr->den);        
              break;
        
        
            default:
              cwLogPrintH(logH,"%s ", metaStatusToLabel(tmp->metaId));

          }
        }
        else
        {
          cwLogPrintH(logH,"%4s %3i %3i %3i",
                      statusToLabel(tmp->status),
                      tmp->u.chMsgPtr->ch,
                      tmp->u.chMsgPtr->d0,
                      tmp->u.chMsgPtr->d1);    
        }

        if( midi::isChStatus(tmp->status) && midi::isNoteOn(tmp->status,tmp->u.chMsgPtr->d1) )
          cwLogPrintH(logH," %4s ",midi::midiToSciPitch(tmp->u.chMsgPtr->d0,NULL,0));
    
  
        cwLogPrintH(logH,"\n");
      }

      rc_t _printCsvHdr( cw::file::handle_t fH )
      {
        return cw::file::printf(fH,"UID,trk,dtick,atick,amicro,type,ch,D0,D1,sci_pitch\n");        
      }
      
      rc_t _printCsvRow( cw::file::handle_t fH, const trackMsg_t* m )
      {
        cw::file::printf(fH,"%5i,%3i,%10u,%10llu,%10llu",
                    m->uid,
                    m->trkIdx,
                    m->dtick,
                    m->atick,
                    m->amicro );
        
        if( m->status == kMetaStId )
        {

          switch( m->metaId )
          {
            case kTempoMdId:
              cw::file::printf(fH,",%s,%i,bpm,", metaStatusToLabel(m->metaId),60000000 / m->u.iVal);        
              break;

            case kTimeSigMdId:
              cw::file::printf(fH,",%s,%i,%i,", metaStatusToLabel(m->metaId), m->u.timeSigPtr->num,m->u.timeSigPtr->den);        
              break;
        
        
            default:
              cw::file::printf(fH,",%s,,,", metaStatusToLabel(m->metaId));

          }
        }
        else
        {
          cw::file::printf(fH,",%4s,%3i,%3i,%3i",
                      statusToLabel(m->status),
                      m->u.chMsgPtr->ch,
                      m->u.chMsgPtr->d0,
                      m->u.chMsgPtr->d1);    
        }

        bool fl = midi::isChStatus(m->status) && midi::isNoteOn(m->status,m->u.chMsgPtr->d1);
        cw::file::printf(fH,",%4s",fl ? midi::midiToSciPitch(m->u.chMsgPtr->d0,NULL,0) : "");
    
  
        return cw::file::printf(fH,"\n");
      }

      
    }
  }
}

void cw::midi::file::printMsgs( handle_t h, log::handle_t logH )
{
  file_t* p = _handleToPtr(h);
  unsigned mi;
  
  
  _printHdr(p,logH);

  const trackMsg_t** msgV = _msgArray(p);

  for(mi=0; mi<p->msgN; ++mi)
  {
    const trackMsg_t* mp = msgV[mi];

    if( mp != NULL )
      _printMsg(logH,mp);
  }
  
}

void cw::midi::file::printTrack( handle_t h, unsigned trkIdx, log::handle_t logH )
{
  const file_t* mfp = _handleToPtr(h);

  _printHdr(mfp,logH);

  int i = trkIdx == kInvalidIdx ? 0         : trkIdx;
  int n = trkIdx == kInvalidIdx ? mfp->trkN : trkIdx+1;

  for(; i<n; ++i)
  {
    cwLogPrintH(logH,"Track:%i\n",i);
    
    trackMsg_t* tmp = mfp->trkV[i].base;
    while( tmp != NULL )
    {
      _printMsg(logH,tmp);
      tmp = tmp->link;
    }
  }  
}


cw::midi::file::density_t* cw::midi::file::noteDensity( handle_t h, unsigned* cntRef )
{
  int                msgN = msgCount(h);
  const trackMsg_t** msgs = msgArray(h);
  density_t*         dV   = mem::allocZ<density_t>(msgN);
  
  int i,j,k;
  for(i=0,k=0; i<msgN && k<msgN; ++i)
    if( msgs[i]->status == kNoteOnMdId && msgs[i]->u.chMsgPtr->d1 > 0 )
    {
      dV[k].uid    = msgs[i]->uid;
      dV[k].amicro = msgs[i]->amicro;

      // count the number of notes occuring in the time window
      // between this note and one second prior to this note.
      for(j=i; j>=0; --j)
      {        
        if( msgs[i]->amicro - msgs[j]->amicro > 1000000 )
          break;

        dV[k].density += 1;
      }
      
      k += 1;
      
    }

  if( cntRef != NULL )
    *cntRef = k;

  return dV;
}


cw::rc_t cw::midi::file::genPlotFile( const char* midiFn, const char* outFn )
{
  rc_t               rc = kOkRC;
  handle_t           mfH;  
  cw::file::handle_t fH;
  unsigned           i  = 0;
  const trackMsg_t** m  = NULL;
  unsigned           mN = 0;

  if((rc = open( mfH, midiFn )) != kOkRC )
    return cwLogError(rc,"The MIDI file object could not be opened from '%s'.",cwStringNullGuard(midiFn));

  if( (m = msgArray(mfH)) == NULL || (mN = msgCount(mfH)) == 0 )
  {
    rc = cwLogError(kInvalidArgRC,"The MIDI file object appears to be empty.");
    goto errLabel;
  }
  
  calcNoteDurations( mfH, 0 );
  
  if((rc = cw::file::open(fH,outFn,cw::file::kWriteFl)) != kOkRC )
    return cwLogError(rc,"Unable to create the file '%s'.",cwStringNullGuard(outFn));

  for(i=0; i<mN; ++i)
    if( (m[i]!=NULL) && midi::isChStatus(m[i]->status) && midi::isNoteOn(m[i]->status,m[i]->u.chMsgPtr->d1) )
      cw::file::printf(fH,"n %f %f %i %s\n",m[i]->amicro/1000000.0,m[i]->u.chMsgPtr->durMicros/1000000.0,m[i]->uid,midi::midiToSciPitch(m[i]->u.chMsgPtr->d0,NULL,0));

 errLabel:
  
  close(mfH);
  cw::file::close(fH);
  return rc;
}

cw::rc_t cw::midi::file::genCsvFile( const char* midiFn, const char* csvFn, bool printWarningsFl)
{
  rc_t               rc = kOkRC;
  handle_t           mfH;  
  cw::file::handle_t fH;
  
  if((rc = open( mfH, midiFn )) != kOkRC )
    return cwLogError(rc,"The MIDI file object could not be opened from '%s'.",cwStringNullGuard(midiFn));
  
  calcNoteDurations( mfH, printWarningsFl ? kWarningsMfFl : 0  );
  
  if((rc = cw::file::open(fH, csvFn,cw::file::kWriteFl)) != kOkRC )
  {
    rc = cwLogError(rc,"Unable to create the CSV file '%s'.",cwStringNullGuard(csvFn));
    goto errLabel;
  }
  else
  {

    file_t*  p = _handleToPtr(mfH);

    const trackMsg_t** msgV = _msgArray(p);

    _printCsvHdr(fH);
    
    for(unsigned mi=0; mi<p->msgN; ++mi)
    {
      const trackMsg_t* mp = msgV[mi];
      
      if( mp != NULL )
        _printCsvRow(fH, mp );
    }
  }
  

 errLabel:
  
  close(mfH);
  cw::file::close(fH);
  return rc;  
}


/*
  cw::rc_t cw::midi::file::genSvgFile( cmCtx_t* ctx, const char* midiFn, const char* outSvgFn, const char* cssFn, bool standAloneFl, bool panZoomFl )
  {
  rc_t               rc             = kOkRC;
  cmSvgH_t           svgH           = cmSvgNullHandle;
  handle_t           mfH            = nullHandle;
  unsigned           msgN           = 0;
  const trackMsg_t** msgs           = NULL;
  unsigned           noteHeight     = 10;
  double             micros_per_sec = 1000.0;
  unsigned           i;

  if((rc = open(ctx,&mfH,midiFn)) != kOkRC )
  {
  rc = cwLogError(rc,"Unable to open the MIDI file '%s'.",cwStringNullGuard(midiFn));
  goto errLabel;
  }

  calcNoteDurations( mfH, 0 );

  msgN = msgCount(mfH);
  msgs = msgArray(mfH);

  
  if( cmSvgWriterAlloc(ctx,&svgH) != kOkSvgRC )
  {
  rc = cwLogError(kSvgFailMfRC,"Unable to create the MIDI SVG output file '%s'.",cwStringNullGuard(outSvgFn));
  goto errLabel;
  }


  for(i=0; i<msgN && rc==kOkRC; ++i)    
  if( msgs[i]->status == kNoteOnMdId && msgs[i]->u.chMsgPtr->d1 > 0 )
  {
  const trackMsg_t* m = msgs[i];

      
  if( cmSvgWriterRect(svgH, m->amicro/micros_per_sec, m->u.chMsgPtr->d0 * noteHeight,  m->u.chMsgPtr->durMicros/micros_per_sec,  noteHeight-1, "note" ) != kOkSvgRC )
  rc                                                                                                                                                     = kSvgFailMfRC;

  const char* t0 = toSciPitch(m->u.chMsgPtr->d0,NULL,0);

  if( cmSvgWriterText(svgH, (m->amicro + (m->u.chMsgPtr->durMicros/2)) / micros_per_sec, m->u.chMsgPtr->d0 * noteHeight, t0, "text" ) != kOkSvgRC )
  rc                                                                                                                                   = kSvgFailMfRC;

  }

  if( rc != kOkRC )
  {
  cwLogError(rc,"SVG Shape insertion failed.");
  goto errLabel;
  }
  
  unsigned             dN = 0;
  density_t* dV = noteDensity( mfH, &dN );
  double               t0 = 0;
  double               y0 = 64.0;
  char*            tx     = NULL;
  
  for(i = 0; i<dN; ++i)
  {
  const trackMsg_t* m;

  if((m = _uidToMsg( _handleToPtr(mfH), dV[i].uid )) == NULL )
  rc = cwLogError(kUidNotFoundMfRC,"The MIDI msg form UID:%i was not found.",dV[i].uid);
  else
  {
  double t1 = m->amicro / micros_per_sec;
  double y1 = dV[i].density * noteHeight;
      
  cmSvgWriterLine(svgH, t0, y0, t1, y1, "density" );
  cmSvgWriterText(svgH, t1, y1, tx = cmTsPrintfP(tx,"%i",dV[i].density),"dtext");

  t0 = t1;
  y0 = y1;

  }
  }

  cmMemFree(dV);
  cmMemFree(tx);
  
  if( rc                                                             == kOkRC )
  if( cmSvgWriterWrite(svgH,cssFn,outSvgFn, standAloneFl, panZoomFl) != kOkSvgRC )
  rc                                                                  = cwLogError(kSvgFailMfRC,"SVG file write to '%s' failed.",cwStringNullGuard(outSvgFn));


  errLabel:
  close(&mfH);
  cmSvgWriterFree(&svgH);
  
  return rc;
  }
*/

cw::rc_t  cw::midi::file::report( const char* midiFn, log::handle_t logH )
{
  handle_t mfH;
  rc_t     rc;
  
  if((rc = open(mfH,midiFn)) != kOkRC )
  {
    rc = cwLogError(rc,"Unable to open the MIDI file: %s\n",cwStringNullGuard(midiFn));
    goto errLabel;
  }

  printMsgs(mfH, logH );

 errLabel:
  close(mfH);

  return rc;
}

cw::rc_t cw::midi::file::report_begin_end( const char* midiFn, unsigned msg_cnt, bool noteon_only_fl, log::handle_t logH )
{
  handle_t mfH;
  rc_t     rc;

  // Selector function
  auto sel = []( auto noteon_fl, const trackMsg_t* m){ return (m!=nullptr) && ((!noteon_fl) || (noteon_fl && isNoteOn(m))); };

  if( !logH.isValid() )
    logH = log::globalHandle();
  
  if((rc = open(mfH,midiFn)) != kOkRC )
  {
    rc = cwLogError(rc,"Unable to open the MIDI file: %s\n",cwStringNullGuard(midiFn));
    goto errLabel;
  }

  else
  {
    file_t* p = _handleToPtr(mfH);

    _printHdr(p,logH);

    msg_cnt = std::min(msg_cnt,p->msgN);

    const trackMsg_t** msgV = _msgArray(p);
    unsigned           mn   = 0;
      
    for(unsigned mi=0; mn<msg_cnt && mi<p->msgN; ++mi)
      if( sel(noteon_only_fl,msgV[mi] ) )
      {
        _printMsg(logH,msgV[mi]);
        ++mn;
      }

    if( p->msgN > 0 )
    {
      int mi = p->msgN-1;
      for(mn=0; mn<msg_cnt && mi>=0; --mi)
        if( sel(noteon_only_fl,msgV[mi]) )
          ++mn;

      for(; mi<(int)p->msgN; ++mi)
        if( sel(noteon_only_fl,msgV[mi]) )
          _printMsg(logH,msgV[mi]);
    }
    
  }
 errLabel:
  close(mfH);

  return rc;
}



void cw::midi::file::printControlNumbers( const char* fn )
{
  handle_t h;
  rc_t     rc;

  if((rc = open( h, fn )) != kOkRC )
  {
    cwLogError(rc,"MIDI file open failed on '%s'.",fn);
    goto errLabel;
  }
  else
  {
    const trackMsg_t** mm;
    unsigned n = msgCount(h);
    if((mm = msgArray(h)) != NULL )
    {
      unsigned j;
      for(j=0; j<n; ++j)
      {
        const trackMsg_t* m = mm[j];
      
        if(  m->status == kCtlMdId && m->u.chMsgPtr->d0==66 )
          printf("%i %i\n",m->u.chMsgPtr->d0,m->u.chMsgPtr->d1);
      }
    }
  }
 errLabel:
  close(h);

}


cw::rc_t cw::midi::file::test( const object_t* cfg )
{

  rc_t rc = kOkRC;
  
  const object_t* o;

  for(unsigned i=0; i<cfg->child_count(); ++i)
  {
    if((o = cfg->child_ele(i)) != nullptr )
    {
      if( textIsEqual(o->pair_label(),"rpt") )
        rc = _testReport(o->pair_value());
      
      if( textIsEqual(o->pair_label(),"gen_csv") )
        rc = _testGenCsv(o->pair_value());

      if( textIsEqual(o->pair_label(),"open_csv") )
        rc = _testOpenCsv(o->pair_value());
      
      if( textIsEqual(o->pair_label(),"batch_convert") )
        rc = _testBatchConvert(o->pair_value());

      if( textIsEqual(o->pair_label(),"rpt_beg_end") )
        rc = _testRptBeginEnd(o->pair_value());

    }
  }
  
  return rc;

#ifdef NOT_DEF
  rc_t      rc;
  handle_t h;
  log::handle_t logH = log::globalHandle();

  if((rc = open(h,fn)) != kOkRC )
  {
    printf("Error:%i Unable to open the  file: %s\n",rc,fn);
    return;
  }

  calcNoteDurations(  h, 0 );

  if( 1 )
  {
    //tickToMicros( h );
    //tickToSamples(h,96000,false);
    printMsgs(h,logH);
  }

  if( 0 )
  {
    //print(h,trackCount(h)-1,&ctx->rpt);
    //print(h,kInvalidIdx,&ctx->rpt);
    printControlNumbers( fn );

  }
  if( 0 )
  {
    printf("Tracks:%i\n",trackCount(h));

    unsigned i = 0;
    for(i=0; i<msgCount(h); ++i)
    {
      trackMsg_t* tmp = (trackMsg_t*)msgArray(h)[i];
      
      if( tmp->status==kMetaStId && tmp->metaId == kTempoMdId )
      {
        double bpm = 60000000.0/tmp->u.iVal;
        printf("Tempo:%i %f\n",tmp->u.iVal,bpm);

        tmp->u.iVal = floor( 60000000.0/69.0 );

        break;
      }
    }

    write(h,"/home/kevin/temp/test0.mid");
  }

  close(h);
#endif
}
