#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwTextBuf.h"
#include "cwThread.h"
#include "cwAudioDevice.h"
#include "cwAudioDeviceAlsa.h"

#include "alsa/asoundlib.h"
#include <unistd.h> // usleep

namespace cw
{
  namespace audio
  {
    namespace device
    {
      namespace alsa
      {
        enum { kDfltPeriodsPerBuf = 2, kPollfdsArrayCnt=2 };

        enum { kInFl=0x01, kOutFl=0x02 };

        struct alsa_str;

        typedef struct devRecd_str
        {
          struct alsa_str*     rootPtr;
          unsigned             devIdx;
          char*                nameStr;
          char*                descStr;
          unsigned             flags;

          unsigned             framesPerCycle; // samples per sub-buffer 
          unsigned             periodsPerBuf;  // sub-buffers per buffer
          snd_async_handler_t* ahandler;
          unsigned             srate;          // device sample rate

          unsigned             iChCnt;         // ch count 
          unsigned             oChCnt;

          unsigned             iBits;          // bits per sample
          unsigned             oBits;

          bool                 iSignFl;        // sample type is signed
          bool                 oSignFl;

          bool                 iSwapFl;        // swap the sample bytes
          bool                 oSwapFl;

          unsigned             iSigBits;       // significant bits in each sample beginning
          unsigned             oSigBits;       // with the most sig. bit.


          device::sample_t*    iBuf;    // iBuf[ iFpc * iChCnt ]
          device::sample_t*    oBuf;    // oBuf[ oFpc * oChCnt ]

          unsigned             oBufCnt;  // count of buffers written

          unsigned             iFpC;    // buffer frames per cycle  (in ALSA this is call period_size)
          unsigned             oFpC;

          snd_pcm_t*           iPcmH;    // device handle
          snd_pcm_t*           oPcmH;

          unsigned             iCbCnt;   // callback count 
          unsigned             oCbCnt;

          unsigned             iErrCnt;  // error count
          unsigned             oErrCnt;

          device::cbFunc_t     cbFunc;    // user callback
          void*                cbArg;

        } devRecd_t;

        typedef struct poll_str
        {
          devRecd_t*     devPtr;
          bool           inputFl;
          unsigned       fdsCnt;
        } pollfdsDesc_t;

        typedef struct alsa_str
        {
          driver_t          driver;
          devRecd_t*        devArray        = nullptr; // array of device records
          unsigned          devCnt          = 0;       // count of actual dev recds in devArray[]
          unsigned          devAllocCnt     = 0;       // count of dev recds allocated in devArray[]
          bool              asyncFl         = false;   // true=use async callback false=use polling thread
          
          thread::handle_t  thH;                       // polling thread
          
          unsigned          pollfdsAllocCnt = 0;       // 2*devCnt (max possible in+out handles)
          struct pollfd*    pollfds         = nullptr; // pollfds[ pollfdsAllocCnt ]
          pollfdsDesc_t    *pollfdsDesc     = nullptr; // pollfdsDesc[ pollfdsAllocCnt ]
          unsigned          pollfdsCnt      = 0;       // count of active recds in pollfds[] and pollfdsDesc[]
  
        } alsa_t;
        
        alsa_t* _handleToPtr( handle_t h) { return handleToPtr<handle_t,alsa_t>(h); }

        rc_t _alsaErrorImpl( int alsaRC, const char* func, const char* fn, int lineNumb, const char* fmt, ... )
        {
          rc_t    rc = kOkRC;
          va_list vl0;
          va_start(vl0,fmt);
          
          if( alsaRC == 0 )
          {
            rc = logMsg( logGlobalHandle(), kError_LogLevel, func, fn, lineNumb, 0, kOpFailRC, fmt, vl0 );            
          }
          else
          {
            va_list vl1;
            va_copy(vl1,vl0);
            
            int n = vsnprintf(NULL,0,fmt,vl0);
            char msg0[n+1];
            int m = vsnprintf(msg0,n+1,fmt,vl1);

            cwAssert(n==m);
            va_end(vl1);

            const char* fmt1 = "%s ALSA Error:%s";
            n = snprintf(NULL,0,fmt1,msg0,snd_strerror(alsaRC));
            char msg1[n+1];
            m = snprintf(msg1,n+1,fmt1,msg0,snd_strerror(alsaRC));
            
            rc = logMsg( logGlobalHandle(), kError_LogLevel, func, fn, lineNumb, 0, kOpFailRC, "%s", msg1 );
          }
          
          va_end(vl0);
          
          return rc;
        }

#define _alsaError( alsaRC, fmt, ...) _alsaErrorImpl( alsaRC, __FUNCTION__, __FILE__, __LINE__, fmt,  ##__VA_ARGS__ )

        rc_t  _alsaSetupErrorImpl( int alsaRC, bool inputFl, const devRecd_t* drp, const char* func, const char* fn, int lineNumb, const char* fmt, ... )
        {
          va_list vl0,vl1;
          va_start(vl0,fmt);
          va_copy(vl1,vl0);
          int n = vsnprintf(NULL,0,fmt,vl0);
          char msg[n+1];
          int m = vsnprintf(msg,n+1,fmt,vl1);
          cwAssert(m==n);
          return _alsaErrorImpl( alsaRC, func, fn, lineNumb, "%s for %s '%s' : '%s'.",msg,inputFl ? "INPUT" : "OUTPUT", drp->nameStr, drp->descStr );
        }

#define _alsaSetupError( alsaRC, inputFl, drp, fmt, ...) _alsaSetupErrorImpl( alsaRC, inputFl, drp,  __FUNCTION__, __FILE__, __LINE__, fmt, ##__VA_ARGS__ )

        const char* _pcmStateToString( snd_pcm_state_t state )
        {
          switch( state )
          {
            case SND_PCM_STATE_OPEN:         return "open";
            case SND_PCM_STATE_SETUP:        return "setup";
            case SND_PCM_STATE_PREPARED:     return "prepared";
            case SND_PCM_STATE_RUNNING:      return "running";
            case SND_PCM_STATE_XRUN:         return "xrun";
            case SND_PCM_STATE_DRAINING:     return "draining";
            case SND_PCM_STATE_PAUSED:       return "paused";
            case SND_PCM_STATE_SUSPENDED:    return "suspended";
            case SND_PCM_STATE_DISCONNECTED: return "disconnected";
            case SND_PCM_STATE_PRIVATE1:     return "private1";

          }
          return "<invalid>";
        }

