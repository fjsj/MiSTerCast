#include "pch.h"
#include "MiSTerCastLib.h"
#include "AudioCapture.h"
#include "VideoCapture.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Winmm.lib")

log_function logFunction = nullptr;
void LogMessage(std::string message, bool error)
{
    if (logFunction != nullptr)
        logFunction(message.c_str(), error);
}

std::atomic_bool stopCapture = false;
std::atomic_bool stopStream = false;
std::string targetIpString;
std::unique_ptr<std::thread> captureScreenTask;
std::unique_ptr<std::thread> castScreenTask;
std::mutex captureTaskMutex;
std::mutex castTaskMutex;

#include "groovymister.h"
#include "renderer_nogpu.h"

std::atomic_bool capturing_screen = false;
void capture_screen()
{
    LogMessage("Screen capture starting.");
    try
    {
        do
        {
            TickVideoCapture();
        } while (!stopCapture.load(std::memory_order_acquire));
    }
    catch (const std::exception& exception)
    {
        LogMessage("Screen capture stopped after an unexpected error: " + std::string(exception.what()), true);
    }
    catch (...)
    {
        LogMessage("Screen capture stopped after an unexpected error.", true);
    }
    capturing_screen = false;
    LogMessage("Screen capture stopped.");
}

bool start_capture_worker()
{
    std::lock_guard<std::mutex> lock(captureTaskMutex);
    if (captureScreenTask != nullptr)
    {
        LogMessage("Screen capture is already running.", true);
        return false;
    }

    stopCapture.store(false, std::memory_order_release);
    capturing_screen.store(true, std::memory_order_release);
    try
    {
        captureScreenTask = std::make_unique<std::thread>(capture_screen);
        return true;
    }
    catch (const std::exception& exception)
    {
        capturing_screen.store(false, std::memory_order_release);
        LogMessage("Starting screen capture failed: " + std::string(exception.what()), true);
        return false;
    }
}

void stop_capture_worker()
{
    std::lock_guard<std::mutex> lock(captureTaskMutex);
    stopCapture.store(true, std::memory_order_release);
    if (captureScreenTask != nullptr)
    {
        if (captureScreenTask->joinable())
            captureScreenTask->join();
        captureScreenTask.reset();
    }
    capturing_screen.store(false, std::memory_order_release);
    stopCapture.store(false, std::memory_order_release);
}

std::atomic_bool casting_screen = false;
void cast_screen()
{
    const SourceOptions streamSource = source_config.snapshot();
    bool audioStarted = false;

    try
    {
        if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST))
        {
            LogMessage("Setting cast screen thread priority failed: " + std::to_string(GetLastError()), true);
        }

        if (streamSource.audio)
        {
            LogMessage("Audio capture starting.");
            audioStarted = StartAudioCapture();
        }

        LogMessage("Casting to MiSTer starting.");
        {
            auto renderer = std::make_unique<renderer_nogpu>(targetIpString, audioStarted);
            {
                do
                {
                    renderer->draw();
                } while (!stopStream.load(std::memory_order_acquire));
            }
        }
    }
    catch (const std::exception& exception)
    {
        LogMessage("Casting stopped after an unexpected error: " + std::string(exception.what()), true);
    }
    catch (...)
    {
        LogMessage("Casting stopped after an unexpected error.", true);
    }

    LogMessage("Casting to MiSTer stopped.");

    if (audioStarted)
    {
        StopAudioCapture();
        LogMessage("Audio capture stopped.");
    }
    casting_screen.store(false, std::memory_order_release);
}

bool initialized = false;
MISTERCASTLIB_API bool Initialize(log_function fnLog, capture_image_function fnCapture)
{
    if (initialized)
    {
        LogMessage("MiSTerCast is already initialized.", true);
        return true;
    }

    logFunction = fnLog;
    LogMessage("Initializing MiSTerCast");

    SourceOptions initialSource = source_config.snapshot();
    initialSource.syncrefresh = true;
    initialSource.framedelay = 0;
    initialSource.alignment = Alignment::Center;
    initialSource.cropmode = CropMode::Full43;
    source_config.publish(initialSource);

    {
        std::lock_guard<std::mutex> lock(selected_modeline_mutex);
        selected_modeline.pclock = 6.700;
        selected_modeline.hactive = 320;
        selected_modeline.hbegin = 336;
        selected_modeline.hend = 367;
        selected_modeline.htotal = 426;
        selected_modeline.vactive = 240;
        selected_modeline.vbegin = 244;
        selected_modeline.vend = 247;
        selected_modeline.vtotal = 262;
        selected_modeline.interlace = 0;
    }
    

    if (!InitializeVideoCapture(0, fnCapture))
    {
        LogMessage("Failed to initialize video capture.", true);
        CleanupVideoCapture();
        return false;
    }

    if (!InitAudioCapture())
    {
        LogMessage("Failed to initialize audio capture.", true);
        CleanupAudioCapture();
        CleanupVideoCapture();
        return false;
    }

    // Fill the buffers to be safe
    for (int i = 0; i < BUFFER_COUNT; i++)
        TickVideoCapture();

    if (!start_capture_worker())
    {
        CleanupAudioCapture();
        CleanupVideoCapture();
        return false;
    }

    LogMessage("MiSTerCast ready.");

    initialized = true;
    return true;
}

