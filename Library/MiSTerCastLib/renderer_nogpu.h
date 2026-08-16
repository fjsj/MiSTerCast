// Groovy_MiSTer communication adapted from the Groovy_Mame source.
// See https://github.com/antonioginer/GroovyMAME for original source

// Original license:
// license:BSD-3-Clause
// copyright-holders:Aaron Giles, Antonio Giner, Sergi Clara

// Modification by Shane Lynch

#pragma once

#include "AudioProcessing.h"
#include "StreamingTiming.h"

uint64_t CurrentTicks()
{
    // use the standard library clock function
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);
    return ticks.QuadPart;
}

uint64_t TicksPerSecond() noexcept
{
    LARGE_INTEGER val;
    QueryPerformanceFrequency(&val);
    return val.QuadPart;
}

inline double get_ms(uint64_t ticks) { return (double)ticks / TicksPerSecond() * 1000; };

// nogpu UDP server
#define UDP_PORT 32100

#pragma pack(1)

typedef struct nogpu_modeline
{
    double    pclock;
    uint16_t  hactive;
    uint16_t  hbegin;
    uint16_t  hend;
    uint16_t  htotal;
    uint16_t  vactive;
    uint16_t  vbegin;
    uint16_t  vend;
    uint16_t  vtotal;
    bool  interlace;
} nogpu_modeline;

std::atomic_bool shouldUpdateVideoMode = false;
nogpu_modeline selected_modeline = {};

#pragma pack()

// renderer_nogpu is the information for the current screen
class renderer_nogpu
{
public:
    renderer_nogpu(std::string targetip)
        : m_targetip(targetip)
    {
    }

    ~renderer_nogpu();
    int create();
    void draw();
    void save() {}
    void record() {}
    void toggle_fsfx() {}

private:
    // npgpu private members
    GroovyMister groovyMister;
    bool m_initialized = false;
    bool m_first_blit = true;
    int m_compression = 0;
    uint32_t m_frame = 0;
    uint8_t m_field = 0;
    unsigned int m_width = 0;
    unsigned int m_height = 0;
    int m_vtotal = 0;
    int m_vsync_scanline = 0;
    double m_period = 16.666667;
    double m_line_period = 0.064;
    double m_frame_delay = 0.0;
    double m_fd_margin = 1.5;
    nogpu_modeline m_current_mode;

    uint64_t time_start = 0;
    uint64_t time_entry = 0;
    uint64_t time_blit = 0;
    uint64_t time_exit = 0;
    uint64_t time_frame[16];
    uint64_t time_frame_avg = 0;
    uint64_t time_frame_dm = 0;

    int m_sockfd = -1; //INVALID_SOCKET;
    sockaddr_in m_server_addr;
    std::string m_targetip;

    bool nogpu_init();
    bool nogpu_switch_video_mode();
    void nogpu_register_frametime(uint64_t frametime);
};

//============================================================
//  renderer_nogpu::create
//============================================================

int renderer_nogpu::create()
{
    return 0;
}

//============================================================
//  renderer_nogpu::~renderer_nogpu
//============================================================

renderer_nogpu::~renderer_nogpu()
{
    // Wait for fpga to flush last blit
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(m_period));

    LogMessage("Sending CMD_CLOSE...");
    groovyMister.CmdClose();
}

//============================================================
//  renderer_nogpu::draw
//============================================================
void renderer_nogpu::draw()
{
    // Hack because these aren't intiailized...
    m_width = selected_modeline.hactive;
    m_height = selected_modeline.interlace ? selected_modeline.vactive / 2 : selected_modeline.vactive;

    // resize window if required
    static int old_width = 0;
    static int old_height = 0;
    if (old_width != m_width || old_height != m_height)
    {
        old_width = m_width;
        old_height = m_height;
    }

    // initialize nogpu right before first blit
    if (m_first_blit && !m_initialized)
    {
        m_initialized = nogpu_init();
        if (m_initialized)
        {
            LogMessage("Done.");
            nogpu_switch_video_mode();
        }
        else
        {
            m_first_blit = false;
        }
    }

    // only send frame if nogpu is initialized
    if (!m_initialized)
        return;

    // Reset the transport phase before selecting and sampling the first field
    // of a new mode. Selecting a field before CMD_SWITCHRES would use stale
    // feedback from the previous raster for that first upload.
    if (shouldUpdateVideoMode)
        nogpu_switch_video_mode();

    m_frame++;

    // Use only post-mode-switch FPGA feedback as authoritative field phase.
    // This re-locks after skipped frames without adopting stale pre-switch state.
    groovyMister.AlignFrame(m_frame, m_field);

    unsigned int drawIndex = lastVideoCaptureIndex;
    int screenwidth = videoCaptures[drawIndex].width;
    int screenheight = videoCaptures[drawIndex].height;
    char* fb = groovyMister.getPBufferBlit(m_field);
    const size_t outputSize = Rgb24FrameSize(m_width, m_current_mode.vactive, m_current_mode.interlace);
    if (!TransformBgraToRgb24(
        videoCaptures[drawIndex].buffer.data(),
        screenwidth,
        screenheight,
        screenwidth * 4,
        m_width,
        m_current_mode.vactive,
        m_current_mode.interlace,
        static_cast<uint8_t>(m_field),
        source_config.rotation,
        reinterpret_cast<uint8_t*>(fb),
        outputSize))
    {
        LogMessage("Unable to transform the captured frame.", true);
        return;
    }

    bool valid_status = true;

    time_entry = CurrentTicks();

    if (source_config.syncrefresh && m_first_blit)
    {
        time_start = time_entry;
        time_blit = time_entry;
        time_exit = time_entry;

        m_first_blit = false;
        m_frame = 0;

        // Skip blitting first frame, so we avoid glitches while MAME loads roms
        return;
    }

    int vsync_offset = 0;

    if (source_config.framedelay == 0)
        // automatic
        m_frame_delay = std::max((double)(m_period - std::max(m_fd_margin, get_ms(time_frame_dm))) / m_period, 0.0);
    else
    {
        // user defined
        m_frame_delay = (double)(source_config.framedelay) / 10.0;
        vsync_offset = 0;// window().machine().video().vsync_offset();
    }

    // Capture and send audio
    if (source_config.audio)
    {
        TickAudioCapture();
        if (groovyMister.fpga.audio && AudioWritePos > 0)
            groovyMister.CmdAudio(static_cast<uint16_t>(AudioWritePos * sizeof(int16_t)));
    }

    // Update vsync scanline
    m_vsync_scanline = mistercast::RequestedSyncLine(
        m_current_mode.vtotal,
        m_frame_delay + (m_current_mode.vtotal == 0 ? 0.0 : (double)vsync_offset / m_current_mode.vtotal),
        m_current_mode.interlace,
        source_config.framedelay == 0);

    // Blit now
    groovyMister.CmdBlit(m_frame, m_field, static_cast<uint16_t>(m_vsync_scanline), 15000, 0);
    groovyMister.WaitSync();

    time_blit = CurrentTicks();
    nogpu_register_frametime(time_entry - time_exit);
    time_exit = CurrentTicks();

    return;
}

