set( CORE_HDR_FILES          core/cwCommon.h                            core/cwLog.h   core/cwMem.h )
set( CORE_SRC_FILES          core/cwCommonImpl.h  core/cwCommonImpl.cpp core/cwLog.cpp core/cwMem.cpp )
# Note that cwCommonImpl.h is included with the SRC files because it is not PUBLIC.

list( APPEND CORE_HDR_FILES  core/cwNumericConvert.h   core/cwObjectTemplate.h core/cwObject.h)
list( APPEND CORE_SRC_FILES  core/cwNumericConvert.cpp                         core/cwObject.cpp )

list( APPEND CORE_HDR_FILES  core/cwString.h   core/cwText.h   core/cwTextBuf.h )
list( APPEND CORE_SRC_FILES  core/cwString.cpp core/cwText.cpp core/cwTextBuf.cpp )

list( APPEND CORE_HDR_FILES core/cwMath.h   core/cwVectOps.h   core/cwUtility.h )
list( APPEND CORE_SRC_FILES core/cwMath.cpp core/cwVectOps.cpp core/cwUtility.cpp)

list( APPEND CORE_HDR_FILES core/cwB23Tree.h   core/cwMtx.h   core/cwVariant.h )
list( APPEND CORE_SRC_FILES core/cwB23Tree.cpp core/cwMtx.cpp core/cwVariant.cpp)

list( APPEND CORE_HDR_FILES core/cwTime.h   core/cwFile.h   core/cwFileSys.h   core/cwLib.h )
list( APPEND CORE_SRC_FILES core/cwTime.cpp core/cwFile.cpp core/cwFileSys.cpp core/cwLib.cpp)

list( APPEND CORE_HDR_FILES core/cwMutex.h   core/cwThread.h   core/cwThreadMach.h )
list( APPEND CORE_SRC_FILES core/cwMutex.cpp core/cwThread.cpp core/cwThreadMach.cpp )
  
# core/cwNbMem.cpp
# core/cwNbMem.h

list( APPEND CORE_HDR_FILES core/cwMpScNbCircQueue.h core/cwMpScNbQueue.h core/cwMtQueueTester.h   core/cwSpScQueueTmpl.h   core/cwSpScBuf.h   core/cwNbMpScQueue.h)
list( APPEND CORE_SRC_FILES                                               core/cwMtQueueTester.cpp core/cwSpScQueueTmpl.cpp core/cwSpScBuf.cpp core/cwNbMpScQueue.cpp )
  
list( APPEND CORE_HDR_FILES core/cwLex.h core/cwCsv.h core/cwSvg.h )
list( APPEND CORE_SRC_FILES core/cwLex.cpp core/cwCsv.cpp core/cwSvg.cpp)

list( APPEND CORE_HDR_FILES core/cwAudioFile.h   core/cwMidiDecls.h core/cwMidi.h core/cwMidiParser.h   core/cwMidiState.h   core/cwWaveTableBank.h   core/cwMidiFile.h )
list( APPEND CORE_SRC_FILES core/cwAudioFile.cpp core/cwMidi.cpp                  core/cwMidiParser.cpp core/cwMidiState.cpp core/cwWaveTableBank.cpp core/cwMidiFile.cpp )
  
list( APPEND CORE_HDR_FILES core/cwTracer.h   core/cwTest.h)
list( APPEND CORE_SRC_FILES core/cwTracer.cpp core/cwTest.cpp  )

list( APPEND CORE_HDR_FILES core/cwDspTypes.h core/cwDsp.h   core/cwFFT.h   core/cwAudioTransforms.h   core/cwDspTransforms.h )
list( APPEND CORE_SRC_FILES                   core/cwDsp.cpp core/cwFFT.cpp core/cwAudioTransforms.cpp core/cwDspTransforms.cpp )

# core/cwCmInterface.h
# core/cwCmInterface.cpp

list( APPEND CORE_HDR_FILES core/cwAudioFileOps.h   core/cwAudioFileProc.h   core/cwPvAudioFileProc.h   core/cwDataSets.h )
list( APPEND CORE_SRC_FILES core/cwAudioFileOps.cpp core/cwAudioFileProc.cpp core/cwPvAudioFileProc.cpp core/cwDataSets.cpp )

  

  
#-------------------------------------
# io files

set(  IO_HDR_FILES io/cwAudioBufDecls.h io/cwAudioBuf.h io/cwAudioDeviceDecls.h io/cwAudioDevice.h   io/cwAudioDeviceAlsa.h   io/cwAudioDeviceFile.h   io/cwAudioDeviceTest.h )
set(  IO_SRC_FILES                      io/cwAudioBuf.cpp                       io/cwAudioDevice.cpp io/cwAudioDeviceAlsa.cpp io/cwAudioDeviceFile.cpp io/cwAudioDeviceTest.cpp )

list( APPEND IO_SRC_FILES io/cwMidiDevice.cpp io/cwMidiFileDev.cpp io/cwMidiAlsa.cpp io/cwMidiDeviceTest.cpp )
list( APPEND IO_HDR_FILES io/cwMidiDevice.h   io/cwMidiFileDev.h   io/cwMidiAlsa.h   io/cwMidiDeviceTest.h )

