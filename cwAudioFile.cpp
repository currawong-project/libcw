#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwObject.h"
#include "cwAudioFile.h"
#include "cwUtility.h"
#include "cwFileSys.h"

// #define _24to32_aif( p ) ((int)( ((p[0]>127?255:0) << 24) + (((int)p[0]) << 16) +  (((int)p[1]) <<8) + p[2]))  // no-swap equivalent
// See note in:_cmAudioFileReadFileHdr()
// Note that this code byte swaps as it converts - this is to counter the byte swap that occurs in cmAudioFileReadInt().
#define _24to32_aif( p ) ((int)( ((p[0]>127?255:0) <<  0) + (((int)p[2]) << 24) +  (((int)p[1]) <<16) + (((int)p[0]) << 8)))

#define _24to32_wav( p ) ((int)( ((p[2]>127?255:0) << 24) + (((int)p[2]) << 16) +  (((int)p[1]) <<8) + p[0]))

#define _cmAfSwap16(v)  cwSwap16(v)
#define _cmAfSwap32(v)  cwSwap32(v)

#ifdef cmBIG_ENDIAN
#define _cmAifSwapFl    (0)
#define _cmWavSwapFl    (1)
#else
#define _cmAifSwapFl    (1)
#define _cmWavSwapFl    (0)
#endif

namespace cw
{
  namespace audiofile
  {
    enum
    {
     kAiffFileId   = 'FORM',
     kAiffChkId    = 'AIFF',
     kAifcChkId    = 'AIFC',
     kSowtCompId   = 'sowt',
     kNoneCompId   = 'NONE',

     kWavFileId    = 'FFIR',
     kWavChkId     = 'EVAW',
    };

    enum { kWriteAudioGutsFl=0x01 };

    typedef struct audiofile_str
    {
      FILE*          fp;
      info_t         info;      // audio file details 
      unsigned       curFrmIdx; // current frame offset 
      unsigned       fileByteCnt; // file byte cnt 
      unsigned       smpByteOffs; // byte offset of the first sample
      marker_t*      markArray;
      unsigned       flags;
      char*          fn;
    } af_t;

    rc_t _writeHdr( af_t* p );

    af_t* _handleToPtr( handle_t h )
    {  return handleToPtr<handle_t,af_t>(h); }
    
    rc_t _destroy( af_t* p )
    {
      rc_t rc = kOkRC;
      
      if( p != nullptr )
      {

        if( cwIsFlag(p->flags, kWriteAudioGutsFl ) )
          if((rc = _writeHdr(p)) != kOkRC )
            return rc;
        
        if( p->fp != nullptr )
        {
          fclose(p->fp);
          p->fp = nullptr;
        }
        
        mem::release(p);
        
      }
      
      return rc;
    }

    rc_t _seek( af_t* p, long byteOffset, int origin )
    {
      if( fseek(p->fp,byteOffset,origin) != 0 )
        return cwLogError(kSeekFailRC,"Audio file seek failed on '%s'.",cwStringNullGuard(p->fn));
      return kOkRC;
    }

    rc_t _read( af_t* p, void* eleBuf, unsigned bytesPerEle, unsigned eleCnt )
    {
      if( fread(eleBuf,bytesPerEle,eleCnt,p->fp) != eleCnt )
        return cwLogError(kReadFailRC,"Audio file read failed on '%s'.",cwStringNullGuard(p->fn));
    
      return kOkRC;
    }

    rc_t _readUInt32( af_t* p, std::uint32_t* valuePtr )
    {
      rc_t rc;

      if(( rc = _read(p, valuePtr, sizeof(*valuePtr), 1 )) != kOkRC )
        return rc;
    
      if( cwIsFlag(p->info.flags,kSwapAfFl) )
        *valuePtr = _cmAfSwap32(*valuePtr);

      return rc;
    } 


    rc_t _readUInt16( af_t* p, std::uint16_t* valuePtr )
    {
      rc_t rc;

      if(( rc = _read(p, valuePtr, sizeof(*valuePtr), 1 )) != kOkRC )
        return rc;
    
      if( cwIsFlag(p->info.flags,kSwapAfFl) )
        *valuePtr = _cmAfSwap16(*valuePtr);

      return rc;
    } 

    rc_t _readPascalString( af_t* p, char s[kAudioFileLabelCharCnt] )
    {
      rc_t rc;
      unsigned char n;

      if((rc = _read(p,&n,sizeof(n),1)) != kOkRC )
        return rc;

      if((rc = _read(p,s,n,1)) != kOkRC )
        return rc;

      s[n] = '\0';

      if( n % 2 == 0 )
        rc = _seek(p,1,SEEK_CUR);

      return rc;
    }

    rc_t _readString( af_t* p, char* s, unsigned sn )
    {
      rc_t rc;
      if((rc = _read(p,s,sn,1)) != kOkRC )
        return rc;

      return kOkRC;
    }

    rc_t _readX80( af_t* p, double* x80Ptr )
    {
      unsigned char s[10];
      rc_t rc = kOkRC;

      if((rc = _read(p,s,10,1)) != kOkRC )
        return rc;

      *x80Ptr = x80ToDouble(s);
      return kOkRC;
    }

    rc_t _readChunkHdr( af_t* p, std::uint32_t* chkIdPtr, unsigned* chkByteCntPtr )
    {
      rc_t rc      = kOkRC;

      *chkIdPtr      = 0;
      *chkByteCntPtr = 0;

      if((rc = _readUInt32(p,chkIdPtr)) != kOkRC )
        return rc;

      if((rc = _readUInt32(p,chkByteCntPtr)) != kOkRC )
        return rc;

      // the actual on disk chunk size is always incrmented up to the next even integer
      *chkByteCntPtr += (*chkByteCntPtr) % 2;

      return rc;
    }

    rc_t _readFileHdr( af_t* p, unsigned constFormId, unsigned constAifId, bool swapFl )
    {
      rc_t     rc         = kOkRC;
      std::uint32_t formId     = 0;
      std::uint32_t aifId      = 0;
      unsigned   chkByteCnt = 0;

      p->info.flags     = 0;
      p->curFrmIdx      = 0;
      p->fileByteCnt    = 0;

      if((rc = _seek(p,0,SEEK_SET)) != kOkRC ) 
        return rc;

      // set the swap flags
      p->info.flags = cwEnaFlag(p->info.flags,kSwapAfFl,       swapFl);
      p->info.flags = cwEnaFlag(p->info.flags,kSwapSamplesAfFl,swapFl);

      if((rc = _readChunkHdr(p,&formId,&p->fileByteCnt)) != kOkRC )
        return rc;

      //
      // use -Wno-multichar on GCC cmd line to  disable the multi-char warning 
      //


      // check the FORM/RIFF id
      if( formId != constFormId )
        return kSyntaxErrorRC;

      // read the AIFF/WAVE id
      if((rc = _readChunkHdr(p,&aifId,&chkByteCnt)) != kOkRC )
        return rc;

      // check for the AIFC 
      if( formId == kAiffFileId && aifId != constAifId )
      {
        if( aifId == kAifcChkId )
          p->info.flags = cwSetFlag(p->info.flags,kAifcAfFl);
        else
          return kInvalidDataTypeRC;
      }

      // set the audio file type flag 
      if( aifId==kAiffChkId || aifId==kAifcChkId )
        p->info.flags = cwSetFlag(p->info.flags,kAiffAfFl);
  
      if( aifId==kWavChkId )
        p->info.flags = cwSetFlag(p->info.flags,kWavAfFl);
    

      return rc;
    }