//============================================================
//  renderer_nogpu::nogpu_init
//============================================================

bool renderer_nogpu::nogpu_init()
{
    int result;

    m_compression = 0x01; // lz4 compression

    const uint32_t negotiatedAudioRate = source_config.audio ? audioSampleRate.load() : 0;
    switch (negotiatedAudioRate)
    {
    case 0:
        LogMessage("Audio disabled");
        break;
    case 22050:
        LogMessage("Audio Freq 22.05KHz");
        break;
    case 44100:
        LogMessage("Audio Freq 44.1KHz");
        break;
    case 48000:
        LogMessage("Audio Freq 48KHz");
        break;
    default:
        LogMessage("Unsupported audio sample rate. Only 48kHz, 44.1kHz and 22.05kHz are supported.", true);
        return false;
    }

    LogMessage("Sending CMD_INIT...");

    // Reset current mode
    m_current_mode = {};

    int ret = groovyMister.CmdInit(
        m_targetip.c_str(),
        UDP_PORT,
        m_compression,
        negotiatedAudioRate,
        source_config.audio ? 2 : 0,
        0,
        1500);
    if (ret == 0)
    {
        audioBuffer = reinterpret_cast<int16_t*>(groovyMister.getPBufferAudio());
        return true;
    }
    else
    {
        LogMessage("Groovy MiSTer API failed to initialize!");
        return false;
    }
}

//============================================================
//  renderer_nogpu::nogpu_switch_video_mode()
//============================================================

bool renderer_nogpu::nogpu_switch_video_mode()
{
    nogpu_modeline *mode = &selected_modeline;
    if (mode == nullptr)
        return false;

    m_current_mode = *mode;

    // Send new modeline to nogpu
    LogMessage("Sending CMD_SWITCHRES...");

    m_width = mode->hactive;
    m_height = mode->vactive;
    m_vtotal = mode->vtotal;
    m_field = 0;

    const double linePeriod = mistercast::LinePeriodMilliseconds(mode->pclock, mode->htotal);
    const double fieldPeriod = mistercast::FieldPeriodMilliseconds(
        mode->pclock, mode->htotal, mode->vtotal, mode->interlace);
    if (linePeriod > 0.0 && fieldPeriod > 0.0)
    {
        m_line_period = linePeriod;
        m_period = fieldPeriod;
    }

    shouldUpdateVideoMode = false;
    groovyMister.CmdSwitchres(
        mode->pclock,
        mode->hactive,
        mode->hbegin,
        mode->hend,
        mode->htotal,
        mode->vactive,
        mode->vbegin,
        mode->vend,
        mode->vtotal,
        mode->interlace
    );

    return true;
}

//============================================================
//  renderer_nogpu::nogpu_register_frametime
//============================================================

void renderer_nogpu::nogpu_register_frametime(uint64_t frametime)
{
    static int i = 0;
    static int regs = 0;
    const int max_regs = sizeof(time_frame) / sizeof(time_frame[0]);
    uint64_t acum = 0;
    uint64_t diff = 0;

    // Discard invalid values
    if (frametime <= 0 || get_ms(frametime) > m_period)
        return;

    // Register value and compute current average
    time_frame[i] = frametime;
    i++;

    if (i >= max_regs)
        i = 0;

    if (regs < max_regs)
        regs++;

    for (int k = 0; k < regs; k++)
        acum += time_frame[k];

    time_frame_avg = acum / regs;

    // Compute current max deviation
    uint64_t max_diff = 0;

    for (int k = 1; k < regs; k++)
    {
        diff = time_frame[k] >= time_frame[k - 1]
            ? time_frame[k] - time_frame[k - 1]
            : time_frame[k - 1] - time_frame[k];

        if (diff > 0 && diff > max_diff)
            max_diff = diff;
    }

    time_frame_dm = max_diff;
}