list( APPEND IO_SRC_FILES                        io/cwSerialPort.cpp io/cwSerialPortSrv.cpp )
list( APPEND IO_HDR_FILES io/cwSerialPortDecls.h io/cwSerialPort.h   io/cwSerialPortSrv.h )

list( APPEND IO_SRC_FILES                    io/cwSocket.cpp io/cwTcpSocket.cpp io/cwTcpSocketSrv.cpp io/cwTcpSocketTest.cpp )
list( APPEND IO_HDR_FILES io/cwSocketDecls.h io/cwSocket.h   io/cwTcpSocket.h   io/cwTcpSocketSrv.h   io/cwTcpSocketTest.h )

list( APPEND IO_HDR_FILES io/cwUiDecls.h io/cwUi.h )
list( APPEND IO_SRC_FILES                io/cwUi.cpp )

list( APPEND IO_SRC_FILES                     io/cwWebSock.cpp io/cwWebSockSvr.cpp )
list( APPEND IO_HDR_FILES io/cwWebSockDecls.h io/cwWebSock.h   io/cwWebSockSvr.h )
  
list( APPEND IO_HDR_FILES io/cwKeyboard.h )
list( APPEND IO_SRC_FILES io/cwKeyboard.cpp )
  
list( APPEND IO_HDR_FILES io/cwIo.h )
list( APPEND IO_SRC_FILES io/cwIo.cpp )


#-------------------------------------
# flow source files
set(  FLOW_HDR_FILES flow/cwFlowDecl.h flow/cwFlowValue.h   flow/cwFlowTypes.h   flow/cwFlowNet.h   flow/cwFlow.h   flow/cwFlowCross.h   flow/cwFlowPerf.h   flow/cwFlowProc.h )
set(  FLOW_SRC_FILES                   flow/cwFlowValue.cpp flow/cwFlowTypes.cpp flow/cwFlowNet.cpp flow/cwFlow.cpp flow/cwFlowCross.cpp flow/cwFlowPerf.cpp flow/cwFlowProc.cpp)


#-------------------------------------
# cw
set(  CW_HDR_FILES cw/cwDynRefTbl.h cw/cwGutimReg.h     cw/cwMidiDetectors.h   cw/cwPerfMeas.h   cw/cwPianoScore.h   cw/cwPresetSel.h )
set(  CW_SRC_FILES cw/cwDynRefTbl.cpp cw/cwGutimReg.cpp cw/cwMidiDetectors.cpp cw/cwPerfMeas.cpp cw/cwPianoScore.cpp cw/cwPresetSel.cpp )
  
list( APPEND CW_HDR_FILES cw/cwScoreFollow2.h   cw/cwScoreFollow2Test.h   cw/cwScoreFollower.h   cw/cwScoreFollowerPerf.h cw/cwScoreFollowTest.h )
list( APPEND CW_SRC_FILES cw/cwScoreFollow2.cpp cw/cwScoreFollow2Test.cpp cw/cwScoreFollower.cpp cw/cwScoreFollowTest.cpp )

list( APPEND CW_HDR_FILES cw/cwScoreParse.h   cw/cwScoreTest.h   cw/cwSfAnalysis.h   cw/cwSfMatch.h   cw/cwSfScore.h   cw/cwSfTrack.h )
list( APPEND CW_SRC_FILES cw/cwScoreParse.cpp cw/cwScoreTest.cpp cw/cwSfAnalysis.cpp cw/cwSfMatch.cpp cw/cwSfScore.cpp cw/cwSfTrack.cpp )

list( APPEND CW_HDR_FILES cw/cwSvgMidi.h   cw/cwSvgScoreFollow.h   cw/cwWaveTableNotes.h )
list( APPEND CW_SRC_FILES cw/cwSvgMidi.cpp cw/cwSvgScoreFollow.cpp cw/cwWaveTableNotes.cpp )


#-------------------------------------
# io_components
set(  IO_COMP_HDR_FILES io_components/cwIoAudioPanel.h   io_components/cwIoAudioRecordPlay.h   io_components/cwIoMidiRecordPlay.h )
set(  IO_COMP_SRC_FILES io_components/cwIoAudioPanel.cpp io_components/cwIoAudioRecordPlay.cpp io_components/cwIoMidiRecordPlay.cpp )

list( APPEND IO_COMP_HDR_FILES io_components/cwIoMinTest.h   io_components/cwIoSocketChat.h   io_components/cwUiTest.h   io_components/cwIoAudioMidiApp.h )
list( APPEND IO_COMP_SRC_FILES io_components/cwIoMinTest.cpp io_components/cwIoSocketChat.cpp io_components/cwUiTest.cpp io_components/cwIoAudioMidiApp.cpp )

list( APPEND IO_COMP_HDR_FILES io_components/cwIoTest.h )
list( APPEND IO_COMP_SRC_FILES io_components/cwIoTest.cpp )
    
#-------------------------------------
# io_flow
set(  IO_FLOW_HDR_FILES io_flow/cwIoFlow.h   io_flow/cwIoFlowCtl.h )
set(  IO_FLOW_SRC_FILES io_flow/cwIoFlow.cpp io_flow/cwIoFlowCtl.cpp )
