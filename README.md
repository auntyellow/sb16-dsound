# sb16-dsound

A DirectSound re-implementation for Sound Blaster 16 audio in QEMU, v86, and other emulators.

## Use case

This project is intended for older PC games and demos that use DirectSound, but can be redirected to a Sound Blaster 16-compatible audio path. It is especially useful in virtual machines and emulator setups where the host can expose SB16 hardware or a compatible software bridge.

The following demos were tested under [Windows NT 3.51](https://winworldpc.com/product/windows-nt-3x/351) using the [v86 emulator](https://github.com/copy/v86):

- [C&C Red Alert (RA95) Demo](https://www.chess-wizard.com/minigames/minigame_ra95demo.htm)
- [StarCraft Shareware](https://www.chess-wizard.com/minigames/minigame_scdemo.htm)

## Overview

**sb16-dsound** implements the `IDirectSound` and `IDirectSoundBuffer` interfaces over a custom `DirectSB16` kernel-mode component. It enables legacy DirectX games and audio applications to run in environments where native Sound Blaster 16 DirectSound drivers are missing or broken.

Emulators like QEMU and v86 can perfectly emulate Sound Blaster 16 hardware. However, legacy operating systems like Windows NT 3.51 only provide standard WaveOut drivers for SB16 — they lack native DirectSound acceleration drivers. As a result, many retro games and multimedia demos using DirectSound either crash or refuse to play audio. This project bridges that gap by providing a software-based DirectSound compatibility layer on top of the emulated SB16 stack.

## Supported platforms

- Windows 10 / 8 / 7 / Vista (32-bit only)
- Windows XP / 2000 / NT 4.0 / ReactOS
- Windows NT 3.51<sup> [2]</sup>
- Windows ME<sup> [3]</sup>
- Windows 98<sup> [4]</sup>
- Windows 95<sup> [2,4]</sup>

Notes:

1. DirectSB16 (`dsb16.sys`) is required for all NT-like OSes.
2. `msvcrt.dll` (from Windows NT 4.0) is required.
3. `IFSHLP.SYS` should be hacked to reserve 64K conventional memory as DMA buffer. Sometimes system's SB16 driver should be disabled.
4. `DSB16MEM.COM` should be loaded in `AUTOEXEC.BAT` to reserve 64K conventional memory as DMA buffer.

## How to use

- Download `sb16-dsound.7z/img/iso` from [latest release](https://github.com/auntyellow/sb16-dsound/releases/latest).
- Extract `sb16-dsound.7z` or mount `sb16-dsound.img/iso`.
- Unnecessary to disable system's SB16 driver in most cases.

### WinNT

- Under Windows 10 / 8 / 7 / Vista / XP / 2000 / ReactOS, run `install.bat` to install DirectSB16 (`dsb16.sys`).
- Under Windows NT 4.0 / 3.51, move `install.bat` into `nt351` folder and run it.
- Under Windows NT 3.51, copy `msvcrt.dll` from [Windows NT 4.0](https://winworldpc.com/product/windows-nt-40/40) into `system32` folder.
- Put `dsound.dll` into the same folder as the game executable file, or backup and overwrite system's `dsound.dll`.

### Win9x

- Under Windows Me, backup and override `IFSHLP.SYS` (because Windows ME can't load any other DOS drivers during startup, hacking `IFSHLP.SYS` is the only way to reserve DMA buffer), disable `StateMgr` in Startup tab in `msconfig` (to avoid restoring system's `DSOUND.DLL`), disable system's SB16 driver (it sometimes prevents DSP reset), then restart.
- Under Windows 98 / 95, Copy `DSB16MEM.COM` into `C:\`, append line `C:\DSB16MEM.COM` to `C:\AUTOEXEC.BAT`, then restart.
- Under Windows 95, copy `msvcrt.dll` from Windows NT 4.0 into `system` folder.
- Backup and overwrite system's `dsound.dll` (Win9x prefers system's DLL).

## Known Limitations

This project is currently a proof-of-concept and has the following limitations:

- **No Audio Mixing Support:** Simultaneous audio playback is not supported yet. Triggering a new sound will immediately interrupt and stop the currently playing sound.
- **No Ring Buffer Support:** Due to a strict 64KB buffer size constraint, audio must be downsampled to **8-bit, 11 kHz mono**. This limits the maximum duration of a single audio playback session to approximately **5.9 seconds**.
- **No Dynamic Buffer Updates After Playback:** Audio data written via `Lock()` and `Unlock()` *after* calling `Play()` will be ignored. The buffer must be completely filled before playback starts.

## How to Compile

### NT 5.x Driver

- Install [DDK 5.1.2600.1106](https://winworldpc.com/product/windows-sdk-ddk/xp-nt-51) into `C:\WINDDK\2600.1106`
- Run `makesys.bat`

### NT 4.0 / 3.51 Driver

- Install [MSVC 2.1](https://winworldpc.com/product/visual-c/2x) into `C:\MSVC20`
- Install [DDK 3.51.1057.1](https://winworldpc.com/product/windows-sdk-ddk/nt-3x) into `C:\DDK`
- Run `makesys_nt351.bat`

### DirectSound, Tests and Wrapper

- Install MSVC 6.0 (Visual Studio 6.0) or MinGW (`sudo apt install gcc-mingw-w64-i686`)
- Run `makefile.bat` or `makefile.sh` to compile DirectSound
- Run `maketest.bat` or `maketest.sh` to compile Tests
- Run `makefile.bat` or `makefile.sh` under `wrapper` folder to compile Wrapper

## dsound-wrapper

[dsound-wrapper](wrapper) is a lightweight DirectSound interceptor that forwards DirectSound calls to the real `dsound.dll` from the Windows system directory. It is useful for debugging DirectSound usage, tracing API calls, and modifying or replacing DirectSound behavior in legacy Windows games.