        // Print a report of the audio signal formats this described in this 'snd_pcm_hw_params_t' record.
        void _devReportFormats(  snd_pcm_hw_params_t* hwParams )
        {
          snd_pcm_format_mask_t* mask;

          snd_pcm_format_t fmt[] =
            {
             SND_PCM_FORMAT_S8,
             SND_PCM_FORMAT_U8,
             SND_PCM_FORMAT_S16_LE,
             SND_PCM_FORMAT_S16_BE,
             SND_PCM_FORMAT_U16_LE,
             SND_PCM_FORMAT_U16_BE,
             SND_PCM_FORMAT_S24_LE,
             SND_PCM_FORMAT_S24_BE,
             SND_PCM_FORMAT_U24_LE,
             SND_PCM_FORMAT_U24_BE,
             SND_PCM_FORMAT_S32_LE,
             SND_PCM_FORMAT_S32_BE,
             SND_PCM_FORMAT_U32_LE,
             SND_PCM_FORMAT_U32_BE,
             SND_PCM_FORMAT_FLOAT_LE,
             SND_PCM_FORMAT_FLOAT_BE,
             SND_PCM_FORMAT_FLOAT64_LE,
             SND_PCM_FORMAT_FLOAT64_BE,
             SND_PCM_FORMAT_IEC958_SUBFRAME_LE,
             SND_PCM_FORMAT_IEC958_SUBFRAME_BE,
             SND_PCM_FORMAT_MU_LAW,
             SND_PCM_FORMAT_A_LAW,
             SND_PCM_FORMAT_IMA_ADPCM,
             SND_PCM_FORMAT_MPEG,
             SND_PCM_FORMAT_GSM,
             SND_PCM_FORMAT_SPECIAL,
             SND_PCM_FORMAT_S24_3LE,
             SND_PCM_FORMAT_S24_3BE,
             SND_PCM_FORMAT_U24_3LE,
             SND_PCM_FORMAT_U24_3BE,
             SND_PCM_FORMAT_S20_3LE,
             SND_PCM_FORMAT_S20_3BE,
             SND_PCM_FORMAT_U20_3LE,
             SND_PCM_FORMAT_U20_3BE,
             SND_PCM_FORMAT_S18_3LE,
             SND_PCM_FORMAT_S18_3BE,
             SND_PCM_FORMAT_U18_3LE,
             SND_PCM_FORMAT_U18_3BE,
             SND_PCM_FORMAT_G723_24,
             SND_PCM_FORMAT_G723_24_1B,
             SND_PCM_FORMAT_G723_40,
             SND_PCM_FORMAT_G723_40_1B,
             SND_PCM_FORMAT_DSD_U8,
             //SND_PCM_FORMAT_DSD_U16_LE,
             //SND_PCM_FORMAT_DSD_U32_LE,
             //SND_PCM_FORMAT_DSD_U16_BE,
             //SND_PCM_FORMAT_DSD_U32_BE,
             SND_PCM_FORMAT_UNKNOWN 
            };

          snd_pcm_format_mask_alloca(&mask);

          snd_pcm_hw_params_get_format_mask(hwParams,mask);

          cwLogInfo("Formats: " );
  
          int i;
          for(i=0; fmt[i]!=SND_PCM_FORMAT_UNKNOWN; ++i)
            if( snd_pcm_format_mask_test(mask, fmt[i] ))
              cwLogInfo("%s%s",snd_pcm_format_name(fmt[i]), snd_pcm_format_cpu_endian(fmt[i]) ? " " : " (swap) ");
  
  
        }

        void _devReport( devRecd_t* drp )
        {
          bool       inputFl = true;
          snd_pcm_t* pcmH;
          int        err;
          unsigned   i;

          cwLogInfo("%s %s Device:%s Desc:%s", drp->flags & kInFl ? "IN ":"", drp->flags & kOutFl ? "OUT ":"", drp->nameStr, drp->descStr);
  
          for(i=0; i<2; i++,inputFl=!inputFl)
          {
            if( ((inputFl==true) && (drp->flags&kInFl)) || (((inputFl==false) && (drp->flags&kOutFl))))
            {
              const char* ioLabel = inputFl ? "In " : "Out";

              // attempt to open the sub-device
              if((err = snd_pcm_open(&pcmH,drp->nameStr,inputFl ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK,0)) < 0 )
                _alsaSetupError(err,inputFl,drp,"Attempt to open the PCM handle failed");
              else
              {		
                snd_pcm_hw_params_t* hwParams;
		  
                snd_pcm_hw_params_alloca(&hwParams);
                memset(hwParams,0,snd_pcm_hw_params_sizeof());

                // load the parameter record
                if((err = snd_pcm_hw_params_any(pcmH,hwParams)) < 0 )
                  _alsaSetupError(err,inputFl,drp,"Error obtaining hw param record");
                else
                {
                  unsigned minChCnt=0,maxChCnt=0,minSrate=0,maxSrate=0;
                  snd_pcm_uframes_t minPeriodFrmCnt=0,maxPeriodFrmCnt=0,minBufFrmCnt=0,maxBufFrmCnt=0;
                  int dir;


                  // extract the min channel count
                  if((err = snd_pcm_hw_params_get_channels_min(hwParams, &minChCnt )) < 0 )
                    _alsaSetupError(err,inputFl,drp,"Error getting min. channel count.");

                  // extract the max channel count
                  if((err = snd_pcm_hw_params_get_channels_max(hwParams, &maxChCnt )) < 0 )
                    _alsaSetupError(err,inputFl,drp,"Error getting max. channel count.");

                  // extract the min srate
                  if((err = snd_pcm_hw_params_get_rate_min(hwParams, &minSrate,&dir )) < 0 )
                    _alsaSetupError(err,inputFl,drp,"Error getting min. sample rate.");

                  // extract the max srate
                  if((err = snd_pcm_hw_params_get_rate_max(hwParams, &maxSrate,&dir )) < 0 )
                    _alsaSetupError(err,inputFl,drp,"Error getting max. sample rate.");

                  // extract the min period
                  if((err = snd_pcm_hw_params_get_period_size_min(hwParams, &minPeriodFrmCnt,&dir )) < 0 )
                    _alsaSetupError(err,inputFl,drp,"Error getting min. period frame count.");

                  // extract the max period
                  if((err = snd_pcm_hw_params_get_period_size_max(hwParams, &maxPeriodFrmCnt,&dir )) < 0 )
                    _alsaSetupError(err,inputFl,drp,"Error getting max. period frame count.");

                  // extract the min buf
                  if((err = snd_pcm_hw_params_get_buffer_size_min(hwParams, &minBufFrmCnt )) < 0 )
                    _alsaSetupError(err,inputFl,drp,"Error getting min. period frame count.");

                  // extract the max buffer
                  if((err = snd_pcm_hw_params_get_buffer_size_max(hwParams, &maxBufFrmCnt )) < 0 )
                    _alsaSetupError(err,inputFl,drp,"Error getting max. period frame count.");

                  cwLogInfo("%s chs:%i - %i srate:%i - %i  period:%i - %i buf:%i - %i half duplex only:%s joint duplex:%s",
                    ioLabel,minChCnt,maxChCnt,minSrate,maxSrate,minPeriodFrmCnt,maxPeriodFrmCnt,minBufFrmCnt,maxBufFrmCnt,
                    (snd_pcm_hw_params_is_half_duplex(hwParams)  ? "yes" : "no"),
                    (snd_pcm_hw_params_is_joint_duplex(hwParams) ? "yes" : "no"));
          
                  _devReportFormats( hwParams );
                }

                if((err = snd_pcm_close(pcmH)) < 0)
                  _alsaSetupError(err,inputFl,drp,"Error closing PCM handle");
              }
            }
          }
        }			

        // Called by create() to append a devRecd to the alsa_t.devArray[].
        void _devAppend( alsa_t* p, devRecd_t* drp )
        {
          const int reallocN = 5;
          if( p->devCnt == p->devAllocCnt )
          {
            p->devArray     = memResizeZ<devRecd_t>( p->devArray, p->devAllocCnt + reallocN );
            p->devAllocCnt += reallocN;
          }

          drp->devIdx  = p->devCnt; // set the device index 
          drp->rootPtr = p;         // set the pointer back to the root

          memcpy(p->devArray + p->devCnt, drp, sizeof(devRecd_t));
  
          ++p->devCnt;
        }

