# libcw

libcw is an application framework for developming real-time interactive applications on Linux.

Some of libcw's features are:

- Real-time, low-latency, synchronous and asynchronous interfaces to many common IO devices including:
  + Audio
  + MIDI
  + Serial
  + Sockets
  + Websockets
 
- Built-in dataflow framework for describing real-time audio and DSP programs.

- A GUI development API and collection of predefined widgets for creating websocket based user interfaces.

- Most elements of the framework are configurable based on configuration files which use an extended JSON syntax.

- The library has minimal dependencies.  The only external dependencies are `libasound`, `libwebsockets` and `libfftw`.

- The library implements a large collection of pre-built audio signal processing algorithms.











