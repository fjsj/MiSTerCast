#pragma once

#include "FrameTransform.h"

#ifdef MISTERCASTLIB_EXPORTS
#define MISTERCASTLIB_API extern "C" __declspec(dllexport)
#else
#define MISTERCASTLIB_API __declspec(dllimport)
#endif

void LogMessage(std::string message, bool error = false);

#define EXIT_ON_ERROR(hres, message)  \
              if (FAILED(hres)){\
                LogMessage(std::string(message) + ": " + std::to_string(hres), true);\
                return false;}
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

enum Alignment : int
{
    Center,
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left
};

enum CropMode : int
{
    Custom,
    X1,
    X2,
    X3,
    X4,
    X5,
    Full43,
    Full54,
};

struct SourceOptions {
    bool syncrefresh;
    UINT16 framedelay;
    UINT8 display;
    bool audio;
    bool preview;
    Alignment alignment;
    CropMode cropmode;
    UINT16 width;
    UINT16 height;
    INT16 xoffset;
    INT16 yoffset;
    Rotation rotation;
};

typedef void(__stdcall *log_function)(const char* message, bool error);

typedef void(__stdcall *capture_image_function)(int width, int height, void* buffer);

MISTERCASTLIB_API bool Initialize(log_function fnLog, capture_image_function fnCapture);

MISTERCASTLIB_API bool Shutdown();

MISTERCASTLIB_API bool StartStream(const char* targetIp);

MISTERCASTLIB_API bool StopStream();

MISTERCASTLIB_API bool SetDiagnosticFaults(
    UINT32 skipEvery,
    UINT32 stallEvery,
    UINT32 stallMilliseconds);

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
    bool interlace);

MISTERCASTLIB_API bool SetModelineEx(
    double pclock,
    UINT16 hactive,
    UINT16 hbegin,
    UINT16 hend,
    UINT16 htotal,
    UINT16 vactive,
    UINT16 vbegin,
    UINT16 vend,
    UINT16 vtotal,
    bool interlace,
    bool progressiveFramebuffer);

MISTERCASTLIB_API bool SetSource(
    UINT8 display,
    bool audio,
    bool preview,
    UINT8 alignment,
    UINT8 cropmode,
    UINT16 width,
    UINT16 height,
    INT16 xoffset,
    INT16 yoffset,
    UINT8 rotation);
