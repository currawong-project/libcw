
HDR =          cwCommon.h cwCommonImpl.h   cwMem.h   cwLog.h 
SRC = main.cpp            cwCommonImpl.cpp cwMem.cpp cwLog.cpp

HDR += cwFileSys.h   cwText.h   cwFile.h    cwLex.h   cwNumericConvert.h 
SRC += cwFileSys.cpp cwText.cpp cwFile.cpp  cwLex.cpp

HDR += cwObject.h   cwTextBuf.h    cwThread.h
SRC += cwObject.cpp cwTextBuf.cpp  cwThread.cpp


LIBS = -lpthread


cw_rt : $(SRC) $(HDR)
	g++ -g --std=c++17 $(LIBS) -Wall -DcwLINUX -o $@ $(SRC)


