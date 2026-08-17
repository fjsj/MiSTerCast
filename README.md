# MiSTerCast

MiSTerCast streams your Windows PC screen and loopback audio to the Groovy_MiSTer core. It is a general-purpose desktop caster, not a replacement for GroovyMAME or another emulator with integrated Groovy_MiSTer support.

A direct Ethernet connection is strongly recommended. If the MiSTer is also reachable over Wi-Fi, enter the Ethernet adapter's raw IPv4 address so a host name cannot select the slower route.

Useful Groovy_MiSTer setup references:

- https://github.com/lutechsource/MiSTerStuff/blob/main/GroovyMiSTer/mame_documentation.md
- https://github.com/psakhis/Groovy_MiSTer

## Requirements

- A MiSTer running the Groovy_MiSTer core
- Microsoft Visual C++ x86 Redistributable: https://aka.ms/vs/17/release/vc_redist.x86.exe
- Audio enabled in the core if you want sound
- A Windows display running near the output refresh rate, normally about 60 Hz

## Features

- GUI and command-line streaming tools
- Low-latency Windows Desktop Duplication capture and WASAPI loopback audio
- Point, Bilinear, and Line Blend scaling
- Stable interlaced field alignment after start and mode changes
- Optional full-height framebuffer mode for interlaced output
- Timestamped performance and transport diagnostics
- Safe stop/restart behavior and non-blocking host-name lookup

Direct-Ethernet frame-counter tests have measured the same frame as HDMI or one frame behind.

## Sampling

Choose a filter from the GUI's **Sampling** menu or with CLI `--sampling`:

| Mode | Best for |
| --- | --- |
| Point | Sharp, pixel-exact output. This is the default. |
| Bilinear | General smoothing in both directions. |
| Line Blend | Reducing vertical line shimmer while keeping horizontal pixels sharp. With 90° rotation, it blends source columns. |

Bilinear and Line Blend use more CPU than Point but do not buffer another frame. The filters are ported from [MiSTerCast-Linux](https://github.com/fjsj/MiSTerCast-Linux).

## Command-line diagnostics

`MiSTerCastCli.exe` is built beside the GUI and uses the same capture and streaming code. It requires a raw IPv4 target address.

List available modelines:

```powershell
FrontEnd\bin\Release\MiSTerCastCli.exe --list-modelines
```

Run a 15-second direct-Ethernet 480i test with a moving frame counter:

```powershell
FrontEnd\bin\Release\MiSTerCastCli.exe --target 192.168.200.2 --modeline "720x480i NTSC (60Hz)" --duration 15 --test-pattern --no-audio
```

Add `--sampling line-blend` to test line filtering, `--cycles 3` to repeat stop/start, or `--switch-modeline "640x480i NTSC (60Hz)"` to switch modes halfway through each cycle. Use `--help` for every option.

During a healthy 480i test, check that:

- output rate stays near 60 fields/s;
- phase changes from `local` to `locked` immediately after start or a mode switch;
- `dropped_video`, `dropped_audio`, and `transport_errors` remain zero;
- the moving counter is the same as HDMI or no more than one frame behind.

Logs are written under `%LOCALAPPDATA%\MiSTerCast\Logs`. The CLI can use another folder with `--log-directory`.

## Known limitations

- Wi-Fi can fall behind badly enough to make the MiSTer core unresponsive. Use direct Ethernet for gaming and recovery.
- The full-height interlaced framebuffer roughly doubles video bandwidth and is disabled by default.
- Nothing above 720x480i is currently recommended because of MiSTer-side throughput.
- A high-refresh Windows desktop is not supported well; match the desktop refresh to the output when possible.
- The bundled modelines are starting points. You can add modes to `modelines.dat`.

Build instructions, implementation notes, and the complete test procedure are in [AGENTS.md](AGENTS.md).
