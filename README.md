

- Clean up the cwObject namespace - add an 'object' namespace inside 'cw'

- Add underscore to the member variables of object_t.

- 

- logDefaultFormatter() in cwLog.cpp uses stack allocated memory in a way that could easily be exploited.

- lexIntMatcher() in cwLex.cpp doesn't handle 'e' notation correctly. See note in code.

- numeric_convert() in cwNumericConvert.h could be made more efficient using type_traits.

- thread needs setters and getters for internal variables

- change cwMpScNbQueue so that it does not require 'new'.

- cwAudioBuf.cpp - the ch->fn in update() does not have the correct memory fence.

- change file names to match object names

- change all NULL's to nullptr

- implement kTcpFl in cwTcpSocket.cpp

# Development Setup

1) Install libwebsockets.

    cd sdk
    git clone https://libwebsockets.org/repo/libwebsockets
    cd libwebsockets
    mkdir build
    cd build
    sudo dnf install openssl-devel cmake
    cmake -DCMAKE_INSTALL_PREFIX:PATH=/home/kevin/sdk/libwebsockets/build/out ..