    rc_t _readCommChunk( af_t* p )
    {
      rc_t rc = kOkRC;
      std::uint16_t ui16;
      std::uint32_t ui32;

      if((rc = _readUInt16(p,&ui16)) != kOkRC )
        return rc;
      p->info.chCnt = ui16;

      if((rc = _readUInt32(p,&ui32)) != kOkRC )
        return rc;
      p->info.frameCnt = ui32;

      if((rc = _readUInt16(p,&ui16)) != kOkRC )
        return rc;
      p->info.bits = ui16;

      if((rc = _readX80(p,&p->info.srate)) != kOkRC )
        return rc;

      // if this is an AIFC format file  ...
      if( cwIsFlag(p->info.flags,kAifcAfFl) )
      {
        if((rc = _readUInt32(p,&ui32))  != kOkRC )
          return rc;

        switch( ui32 )
        {
          case kNoneCompId:
            break;

          case kSowtCompId:
            // If the compression type is set to 'swot' 
            // then the samples are written in little-endian (Intel) format
            // rather than the default big-endian format. 
            p->info.flags = cwTogFlag(p->info.flags,kSwapSamplesAfFl);
            break;

          default:
            rc = cwLogError(kSyntaxErrorRC,"Unknown AIFC type id: 0x%x in '%s'.",ui32,cwStringNullGuard(p->fn) );
        }
      }

      return rc;
    }

    rc_t _readSsndChunk( af_t* p )
    {
      rc_t rc = kOkRC;

      std::uint32_t smpOffs=0, smpBlkSize=0;
  
      if((rc = _readUInt32(p,&smpOffs)) != kOkRC )
        return rc;

      if((rc = _readUInt32(p,&smpBlkSize)) != kOkRC )
        return rc;
  
      if((rc = _seek(p,smpOffs, SEEK_CUR)) != kOkRC )
        return rc;

      p->smpByteOffs = ftell(p->fp);

      return rc;
    }

    rc_t _readMarkerChunk( af_t* p )
    {
      rc_t rc = kOkRC;

      std::uint16_t ui16;
      std::uint32_t ui32;
      unsigned   i;

      if((rc = _readUInt16(p,&ui16)) != kOkRC )
        return rc;

      p->info.markerCnt = ui16;

      assert(p->markArray == NULL);

      marker_t* m = mem::allocZ<marker_t>(p->info.markerCnt);

      p->info.markerArray = m; 

      for(i=0; i<p->info.markerCnt; ++i)
      {
        if((rc = _readUInt16(p,&ui16)) != kOkRC )
          return rc;

        m[i].id = ui16;

        if((rc = _readUInt32(p,&ui32)) != kOkRC )
          return rc;

        m[i].frameIdx = ui32;

        if((rc = _readPascalString(p,m[i].label)) != kOkRC )
          return rc;
    
      }
      return rc;
    }

    rc_t _readFmtChunk( af_t* p )
    {
      rc_t rc = kOkRC;
      unsigned short fmtId, chCnt, blockAlign, bits;
      unsigned srate, bytesPerSec;

      if((rc = _readUInt16(p,&fmtId)) != kOkRC )
        return rc;
  
      if((rc = _readUInt16(p,&chCnt)) != kOkRC )
        return rc;

      if((rc = _readUInt32(p,&srate)) != kOkRC )
        return rc;
  
      if((rc = _readUInt32(p,&bytesPerSec)) != kOkRC )
        return rc;

      if((rc = _readUInt16(p,&blockAlign)) != kOkRC )
        return rc;
  
      if((rc = _readUInt16(p,&bits)) != kOkRC )
        return rc;

      p->info.chCnt = chCnt;
      p->info.bits  = bits;
      p->info.srate = srate;

      // if the 'data' chunk was read before the 'fmt' chunk then info.frameCnt 
      // holds the number of bytes in the data chunk
      if( p->info.frameCnt != 0 )
        p->info.frameCnt = p->info.frameCnt / (p->info.chCnt * p->info.bits/8);

      return rc;
    }

    rc_t _readDatcmhunk( af_t* p, unsigned chkByteCnt )
    {
      // if the 'fmt' chunk was read before the 'data' chunk then info.chCnt is non-zero
      if( p->info.chCnt != 0 )
        p->info.frameCnt = chkByteCnt / (p->info.chCnt * p->info.bits/8);
      else
        p->info.frameCnt = chkByteCnt;

      p->smpByteOffs = ftell(p->fp);

      return kOkRC;
    }

    rc_t _readBextChunk( af_t* p)
    {
      rc_t rc = kOkRC;

      if((rc = _readString(p,p->info.bextRecd.desc,kAfBextDescN)) != kOkRC )
        return rc;

      if((rc = _readString(p,p->info.bextRecd.origin,kAfBextOriginN)) != kOkRC )
        return rc;

      if((rc = _readString(p,p->info.bextRecd.originRef,kAfBextOriginRefN)) != kOkRC )
        return rc;

      if((rc = _readString(p,p->info.bextRecd.originDate,kAfBextOriginDateN)) != kOkRC )
        return rc;

      if((rc = _readString(p,p->info.bextRecd.originTime,kAfBextOriginTimeN)) != kOkRC )
        return rc;

      if((rc = _readUInt32(p,&p->info.bextRecd.timeRefLow)) != kOkRC )
        return rc;

      if((rc = _readUInt32(p,&p->info.bextRecd.timeRefHigh)) != kOkRC )
        return rc;

      return rc;
    }

    rc_t _open( af_t* p, const char* fn, const char* fileModeStr )
    {
      rc_t rc = kOkRC;
  
      // zero the info record
      memset(&p->info,0,sizeof(p->info));

      // open the file
      if((p->fp = fopen(fn,fileModeStr)) == NULL )
      {
        rc = cwLogError(kOpenFailRC,"Audio file open failed on '%s'.",cwStringNullGuard(fn));
        goto errLabel;
      }

      // read the file header
      if((rc = _readFileHdr(p,kAiffFileId,kAiffChkId,_cmAifSwapFl)) != kOkRC )
        if((rc = _readFileHdr(p,kWavFileId,kWavChkId,_cmWavSwapFl)) != kOkRC )
          goto errLabel;
  
      // seek past the file header
      if((rc = _seek(p,12,SEEK_SET)) != kOkRC )
        goto errLabel;

      // zero chCnt and frameCnt to allow the order of the 'data' and 'fmt' chunks to be noticed
      p->info.chCnt    = 0;
      p->info.frameCnt = 0;

      while( ftell(p->fp ) < p->fileByteCnt )
      {
        unsigned chkId, chkByteCnt;
        if((rc = _readChunkHdr(p,&chkId,&chkByteCnt)) != kOkRC )
          goto errLabel;

        unsigned offs = ftell(p->fp);

        if( cwIsFlag(p->info.flags,kAiffAfFl) )
          switch(chkId)
          {
            case 'COMM':
              if((rc = _readCommChunk(p)) != kOkRC )
                goto errLabel;
              break;

            case 'SSND':
              if((rc = _readSsndChunk(p)) != kOkRC )
                goto errLabel;
              break;

            case 'MARK':
              if((rc = _readMarkerChunk(p)) != kOkRC )
                goto errLabel;
              break;
          }
        else
          switch(chkId)
          {
            case ' tmf':
              if((rc = _readFmtChunk(p)) != kOkRC )
                goto errLabel;
              break;

            case 'atad':
              if((rc = _readDatcmhunk(p,chkByteCnt)) != kOkRC )
                goto errLabel;
              break;

            case 'txeb':
              if((rc = _readBextChunk(p)) != kOkRC )
                goto errLabel;
              break;
          }


        // seek to the end of this chunk
        if((rc = _seek(p,offs+chkByteCnt,SEEK_SET)) != kOkRC )
          goto errLabel;
      }

    errLabel:
      if( rc!=kOkRC )
        _destroy(p);

      return rc;
    }

    rc_t   _writeBytes( af_t* p, const void* b, unsigned bn )
    {
      rc_t rc = kOkRC;
      if( fwrite( b, bn, 1, p->fp ) != 1 )
        return cwLogError(kWriteFailRC,"Audio file write failed on '%s'.",cwStringNullGuard(p->fn));

      return rc;
    }

    rc_t _writeId( af_t* p, const char* s )
    {  return _writeBytes( p,  s, strlen(s)) ; }

