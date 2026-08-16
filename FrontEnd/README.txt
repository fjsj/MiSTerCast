<Bold>MiSTerCast 1.00</Bold>

MiSTerCast is a general-purpose tool for streaming your PC screen to your MiSTer through the Groovy_MiSTer core. This is not a replacement for Groovy_Mame or other integrated emulators.	

Make sure you already have Groovy_Mame working well with Groovy_MiSTer before using MiSTerCast. A direct Ethernet connection to your MiSTer is recommended. If the MiSTer is also on Wi-Fi, enter the raw IPv4 address of its Ethernet adapter so a host name cannot select the slower path.
<Hyperlink>https://github.com/lutechsource/MiSTerStuff/blob/main/GroovyMiSTer/mame_documentation.md</Hyperlink>
<Hyperlink>https://github.com/psakhis/Groovy_MiSTer</Hyperlink>

For audio, you will need to enable audio on the Groovy_MiSTer core.

<Bold>Known issues</Bold>
- Frames may be dropped or doubled due to sync with video signal.
- Latency depends on the capture and network path; direct-Ethernet frame-counter tests have measured the same frame as HDMI or one frame behind.
- Wi-Fi may drop complete video or audio batches when it cannot keep pace. Direct Ethernet is recommended for stable low-latency streaming.
- The Windows sender keeps its RIO/FPGA-acknowledgement pacing and does not add the Linux sender's UDP rate shaper or audio prebuffer, which could increase gaming latency.
- Interlaced field selection retains the original Windows formula based on the MiSTer's F1 and frame counters. Linux's additional mode-switch phase recovery was prototyped and reverted, but its negative test was confounded by Wi-Fi; it still needs a controlled direct-Ethernet A/B test (issue #9).
- Nothing over 720x480i is recommended at the moment due to throughput on MiSTer. This will improve soon.
- High refresh rate monitors are not supported due to frame times. Please change your monitor to ~60hz.

<Bold>Notes</Bold>
The current pre-defined modelines are just for testing. You can add your own in modelines.dat.
It's best to use a refresh that matches your PC for better sync.
Find more modeline examples here: <Hyperlink>https://www.geocities.ws/podernixie/htpc/modes-en.html</Hyperlink>

<Bold>Contact</Bold>
You can find me on the official MiSTer Discord
