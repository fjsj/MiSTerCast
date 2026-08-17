<Bold>MiSTerCast 1.02</Bold>

MiSTerCast streams your Windows PC screen and loopback audio to the Groovy_MiSTer core. It is not a replacement for GroovyMAME or another emulator with integrated Groovy_MiSTer support.

A direct Ethernet connection is strongly recommended. If the MiSTer is also on Wi-Fi, enter the raw IPv4 address of its Ethernet adapter.
<Hyperlink>https://github.com/lutechsource/MiSTerStuff/blob/main/GroovyMiSTer/mame_documentation.md</Hyperlink>
<Hyperlink>https://github.com/psakhis/Groovy_MiSTer</Hyperlink>

For sound, enable audio in the Groovy_MiSTer core.

<Bold>Features</Bold>
- Point is the sharp, pixel-exact sampling default.
- Bilinear smooths neighboring pixels in both directions.
- Line Blend reduces vertical CRT shimmer while preserving horizontal sharpness. With 90-degree rotation, it blends source columns.
- Full-height framebuffer mode is available for interlaced output, but roughly doubles video bandwidth.
- Diagnostic logs are stored under %LOCALAPPDATA%\MiSTerCast\Logs.
- MiSTerCastCli.exe provides repeatable frame-counter, mode-switch, stop/start, and sampling tests.

<Bold>Healthy direct-Ethernet test</Bold>
- Output stays near 60 fields per second for 480i.
- Phase changes from local to locked just after start or a mode switch.
- dropped_video, dropped_audio, and transport_errors stay at zero.
- The moving frame counter matches HDMI or is no more than one frame behind.

<Bold>Known limitations</Bold>
- Wi-Fi can overload the MiSTer-side receiver. Use direct Ethernet for stable, low-latency streaming.
- Nothing above 720x480i is currently recommended because of MiSTer-side throughput.
- High-refresh Windows desktops are not supported well. Match the desktop refresh to the output when possible.

<Bold>Notes</Bold>
The bundled modelines are starting points. You can add modes to modelines.dat.
Find more examples here: <Hyperlink>https://www.geocities.ws/podernixie/htpc/modes-en.html</Hyperlink>

<Bold>Contact</Bold>
You can find me on the official MiSTer Discord.