        rc_t _devShutdown( alsa_t* p, devRecd_t* drp, bool inputFl )
        {
          int  err;

          snd_pcm_t** pcmH = inputFl ? &drp->iPcmH : &drp->oPcmH;
  
          if( *pcmH != NULL )
          {
            if((err = snd_pcm_close(*pcmH)) < 0 )
            {
              return _alsaSetupError(err,inputFl,drp,"Error closing device handle.");
            }

            *pcmH = NULL;
          }

          return kOkRC;
        }

        int _devOpen( snd_pcm_t** pcmHPtr, const char* devNameStr, bool inputFl )
        {
          int cnt = 0;
          int err;

          do
          {
            if((err = snd_pcm_open(pcmHPtr,devNameStr,inputFl ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK,0)) < 0 )
            {
              cnt++;
              usleep(10000); // sleep for 10 milliseconds
            }

          }while(cnt<100 && err == -EBUSY );

          return err;
        }

        rc_t _destroy( alsa_t* p )
        {
          unsigned    i;
          rc_t        rc = kOkRC;

          if( p->asyncFl==false )
            if((rc = thread::destroy(p->thH)) != kOkRC )
            {
              rc = cwLogError(rc,"Thread destroy failed.");
            }

          for(i=0; i<p->devCnt; ++i)
          {
            _devShutdown(p,p->devArray+i,true);
            _devShutdown(p,p->devArray+i,false);

            memRelease(p->devArray[i].iBuf);
            memRelease(p->devArray[i].oBuf);
            memRelease(p->devArray[i].nameStr);
            memRelease(p->devArray[i].descStr);
  
          }

          memRelease(p->pollfds);
          memRelease(p->pollfdsDesc);

          memRelease(p->devArray);
          p->devAllocCnt = 0;
          p->devCnt      = 0;

          memRelease(p);

          return rc;            
        }
        
        void _devXrun_recover( snd_pcm_t* pcmH, int err, devRecd_t* drp, bool inputFl, int line )
        {
          char dirCh = inputFl ? 'I' : 'O';

          inputFl ? drp->iErrCnt++ : drp->oErrCnt++;

          // -EPIPE signals and over/underrun (see pcm.c example xrun_recovery())
          switch( err )
          {
            case -EPIPE:
              {

                int silentFl = 1;
                if((err = snd_pcm_recover( pcmH, err, silentFl )) < 0 )
                  _alsaSetupError(err,inputFl,drp,"recover failed.");

                if( inputFl )
                {
                  if((err= snd_pcm_prepare(pcmH)) < 0 )
                    _alsaSetupError(err,inputFl,drp,"re-prepare failed.");
                  else
                    if((err = snd_pcm_start(pcmH)) < 0 )
                      _alsaSetupError(err,inputFl,drp,"restart failed.");
                }
        

                printf("EPIPE %c %i %i %i\n",dirCh,drp->devIdx,drp->oCbCnt,line);

                break;
              }

            case -ESTRPIPE:
              {
                int silentFl = 1;
                if((err = snd_pcm_recover( pcmH, err, silentFl )) < 0 )
                  _alsaSetupError(err,inputFl,drp,"recover failed.");

                printf("audio port impl ESTRPIPE:%c\n",dirCh);
                break;
              }

            case -EBADFD:
              {
                _alsaSetupError(err,inputFl,drp,"%s failed.",inputFl ? "Read" : "Write" );
                break;
              }

            default:
              _alsaSetupError(err,inputFl,drp,"Unknown rd/wr error.\n");

          } // switch

        }

        void _devStateRecover( snd_pcm_t* pcmH, devRecd_t* drp, bool inputFl  )
        {
          int err = 0;

          switch( snd_pcm_state(pcmH))
          {
            case SND_PCM_STATE_XRUN:
              err = -EPIPE;
              break;

            case SND_PCM_STATE_SUSPENDED:
              err = -ESTRPIPE;
              break;

            case SND_PCM_STATE_OPEN:
            case SND_PCM_STATE_SETUP:
            case SND_PCM_STATE_PREPARED:
            case SND_PCM_STATE_RUNNING:
            case SND_PCM_STATE_DRAINING:
            case SND_PCM_STATE_PAUSED:
            case SND_PCM_STATE_DISCONNECTED:
            case SND_PCM_STATE_PRIVATE1:
              //case SND_PCM_STATE_LAST:
              break;
          }

          if( err < 0 )
            _devXrun_recover( pcmH, err, drp, inputFl, __LINE__ );
  
        }

        void _devS24_3BE_to_Float( const char* x, device::sample_t* y, unsigned n )
        {
          unsigned i;
          for(i=0; i<n; ++i,x+=3)
          {
            int s = (((int)x[0])<<16) + (((int)x[1])<<8) + (((int)x[2]));
            y[i] = ((device::sample_t)s)/0x7fffff;
          }
        }

        void _devS24_3BE_from_Float( const device::sample_t* x, char* y, unsigned n )
        {
          unsigned i;
          for(i=0; i<n; ++i)
          {
            int s = x[i] * 0x7fffff;
            y[i*3+2] = (char)((s & 0x7f0000) >> 16);
            y[i*3+1] = (char)((s & 0x00ff00) >>  8);
            y[i*3+0] = (char)((s & 0x0000ff) >>  0);
          }
        }


        // Returns count of frames written on success or < 0 on error;
        // set smpPtr to NULL to write a buffer of silence
        int _devWriteBuf( devRecd_t* drp, snd_pcm_t* pcmH, const device::sample_t* sp, unsigned chCnt, unsigned frmCnt, unsigned bits, unsigned sigBits )
        {
          int                 err         = 0;
          unsigned            bytesPerSmp = (bits==24 ? 32 : bits)/8;
          unsigned            smpCnt      = chCnt * frmCnt;
          unsigned            byteCnt     = bytesPerSmp * smpCnt;
          const device::sample_t* ep          = sp + smpCnt;
          char                obuf[ byteCnt ];

          // if no output was given then fill the device buffer with zeros
          if( sp == NULL )
            memset(obuf,0,byteCnt);
          else
          {
            // otherwise convert the floating point samples to integers
            switch( bits )
            {
              case 8:
                {
                  char* dp = (char*)obuf;
                  while( sp < ep )
                    *dp++ = (char)(*sp++ * 0x7f);        
                }
                break;

              case 16:
                {
                  short* dp = (short*)obuf;
                  while( sp < ep )
                    *dp++ = (short)(*sp++ * 0x7fff);        
                }
                break;

              case 24:
                {
                  // for use w/ MBox
                  //_devS24_3BE_from_Float(sp, obuf, ep-sp );
          
                  int* dp = (int*)obuf;
                  while( sp < ep )
                    *dp++ = (int)(*sp++ * 0x7fffff);        
            
                }
                break;

              case 32:
                {
                  int* dp = (int*)obuf;

                  while( sp < ep )
                    *dp++ = (int)(*sp++ * 0x7fffffff);        

                }
                break;
            }
          }


          // send the bytes to the device
          err = snd_pcm_writei( pcmH, obuf, frmCnt );

          ++drp->oBufCnt;
  
          if( err < 0 )
          {
            _alsaSetupError( err, false, drp, "ALSA write error" );
          }
          else
            if( err > 0 && ((unsigned)err) != frmCnt )
            {
              _alsaSetupError( 0, false, drp, "Actual count of bytes written did not match the count provided." );
            }
 

          return err;
        }