MISTERCASTLIB_API bool Shutdown()
{
    StopStream();
    stop_capture_worker();

    CleanupVideoCapture();
    CleanupAudioCapture();
    delete[] videoCaptures;
    videoCaptures = nullptr;
    initialized = false;

    return true;
}

MISTERCASTLIB_API bool StartStream(const char* targetIp)
{
    std::lock_guard<std::mutex> lock(castTaskMutex);
    if (!initialized)
    {
        LogMessage("MiSTerCast must be initialized before starting a stream.", true);
        return false;
    }
    if (targetIp == nullptr || targetIp[0] == '\0')
    {
        LogMessage("A target IPv4 address is required.", true);
        return false;
    }
    if (castScreenTask != nullptr)
    {
        LogMessage("A stream is already running. Stop it before starting another.", true);
        return false;
    }

    targetIpString = std::string(targetIp);
    LogMessage("Starting stream to " + targetIpString + ".");
    stopStream.store(false, std::memory_order_release);
    casting_screen.store(true, std::memory_order_release);
    try
    {
        castScreenTask = std::make_unique<std::thread>(cast_screen);
    }
    catch (const std::exception& exception)
    {
        casting_screen.store(false, std::memory_order_release);
        LogMessage("Starting the stream worker failed: " + std::string(exception.what()), true);
        return false;
    }

    return true;
}

MISTERCASTLIB_API bool StopStream()
{
    std::lock_guard<std::mutex> lock(castTaskMutex);
    stopStream.store(true, std::memory_order_release);
    if (castScreenTask != nullptr)
    {
        if (castScreenTask->joinable())
            castScreenTask->join();
        castScreenTask.reset();
    }

    casting_screen.store(false, std::memory_order_release);
    stopStream.store(false, std::memory_order_release);
    return true;
}

MISTERCASTLIB_API bool SetModeline(
    double pclock,
    UINT16 hactive,
    UINT16 hbegin,
    UINT16 hend,
    UINT16 htotal,
    UINT16 vactive,
    UINT16 vbegin,
    UINT16 vend,
    UINT16 vtotal,
    bool interlace)
{
    LogMessage("SetModeline called");
    {
        std::lock_guard<std::mutex> lock(selected_modeline_mutex);
        selected_modeline.pclock = pclock;
        selected_modeline.hactive = hactive;
        selected_modeline.hbegin = hbegin;
        selected_modeline.hend = hend;
        selected_modeline.htotal = htotal;
        selected_modeline.vactive = vactive;
        selected_modeline.vbegin = vbegin;
        selected_modeline.vend = vend;
        selected_modeline.vtotal = vtotal;
        selected_modeline.interlace = interlace;
    }

    shouldUpdateVideoMode.store(true, std::memory_order_release);

    return true;
}

MISTERCASTLIB_API bool SetSource(
    UINT8 display,
    bool audio,
    bool preview,
    UINT8 alignment,
    UINT8 cropmode,
    UINT16 xcrop,
    UINT16 ycrop,
    INT16 xoffset,
    INT16 yoffset,
    UINT8 rotation)
{
    const nogpu_modeline modeline = selected_modeline_snapshot();
    const SourceOptions previousSource = source_config.snapshot();
    SourceOptions source = previousSource;
    source.display = display;
    source.audio = audio;
    source.preview = preview;
    source.alignment = (Alignment)alignment;
    source.cropmode = (CropMode)cropmode;
    source.width = xcrop;
    source.height = ycrop;
    source.xoffset = xoffset;
    source.yoffset = yoffset;
    source.rotation = static_cast<Rotation>(rotation);

    switch (cropmode)
    {
    case CropMode::X1:
        source.width = modeline.hactive;
        source.height = modeline.vactive;
        break;
    case CropMode::X2:
        source.width = modeline.hactive * 2;
        source.height = modeline.vactive * 2;
        break;
    case CropMode::X3:
        source.width = modeline.hactive * 3;
        source.height = modeline.vactive * 3;
        break;
    case CropMode::X4:
        source.width = modeline.hactive * 4;
        source.height = modeline.vactive * 4;
        break;
    case CropMode::X5:
        source.width = modeline.hactive * 5;
        source.height = modeline.vactive * 5;
        break;
    default:
        break;
    }

    if (displayIndex != source.display)
    {
        stop_capture_worker();
        CleanupVideoCapture();
        source_config.publish(source);
        if (!InitializeVideoCapture(source.display, captureFunction))
        {
            LogMessage("Failed to initialize the selected video capture source.", true);
            CleanupVideoCapture();
            source_config.publish(previousSource);
            if (InitializeVideoCapture(previousSource.display, captureFunction))
                start_capture_worker();
            else
                LogMessage("Failed to restore the previous video capture source.", true);
            return false;
        }
        if (!start_capture_worker())
            return false;
    }
    else
    {
        source_config.publish(source);
    }

    return true;
}
