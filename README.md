# OBS VBAN Audio Plugin

## Introduction

This plugin provides audio source from and output to VBAN, an audio over UDP protocol.

## Properties for VBAN Audio Source

Streams will be identified by these two properties. If your computer is receiving multiple VBAN streams, please set them.

### IP Address From

Set IP address of your source.
If empty, any sources can be received.

### Stream Name

Set name of your stream.
If empty, any stream will be received.

## Properties for VBAN Audio Output and Filter

### IP Address To
Set IP address of your receiver.

### Stream Name
Set name of your stream.

### Track
Choose the track number in OBS Studio to be streamed.
This property is not available for filters.

### Sampling Rate
Choose the sampling rate to stream.
The default is same as OBS Studio so that no resample will happen.
If you choose different sampling rate, a resampler will convert the sampling rate.

### Format
Choose the format of each sample. Available options are 16-bit and 24-bit integers and 32-bit floating point.

## Build and install
### Linux
Use cmake to build on Linux. After checkout, run these commands.
```
sed -i 's;${CMAKE_INSTALL_FULL_LIBDIR};/usr/lib;' CMakeLists.txt
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=/usr/lib ..
make
sudo make install
```
You might need to adjust `CMAKE_INSTALL_LIBDIR` for your system.

### macOS
Build flow is similar to that for Linux.

## See also

- Tools supporting VBAN
  - For Windows
  - [VB-Audio VoiceMeeter Banana](https://vb-audio.com/Voicemeeter/banana.htm)
  - For macOS
    - [VBAN Receptor](https://apps.apple.com/us/app/vban-receptor/id1462414931)
    - [VBAN Talkie Cherry](https://apps.apple.com/us/app/vban-talkie-cherry/id1553486090)
  - For iOS
    - [VBAN Receptor](https://apps.apple.com/us/app/vban-receptor/id1094354001)
    - [VBAN Talkie](https://apps.apple.com/us/app/vban-talkie/id1541587241)
  - For Android
    - [VBAN Receptor](https://play.google.com/store/apps/details?id=vbaudio.vbanreceptor)
    - [VBAN Talkie](https://play.google.com/store/apps/details?id=com.vbaudio.vbantalkie)
  - For Linux,
    - [vban](https://github.com/quiniouben/vban)
  - For developers
    - [Specification](https://vb-audio.com/Services/support.htm#VBAN)
- For better quality audio on OBS Studio
  - [Asynchronous Audio Filter](https://github.com/norihiro/obs-async-audio-filter) - If you hear a glitch, please consider using this plugin.