        // Returns frames read on success or < 0 on error.
        // Set smpPtr to NULL to read the incoming buffer and discard it
        int _devReadBuf( devRecd_t* drp, snd_pcm_t* pcmH, device::sample_t* smpPtr, unsigned chCnt, unsigned frmCnt, unsigned bits, unsigned sigBits )
        {
          int      err         = 0;
          unsigned bytesPerSmp = (bits==24 ? 32 : bits)/8;
          unsigned smpCnt      = chCnt * frmCnt;
          unsigned byteCnt     = smpCnt * bytesPerSmp;

          char     buf[ byteCnt ];

          // get the incoming samples into buf[] ...
          err = snd_pcm_readi(pcmH,buf,frmCnt);

          // if a read error occurred
          if( err < 0 )
          {
            _alsaSetupError( err, false, drp, "ALSA read error" );
          }
          else
            if( err > 0 && ((unsigned)err) != frmCnt )
            {
              _alsaSetupError( 0, false, drp, "Actual count of bytes read did not match the count requested." );
            }

          // if no buffer was given then there is nothing else to do 
          if( smpPtr == NULL )
            return err;

          // setup the return buffer
          device::sample_t* dp = smpPtr;
          device::sample_t* ep = dp + std::min(smpCnt,err*chCnt);
  
          switch(bits)
          {
            case 8: 
              {
                char* sp = buf;
                while(dp < ep)
                  *dp++ = ((device::sample_t)*sp++) /  0x7f;
              }
              break;

            case 16:
              {
                short* sp = (short*)buf;
                while(dp < ep)
                  *dp++ = ((device::sample_t)*sp++) /  0x7fff;
              }
              break;

            case 24:
              {
                // For use with MBox
                //_devS24_3BE_to_Float(buf, dp, ep-dp );
                int* sp = (int*)buf;
                while(dp < ep)
                  *dp++ = ((device::sample_t)*sp++) /  0x7fffff;
              }
              break;


            case 32:
              {
                int* sp = (int*)buf;
                // The delta1010 (ICE1712) uses only the 24 highest bits according to
                //
                // http://www.alsa-project.org/alsa-doc/alsa-lib/pcm.html
                // <snip> The example: ICE1712 chips support 32-bit sample processing, 
                // but low byte is ignored (playback) or zero (capture). 
                //
                int  mv = sigBits==24 ? 0x7fffff00 : 0x7fffffff;
                while(dp < ep)
                  *dp++ = ((device::sample_t)*sp++) /  mv;

              }
              break;
            default:
              { cwAssert(0); }
          }

          return err;
        }



        void _staticAsyncHandler( snd_async_handler_t* ahandler )
        { 
          int               err;
          snd_pcm_sframes_t avail;
          devRecd_t*        drp     = (devRecd_t*)snd_async_handler_get_callback_private(ahandler);
          snd_pcm_t*        pcmH    = snd_async_handler_get_pcm(ahandler);
          bool              inputFl = snd_pcm_stream(pcmH) == SND_PCM_STREAM_CAPTURE;
          device::sample_t* b       = inputFl ? drp->iBuf   : drp->oBuf;
          unsigned          chCnt   = inputFl ? drp->iChCnt : drp->oChCnt;
          unsigned          frmCnt  = inputFl ? drp->iFpC   : drp->oFpC;
          device::audioPacket_t pkt;

          inputFl ? drp->iCbCnt++ : drp->oCbCnt++;

          pkt.devIdx         = drp->devIdx;
          pkt.begChIdx       = 0;
          pkt.chCnt          = chCnt;
          pkt.audioFramesCnt = frmCnt;
          pkt.bitsPerSample  = 32;
          pkt.flags          = kInterleavedApFl | kFloatApFl;
          pkt.audioBytesPtr  = b;
          pkt.cbArg          = drp->cbArg;
  
          _devStateRecover( pcmH, drp, inputFl );
  
          while( (avail = snd_pcm_avail_update(pcmH)) >= (snd_pcm_sframes_t)frmCnt )
          {

            // Handle input
            if( inputFl )
            {
              // read samples from the device
              if((err = _devReadBuf(drp,pcmH,drp->iBuf,chCnt,frmCnt,drp->iBits,drp->oBits)) > 0 )
              {
                pkt.audioFramesCnt = err;

                drp->cbFunc(&pkt,1,NULL,0 ); // send the samples to the application
              }
            }

            // Handle output
            else
            {
              // callback to fill the buffer
              drp->cbFunc(NULL,0,&pkt,1);

              // note that the application may return fewer samples than were requested
              err = _devWriteBuf(drp, pcmH, pkt.audioFramesCnt < frmCnt ? NULL : drp->oBuf,chCnt,frmCnt,drp->oBits,drp->oSigBits);

            }

            // Handle read/write errors
            if( err < 0 )
            {
              inputFl ? drp->iErrCnt++ : drp->oErrCnt++;
              _devXrun_recover( pcmH, err, drp, inputFl, __LINE__ );
            }

          } // while

        }

