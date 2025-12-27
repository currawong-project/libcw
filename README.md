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






# Build

```
cd ~/src/libcw
rm -rf build  #clean
cmake -S ~/src/libcw -B ~/src/libcw/build/debug -DCMAKE_INSTALL_PREFIX=~/src/libcw/build/debug/install --preset debug
cmake --build ~/src/libcw/build/debug --preset debug
cmake --install ~/src/libcw/build/debug 
```

# Run tests with 'ctest'

See https://cmake.org/cmake/help/latest/manual/ctest.1.html#manual:ctest(1) for more options.
```
ctest --test-dir build/debug/test             # Run all tests.
ctest --test-dir build/debug/test  --verbose  # Run all tests without surpressing output to stdout
ctest -R "MyTest" --test-dir build/debug/test # Identify the tests to run with a regex.
```