    rc_t _writeUInt32( af_t* p, unsigned v )
    {
      if( cwIsFlag(p->info.flags,kSwapAfFl) )
        v = _cmAfSwap32(v);
  
      return _writeBytes( p, &v, sizeof(v)) ; 
    }

    rc_t _writeUInt16( af_t* p, unsigned short v )
    {
      if( cwIsFlag(p->info.flags,kSwapAfFl) )
        v = _cmAfSwap16(v);
  
      return _writeBytes( p, &v, sizeof(v)) ; 
    }

    rc_t _writeAiffHdr( af_t* p )
    {
      rc_t        rc = kOkRC;
      unsigned char srateX80[10];
 
      doubleToX80( p->info.srate, srateX80 );  

      unsigned hdrByteCnt  = 54;
      unsigned ssndByteCnt = 8 + (p->info.chCnt * p->info.frameCnt * (p->info.bits/8));
      unsigned formByteCnt = hdrByteCnt + ssndByteCnt - 8;
      unsigned commByteCnt = 18;
      unsigned ssndSmpOffs = 0;
      unsigned ssndBlkSize = 0;

      // if ssndByteCnt is odd
      if( ssndByteCnt % 2 == 1 )
      {
        formByteCnt++;
      }

      if(( rc = _seek( p, 0, SEEK_SET )) != kOkRC )
        return rc;

      if(( rc = _writeId(     p, "FORM"))           != kOkRC ) return rc;
      if(( rc = _writeUInt32( p, formByteCnt))      != kOkRC ) return rc;
      if(( rc = _writeId(     p, "AIFF"))           != kOkRC ) return rc;
      if(( rc = _writeId(     p, "COMM"))           != kOkRC ) return rc;
      if(( rc = _writeUInt32( p, commByteCnt))      != kOkRC ) return rc;
      if(( rc = _writeUInt16( p, p->info.chCnt))    != kOkRC ) return rc;
      if(( rc = _writeUInt32( p, p->info.frameCnt)) != kOkRC ) return rc;
      if(( rc = _writeUInt16( p, p->info.bits))     != kOkRC ) return rc;
      if(( rc = _writeBytes(  p, &srateX80,10))     != kOkRC ) return rc;
      if(( rc = _writeId(     p, "SSND"))           != kOkRC ) return rc;
      if(( rc = _writeUInt32( p, ssndByteCnt))      != kOkRC ) return rc;
      if(( rc = _writeUInt32( p, ssndSmpOffs))      != kOkRC ) return rc;
      if(( rc = _writeUInt32( p, ssndBlkSize))      != kOkRC ) return rc;

      return rc;
    }

    rc_t _writeWavHdr( af_t* p )
    {
      rc_t     rc            = kOkRC;
      short    chCnt         = p->info.chCnt;
      unsigned frmCnt        = p->info.frameCnt;
      short    bits          = p->info.bits;
      unsigned srate         = p->info.srate;
      short    fmtId         = 1;
      unsigned bytesPerSmp   = bits/8;
      unsigned hdrByteCnt    = 36;
      unsigned fmtByteCnt    = 16;
      unsigned blockAlignCnt = chCnt * bytesPerSmp;
      unsigned sampleCnt     = chCnt * frmCnt;
      unsigned dataByteCnt   = sampleCnt * bytesPerSmp;

      if(( rc = _seek( p, 0, SEEK_SET )) != kOkRC )
        return rc;

      if((rc = _writeId(     p, "RIFF"))                   != kOkRC ) goto errLabel;
      if((rc = _writeUInt32( p, hdrByteCnt + dataByteCnt)) != kOkRC ) goto errLabel;
      if((rc = _writeId(     p, "WAVE"))                   != kOkRC ) goto errLabel;
      if((rc = _writeId(     p, "fmt "))                   != kOkRC ) goto errLabel;
      if((rc = _writeUInt32( p, fmtByteCnt))               != kOkRC ) goto errLabel;
      if((rc = _writeUInt16( p, fmtId))                    != kOkRC ) goto errLabel;
      if((rc = _writeUInt16( p, chCnt))                    != kOkRC ) goto errLabel;
      if((rc = _writeUInt32( p, srate))                    != kOkRC ) goto errLabel;
      if((rc = _writeUInt32( p, srate * blockAlignCnt))    != kOkRC ) goto errLabel;
      if((rc = _writeUInt16( p, blockAlignCnt))            != kOkRC ) goto errLabel;
      if((rc = _writeUInt16( p, bits))                     != kOkRC ) goto errLabel;
      if((rc = _writeId(     p, "data"))                   != kOkRC ) goto errLabel;
      if((rc = _writeUInt32( p, dataByteCnt))              != kOkRC ) goto errLabel;

    errLabel:
      return rc;
    }

    rc_t _writeHdr( af_t* p )
    {
      if( cwIsFlag(p->info.flags,kWavAfFl) )
        return _writeWavHdr(p);

      return _writeAiffHdr(p);
    }

    rc_t _filenameToAudioFileType( const char* fn, unsigned& flagsRef )
    {
      rc_t rc = kOkRC;
      
      filesys::pathPart_t* pp = nullptr;

      if((pp = filesys::pathParts(fn)) == NULL )
      {
        rc = cwLogError(kInvalidArgRC,"The audio file name '%s' could not be parsed.",cwStringNullGuard(fn));
        goto errLabel;
      }

      if(pp->extStr == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"The audio file name '%s' does not have an extension.",cwStringNullGuard(fn));
        goto errLabel;
      }
      else
      {
        // convert the extension to upper case
        unsigned extN = strlen(pp->extStr);
        char ext[ extN+1 ];
        for(unsigned i=0; i<extN; ++i)
          ext[i] = toupper(pp->extStr[i]);
        ext[extN] = 0;
      
        if( strcmp(ext,"WAV") == 0 )
        {
          flagsRef = cwSetFlag(flagsRef, kWavAfFl);
          flagsRef = cwClrFlag(flagsRef, kAiffAfFl);      
        }
        else
          if( strcmp(ext,"AIF")==0 || strcmp(ext,"AIFF")==0 )
          {
            flagsRef = cwClrFlag(flagsRef, kWavAfFl);
            flagsRef = cwSetFlag(flagsRef, kAiffAfFl);      
          }
          else
          {
            rc = cwLogError(kInvalidArgRC,"The audio file extension '%s' does not match WAV,AIFF,or AIF.", ext );
          }
      }
    errLabel:      
      mem::release(pp);

      return rc;
      
    }