        bool _threadFunc(void* param)
        {
          alsa_t* p = static_cast<alsa_t*>(param);
          int result;
          bool retFl = true;

          switch( result = poll(p->pollfds, p->pollfdsCnt, 250) )
          {
            case 0:
              // time out
              break;

            case -1:
              _alsaError(errno,"Poll fail.");
              break;

            default:
              {
                cwAssert( result > 0 );
    
                unsigned i;

                // for each i/o stream
                for(i=0; i<p->pollfdsCnt; ++i)
                {
                  devRecd_t*            drp     = p->pollfdsDesc[i].devPtr;
                  bool                  inputFl = p->pollfdsDesc[i].inputFl;
                  snd_pcm_t*            pcmH    = inputFl ? drp->iPcmH  : drp->oPcmH;
                  unsigned              chCnt   = inputFl ? drp->iChCnt : drp->oChCnt;
                  unsigned              frmCnt  = inputFl ? drp->iFpC   : drp->oFpC;
                  device::sample_t*     b       = inputFl ? drp->iBuf   : drp->oBuf;
                  unsigned short        revents = 0;
                  int                   err;
                  device::audioPacket_t pkt;
                  snd_pcm_uframes_t     avail_frames;

                  inputFl ? drp->iCbCnt++ : drp->oCbCnt++;
          
                  pkt.devIdx         = drp->devIdx;
                  pkt.begChIdx       = 0;
                  pkt.chCnt          = chCnt;
                  pkt.audioFramesCnt = frmCnt;
                  pkt.bitsPerSample  = 32;
                  pkt.flags          = kInterleavedApFl | kFloatApFl;
                  pkt.audioBytesPtr  = b;
                  pkt.cbArg          = drp->cbArg;

                  inputFl ? drp->iCbCnt++ : drp->oCbCnt++;

                  // get the timestamp for this buffer
                  if((err = snd_pcm_htimestamp(pcmH,&avail_frames,&pkt.timeStamp)) != 0 )
                  {
                    _alsaSetupError( err, p->pollfdsDesc[i].inputFl, drp, "Get timestamp error.");
                    pkt.timeStamp.tv_sec  = 0;
                    pkt.timeStamp.tv_nsec = 0;
                  }

                  // Note that based on experimenting with the timestamp and the current
                  // clock_gettime(CLOCK_MONOTONIC) time it appears that the time stamp
                  // marks the end of the current buffer - so in fact the time stamp should
                  // be backed up by the availble sample count period to get the time of the 
                  // first sample in the buffer
                  /*
                    unsigned avail_nano_secs = (unsigned)(avail_frames * (1000000000.0/drp->srate));
                    if( pkt.timeStamp.tv_nsec > avail_nano_secs )
                    pkt.timeStamp.tv_nsec -= avail_nano_secs;
                    else
                    {
                    pkt.timeStamp.tv_sec -= 1;
                    pkt.timeStamp.tv_nsec = 1000000000 - avail_nano_secs;
                    }
                  */

                  //printf("AUDI: %ld %ld\n",pkt.timeStamp.tv_sec,pkt.timeStamp.tv_nsec);
                  //cmTimeSpec_t t;
                  //clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);
                  //printf("AUDI: %ld %ld\n",t.tv_sec,t.tv_nsec);
          

                  switch( snd_pcm_state(pcmH) )
                  {
                    case SND_PCM_STATE_OPEN:
                    case SND_PCM_STATE_SETUP:
                    case SND_PCM_STATE_PREPARED:
                    case SND_PCM_STATE_DRAINING:
                    case SND_PCM_STATE_PAUSED:
                    case SND_PCM_STATE_DISCONNECTED:
                    case SND_PCM_STATE_PRIVATE1:
                      continue;

                    case SND_PCM_STATE_RUNNING:
                    case SND_PCM_STATE_XRUN:
                    case SND_PCM_STATE_SUSPENDED:
                      break;
                  }

                  if(( err = snd_pcm_poll_descriptors_revents(pcmH, p->pollfds + i, 1 , &revents)) != 0 )
                  {
                    _alsaSetupError( err, p->pollfdsDesc[i].inputFl, drp, "Return poll events failed.");
                    retFl = false;
                    goto errLabel;
                  }

                  if(revents & POLLERR)
                  {
                    _alsaSetupError( err, p->pollfdsDesc[i].inputFl, drp, "Poll error.");
                    _devStateRecover( pcmH, drp, inputFl );    
                    //goto errLabel;
                  }
          
                  if( inputFl && (revents & POLLIN) )
                  {
                    if((err = _devReadBuf(drp,pcmH,drp->iBuf,chCnt,frmCnt,drp->iBits,drp->oBits)) > 0 )
                    {
                      pkt.audioFramesCnt = err;
                      drp->cbFunc(&pkt,1,NULL,0 ); // send the samples to the application

                    }
                  }

                  if( !inputFl && (revents & POLLOUT) )
                  {

                    // callback to fill the buffer
                    drp->cbFunc(NULL,0,&pkt,1);

                    // note that the application may return fewer samples than were requested
                    err = _devWriteBuf(drp, pcmH, pkt.audioFramesCnt < frmCnt ? NULL : drp->oBuf,chCnt,frmCnt,drp->oBits,drp->oSigBits);
            
                  }          
                }
              }      
          }

        errLabel:
          return retFl;
        }

