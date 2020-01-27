
HDR =          cwCommon.h cwCommonImpl.h   cwMem.h   cwLog.h 
SRC = main.cpp            cwCommonImpl.cpp cwMem.cpp cwLog.cpp

HDR += cwFileSys.h   cwText.h   cwFile.h    cwTime.h   cwLex.h   cwNumericConvert.h 
SRC += cwFileSys.cpp cwText.cpp cwFile.cpp  cwTime.cpp cwLex.cpp

HDR += cwObject.h   cwTextBuf.h    cwThread.h    cwMpScNbQueue.h
SRC += cwObject.cpp cwTextBuf.cpp  cwThread.cpp

HDR += cwWebSock.h   cwWebSockSvr.h     
SRC += cwWebSock.cpp cwWebSockSvr.cpp

HDR += cwSerialPort.h    cwSerialPortSrv.h
SRC += cwSerialPort.cpp  cwSerialPortSrv.cpp

HDR += cwMidi.h   cwMidiPort.h
SRC += cwMidi.cpp cwMidiPort.cpp cwMidiAlsa.cpp

HDR += cwAudioBuf.h    cwAudioDevice.h   cwAudioDeviceAlsa.h
SRC += cwAudioBuf.cpp  cwAudioDevice.cpp cwAudioDeviceAlsa.cpp cwAudioDeviceTest.cpp

HDR += cwTcpSocket.h   cwTcpSocketSrv.h   cwTcpSocketTest.h   cwMdns.h
SRC += cwTcpSocket.cpp cwTcpSocketSrv.cpp cwTcpSocketTest.cpp cwMdns.cpp

# HDR += cwIo.h   cwIoTest.h   cwNbMem.h
# SRC += cwIo.cpp cwIoTest.cpp cwNbMem.cpp

LIBS = -lpthread  -lwebsockets  -lasound

WS_DIR   = /home/kevin/sdk/libwebsockets/build/out
INC_PATH = $(WS_DIR)/include
LIB_PATH = $(WS_DIR)/lib

cw_rt : $(SRC) $(HDR)
	g++ -g --std=c++17 $(LIBS) -Wall  -L$(LIB_PATH) -I$(INC_PATH) -DcwLINUX -o $@ $(SRC)


