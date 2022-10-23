# OBS VBAN Plugin

## Introduction

This plugin provides audio source from VBAN, an audio over UDP protocol.

## Properties

### IP address from

### Name

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
