
HDR =          cwCommon.h cwCommonImpl.h   cwMem.h   cwLog.h    cwUtility.h
SRC =                     cwCommonImpl.cpp cwMem.cpp cwLog.cpp  cwUtility.cpp

HDR += cwFileSys.h   cwText.h   cwFile.h    cwTime.h   cwLex.h   cwNumericConvert.h 
SRC += cwFileSys.cpp cwText.cpp cwFile.cpp  cwTime.cpp cwLex.cpp

HDR += cwObject.h   cwObjectTemplate.h cwTextBuf.h    cwThread.h    cwMpScNbQueue.h
SRC += cwObject.cpp                    cwTextBuf.cpp  cwThread.cpp

HDR += cwWebSock.h   cwWebSockSvr.h     
SRC += cwWebSock.cpp cwWebSockSvr.cpp

HDR += cwSerialPort.h    cwSerialPortSrv.h
SRC += cwSerialPort.cpp  cwSerialPortSrv.cpp

HDR += cwMidi.h   cwMidiPort.h
SRC += cwMidi.cpp cwMidiPort.cpp cwMidiAlsa.cpp

HDR += cwAudioBuf.h    cwAudioDevice.h   cwAudioDeviceAlsa.h
SRC += cwAudioBuf.cpp  cwAudioDevice.cpp cwAudioDeviceAlsa.cpp cwAudioDeviceTest.cpp

HDR += cwTcpSocket.h   cwTcpSocketSrv.h   cwTcpSocketTest.h   
SRC += cwTcpSocket.cpp cwTcpSocketSrv.cpp cwTcpSocketTest.cpp 

HDR += cwMdns.h   cwEuCon.h   cwDnsSd.h   dns_sd/dns_sd.h   dns_sd/dns_sd_print.h   dns_sd/dns_sd_const.h  dns_sd/fader.h     dns_sd/rpt.h
SRC += cwMdns.cpp cwEuCon.cpp cwDnsSd.cpp dns_sd/dns_sd.cpp dns_sd/dns_sd_print.cpp                        dns_sd/fader.cpp   dns_sd/rpt.cpp

HDR += cwIo.h   cwIoTest.h
SRC += cwIo.cpp cwIoTest.cpp

LIBS = -lpthread  -lwebsockets  -lasound

WS_DIR   = /home/kevin/sdk/libwebsockets/build/out
INC_PATH = $(WS_DIR)/include 
LIB_PATH = $(WS_DIR)/lib

cw_rt : main.cpp $(SRC) $(HDR) 
	g++ -g --std=c++17 $(LIBS) -Wall  -L$(LIB_PATH) -I$(INC_PATH) -I .. -DcwLINUX -o $@ main.cpp $(SRC) 


avahi_surface : cwAvahiSurface.cpp $(SRC) $(HDR)
	g++ -g --std=c++17 $(LIBS) -Wall  -L$(LIB_PATH)  -lavahi-core -lavahi-common -lavahi-client -I$(INC_PATH) -DcwLINUX -o $@ cwAvahiSurface.cpp $(SRC) 

