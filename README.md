<h1 align="center">
<img align="center" src="https://cdn.cloudflare.steamstatic.com/steam/apps/322170/header.jpg" width="45%"><br>
Geometry Dash · PS Vita Port
</h1>

### Jump and fly your way through danger in this rhythm-based action platformer!
#### Prepare for a near impossible challenge in the world of Geometry Dash. Push your skills to the limit as you jump, fly and flip your way through dangerous passages and spiky obstacles.

<p align="center">
  <a href="#about">About</a> •
  <a href="#set-up-for-end-users">Set-Up (For End-Users)</a> •
  <a href="#controls">Controls</a> •
  <a href="#set-up-for-developers">How To Compile</a> •
  <a href="#credits">Credits</a> •
  <a href="#license">License</a>
</p>


> **This fork: online features & custom-song playback.**  
> Compared to upstream, this fork makes the online part of the game work on real hardware (profile, featured/search, level download, song download from the servers) and plays custom songs inside levels. Downloaded songs are stored in `ux0:data/gdash/songs/<id>.mp3` (the folder is created automatically). The vitaGL boot splash has also been removed. See [What changed](#what-changed-in-this-fork) for the technical details.
>
> Claude was used to help during this project as i'm definitely not a developer.

# About

Geometry Dash is a side-scrolling music platforming game series developed by RobTop. The game is known for it's challenging levels and legacy, garnering millions of players and a passionate fanbase making user levels to this day.

This repository contains a loader of the *Android release* of Geometry Dash, based on the Android .so Loader by TheFloW. The loader provides a tailored, minimalistic Android-like environment to run the official ARMv7 game executables on the PS Vita.

***This software does not contain the original code, executables, assets, or other not redistributable parts of the game. The authors do not promote or condone piracy in any way. To launch and play the game on their PS Vita device, users must provide their own legally obtained copy of the game in form of an .apk file.***

# Set-Up (for End-Users)

In order to properly install the game, you'll have to follow these steps precisely. 

## Please note that only version 2.2.13  has been tested as of 24/01/24.

- Install [kubridge](https://github.com/TheOfficialFloW/kubridge/releases/) and [FdFix](https://github.com/TheOfficialFloW/FdFix/releases/) by copying `kubridge.skprx` and `fd_fix.skprx` to your taiHEN plugins folder (usually `ur0:tai`) and adding two entries to your `config.txt` under `*KERNEL`:
  
```
  *KERNEL
  ur0:tai/kubridge.skprx
  ur0:tai/fd_fix.skprx
```

**Note**: don't install fd_fix.skprx if you're using rePatch plugin!

- Make sure you have `libshacccg.suprx` in the `ur0:/data/` folder on your console. If you don't, follow [this guide](https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx) to extract it.
- Also make sure that you have `libfmodstudio.suprx`, `libc.suprx` and `libfios2.suprx` in the same folder. If you don't, follow [this guide](https://gist.github.com/hatoving/99253e1b3efdefeaf0ca66e0c5dc7089) to extract those files.
- <u>Legally</u> obtain your copy of [Geometry Dash](https://play.google.com/store/apps/details?id=com.robtopx.geometryjump&hl=en&gl=US)
for Android in form of an `.apk` file. [You can get all the required files directly from your phone](https://stackoverflow.com/questions/11012976/how-do-i-get-the-apk-of-an-installed-app-without-root-access) or by using any APK extractor you can find on Google Play.
- Open the `.apk` with any zip explorer (like [7-Zip](https://www.7-zip.org/)) and extract every single audio file and folders `sfx` and `songs` from the `.apk` into `ux0:data/gdash/assets`. Example of resulting path: `ux0:data/gdash/assets/songs/10000104.ogg`, `ux0:data/gdash/assets/menuLoop.mp3`.
- Obtain the `.so` file called `libcocos2dcpp.so` from the `.apk` and place it in `ux0:data/gdash/`.
- Place the `.apk` file in `ux0:data/gdash/` and rename as `GeometryDash.apk`.
- Install `gdash.vpk` (from [Releases](https://github.com/YipWig/gdash-vita/releases/latest)).

# Controls

The game uses only button to operate; which means you can entirely play this with the touchscreen.

However, there's gamepad support to emulate some controls:

|             Button             | Action                      |
|:------------------------------:|:---------------------------:|
| ![dpadh]                       | Move Player (The Tower)     |
|            ![cross]            | Jump                        |
|      ![circl]/![start]                  | Back/Pause                  |

[cross]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/cross.svg "Cross"
[circl]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/circle.svg "Circle"
[dpadh]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/dpad-left-right.svg "D-Pad Left/Right"
[start]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/dpad-start.svg "Start"

# Set-Up (for Developers)

In order to build the loader, you'll need a [vitasdk](https://github.com/vitasdk) build fully compiled with softfp usage.  
You can find a precompiled version [here](https://github.com/vitasdk/buildscripts/releases).  

Additionally, you'll need vitaGl to be compiled with these flags: ``make HAVE_GLSL_SUPPORT=1 SOFTFP_ABI=1 NO_DEBUG=1 install``.

You also have to install FMOD onto your VitaSDK enviroment. Info on how to acquire the stubs needed will need to be taken care of by yourself; `fmodpp` can be installed via the already-available precompiled library file found [here](https://github.com/Rinnegatamante/fmodpp/tree/master/build_sfp).

This fork looks for FMOD in a local `vitasdk_fmod_include/` directory instead of the global VitaSDK tree (it is added to the include and link paths by `CMakeLists.txt`):

```
vitasdk_fmod_include/
  fmod/                  <- the FMOD Studio API headers (fmod.h, fmod_common.h, ...), from the FMOD SDK (not redistributed here)
  libfmodpp.a            <- Rinnegatamante's fmodpp (softfp build)
  libfmodstudio_stub.a   <- import stub generated from libfmodstudio.suprx with vita-libs-gen
```

Only the FMOD headers are missing from the repo for licensing reasons: copy them from the FMOD Engine SDK (`api/core/inc` and `api/studio/inc`) into `vitasdk_fmod_include/fmod/`.

Diagnostic tracing (files written to `ux0:data/gdash/*_trace.txt`) is compiled out by default; enable it with `cmake -DGDASH_TRACE=ON`.

After all these requirements are met, you can compile the loader with the following commands:

```bash
cmake -Bbuild .
cmake --build build
```

Also note that this CMakeLists has two "convenience targets". While developing, I highly recommed using them, like this:
```bash
cmake --build build --target send # Build, upload eboot.bin and run (requires vitacompanion)
cmake --build build --target dump # Fetch latest coredump and parse
```

For more information and build options, read the [CMakeLists.txt](CMakeLists.txt).

#
# What changed in this fork

- **OpenSSL**: the `.so` ships its own statically-linked OpenSSL 1.1.x. Upstream redirected many of its `EVP_*` / `CRYPTO_*` / `ERR_*` entry points into vitasdk's separate libcrypto, which left the game's OpenSSL with an empty cipher/digest table ("library has no ciphers", "x509 verification setup problems"). Only the allocator, `OPENSSL_cleanse`, `CRYPTO_memcmp` and `CRYPTO_atomic_add` (which otherwise uses the Linux kuser helper at `0xffff0fc0`) are hooked now (`source/openssl_patch.c`).
- **libc bridge**: a failed `fopen` left `errno == 0`, so OpenSSL treated the missing `openssl.cnf` as a fatal config error instead of "file not found" (`source/reimpl/io.c`).
- **bionic ↔ Vita socket ABI**: `AF_INET6` (10 vs 28) made `inet_pton` fail so curl never sent SNI and Cloudflare aborted the TLS handshake; bionic `MSG_NOSIGNAL` (0x4000) passed to Vita `send()` failed with errno 106 on song downloads. Both are translated in `source/dynlib.c` / `source/reimpl/sockopt.c` / `sockaddr_abi.c`.
- **Song storage**: `getCocos2dxWritablePath` is implemented (`source/falso_jni_impl.c`) and every file API translates the Android data prefix to `ux0:data/gdash/`, so songs land in `ux0:data/gdash/songs/<id>.mp3` and are found again on the next launch.
- **Music in levels**: the game drives FMOD through its C++ API, so those symbols are redirected to wrappers in `source/main.c`. Level music starts with a 2 s fade-in via `Channel::addFadePoint`, which never raises the volume on the Vita FMOD build, so fade points are ignored; `setPosition` is clamped (negative music offsets, positions past the end).
- **Chest / daily timers**: bionic clock ids are translated (`CLOCK_REALTIME` is 0 on Android but 1 on the Vita, so `clock_gettime(0)` failed and the timers showed "24092 days").
- **Misc**: keyboard input goes through the Android text-input JNI flow (`nativeInsertText`/`nativeTextClosed`), `ioctl(FIONBIO/FIONREAD)` and a few pthread/process stubs are implemented, and the vitaGL boot splash is disabled (`source/utils/nosplash.c`).

# Credits
- [Andy "The FloW" Nguyen](https://github.com/TheOfficialFloW/) for the original .so loader.
- [Rinnegatamante](https://github.com/Rinnegatamante/) and [gl33ntwine](https://github.com/v-atamanenko/) for helping me a ton with the port (+ for vitaGL by Rinnegatamante.)
- [CatoTheYounger97](https://github.com/CatoTheYounger97/), [Dexxtrip](https://www.reddit.com/user/Dexxtrip/) and [withLogic](https://github.com/withLogic/) for testing the game out.

Style of the page has been taken from https://github.com/v-atamanenko/baba-is-you-vita.