    rc_t _readInt( handle_t h, unsigned totalFrmCnt, unsigned chIdx, unsigned chCnt, int* buf[], unsigned* actualFrmCntPtr, bool sumFl )
    {
      rc_t    rc = kOkRC;
      af_t* p  = _handleToPtr(h);

      if( chIdx+chCnt > p->info.chCnt )
        return cwLogError(kInvalidArgRC,"Invalid channel index on read. %i > %i",chIdx+chCnt,chCnt);

      if( actualFrmCntPtr != NULL )
        *actualFrmCntPtr = 0;

      unsigned       bps            = p->info.bits / 8;       // bytes per sample
      unsigned       bpf            = bps * p->info.chCnt;    // bytes per file frame
      unsigned       bufFrmCnt      = std::min(totalFrmCnt,(unsigned)cwAudioFile_MAX_FRAME_READ_CNT);
      unsigned       bytesPerBuf    = bufFrmCnt * bpf;
      unsigned char  fbuf[ bytesPerBuf ];                     // raw bytes buffer 
      unsigned       ci;
      unsigned       frmCnt = 0;  
      unsigned       totalReadFrmCnt;
      int*           ptrBuf[ chCnt ];
  

      for(ci=0; ci<chCnt; ++ci)
        ptrBuf[ci] = buf[ci];

      for(totalReadFrmCnt=0; totalReadFrmCnt<totalFrmCnt; totalReadFrmCnt+=frmCnt )
      {
    
        // don't read past the end of the file or past the end of the buffer
        frmCnt = std::min( p->info.frameCnt - p->curFrmIdx, std::min( totalFrmCnt-totalReadFrmCnt, bufFrmCnt ));


        // read the file frmCnt sample 
        if((rc = _read(p,fbuf,frmCnt*bpf,1)) != kOkRC )
          return rc;

        if( actualFrmCntPtr != NULL )
          *actualFrmCntPtr += frmCnt;

        assert( chIdx+chCnt <= p->info.chCnt );


        for(ci=0; ci<chCnt; ++ci)
        {
          unsigned char* sp = fbuf + (ci+chIdx)*bps;
          int*           dp = ptrBuf[ci];
          int*           ep = dp + frmCnt;

          if( !sumFl )
            memset(dp,0,frmCnt*sizeof(int));

          // 8 bit AIF files use 'signed char' and WAV files use 'unsigned char' for the sample data type. 
          if( p->info.bits == 8 )
          {
            if( cwIsFlag(p->info.flags,kAiffAfFl) )
            {
              for(; dp<ep; sp+=bpf,++dp)
                *dp +=  *(char*)sp;
            }
            else
            {
              for(; dp<ep; sp+=bpf,++dp)
              {
                int v = *(unsigned char*)sp;
                *dp +=  v -= 128;
              }
            }

          }

          // handle non-8 bit files here
          if( cwIsFlag(p->info.flags,kSwapSamplesAfFl) )
          {
            switch( p->info.bits )
            {
              case 8:
                break;

              case 16:
                for(; dp<ep; sp+=bpf,++dp)
                  *dp += (short)_cmAfSwap16(*(short*)sp);
                break;

              case 24:
                if( cwIsFlag(p->info.flags,kAiffAfFl) )
                {
                  for(; dp<ep; sp+=bpf,++dp)
                    *dp += (int)(_cmAfSwap32(_24to32_aif(sp)));
                }
                else
                {
                  for(; dp<ep; sp+=bpf,++dp)
                    *dp += (int)(_cmAfSwap32(_24to32_wav(sp)));
                }
                break;

              case 32:
                for(; dp<ep; sp+=bpf,++dp)
                  *dp += (int)_cmAfSwap32(*(int*)sp  );
                break;
            }
          }
          else
          {
            switch(p->info.bits)
            {
              case 8:
                break;

              case 16:
                for(; dp<ep; sp+=bpf,++dp)
                  *dp += *(short*)sp;
                break;

              case 24:
                if( cwIsFlag(p->info.flags,kAiffAfFl) )
                {
                  for(; dp<ep; sp+=bpf,++dp)
                    *dp +=  _24to32_aif(sp);
                }
                else
                {
                  for(; dp<ep; sp+=bpf,++dp)
                    *dp +=  _24to32_wav(sp);
                }
                break;

              case 32:
                for(; dp<ep; sp+=bpf,++dp)
                  *dp += *(int*)sp;


                break;
            }

            ptrBuf[ci] = dp;
            assert( dp <= buf[ci] + totalFrmCnt );
          }
          /*
            dp = ptrBuf[ci];
            ep = dp + frmCnt;
            while(dp<ep)
            sum += (double)*dp++;
          */
        }

        p->curFrmIdx += frmCnt;
      }

      if( totalReadFrmCnt < totalFrmCnt  )
      {
        for(ci=0; ci<chCnt; ++ci)
          memset(buf[ci] + frmCnt,0,(totalFrmCnt-totalReadFrmCnt)*sizeof(int));
      }



      //if( actualFrmCntPtr != NULL )
      //  *actualFrmCntPtr = totalReadFrmCnt;

      //printf("SUM: %f %f swap:%i\n", sum, sum/(totalFrmCnt*chCnt), cwIsFlag(p->info.flags,kSwapAfFl));

      return rc;  
    }

    rc_t _readRealSamples(  handle_t h, unsigned totalFrmCnt, unsigned chIdx, unsigned chCnt, float**  fbuf, double** dbuf, unsigned* actualFrmCntPtr, bool sumFl )
    {
      rc_t    rc = kOkRC;
      af_t* p  = _handleToPtr(h);

      if( actualFrmCntPtr != NULL )
        *actualFrmCntPtr = 0;


      unsigned         totalReadCnt = 0;
      unsigned         bufFrmCnt    = std::min( totalFrmCnt, (unsigned)cwAudioFile_MAX_FRAME_READ_CNT );
      unsigned         bufSmpCnt    = bufFrmCnt * chCnt;
      float            fltMaxSmpVal = 0;  

      int              buf[ bufSmpCnt ];
      int*             ptrBuf[ chCnt ];
      float*           fPtrBuf[ chCnt ];
      double*          dPtrBuf[ chCnt ];
      unsigned         i;
      unsigned         frmCnt = 0;

      switch( p->info.bits )
      {
        case 8:   fltMaxSmpVal = 0x80;       break;
        case 16:  fltMaxSmpVal = 0x8000;     break;
        case 24:  fltMaxSmpVal = 0x800000;   break;
        case 32:  fltMaxSmpVal = 0x80000000; break;
        default:
          return cwLogError(kInvalidArgRC,"Audio file invalid sample word size:%i bits.",p->info.bits);
      }

      double         dblMaxSmpVal = fltMaxSmpVal;

      // initialize the audio ptr buffers
      for(i=0; i<chCnt; ++i)
      {
        ptrBuf[i] = buf + (i*bufFrmCnt);

        if( dbuf != NULL )
          dPtrBuf[i] = dbuf[i];

        if( fbuf != NULL )
          fPtrBuf[i] = fbuf[i];

      }

      // 
      for(totalReadCnt=0; totalReadCnt<totalFrmCnt && p->curFrmIdx < p->info.frameCnt; totalReadCnt+=frmCnt)
      {
        unsigned actualReadFrmCnt = 0;
        frmCnt = std::min( p->info.frameCnt - p->curFrmIdx, std::min( totalFrmCnt-totalReadCnt, bufFrmCnt ) );

        // fill the integer audio buffer from the file
        if((rc = _readInt( h, frmCnt, chIdx, chCnt, ptrBuf, &actualReadFrmCnt, false )) != kOkRC )
          return rc;

        if( actualFrmCntPtr != NULL )
          *actualFrmCntPtr += actualReadFrmCnt;

        // convert the integer buffer to floating point
        for(i=0; i<chCnt; ++i)
        {

          int* sp = ptrBuf[i];

          if( fbuf != NULL )
          {

            float* dp  = fPtrBuf[i];
            float* ep = dp + frmCnt;

            if( sumFl )
            {
              for(; dp<ep; ++dp,++sp)
                *dp += ((float)*sp) / fltMaxSmpVal;

            }
            else
            {
              for(; dp<ep; ++dp,++sp)
                *dp = ((float)*sp) / fltMaxSmpVal;
            }

            assert( dp <= fbuf[i] + totalFrmCnt );

            fPtrBuf[i] = dp;
          }
          else
          {
            double* dp = dPtrBuf[i];
            double* ep = dp + frmCnt;

            if( sumFl )
            {
              for(; dp<ep; ++dp,++sp)
                *dp += ((double)*sp) / dblMaxSmpVal;                
            }
            else
            {
              for(; dp<ep; ++dp,++sp)
                *dp = ((double)*sp) / dblMaxSmpVal;      
            }

            assert( dp <= dbuf[i] + totalFrmCnt );
            dPtrBuf[i] = dp;
          }
      
        }
      }


      return rc;
    }

    rc_t _readFloat( handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, float** buf, unsigned* actualFrmCntPtr, bool sumFl )
    {
      return _readRealSamples(h,frmCnt,chIdx,chCnt,buf, NULL, actualFrmCntPtr, sumFl );
    }

    rc_t _readDouble( handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, double** buf, unsigned* actualFrmCntPtr, bool sumFl )
    {
      return _readRealSamples(h,frmCnt,chIdx,chCnt,NULL, buf, actualFrmCntPtr, sumFl );
    }

    

    
    rc_t _get( handle_t& h, const char* fn, unsigned begFrmIdx, info_t* afInfoPtr )
    {
      rc_t rc;

      if((rc = open( h, fn, afInfoPtr )) != kOkRC )
        if( begFrmIdx > 0 )
          rc = seek( h, begFrmIdx );
       
      return rc;
    }