        rc_t _devSetup( devRecd_t *drp, unsigned srate, unsigned framesPerCycle, unsigned periodsPerBuf )
        {
          int               err;
          int               dir;
          unsigned          i;
          rc_t              rc             = kOkRC;
          bool              inputFl        = true;
          snd_pcm_uframes_t periodFrameCnt = framesPerCycle;
          snd_pcm_uframes_t bufferFrameCnt = 0;
          unsigned          bits           = 0;
          int               sig_bits       = 0;
          bool              signFl         = true;
          bool              swapFl         = false;
          alsa_t*           p              = drp->rootPtr;

          snd_pcm_format_t fmt[] =
            {
             SND_PCM_FORMAT_S32_LE,
             SND_PCM_FORMAT_S32_BE,
             SND_PCM_FORMAT_S24_LE,
             SND_PCM_FORMAT_S24_BE,
             SND_PCM_FORMAT_S24_3LE,
             SND_PCM_FORMAT_S24_3BE,
             SND_PCM_FORMAT_S16_LE,
             SND_PCM_FORMAT_S16_BE,
            };
  

          // setup input, then output device
          for(i=0; i<2; i++,inputFl=!inputFl)
          {
            unsigned          chCnt  = inputFl ? drp->iChCnt : drp->oChCnt;
            snd_pcm_uframes_t actFpC = 0;

            // if this is the in/out pass and the in/out flag is set
            if( ((inputFl==true) && (drp->flags & kInFl)) || ((inputFl==false) && (drp->flags & kOutFl)) )
            {
              snd_pcm_t* pcmH = NULL;
              rc_t rc0;
              
              if((rc0 = _devShutdown(p, drp, inputFl )) != kOkRC )
                rc = rc0;

              // attempt to open the sub-device
              if((err = snd_pcm_open(&pcmH,drp->nameStr, inputFl ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK, 0)) < 0 )
                rc = _alsaSetupError(err,inputFl,drp,"Unable to open the PCM handle");
              else
              {		

                snd_pcm_hw_params_t* hwParams;
                snd_pcm_sw_params_t* swParams;

                // prepare the hwParam recd
                snd_pcm_hw_params_alloca(&hwParams);
                memset(hwParams,0,snd_pcm_hw_params_sizeof());


                // load the hw parameter record
                if((err = snd_pcm_hw_params_any(pcmH,hwParams)) < 0 )
                  rc = _alsaSetupError(err,inputFl,drp,"Error obtaining hw param record");
                else
                {
                  if((err = snd_pcm_hw_params_set_rate_resample(pcmH,hwParams,0)) < 0 )
                    rc = _alsaSetupError(err,inputFl, drp,"Unable to disable the ALSA sample rate converter.");
		 

                  if((err = snd_pcm_hw_params_set_channels(pcmH,hwParams,chCnt)) < 0 )
                    rc = _alsaSetupError(err,inputFl, drp,"Unable to set channel count to: %i",chCnt);
		 

                  if((err = snd_pcm_hw_params_set_rate(pcmH,hwParams,srate,0)) < 0 )
                    rc = _alsaSetupError(err,inputFl, drp, "Unable to set sample rate to: %i",srate);
		  

                  if((err = snd_pcm_hw_params_set_access(pcmH,hwParams,SND_PCM_ACCESS_RW_INTERLEAVED )) < 0 )
                    rc = _alsaSetupError(err,inputFl, drp, "Unable to set access to: RW Interleaved");
          
                  // select the format width
                  int j;
                  int fmtN = sizeof(fmt)/sizeof(fmt[0]);
                  for(j=0; j<fmtN; ++j)
                    if((err = snd_pcm_hw_params_set_format(pcmH,hwParams,fmt[j])) >= 0 )
                      break;

                  if( j == fmtN )
                    rc = _alsaSetupError(err,inputFl, drp, "Unable to set format to: S16");
                  else
                  {
                    bits = snd_pcm_format_width(fmt[j]); // bits per sample
                    signFl = snd_pcm_format_signed(fmt[j]);
                    swapFl = !snd_pcm_format_cpu_endian(fmt[j]);
                  }
          
                  sig_bits = snd_pcm_hw_params_get_sbits(hwParams);

                  snd_pcm_uframes_t ps_min,ps_max;
                  if((err = snd_pcm_hw_params_get_period_size_min(hwParams,&ps_min,NULL)) < 0 )
                    rc = _alsaSetupError(err,inputFl, drp, "Unable to get the minimum period size.");

                  if((err = snd_pcm_hw_params_get_period_size_max(hwParams,&ps_max,NULL)) < 0 )
                    rc = _alsaSetupError(err,inputFl, drp, "Unable to get the maximum period size.");

                  if( periodFrameCnt < ps_min )
                    periodFrameCnt = ps_min;
                  else
                    if( periodFrameCnt > ps_max )
                      periodFrameCnt = ps_max;
		  
                  if((err = snd_pcm_hw_params_set_period_size_near(pcmH,hwParams,&periodFrameCnt,NULL)) < 0 )
                    rc = _alsaSetupError(err,inputFl, drp, "Unable to set period to %i.",periodFrameCnt);

                  bufferFrameCnt = periodFrameCnt * periodsPerBuf + 1;

                  if((err = snd_pcm_hw_params_set_buffer_size_near(pcmH,hwParams,&bufferFrameCnt)) < 0 )
                    rc = _alsaSetupError(err,inputFl, drp, "Unable to set buffer to %i.",bufferFrameCnt);


                  // Note: snd_pcm_hw_params() automatically calls snd_pcm_prepare()
                  if((err = snd_pcm_hw_params(pcmH,hwParams)) < 0 )
                    rc = _alsaSetupError(err,inputFl, drp, "Parameter application failed.");
		 
                }

                // prepare the sw param recd
                snd_pcm_sw_params_alloca(&swParams);
                memset(swParams,0,snd_pcm_sw_params_sizeof());

                // load the sw param recd
                if((err = snd_pcm_sw_params_current(pcmH,swParams)) < 0 )
                  rc = _alsaSetupError(err,inputFl,drp,"Error obtaining sw param record.");
                else
                {

                  if((err = snd_pcm_sw_params_set_start_threshold(pcmH,swParams, inputFl ? 0x7fffffff : periodFrameCnt)) < 0 )
                    rc = _alsaSetupError(err,inputFl,drp,"Error seting the start threshold.");

                  // setting the stop-threshold to twice the buffer frame count is intended to stop spurious
                  // XRUN states - it will also mean that there will have no direct way of knowing about a
                  // in/out buffer over/under run.
                  if((err = snd_pcm_sw_params_set_stop_threshold(pcmH,swParams,bufferFrameCnt*2)) < 0 )
                    rc = _alsaSetupError(err,inputFl,drp,"Error setting the stop threshold.");

                  if((err = snd_pcm_sw_params_set_avail_min(pcmH,swParams,periodFrameCnt)) < 0 )
                    rc = _alsaSetupError(err,inputFl,drp,"Error setting the avail. min. setting.");

                  if((err = snd_pcm_sw_params_set_tstamp_mode(pcmH,swParams,SND_PCM_TSTAMP_MMAP)) < 0 )
                    rc = _alsaSetupError(err,inputFl,drp,"Error setting the time samp mode.");

                  if((err = snd_pcm_sw_params(pcmH,swParams)) < 0 )
                    rc = _alsaSetupError(err,inputFl,drp,"Error applying sw params.");
                }

                // setup the callback
                if( p->asyncFl )
                  if((err = snd_async_add_pcm_handler(&drp->ahandler,pcmH,_staticAsyncHandler, drp )) < 0 )
                    rc = _alsaSetupError(err,inputFl,drp,"Error assigning callback handler.");

                // get the actual frames per cycle
                if((err = snd_pcm_hw_params_get_period_size(hwParams,&actFpC,&dir)) < 0 )
                  rc = _alsaSetupError(err,inputFl,drp,"Unable to get the actual period.");
	 
                // store the device handle
                if( inputFl )
                {
                  drp->iBits    = bits;
                  drp->iSigBits = sig_bits;
                  drp->iSignFl  = signFl;
                  drp->iSwapFl  = swapFl;
                  drp->iPcmH    = pcmH;
                  drp->iBuf     = memResizeZ<device::sample_t>( drp->iBuf, actFpC * drp->iChCnt );
                  drp->iFpC     = actFpC;
                }		
                else
                {
                  drp->oBits    = bits;
                  drp->oSigBits = sig_bits;
                  drp->oSignFl  = signFl;
                  drp->oSwapFl  = swapFl;
                  drp->oPcmH    = pcmH;
                  drp->oBuf     = memResizeZ<device::sample_t>( drp->oBuf, actFpC * drp->oChCnt );
                  drp->oFpC     = actFpC;
                }

                if( p->asyncFl == false )
                {
                  cwAssert( p->pollfdsCnt < p->pollfdsAllocCnt );
        
                  unsigned incrFdsCnt = 0;
                  unsigned fdsCnt     = 0;

                  // locate the pollfd associated with this device/direction
                  unsigned j;
                  for(j=0; j<p->pollfdsCnt; j+=p->pollfdsDesc[j].fdsCnt)
                    if( p->pollfdsDesc[j].devPtr == drp && inputFl == p->pollfdsDesc[j].inputFl )
                      break;

                  // get the count of descriptors for this device/direction
                  fdsCnt = snd_pcm_poll_descriptors_count(pcmH);           

                  // if the device was not found
                  if( j == p->pollfdsCnt )
                  {            
                    j          = p->pollfdsCnt;
                    incrFdsCnt = fdsCnt;

                    // if the pollfds[] needs more memroy
                    if( p->pollfdsCnt + fdsCnt > p->pollfdsAllocCnt )
                    {
                      p->pollfds          = memResizeZ<struct pollfd>( p->pollfds,     p->pollfdsCnt + fdsCnt );
                      p->pollfdsDesc      = memResizeZ<pollfdsDesc_t>( p->pollfdsDesc, p->pollfdsCnt + fdsCnt );
                      p->pollfdsAllocCnt += fdsCnt;
                    }
                  }

                  // get the poll descriptors for this device/dir
                  if( snd_pcm_poll_descriptors(pcmH,p->pollfds + j,fdsCnt) != 1 )
                    rc = _alsaSetupError(0,inputFl,drp,"Poll descriptor assignment failed.");
                  else
                  {
                    // store the desc. record assicoated with the poll descriptor
                    p->pollfdsDesc[ j ].fdsCnt  = fdsCnt;
                    p->pollfdsDesc[ j ].devPtr  = drp;
                    p->pollfdsDesc[ j ].inputFl = inputFl;
                  }

                  p->pollfdsCnt += incrFdsCnt;
                }
                printf("%s %s period:%i %i buffer:%i bits:%i sig_bits:%i\n",inputFl?"in ":"out",drp->nameStr,(unsigned)periodFrameCnt,(unsigned)actFpC,(unsigned)bufferFrameCnt,bits,sig_bits);

              } // end if async

            } // end if 
	
          } // end for 

          return rc;
        }        
      }  // alsa
    }  // device
  }  // audio
} // cw

