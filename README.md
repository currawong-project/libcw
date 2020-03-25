
# To Do

- UI needs a special UUID (not kInvalidId) to specify the 'root' UI element. See note in cwUi._createFromObj()
- Look at 'BUG' warnings in cwNumericConvert.h.
- cwObject must be able to parse without dynamic memory allocation into a fixed buffer
- cwObject must be able to be composed without dynamic memory allocation or from a fixed buffer.

- cwWebsock is allocating memory on send().
- cwWebsock: if the size of the recv and xmt buffer, as passed form the protocolArray[], is too small send() will fail without an error message.
This is easy to reproduce by simply decreasing the size of the buffers in the protocol array.

- Clean up the cwObject namespace - add an 'object' namespace inside 'cw'

- Add underscore to the member variables of object_t.


- logDefaultFormatter() in cwLog.cpp uses stack allocated memory in a way that could easily be exploited.

- lexIntMatcher() in cwLex.cpp doesn't handle 'e' notation correctly. See note in code.

- numeric_convert() in cwNumericConvert.h could be made more efficient using type_traits.

- thread needs setters and getters for internal variables

- change cwMpScNbQueue so that it does not require 'new'.

- cwAudioBuf.cpp - the ch->fn in update() does not have the correct memory fence.

- change file names to match object names

- (DONE) change all NULL's to nullptr

- (DONE) implement kTcpFl in cwTcpSocket.cpp




# Development Setup

1) Install libwebsockets.

```
    sudo dnf install g++ openssl-devel cmake
    cd sdk
    git clone https://libwebsockets.org/repo/libwebsockets
    cd libwebsockets
    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX:PATH=/home/kevin/sdk/libwebsockets/build/out ..
```

2) Environment setup:

    export LD_LIBRARY_PATH=~/sdk/libwebsockets/build/out/lib