    rc_t     _getInt( const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, int**    buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr, bool sumFl )
    {
      rc_t     rc;
      handle_t h;

      if((rc = _get(h,fn,begFrmIdx,afInfoPtr)) == kOkRC )        
        rc = _readInt(h, frmCnt, chIdx,  chCnt, buf, actualFrmCntPtr, sumFl );

      close(h);
  
      return rc;
    }

    rc_t     _getFloat(  const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, float**  buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr, bool sumFl )
    {
      rc_t     rc;
      handle_t h;

      if((rc = _get(h,fn,begFrmIdx,afInfoPtr)) == kOkRC )
        rc = _readFloat(h, frmCnt, chIdx,  chCnt, buf, actualFrmCntPtr, sumFl );

      close(h);
  
      return rc;
    }

    rc_t     _getDouble( const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, double** buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr, bool sumFl )
    {
      rc_t     rc;
      handle_t h;

      if((rc = _get(h,fn,begFrmIdx,afInfoPtr)) == kOkRC )
        rc = _readDouble(h, frmCnt, chIdx,  chCnt, buf, actualFrmCntPtr, sumFl );

      close(h);
  
      return rc;
    }

    rc_t    _writeRealSamples( handle_t h, unsigned frmCnt, unsigned chCnt, const void*  srcPtrPtr, unsigned realSmpByteCnt )
    {
      rc_t  rc = kOkRC;
      af_t* p  = _handleToPtr(h);

      unsigned         bufFrmCnt = 1024;
      unsigned         wrFrmCnt  = 0;
      unsigned         i         = 0;
      int              maxSmpVal = 0;

      int              buf[   chCnt * bufFrmCnt ];
      int*             srcCh[ chCnt ];
  
      for(i=0; i<chCnt; ++i)
        srcCh[i] = buf + (i*bufFrmCnt);

      switch( p->info.bits )
      {
        case 8:   maxSmpVal = 0x7f;       break;
        case 16:  maxSmpVal = 0x7fff;     break;
        case 24:  maxSmpVal = 0x7fffff;   break;
        case 32:  maxSmpVal = 0x7fffffb0; break; // Note: the full range is not used for 32 bit numbers
        default:                                 // because it was found to cause difficult to detect overflows
          { assert(0); }                         // when the signal approached full scale. 
      }

      // duplicate the audio buffer ptr array - this will allow the buffer ptr's to be changed
      // during the float to int conversion without changing the ptrs passed in from the client
      const void* ptrArray[ chCnt ];
      memcpy(ptrArray,srcPtrPtr,sizeof(ptrArray));

      const float**  sfpp = (const float**)ptrArray;
      const double** sdpp = (const double**)ptrArray;

      while( wrFrmCnt < frmCnt )
      {
        unsigned n = std::min( frmCnt - wrFrmCnt, bufFrmCnt );

        for(i=0; i<chCnt; ++i)
        {
          int*       obp = srcCh[i];

          switch( realSmpByteCnt )
          {
            case 4:
              {
                const float* sbp = sfpp[i];
                const float* sep = sbp + n;
                for(;sbp<sep; ++sbp)
                {
                  *obp++ = (int)fmaxf(-maxSmpVal,fminf(maxSmpVal, *sbp * maxSmpVal));
                }

                sfpp[i] = sbp;
              }
              break;
 
            case 8:
              {
                const double* sbp = sdpp[i];
                const double* sep = sbp + n;
                for(; sbp<sep; ++sbp)
                {
                  *obp++ = (int)fmax(-maxSmpVal,fmin(maxSmpVal,*sbp * maxSmpVal));
                }
                sdpp[i] = sbp;
              }
              break;

            default:
              { assert(0); }
          }  
        }

        if((rc = writeInt( h, n, chCnt, srcCh )) != kOkRC )
          break;

        wrFrmCnt += n;
      }

      return rc;
    }
    
    void _test( const char* audioFn )
    {
      info_t afInfo;
      rc_t   rc;

      // open an audio file
      handle_t     afH;
      if((rc = open( afH, audioFn, &afInfo )) == kOkRC )
      {        
        report( afH, log::globalHandle(), 0, 0);
        close(afH);
      }
      
    }

    rc_t _testSine( const object_t* cfg )
    {
      rc_t        rc;
      double      srate, hz, gain, secs;
      unsigned    bits;
      const char* fn = nullptr;

      if((rc = cfg->getv("fn",fn,"srate",srate,"bits",bits,"hz",hz,"gain",gain,"secs",secs)) != kOkRC )
        return cwLogError(kSyntaxErrorRC,"Invalid parameter to audio file sine test.");

      char* afn = filesys::expandPath(fn);
      rc = sine( afn, srate, bits, hz, gain, secs );
      mem::release(afn);
      
      return rc;
    }

    rc_t _testReport( const object_t* cfg )
    {
      rc_t        rc     = kOkRC;
      const char* fn     = nullptr;
      unsigned    begIdx = 0;
      unsigned    frmN   = 0;
      
      if((rc = cfg->getv("fn",fn,"begIdx",begIdx,"frmCnt",frmN)) != kOkRC )
        return cwLogError(kSyntaxErrorRC,"Invalid parameter to audio file report test.");
      
      char* afn = filesys::expandPath(fn);
      rc = reportFn(afn,log::globalHandle(),begIdx,frmN);
      mem::release(afn);
      
      return rc;
    }
  }
}


cw::rc_t cw::audiofile::open( handle_t& h, const char* fn, info_t* info )
{
  rc_t rc;
  if((rc = close(h)) != kOkRC )
    return rc;

  af_t* p = mem::allocZ<af_t>(1);

  // read the file header
  if((rc = _open(p, fn, "rb" )) != kOkRC )
    goto errLabel;

  // seek to the first sample offset
  if((rc = _seek(p,p->smpByteOffs,SEEK_SET)) != kOkRC )
    goto errLabel;

  p->fn = mem::duplStr( fn );

  if( info != NULL)
    memcpy(info,&p->info,sizeof(info_t));
  
  h.set(p);
 errLabel:
  return rc;
}

    
cw::rc_t cw::audiofile::create( handle_t& h, const char* fn, double srate, unsigned bits, unsigned chCnt )
{
  rc_t rc = kOkRC;
  af_t* p = nullptr;
  
  if((rc = close(h)) != kOkRC )
    return rc;

  if( fn == NULL || strlen(fn)==0 )
    return cwLogError(kInvalidArgRC,"Audio file create failed. The file name is blank.");
  
  p = mem::allocZ<af_t>(1);

  if((rc =  _filenameToAudioFileType(fn, p->info.flags )) != kOkRC )
    goto errLabel;
  

  // open the file for writing
  if((p->fp = fopen(fn,"wb")) == NULL )
  {
    rc = cwLogError(kOpenFailRC,"Audio file create failed on '%s'.",cwStringNullGuard(fn));
    goto errLabel;
  }
  else
  {
    p->fn            = mem::duplStr( fn );
    p->info.srate    = srate;
    p->info.bits     = bits;
    p->info.chCnt    = chCnt;
    p->info.frameCnt = 0;
    p->flags         = kWriteAudioGutsFl;
    
    // set the swap flags
    bool swapFl = cwIsFlag(p->info.flags,kWavAfFl) ?  _cmWavSwapFl :  _cmAifSwapFl;
    
    p->info.flags = cwEnaFlag( p->info.flags, kSwapAfFl,        swapFl);
    p->info.flags = cwEnaFlag( p->info.flags, kSwapSamplesAfFl, swapFl);
    
    if((rc = _writeHdr(p)) != kOkRC )
      goto errLabel;
    
  
    h.set(p);
  }
 errLabel:
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;
}
    
cw::rc_t cw::audiofile::close( handle_t& h )
{
  rc_t rc = kOkRC;
  if( !h.isValid() )
    return rc;

  af_t* p = _handleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;
  
  h.clear();
  return rc;
}



