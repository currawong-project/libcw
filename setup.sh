export LD_LIBRARY_PATH=~/sdk/libwebsockets/build/out/lib

# restart pulseaudio to free ALSA driver
systemctl --user restart pulseaudio