cw::rc_t cw::audio::device::alsa::create( handle_t& hRef, struct driver_str*& drvRef )
{
  rc_t rc      = kOkRC;
  int  cardNum = -1;
  int  err;

  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  alsa_t* p = memAllocZ<alsa_t>();

  // for each sound card
  while(1)
  {
    snd_ctl_t* cardH           = NULL;
    char*      cardNamePtr     = NULL;
    char*      cardLongNamePtr = NULL;
    int        devNum          = -1;
    int        devStrN         = 31;
    char       devStr[devStrN+1];

    // get the next card handle
    if((err = snd_card_next(&cardNum)) < 0 )
      return _alsaError(err,"Error getting sound card handle");

    // if no more card's to get
    if( cardNum < 0 )
      break;

    // get the card short name (and free() when done)
    if(((err = snd_card_get_name(cardNum,&cardNamePtr)) < 0) || (cardNamePtr == NULL))
    {
      _alsaError(err,"Unable to get card name for card number %i",cardNum);
      goto releaseCard;
    }

    // get the card long name (and free() when done)
    if((err = snd_card_get_longname(cardNum,&cardLongNamePtr)) < 0 || cardLongNamePtr == NULL )
    {
      _alsaError(err,"Unable to get long card name for card number %i",cardNum);
      goto releaseCard;
    }
    
    // form the device name for this card
    if(snprintf(devStr,devStrN,"hw:%i",cardNum) > devStrN )
    {
      _alsaError(0,"Device name is too long for buffer.");
      goto releaseCard;
    }

    // open the card device driver
    if((err = snd_ctl_open(&cardH, devStr, 0)) < 0 )
    {
      _alsaError(err,"Error opening sound card %i.",cardNum);
      goto releaseCard;
    }
        
    // for each device on this card
    while(1)
    {
      snd_pcm_info_t* info; 
      int             subDevCnt = 1;
      int             i,j;

      // get the next device on this card
      if((err = snd_ctl_pcm_next_device(cardH,&devNum)) < 0 )
      {
        _alsaError(err,"Error gettign next device on card %i",cardNum);
        break;
      }

      // if no more devices to get
      if( devNum < 0 )
        break;

      // allocate a pcmInfo record
      snd_pcm_info_alloca(&info);
      memset(info, 0, snd_pcm_info_sizeof());

      // set the device  to query
      snd_pcm_info_set_device(info, devNum );

      for(i=0; i<subDevCnt; i++)
      {
        devRecd_t dr;
        bool      inputFl = false;

        memset(&dr,0,sizeof(dr));

        for(j=0; j<2; j++,inputFl=!inputFl)
        {
          snd_pcm_t* pcmH = NULL;

          dr.devIdx = -1;

          // set the subdevice and I/O direction to query
          snd_pcm_info_set_subdevice(info,i);
          snd_pcm_info_set_stream(info,inputFl ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK);

          // if this device does not use this sub-device
          if((err = snd_ctl_pcm_info(cardH,info)) < 0 )
            continue;                   

          // get the count of subdevices this device uses
          if(i == 0 )
            subDevCnt = snd_pcm_info_get_subdevices_count(info);
                
          // if this device has no sub-devices
          if(subDevCnt == 0 )
            continue;

          // form the device name and desc. string
          dr.nameStr = memPrintf(dr.nameStr,"hw:%i,%i,%i",cardNum,devNum,i);
          dr.descStr = memPrintf(dr.descStr,"%s %s",cardNamePtr,snd_pcm_info_get_name(info));
         
          // attempt to open the sub-device
          if((err = _devOpen(&pcmH,dr.nameStr,inputFl)) < 0 )
            _alsaSetupError(err,inputFl,&dr,"Unable to open the PCM handle");
          else
          {
            snd_pcm_hw_params_t* hwParams;

            // allocate the parameter record
            snd_pcm_hw_params_alloca(&hwParams);
            memset( hwParams,0,snd_pcm_hw_params_sizeof());
                        
            // load the parameter record
            if((err = snd_pcm_hw_params_any(pcmH,hwParams)) < 0 )
              _alsaSetupError(err,inputFl,&dr,"Error obtaining hw param record");
            else
            {
              unsigned* chCntPtr = inputFl ? &dr.iChCnt : &dr.oChCnt;

              snd_pcm_hw_params_get_rate_max(hwParams,&dr.srate,NULL);
              
              // extract the channel count
              if((err = snd_pcm_hw_params_get_channels_max(hwParams, chCntPtr )) < 0 )
                _alsaSetupError(err,inputFl,&dr,"Error getting channel count.");
              else
                // this device uses this subdevice in the current direction 
                dr.flags += inputFl ? kInFl : kOutFl;
            }                        

            // close the sub-device
            snd_pcm_close(pcmH);

          } 
                  
        } // in/out loop
        
        // insert the device in the device array
        if( dr.flags != 0 )          
          _devAppend(p,&dr);
        else
        {
          memRelease(dr.nameStr);
          memRelease(dr.descStr);
        }

      } // sub-dev loop
                  
    } // device loop

  releaseCard:
    snd_ctl_close(cardH);
    free(cardNamePtr);  
    free(cardLongNamePtr);
    
  } // card loop

  //https://stackoverflow.com/questions/13478861/alsa-mem-leak
  snd_config_update_free_global();


  if( rc == kOkRC && p->asyncFl==false )
  {
    p->pollfdsCnt      = 0;
    p->pollfdsAllocCnt = 2*p->devCnt;
    p->pollfds         = memAllocZ<struct pollfd>(    p->pollfdsAllocCnt );
    p->pollfdsDesc     = memAllocZ<pollfdsDesc_t>(p->pollfdsAllocCnt );

    if((rc = thread::create(p->thH,_threadFunc,p)) != kOkRC )
    {
      rc = cwLogError(rc,"Thread create failed.");
    }
  }

  if( rc != kOkRC )
    _destroy(p);
  else
  {
    p->driver.drvArg               = p;
    p->driver.deviceCount          = deviceCount;
    p->driver.deviceLabel          = deviceLabel;
    p->driver.deviceChannelCount   = deviceChannelCount;
    p->driver.deviceSampleRate     = deviceSampleRate;
    p->driver.deviceFramesPerCycle = deviceFramesPerCycle;
    p->driver.deviceSetup          = deviceSetup;
    p->driver.deviceStart          = deviceStart;
    p->driver.deviceStop           = deviceStop;
    p->driver.deviceIsStarted      = deviceIsStarted;
    p->driver.deviceRealTimeReport = deviceRealTimeReport;
    
    hRef.set(p);
    drvRef = &p->driver;
  }

  return rc;
  
  
}

cw::rc_t cw::audio::device::alsa::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return rc;

  alsa_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();
  return rc;
}
  
unsigned cw::audio::device::alsa::deviceCount( struct driver_str* drv)
{
  alsa_t* p = static_cast<alsa_t*>(drv->drvArg);
  return p->devCnt;
}

const char* cw::audio::device::alsa::deviceLabel( struct driver_str* drv, unsigned devIdx )
{
  alsa_t* p = static_cast<alsa_t*>(drv->drvArg);
  cwAssert( devIdx < deviceCount(drv));
  return p->devArray[devIdx].descStr;
}

unsigned cw::audio::device::alsa::deviceChannelCount(struct driver_str* drv, unsigned devIdx, bool inputFl )
{
  alsa_t* p = static_cast<alsa_t*>(drv->drvArg);
  cwAssert( devIdx < deviceCount(drv));
  return inputFl ? p->devArray[devIdx].iChCnt : p->devArray[devIdx].oChCnt;
}

double cw::audio::device::alsa::deviceSampleRate(  struct driver_str* drv, unsigned devIdx )
{
  alsa_t* p = static_cast<alsa_t*>(drv->drvArg);
  cwAssert( devIdx < deviceCount(drv));
  return (double)p->devArray[devIdx].srate;    
}