bool     cw::audiofile::isOpen( handle_t h )
{
  if( !h.isValid() )
    return false;
 
  return _handleToPtr(h)->fp != NULL;
}


bool   cw::audiofile::isEOF(      handle_t h )
{
  af_t* p  = _handleToPtr(h);
  return  (p->curFrmIdx >= p->info.frameCnt) || (p->fp==NULL) ||  feof(p->fp) ? true : false;
}

unsigned   cw::audiofile::tell(       handle_t h )
{
  af_t* p  = _handleToPtr(h);
  return p->curFrmIdx;
}

cw::rc_t     cw::audiofile::seek(       handle_t h, unsigned frmIdx )
{
  rc_t  rc = kOkRC;
  af_t* p  = _handleToPtr(h);
  
  if((rc = _seek(p,p->smpByteOffs + (frmIdx * p->info.chCnt * (p->info.bits/8)), SEEK_SET)) != kOkRC )
    return rc;

  p->curFrmIdx = frmIdx;

  return rc;

}


cw::rc_t     cw::audiofile::readInt(    handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, int**    buf, unsigned* actualFrmCntPtr )
{ return _readInt( h, frmCnt, chIdx, chCnt, buf, actualFrmCntPtr, false ); }

cw::rc_t     cw::audiofile::readFloat(  handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, float**  buf, unsigned* actualFrmCntPtr )
{ return _readFloat( h, frmCnt, chIdx, chCnt, buf, actualFrmCntPtr, false ); }

cw::rc_t     cw::audiofile::readDouble( handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, double** buf, unsigned* actualFrmCntPtr )
{ return _readDouble( h, frmCnt, chIdx, chCnt, buf, actualFrmCntPtr, false ); }

cw::rc_t     cw::audiofile::readSumInt(    handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, int**    buf, unsigned* actualFrmCntPtr )
{ return _readInt( h, frmCnt, chIdx, chCnt, buf, actualFrmCntPtr, true ); }

cw::rc_t     cw::audiofile::readSumFloat(  handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, float**  buf, unsigned* actualFrmCntPtr )
{ return _readFloat( h, frmCnt, chIdx, chCnt, buf, actualFrmCntPtr, true ); }

cw::rc_t     cw::audiofile::readSumDouble( handle_t h, unsigned frmCnt, unsigned chIdx, unsigned chCnt, double** buf, unsigned* actualFrmCntPtr )
{ return _readDouble( h, frmCnt, chIdx, chCnt, buf, actualFrmCntPtr, true ); }


cw::rc_t     cw::audiofile::getInt(    const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, int**    buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr )
{ return _getInt( fn, begFrmIdx, frmCnt, chIdx, chCnt, buf, actualFrmCntPtr, afInfoPtr, false ); }

cw::rc_t     cw::audiofile::getFloat(  const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, float**  buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr )
{ return _getFloat( fn, begFrmIdx, frmCnt, chIdx, chCnt, buf, actualFrmCntPtr, afInfoPtr, false ); }

cw::rc_t     cw::audiofile::getDouble( const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, double** buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr )
{ return _getDouble( fn, begFrmIdx, frmCnt, chIdx, chCnt, buf, actualFrmCntPtr, afInfoPtr, false); }

cw::rc_t     cw::audiofile::getSumInt(    const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, int**    buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr )
{ return _getInt( fn, begFrmIdx, frmCnt, chIdx, chCnt, buf, actualFrmCntPtr, afInfoPtr, true ); }

cw::rc_t     cw::audiofile::getSumFloat(  const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, float**  buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr )
{ return _getFloat( fn, begFrmIdx, frmCnt, chIdx, chCnt, buf, actualFrmCntPtr, afInfoPtr, true ); }

cw::rc_t     cw::audiofile::getSumDouble( const char* fn, unsigned begFrmIdx, unsigned frmCnt, unsigned chIdx, unsigned chCnt, double** buf, unsigned* actualFrmCntPtr, info_t* afInfoPtr )
{ return _getDouble( fn, begFrmIdx, frmCnt, chIdx, chCnt, buf, actualFrmCntPtr, afInfoPtr, true ); }



cw::rc_t    cw::audiofile::writeInt(    handle_t h, unsigned frmCnt, unsigned chCnt, int** srcPtrPtr )
{
  rc_t     rc          = kOkRC;
  af_t*    p           = _handleToPtr(h);
  unsigned bytesPerSmp = p->info.bits / 8;
  unsigned bufFrmCnt   = 1024;
  unsigned bufByteCnt  = bufFrmCnt * bytesPerSmp;
  unsigned ci;
  unsigned wrFrmCnt    = 0;
  char     buf[ bufByteCnt * chCnt ];
  
  while( wrFrmCnt < frmCnt )
  {
    unsigned n = std::min( frmCnt-wrFrmCnt, bufFrmCnt );

    // interleave each channel into buf[]
    for(ci=0; ci<chCnt; ++ci)
    {
      // get the begin and end source pointers
      const int* sbp = srcPtrPtr[ci] + wrFrmCnt;
      const int* sep = sbp + n;

      // 8 bit samples can't be byte swapped
      if( p->info.bits == 8 )
      {
        char*  dbp = buf + ci;
        for(; sbp < sep; dbp+=chCnt )
          *dbp = (char)*sbp++;             
      }
      else
      {
        // if the samples do need to be byte swapped
        if( cwIsFlag(p->info.flags,kSwapSamplesAfFl) )
        {
          switch( p->info.bits )
          {
            case 16:
              {
                short*  dbp = (short*)buf;
                for(dbp+=ci; sbp < sep; dbp+=chCnt, ++sbp )
                  *dbp = _cmAfSwap16((short)*sbp);
              }
              break;

            case 24:
              {
                unsigned char* dbp = (unsigned char*)buf;
                for( dbp+=(ci*3); sbp < sep; dbp+=(3*chCnt), ++sbp)
                {
                  unsigned char* s = (unsigned char*)sbp;
                  dbp[0] = s[2];
                  dbp[1] = s[1];
                  dbp[2] = s[0];
                }
              }
              break;
          

            case 32:
              {
                int*  dbp = (int*)buf;
                for(dbp+=ci; sbp < sep; dbp+=chCnt, ++sbp )
                  *dbp = _cmAfSwap32(*sbp);
              }
              break;

            default:
              { assert(0);}
          } 

        }
        else // interleave without byte swapping
        {
          switch( p->info.bits )
          {
            case 16:
              {
                short*  dbp = (short*)buf;
                for(dbp+=ci; sbp < sep; dbp+=chCnt, ++sbp )
                  *dbp = (short)*sbp;
              }
              break;
              
            case 24:
              {
                unsigned char* dbp = (unsigned char*)buf;
                for( dbp+=(ci*3); sbp < sep; dbp+=(3*chCnt), ++sbp)
                {
                  unsigned char* s = (unsigned char*)sbp;
                  dbp[0] = s[0];
                  dbp[1] = s[1];
                  dbp[2] = s[2];
                }
              }
              break;
          

            case 32:
              {
                int*  dbp = (int*)buf;
                for(dbp+=ci; sbp < sep; dbp+=chCnt, ++sbp )
                  *dbp = *sbp;
              }
              break;

            default:
              { assert(0);}
          } // switch
        } // don't swap
      } // 8 bits
    } // ch

    // advance the source pointer index
    wrFrmCnt+=n;

    if( fwrite( buf, n*bytesPerSmp*chCnt, 1, p->fp ) != 1)
    {
      rc = cwLogError(kWriteFailRC,"Audio file write failed on '%s'.",cwStringNullGuard(p->fn));
      break;
    }
    
  } // while

  p->info.frameCnt += wrFrmCnt;

  return rc;
}


cw::rc_t    cw::audiofile::writeFloat(  handle_t h, unsigned frmCnt, unsigned chCnt, float**  bufPtrPtr )
{ return _writeRealSamples(h,frmCnt,chCnt,bufPtrPtr,sizeof(float)); }

