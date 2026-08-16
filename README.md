# MiSTerCast
A general-purpose tool for streaming your Windows PC screen to your MiSTer through the Groovy_MiSTer core.

This is not a replacement for Groovy_Mame or other integrated emulators.	

Make sure you already have Groovy_Mame working well with Groovy_MiSTer before using MiSTerCast. A direct ethernet connection to your MiSTer is recommended.
https://github.com/lutechsource/MiSTerStuff/blob/main/GroovyMiSTer/mame_documentation.md
https://github.com/psakhis/Groovy_MiSTer

The Microsoft VC++ x86 Redistributable is required. You can install it from here: https://aka.ms/vs/17/release/vc_redist.x86.exe

For audio, you will need to enable audio on the Groovy_MiSTer core.
MiSTerCast converts the default Windows loopback endpoint's 32-bit floating-point mix to stereo signed 16-bit PCM. Mono endpoints are duplicated to stereo; for multichannel endpoints, the front-left and front-right channels are used.
The Windows sender intentionally forwards the newest audio accumulated during each render cycle instead of adding the Linux PulseAudio prebuffer, preserving the lower-latency WASAPI path for gaming. A long-stall backlog is capped to the newest protocol-safe block.

Frame-delay requests are sent to the MiSTer core and raster pacing is calculated from the active modeline. Automatic interlaced output limits the requested sync line to the first half of the raster so an alternating field upload cannot race the field being displayed. Manual frame-delay values remain explicit and are not capped this way.

MiSTer status packets are decoded explicitly as little-endian protocol data. Status ordering follows the wrapping 32-bit frame counter, so a long-running stream continues to accept acknowledgements when the counter rolls over.

Each run writes a timestamped diagnostic log under `%LOCALAPPDATA%\MiSTerCast\Logs`. While streaming, one `[stream]` sample per second records capture and output cadence, maximum transform and send/wait time, commanded frame/field, MiSTer frame/F1 status, field repeats, frame-counter realignments, and protocol readiness flags. The GUI shows only the latest telemetry sample so long sessions do not continually grow the log panel. These diagnostics do not add extra network waits or change field selection.

## Building from source

The supported build uses Visual Studio 2022 or Visual Studio 2022 Build Tools with:

- Desktop development with C++
- MSVC v143
- A Windows 10 or Windows 11 SDK
- MSBuild
- The .NET Framework 4.7.2 targeting pack

Download LZ4 1.9.4 from https://github.com/lz4/lz4/releases/download/v1.9.4/lz4_win32_v1_9_4.zip and extract it to `External/lz4`. The Win32 build links the package's import library and copies `msys-lz4-1.dll` beside `MiSTerCast.exe` automatically.

From a Visual Studio developer shell, build the release application with:

```powershell
msbuild FrontEnd\MiSTerCast.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x86
```

Run the native regression tests after building:

```powershell
Tests\Release\MiSTerCastTests.exe
```

## Command-line hardware diagnostics

`MiSTerCastCli.exe` is built beside the GUI and runs the same Windows capture, audio, frame transform, and Groovy_MiSTer transport code for a fixed duration. It requires a raw IPv4 address so a diagnostic run cannot silently select Wi-Fi through host-name resolution.

List the modeline presets:

```powershell
FrontEnd\bin\Release\MiSTerCastCli.exe --list-modelines
```

Run a 15-second direct-Ethernet interlaced test:

```powershell
FrontEnd\bin\Release\MiSTerCastCli.exe --target 192.168.200.2 --modeline "640x480i NTSC (60Hz)" --duration 15
```

Use `--no-audio` to isolate video throughput, or `--help` for all options. The command prints timestamped diagnostics and writes the same output under `%LOCALAPPDATA%\MiSTerCast\Logs`.

## Known issues
- Frames may be dropped or doubled due to sync with video signal.
- At least 1-2 frames of latency.
- If the app crashes, you will need to restart your MiSTer and Groovy_MiSTer.
- Nothing over 720x480i is recommended at the moment due to throughput on MiSTer. This will improve soon.
- High refresh rate monitors are not supported due to frame times. Please change your monitor to ~60hz.

## Notes
The current pre-defined modelines are just for testing. You can add your own in modelines.dat.
It's best to use a refresh that matches your PC for better sync.
Find more modeline examples here: https://www.geocities.ws/podernixie/htpc/modes-en.html
