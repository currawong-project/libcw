libcwHDR =
libcwSRC =

libcwHDR += src/libcw/cwCommon.h src/libcw/cwCommonImpl.h   src/libcw/cwMem.h   src/libcw/cwLog.h    src/libcw/cwUtility.h
libcwSRC +=                      src/libcw/cwCommonImpl.cpp src/libcw/cwMem.cpp src/libcw/cwLog.cpp  src/libcw/cwUtility.cpp

libcwHDR += src/libcw/cwString.h   src/libcw/cwMath.h   src/libcw/cwVectOps.h src/libcw/cwMtx.h    src/libcw/cwVariant.h
libcwSRC += src/libcw/cwString.cpp src/libcw/cwMath.cpp                       src/libcw/cwMtx.cpp  src/libcw/cwVariant.cpp

libcwHDR += src/libcw/cwFileSys.h   src/libcw/cwText.h   src/libcw/cwFile.h    src/libcw/cwTime.h   src/libcw/cwLex.h   src/libcw/cwNumericConvert.h 
libcwSRC += src/libcw/cwFileSys.cpp src/libcw/cwText.cpp src/libcw/cwFile.cpp  src/libcw/cwTime.cpp src/libcw/cwLex.cpp

libcwHDR += src/libcw/cwLib.h
libcwSRC += src/libcw/cwLib.cpp

libcwHDR += src/libcw/cwObject.h   src/libcw/cwObjectTemplate.h src/libcw/cwTextBuf.h    
libcwSRC += src/libcw/cwObject.cpp                              src/libcw/cwTextBuf.cpp

libcwHDR += src/libcw/cwThread.h    src/libcw/cwMutex.h   src/libcw/cwThreadMach.h      
libcwSRC += src/libcw/cwThread.cpp  src/libcw/cwMutex.cpp src/libcw/cwThreadMach.cpp

libcwHDR += src/libcw/cwMpScNbQueue.h src/libcw/cwSpScBuf.h   src/libcw/cwSpScQueueTmpl.h
libcwSRC +=                           src/libcw/cwSpScBuf.cpp src/libcw/cwSpScQueueTmpl.cpp

libcwHDR += src/libcw/cwAudioFile.h     src/libcw/cwAudioFileOps.h    src/libcw/cwMidiFile.h
libcwSRC += src/libcw/cwAudioFile.cpp   src/libcw/cwAudioFileOps.cpp  src/libcw/cwMidiFile.cpp

if cwWEBSOCK
libcwHDR += src/libcw/cwWebSock.h   src/libcw/cwWebSockSvr.h
libcwSRC += src/libcw/cwWebSock.cpp src/libcw/cwWebSockSvr.cpp

libcwHDR += src/libcw/cwUiDecls.h src/libcw/cwUi.h   src/libcw/cwUiTest.h
libcwSRC +=                       src/libcw/cwUi.cpp src/libcw/cwUiTest.cpp

endif


libcwHDR += src/libcw/cwSerialPortDecls.h src/libcw/cwSerialPort.h    src/libcw/cwSerialPortSrv.h
libcwSRC +=                               src/libcw/cwSerialPort.cpp  src/libcw/cwSerialPortSrv.cpp

libcwHDR += src/libcw/cwMidiDecls.h src/libcw/cwMidi.h     
libcwSRC +=                         src/libcw/cwMidi.cpp    

libcwHDR += src/libcw/cwAudioDeviceDecls.h src/libcw/cwAudioDevice.h   
libcwSRC += src/libcw/cwAudioDevice.cpp

if cwALSA

libcwHDR += src/libcw/cwMidiPort.h                            src/libcw/cwAudioDeviceAlsa.h                                    
libcwSRC += src/libcw/cwMidiPort.cpp src/libcw/cwMidiAlsa.cpp src/libcw/cwAudioDeviceAlsa.cpp src/libcw/cwAudioDeviceTest.cpp

if cwWEBSOCK
libcwHDR += src/libcw/cwIo.h   src/libcw/cwIoTest.h     src/libcw/cwIoSocketChat.h   src/libcw/cwIoAudioPanel.h    src/libcw/cwIoAudioMidi.h
libcwSRC += src/libcw/cwIo.cpp src/libcw/cwIoTest.cpp   src/libcw/cwIoSocketChat.cpp src/libcw/cwIoAudioPanel.cpp  src/libcw/cwIoAudioMidi.cpp

libcwHDR += src/libcw/cwIoMidiRecordPlay.h    src/libcw/cwIoAudioRecordPlay.h   src/libcw/cwIoAudioMidiApp.h
libcwSRC += src/libcw/cwIoMidiRecordPlay.cpp  src/libcw/cwIoAudioRecordPlay.cpp src/libcw/cwIoAudioMidiApp.cpp

endif


endif

libcwHDR += src/libcw/cwAudioBufDecls.h src/libcw/cwAudioBuf.h
libcwSRC +=                             src/libcw/cwAudioBuf.cpp

libcwHDR += src/libcw/cwSocketDecls.h src/libcw/cwSocket.h
libcwSRC +=                           src/libcw/cwSocket.cpp

libcwHDR += src/libcw/cwTcpSocket.h   src/libcw/cwTcpSocketSrv.h   src/libcw/cwTcpSocketTest.h   
libcwSRC += src/libcw/cwTcpSocket.cpp src/libcw/cwTcpSocketSrv.cpp src/libcw/cwTcpSocketTest.cpp

libcwHDR += src/libcw/cwDsp.h
libcwSRC += src/libcw/cwDsp.cpp


libcwHDR += src/libcw/cwMdns.h   src/libcw/cwEuCon.h   src/libcw/cwDnsSd.h   src/libcw/dns_sd/dns_sd.h   src/libcw/dns_sd/dns_sd_print.h   src/libcw/dns_sd/dns_sd_const.h  src/libcw/dns_sd/fader.h     src/libcw/dns_sd/rpt.h
libcwSRC += src/libcw/cwMdns.cpp src/libcw/cwEuCon.cpp src/libcw/cwDnsSd.cpp src/libcw/dns_sd/dns_sd.cpp src/libcw/dns_sd/dns_sd_print.cpp                                  src/libcw/dns_sd/fader.cpp   src/libcw/dns_sd/rpt.cpp

if cwWEB

else
 libcwHDR += src/libcw/cwSvg.h    src/libcw/cwDataSets.h
 libcwSRC += src/libcw/cwSvg.cpp  src/libcw/cwDataSets.cpp
endif

if cwFFTW
else
libcwHDR += src/libcw/cwFFT.h
libcwSRC += src/libcw/cwFFT.cpp
endif