cw::rc_t    cw::audiofile::writeDouble( handle_t h, unsigned frmCnt, unsigned chCnt, double** bufPtrPtr )
{ return _writeRealSamples(h,frmCnt,chCnt,bufPtrPtr,sizeof(double)); }



cw::rc_t    cw::audiofile::minMaxMean( handle_t h, unsigned chIdx, float* minPtr, float* maxPtr, float* meanPtr )
{
  assert( minPtr != NULL && maxPtr != NULL && meanPtr != NULL );

  *minPtr = -FLT_MAX;
  *maxPtr = FLT_MAX;
  *meanPtr = 0;

  rc_t     rc        = kOkRC;
  af_t*    p         = _handleToPtr(h);
  unsigned orgFrmIdx = p->curFrmIdx;

  if((rc = seek(h,0)) != kOkRC )
    return rc;

  *minPtr = FLT_MAX;
  *maxPtr = -FLT_MAX;

  unsigned   bufN       = 1024;
  float buf[ bufN ];
  unsigned   frmCnt     = 0;
  unsigned   actualFrmCnt;
  float* bufPtr[1] = { &buf[0] };

  for(; frmCnt<p->info.frameCnt; frmCnt+=actualFrmCnt) 
  {
    actualFrmCnt = 0;
    unsigned n = std::min( p->info.frameCnt-frmCnt, bufN );
 
    if((rc = readFloat(h, n, chIdx, 1, bufPtr, &actualFrmCnt)) != kOkRC )
      return rc;

    const float* sbp = buf;
    const float* sep = buf + actualFrmCnt;

    for(; sbp < sep; ++sbp )
    {
      *meanPtr += *sbp;
      if( *minPtr > *sbp )
        *minPtr = *sbp;
      if( *maxPtr < *sbp )
        *maxPtr = *sbp;
    }
    
  }

  if( frmCnt > 0 )
    *meanPtr /= frmCnt;
  else
    *minPtr = *maxPtr = 0;
 
  return seek( h, orgFrmIdx );

}

cw::rc_t    cw::audiofile::writeFileInt(    const char* fn, double srate, unsigned bits, unsigned frmCnt, unsigned chCnt, int**  bufPtrPtr )
{
  rc_t     rc;
  handle_t h;
  
  if(( rc = create(h,fn,srate,bits,chCnt)) != kOkRC )
  {
    rc = writeInt( h, frmCnt, chCnt, bufPtrPtr );
    
    close(h);
  }

  return rc;  
}

cw::rc_t    cw::audiofile::writeFileFloat(  const char* fn, double srate, unsigned bits, unsigned frmCnt, unsigned chCnt, float**  bufPtrPtr )
{
  rc_t     rc;
  handle_t h;
  
  if(( rc = create(h,fn,srate,bits,chCnt)) == kOkRC )
  {
    rc = writeFloat( h, frmCnt, chCnt, bufPtrPtr );
    
    close(h);
  }

  return rc;  
}

cw::rc_t    cw::audiofile::writeFileDouble( const char* fn, double srate, unsigned bits, unsigned frmCnt, unsigned chCnt, double** bufPtrPtr )
{
  rc_t     rc;
  handle_t h;
  
  if(( rc = create(h,fn,srate,bits,chCnt)) == kOkRC )
  {
    rc = writeDouble( h, frmCnt, chCnt, bufPtrPtr );
    
    close(h);
  }

  return rc;  
}


cw::rc_t cw::audiofile::minMaxMeanFn( const char* fn, unsigned chIdx, float* minPtr, float* maxPtr, float* meanPtr )
{
  rc_t rc = kOkRC;
  handle_t h;

  if((rc = open(h,fn,nullptr)) == kOkRC )
  {
    rc = minMaxMean( h, chIdx, minPtr, maxPtr, meanPtr ); 
    close(h);
  }

  return rc;
}



const char* cw::audiofile::name( handle_t h )
{
  af_t* p = _handleToPtr(h);
  return p->fn;
}

unsigned    cw::audiofile::channelCount( handle_t h )
{
  af_t* p = _handleToPtr(h);
  return p->info.chCnt;
}

double      cw::audiofile::sampleRate( handle_t h )
{
  af_t* p = _handleToPtr(h);
  return p->info.srate;
}


cw::rc_t cw::audiofile::getInfo(   const char* fn, info_t* infoPtr  )
{
  rc_t rc = kOkRC;
  handle_t h;

  if((rc = open(h,fn,infoPtr)) == kOkRC )
    close(h);

  return rc;
}


void   cw::audiofile::printInfo( const info_t* infoPtr, log::handle_t logH )
{
  const char*  typeStr = "AIFF";
  const char*  swapStr = "";
  const char*  aifcStr = "";
  unsigned i;
  
  if( cwIsFlag(infoPtr->flags,kWavAfFl) )
    typeStr = "WAV";

  if( cwIsFlag(infoPtr->flags,kSwapAfFl) )
    swapStr = "Swap:On";

  if( cwIsFlag(infoPtr->flags,kAifcAfFl))
    aifcStr = "AIFC";

  cwLogPrintH(logH,"bits:%i chs:%i srate:%f frames:%i type:%s %s %s\n", infoPtr->bits, infoPtr->chCnt, infoPtr->srate, infoPtr->frameCnt, typeStr, swapStr, aifcStr );

  for(i=0; i<infoPtr->markerCnt; ++i)
    cwLogPrintH(logH,"%i %i %s\n", infoPtr->markerArray[i].id, infoPtr->markerArray[i].frameIdx, infoPtr->markerArray[i].label);

  if( strlen(infoPtr->bextRecd.desc) )
    cwLogPrintH(logH,"Bext Desc:%s\n",infoPtr->bextRecd.desc );

  if( strlen(infoPtr->bextRecd.origin) )
    cwLogPrintH(logH,"Bext Origin:%s\n",infoPtr->bextRecd.origin );

  if( strlen(infoPtr->bextRecd.originRef) )
    cwLogPrintH(logH,"Bext Origin Ref:%s\n",infoPtr->bextRecd.originRef );

  if( strlen(infoPtr->bextRecd.originDate) )
    cwLogPrintH(logH,"Bext Origin Date:%s\n",infoPtr->bextRecd.originDate );

  if( strlen(infoPtr->bextRecd.originTime ) )
    cwLogPrintH(logH,"Bext Origin Time:%s\n",infoPtr->bextRecd.originTime );

  cwLogPrintH(logH,"Bext time high:%i low:%i  0x%x%x\n",infoPtr->bextRecd.timeRefHigh,infoPtr->bextRecd.timeRefLow, infoPtr->bextRecd.timeRefHigh,infoPtr->bextRecd.timeRefLow);

}

cw::rc_t     cw::audiofile::report( handle_t h, log::handle_t logH, unsigned frmIdx, unsigned frmCnt )
{
  rc_t  rc = kOkRC;
  af_t* p  = _handleToPtr(h);
  
  
  cwLogPrintH(logH,"function cm_audio_file_test()\n");
  cwLogPrintH(logH,"#{\n");
  printInfo(&p->info,logH);
  cwLogPrintH(logH,"#}\n");

  if( frmCnt == kInvalidCnt )
    frmCnt = p->info.frameCnt;
  

  float           buf[ p->info.chCnt * frmCnt ];
  float*          bufPtr[p->info.chCnt];
  unsigned      i,j,cmtFrmCnt=0;

  for(i=0; i<p->info.chCnt; ++i)
    bufPtr[i] = buf + (i*frmCnt);

  if((rc = seek(h,frmIdx)) != kOkRC )
    return rc;
  
  if((rc= readFloat(h,frmCnt,0,p->info.chCnt,bufPtr,&cmtFrmCnt )) != kOkRC)
    return rc;

  cwLogPrintH(logH,"m = [\n");
  for(i=0; i<frmCnt; i++)
  {
    for(j=0; j<p->info.chCnt; ++j)
      cwLogPrintH(logH,"%f ", bufPtr[j][i] );
    cwLogPrintH(logH,"\n");
  }
    cwLogPrintH(logH,"];\nplot(m)\nendfunction\n");

  return rc;
  
}