unsigned cw::audio::device::alsa::deviceFramesPerCycle( struct driver_str* drv, unsigned devIdx, bool inputFl )
{
  alsa_t* p = static_cast<alsa_t*>(drv->drvArg);
  cwAssert( devIdx < deviceCount(drv));
  return p->devArray[devIdx].framesPerCycle;  
}

cw::rc_t  cw::audio::device::alsa::deviceSetup( struct driver_str* drv, unsigned devIdx, double srate, unsigned framesPerCycle, cbFunc_t cbFunc, void* cbArg )
{
  alsa_t* p = static_cast<alsa_t*>(drv->drvArg);
  cwAssert( devIdx < deviceCount(drv));
  
  rc_t       rc            = kOkRC;
  devRecd_t* drp           = p->devArray + devIdx;
  unsigned   periodsPerBuf = kDfltPeriodsPerBuf;

  if( p->asyncFl == false )
    if((rc = thread::pause(p->thH,thread::kWaitFl | thread::kPauseFl)) != kOkRC )
      return cwLogError(rc,"Audio thread pause failed.");

  if((rc = _devSetup(drp, srate, framesPerCycle, periodsPerBuf )) == kOkRC )
  {
    drp->srate          = srate;
    drp->framesPerCycle = framesPerCycle;
    drp->periodsPerBuf  = periodsPerBuf;
    drp->cbFunc         = cbFunc;
    drp->cbArg          = cbArg;
  }

  return rc;
  
}

cw::rc_t  cw::audio::device::alsa::deviceStart( struct driver_str* drv, unsigned devIdx )
{
  cwAssert( devIdx < deviceCount(drv));
  alsa_t*    p       = static_cast<alsa_t*>(drv->drvArg);
  int        err;
  devRecd_t* drp     = p->devArray + devIdx;
  rc_t       rc      = kOkRC;
  bool       inputFl = true;
  unsigned   i;

  for(i=0; i<2; ++i,inputFl=!inputFl)
  {
    snd_pcm_t* pcmH = inputFl ? drp->iPcmH : drp->oPcmH;

    if(  pcmH != NULL )	
    {

      snd_pcm_state_t state = snd_pcm_state(pcmH);

      if( state != SND_PCM_STATE_RUNNING )
      {
        unsigned    chCnt   = inputFl ? drp->iChCnt : drp->oChCnt;
        unsigned    frmCnt  = inputFl ? drp->iFpC   : drp->oFpC;
        const char* ioLabel = inputFl ? "Input"     : "Output";
	  
        //printf("%i %s state:%s %i %i\n",drp->devIdx, ioLabel,_pcmStateToString(snd_pcm_state(pcmH)),chCnt,frmCnt);

        // preparing may not always be necessary because the earlier call to snd_pcm_hw_params()
        // may have left the device prepared - the redundant call however doesn't seem to hurt
        if((err= snd_pcm_prepare(pcmH)) < 0 )
        	rc = _alsaSetupError(err,inputFl,drp,"Error preparing the %i device.",ioLabel);
        else
        {

          if( inputFl == false )
          {
            int j;
            for(j=0; j<1; ++j)
              if((err = _devWriteBuf( drp, pcmH, NULL, chCnt, frmCnt, drp->oBits, drp->oSigBits )) < 0 )
              {
                rc = _alsaSetupError(err,inputFl,drp,"Write before start failed.");
                break;
              }
          }
          else
          {
          
            if((err = snd_pcm_start(pcmH)) < 0 )
              rc = _alsaSetupError(err,inputFl,drp,"'%s' start failed.",ioLabel);
          }

          // wait 500 microseconds between starting and stopping - this prevents
          // input and output and other device callbacks from landing on top of
          // each other - when this happens callbacks are dropped.
          if( p->asyncFl )
            usleep(500);

        }
        //printf("%i %s state:%s %i %i\n",drp->devIdx, ioLabel,_cmApPcmStateToString(snd_pcm_state(pcmH)),chCnt,frmCnt);

      }
	  
    }
  }

  if( p->asyncFl == false )
  {
    rc_t rc0 = kOkRC;
    if((rc0 = thread::unpause(p->thH)) != kOkRC )
      rc = _alsaError(rc0,"Audio thread start failed.");
  }
  return rc;
  
}

cw::rc_t  cw::audio::device::alsa::deviceStop(  struct driver_str* drv, unsigned devIdx )
{
  cwAssert( devIdx < deviceCount(drv));
  alsa_t*    p   = static_cast<alsa_t*>(drv->drvArg);
  devRecd_t* drp = p->devArray + devIdx;
  rc_t       rc  = kOkRC;
  int        err;
  
  
  if( drp->iPcmH != NULL )
    if((err = snd_pcm_drop(drp->iPcmH)) < 0 )
      rc = _alsaSetupError(err,true,drp,"Input stop failed.");

  if( drp->oPcmH != NULL )
    if((err = snd_pcm_drop(drp->oPcmH)) < 0 )
      rc = _alsaSetupError(err,false,drp,"Output stop failed.");

  if( p->asyncFl == false )
  {
    rc_t rc0;
    if((rc0 = thread::pause(p->thH,thread::kPauseFl)) != kOkRC )
      rc =_alsaError(rc0,"Audio thread pause failed.");
  }
  
  return rc;  
}

bool  cw::audio::device::alsa::deviceIsStarted(struct driver_str* drv, unsigned devIdx ) 
{
  cwAssert( devIdx < deviceCount(drv));
  alsa_t*          p   = static_cast<alsa_t*>(drv->drvArg);
  bool             iFl = false;
  bool             oFl = false;
  const devRecd_t* drp = p->devArray + devIdx;

  if( drp->iPcmH != NULL )
    iFl = snd_pcm_state(drp->iPcmH) == SND_PCM_STATE_RUNNING;
	
  if( drp->oPcmH != NULL )
    oFl = snd_pcm_state(drp->oPcmH) == SND_PCM_STATE_RUNNING;
	  
  return iFl || oFl;  
  
}

void cw::audio::device::alsa::deviceRealTimeReport(struct driver_str* drv, unsigned devIdx ) 
{
  alsa_t*     p      = static_cast<alsa_t*>(drv->drvArg);
  devRecd_t*  drp    = p->devArray + devIdx;
  const char* iState = drp->iPcmH == NULL ? "<not-used>" : _pcmStateToString(snd_pcm_state(drp->iPcmH));
  const char* oState = drp->oPcmH == NULL ? "<not-used>" : _pcmStateToString(snd_pcm_state(drp->oPcmH));
  
  cwLogInfo("ALSA cb i:%i o:%i err i:%i  o:%i state: i:%s o:%s",drp->iCbCnt,drp->oCbCnt,drp->iErrCnt,drp->oErrCnt,iState,oState);
}



//{ { label:alsaDevRpt  }
//(
//  Here's an example of generating a report of available
//  ALSA devices.
//)

//[
void        cw::audio::device::alsa::report( handle_t h )
{
  alsa_t*  p = _handleToPtr(h);
  unsigned i;
  
  for(i=0; i<p->devCnt; i++)
  {
    cwLogInfo(" ");
    cwLogInfo("%i : ",i);
    _devReport(p->devArray + i );
  }

  snd_config_update_free_global();
}

void cw::audio::device::alsa::report()
{
  handle_t h;
  driver_t* d;
  if( create(h,d) == kOkRC )
  {
    report(h);
    destroy(h);
  }
}

//]
//}

