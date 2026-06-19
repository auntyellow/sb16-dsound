# sb16-dsound

A DirectSound re-implementation for Sound Blaster 16 audio in QEMU, v86, and other emulators.

## Overview

`sb16-dsound` implements the `IDirectSound` and `IDirectSoundBuffer` interfaces over a custom `\\.\DirectSB16` kernel-mode component. It enables legacy DirectX games and audio applications to run in environments where native Sound Blaster 16 DirectSound drivers are missing or broken.

Emulators like QEMU and v86 can perfectly emulate Sound Blaster 16 hardware. However, legacy operating systems like Windows NT 3.51 or NT 4.0 only provide standard WaveOut drivers for SB16—they lack native DirectSound acceleration drivers. As a result, many retro games and multimedia demos using DirectSound either crash or refuse to play audio. This project bridges that gap by providing a software-based DirectSound compatibility layer on top of the emulated SB16 stack.

## Supported platforms

- Windows NT 4.0 / 2000 / XP / ReactOS
- Windows NT 3.51: msvcrt.dll (from Windows NT 4.0) is required

## Use case

This project is intended for older PC games and demos that use DirectSound, but can be redirected to a Sound Blaster 16-compatible audio path. It is especially useful in virtual machines and emulator setups where the host can expose SB16 hardware or a compatible software bridge.

The following demos were tested under [Windows NT 3.51](https://winworldpc.com/product/windows-nt-3x/351) using the [v86 emulator](https://github.com/copy/v86):

- [C&C Red Alert (RA95) Demo](https://www.chess-wizard.com/minigames/minigame_ra95demo.htm)
- [StarCraft Shareware](https://www.chess-wizard.com/minigames/minigame_scdemo.htm)

## Known Limitations

This project is currently a proof-of-concept and has the following limitations:

- **No Audio Mixing Support:** Simultaneous audio playback is not supported yet. Triggering a new sound will immediately interrupt and stop the currently playing sound.
- **No Ring Buffer Support:** Due to a strict 64KB buffer size constraint, audio must be downsampled to **8-bit, 11 kHz mono**. This limits the maximum duration of a single audio playback session to approximately **5.9 seconds**.
- **No Dynamic Buffer Updates After Playback:** Audio data written via `Lock()` and `Unlock()` *after* calling `Play()` will be ignored. The buffer must be completely filled before playback starts.

## How to Compile

### NT 5.x Driver

- Install [DDK 5.1.2600.1106](https://winworldpc.com/product/windows-sdk-ddk/xp-nt-51) into `C:\WINDDK\2600.1106`
- Run `makesys.bat`

### NT 3.51 / 4.0 Driver

- Install [MSVC 2.1](https://winworldpc.com/product/visual-c/2x) into `C:\MSVC20`
- Install [DDK 3.51.1057.1](https://winworldpc.com/product/windows-sdk-ddk/nt-3x) into `C:\DDK`
- Run `makesys_nt351.bat`

### DirectSound and Tests

- Install MSVC 6.0 (Visual Studio 6.0) or mingw32 (apt install gcc-mingw-w64-i686)
- Run `makefile.bat` or `makefile.sh` to compile DirectSound
- Run `maketest.bat` or `maketest.sh` to compile Tests