cw::rc_t     cw::audiofile::reportFn( const char* fn, log::handle_t logH, unsigned frmIdx, unsigned frmCnt )
{
  rc_t     rc;
  handle_t h;

  if((rc = open(h,fn,nullptr)) == kOkRC )
  {
    rc = report(h,logH,frmIdx,frmCnt);
    
    close(h);
  }

  return rc;
}


cw::rc_t     cw::audiofile::setSrate( const char* fn, unsigned srate )
{
  rc_t  rc = kOkRC;
  af_t  af;
  af_t* p  = &af;

  memset(&af,0,sizeof(af));

  if((rc = _open(p, fn, "r+b")) != kOkRC )
    goto errLabel;  

  if( p->info.srate != srate )
  {
    // change the sample rate
    p->info.srate = srate;

    // write the file header
    if((rc = _writeHdr(p)) != kOkRC )
      goto errLabel;
  }
  
 errLabel:
  if( p->fp != NULL )
    fclose(p->fp);
  
  return rc;
}

cw::rc_t     cw::audiofile::sine( const char* fn, double srate, unsigned bits, double hz, double gain, double secs )
{
  rc_t        rc    = kOkRC;
  unsigned    bN    = srate * secs;
  float* b     = mem::alloc<float>(bN);
  unsigned    chCnt = 1;

  unsigned    i;
  for(i=0; i<bN; ++i)
    b[i] = gain * sin(2.0*M_PI*hz*i/srate);

  if((rc = writeFileFloat(fn, srate, bits, bN, chCnt, &b)) != kOkRC)
    return rc;

  return rc;
}

cw::rc_t      cw::audiofile::mix( const char* fnV[], const float* gainV, unsigned srcN, const char* outFn, unsigned outBits )
{
  rc_t rc = kOkRC;
  
  if( srcN == 0 )
    return rc;

  unsigned       maxFrmN  = 0;
  unsigned       maxChN   = 0;
  double         srate    = 0;
  handle_t       hV[ srcN ];
  handle_t       oH;

  // open each source file and determine the output file audio format
  for(unsigned i=0; i<srcN; ++i)
  {
    info_t info;

    if((rc = open( hV[i], fnV[i], &info )) != kOkRC )
    {
      rc = cwLogError(kOpFailRC,"Unable to open the audio mix source file '%s'.", fnV[i] );
      goto errLabel;
    }

    if( srate == 0 )
      srate = info.srate;
    
    if( srate != info.srate )
    {
      rc = cwLogError(kInvalidArgRC,"The sample rate (%f) of '%s' does not match the sample rate (%f) of '%s'.", info.srate, fnV[i], srate, fnV[0] );
      goto errLabel;
    }
        
    if( maxFrmN < info.frameCnt )
      maxFrmN = info.frameCnt;

    if( maxChN < info.chCnt )
      maxChN = info.chCnt;
  }

  // create the output file
  if((rc = create( oH, outFn, srate, outBits, maxChN)) != kOkRC )
    goto errLabel;
  else
  {
    const unsigned kBufFrmN = 1024;
    float    ibuf[   maxChN * kBufFrmN ];
    float    obuf[   maxChN * kBufFrmN ];
    float*   iChBuf[ maxChN ];
    float*   oChBuf[ maxChN ];

    // setup the in/out channel buffers
    for(unsigned i=0; i<maxChN; ++i)
    {
      iChBuf[i] = ibuf + (i*kBufFrmN);
      oChBuf[i] = obuf + (i*kBufFrmN);
    }

    // for each frame
    for(unsigned frmIdx=0; frmIdx < maxFrmN; frmIdx += kBufFrmN )
    {
      // zero the mix buf
      memset(obuf,0,sizeof(obuf));

      unsigned maxActualFrmN = 0;

      // for each source
      for(unsigned i=0; i<srcN; ++i)
      {      
        unsigned actualFrmN = 0;
        
        // read a buffer of audio from the ith source.
        if((rc = readFloat( hV[i], kBufFrmN, 0, channelCount(hV[i]), iChBuf, &actualFrmN)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"Read failed on source '%s'.", name(hV[i]));
          goto errLabel;
        }

        // mix the input buffer into the output buffer
        for(unsigned j=0; j<channelCount(hV[i]); ++j)
          for(unsigned k=0; k<actualFrmN; ++k)
            oChBuf[j][k] += gainV[i] * iChBuf[j][k];

        // track the max. count of samples actually read for this buffer cycle
        if( actualFrmN > maxActualFrmN )
          maxActualFrmN = actualFrmN;

      }

      // write the mixed output buffer
      if((rc = writeFloat(oH, maxActualFrmN, maxChN, oChBuf )) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"Write failed on output file '%s'.", outFn );
        goto errLabel;
      }
    }
  }
  
  
 errLabel:
  if( rc != kOkRC )
    cwLogError(kOpFailRC,"Mix failed.");

  // close the source audio files
  for(unsigned i=0; i<srcN; ++i)
    close(hV[i]);

  close(oH); // close the output file
  
  return rc;
}

cw::rc_t       cw::audiofile::mix( const object_t* cfg )
{
  rc_t            rc      = kOkRC;
  const object_t* srcL    = nullptr;
  const char*     oFn      = nullptr;
  unsigned        outBits = 16;

  // read the top level cfg record
  if((rc = cfg->getv("outFn",oFn,"outBits",outBits,"srcL",srcL)) != kOkRC )
    goto errLabel;
  else
  {
    char*       outFn = filesys::expandPath(oFn);
    unsigned    srcN = srcL->child_count();
    const char* fnV[ srcN ];
    float       gainV[ srcN ];

    memset(fnV,0,sizeof(fnV));

    // read each source record
    for(unsigned i=0; i<srcN; ++i)
    {
      const char* fn = nullptr;
      if((rc = srcL->child_ele(i)->getv("gain",gainV[i],"src",fn)) != kOkRC )
        rc = cwLogError(kSyntaxErrorRC,"Mix source index %i syntax error.");
      else
        fnV[i] = filesys::expandPath(fn);      
    }

    if( rc == kOkRC )
      rc = mix( fnV, gainV, srcN, outFn, outBits);

    mem::free(outFn);
    for(unsigned i=0; i<srcN; ++i)
      mem::free((char*)fnV[i]);

  }
  
 errLabel:
  return rc;
}


/// [example]

cw::rc_t cw::audiofile::test( const object_t* cfg )
{
 
  rc_t rc = kOkRC;
  const object_t* o;
  
  if((o = cfg->find("sine")) != nullptr )
    rc = _testSine(o);

  if((o = cfg->find("rpt")) != nullptr )
    rc = _testReport(o);

  if((o = cfg->find("mix")) != nullptr )
    rc = _testReport(o);

  
  /*
  switch( argc )
  {
    case 3:
      //_test(argv[2],&ctx->rpt);
      reportFn(argv[2],0,0,&ctx->rpt);
      break;
      
    case 4:
      {
        errno = 0;
        long srate =  strtol(argv[3], NULL, 10);
        if( srate == 0 && errno != 0 )
          rc = cwLogError(kInvalidArgRC,"Invalid sample rate argument to test().");
        else
          setSrate(argv[2],srate);                
      }
      break;

    case 8:
      {
        errno = 0;
        double   srate = strtod(argv[3],NULL);
        unsigned bits  = strtol(argv[4],NULL,10);
        double   hz    = strtod(argv[5],NULL);
        double   gain  = strtod(argv[6],NULL);
        double   secs  = strtod(argv[7],NULL);
        
        if( errno != 0 )
          rc = cwLogError(kInvalidArgRC,"Invalid arg. to test().");
        
        sine( ctx, argv[2], srate, bits,  hz, gain,  secs );
      }
      break;

    default:
      rc = cwLogError(kInvalidArgRC,"Invalid argument count to test().");
      break;
  }
  */
  return rc;
}


/// [example]
